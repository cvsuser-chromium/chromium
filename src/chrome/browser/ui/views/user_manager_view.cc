// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/user_manager_view.h"

#include "base/strings/string_number_conversions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_dialogs.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_view.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

#if defined(USE_ASH)
#include "ash/wm/window_util.h"
#endif

#if defined(OS_WIN)
#include "chrome/browser/shell_integration.h"
#include "ui/base/win/shell.h"
#include "ui/views/win/hwnd_util.h"
#include "win8/util/win8_util.h"
#endif

namespace {

// Default window size.
const int kWindowWidth = 900;
const int kWindowHeight = 700;

}

namespace chrome {

// Declared in browser_dialogs.h so others don't have to depend on this header.
void ShowUserManager(const base::FilePath& profile_path_to_focus) {
  UserManagerView::Show(profile_path_to_focus);
}

void HideUserManager() {
  UserManagerView::Hide();
}

}  // namespace chrome

// static
UserManagerView* UserManagerView::instance_ = NULL;

UserManagerView::UserManagerView(Profile* profile)
    : web_view_(new views::WebView(profile)) {
  SetLayoutManager(new views::FillLayout);
  AddChildView(web_view_);
}

UserManagerView::~UserManagerView() {
  chrome::EndKeepAlive();  // Remove shutdown prevention.
}

// static
void UserManagerView::Show(const base::FilePath& profile_path_to_focus) {
  // Prevent the browser process from shutting down while this window is open.
  chrome::StartKeepAlive();

  if (instance_) {
    // If there's a user manager window open already, just activate it.
    instance_->GetWidget()->Activate();
    return;
  }

  // Create the guest profile, if necessary, and open the user manager
  // from the guest profile.
  ProfileManager* profile_manager = g_browser_process->profile_manager();
  profile_manager->CreateProfileAsync(
      ProfileManager::GetGuestProfilePath(),
      base::Bind(&UserManagerView::OnGuestProfileCreated,
                 profile_path_to_focus),
      string16(),
      string16(),
      std::string());
}

// static
void UserManagerView::Hide() {
  if (instance_)
    instance_->GetWidget()->Close();
}

// static
bool UserManagerView::IsShowing() {
  return instance_ ? instance_->GetWidget()->IsActive() : false;
}

void UserManagerView::OnGuestProfileCreated(
    const base::FilePath& profile_path_to_focus,
    Profile* guest_profile,
    Profile::CreateStatus status) {
  if (status != Profile::CREATE_STATUS_INITIALIZED)
    return;

  instance_ = new UserManagerView(guest_profile);
  DialogDelegate::CreateDialogWidget(instance_, NULL, NULL);

#if defined(OS_WIN)
  // Set the app id for the task manager to the app id of its parent
  ui::win::SetAppIdForWindow(
      ShellIntegration::GetChromiumModelIdForProfile(
          guest_profile->GetPath()),
      views::HWNDForWidget(instance_->GetWidget()));
#endif
  instance_->GetWidget()->Show();

  // Tell the webui which user pod should be focused.
  std::string page = chrome::kChromeUIUserManagerURL;

  if (!profile_path_to_focus.empty()) {
    ProfileInfoCache& cache =
        g_browser_process->profile_manager()->GetProfileInfoCache();
    size_t index = cache.GetIndexOfProfileWithPath(profile_path_to_focus);
    if (index != std::string::npos) {
      page += "#";
      page += base::IntToString(index);
    }
  }

  instance_->web_view_->LoadInitialURL(GURL(page));
  instance_->web_view_->RequestFocus();
}

gfx::Size UserManagerView::GetPreferredSize() {
  return gfx::Size(kWindowWidth, kWindowHeight);
}

bool UserManagerView::CanResize() const {
  return true;
}

bool UserManagerView::CanMaximize() const {
  return true;
}

string16 UserManagerView::GetWindowTitle() const {
  return l10n_util::GetStringUTF16(IDS_USER_MANAGER_SCREEN_TITLE);
}

int UserManagerView::GetDialogButtons() const {
  return ui::DIALOG_BUTTON_NONE;
}

void UserManagerView::WindowClosing() {
  // Now that the window is closed, we can allow a new one to be opened.
  // (WindowClosing comes in asynchronously from the call to Close() and we
  // may have already opened a new instance).
  if (instance_ == this)
    instance_ = NULL;
}

bool UserManagerView::UseNewStyleForThisDialog() const {
  return false;
}
