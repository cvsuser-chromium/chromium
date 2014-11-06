// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/ui/cocoa/screen_capture_notification_ui_cocoa.h"

#include "base/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/cocoa/cocoa_test_helper.h"

@interface ScreenCaptureNotificationController (ExposedForTesting)
- (NSButton*)stopButton;
@end

@implementation ScreenCaptureNotificationController (ExposedForTesting)
- (NSButton*)stopButton {
  return stopButton_;
}
@end

class ScreenCaptureNotificationUICocoaTest : public CocoaTest {
 public:
  ScreenCaptureNotificationUICocoaTest()
      : callback_called_(0) {
  }

  virtual void TearDown() OVERRIDE {
    callback_called_ = 0;
    target_.reset();
    EXPECT_EQ(0, callback_called_);

    CocoaTest::TearDown();
  }

  void StopCallback() {
    ++callback_called_;
  }

 protected:
  ScreenCaptureNotificationController* controller() {
    return target_->windowController_.get();
  }

  scoped_ptr<ScreenCaptureNotificationUICocoa> target_;
  int callback_called_;

  DISALLOW_COPY_AND_ASSIGN(ScreenCaptureNotificationUICocoaTest);
};

TEST_F(ScreenCaptureNotificationUICocoaTest, CreateAndDestroy) {
  target_.reset(
      new ScreenCaptureNotificationUICocoa(base::UTF8ToUTF16("Title")));
}

TEST_F(ScreenCaptureNotificationUICocoaTest, CreateAndStart) {
  target_.reset(
      new ScreenCaptureNotificationUICocoa(base::UTF8ToUTF16("Title")));
  target_->OnStarted(
      base::Bind(&ScreenCaptureNotificationUICocoaTest::StopCallback,
                 base::Unretained(this)));
}

TEST_F(ScreenCaptureNotificationUICocoaTest, LongTitle) {
  target_.reset(new ScreenCaptureNotificationUICocoa(base::UTF8ToUTF16(
      "Very long title, with very very very very very very very very "
      "very very very very very very very very very very very very many "
      "words")));
  target_->OnStarted(
      base::Bind(&ScreenCaptureNotificationUICocoaTest::StopCallback,
                 base::Unretained(this)));
  EXPECT_LE(NSWidth([[controller() window] frame]), 1000);
}

TEST_F(ScreenCaptureNotificationUICocoaTest, ShortTitle) {
  target_.reset(
      new ScreenCaptureNotificationUICocoa(base::UTF8ToUTF16("Title")));
  target_->OnStarted(
      base::Bind(&ScreenCaptureNotificationUICocoaTest::StopCallback,
                 base::Unretained(this)));
  EXPECT_EQ(460, NSWidth([[controller() window] frame]));
}

TEST_F(ScreenCaptureNotificationUICocoaTest, ClickStop) {
  target_.reset(
      new ScreenCaptureNotificationUICocoa(base::UTF8ToUTF16("Title")));
  target_->OnStarted(
      base::Bind(&ScreenCaptureNotificationUICocoaTest::StopCallback,
                 base::Unretained(this)));

  [[controller() stopButton] performClick:nil];
  EXPECT_EQ(1, callback_called_);
}

TEST_F(ScreenCaptureNotificationUICocoaTest, CloseWindow) {
  target_.reset(
      new ScreenCaptureNotificationUICocoa(base::UTF8ToUTF16("Title")));
  target_->OnStarted(
      base::Bind(&ScreenCaptureNotificationUICocoaTest::StopCallback,
                 base::Unretained(this)));

  [[controller() window] close];

  EXPECT_EQ(1, callback_called_);
}
