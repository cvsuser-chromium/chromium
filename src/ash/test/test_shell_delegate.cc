// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/test/test_shell_delegate.h"

#include <limits>

#include "ash/caps_lock_delegate_stub.h"
#include "ash/default_accessibility_delegate.h"
#include "ash/host/root_window_host_factory.h"
#include "ash/keyboard_controller_proxy_stub.h"
#include "ash/media_delegate.h"
#include "ash/new_window_delegate.h"
#include "ash/session_state_delegate.h"
#include "ash/shell.h"
#include "ash/shell_window_ids.h"
#include "ash/test/test_launcher_delegate.h"
#include "ash/test/test_session_state_delegate.h"
#include "ash/test/test_system_tray_delegate.h"
#include "ash/test/test_user_wallpaper_delegate.h"
#include "ash/wm/window_state.h"
#include "ash/wm/window_util.h"
#include "base/logging.h"
#include "content/public/test/test_browser_context.h"
#include "ui/aura/window.h"

namespace ash {
namespace test {
namespace {

class NewWindowDelegateImpl : public NewWindowDelegate {
  virtual void NewTab() OVERRIDE {}
  virtual void NewWindow(bool incognito) OVERRIDE {}
  virtual void OpenFileManager() OVERRIDE {}
  virtual void OpenCrosh() OVERRIDE {}
  virtual void RestoreTab() OVERRIDE {}
  virtual void ShowKeyboardOverlay() OVERRIDE {}
  virtual void ShowTaskManager() OVERRIDE {}
  virtual void OpenFeedbackPage() OVERRIDE {}
};

class MediaDelegateImpl : public MediaDelegate {
 public:
  virtual void HandleMediaNextTrack() OVERRIDE {}
  virtual void HandleMediaPlayPause() OVERRIDE {}
  virtual void HandleMediaPrevTrack() OVERRIDE {}
};

}  // namespace

TestShellDelegate::TestShellDelegate()
    : num_exit_requests_(0),
      multi_profiles_enabled_(false),
      test_session_state_delegate_(NULL) {
}

TestShellDelegate::~TestShellDelegate() {
}

bool TestShellDelegate::IsFirstRunAfterBoot() const {
  return false;
}

bool TestShellDelegate::IsIncognitoAllowed() const {
  return true;
}

bool TestShellDelegate::IsMultiProfilesEnabled() const {
  return multi_profiles_enabled_;
}

bool TestShellDelegate::IsRunningInForcedAppMode() const {
  return false;
}

void TestShellDelegate::PreInit() {
}

void TestShellDelegate::Shutdown() {
}

void TestShellDelegate::Exit() {
  num_exit_requests_++;
}

keyboard::KeyboardControllerProxy*
    TestShellDelegate::CreateKeyboardControllerProxy() {
  return new KeyboardControllerProxyStub();
}

content::BrowserContext* TestShellDelegate::GetCurrentBrowserContext() {
  current_browser_context_.reset(new content::TestBrowserContext());
  return current_browser_context_.get();
}

app_list::AppListViewDelegate* TestShellDelegate::CreateAppListViewDelegate() {
  return NULL;
}

LauncherDelegate* TestShellDelegate::CreateLauncherDelegate(
    ash::LauncherModel* model) {
  return new TestLauncherDelegate(model);
}

SystemTrayDelegate* TestShellDelegate::CreateSystemTrayDelegate() {
  return new TestSystemTrayDelegate;
}

UserWallpaperDelegate* TestShellDelegate::CreateUserWallpaperDelegate() {
  return new TestUserWallpaperDelegate();
}

CapsLockDelegate* TestShellDelegate::CreateCapsLockDelegate() {
  return new CapsLockDelegateStub;
}

SessionStateDelegate* TestShellDelegate::CreateSessionStateDelegate() {
  DCHECK(!test_session_state_delegate_);
  test_session_state_delegate_ = new TestSessionStateDelegate();
  return test_session_state_delegate_;
}

AccessibilityDelegate* TestShellDelegate::CreateAccessibilityDelegate() {
  return new internal::DefaultAccessibilityDelegate();
}

NewWindowDelegate* TestShellDelegate::CreateNewWindowDelegate() {
  return new NewWindowDelegateImpl;
}

MediaDelegate* TestShellDelegate::CreateMediaDelegate() {
  return new MediaDelegateImpl;
}

aura::client::UserActionClient* TestShellDelegate::CreateUserActionClient() {
  return NULL;
}

void TestShellDelegate::RecordUserMetricsAction(UserMetricsAction action) {
}

ui::MenuModel* TestShellDelegate::CreateContextMenu(aura::Window* root) {
  return NULL;
}

RootWindowHostFactory* TestShellDelegate::CreateRootWindowHostFactory() {
  return RootWindowHostFactory::Create();
}

base::string16 TestShellDelegate::GetProductName() const {
  return base::string16();
}

TestSessionStateDelegate* TestShellDelegate::test_session_state_delegate() {
  return test_session_state_delegate_;
}

}  // namespace test
}  // namespace ash
