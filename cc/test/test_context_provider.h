// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_TEST_TEST_CONTEXT_PROVIDER_H_
#define CC_TEST_TEST_CONTEXT_PROVIDER_H_

#include "base/callback.h"
#include "base/memory/scoped_ptr.h"
#include "base/synchronization/lock.h"
#include "base/threading/thread_checker.h"
#include "cc/output/context_provider.h"
#include "cc/test/test_context_support.h"

namespace blink { class WebGraphicsContext3D; }

namespace cc {
class TestWebGraphicsContext3D;

class TestContextProvider : public cc::ContextProvider {
 public:
  typedef base::Callback<scoped_ptr<TestWebGraphicsContext3D>(void)>
    CreateCallback;

  static scoped_refptr<TestContextProvider> Create();
  static scoped_refptr<TestContextProvider> Create(
      scoped_ptr<TestWebGraphicsContext3D> context);

  virtual bool BindToCurrentThread() OVERRIDE;
  virtual Capabilities ContextCapabilities() OVERRIDE;
  virtual blink::WebGraphicsContext3D* Context3d() OVERRIDE;
  virtual gpu::ContextSupport* ContextSupport() OVERRIDE;
  virtual class GrContext* GrContext() OVERRIDE;
  virtual bool IsContextLost() OVERRIDE;
  virtual void VerifyContexts() OVERRIDE;
  virtual bool DestroyedOnMainThread() OVERRIDE;
  virtual void SetLostContextCallback(const LostContextCallback& cb) OVERRIDE;
  virtual void SetSwapBuffersCompleteCallback(
      const SwapBuffersCompleteCallback& cb) OVERRIDE;
  virtual void SetMemoryPolicyChangedCallback(
      const MemoryPolicyChangedCallback& cb) OVERRIDE;

  TestWebGraphicsContext3D* TestContext3d();

  // This returns the TestWebGraphicsContext3D but is valid to call
  // before the context is bound to a thread. This is needed to set up
  // state on the test context before binding. Don't call
  // makeContextCurrent on the context returned from this method.
  TestWebGraphicsContext3D* UnboundTestContext3d();

  void SetMemoryAllocation(const ManagedMemoryPolicy& policy);

  void SetMaxTransferBufferUsageBytes(size_t max_transfer_buffer_usage_bytes);

 protected:
  explicit TestContextProvider(scoped_ptr<TestWebGraphicsContext3D> context);
  virtual ~TestContextProvider();

  void OnLostContext();
  void OnSwapBuffersComplete();

  TestContextSupport support_;

  scoped_ptr<TestWebGraphicsContext3D> context3d_;
  bool bound_;

  base::ThreadChecker main_thread_checker_;
  base::ThreadChecker context_thread_checker_;

  base::Lock destroyed_lock_;
  bool destroyed_;

  LostContextCallback lost_context_callback_;
  SwapBuffersCompleteCallback swap_buffers_complete_callback_;
  MemoryPolicyChangedCallback memory_policy_changed_callback_;

  class LostContextCallbackProxy;
  scoped_ptr<LostContextCallbackProxy> lost_context_callback_proxy_;

  class SwapBuffersCompleteCallbackProxy;
  scoped_ptr<SwapBuffersCompleteCallbackProxy>
      swap_buffers_complete_callback_proxy_;
};

}  // namespace cc

#endif  // CC_TEST_TEST_CONTEXT_PROVIDER_H_

