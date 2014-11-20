// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include "nacl_io/ioctl.h"
#include "nacl_io/kernel_wrap.h"
#include "nacl_io/nacl_io.h"

#include "ppapi/c/ppb_var.h"

#include "ppapi/cpp/input_event.h"
#include "ppapi/cpp/message_loop.h"
#include "ppapi/cpp/module.h"
#include "ppapi/cpp/rect.h"
#include "ppapi/cpp/size.h"
#include "ppapi/cpp/touch_point.h"
#include "ppapi/cpp/var.h"

#include "ppapi_simple/ps_event.h"
#include "ppapi_simple/ps_instance.h"
#include "ppapi_simple/ps_interface.h"
#include "ppapi_simple/ps_main.h"

#if defined(WIN32)
#define open _open
#define dup2 _dup2
#endif

static PSInstance* s_InstanceObject = NULL;

PSInstance* PSInstance::GetInstance() {
  return s_InstanceObject;
}

struct StartInfo {
  PSInstance* inst_;
  uint32_t argc_;
  char** argv_;
};


// The starting point for 'main'.  We create this thread to hide the real
// main pepper thread which must never be blocked.
void* PSInstance::MainThreadThunk(void *info) {
  s_InstanceObject->Trace("Got MainThreadThunk.\n");
  StartInfo* si = static_cast<StartInfo*>(info);
  si->inst_->main_loop_ = new pp::MessageLoop(si->inst_);
  si->inst_->main_loop_->AttachToCurrentThread();

  int ret = si->inst_->MainThread(si->argc_, si->argv_);

  char* exit_message = si->inst_->exit_message_;
  bool should_exit = exit_message == NULL;

  if (exit_message) {
    // Send the exit message to JavaScript. Don't call exit(), so the message
    // doesn't get dropped.
    si->inst_->Log("Posting exit message to JavaScript.\n");
    si->inst_->PostMessage(exit_message);
    free(exit_message);
    exit_message = si->inst_->exit_message_ = NULL;
  }

  // Clean up StartInfo.
  for (uint32_t i = 0; i < si->argc_; i++) {
    delete[] si->argv_[i];
  }
  delete[] si->argv_;
  delete si;


  if (should_exit) {
    // Exit the entire process once the 'main' thread returns.
    // The error code will be available to javascript via
    // the exitcode parameter of the crash event.
    exit(ret);
  }

  return NULL;
}

// The default implementation supports running a 'C' main.
int PSInstance::MainThread(int argc, char* argv[]) {
  if (!main_cb_) {
    Error("No main defined.\n");
    return 0;
  }

  Trace("Starting MAIN.\n");
  int ret = main_cb_(argc, argv);
  Log("Main thread returned with %d.\n", ret);

  return ret;
}

PSInstance::PSInstance(PP_Instance instance)
    : pp::Instance(instance),
      pp::MouseLock(this),
      pp::Graphics3DClient(this),
      main_loop_(NULL),
      events_enabled_(PSE_NONE),
      verbosity_(PSV_WARN),
      tty_fd_(-1),
      tty_prefix_(NULL),
      exit_message_(NULL) {
  // Set the single Instance object
  s_InstanceObject = this;

#ifdef NACL_SDK_DEBUG
  SetVerbosity(PSV_LOG);
#endif

  RequestInputEvents(PP_INPUTEVENT_CLASS_MOUSE |
                     PP_INPUTEVENT_CLASS_KEYBOARD |
                     PP_INPUTEVENT_CLASS_WHEEL |
                     PP_INPUTEVENT_CLASS_TOUCH);
}

PSInstance::~PSInstance() {}

void PSInstance::SetMain(PSMainFunc_t main) {
  main_cb_ = main;
}

