// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <string>

#include "gtest/gtest.h"
#include "mount_dev_mock.h"
#include "nacl_io/ioctl.h"
#include "nacl_io/kernel_intercept.h"
#include "nacl_io/kernel_proxy.h"
#include "nacl_io/mount.h"
#include "nacl_io/mount_dev.h"
#include "nacl_io/mount_mem.h"
#include "nacl_io/osdirent.h"

using namespace nacl_io;

namespace {

class TtyTest : public ::testing::Test {
 public:
  void SetUp() {
    ki_init(&kp_);
    ASSERT_EQ(0, mnt_.Access(Path("/tty"), R_OK | W_OK));
    ASSERT_EQ(EACCES, mnt_.Access(Path("/tty"), X_OK));
    ASSERT_EQ(0, mnt_.Open(Path("/tty"), O_RDWR, &dev_tty_));
    ASSERT_NE(NULL_NODE, dev_tty_.get());
  }

  void TearDown() {
    ki_uninit();
  }

 protected:
  KernelProxy kp_;
  MountDevMock mnt_;
  ScopedMountNode dev_tty_;
};

TEST_F(TtyTest, InvalidIoctl) {
  // 123 is not a valid ioctl request.
  EXPECT_EQ(EINVAL, dev_tty_->Ioctl(123));
}

TEST_F(TtyTest, TtyInput) {
  // Now let's try sending some data over.
  // First we create the message.
  std::string message("hello, how are you?\n");
  struct tioc_nacl_input_string packaged_message;
  packaged_message.length = message.size();
  packaged_message.buffer = message.data();

  // Now we make buffer we'll read into.
  // We fill the buffer and a backup buffer with arbitrary data
  // and compare them after reading to make sure read doesn't
  // clobber parts of the buffer it shouldn't.
  int bytes_read;
  char buffer[100];
  char backup_buffer[100];
  memset(buffer, 'a', 100);
  memset(backup_buffer, 'a', 100);

  // Now we actually send the data
  EXPECT_EQ(0, dev_tty_->Ioctl(TIOCNACLINPUT, &packaged_message));

  // We read a small chunk first to ensure it doesn't give us
  // more than we ask for.
  HandleAttr attrs;
  EXPECT_EQ(0, dev_tty_->Read(attrs, buffer, 5, &bytes_read));
  EXPECT_EQ(bytes_read, 5);
  EXPECT_EQ(0, memcmp(message.data(), buffer, 5));
  EXPECT_EQ(0, memcmp(buffer + 5, backup_buffer + 5, 95));

  // Now we ask for more data than is left in the tty, to ensure
  // it doesn't give us more than is there.
  EXPECT_EQ(0, dev_tty_->Read(attrs, buffer + 5, 95, &bytes_read));
  EXPECT_EQ(bytes_read, message.size() - 5);
  EXPECT_EQ(0, memcmp(message.data(), buffer, message.size()));
  EXPECT_EQ(0, memcmp(buffer + message.size(),
                      backup_buffer + message.size(),
                      100 - message.size()));
}

struct user_data_t {
  const char* output_buf;
  size_t output_count;
};

static ssize_t output_handler(const char* buf, size_t count, void* data) {
  user_data_t* user_data = static_cast<user_data_t*>(data);
  user_data->output_buf = buf;
  user_data->output_count = count;
  return count;
}

TEST_F(TtyTest, TtyOutput) {
  // When no handler is registered then all writes should return EIO
  int bytes_written = 10;
  const char* message = "hello\n";
  int message_len = strlen(message);
  HandleAttr attrs;
  EXPECT_EQ(EIO, dev_tty_->Write(attrs, message, message_len, &bytes_written));

  // Setup output handler with user_data to record calls.
  user_data_t user_data;
  user_data.output_buf = NULL;
  user_data.output_count = 0;

  tioc_nacl_output handler;
  handler.handler = output_handler;
  handler.user_data = &user_data;

  EXPECT_EQ(0, dev_tty_->Ioctl(TIOCNACLOUTPUT, &handler));

  EXPECT_EQ(0, dev_tty_->Write(attrs, message, message_len, &bytes_written));
  EXPECT_EQ(message_len, bytes_written);
  EXPECT_EQ(message_len, user_data.output_count);
  EXPECT_EQ(0, strncmp(user_data.output_buf, message, message_len));
}

static int ki_ioctl_wrapper(int fd, int request, ...) {
  va_list ap;
  va_start(ap, request);
  int rtn = ki_ioctl(fd, request, ap);
  va_end(ap);
  return rtn;
}

static int TtyWrite(int fd, const char* string) {
  struct tioc_nacl_input_string input;
  input.buffer = string;
  input.length = strlen(input.buffer);
  return ki_ioctl_wrapper(fd, TIOCNACLINPUT, &input);
}

// Returns:
//   0 -> Not readable
//   1 -> Readable
//  -1 -> Error occured
static int IsReadable(int fd) {
  struct timeval timeout = { 0, 0 };
  fd_set readfds;
  fd_set errorfds;
  FD_ZERO(&readfds);
  FD_ZERO(&errorfds);
  FD_SET(fd, &readfds);
  FD_SET(fd, &errorfds);
  int rtn = ki_select(fd + 1, &readfds, NULL, &errorfds, &timeout);
  if (rtn == 0)
    return 0; // not readable
  if (rtn != 1)
    return -1; // error
  if (FD_ISSET(fd, &errorfds))
    return -2; // error
  if (!FD_ISSET(fd, &readfds))
    return -3; // error
  return 1; // readable
}

TEST_F(TtyTest, TtySelect) {
  struct timeval timeout;
  fd_set readfds;
  fd_set writefds;
  fd_set errorfds;

  int tty_fd = ki_open("/dev/tty", O_RDONLY);
  ASSERT_TRUE(tty_fd >= 0) << "tty open failed: " << errno;

  FD_ZERO(&readfds);
  FD_ZERO(&errorfds);
  FD_SET(tty_fd, &readfds);
  FD_SET(tty_fd, &errorfds);
  // 10 millisecond timeout
  timeout.tv_sec = 0;
  timeout.tv_usec = 10 * 1000;
  // Should timeout when no input is available.
  int rtn = ki_select(tty_fd + 1, &readfds, NULL, &errorfds, &timeout);
  ASSERT_EQ(rtn, 0) << "select failed: " << rtn << " err=" << strerror(errno);
  ASSERT_FALSE(FD_ISSET(tty_fd, &readfds));
  ASSERT_FALSE(FD_ISSET(tty_fd, &errorfds));

  FD_ZERO(&readfds);
  FD_ZERO(&writefds);
  FD_ZERO(&errorfds);
  FD_SET(tty_fd, &readfds);
  FD_SET(tty_fd, &writefds);
  FD_SET(tty_fd, &errorfds);
  // TTY should be writable on startup.
  rtn = ki_select(tty_fd + 1, &readfds, &writefds, &errorfds, NULL);
  ASSERT_EQ(rtn, 1);
  ASSERT_TRUE(FD_ISSET(tty_fd, &writefds));
  ASSERT_FALSE(FD_ISSET(tty_fd, &readfds));
  ASSERT_FALSE(FD_ISSET(tty_fd, &errorfds));

  // Send 4 bytes to TTY input
  ASSERT_EQ(0, TtyWrite(tty_fd, "input:test"));

  // TTY should not be readable until newline in written
  ASSERT_EQ(IsReadable(tty_fd), 0);
  ASSERT_EQ(0, TtyWrite(tty_fd, "input:\n"));

  // TTY should now be readable
  ASSERT_EQ(IsReadable(tty_fd), 1);

  ki_close(tty_fd);
}

TEST_F(TtyTest, TtyICANON) {
  int tty_fd = ki_open("/dev/tty", O_RDONLY);

  ASSERT_EQ(IsReadable(tty_fd), 0);

  struct termios tattr;
  tcgetattr(tty_fd, &tattr);
  tattr.c_lflag &= ~(ICANON|ECHO); /* Clear ICANON and ECHO. */
  tcsetattr(tty_fd, TCSAFLUSH, &tattr);

  ASSERT_EQ(IsReadable(tty_fd), 0);

  // Set some bytes to the TTY, not including newline
  ASSERT_EQ(0, TtyWrite(tty_fd, "a"));

  // Since we are not in canonical mode the bytes should be
  // immediately readable.
  ASSERT_EQ(IsReadable(tty_fd), 1);

  // Read byte from tty.
  char c;
  ASSERT_EQ(1, read(tty_fd, &c, 1));
  ASSERT_EQ('a', c);

  ASSERT_EQ(IsReadable(tty_fd), 0);
}

int g_recieved_signal = 0;

void sighandler(int sig) {
  g_recieved_signal = sig;
}

TEST_F(TtyTest, WindowSize) {
  // Get current window size
  struct winsize old_winsize = { 0 };
  ASSERT_EQ(0, dev_tty_->Ioctl(TIOCGWINSZ, &old_winsize));

  // Install signal handler
  sighandler_t new_handler = sighandler;
  sighandler_t old_handler = ki_signal(SIGWINCH, new_handler);
  ASSERT_NE(old_handler, SIG_ERR) << "signal return error: " << errno;

  // Set a new windows size
  struct winsize winsize;
  winsize.ws_col = 100;
  winsize.ws_row = 200;
  EXPECT_EQ(0, dev_tty_->Ioctl(TIOCSWINSZ, &winsize));
  EXPECT_EQ(g_recieved_signal, SIGWINCH);

  // Restore old signal handler
  EXPECT_EQ(new_handler, ki_signal(SIGWINCH, old_handler));

  // Verify new window size can be queried correctly.
  winsize.ws_col = 0;
  winsize.ws_row = 0;
  EXPECT_EQ(0, dev_tty_->Ioctl(TIOCGWINSZ, &winsize));
  EXPECT_EQ(winsize.ws_col, 100);
  EXPECT_EQ(winsize.ws_row, 200);

  // Restore original windows size.
  EXPECT_EQ(0, dev_tty_->Ioctl(TIOCSWINSZ, &old_winsize));
}

/*
 * Sleep for 50ms then send a resize event to /dev/tty.
 */
static void* resize_thread_main(void* arg) {
  usleep(50 * 1000);

  int* tty_fd = static_cast<int*>(arg);
  struct winsize winsize;
  winsize.ws_col = 100;
  winsize.ws_row = 200;
  ki_ioctl_wrapper(*tty_fd, TIOCSWINSZ, &winsize);
  return NULL;
}

TEST_F(TtyTest, ResizeDuringSelect) {
  // Test that a window resize during a call
  // to select(3) will cause it to fail with EINTR.
  int tty_fd = ki_open("/dev/tty", O_RDONLY);

  fd_set readfds;
  fd_set errorfds;
  FD_ZERO(&readfds);
  FD_ZERO(&errorfds);
  FD_SET(tty_fd, &readfds);
  FD_SET(tty_fd, &errorfds);

  pthread_t resize_thread;
  pthread_create(&resize_thread, NULL, resize_thread_main,
                 &tty_fd);

  struct timeval timeout;
  timeout.tv_sec = 20;
  timeout.tv_usec = 0;

  // TTY should not be readable either before or after the
  // call to select(3).
  ASSERT_EQ(IsReadable(tty_fd), 0);

  int rtn = ki_select(tty_fd + 1, &readfds, NULL, &errorfds, &timeout);
  pthread_join(resize_thread, NULL);
  ASSERT_EQ(-1, rtn);
  ASSERT_EQ(EINTR, errno);
  ASSERT_EQ(IsReadable(tty_fd), 0);
}

/*
 * Sleep for 50ms then send some input to the /dev/tty.
 */
static void* input_thread_main(void* arg) {
  usleep(50 * 1000);

  int fd = ki_open("/dev/tty", O_RDONLY);
  TtyWrite(fd, "test\n");
  return NULL;
}

TEST_F(TtyTest, InputDuringSelect) {
  // Test that input which occurs while in select causes
  // select to return.
  int tty_fd = ki_open("/dev/tty", O_RDONLY);

  fd_set readfds;
  fd_set errorfds;
  FD_ZERO(&readfds);
  FD_ZERO(&errorfds);
  FD_SET(tty_fd, &readfds);
  FD_SET(tty_fd, &errorfds);

  pthread_t resize_thread;
  pthread_create(&resize_thread, NULL, input_thread_main,
                 &dev_tty_);

  struct timeval timeout;
  timeout.tv_sec = 20;
  timeout.tv_usec = 0;

  int rtn = ki_select(tty_fd + 1, &readfds, NULL, &errorfds, &timeout);
  pthread_join(resize_thread, NULL);

  ASSERT_EQ(1, rtn);
}

}
