// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/shell_window.h"
#include "apps/shell_window_registry.h"
#include "base/bind.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_metrics.h"
#include "chrome/browser/profiles/profile_window.h"
#include "chrome/browser/profiles/profiles_state.h"
#include "chrome/browser/ui/browser_finder.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"

namespace options {
namespace helper {

chrome::HostDesktopType GetDesktopType(content::WebUI* web_ui) {
  DCHECK(web_ui);
  content::WebContents* web_contents = web_ui->GetWebContents();
  Browser* browser = chrome::FindBrowserWithWebContents(web_contents);
  if (browser)
    return browser->host_desktop_type();

  apps::ShellWindow* shell_window =
      apps::ShellWindowRegistry::Get(Profile::FromWebUI(web_ui))->
          GetShellWindowForRenderViewHost(web_contents->GetRenderViewHost());
  if (shell_window) {
    return chrome::GetHostDesktopTypeForNativeWindow(
        shell_window->GetNativeWindow());
  }

  return chrome::GetActiveDesktop();
}

void OpenNewWindowForProfile(chrome::HostDesktopType desktop_type,
                             Profile* profile,
                             Profile::CreateStatus status) {
  if (status != Profile::CREATE_STATUS_INITIALIZED)
    return;

  profiles::FindOrCreateNewWindowForProfile(
    profile,
    chrome::startup::IS_PROCESS_STARTUP,
    chrome::startup::IS_FIRST_RUN,
    desktop_type,
    false);
}

void DeleteProfileAtPath(base::FilePath file_path, content::WebUI* web_ui) {
  DCHECK(web_ui);
  // This handler could have been called in managed mode, for example because
  // the user fiddled with the web inspector. Silently return in this case.
  if (Profile::FromWebUI(web_ui)->IsManaged())
    return;

  if (!profiles::IsMultipleProfilesEnabled())
    return;

  ProfileMetrics::LogProfileDeleteUser(ProfileMetrics::PROFILE_DELETED);

  g_browser_process->profile_manager()->ScheduleProfileForDeletion(
      file_path,
      base::Bind(&OpenNewWindowForProfile, GetDesktopType(web_ui)));
}

}  // namespace helper
}  // namespace options


