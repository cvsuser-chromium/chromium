// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_SYSTEM_CORE_TEST_BASE_H_
#define MOJO_SYSTEM_CORE_TEST_BASE_H_

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/synchronization/lock.h"
#include "mojo/public/system/core.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace mojo {
namespace system {

class CoreImpl;

namespace test {

class CoreTestBase_MockHandleInfo;

class CoreTestBase : public testing::Test {
 public:
  typedef CoreTestBase_MockHandleInfo MockHandleInfo;

  CoreTestBase();
  virtual ~CoreTestBase();

  virtual void SetUp() OVERRIDE;
  virtual void TearDown() OVERRIDE;

 protected:
  // |info| must remain alive until the returned handle is closed.
  MojoHandle CreateMockHandle(MockHandleInfo* info);

  CoreImpl* core() { return core_; }

 private:
  CoreImpl* core_;

  DISALLOW_COPY_AND_ASSIGN(CoreTestBase);
};

class CoreTestBase_MockHandleInfo {
 public:
  CoreTestBase_MockHandleInfo();
  ~CoreTestBase_MockHandleInfo();

  unsigned GetCtorCallCount() const;
  unsigned GetDtorCallCount() const;
  unsigned GetCloseCallCount() const;
  unsigned GetWriteMessageCallCount() const;
  unsigned GetReadMessageCallCount() const;
  unsigned GetAddWaiterCallCount() const;
  unsigned GetRemoveWaiterCallCount() const;
  unsigned GetCancelAllWaitersCallCount() const;

  // For use by |MockDispatcher|:
  void IncrementCtorCallCount();
  void IncrementDtorCallCount();
  void IncrementCloseCallCount();
  void IncrementWriteMessageCallCount();
  void IncrementReadMessageCallCount();
  void IncrementAddWaiterCallCount();
  void IncrementRemoveWaiterCallCount();
  void IncrementCancelAllWaitersCallCount();

 private:
  mutable base::Lock lock_;  // Protects the following members.
  unsigned ctor_call_count_;
  unsigned dtor_call_count_;
  unsigned close_call_count_;
  unsigned write_message_call_count_;
  unsigned read_message_call_count_;
  unsigned add_waiter_call_count_;
  unsigned remove_waiter_call_count_;
  unsigned cancel_all_waiters_call_count_;

  DISALLOW_COPY_AND_ASSIGN(CoreTestBase_MockHandleInfo);
};

}  // namespace test
}  // namespace system
}  // namespace mojo

#endif  // MOJO_SYSTEM_CORE_TEST_BASE_H_
