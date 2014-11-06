// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_EXTENSIONS_DEVICE_LOCAL_ACCOUNT_MANAGEMENT_POLICY_PROVIDER_H_
#define CHROME_BROWSER_CHROMEOS_EXTENSIONS_DEVICE_LOCAL_ACCOUNT_MANAGEMENT_POLICY_PROVIDER_H_

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "chrome/browser/chromeos/policy/device_local_account.h"
#include "chrome/browser/extensions/management_policy.h"

namespace chromeos {

// A managed policy for device-local accounts that ensures only extensions whose
// type or ID has been whitelisted for use in device-local accounts can be
// installed.
class DeviceLocalAccountManagementPolicyProvider
    : public extensions::ManagementPolicy::Provider {
 public:
  explicit DeviceLocalAccountManagementPolicyProvider(
      policy::DeviceLocalAccount::Type account_type);
  virtual ~DeviceLocalAccountManagementPolicyProvider();

  // extensions::ManagementPolicy::Provider:
  virtual std::string GetDebugPolicyProviderName() const OVERRIDE;
  virtual bool UserMayLoad(const extensions::Extension* extension,
                           string16* error) const OVERRIDE;

 private:
  const policy::DeviceLocalAccount::Type account_type_;

  DISALLOW_COPY_AND_ASSIGN(DeviceLocalAccountManagementPolicyProvider);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_EXTENSIONS_DEVICE_LOCAL_ACCOUNT_MANAGEMENT_POLICY_PROVIDER_H_
