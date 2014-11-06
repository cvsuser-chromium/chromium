// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This file defines a class that contains various method related to branding.
// It provides only default implementations of these methods. Usually to add
// specific branding, we will need to extend this class with a custom
// implementation.

#include "chrome/installer/util/browser_distribution.h"

#include "base/atomicops.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/win/registry.h"
#include "base/win/windows_version.h"
#include "chrome/common/env_vars.h"
#include "chrome/installer/util/chrome_app_host_distribution.h"
#include "chrome/installer/util/chrome_frame_distribution.h"
#include "chrome/installer/util/chromium_binaries_distribution.h"
#include "chrome/installer/util/google_chrome_binaries_distribution.h"
#include "chrome/installer/util/google_chrome_distribution.h"
#include "chrome/installer/util/google_chrome_sxs_distribution.h"
#include "chrome/installer/util/install_util.h"
#include "chrome/installer/util/l10n_string_util.h"
#include "chrome/installer/util/master_preferences.h"

#include "installer_util_strings.h"  // NOLINT

using installer::MasterPreferences;

namespace {

const wchar_t kChromiumActiveSetupGuid[] =
    L"{7D2B3E1D-D096-4594-9D8F-A6667F12E0AC}";

const wchar_t kCommandExecuteImplUuid[] =
    L"{A2DF06F9-A21A-44A8-8A99-8B9C84F29160}";

// The Chromium App Launcher icon is index 1; see chrome_exe.rc.
const int kAppLauncherIconIndex = 1;

// The BrowserDistribution objects are never freed.
BrowserDistribution* g_browser_distribution = NULL;
BrowserDistribution* g_chrome_frame_distribution = NULL;
BrowserDistribution* g_binaries_distribution = NULL;
BrowserDistribution* g_chrome_app_host_distribution = NULL;

// Returns true if currently running in npchrome_frame.dll
bool IsChromeFrameModule() {
  base::FilePath module_path;
  PathService::Get(base::FILE_MODULE, &module_path);
  return base::FilePath::CompareEqualIgnoreCase(module_path.BaseName().value(),
                                          installer::kChromeFrameDll);
}

BrowserDistribution::Type GetCurrentDistributionType() {
  // TODO(erikwright): If the app host is installed, but not Chrome, perhaps
  // this should return CHROME_APP_HOST.
  static BrowserDistribution::Type type =
      (MasterPreferences::ForCurrentProcess().install_chrome_frame() ||
       IsChromeFrameModule()) ?
          BrowserDistribution::CHROME_FRAME :
          BrowserDistribution::CHROME_BROWSER;
  return type;
}

}  // end namespace

BrowserDistribution::BrowserDistribution()
    : type_(CHROME_BROWSER) {
}

BrowserDistribution::BrowserDistribution(Type type)
    : type_(type) {
}

template<class DistributionClass>
BrowserDistribution* BrowserDistribution::GetOrCreateBrowserDistribution(
    BrowserDistribution** dist) {
  if (!*dist) {
    DistributionClass* temp = new DistributionClass();
    if (base::subtle::NoBarrier_CompareAndSwap(
            reinterpret_cast<base::subtle::AtomicWord*>(dist), NULL,
            reinterpret_cast<base::subtle::AtomicWord>(temp)) != NULL)
      delete temp;
  }

  return *dist;
}

BrowserDistribution* BrowserDistribution::GetDistribution() {
  return GetSpecificDistribution(GetCurrentDistributionType());
}

// static
BrowserDistribution* BrowserDistribution::GetSpecificDistribution(
    BrowserDistribution::Type type) {
  BrowserDistribution* dist = NULL;

  switch (type) {
    case CHROME_BROWSER:
#if defined(GOOGLE_CHROME_BUILD)
      if (InstallUtil::IsChromeSxSProcess()) {
        dist = GetOrCreateBrowserDistribution<GoogleChromeSxSDistribution>(
            &g_browser_distribution);
      } else {
        dist = GetOrCreateBrowserDistribution<GoogleChromeDistribution>(
            &g_browser_distribution);
      }
#else
      dist = GetOrCreateBrowserDistribution<BrowserDistribution>(
          &g_browser_distribution);
#endif
      break;

    case CHROME_FRAME:
      dist = GetOrCreateBrowserDistribution<ChromeFrameDistribution>(
          &g_chrome_frame_distribution);
      break;

    case CHROME_APP_HOST:
      dist = GetOrCreateBrowserDistribution<ChromeAppHostDistribution>(
          &g_chrome_app_host_distribution);
      break;

    default:
      DCHECK_EQ(CHROME_BINARIES, type);
#if defined(GOOGLE_CHROME_BUILD)
      dist = GetOrCreateBrowserDistribution<GoogleChromeBinariesDistribution>(
          &g_binaries_distribution);
#else
      dist = GetOrCreateBrowserDistribution<ChromiumBinariesDistribution>(
          &g_binaries_distribution);
#endif
  }

  return dist;
}