bool PSInstance::Init(uint32_t arg,
                      const char* argn[],
                      const char* argv[]) {
  StartInfo* si = new StartInfo;

  si->inst_ = this;
  si->argc_ = 0;
  si->argv_ = new char *[arg+1];
  si->argv_[0] = NULL;

  // Process embed attributes into the environment.
  // Converted the attribute names to uppercase as environment variables are
  // case sensitive but are almost universally uppercase in practice.
  for (uint32_t i = 0; i < arg; i++) {
    std::string key = argn[i];
    std::transform(key.begin(), key.end(), key.begin(), toupper);
    setenv(key.c_str(), argv[i], 1);
  }

  // Set a default value for SRC.
  setenv("SRC", "NMF?", 0);
  // Use the src tag name if ARG0 is not explicitly specified.
  setenv("ARG0", getenv("SRC"), 0);

  // Walk ARG0..ARGn populating argv until an argument is missing.
  for (;;) {
    std::ostringstream arg_stream;
    arg_stream << "ARG" << si->argc_;
    std::string arg_name = arg_stream.str();
    const char* next_arg = getenv(arg_name.c_str());
    if (NULL == next_arg)
      break;

    char* value = new char[strlen(next_arg) + 1];
    strcpy(value, next_arg);
    si->argv_[si->argc_++] = value;
  }

  PSInterfaceInit();
  bool props_processed = ProcessProperties();

  // Log arg values only once ProcessProperties has been
  // called so that the ps_verbosity attribute will be in
  // effect.
  for (uint32_t i = 0; i < arg; i++) {
    if (argv[i]) {
      Trace("attribs[%d] '%s=%s'\n", i, argn[i], argv[i]);
    } else {
      Trace("attribs[%d] '%s'\n", i, argn[i]);
    }
  }

  for (uint32_t i = 0; i < si->argc_; i++) {
    Trace("argv[%d] '%s'\n", i, si->argv_[i]);
  }

  if (!props_processed) {
    Warn("Skipping create thread.\n");
    return false;
  }

  pthread_t main_thread;
  int ret = pthread_create(&main_thread, NULL, MainThreadThunk, si);
  Trace("Created thread: %d.\n", ret);
  return ret == 0;
}

// Processes the properties set at compile time via the
// initialization macro, or via dynamically set embed attributes
// through instance DidCreate.
bool PSInstance::ProcessProperties() {
  // Set default values
  setenv("PS_STDIN", "/dev/stdin", 0);
  setenv("PS_STDOUT", "/dev/stdout", 0);
  setenv("PS_STDERR", "/dev/console3", 0);

  // Reset verbosity if passed in
  const char* verbosity = getenv("PS_VERBOSITY");
  if (verbosity) SetVerbosity(static_cast<Verbosity>(atoi(verbosity)));

  // Enable NaCl IO to map STDIN, STDOUT, and STDERR
  nacl_io_init_ppapi(PSGetInstanceId(), PSGetInterface);
  int fd0 = open(getenv("PS_STDIN"), O_RDONLY);
  dup2(fd0, 0);

  int fd1 = open(getenv("PS_STDOUT"), O_WRONLY);
  dup2(fd1, 1);

  int fd2 = open(getenv("PS_STDERR"), O_WRONLY);
  dup2(fd2, 2);

  tty_prefix_ = getenv("PS_TTY_PREFIX");
  if (tty_prefix_) {
    tty_fd_ = open("/dev/tty", O_WRONLY);
    if (tty_fd_ >= 0) {
      RegisterMessageHandler(tty_prefix_, MessageHandlerInputStatic, this);
      const char* tty_resize = getenv("PS_TTY_RESIZE");
      if (tty_resize)
        RegisterMessageHandler(tty_resize, MessageHandlerResizeStatic, this);

      char* tty_rows = getenv("PS_TTY_ROWS");
      char* tty_cols = getenv("PS_TTY_COLS");
      if (tty_rows && tty_cols) {
        char* end = tty_rows;
        int rows = strtol(tty_rows, &end, 10);
        if (*end != '\0' || rows < 0) {
          Error("Invalid value for PS_TTY_ROWS: %s", tty_rows);
        } else {
          end = tty_cols;
          int cols = strtol(tty_cols, &end, 10);
          if (*end != '\0' || cols < 0)
            Error("Invalid value for PS_TTY_COLS: %s", tty_cols);
          else
            HandleResize(cols, rows);
        }
      }
      else if (tty_rows || tty_cols) {
        Error("PS_TTY_ROWS and PS_TTY_COLS must be set together");
      }

      tioc_nacl_output handler;
      handler.handler = TtyOutputHandlerStatic;
      handler.user_data = this;
      ioctl(tty_fd_, TIOCNACLOUTPUT, reinterpret_cast<char*>(&handler));
    } else {
      Error("Failed to open /dev/tty.\n");
    }
  }

  const char* exit_message = getenv("PS_EXIT_MESSAGE");
  if (exit_message) {
    exit_message_ = strdup(exit_message);
  }

  // Set line buffering on stdout and stderr
#if !defined(WIN32)
  setvbuf(stderr, NULL, _IOLBF, 0);
  setvbuf(stdout, NULL, _IOLBF, 0);
#endif
  return true;
}

void PSInstance::SetVerbosity(Verbosity verbosity) {
  verbosity_ = verbosity;
}

void PSInstance::VALog(Verbosity verbosity, const char *fmt, va_list args) {
  if (verbosity <= verbosity_) {
    fprintf(stderr, "ps: ");
    vfprintf(stderr, fmt, args);
  }
}

