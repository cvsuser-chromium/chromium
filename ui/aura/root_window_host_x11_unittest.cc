// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "base/sys_info.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/aura/root_window.h"
#include "ui/aura/root_window_host_delegate.h"
#include "ui/aura/root_window_host_x11.h"
#include "ui/aura/test/aura_test_base.h"
#include "ui/events/x/events_x_utils.h"

namespace {
class TestRootWindowHostDelegate : public aura::RootWindowHostDelegate {
 public:
  TestRootWindowHostDelegate() : last_touch_type_(ui::ET_UNKNOWN),
                                 last_touch_id_(-1),
                                 last_touch_location_(0, 0) {
  }
  virtual ~TestRootWindowHostDelegate() {}

  virtual bool OnHostKeyEvent(ui::KeyEvent* event) OVERRIDE {
    return true;
  }

  virtual bool OnHostMouseEvent(ui::MouseEvent* event) OVERRIDE {
    return true;
  }
  virtual bool OnHostScrollEvent(ui::ScrollEvent* event) OVERRIDE {
    return true;
  }

  virtual bool OnHostTouchEvent(ui::TouchEvent* event) OVERRIDE {
    last_touch_id_ = event->touch_id();
    last_touch_type_ = event->type();
    last_touch_location_ = event->location();
    return true;
  }

  virtual void OnHostCancelMode() OVERRIDE {}

  // Called when the windowing system activates the window.
  virtual void OnHostActivated() OVERRIDE {}

  // Called when system focus is changed to another window.
  virtual void OnHostLostWindowCapture() OVERRIDE {}

  // Called when the windowing system has mouse grab because it's performing a
  // window move on our behalf, but we should still paint as if we're active.
  virtual void OnHostLostMouseGrab() OVERRIDE {}

  virtual void OnHostPaint(const gfx::Rect& damage_rect) OVERRIDE {}

  virtual void OnHostMoved(const gfx::Point& origin) OVERRIDE {}
  virtual void OnHostResized(const gfx::Size& size) OVERRIDE {}

  virtual float GetDeviceScaleFactor() OVERRIDE {
    return 1.0f;
  }

  virtual aura::RootWindow* AsRootWindow() OVERRIDE {
    return NULL;
  }

  ui::EventType last_touch_type() {
    return last_touch_type_;
  }

  int last_touch_id() {
    return last_touch_id_;
  }

  gfx::Point last_touch_location() {
    return last_touch_location_;
  }

 private:
  ui::EventType last_touch_type_;
  int last_touch_id_;
  gfx::Point last_touch_location_;

  DISALLOW_COPY_AND_ASSIGN(TestRootWindowHostDelegate);
};

}  // namespace

