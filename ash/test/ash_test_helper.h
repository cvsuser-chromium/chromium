// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_TEST_ASH_TEST_HELPER_H_
#define ASH_TEST_ASH_TEST_HELPER_H_

#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"

namespace aura {
class Window;
}  // namespace aura

namespace base {
class MessageLoopForUI;
}  // namespace base

namespace ui {
class ScopedAnimationDurationScaleMode;
}  // namespace ui

namespace ash {
namespace test {

class TestScreenshotDelegate;
class TestShellDelegate;

// A helper class that does common initialization required for Ash. Creates a
// root window and an ash::Shell instance with a test delegate.
class AshTestHelper {
 public:
  explicit AshTestHelper(base::MessageLoopForUI* message_loop);
  ~AshTestHelper();

  // Creates the ash::Shell and performs associated initialization.
  // Set |start_session| to true if the user should log in before
  // the test is run.
  void SetUp(bool start_session);

  // Destroys the ash::Shell and performs associated cleanup.
  void TearDown();

  // Returns a root Window. Usually this is the active root Window, but that
  // method can return NULL sometimes, and in those cases, we fall back on the
  // primary root Window.
  aura::Window* CurrentContext();

  void RunAllPendingInMessageLoop();

  base::MessageLoopForUI* message_loop() { return message_loop_; }
  TestShellDelegate* test_shell_delegate() { return test_shell_delegate_; }
  TestScreenshotDelegate* test_screenshot_delegate() {
    return test_screenshot_delegate_;
  }

 private:
  base::MessageLoopForUI* message_loop_;  // Not owned.
  TestShellDelegate* test_shell_delegate_;  // Owned by ash::Shell.
  scoped_ptr<ui::ScopedAnimationDurationScaleMode> zero_duration_mode_;

  // Owned by ash::AcceleratorController
  TestScreenshotDelegate* test_screenshot_delegate_;

  // true, if NetworkHandler was initialized by this instance.
  bool tear_down_network_handler_;

  DISALLOW_COPY_AND_ASSIGN(AshTestHelper);
};

}  // namespace test
}  // namespace ash

#endif  // ASH_TEST_ASH_TEST_HELPER_H_
