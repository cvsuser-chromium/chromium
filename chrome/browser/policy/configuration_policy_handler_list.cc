// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/policy/configuration_policy_handler_list.h"

#include <limits>

#include "base/basictypes.h"
#include "base/prefs/pref_value_map.h"
#include "base/stl_util.h"
#include "base/values.h"
#include "chrome/browser/extensions/policy_handlers.h"
#include "chrome/browser/net/disk_cache_dir_policy_handler.h"
#include "chrome/browser/net/proxy_policy_handler.h"
#include "chrome/browser/policy/autofill_policy_handler.h"
#include "chrome/browser/policy/configuration_policy_handler.h"
#include "chrome/browser/policy/file_selection_dialogs_policy_handler.h"
#include "chrome/browser/policy/javascript_policy_handler.h"
#include "chrome/browser/policy/policy_error_map.h"
#include "chrome/browser/policy/policy_map.h"
#include "chrome/browser/policy/url_blacklist_policy_handler.h"
#include "chrome/browser/profiles/incognito_mode_policy_handler.h"
#include "chrome/browser/search_engines/default_search_policy_handler.h"
#include "chrome/browser/sessions/restore_on_startup_policy_handler.h"
#include "chrome/browser/sync/sync_policy_handler.h"
#include "chrome/common/pref_names.h"
#include "components/policy/core/common/policy_pref_names.h"
#include "extensions/common/manifest.h"
#include "grit/generated_resources.h"
#include "policy/policy_constants.h"

#if defined(OS_CHROMEOS)
#include "ash/magnifier/magnifier_constants.h"
#include "chrome/browser/chromeos/policy/configuration_policy_handler_chromeos.h"
#include "chromeos/dbus/power_policy_controller.h"
#endif  // defined(OS_CHROMEOS)

#if defined(OS_ANDROID)
#include "chrome/browser/policy/configuration_policy_handler_android.h"
#endif  // defined(OS_ANDROID)

#if !defined(OS_CHROMEOS) && !defined(OS_ANDROID) && !defined(OS_IOS)
#include "chrome/browser/download/download_dir_policy_handler.h"
#endif

#if !defined(OS_MACOSX)
#include "apps/pref_names.h"
#endif

