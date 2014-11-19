// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/services/native_viewport/native_viewport.h"

#import <AppKit/NSApplication.h>
#import <AppKit/NSView.h>
#import <AppKit/NSWindow.h>

#include "base/bind.h"
#include "ui/gfx/rect.h"

namespace mojo {
namespace services {

class NativeViewportMac : public NativeViewport {
 public:
  NativeViewportMac(NativeViewportDelegate* delegate)
      : delegate_(delegate),
        window_(nil),
        rect_(10, 10, 500, 500) {
  }

  virtual ~NativeViewportMac() {
    [window_ orderOut:nil];
    [window_ close];
  }

 private:
  // Overridden from NativeViewport:
  virtual gfx::Size GetSize() OVERRIDE {
    return rect_.size();
  }

  virtual void Init() OVERRIDE {
    [NSApplication sharedApplication];

    window_ = [[NSWindow alloc]
                  initWithContentRect:NSRectFromCGRect(rect_.ToCGRect())
                            styleMask:NSTitledWindowMask
                              backing:NSBackingStoreBuffered
                                defer:NO];
    [window_ orderFront:nil];
    delegate_->OnAcceleratedWidgetAvailable([window_ contentView]);
  }

  virtual void Close() OVERRIDE {
    // TODO(beng): perform this in response to NSWindow destruction.
    delegate_->OnDestroyed();
  }

  NativeViewportDelegate* delegate_;
  NSWindow* window_;
  gfx::Rect rect_;

  DISALLOW_COPY_AND_ASSIGN(NativeViewportMac);
};

// static
scoped_ptr<NativeViewport> NativeViewport::Create(
    shell::Context* context,
    NativeViewportDelegate* delegate) {
  return scoped_ptr<NativeViewport>(new NativeViewportMac(delegate)).Pass();
}

}  // namespace services
}  // namespace mojo
