// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_POLICY_PROFILE_POLICY_CONNECTOR_H_
#define CHROME_BROWSER_POLICY_PROFILE_POLICY_CONNECTOR_H_

#include <string>

#include "base/basictypes.h"
#include "base/callback.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "components/browser_context_keyed_service/browser_context_keyed_service.h"

class Profile;

namespace net {
class CertTrustAnchorProvider;
}

namespace chromeos {
class User;
}

namespace policy {

class CloudPolicyManager;
class ConfigurationPolicyProvider;
class PolicyService;

// A BrowserContextKeyedService that creates and manages the per-Profile policy
// components.
class ProfilePolicyConnector : public BrowserContextKeyedService {
 public:
  explicit ProfilePolicyConnector(Profile* profile);
  virtual ~ProfilePolicyConnector();

  // If |force_immediate_load| then disk caches will be loaded synchronously.
  void Init(bool force_immediate_load,
#if defined(OS_CHROMEOS)
            const chromeos::User* user,
#endif
            CloudPolicyManager* user_cloud_policy_manager);

  void InitForTesting(scoped_ptr<PolicyService> service);

  // BrowserContextKeyedService:
  virtual void Shutdown() OVERRIDE;

  // This is never NULL.
  PolicyService* policy_service() const { return policy_service_.get(); }

#if defined(OS_CHROMEOS)
  // Returns a callback that should be called if a policy installed certificate
  // was trusted for the associated profile. The closure can be safely used (on
  // the UI thread) even after this Connector is destructed.
  base::Closure GetPolicyCertTrustedCallback();
#endif

  // Returns true if |profile()| has used certificates installed via policy
  // to establish a secure connection before. This means that it may have
  // cached content from an untrusted source.
  bool UsedPolicyCertificates();

 private:
#if defined(ENABLE_CONFIGURATION_POLICY)

#if defined(OS_CHROMEOS)
  void SetUsedPolicyCertificatesOnce();
  void InitializeDeviceLocalAccountPolicyProvider(const std::string& username);
#endif

#if defined(OS_CHROMEOS)
  // Some of the user policy configuration affects browser global state, and
  // can only come from one Profile. |is_primary_user_| is true if this
  // connector belongs to the first signed-in Profile, and in that case that
  // Profile's policy is the one that affects global policy settings in
  // local state.
  bool is_primary_user_;

  scoped_ptr<ConfigurationPolicyProvider> special_user_policy_provider_;

  base::WeakPtrFactory<ProfilePolicyConnector> weak_ptr_factory_;
#endif

  Profile* profile_;

#endif  // ENABLE_CONFIGURATION_POLICY

  scoped_ptr<PolicyService> policy_service_;

  DISALLOW_COPY_AND_ASSIGN(ProfilePolicyConnector);
};

}  // namespace policy

#endif  // CHROME_BROWSER_POLICY_PROFILE_POLICY_CONNECTOR_H_