namespace policy {

namespace {

// List of policy types to preference names. This is used for simple policies
// that directly map to a single preference.
const PolicyToPreferenceMapEntry kSimplePolicyMap[] = {
  { key::kHomepageLocation,
    prefs::kHomePage,
    Value::TYPE_STRING },
  { key::kHomepageIsNewTabPage,
    prefs::kHomePageIsNewTabPage,
    Value::TYPE_BOOLEAN },
  { key::kRestoreOnStartupURLs,
    prefs::kURLsToRestoreOnStartup,
    Value::TYPE_LIST },
  { key::kAlternateErrorPagesEnabled,
    prefs::kAlternateErrorPagesEnabled,
    Value::TYPE_BOOLEAN },
  { key::kSearchSuggestEnabled,
    prefs::kSearchSuggestEnabled,
    Value::TYPE_BOOLEAN },
  { key::kDnsPrefetchingEnabled,
    prefs::kNetworkPredictionEnabled,
    Value::TYPE_BOOLEAN },
  { key::kBuiltInDnsClientEnabled,
    prefs::kBuiltInDnsClientEnabled,
    Value::TYPE_BOOLEAN },
  { key::kDisableSpdy,
    prefs::kDisableSpdy,
    Value::TYPE_BOOLEAN },
  { key::kSafeBrowsingEnabled,
    prefs::kSafeBrowsingEnabled,
    Value::TYPE_BOOLEAN },
  { key::kForceSafeSearch,
    prefs::kForceSafeSearch,
    Value::TYPE_BOOLEAN },
  { key::kPasswordManagerEnabled,
    prefs::kPasswordManagerEnabled,
    Value::TYPE_BOOLEAN },
  { key::kPasswordManagerAllowShowPasswords,
    prefs::kPasswordManagerAllowShowPasswords,
    Value::TYPE_BOOLEAN },
  { key::kPrintingEnabled,
    prefs::kPrintingEnabled,
    Value::TYPE_BOOLEAN },
  { key::kDisablePrintPreview,
    prefs::kPrintPreviewDisabled,
    Value::TYPE_BOOLEAN },
  { key::kMetricsReportingEnabled,
    prefs::kMetricsReportingEnabled,
    Value::TYPE_BOOLEAN },
  { key::kApplicationLocaleValue,
    prefs::kApplicationLocale,
    Value::TYPE_STRING },
  { key::kDisabledPlugins,
    prefs::kPluginsDisabledPlugins,
    Value::TYPE_LIST },
  { key::kDisabledPluginsExceptions,
    prefs::kPluginsDisabledPluginsExceptions,
    Value::TYPE_LIST },
  { key::kEnabledPlugins,
    prefs::kPluginsEnabledPlugins,
    Value::TYPE_LIST },
  { key::kShowHomeButton,
    prefs::kShowHomeButton,
    Value::TYPE_BOOLEAN },
  { key::kSavingBrowserHistoryDisabled,
    prefs::kSavingBrowserHistoryDisabled,
    Value::TYPE_BOOLEAN },
  { key::kAllowDeletingBrowserHistory,
    prefs::kAllowDeletingBrowserHistory,
    Value::TYPE_BOOLEAN },
  { key::kDeveloperToolsDisabled,
    prefs::kDevToolsDisabled,
    Value::TYPE_BOOLEAN },
  { key::kBlockThirdPartyCookies,
    prefs::kBlockThirdPartyCookies,
    Value::TYPE_BOOLEAN },
  { key::kDefaultCookiesSetting,
    prefs::kManagedDefaultCookiesSetting,
    Value::TYPE_INTEGER },
  { key::kDefaultImagesSetting,
    prefs::kManagedDefaultImagesSetting,
    Value::TYPE_INTEGER },
  { key::kDefaultPluginsSetting,
    prefs::kManagedDefaultPluginsSetting,
    Value::TYPE_INTEGER },
  { key::kDefaultPopupsSetting,
    prefs::kManagedDefaultPopupsSetting,
    Value::TYPE_INTEGER },
  { key::kAutoSelectCertificateForUrls,
    prefs::kManagedAutoSelectCertificateForUrls,
    Value::TYPE_LIST },
  { key::kCookiesAllowedForUrls,
    prefs::kManagedCookiesAllowedForUrls,
    Value::TYPE_LIST },
  { key::kCookiesBlockedForUrls,
    prefs::kManagedCookiesBlockedForUrls,
    Value::TYPE_LIST },
  { key::kCookiesSessionOnlyForUrls,
    prefs::kManagedCookiesSessionOnlyForUrls,
    Value::TYPE_LIST },
  { key::kImagesAllowedForUrls,
    prefs::kManagedImagesAllowedForUrls,
    Value::TYPE_LIST },
  { key::kImagesBlockedForUrls,
    prefs::kManagedImagesBlockedForUrls,
    Value::TYPE_LIST },
  { key::kJavaScriptAllowedForUrls,
    prefs::kManagedJavaScriptAllowedForUrls,
    Value::TYPE_LIST },
  { key::kJavaScriptBlockedForUrls,
    prefs::kManagedJavaScriptBlockedForUrls,
    Value::TYPE_LIST },
  { key::kPluginsAllowedForUrls,
    prefs::kManagedPluginsAllowedForUrls,
    Value::TYPE_LIST },
  { key::kPluginsBlockedForUrls,
    prefs::kManagedPluginsBlockedForUrls,
    Value::TYPE_LIST },
  { key::kPopupsAllowedForUrls,
    prefs::kManagedPopupsAllowedForUrls,
    Value::TYPE_LIST },
  { key::kPopupsBlockedForUrls,
    prefs::kManagedPopupsBlockedForUrls,
    Value::TYPE_LIST },
  { key::kNotificationsAllowedForUrls,
    prefs::kManagedNotificationsAllowedForUrls,
    Value::TYPE_LIST },
  { key::kNotificationsBlockedForUrls,
    prefs::kManagedNotificationsBlockedForUrls,
    Value::TYPE_LIST },
  { key::kDefaultNotificationsSetting,
    prefs::kManagedDefaultNotificationsSetting,
    Value::TYPE_INTEGER },
  { key::kDefaultGeolocationSetting,
    prefs::kManagedDefaultGeolocationSetting,
    Value::TYPE_INTEGER },
  { key::kSigninAllowed,
    prefs::kSigninAllowed,
    Value::TYPE_BOOLEAN },
  { key::kEnableOriginBoundCerts,
    prefs::kEnableOriginBoundCerts,
    Value::TYPE_BOOLEAN },
  { key::kDisableSSLRecordSplitting,
    prefs::kDisableSSLRecordSplitting,
    Value::TYPE_BOOLEAN },
  { key::kEnableOnlineRevocationChecks,
    prefs::kCertRevocationCheckingEnabled,
    Value::TYPE_BOOLEAN },
  { key::kRequireOnlineRevocationChecksForLocalAnchors,
    prefs::kCertRevocationCheckingRequiredLocalAnchors,
    Value::TYPE_BOOLEAN },
  { key::kAuthSchemes,
    prefs::kAuthSchemes,
    Value::TYPE_STRING },
  { key::kDisableAuthNegotiateCnameLookup,
    prefs::kDisableAuthNegotiateCnameLookup,
    Value::TYPE_BOOLEAN },
  { key::kEnableAuthNegotiatePort,
    prefs::kEnableAuthNegotiatePort,
    Value::TYPE_BOOLEAN },
  { key::kAuthServerWhitelist,
    prefs::kAuthServerWhitelist,
    Value::TYPE_STRING },
  { key::kAuthNegotiateDelegateWhitelist,
    prefs::kAuthNegotiateDelegateWhitelist,
    Value::TYPE_STRING },
  { key::kGSSAPILibraryName,
    prefs::kGSSAPILibraryName,
    Value::TYPE_STRING },
  { key::kAllowCrossOriginAuthPrompt,
    prefs::kAllowCrossOriginAuthPrompt,
    Value::TYPE_BOOLEAN },
  { key::kDisable3DAPIs,
    prefs::kDisable3DAPIs,
    Value::TYPE_BOOLEAN },
  { key::kDisablePluginFinder,
    prefs::kDisablePluginFinder,
    Value::TYPE_BOOLEAN },
  { key::kDiskCacheSize,
    prefs::kDiskCacheSize,
    Value::TYPE_INTEGER },
  { key::kMediaCacheSize,
    prefs::kMediaCacheSize,
    Value::TYPE_INTEGER },
  { key::kPolicyRefreshRate,
    policy_prefs::kUserPolicyRefreshRate,
    Value::TYPE_INTEGER },
  { key::kDevicePolicyRefreshRate,
    prefs::kDevicePolicyRefreshRate,
    Value::TYPE_INTEGER },
  { key::kDefaultBrowserSettingEnabled,
    prefs::kDefaultBrowserSettingEnabled,
    Value::TYPE_BOOLEAN },
  { key::kRemoteAccessHostFirewallTraversal,
    prefs::kRemoteAccessHostFirewallTraversal,
    Value::TYPE_BOOLEAN },
  { key::kRemoteAccessHostRequireTwoFactor,
    prefs::kRemoteAccessHostRequireTwoFactor,
    Value::TYPE_BOOLEAN },
  { key::kRemoteAccessHostDomain,
    prefs::kRemoteAccessHostDomain,
    Value::TYPE_STRING },
  { key::kRemoteAccessHostTalkGadgetPrefix,
    prefs::kRemoteAccessHostTalkGadgetPrefix,
    Value::TYPE_STRING },
  { key::kRemoteAccessHostRequireCurtain,
    prefs::kRemoteAccessHostRequireCurtain,
    Value::TYPE_BOOLEAN },
  { key::kRemoteAccessHostAllowClientPairing,
    prefs::kRemoteAccessHostAllowClientPairing,
    Value::TYPE_BOOLEAN },
  { key::kCloudPrintProxyEnabled,
    prefs::kCloudPrintProxyEnabled,
    Value::TYPE_BOOLEAN },
  { key::kCloudPrintSubmitEnabled,
    prefs::kCloudPrintSubmitEnabled,
    Value::TYPE_BOOLEAN },
  { key::kTranslateEnabled,
    prefs::kEnableTranslate,
    Value::TYPE_BOOLEAN },
  { key::kAllowOutdatedPlugins,
    prefs::kPluginsAllowOutdated,
    Value::TYPE_BOOLEAN },
  { key::kAlwaysAuthorizePlugins,
    prefs::kPluginsAlwaysAuthorize,
    Value::TYPE_BOOLEAN },
  { key::kBookmarkBarEnabled,
    prefs::kShowBookmarkBar,
    Value::TYPE_BOOLEAN },
  { key::kEditBookmarksEnabled,
    prefs::kEditBookmarksEnabled,
    Value::TYPE_BOOLEAN },
  { key::kAllowFileSelectionDialogs,
    prefs::kAllowFileSelectionDialogs,
    Value::TYPE_BOOLEAN },
  { key::kImportBookmarks,
    prefs::kImportBookmarks,
    Value::TYPE_BOOLEAN },
  { key::kImportHistory,
    prefs::kImportHistory,
    Value::TYPE_BOOLEAN },
  { key::kImportHomepage,
    prefs::kImportHomepage,
    Value::TYPE_BOOLEAN },
  { key::kImportSearchEngine,
    prefs::kImportSearchEngine,
    Value::TYPE_BOOLEAN },
  { key::kImportSavedPasswords,
    prefs::kImportSavedPasswords,
    Value::TYPE_BOOLEAN },
  { key::kMaxConnectionsPerProxy,
    prefs::kMaxConnectionsPerProxy,
    Value::TYPE_INTEGER },
  { key::kURLWhitelist,
    prefs::kUrlWhitelist,
    Value::TYPE_LIST },
  { key::kEnableMemoryInfo,
    prefs::kEnableMemoryInfo,
    Value::TYPE_BOOLEAN },
  { key::kRestrictSigninToPattern,
    prefs::kGoogleServicesUsernamePattern,
    Value::TYPE_STRING },
  { key::kDefaultMediaStreamSetting,
    prefs::kManagedDefaultMediaStreamSetting,
    Value::TYPE_INTEGER },
  { key::kDisableSafeBrowsingProceedAnyway,
    prefs::kSafeBrowsingProceedAnywayDisabled,
    Value::TYPE_BOOLEAN },
  { key::kSpellCheckServiceEnabled,
    prefs::kSpellCheckUseSpellingService,
    Value::TYPE_BOOLEAN },
  { key::kDisableScreenshots,
    prefs::kDisableScreenshots,
    Value::TYPE_BOOLEAN },
  { key::kAudioCaptureAllowed,
    prefs::kAudioCaptureAllowed,
    Value::TYPE_BOOLEAN },
  { key::kVideoCaptureAllowed,
    prefs::kVideoCaptureAllowed,
    Value::TYPE_BOOLEAN },
  { key::kAudioCaptureAllowedUrls,
    prefs::kAudioCaptureAllowedUrls,
    Value::TYPE_LIST },
  { key::kVideoCaptureAllowedUrls,
    prefs::kVideoCaptureAllowedUrls,
    Value::TYPE_LIST },
  { key::kHideWebStoreIcon,
    prefs::kHideWebStoreIcon,
    Value::TYPE_BOOLEAN },
  { key::kVariationsRestrictParameter,
    prefs::kVariationsRestrictParameter,
    Value::TYPE_STRING },
  { key::kSupervisedUserCreationEnabled,
    prefs::kManagedUserCreationAllowed,
    Value::TYPE_BOOLEAN },
  { key::kForceEphemeralProfiles,
    prefs::kForceEphemeralProfiles,
    Value::TYPE_BOOLEAN },

#if !defined(OS_MACOSX)
  { key::kFullscreenAllowed,
    prefs::kFullscreenAllowed,
    Value::TYPE_BOOLEAN },
  { key::kFullscreenAllowed,
    apps::prefs::kAppFullscreenAllowed,
    Value::TYPE_BOOLEAN },
#endif  // !defined(OS_MACOSX)

#if defined(OS_CHROMEOS)
  { key::kChromeOsLockOnIdleSuspend,
    prefs::kEnableScreenLock,
    Value::TYPE_BOOLEAN },
  { key::kChromeOsReleaseChannel,
    prefs::kChromeOsReleaseChannel,
    Value::TYPE_STRING },
  { key::kDriveDisabled,
    prefs::kDisableDrive,
    Value::TYPE_BOOLEAN },
  { key::kDriveDisabledOverCellular,
    prefs::kDisableDriveOverCellular,
    Value::TYPE_BOOLEAN },
  { key::kExternalStorageDisabled,
    prefs::kExternalStorageDisabled,
    Value::TYPE_BOOLEAN },
  { key::kAudioOutputAllowed,
    prefs::kAudioOutputAllowed,
    Value::TYPE_BOOLEAN },
  { key::kShowLogoutButtonInTray,
    prefs::kShowLogoutButtonInTray,
    Value::TYPE_BOOLEAN },
  { key::kShelfAutoHideBehavior,
    prefs::kShelfAutoHideBehaviorLocal,
    Value::TYPE_STRING },
  { key::kSessionLengthLimit,
    prefs::kSessionLengthLimit,
    Value::TYPE_INTEGER },
  { key::kWaitForInitialUserActivity,
    prefs::kSessionWaitForInitialUserActivity,
    Value::TYPE_BOOLEAN },
  { key::kPowerManagementUsesAudioActivity,
    prefs::kPowerUseAudioActivity,
    Value::TYPE_BOOLEAN },
  { key::kPowerManagementUsesVideoActivity,
    prefs::kPowerUseVideoActivity,
    Value::TYPE_BOOLEAN },
  { key::kAllowScreenWakeLocks,
    prefs::kPowerAllowScreenWakeLocks,
    Value::TYPE_BOOLEAN },
  { key::kWaitForInitialUserActivity,
    prefs::kPowerWaitForInitialUserActivity,
    Value::TYPE_BOOLEAN },
  { key::kTermsOfServiceURL,
    prefs::kTermsOfServiceURL,
    Value::TYPE_STRING },
  { key::kShowAccessibilityOptionsInSystemTrayMenu,
    prefs::kShouldAlwaysShowAccessibilityMenu,
    Value::TYPE_BOOLEAN },
  { key::kLargeCursorEnabled,
    prefs::kLargeCursorEnabled,
    Value::TYPE_BOOLEAN },
  { key::kSpokenFeedbackEnabled,
    prefs::kSpokenFeedbackEnabled,
    Value::TYPE_BOOLEAN },
  { key::kHighContrastEnabled,
    prefs::kHighContrastEnabled,
    Value::TYPE_BOOLEAN },
  { key::kDeviceLoginScreenDefaultLargeCursorEnabled,
    NULL,
    Value::TYPE_BOOLEAN },
  { key::kDeviceLoginScreenDefaultSpokenFeedbackEnabled,
    NULL,
    Value::TYPE_BOOLEAN },
  { key::kDeviceLoginScreenDefaultHighContrastEnabled,
    NULL,
    Value::TYPE_BOOLEAN },
  { key::kRebootAfterUpdate,
    prefs::kRebootAfterUpdate,
    Value::TYPE_BOOLEAN },
  { key::kAttestationEnabledForUser,
    prefs::kAttestationEnabled,
    Value::TYPE_BOOLEAN },
  { key::kChromeOsMultiProfileUserBehavior,
    prefs::kMultiProfileUserBehavior,
    Value::TYPE_STRING },
#endif  // defined(OS_CHROMEOS)

#if !defined(OS_MACOSX) && !defined(OS_CHROMEOS)
  { key::kBackgroundModeEnabled,
    prefs::kBackgroundModeEnabled,
    Value::TYPE_BOOLEAN },
#endif  // !defined(OS_MACOSX) && !defined(OS_CHROMEOS)

#if defined(OS_ANDROID)
  { key::kDataCompressionProxyEnabled,
    prefs::kSpdyProxyAuthEnabled,
    Value::TYPE_BOOLEAN },
#endif  // defined(OS_ANDROID)
};

// Mapping from extension type names to Manifest::Type.
StringToIntEnumListPolicyHandler::MappingEntry kExtensionAllowedTypesMap[] = {
  { "extension", extensions::Manifest::TYPE_EXTENSION },
  { "theme", extensions::Manifest::TYPE_THEME },
  { "user_script", extensions::Manifest::TYPE_USER_SCRIPT },
  { "hosted_app", extensions::Manifest::TYPE_HOSTED_APP },
  { "legacy_packaged_app", extensions::Manifest::TYPE_LEGACY_PACKAGED_APP },
  { "platform_app", extensions::Manifest::TYPE_PLATFORM_APP },
};

}  // namespace

ConfigurationPolicyHandlerList::ConfigurationPolicyHandlerList() {
}

ConfigurationPolicyHandlerList::~ConfigurationPolicyHandlerList() {
  STLDeleteElements(&handlers_);
}

void ConfigurationPolicyHandlerList::AddHandler(
    scoped_ptr<ConfigurationPolicyHandler> handler) {
  handlers_.push_back(handler.release());
}

void ConfigurationPolicyHandlerList::ApplyPolicySettings(
    const PolicyMap& policies,
    PrefValueMap* prefs,
    PolicyErrorMap* errors) const {
  PolicyErrorMap scoped_errors;
  if (!errors)
    errors = &scoped_errors;

  std::vector<ConfigurationPolicyHandler*>::const_iterator handler;
  for (handler = handlers_.begin(); handler != handlers_.end(); ++handler) {
    if ((*handler)->CheckPolicySettings(policies, errors) && prefs)
      (*handler)->ApplyPolicySettings(policies, prefs);
  }

  for (PolicyMap::const_iterator it = policies.begin();
       it != policies.end();
       ++it) {
    if (IsDeprecatedPolicy(it->first))
      errors->AddError(it->first, IDS_POLICY_DEPRECATED);
  }
}

void ConfigurationPolicyHandlerList::PrepareForDisplaying(
    PolicyMap* policies) const {
  std::vector<ConfigurationPolicyHandler*>::const_iterator handler;
  for (handler = handlers_.begin(); handler != handlers_.end(); ++handler)
    (*handler)->PrepareForDisplaying(policies);
}

#if !defined(OS_IOS)
scoped_ptr<ConfigurationPolicyHandlerList> BuildHandlerList() {
  scoped_ptr<ConfigurationPolicyHandlerList> handlers(
      new ConfigurationPolicyHandlerList);
  for (size_t i = 0; i < arraysize(kSimplePolicyMap); ++i) {
    handlers->AddHandler(make_scoped_ptr<ConfigurationPolicyHandler>(
        new SimplePolicyHandler(kSimplePolicyMap[i].policy_name,
                                kSimplePolicyMap[i].preference_path,
                                kSimplePolicyMap[i].value_type)));
  }

  handlers->AddHandler(make_scoped_ptr<ConfigurationPolicyHandler>(
      new AutofillPolicyHandler()));
  handlers->AddHandler(make_scoped_ptr<ConfigurationPolicyHandler>(
      new DefaultSearchPolicyHandler()));
  handlers->AddHandler(make_scoped_ptr<ConfigurationPolicyHandler>(
      new FileSelectionDialogsPolicyHandler()));
  handlers->AddHandler(make_scoped_ptr<ConfigurationPolicyHandler>(
      new IncognitoModePolicyHandler()));
  handlers->AddHandler(make_scoped_ptr<ConfigurationPolicyHandler>(
      new JavascriptPolicyHandler()));
  handlers->AddHandler(make_scoped_ptr<ConfigurationPolicyHandler>(
      new ProxyPolicyHandler()));
  handlers->AddHandler(make_scoped_ptr<ConfigurationPolicyHandler>(
      new RestoreOnStartupPolicyHandler()));
  handlers->AddHandler(make_scoped_ptr<ConfigurationPolicyHandler>(
      new browser_sync::SyncPolicyHandler()));
  handlers->AddHandler(make_scoped_ptr<ConfigurationPolicyHandler>(
      new URLBlacklistPolicyHandler()));

  handlers->AddHandler(make_scoped_ptr<ConfigurationPolicyHandler>(
      new extensions::ExtensionListPolicyHandler(
          key::kExtensionInstallWhitelist,
          prefs::kExtensionInstallAllowList,
          false)));
  handlers->AddHandler(make_scoped_ptr<ConfigurationPolicyHandler>(
      new extensions::ExtensionListPolicyHandler(
          key::kExtensionInstallBlacklist,
          prefs::kExtensionInstallDenyList,
          true)));
  handlers->AddHandler(make_scoped_ptr<ConfigurationPolicyHandler>(
      new extensions::ExtensionInstallForcelistPolicyHandler()));
  handlers->AddHandler(make_scoped_ptr<ConfigurationPolicyHandler>(
      new extensions::ExtensionURLPatternListPolicyHandler(
          key::kExtensionInstallSources,
          prefs::kExtensionAllowedInstallSites)));
  handlers->AddHandler(make_scoped_ptr<ConfigurationPolicyHandler>(
      new StringToIntEnumListPolicyHandler(
          key::kExtensionAllowedTypes,
          prefs::kExtensionAllowedTypes,
          kExtensionAllowedTypesMap,
          kExtensionAllowedTypesMap + arraysize(kExtensionAllowedTypesMap))));
#if defined(OS_CHROMEOS)
  handlers->AddHandler(make_scoped_ptr<ConfigurationPolicyHandler>(
      new extensions::ExtensionListPolicyHandler(
          key::kAttestationExtensionWhitelist,
          prefs::kAttestationExtensionWhitelist,
          false)));
#endif  // defined(OS_CHROMEOS)

#if !defined(OS_CHROMEOS) && !defined(OS_ANDROID) && !defined(OS_IOS)
  handlers->AddHandler(make_scoped_ptr<ConfigurationPolicyHandler>(
      new DiskCacheDirPolicyHandler()));
  handlers->AddHandler(make_scoped_ptr<ConfigurationPolicyHandler>(
      new DownloadDirPolicyHandler));
#endif  // !defined(OS_CHROMEOS) && !defined(OS_ANDROID) && !defined(OS_IOS)

#if defined(OS_CHROMEOS)
  handlers->AddHandler(make_scoped_ptr<ConfigurationPolicyHandler>(
      NetworkConfigurationPolicyHandler::CreateForDevicePolicy()));
  handlers->AddHandler(make_scoped_ptr<ConfigurationPolicyHandler>(
      NetworkConfigurationPolicyHandler::CreateForUserPolicy()));
  handlers->AddHandler(make_scoped_ptr<ConfigurationPolicyHandler>(
      new PinnedLauncherAppsPolicyHandler()));
  handlers->AddHandler(make_scoped_ptr<ConfigurationPolicyHandler>(
      new ScreenMagnifierPolicyHandler()));
  handlers->AddHandler(make_scoped_ptr<ConfigurationPolicyHandler>(
      new LoginScreenPowerManagementPolicyHandler));

  handlers->AddHandler(make_scoped_ptr<ConfigurationPolicyHandler>(
      new IntRangePolicyHandler(key::kScreenDimDelayAC,
                                prefs::kPowerAcScreenDimDelayMs,
                                0,
                                INT_MAX,
                                true)));
  handlers->AddHandler(make_scoped_ptr<ConfigurationPolicyHandler>(
      new IntRangePolicyHandler(key::kScreenOffDelayAC,
                                prefs::kPowerAcScreenOffDelayMs,
                                0,
                                INT_MAX,
                                true)));
  handlers->AddHandler(make_scoped_ptr<ConfigurationPolicyHandler>(
      new IntRangePolicyHandler(key::kScreenLockDelayAC,
                                prefs::kPowerAcScreenLockDelayMs,
                                0,
                                INT_MAX,
                                true)));
  handlers->AddHandler(make_scoped_ptr<ConfigurationPolicyHandler>(
      new IntRangePolicyHandler(key::kIdleWarningDelayAC,
                                prefs::kPowerAcIdleWarningDelayMs,
                                0,
                                INT_MAX,
                                true)));
  handlers->AddHandler(
      make_scoped_ptr<ConfigurationPolicyHandler>(new IntRangePolicyHandler(
          key::kIdleDelayAC, prefs::kPowerAcIdleDelayMs, 0, INT_MAX, true)));
  handlers->AddHandler(make_scoped_ptr<ConfigurationPolicyHandler>(
      new IntRangePolicyHandler(key::kScreenDimDelayBattery,
                                prefs::kPowerBatteryScreenDimDelayMs,
                                0,
                                INT_MAX,
                                true)));
  handlers->AddHandler(make_scoped_ptr<ConfigurationPolicyHandler>(
      new IntRangePolicyHandler(key::kScreenOffDelayBattery,
                                prefs::kPowerBatteryScreenOffDelayMs,
                                0,
                                INT_MAX,
                                true)));
  handlers->AddHandler(make_scoped_ptr<ConfigurationPolicyHandler>(
      new IntRangePolicyHandler(key::kScreenLockDelayBattery,
                                prefs::kPowerBatteryScreenLockDelayMs,
                                0,
                                INT_MAX,
                                true)));
  handlers->AddHandler(make_scoped_ptr<ConfigurationPolicyHandler>(
      new IntRangePolicyHandler(key::kIdleWarningDelayBattery,
                                prefs::kPowerBatteryIdleWarningDelayMs,
                                0,
                                INT_MAX,
                                true)));
  handlers->AddHandler(make_scoped_ptr<ConfigurationPolicyHandler>(
      new IntRangePolicyHandler(key::kIdleDelayBattery,
                                prefs::kPowerBatteryIdleDelayMs,
                                0,
                                INT_MAX,
                                true)));
  handlers->AddHandler(
      make_scoped_ptr<ConfigurationPolicyHandler>(new IntRangePolicyHandler(
          key::kIdleActionAC,
          prefs::kPowerAcIdleAction,
          chromeos::PowerPolicyController::ACTION_SUSPEND,
          chromeos::PowerPolicyController::ACTION_DO_NOTHING,
          false)));
  handlers->AddHandler(
      make_scoped_ptr<ConfigurationPolicyHandler>(new IntRangePolicyHandler(
          key::kIdleActionBattery,
          prefs::kPowerBatteryIdleAction,
          chromeos::PowerPolicyController::ACTION_SUSPEND,
          chromeos::PowerPolicyController::ACTION_DO_NOTHING,
          false)));
  handlers->AddHandler(make_scoped_ptr<ConfigurationPolicyHandler>(
      new DeprecatedIdleActionHandler()));
  handlers->AddHandler(
      make_scoped_ptr<ConfigurationPolicyHandler>(new IntRangePolicyHandler(
          key::kLidCloseAction,
          prefs::kPowerLidClosedAction,
          chromeos::PowerPolicyController::ACTION_SUSPEND,
          chromeos::PowerPolicyController::ACTION_DO_NOTHING,
          false)));
  handlers->AddHandler(make_scoped_ptr<ConfigurationPolicyHandler>(
      new IntPercentageToDoublePolicyHandler(
          key::kPresentationScreenDimDelayScale,
          prefs::kPowerPresentationScreenDimDelayFactor,
          100,
          INT_MAX,
          true)));
  handlers->AddHandler(make_scoped_ptr<ConfigurationPolicyHandler>(
      new IntPercentageToDoublePolicyHandler(
          key::kUserActivityScreenDimDelayScale,
          prefs::kPowerUserActivityScreenDimDelayFactor,
          100,
          INT_MAX,
          true)));
  handlers->AddHandler(
      make_scoped_ptr<ConfigurationPolicyHandler>(new IntRangePolicyHandler(
          key::kUptimeLimit, prefs::kUptimeLimit, 3600, INT_MAX, true)));
  handlers->AddHandler(
      make_scoped_ptr<ConfigurationPolicyHandler>(new IntRangePolicyHandler(
          key::kDeviceLoginScreenDefaultScreenMagnifierType,
          NULL,
          0,
          ash::MAGNIFIER_FULL,
          false)));
#endif  // defined(OS_CHROMEOS)

#if defined(OS_ANDROID)
  handlers->AddHandler(make_scoped_ptr<ConfigurationPolicyHandler>(
      new ManagedBookmarksPolicyHandler()));
#endif
  return handlers.Pass();
}
#endif  // !defined(OS_IOS)

}  // namespace policy
