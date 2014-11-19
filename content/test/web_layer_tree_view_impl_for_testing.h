// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_TEST_WEB_LAYER_TREE_VIEW_IMPL_FOR_TESTING_H_
#define CONTENT_TEST_WEB_LAYER_TREE_VIEW_IMPL_FOR_TESTING_H_

#include "base/memory/scoped_ptr.h"
#include "cc/trees/layer_tree_host_client.h"
#include "cc/trees/layer_tree_host_single_thread_client.h"
#include "third_party/WebKit/public/platform/WebLayerTreeView.h"

namespace cc {
class LayerTreeHost;
}

namespace blink { class WebLayer; }

namespace webkit {

class WebLayerTreeViewImplForTesting
    : public blink::WebLayerTreeView,
      public cc::LayerTreeHostClient,
      public cc::LayerTreeHostSingleThreadClient {
 public:
  WebLayerTreeViewImplForTesting();
  virtual ~WebLayerTreeViewImplForTesting();

  bool Initialize();

  // blink::WebLayerTreeView implementation.
  virtual void setSurfaceReady();
  virtual void setRootLayer(const blink::WebLayer& layer);
  virtual void clearRootLayer();
  virtual void setViewportSize(const blink::WebSize& unused_deprecated,
                               const blink::WebSize& device_viewport_size);
  virtual blink::WebSize layoutViewportSize() const;
  virtual blink::WebSize deviceViewportSize() const;
  virtual void setDeviceScaleFactor(float scale_factor);
  virtual float deviceScaleFactor() const;
  virtual void setBackgroundColor(blink::WebColor);
  virtual void setHasTransparentBackground(bool transparent);
  virtual void setVisible(bool visible);
  virtual void setPageScaleFactorAndLimits(float page_scale_factor,
                                           float minimum,
                                           float maximum);
  virtual void startPageScaleAnimation(const blink::WebPoint& destination,
                                       bool use_anchor,
                                       float new_page_scale,
                                       double duration_sec);
  virtual void setNeedsAnimate();
  virtual void setNeedsRedraw();
  virtual bool commitRequested() const;
  virtual void composite();
  virtual void didStopFlinging();
  virtual bool compositeAndReadback(void* pixels, const blink::WebRect& rect);
  virtual void finishAllRendering();
  virtual void setDeferCommits(bool defer_commits);
  virtual void renderingStats(
      blink::WebRenderingStats& stats) const;  // NOLINT(runtime/references)

  // cc::LayerTreeHostClient implementation.
  virtual void WillBeginMainFrame() OVERRIDE {}
  virtual void DidBeginMainFrame() OVERRIDE {}
  virtual void Animate(double frame_begin_time) OVERRIDE {}
  virtual void Layout() OVERRIDE;
  virtual void ApplyScrollAndScale(gfx::Vector2d scroll_delta, float page_scale)
      OVERRIDE;
  virtual scoped_ptr<cc::OutputSurface> CreateOutputSurface(bool fallback)
      OVERRIDE;
  virtual void DidInitializeOutputSurface(bool success) OVERRIDE {}
  virtual void WillCommit() OVERRIDE {}
  virtual void DidCommit() OVERRIDE {}
  virtual void DidCommitAndDrawFrame() OVERRIDE {}
  virtual void DidCompleteSwapBuffers() OVERRIDE {}
  virtual scoped_refptr<cc::ContextProvider>
      OffscreenContextProvider() OVERRIDE;

  // cc::LayerTreeHostSingleThreadClient implementation.
  virtual void ScheduleComposite() OVERRIDE {}
  virtual void DidPostSwapBuffers() OVERRIDE {}
  virtual void DidAbortSwapBuffers() OVERRIDE {}

 private:
  scoped_ptr<cc::LayerTreeHost> layer_tree_host_;

  DISALLOW_COPY_AND_ASSIGN(WebLayerTreeViewImplForTesting);
};

}  // namespace webkit

#endif  // CONTENT_TEST_WEB_LAYER_TREE_VIEW_IMPL_FOR_TESTING_H_
