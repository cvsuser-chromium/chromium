// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/test/fake_layer_tree_host_impl.h"
#include "cc/trees/layer_tree_impl.h"

namespace cc {

FakeLayerTreeHostImpl::FakeLayerTreeHostImpl(Proxy* proxy)
    : LayerTreeHostImpl(LayerTreeSettings(),
                        &client_,
                        proxy,
                        &stats_instrumentation_,
                        NULL) {
  // Explicitly clear all debug settings.
  SetDebugState(LayerTreeDebugState());
}

FakeLayerTreeHostImpl::FakeLayerTreeHostImpl(const LayerTreeSettings& settings,
                                             Proxy* proxy)
    : LayerTreeHostImpl(settings,
                        &client_,
                        proxy,
                        &stats_instrumentation_,
                        NULL) {
  // Explicitly clear all debug settings.
  SetDebugState(LayerTreeDebugState());
}

FakeLayerTreeHostImpl::~FakeLayerTreeHostImpl() {}

void FakeLayerTreeHostImpl::CreatePendingTree() {
  LayerTreeHostImpl::CreatePendingTree();
  float arbitrary_large_page_scale = 100000.f;
  pending_tree()->SetPageScaleFactorAndLimits(
      1.f, 1.f / arbitrary_large_page_scale, arbitrary_large_page_scale);
}

base::TimeTicks FakeLayerTreeHostImpl::CurrentFrameTimeTicks() {
  if (current_frame_time_ticks_.is_null())
    return LayerTreeHostImpl::CurrentFrameTimeTicks();
  return current_frame_time_ticks_;
}

void FakeLayerTreeHostImpl::SetCurrentFrameTimeTicks(
    base::TimeTicks current_frame_time_ticks) {
  current_frame_time_ticks_ = current_frame_time_ticks;
}

}  // namespace cc