void PSInstance::Trace(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  VALog(PSV_TRACE, fmt, ap);
  va_end(ap);
}

void PSInstance::Log(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  VALog(PSV_LOG, fmt, ap);
  va_end(ap);
}

void PSInstance::Warn(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  VALog(PSV_WARN, fmt, ap);
  va_end(ap);
}

void PSInstance::Error(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  VALog(PSV_ERROR, fmt, ap);
  va_end(ap);
}

void PSInstance::SetEnabledEvents(uint32_t mask) {
  events_enabled_ = mask;
  if (mask == 0) {
    static bool warn_once = true;
    if (warn_once) {
      Warn("PSInstance::SetEnabledEvents(mask) where mask == 0 will block\n");
      Warn("all events. This can come from PSEventSetFilter(PSE_NONE);\n");
      warn_once = false;
    }
  }
}

void PSInstance::PostEvent(PSEventType type) {
  assert(PSE_GRAPHICS3D_GRAPHICS3DCONTEXTLOST == type ||
         PSE_MOUSELOCK_MOUSELOCKLOST == type);

  PSEvent *env = (PSEvent *) malloc(sizeof(PSEvent));
  memset(env, 0, sizeof(*env));
  env->type = type;
  event_queue_.Enqueue(env);
}

void PSInstance::PostEvent(PSEventType type, PP_Bool bool_value) {
  assert(PSE_INSTANCE_DIDCHANGEFOCUS == type);

  PSEvent *env = (PSEvent *) malloc(sizeof(PSEvent));
  memset(env, 0, sizeof(*env));
  env->type = type;
  env->as_bool = bool_value;
  event_queue_.Enqueue(env);
}

void PSInstance::PostEvent(PSEventType type, PP_Resource resource) {
  assert(PSE_INSTANCE_HANDLEINPUT == type ||
         PSE_INSTANCE_DIDCHANGEVIEW == type);

  if (resource) {
    PSInterfaceCore()->AddRefResource(resource);
  }
  PSEvent *env = (PSEvent *) malloc(sizeof(PSEvent));
  memset(env, 0, sizeof(*env));
  env->type = type;
  env->as_resource = resource;
  event_queue_.Enqueue(env);
}

ssize_t PSInstance::TtyOutputHandler(const char* buf, size_t count) {
  // We prepend the prefix_ to the data in buf, then package it up
  // and post it as a message to javascript.
  const char* data = static_cast<const char*>(buf);
  std::string message = tty_prefix_;
  message.append(data, count);
  PostMessage(pp::Var(message));
  return count;
}

void PSInstance::MessageHandlerInput(const pp::Var& message) {
  // Since our message may contain null characters, we can't send it as a
  // naked C string, so we package it up in this struct before sending it
  // to the ioctl.
  assert(message.is_string());
  std::string buffer = message.AsString();

  struct tioc_nacl_input_string ioctl_message;
  ioctl_message.length = buffer.size();
  ioctl_message.buffer = buffer.c_str();
  int ret =
    ioctl(tty_fd_, TIOCNACLINPUT, reinterpret_cast<char*>(&ioctl_message));
  if (ret != 0 && errno != ENOTTY) {
    Error("ioctl returned unexpected error: %d.\n", ret);
  }
}

void PSInstance::HandleResize(int width, int height){
  struct winsize size;
  memset(&size, 0, sizeof(size));
  size.ws_col = width;
  size.ws_row = height;
  ioctl(tty_fd_, TIOCSWINSZ, reinterpret_cast<char*>(&size));
}

void PSInstance::MessageHandlerResize(const pp::Var& message) {
  assert(message.is_array());
  pp::VarArray array(message);
  assert(array.GetLength() == 2);

  int width = array.Get(0).AsInt();
  int height = array.Get(1).AsInt();
  HandleResize(width, height);
}

ssize_t PSInstance::TtyOutputHandlerStatic(const char* buf,
                                           size_t count,
                                           void* user_data) {
  PSInstance* instance = reinterpret_cast<PSInstance*>(user_data);
  return instance->TtyOutputHandler(buf, count);
}

void PSInstance::MessageHandlerInputStatic(const pp::Var& key,
                                           const pp::Var& value,
                                           void* user_data) {
  PSInstance* instance = reinterpret_cast<PSInstance*>(user_data);
  instance->MessageHandlerInput(value);
}

void PSInstance::MessageHandlerResizeStatic(const pp::Var& key,
                                            const pp::Var& value,
                                            void* user_data) {
  PSInstance* instance = reinterpret_cast<PSInstance*>(user_data);
  instance->MessageHandlerResize(value);
}

