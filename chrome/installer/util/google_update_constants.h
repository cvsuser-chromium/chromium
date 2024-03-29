// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Defines all the command-line switches used with Google Update.

#ifndef CHROME_INSTALLER_UTIL_GOOGLE_UPDATE_CONSTANTS_H_
#define CHROME_INSTALLER_UTIL_GOOGLE_UPDATE_CONSTANTS_H_

namespace google_update {

// The GUID Google Update uses to keep track of Chrome upgrades.
extern const wchar_t kChromeUpgradeCode[];

// The name of the value where Google Update reads the list of experiments for
// itself and Chrome.
extern const wchar_t kExperimentLabels[];

// The separator used to separate items in kExperimentLabels.
extern const wchar_t kExperimentLabelSep[];

// The GUID Google Update uses to keep track of Google Update self-upgrades.
extern const wchar_t kGoogleUpdateUpgradeCode[];

extern const wchar_t kGoogleUpdateSetupExe[];

extern const wchar_t kRegPathClients[];

// The difference between ClientState and ClientStateMedium is that the former
// lives on HKCU or HKLM and the later always lives in HKLM. ClientStateMedium
// is primarily used for consent of the EULA and stats collection. See bug
// 1594565.
extern const wchar_t kRegPathClientState[];
extern const wchar_t kRegPathClientStateMedium[];

extern const wchar_t kRegPathGoogleUpdate[];

// The name of the "Commands" key that lives in an app's Clients key
// (a.k.a. "Version" key).
extern const wchar_t kRegCommandsKey[];

extern const wchar_t kRegApField[];
extern const wchar_t kRegAutoRunOnOSUpgradeField[];
extern const wchar_t kRegBrandField[];
extern const wchar_t kRegBrowserField[];
extern const wchar_t kRegCFEndTempOptOutCmdField[];
extern const wchar_t kRegCFOptInCmdField[];
extern const wchar_t kRegCFOptOutCmdField[];
extern const wchar_t kRegCFTempOptOutCmdField[];
extern const wchar_t kRegClientField[];
extern const wchar_t kRegCommandLineField[];
extern const wchar_t kRegCriticalVersionField[];
extern const wchar_t kRegDidRunField[];
extern const wchar_t kRegEULAAceptedField[];
extern const wchar_t kRegGoogleUpdateVersion[];
extern const wchar_t kRegLangField[];
extern const wchar_t kRegLastStartedAUField[];
extern const wchar_t kRegLastCheckedField[];
extern const wchar_t kRegLastCheckSuccessField[];
extern const wchar_t kRegLastInstallerResultField[];
extern const wchar_t kRegLastInstallerErrorField[];
extern const wchar_t kRegLastInstallerExtraField[];
extern const wchar_t kRegMetricsId[];
extern const wchar_t kRegMSIField[];
extern const wchar_t kRegNameField[];
extern const wchar_t kRegOemInstallField[];
extern const wchar_t kRegOldVersionField[];
extern const wchar_t kRegOopcrashesField[];
extern const wchar_t kRegPathField[];
extern const wchar_t kRegRLZBrandField[];
extern const wchar_t kRegRLZReactivationBrandField[];
extern const wchar_t kRegReferralField[];
extern const wchar_t kRegRenameCmdField[];
extern const wchar_t kRegRunAsUserField[];
extern const wchar_t kRegSendsPingsField[];
extern const wchar_t kRegUninstallCmdLine[];
extern const wchar_t kRegUsageStatsField[];
extern const wchar_t kRegVersionField[];
extern const wchar_t kRegWebAccessibleField[];

// last time that chrome ran in the Time internal format.
extern const wchar_t kRegLastRunTimeField[];

}  // namespace google_update

#endif  // CHROME_INSTALLER_UTIL_GOOGLE_UPDATE_CONSTANTS_H_
