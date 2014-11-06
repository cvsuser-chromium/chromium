// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This file defines implementation of GoogleChromeSxSDistribution.

#include "chrome/installer/util/google_chrome_sxs_distribution.h"

#include "base/command_line.h"
#include "base/logging.h"

#include "installer_util_strings.h"  // NOLINT

namespace {

const wchar_t kChromeSxSGuid[] = L"{4ea16ac7-fd5a-47c3-875b-dbf4a2008c20}";
const wchar_t kChannelName[] = L"canary";
const wchar_t kBrowserAppId[] = L"ChromeCanary";
const wchar_t kBrowserProgIdPrefix[] = L"ChromeSSHTM";
const wchar_t kBrowserProgIdDesc[] = L"Chrome Canary HTML Document";
const int kSxSIconIndex = 4;
const wchar_t kCommandExecuteImplUuid[] =
    L"{1BEAC3E3-B852-44F4-B468-8906C062422E}";

// The Chrome App Launcher Canary icon is index 6; see chrome_exe.rc.
const int kSxSAppLauncherIconIndex = 6;

}  // namespace

GoogleChromeSxSDistribution::GoogleChromeSxSDistribution()
    : GoogleChromeDistribution() {
  GoogleChromeDistribution::set_product_guid(kChromeSxSGuid);
}

string16 GoogleChromeSxSDistribution::GetBaseAppName() {
  return L"Google Chrome Canary";
}

string16 GoogleChromeSxSDistribution::GetShortcutName(
    ShortcutType shortcut_type) {
  switch (shortcut_type) {
    case SHORTCUT_CHROME_ALTERNATE:
      // This should never be called. Returning the same string as Google Chrome
      // preserves behavior, but it will result in a naming collision.
      NOTREACHED();
      return GoogleChromeDistribution::GetShortcutName(shortcut_type);
    case SHORTCUT_APP_LAUNCHER:
      return installer::GetLocalizedString(
          IDS_APP_LIST_SHORTCUT_NAME_CANARY_BASE);
    default:
      DCHECK_EQ(shortcut_type, SHORTCUT_CHROME);
      return installer::GetLocalizedString(IDS_SXS_SHORTCUT_NAME_BASE);
  }
}

string16 GoogleChromeSxSDistribution::GetBaseAppId() {
  return kBrowserAppId;
}

string16 GoogleChromeSxSDistribution::GetBrowserProgIdPrefix() {
  return kBrowserProgIdPrefix;
}

string16 GoogleChromeSxSDistribution::GetBrowserProgIdDesc() {
  return kBrowserProgIdDesc;
}

string16 GoogleChromeSxSDistribution::GetInstallSubDir() {
  return GoogleChromeDistribution::GetInstallSubDir().append(
      installer::kSxSSuffix);
}

string16 GoogleChromeSxSDistribution::GetUninstallRegPath() {
  return GoogleChromeDistribution::GetUninstallRegPath().append(
      installer::kSxSSuffix);
}

BrowserDistribution::DefaultBrowserControlPolicy
    GoogleChromeSxSDistribution::GetDefaultBrowserControlPolicy() {
  return DEFAULT_BROWSER_OS_CONTROL_ONLY;
}

int GoogleChromeSxSDistribution::GetIconIndex(ShortcutType shortcut_type) {
  if (shortcut_type == SHORTCUT_APP_LAUNCHER)
    return kSxSAppLauncherIconIndex;
  DCHECK(shortcut_type == SHORTCUT_CHROME ||
         shortcut_type == SHORTCUT_CHROME_ALTERNATE) << shortcut_type;
  return kSxSIconIndex;
}

bool GoogleChromeSxSDistribution::GetChromeChannel(string16* channel) {
  *channel = kChannelName;
  return true;
}

bool GoogleChromeSxSDistribution::GetCommandExecuteImplClsid(
    string16* handler_class_uuid) {
  if (handler_class_uuid)
    *handler_class_uuid = kCommandExecuteImplUuid;
  return true;
}

bool GoogleChromeSxSDistribution::AppHostIsSupported() {
  return false;
}

bool GoogleChromeSxSDistribution::ShouldSetExperimentLabels() {
  return true;
}

bool GoogleChromeSxSDistribution::HasUserExperiments() {
  return true;
}

string16 GoogleChromeSxSDistribution::ChannelName() {
  return kChannelName;
}