void BrowserDistribution::DoPostUninstallOperations(
    const Version& version, const base::FilePath& local_data_path,
    const string16& distribution_data) {
}

string16 BrowserDistribution::GetActiveSetupGuid() {
  return kChromiumActiveSetupGuid;
}

string16 BrowserDistribution::GetAppGuid() {
  return L"";
}

string16 BrowserDistribution::GetBaseAppName() {
  return L"Chromium";
}

string16 BrowserDistribution::GetDisplayName() {
  return GetShortcutName(SHORTCUT_CHROME);
}

string16 BrowserDistribution::GetShortcutName(ShortcutType shortcut_type) {
  switch (shortcut_type) {
    case SHORTCUT_CHROME_ALTERNATE:
      // TODO(calamity): Change IDS_OEM_MAIN_SHORTCUT_NAME in
      // chromium_strings.grd to "The Internet" (so that it doesn't collide with
      // the value in google_chrome_strings.grd) then change this to
      // installer::GetLocalizedString(IDS_OEM_MAIN_SHORTCUT_NAME_BASE)
      return L"The Internet";
    case SHORTCUT_APP_LAUNCHER:
      return installer::GetLocalizedString(IDS_APP_LIST_SHORTCUT_NAME_BASE);
    default:
      DCHECK_EQ(shortcut_type, SHORTCUT_CHROME);
      return GetBaseAppName();
  }
}

int BrowserDistribution::GetIconIndex(ShortcutType shortcut_type) {
  if (shortcut_type == SHORTCUT_APP_LAUNCHER)
    return kAppLauncherIconIndex;
  DCHECK(shortcut_type == SHORTCUT_CHROME ||
         shortcut_type == SHORTCUT_CHROME_ALTERNATE) << shortcut_type;
  return 0;
}

string16 BrowserDistribution::GetIconFilename() {
  return installer::kChromeExe;
}

string16 BrowserDistribution::GetStartMenuShortcutSubfolder(
    Subfolder subfolder_type) {
  DCHECK_EQ(subfolder_type, SUBFOLDER_CHROME);
  return GetShortcutName(SHORTCUT_CHROME);
}

string16 BrowserDistribution::GetBaseAppId() {
  return L"Chromium";
}

string16 BrowserDistribution::GetBrowserProgIdPrefix() {
  // This used to be "ChromiumHTML", but was forced to become "ChromiumHTM"
  // because of http://crbug.com/153349.  See the declaration of this function
  // in the header file for more details.
  return L"ChromiumHTM";
}

string16 BrowserDistribution::GetBrowserProgIdDesc() {
  return L"Chromium HTML Document";
}


string16 BrowserDistribution::GetInstallSubDir() {
  return L"Chromium";
}

string16 BrowserDistribution::GetPublisherName() {
  return L"Chromium";
}

string16 BrowserDistribution::GetAppDescription() {
  return L"Browse the web";
}

string16 BrowserDistribution::GetLongAppDescription() {
  const string16& app_description =
      installer::GetLocalizedString(IDS_PRODUCT_DESCRIPTION_BASE);
  return app_description;
}

std::string BrowserDistribution::GetSafeBrowsingName() {
  return "chromium";
}

string16 BrowserDistribution::GetStateKey() {
  return L"Software\\Chromium";
}

string16 BrowserDistribution::GetStateMediumKey() {
  return L"Software\\Chromium";
}

std::string BrowserDistribution::GetNetworkStatsServer() const {
  return "";
}

std::string BrowserDistribution::GetHttpPipeliningTestServer() const {
  return "";
}

string16 BrowserDistribution::GetDistributionData(HKEY root_key) {
  return L"";
}

string16 BrowserDistribution::GetUninstallLinkName() {
  return L"Uninstall Chromium";
}

string16 BrowserDistribution::GetUninstallRegPath() {
  return L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Chromium";
}

string16 BrowserDistribution::GetVersionKey() {
  return L"Software\\Chromium";
}

BrowserDistribution::DefaultBrowserControlPolicy
    BrowserDistribution::GetDefaultBrowserControlPolicy() {
  return DEFAULT_BROWSER_FULL_CONTROL;
}

bool BrowserDistribution::CanCreateDesktopShortcuts() {
  return true;
}

bool BrowserDistribution::GetChromeChannel(string16* channel) {
  return false;
}

bool BrowserDistribution::GetCommandExecuteImplClsid(
    string16* handler_class_uuid) {
  if (handler_class_uuid)
    *handler_class_uuid = kCommandExecuteImplUuid;
  return true;
}

bool BrowserDistribution::AppHostIsSupported() {
  return false;
}

void BrowserDistribution::UpdateInstallStatus(bool system_install,
    installer::ArchiveType archive_type,
    installer::InstallStatus install_status) {
}

bool BrowserDistribution::ShouldSetExperimentLabels() {
  return false;
}

bool BrowserDistribution::HasUserExperiments() {
  return false;
}
