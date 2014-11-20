// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_TEST_TEST_SHELL_DELEGATE_H_
#define ASH_TEST_TEST_SHELL_DELEGATE_H_

#include <string>

#include "ash/shell_delegate.h"
#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"

namespace keyboard {
class KeyboardControllerProxy;
}

namespace ash {
namespace test {

class TestSessionStateDelegate;

class TestShellDelegate : public ShellDelegate {
 public:
  TestShellDelegate();
  virtual ~TestShellDelegate();

  void set_multi_profiles_enabled(bool multi_profiles_enabled) {
    multi_profiles_enabled_ = multi_profiles_enabled;
  }

  // Overridden from ShellDelegate:
  virtual bool IsFirstRunAfterBoot() const OVERRIDE;
  virtual bool IsIncognitoAllowed() const OVERRIDE;
  virtual bool IsMultiProfilesEnabled() const OVERRIDE;
  virtual bool IsRunningInForcedAppMode() const OVERRIDE;
  virtual void PreInit() OVERRIDE;
  virtual void Shutdown() OVERRIDE;
  virtual void Exit() OVERRIDE;
  virtual keyboard::KeyboardControllerProxy*
      CreateKeyboardControllerProxy() OVERRIDE;
  virtual content::BrowserContext* GetCurrentBrowserContext() OVERRIDE;
  virtual app_list::AppListViewDelegate* CreateAppListViewDelegate() OVERRIDE;
  virtual LauncherDelegate* CreateLauncherDelegate(
      ash::LauncherModel* model) OVERRIDE;
  virtual SystemTrayDelegate* CreateSystemTrayDelegate() OVERRIDE;
  virtual UserWallpaperDelegate* CreateUserWallpaperDelegate() OVERRIDE;
  virtual CapsLockDelegate* CreateCapsLockDelegate() OVERRIDE;
  virtual SessionStateDelegate* CreateSessionStateDelegate() OVERRIDE;
  virtual AccessibilityDelegate* CreateAccessibilityDelegate() OVERRIDE;
  virtual NewWindowDelegate* CreateNewWindowDelegate() OVERRIDE;
  virtual MediaDelegate* CreateMediaDelegate() OVERRIDE;
  virtual aura::client::UserActionClient* CreateUserActionClient() OVERRIDE;
  virtual void RecordUserMetricsAction(UserMetricsAction action) OVERRIDE;
  virtual ui::MenuModel* CreateContextMenu(aura::Window* root) OVERRIDE;
  virtual RootWindowHostFactory* CreateRootWindowHostFactory() OVERRIDE;
  virtual base::string16 GetProductName() const OVERRIDE;

  int num_exit_requests() const { return num_exit_requests_; }

  TestSessionStateDelegate* test_session_state_delegate();

 private:
  int num_exit_requests_;
  bool multi_profiles_enabled_;

  scoped_ptr<content::BrowserContext> current_browser_context_;

  TestSessionStateDelegate* test_session_state_delegate_;  // Not owned.

  DISALLOW_COPY_AND_ASSIGN(TestShellDelegate);
};

}  // namespace test
}  // namespace ash

#endif  // ASH_TEST_TEST_SHELL_DELEGATE_H_
