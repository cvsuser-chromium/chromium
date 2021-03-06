// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/ui/cocoa/autofill/autofill_error_bubble_controller.h"

#import "chrome/browser/ui/cocoa/cocoa_test_helper.h"
#include "chrome/browser/ui/cocoa/run_loop_testing.h"

class AutofillErrorBubbleControllerTest : public CocoaTest {
};

TEST_F(AutofillErrorBubbleControllerTest, ShowAndClose) {
  AutofillErrorBubbleController* controller =
      [[AutofillErrorBubbleController alloc] initWithParentWindow:test_window()
                                                          message:@"test msg"];
  EXPECT_FALSE([[controller window] isVisible]);

  [controller showWindow:nil];
  EXPECT_TRUE([[controller window] isVisible]);

  // Close will self-delete, but all pending messages must be processed so the
  // deallocation happens.
  [controller close];
  chrome::testing::NSRunLoopRunAllPending();
}