void PSInstance::RegisterMessageHandler(std::string message_name,
                                        MessageHandler_t handler,
                                        void* user_data) {
  if (handler == NULL) {
    message_handlers_.erase(message_name);
    return;
  }

  MessageHandler message_handler = { handler, user_data };
  message_handlers_[message_name] = message_handler;
}

void PSInstance::PostEvent(PSEventType type, const PP_Var& var) {
  assert(PSE_INSTANCE_HANDLEMESSAGE == type);

  // If the user has specified a tty_prefix_, then filter out the
  // matching message here and pass them to the tty node via
  // ioctl() rather then adding them to the event queue.
  pp::Var event(var);
  if (tty_fd_ >= 0 && event.is_string()) {
    std::string message = event.AsString();
    size_t prefix_len = strlen(tty_prefix_);
    if (message.size() > prefix_len) {
      if (!strncmp(message.c_str(), tty_prefix_, prefix_len)) {
        MessageHandlerInput(pp::Var(message.substr(prefix_len)));
        return;
      }
    }
  }

  // If the message is a dictionary then see if it matches one
  // of the specific handlers, then call that handler rather than
  // queuing an event.
  if (tty_fd_ >= 0 && event.is_dictionary()) {
    pp::VarDictionary dictionary(var);
    pp::VarArray keys = dictionary.GetKeys();
    if (keys.GetLength() == 1) {
      pp::Var key = keys.Get(0);
      MessageHandlerMap::iterator iter =
          message_handlers_.find(key.AsString());
      if (iter != message_handlers_.end()) {
        MessageHandler_t handler = iter->second.handler;
        void* user_data = iter->second.user_data;
        handler(key, dictionary.Get(key), user_data);
        return;
      }
    }
  }

  PSInterfaceVar()->AddRef(var);
  PSEvent *env = (PSEvent *) malloc(sizeof(PSEvent));
  memset(env, 0, sizeof(*env));
  env->type = type;
  env->as_var = var;
  event_queue_.Enqueue(env);
}

PSEvent* PSInstance::TryAcquireEvent() {
  PSEvent* event;
  while(true) {
    event = event_queue_.Dequeue(false);
    if (NULL == event)
      break;
    if (events_enabled_ & event->type)
      break;
    // Release filtered events & continue to acquire.
    ReleaseEvent(event);
  }
  return event;
}

PSEvent* PSInstance::WaitAcquireEvent() {
  PSEvent* event;
  while(true) {
    event = event_queue_.Dequeue(true);
    if (events_enabled_ & event->type)
      break;
    // Release filtered events & continue to acquire.
    ReleaseEvent(event);
  }
  return event;
}

void PSInstance::ReleaseEvent(PSEvent* event) {
  if (event) {
    switch(event->type) {
      case PSE_INSTANCE_HANDLEMESSAGE:
        PSInterfaceVar()->Release(event->as_var);
        break;
      case PSE_INSTANCE_HANDLEINPUT:
      case PSE_INSTANCE_DIDCHANGEVIEW:
        if (event->as_resource) {
          PSInterfaceCore()->ReleaseResource(event->as_resource);
        }
        break;
      default:
        break;
    }
    free(event);
  }
}

void PSInstance::HandleMessage(const pp::Var& message) {
  Trace("Got Message\n");
  PostEvent(PSE_INSTANCE_HANDLEMESSAGE, message.pp_var());
}

bool PSInstance::HandleInputEvent(const pp::InputEvent& event) {
  PostEvent(PSE_INSTANCE_HANDLEINPUT, event.pp_resource());
  return true;
}

void PSInstance::DidChangeView(const pp::View& view) {
  pp::Size new_size = view.GetRect().size();
  Log("Got View change: %d,%d\n", new_size.width(), new_size.height());
  PostEvent(PSE_INSTANCE_DIDCHANGEVIEW, view.pp_resource());
}

void PSInstance::DidChangeFocus(bool focus) {
  Log("Got Focus change: %s\n", focus ? "FOCUS ON" : "FOCUS OFF");
  PostEvent(PSE_INSTANCE_DIDCHANGEFOCUS, focus ? PP_TRUE : PP_FALSE);
}

void PSInstance::Graphics3DContextLost() {
  Log("Graphics3DContextLost\n");
  PostEvent(PSE_GRAPHICS3D_GRAPHICS3DCONTEXTLOST);
}

void PSInstance::MouseLockLost() {
  Log("MouseLockLost\n");
  PostEvent(PSE_MOUSELOCK_MOUSELOCKLOST);
}
