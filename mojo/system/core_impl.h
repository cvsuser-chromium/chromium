// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_SYSTEM_CORE_IMPL_H_
#define MOJO_SYSTEM_CORE_IMPL_H_

#include "base/basictypes.h"
#include "base/containers/hash_tables.h"
#include "base/memory/ref_counted.h"
#include "base/synchronization/lock.h"
#include "mojo/public/system/core.h"

namespace mojo {
namespace system {

class CoreImpl;
class Dispatcher;

namespace test {
class CoreTestBase;
}

// |CoreImpl| is a singleton object that implements the Mojo system calls. With
// the (obvious) exception of |Init()|, which must be called first (and the call
// completed) before making any other calls, all the public methods are
// thread-safe.
class MOJO_SYSTEM_EXPORT CoreImpl {
 public:
  static void Init();

  static CoreImpl* Get() {
    return singleton_;
  }

  MojoResult Close(MojoHandle handle);

  MojoResult Wait(MojoHandle handle,
                  MojoWaitFlags flags,
                  MojoDeadline deadline);

  MojoResult WaitMany(const MojoHandle* handles,
                      const MojoWaitFlags* flags,
                      uint32_t num_handles,
                      MojoDeadline deadline);

  MojoResult CreateMessagePipe(MojoHandle* handle_0, MojoHandle* handle_1);

  MojoResult WriteMessage(MojoHandle handle,
                          const void* bytes, uint32_t num_bytes,
                          const MojoHandle* handles, uint32_t num_handles,
                          MojoWriteMessageFlags flags);

  MojoResult ReadMessage(MojoHandle handle,
                         void* bytes, uint32_t* num_bytes,
                         MojoHandle* handles, uint32_t* num_handles,
                         MojoReadMessageFlags flags);

 private:
  friend class test::CoreTestBase;

  // The |busy| member is used only to deal with functions (in particular
  // |WriteMessage()|) that want to hold on to a dispatcher and later remove it
  // from the handle table, without holding on to the handle table lock.
  //
  // For example, if |WriteMessage()| is called with a handle to be sent, (under
  // the handle table lock) it must first check that that handle is not busy (if
  // it is busy, then it fails with |MOJO_RESULT_BUSY|) and then marks it as
  // busy. To avoid deadlock, it should also try to acquire the locks for all
  // the dispatchers for the handles that it is sending (and fail with
  // |MOJO_RESULT_BUSY| if the attempt fails). At this point, it can release the
  // handle table lock.
  //
  // If |Close()| is simultaneously called on that handle, it too checks if the
  // handle is marked busy. If it is, it fails (with |MOJO_RESULT_BUSY|). This
  // prevents |WriteMessage()| from sending a handle that has been closed (or
  // learning about this too late).
  //
  // TODO(vtl): Move this implementation note.
  // To properly cancel waiters and avoid other races, |WriteMessage()| does not
  // transfer dispatchers from one handle to another, even when sending a
  // message in-process. Instead, it must transfer the "contents" of the
  // dispatcher to a new dispatcher, and then close the old dispatcher. If this
  // isn't done, in the in-process case, calls on the old handle may complete
  // after the the message has been received and a new handle created (and
  // possibly even after calls have been made on the new handle).
  struct HandleTableEntry {
    HandleTableEntry();
    explicit HandleTableEntry(const scoped_refptr<Dispatcher>& dispatcher);
    ~HandleTableEntry();

    scoped_refptr<Dispatcher> dispatcher;
    bool busy;
  };
  typedef base::hash_map<MojoHandle, HandleTableEntry> HandleTableMap;

  CoreImpl();
  ~CoreImpl();

  // Looks up the dispatcher for the given handle. Returns null if the handle is
  // invalid.
  scoped_refptr<Dispatcher> GetDispatcher(MojoHandle handle);

  // Assigns a new handle for the given dispatcher (which must be valid);
  // returns |MOJO_HANDLE_INVALID| on failure (due to hitting resource limits).
  // Must be called under |handle_table_lock_|.
  MojoHandle AddDispatcherNoLock(const scoped_refptr<Dispatcher>& dispatcher);

  // Internal implementation of |Wait()| and |WaitMany()|; doesn't do basic
  // validation of arguments.
  MojoResult WaitManyInternal(const MojoHandle* handles,
                              const MojoWaitFlags* flags,
                              uint32_t num_handles,
                              MojoDeadline deadline);

  // ---------------------------------------------------------------------------

  static CoreImpl* singleton_;

  // ---------------------------------------------------------------------------

  // TODO(vtl): |handle_table_lock_| should be a reader-writer lock (if only we
  // had them).
  base::Lock handle_table_lock_;  // Protects the immediately-following members.
  HandleTableMap handle_table_;
  MojoHandle next_handle_;  // Invariant: never |MOJO_HANDLE_INVALID|.

  // ---------------------------------------------------------------------------

  DISALLOW_COPY_AND_ASSIGN(CoreImpl);
};

}  // namespace system
}  // namespace mojo

#endif  // MOJO_SYSTEM_CORE_IMPL_H_
