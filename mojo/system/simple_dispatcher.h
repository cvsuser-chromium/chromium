// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_SYSTEM_SIMPLE_DISPATCHER_H_
#define MOJO_SYSTEM_SIMPLE_DISPATCHER_H_

#include <list>

#include "base/basictypes.h"
#include "mojo/public/system/system_export.h"
#include "mojo/system/dispatcher.h"
#include "mojo/system/waiter_list.h"

namespace mojo {
namespace system {

// A base class for simple dispatchers. "Simple" means that there's a one-to-one
// correspondence between handles and dispatchers (see the explanatory comment
// in core_impl.cc). This class implements the standard waiter-signalling
// mechanism in that case.
class MOJO_SYSTEM_EXPORT SimpleDispatcher : public Dispatcher {
 protected:
  SimpleDispatcher();

  friend class base::RefCountedThreadSafe<SimpleDispatcher>;
  virtual ~SimpleDispatcher();

  // To be called by subclasses when the state changes (so
  // |SatisfiedFlagsNoLock()| and |SatisfiableFlagsNoLock()| should be checked
  // again). Must be called under lock.
  void StateChangedNoLock();

  // These should return the wait flags that are satisfied by the object's
  // current state and those that may eventually be satisfied by this object's
  // state, respectively. They should be overridden by subclasses to reflect
  // their notion of state. They are never called after the dispatcher has been
  // closed. They are called under |lock_|.
  virtual MojoWaitFlags SatisfiedFlagsNoLock() const = 0;
  virtual MojoWaitFlags SatisfiableFlagsNoLock() const = 0;

  // |Dispatcher| implementation/overrides:
  virtual void CancelAllWaitersNoLock() OVERRIDE;
  virtual MojoResult AddWaiterImplNoLock(Waiter* waiter,
                                         MojoWaitFlags flags,
                                         MojoResult wake_result) OVERRIDE;
  virtual void RemoveWaiterImplNoLock(Waiter* waiter) OVERRIDE;

 private:
  // Protected by |lock()|:
  WaiterList waiter_list_;

  DISALLOW_COPY_AND_ASSIGN(SimpleDispatcher);
};

}  // namespace system
}  // namespace mojo

#endif  // MOJO_SYSTEM_SIMPLE_DISPATCHER_H_
