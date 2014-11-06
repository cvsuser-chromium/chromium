// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/app_list/app_list_controller_delegate_impl.h"

#include "chrome/browser/browser_process.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/app_list/app_list_service_impl.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_dialogs.h"
#include "chrome/browser/ui/extensions/application_launch.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/manifest_handlers/app_launch_info.h"
#include "net/base/url_util.h"
#include "ui/gfx/image/image_skia.h"

AppListControllerDelegateImpl::AppListControllerDelegateImpl(
    AppListService* service)
    : service_(service) {}

AppListControllerDelegateImpl::~AppListControllerDelegateImpl() {}

void AppListControllerDelegateImpl::DismissView() {
  service_->DismissAppList();
}

gfx::NativeWindow AppListControllerDelegateImpl::GetAppListWindow() {
  return service_->GetAppListWindow();
}

gfx::ImageSkia AppListControllerDelegateImpl::GetWindowIcon() {
  return gfx::ImageSkia();
}

bool AppListControllerDelegateImpl::IsAppPinned(
    const std::string& extension_id) {
  return false;
}

void AppListControllerDelegateImpl::PinApp(const std::string& extension_id) {
  NOTREACHED();
}

void AppListControllerDelegateImpl::UnpinApp(const std::string& extension_id) {
  NOTREACHED();
}

AppListControllerDelegateImpl::Pinnable
    AppListControllerDelegateImpl::GetPinnable() {
  return NO_PIN;
}

bool AppListControllerDelegateImpl::CanDoCreateShortcutsFlow() {
  return false;
}

void AppListControllerDelegateImpl::DoCreateShortcutsFlow(
    Profile* profile,
    const std::string& extension_id) {
  DCHECK(CanDoCreateShortcutsFlow());
  ExtensionService* service =
      extensions::ExtensionSystem::Get(profile)->extension_service();
  DCHECK(service);
  const extensions::Extension* extension = service->GetInstalledExtension(
      extension_id);
  DCHECK(extension);

  gfx::NativeWindow parent_window = GetAppListWindow();
  if (!parent_window)
    return;
  OnShowExtensionPrompt();
  chrome::ShowCreateChromeAppShortcutsDialog(
      parent_window, profile, extension,
      base::Bind(&AppListControllerDelegateImpl::OnCloseExtensionPrompt,
                 base::Unretained(this)));
}

void AppListControllerDelegateImpl::CreateNewWindow(Profile* profile,
                                                   bool incognito) {
  Profile* window_profile = incognito ?
      profile->GetOffTheRecordProfile() : profile;
  chrome::NewEmptyWindow(window_profile, chrome::HOST_DESKTOP_TYPE_NATIVE);
}

void AppListControllerDelegateImpl::ActivateApp(
    Profile* profile,
    const extensions::Extension* extension,
    AppListSource source,
    int event_flags) {
  LaunchApp(profile, extension, source, event_flags);
}

void AppListControllerDelegateImpl::LaunchApp(
    Profile* profile,
    const extensions::Extension* extension,
    AppListSource source,
    int event_flags) {
  AppListServiceImpl::RecordAppListAppLaunch();

  AppLaunchParams params(profile, extension, NEW_FOREGROUND_TAB);
  params.desktop_type = chrome::HOST_DESKTOP_TYPE_NATIVE;

  if (source != LAUNCH_FROM_UNKNOWN &&
      extension->id() == extension_misc::kWebStoreAppId) {
    // Set an override URL to include the source.
    GURL extension_url = extensions::AppLaunchInfo::GetFullLaunchURL(extension);
    params.override_url = net::AppendQueryParameter(
        extension_url,
        extension_urls::kWebstoreSourceField,
        AppListSourceToString(source));
  }

  OpenApplication(params);
}

void AppListControllerDelegateImpl::ShowForProfileByPath(
    const base::FilePath& profile_path) {
  service_->SetProfilePath(profile_path);
  service_->Show();
}

bool AppListControllerDelegateImpl::ShouldShowUserIcon() {
  return g_browser_process->profile_manager()->GetNumberOfProfiles() > 1;
}
