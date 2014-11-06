// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/compositor/test/test_compositor_host.h"

#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "ui/compositor/compositor.h"
#include "ui/gfx/win/window_impl.h"

namespace ui {

class TestCompositorHostWin : public TestCompositorHost,
                              public gfx::WindowImpl {
 public:
  TestCompositorHostWin(const gfx::Rect& bounds) {
    Init(NULL, bounds);
    compositor_.reset(new ui::Compositor(hwnd()));
    compositor_->SetScaleAndSize(1.0f, GetSize());
  }

  virtual ~TestCompositorHostWin() {
    DestroyWindow(hwnd());
  }

  // Overridden from TestCompositorHost:
  virtual void Show() OVERRIDE {
    ShowWindow(hwnd(), SW_SHOWNORMAL);
  }
  virtual ui::Compositor* GetCompositor() OVERRIDE {
    return compositor_.get();
  }

 private:
  BEGIN_MSG_MAP_EX(TestCompositorHostWin)
    MSG_WM_PAINT(OnPaint)
  END_MSG_MAP()

  void OnPaint(HDC dc) {
    compositor_->Draw();
    ValidateRect(hwnd(), NULL);
  }

  gfx::Size GetSize() {
    RECT r;
    GetClientRect(hwnd(), &r);
    return gfx::Rect(r).size();
  }

  scoped_ptr<ui::Compositor> compositor_;

  DISALLOW_COPY_AND_ASSIGN(TestCompositorHostWin);
};

TestCompositorHost* TestCompositorHost::Create(const gfx::Rect& bounds) {
  return new TestCompositorHostWin(bounds);
}

}  // namespace ui
