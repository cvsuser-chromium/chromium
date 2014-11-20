// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/test/test_context_provider.h"

#include <set>
#include <vector>

#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/logging.h"
#include "base/strings/string_split.h"
#include "cc/test/test_web_graphics_context_3d.h"

namespace cc {

class TestContextProvider::LostContextCallbackProxy
    : public blink::WebGraphicsContext3D::WebGraphicsContextLostCallback {
 public:
  explicit LostContextCallbackProxy(TestContextProvider* provider)
      : provider_(provider) {
    provider_->context3d_->setContextLostCallback(this);
  }

  virtual ~LostContextCallbackProxy() {
    provider_->context3d_->setContextLostCallback(NULL);
  }

  virtual void onContextLost() {
    provider_->OnLostContext();
  }

 private:
  TestContextProvider* provider_;
};

class TestContextProvider::SwapBuffersCompleteCallbackProxy
    : public blink::WebGraphicsContext3D::
          WebGraphicsSwapBuffersCompleteCallbackCHROMIUM {
 public:
  explicit SwapBuffersCompleteCallbackProxy(TestContextProvider* provider)
      : provider_(provider) {
    provider_->context3d_->setSwapBuffersCompleteCallbackCHROMIUM(this);
  }

  virtual ~SwapBuffersCompleteCallbackProxy() {
    provider_->context3d_->setSwapBuffersCompleteCallbackCHROMIUM(NULL);
  }

  virtual void onSwapBuffersComplete() {
    provider_->OnSwapBuffersComplete();
  }

 private:
  TestContextProvider* provider_;
};

// static
scoped_refptr<TestContextProvider> TestContextProvider::Create() {
  return Create(TestWebGraphicsContext3D::Create().Pass());
}

// static
scoped_refptr<TestContextProvider> TestContextProvider::Create(
    scoped_ptr<TestWebGraphicsContext3D> context) {
  if (!context)
    return NULL;
  return new TestContextProvider(context.Pass());
}

TestContextProvider::TestContextProvider(
    scoped_ptr<TestWebGraphicsContext3D> context)
    : context3d_(context.Pass()),
      bound_(false),
      destroyed_(false) {
  DCHECK(main_thread_checker_.CalledOnValidThread());
  DCHECK(context3d_);
  context_thread_checker_.DetachFromThread();
  context3d_->set_test_support(&support_);
}

TestContextProvider::~TestContextProvider() {
  DCHECK(main_thread_checker_.CalledOnValidThread() ||
         context_thread_checker_.CalledOnValidThread());
}

bool TestContextProvider::BindToCurrentThread() {
  DCHECK(context3d_);

  // This is called on the thread the context will be used.
  DCHECK(context_thread_checker_.CalledOnValidThread());

  if (bound_)
    return true;

  bound_ = true;
  if (!context3d_->makeContextCurrent()) {
    base::AutoLock lock(destroyed_lock_);
    destroyed_ = true;
    return false;
  }

  lost_context_callback_proxy_.reset(new LostContextCallbackProxy(this));
  swap_buffers_complete_callback_proxy_.reset(
      new SwapBuffersCompleteCallbackProxy(this));

  return true;
}

ContextProvider::Capabilities TestContextProvider::ContextCapabilities() {
  DCHECK(context3d_);
  DCHECK(bound_);
  DCHECK(context_thread_checker_.CalledOnValidThread());

  return context3d_->test_capabilities();
}

blink::WebGraphicsContext3D* TestContextProvider::Context3d() {
  DCHECK(context3d_);
  DCHECK(bound_);
  DCHECK(context_thread_checker_.CalledOnValidThread());

  return context3d_.get();
}

gpu::ContextSupport* TestContextProvider::ContextSupport() {
  DCHECK(context3d_);
  DCHECK(bound_);
  DCHECK(context_thread_checker_.CalledOnValidThread());

  return &support_;
}

class GrContext* TestContextProvider::GrContext() {
  DCHECK(context3d_);
  DCHECK(bound_);
  DCHECK(context_thread_checker_.CalledOnValidThread());

  // TODO(danakj): Make a test GrContext that works with a test Context3d.
  return NULL;
}

bool TestContextProvider::IsContextLost() {
  DCHECK(context3d_);
  DCHECK(bound_);
  DCHECK(context_thread_checker_.CalledOnValidThread());

  return context3d_->isContextLost();
}

void TestContextProvider::VerifyContexts() {
  DCHECK(context3d_);
  DCHECK(bound_);
  DCHECK(context_thread_checker_.CalledOnValidThread());

  if (context3d_->isContextLost()) {
    base::AutoLock lock(destroyed_lock_);
    destroyed_ = true;
  }
}

bool TestContextProvider::DestroyedOnMainThread() {
  DCHECK(main_thread_checker_.CalledOnValidThread());

  base::AutoLock lock(destroyed_lock_);
  return destroyed_;
}

void TestContextProvider::OnLostContext() {
  DCHECK(context_thread_checker_.CalledOnValidThread());
  {
    base::AutoLock lock(destroyed_lock_);
    if (destroyed_)
      return;
    destroyed_ = true;
  }
  if (!lost_context_callback_.is_null())
    base::ResetAndReturn(&lost_context_callback_).Run();
}

void TestContextProvider::OnSwapBuffersComplete() {
  DCHECK(context_thread_checker_.CalledOnValidThread());
  if (!swap_buffers_complete_callback_.is_null())
    swap_buffers_complete_callback_.Run();
}

TestWebGraphicsContext3D* TestContextProvider::TestContext3d() {
  DCHECK(context3d_);
  DCHECK(bound_);
  DCHECK(context_thread_checker_.CalledOnValidThread());

  return context3d_.get();
}

TestWebGraphicsContext3D* TestContextProvider::UnboundTestContext3d() {
  DCHECK(context3d_);

  return context3d_.get();
}

void TestContextProvider::SetMemoryAllocation(
    const ManagedMemoryPolicy& policy) {
  if (memory_policy_changed_callback_.is_null())
    return;
  memory_policy_changed_callback_.Run(policy);
}

void TestContextProvider::SetLostContextCallback(
    const LostContextCallback& cb) {
  DCHECK(context_thread_checker_.CalledOnValidThread());
  DCHECK(lost_context_callback_.is_null() || cb.is_null());
  lost_context_callback_ = cb;
}

void TestContextProvider::SetSwapBuffersCompleteCallback(
    const SwapBuffersCompleteCallback& cb) {
  DCHECK(context_thread_checker_.CalledOnValidThread());
  DCHECK(swap_buffers_complete_callback_.is_null() || cb.is_null());
  swap_buffers_complete_callback_ = cb;
}

void TestContextProvider::SetMemoryPolicyChangedCallback(
    const MemoryPolicyChangedCallback& cb) {
  DCHECK(context_thread_checker_.CalledOnValidThread());
  DCHECK(memory_policy_changed_callback_.is_null() || cb.is_null());
  memory_policy_changed_callback_ = cb;
}

void TestContextProvider::SetMaxTransferBufferUsageBytes(
    size_t max_transfer_buffer_usage_bytes) {
  context3d_->SetMaxTransferBufferUsageBytes(max_transfer_buffer_usage_bytes);
}

}  // namespace cc
