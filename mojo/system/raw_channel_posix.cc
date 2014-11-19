// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/system/raw_channel.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <algorithm>
#include <deque>
#include <vector>

#include "base/basictypes.h"
#include "base/bind.h"
#include "base/compiler_specific.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/message_loop/message_loop.h"
#include "base/posix/eintr_wrapper.h"
#include "base/synchronization/lock.h"
#include "mojo/system/message_in_transit.h"
#include "mojo/system/platform_channel_handle.h"

namespace mojo {
namespace system {

const size_t kReadSize = 4096;

class RawChannelPosix : public RawChannel,
                        public base::MessageLoopForIO::Watcher {
 public:
  RawChannelPosix(const PlatformChannelHandle& handle,
                  Delegate* delegate,
                  base::MessageLoop* message_loop);
  virtual ~RawChannelPosix();

  // |RawChannel| implementation:
  virtual bool Init() OVERRIDE;
  virtual void Shutdown() OVERRIDE;
  virtual bool WriteMessage(MessageInTransit* message) OVERRIDE;

 private:
  // |base::MessageLoopForIO::Watcher| implementation:
  virtual void OnFileCanReadWithoutBlocking(int fd) OVERRIDE;
  virtual void OnFileCanWriteWithoutBlocking(int fd) OVERRIDE;

  // Watches for |fd_| to become writable. Must be called on the I/O thread.
  void WaitToWrite();

  // Calls |delegate()->OnFatalError(fatal_error)|. Must be called on the I/O
  // thread WITHOUT |write_lock_| held.
  void CallOnFatalError(Delegate::FatalError fatal_error);

  // Writes the message at the front of |write_message_queue_|, starting at
  // |write_message_offset_|. It removes and destroys if the write completes and
  // otherwise updates |write_message_offset_|. Returns true on success. Must be
  // called under |write_lock_|.
  bool WriteFrontMessageNoLock();

  // Cancels all pending writes and destroys the contents of
  // |write_message_queue_|. Should only be called if |is_dead_| is false; sets
  // |is_dead_| to true. Must be called under |write_lock_|.
  void CancelPendingWritesNoLock();

  base::MessageLoopForIO* message_loop_for_io() {
    return static_cast<base::MessageLoopForIO*>(message_loop());
  }

  int fd_;

  // Only used on the I/O thread:
  scoped_ptr<base::MessageLoopForIO::FileDescriptorWatcher> read_watcher_;
  scoped_ptr<base::MessageLoopForIO::FileDescriptorWatcher> write_watcher_;

  // We store data from |read()|s in |read_buffer_|. The start of |read_buffer_|
  // is always aligned with a message boundary (we will copy memory to ensure
  // this), but |read_buffer_| may be larger than the actual number of bytes we
  // have.
  std::vector<char> read_buffer_;
  size_t read_buffer_num_valid_bytes_;

  base::Lock write_lock_;  // Protects the following members.
  bool is_dead_;
  std::deque<MessageInTransit*> write_message_queue_;
  size_t write_message_offset_;
  // This is used for posting tasks from write threads to the I/O thread. It
  // must only be accessed under |write_lock_|. The weak pointers it produces
  // are only used/invalidated on the I/O thread.
  base::WeakPtrFactory<RawChannelPosix> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(RawChannelPosix);
};

RawChannelPosix::RawChannelPosix(const PlatformChannelHandle& handle,
                                 Delegate* delegate,
                                 base::MessageLoop* message_loop)
    : RawChannel(delegate, message_loop),
      fd_(handle.fd),
      read_buffer_num_valid_bytes_(0),
      is_dead_(false),
      write_message_offset_(0),
      weak_ptr_factory_(this) {
  CHECK_EQ(RawChannel::message_loop()->type(), base::MessageLoop::TYPE_IO);
  DCHECK_NE(fd_, -1);
}

RawChannelPosix::~RawChannelPosix() {
  DCHECK(is_dead_);
  DCHECK_EQ(fd_, -1);

  // No need to take the |write_lock_| here -- if there are still weak pointers
  // outstanding, then we're hosed anyway (since we wouldn't be able to
  // invalidate them cleanly, since we might not be on the I/O thread).
  DCHECK(!weak_ptr_factory_.HasWeakPtrs());

  // These must have been shut down/destroyed on the I/O thread.
  DCHECK(!read_watcher_.get());
  DCHECK(!write_watcher_.get());
}

bool RawChannelPosix::Init() {
  DCHECK_EQ(base::MessageLoop::current(), message_loop());

  DCHECK(!read_watcher_.get());
  read_watcher_.reset(new base::MessageLoopForIO::FileDescriptorWatcher());
  DCHECK(!write_watcher_.get());
  write_watcher_.reset(new base::MessageLoopForIO::FileDescriptorWatcher());

  // No need to take the lock. No one should be using us yet.
  DCHECK(write_message_queue_.empty());

  if (!message_loop_for_io()->WatchFileDescriptor(fd_, true,
          base::MessageLoopForIO::WATCH_READ, read_watcher_.get(), this)) {
    // TODO(vtl): I'm not sure |WatchFileDescriptor()| actually fails cleanly
    // (in the sense of returning the message loop's state to what it was before
    // it was called).
    read_watcher_.reset();
    write_watcher_.reset();
    return false;
  }

  return true;
}

void RawChannelPosix::Shutdown() {
  DCHECK_EQ(base::MessageLoop::current(), message_loop());

  base::AutoLock locker(write_lock_);
  if (!is_dead_)
    CancelPendingWritesNoLock();

  DCHECK_NE(fd_, -1);
  if (close(fd_) != 0)
    PLOG(ERROR) << "close";
  fd_ = -1;

  weak_ptr_factory_.InvalidateWeakPtrs();

  read_watcher_.reset();  // This will stop watching (if necessary).
  write_watcher_.reset();  // This will stop watching (if necessary).
}

// Reminder: This must be thread-safe, and takes ownership of |message| on
// success.
bool RawChannelPosix::WriteMessage(MessageInTransit* message) {
  base::AutoLock locker(write_lock_);
  if (is_dead_) {
    message->Destroy();
    return false;
  }

  if (!write_message_queue_.empty()) {
    write_message_queue_.push_back(message);
    return true;
  }

  write_message_queue_.push_front(message);
  DCHECK_EQ(write_message_offset_, 0u);
  bool result = WriteFrontMessageNoLock();
  DCHECK(result || write_message_queue_.empty());

  if (!result) {
    // Even if we're on the I/O thread, don't call |OnFatalError()| in the
    // nested context.
    message_loop()->PostTask(FROM_HERE,
                             base::Bind(&RawChannelPosix::CallOnFatalError,
                                        weak_ptr_factory_.GetWeakPtr(),
                                        Delegate::FATAL_ERROR_FAILED_WRITE));
  } else if (!write_message_queue_.empty()) {
    // Set up to wait for the FD to become writable. If we're not on the I/O
    // thread, we have to post a task to do this.
    if (base::MessageLoop::current() == message_loop()) {
      WaitToWrite();
    } else {
      message_loop()->PostTask(FROM_HERE,
                               base::Bind(&RawChannelPosix::WaitToWrite,
                                          weak_ptr_factory_.GetWeakPtr()));
    }
  }

  return result;
}

void RawChannelPosix::OnFileCanReadWithoutBlocking(int fd) {
  DCHECK_EQ(fd, fd_);
  DCHECK_EQ(base::MessageLoop::current(), message_loop());

  bool did_dispatch_message = false;
  // Tracks the offset of the first undispatched message in |read_buffer_|.
  // Currently, we copy data to ensure that this is zero at the beginning.
  size_t read_buffer_start = 0;
  for (;;) {
    if (read_buffer_.size() - (read_buffer_start + read_buffer_num_valid_bytes_)
            < kReadSize) {
      // Use power-of-2 buffer sizes.
      // TODO(vtl): Make sure the buffer doesn't get too large (and enforce the
      // maximum message size to whatever extent necessary).
      // TODO(vtl): We may often be able to peek at the header and get the real
      // required extra space (which may be much bigger than |kReadSize|).
      size_t new_size = std::max(read_buffer_.size(), kReadSize);
      while (new_size <
                 read_buffer_start + read_buffer_num_valid_bytes_ + kReadSize)
        new_size *= 2;

      // TODO(vtl): It's suboptimal to zero out the fresh memory.
      read_buffer_.resize(new_size, 0);
    }

    ssize_t bytes_read = HANDLE_EINTR(
        read(fd_,
             &read_buffer_[read_buffer_start + read_buffer_num_valid_bytes_],
             kReadSize));
    if (bytes_read < 0) {
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
        PLOG(ERROR) << "read";
        {
          base::AutoLock locker(write_lock_);
          CancelPendingWritesNoLock();
        }
        CallOnFatalError(Delegate::FATAL_ERROR_FAILED_READ);
        return;
      }

      break;
    }

    read_buffer_num_valid_bytes_ += static_cast<size_t>(bytes_read);

    // Dispatch all the messages that we can.
    while (read_buffer_num_valid_bytes_ >= sizeof(MessageInTransit)) {
      const MessageInTransit* message =
          reinterpret_cast<const MessageInTransit*>(
              &read_buffer_[read_buffer_start]);
      DCHECK_EQ(reinterpret_cast<size_t>(message) %
                    MessageInTransit::kMessageAlignment, 0u);
      // If we have the header, not the whole message....
      if (read_buffer_num_valid_bytes_ <
              message->size_with_header_and_padding())
        break;

      // Dispatch the message.
      delegate()->OnReadMessage(*message);
      if (!read_watcher_.get()) {
        // |Shutdown()| was called in |OnReadMessage()|.
        // TODO(vtl): Add test for this case.
        return;
      }
      did_dispatch_message = true;

      // Update our state.
      read_buffer_start += message->size_with_header_and_padding();
      read_buffer_num_valid_bytes_ -= message->size_with_header_and_padding();
    }

    // If we dispatched any messages, stop reading for now (and let the message
    // loop do its thing for another round).
    // TODO(vtl): Is this the behavior we want? (Alternatives: i. Dispatch only
    // a single message. Risks: slower, more complex if we want to avoid lots of
    // copying. ii. Keep reading until there's no more data and dispatch all the
    // messages we can. Risks: starvation of other users of the message loop.)
    if (did_dispatch_message)
      break;

    // If we didn't max out |kReadSize|, stop reading for now.
    if (static_cast<size_t>(bytes_read) < kReadSize)
      break;

    // Else try to read some more....
  }