namespace aura {

typedef test::AuraTestBase RootWindowHostX11Test;

// Send X touch events to one RootWindowHost. The RootWindowHost's
// delegate will get corresponding ui::TouchEvent if the touch events
// are winthin the bound of the RootWindowHost.
TEST_F(RootWindowHostX11Test, DispatchTouchEventToOneRootWindow) {
#if defined(OS_CHROMEOS)
  // Fake a ChromeOS running env.
  const char* kLsbRelease = "CHROMEOS_RELEASE_NAME=Chromium OS\n";
  base::SysInfo::SetChromeOSVersionInfoForTest(kLsbRelease, base::Time());
#endif  // defined(OS_CHROMEOS)

  scoped_ptr<RootWindowHostX11> root_window_host(
      new RootWindowHostX11(gfx::Rect(0, 0, 2560, 1700)));
  scoped_ptr<TestRootWindowHostDelegate> delegate(
      new TestRootWindowHostDelegate());
  root_window_host->SetDelegate(delegate.get());

  std::vector<unsigned int> devices;
  devices.push_back(0);
  ui::SetupTouchDevicesForTest(devices);
  std::vector<ui::Valuator> valuators;

  EXPECT_EQ(ui::ET_UNKNOWN, delegate->last_touch_type());
  EXPECT_EQ(-1, delegate->last_touch_id());

#if defined(OS_CHROMEOS)
  // This touch is out of bounds.
  ui::XScopedTouchEvent event1(ui::CreateTouchEvent(
      0, XI_TouchBegin, 5, gfx::Point(1500, 2500), valuators));
  root_window_host->Dispatch(event1);
  EXPECT_EQ(ui::ET_UNKNOWN, delegate->last_touch_type());
  EXPECT_EQ(-1, delegate->last_touch_id());
  EXPECT_EQ(gfx::Point(0, 0), delegate->last_touch_location());
#endif  // defined(OS_CHROMEOS)

  // Following touchs are within bounds and are passed to delegate.
  ui::XScopedTouchEvent event2(ui::CreateTouchEvent(
      0, XI_TouchBegin, 5, gfx::Point(1500, 1500), valuators));
  root_window_host->Dispatch(event2);
  EXPECT_EQ(ui::ET_TOUCH_PRESSED, delegate->last_touch_type());
  EXPECT_EQ(0, delegate->last_touch_id());
  EXPECT_EQ(gfx::Point(1500, 1500), delegate->last_touch_location());

  ui::XScopedTouchEvent event3(ui::CreateTouchEvent(
      0, XI_TouchUpdate, 5, gfx::Point(1500, 1600), valuators));
  root_window_host->Dispatch(event3);
  EXPECT_EQ(ui::ET_TOUCH_MOVED, delegate->last_touch_type());
  EXPECT_EQ(0, delegate->last_touch_id());
  EXPECT_EQ(gfx::Point(1500, 1600), delegate->last_touch_location());

  ui::XScopedTouchEvent event4(ui::CreateTouchEvent(
      0, XI_TouchEnd, 5, gfx::Point(1500, 1600), valuators));
  root_window_host->Dispatch(event4);
  EXPECT_EQ(ui::ET_TOUCH_RELEASED, delegate->last_touch_type());
  EXPECT_EQ(0, delegate->last_touch_id());
  EXPECT_EQ(gfx::Point(1500, 1600), delegate->last_touch_location());

  // Revert the CrOS testing env otherwise the following non-CrOS aura
  // tests will fail.
#if defined(OS_CHROMEOS)
  // Fake a ChromeOS running env.
  kLsbRelease = "";
  base::SysInfo::SetChromeOSVersionInfoForTest(kLsbRelease, base::Time());
#endif  // defined(OS_CHROMEOS)
}

// Send X touch events to two RootWindowHost. The RootWindowHost which is
// the event target of the X touch events should generate the corresponding
// ui::TouchEvent for its delegate.
#if defined(OS_CHROMEOS)
TEST_F(RootWindowHostX11Test, DispatchTouchEventToTwoRootWindow) {
  // Fake a ChromeOS running env.
  const char* kLsbRelease = "CHROMEOS_RELEASE_NAME=Chromium OS\n";
  base::SysInfo::SetChromeOSVersionInfoForTest(kLsbRelease, base::Time());

  scoped_ptr<RootWindowHostX11> root_window_host1(
      new RootWindowHostX11(gfx::Rect(0, 0, 2560, 1700)));
  scoped_ptr<TestRootWindowHostDelegate> delegate1(
      new TestRootWindowHostDelegate());
  root_window_host1->SetDelegate(delegate1.get());

  int host2_y_offset = 1700;
  scoped_ptr<RootWindowHostX11> root_window_host2(
      new RootWindowHostX11(gfx::Rect(0, host2_y_offset, 1920, 1080)));
  scoped_ptr<TestRootWindowHostDelegate> delegate2(
      new TestRootWindowHostDelegate());
  root_window_host2->SetDelegate(delegate2.get());

  std::vector<unsigned int> devices;
  devices.push_back(0);
  ui::SetupTouchDevicesForTest(devices);
  std::vector<ui::Valuator> valuators;

  EXPECT_EQ(ui::ET_UNKNOWN, delegate1->last_touch_type());
  EXPECT_EQ(-1, delegate1->last_touch_id());
  EXPECT_EQ(ui::ET_UNKNOWN, delegate2->last_touch_type());
  EXPECT_EQ(-1, delegate2->last_touch_id());

  // 2 Touch events are targeted at the second RootWindowHost.
  ui::XScopedTouchEvent touch1_begin(ui::CreateTouchEvent(
      0, XI_TouchBegin, 5, gfx::Point(1500, 2500), valuators));
  root_window_host1->Dispatch(touch1_begin);
  root_window_host2->Dispatch(touch1_begin);
  EXPECT_EQ(ui::ET_UNKNOWN, delegate1->last_touch_type());
  EXPECT_EQ(-1, delegate1->last_touch_id());
  EXPECT_EQ(gfx::Point(0, 0), delegate1->last_touch_location());
  EXPECT_EQ(ui::ET_TOUCH_PRESSED, delegate2->last_touch_type());
  EXPECT_EQ(0, delegate2->last_touch_id());
  EXPECT_EQ(gfx::Point(1500, 2500 - host2_y_offset),
            delegate2->last_touch_location());

  ui::XScopedTouchEvent touch2_begin(ui::CreateTouchEvent(
      0, XI_TouchBegin, 6, gfx::Point(1600, 2600), valuators));
  root_window_host1->Dispatch(touch2_begin);
  root_window_host2->Dispatch(touch2_begin);
  EXPECT_EQ(ui::ET_UNKNOWN, delegate1->last_touch_type());
  EXPECT_EQ(-1, delegate1->last_touch_id());
  EXPECT_EQ(gfx::Point(0, 0), delegate1->last_touch_location());
  EXPECT_EQ(ui::ET_TOUCH_PRESSED, delegate2->last_touch_type());
  EXPECT_EQ(1, delegate2->last_touch_id());
  EXPECT_EQ(gfx::Point(1600, 2600 - host2_y_offset),
            delegate2->last_touch_location());

  ui::XScopedTouchEvent touch1_move(ui::CreateTouchEvent(
      0, XI_TouchUpdate, 5, gfx::Point(1500, 2550), valuators));
  root_window_host1->Dispatch(touch1_move);
  root_window_host2->Dispatch(touch1_move);
  EXPECT_EQ(ui::ET_UNKNOWN, delegate1->last_touch_type());
  EXPECT_EQ(-1, delegate1->last_touch_id());
  EXPECT_EQ(gfx::Point(0, 0), delegate1->last_touch_location());
  EXPECT_EQ(ui::ET_TOUCH_MOVED, delegate2->last_touch_type());
  EXPECT_EQ(0, delegate2->last_touch_id());
  EXPECT_EQ(gfx::Point(1500, 2550 - host2_y_offset),
            delegate2->last_touch_location());

  ui::XScopedTouchEvent touch2_move(ui::CreateTouchEvent(
      0, XI_TouchUpdate, 6, gfx::Point(1600, 2650), valuators));
  root_window_host1->Dispatch(touch2_move);
  root_window_host2->Dispatch(touch2_move);
  EXPECT_EQ(ui::ET_UNKNOWN, delegate1->last_touch_type());
  EXPECT_EQ(-1, delegate1->last_touch_id());
  EXPECT_EQ(gfx::Point(0, 0), delegate1->last_touch_location());
  EXPECT_EQ(ui::ET_TOUCH_MOVED, delegate2->last_touch_type());
  EXPECT_EQ(1, delegate2->last_touch_id());
  EXPECT_EQ(gfx::Point(1600, 2650 - host2_y_offset),
            delegate2->last_touch_location());

  ui::XScopedTouchEvent touch1_end(ui::CreateTouchEvent(
      0, XI_TouchEnd, 5, gfx::Point(1500, 2550), valuators));
  root_window_host1->Dispatch(touch1_end);
  root_window_host2->Dispatch(touch1_end);
  EXPECT_EQ(ui::ET_UNKNOWN, delegate1->last_touch_type());
  EXPECT_EQ(-1, delegate1->last_touch_id());
  EXPECT_EQ(gfx::Point(0, 0), delegate1->last_touch_location());
  EXPECT_EQ(ui::ET_TOUCH_RELEASED, delegate2->last_touch_type());
  EXPECT_EQ(0, delegate2->last_touch_id());
  EXPECT_EQ(gfx::Point(1500, 2550 - host2_y_offset),
            delegate2->last_touch_location());

  ui::XScopedTouchEvent touch2_end(ui::CreateTouchEvent(
      0, XI_TouchEnd, 6, gfx::Point(1600, 2650), valuators));
  root_window_host1->Dispatch(touch2_end);
  root_window_host2->Dispatch(touch2_end);
  EXPECT_EQ(ui::ET_UNKNOWN, delegate1->last_touch_type());
  EXPECT_EQ(-1, delegate1->last_touch_id());
  EXPECT_EQ(gfx::Point(0, 0), delegate1->last_touch_location());
  EXPECT_EQ(ui::ET_TOUCH_RELEASED, delegate2->last_touch_type());
  EXPECT_EQ(1, delegate2->last_touch_id());
  EXPECT_EQ(gfx::Point(1600, 2650 - host2_y_offset),
            delegate2->last_touch_location());

  // Revert the CrOS testing env otherwise the following non-CrOS aura
  // tests will fail.
  // Fake a ChromeOS running env.
  kLsbRelease = "";
  base::SysInfo::SetChromeOSVersionInfoForTest(kLsbRelease, base::Time());
}
#endif  // defined(OS_CHROMEOS)

}  // namespace aura