  // Move data back to start.
  if (read_buffer_start > 0) {
    memmove(&read_buffer_[0], &read_buffer_[read_buffer_start],
            read_buffer_num_valid_bytes_);
    read_buffer_start = 0;
  }
}

void RawChannelPosix::OnFileCanWriteWithoutBlocking(int fd) {
  DCHECK_EQ(fd, fd_);
  DCHECK_EQ(base::MessageLoop::current(), message_loop());

  bool did_fail = false;
  {
    base::AutoLock locker(write_lock_);
    DCHECK(!is_dead_);
    DCHECK(!write_message_queue_.empty());

    bool result = WriteFrontMessageNoLock();
    DCHECK(result || write_message_queue_.empty());

    if (!result)
      did_fail = true;
    else if (!write_message_queue_.empty())
      WaitToWrite();
  }
  if (did_fail)
    CallOnFatalError(Delegate::FATAL_ERROR_FAILED_WRITE);
}

void RawChannelPosix::WaitToWrite() {
  DCHECK_EQ(base::MessageLoop::current(), message_loop());

  DCHECK(write_watcher_.get());
  bool result = message_loop_for_io()->WatchFileDescriptor(
      fd_, false, base::MessageLoopForIO::WATCH_WRITE, write_watcher_.get(),
      this);
  DCHECK(result);
}

void RawChannelPosix::CallOnFatalError(Delegate::FatalError fatal_error) {
  DCHECK_EQ(base::MessageLoop::current(), message_loop());
  // TODO(vtl): Add a "write_lock_.AssertNotAcquired()"?
  delegate()->OnFatalError(fatal_error);
}

bool RawChannelPosix::WriteFrontMessageNoLock() {
  write_lock_.AssertAcquired();

  DCHECK(!is_dead_);
  DCHECK(!write_message_queue_.empty());

  MessageInTransit* message = write_message_queue_.front();
  DCHECK_LT(write_message_offset_, message->size_with_header_and_padding());
  size_t bytes_to_write =
      message->size_with_header_and_padding() - write_message_offset_;
  ssize_t bytes_written = HANDLE_EINTR(
      write(fd_,
            reinterpret_cast<char*>(message) + write_message_offset_,
            bytes_to_write));
  if (bytes_written < 0) {
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
      PLOG(ERROR) << "write of size " << bytes_to_write;
      CancelPendingWritesNoLock();
      return false;
    }

    // We simply failed to write since we'd block. The logic is the same as if
    // we got a partial write.
    bytes_written = 0;
  }

  DCHECK_GE(bytes_written, 0);
  if (static_cast<size_t>(bytes_written) < bytes_to_write) {
    // Partial (or no) write.
    write_message_offset_ += static_cast<size_t>(bytes_written);
  } else {
    // Complete write.
    DCHECK_EQ(static_cast<size_t>(bytes_written), bytes_to_write);
    write_message_queue_.pop_front();
    write_message_offset_ = 0;
    message->Destroy();
  }

  return true;
}

void RawChannelPosix::CancelPendingWritesNoLock() {
  write_lock_.AssertAcquired();
  DCHECK(!is_dead_);

  is_dead_ = true;
  for (std::deque<MessageInTransit*>::iterator it =
           write_message_queue_.begin(); it != write_message_queue_.end();
       ++it) {
    (*it)->Destroy();
  }
  write_message_queue_.clear();
}

// -----------------------------------------------------------------------------

// Static factory method declared in raw_channel.h.
// static
RawChannel* RawChannel::Create(const PlatformChannelHandle& handle,
                               Delegate* delegate,
                               base::MessageLoop* message_loop) {
  return new RawChannelPosix(handle, delegate, message_loop);
}

}  // namespace system
}  // namespace mojo
