// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/policy/profile_policy_connector_factory.h"

#include "base/logging.h"
#include "base/memory/singleton.h"
#include "chrome/browser/policy/profile_policy_connector.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/pref_names.h"
#include "components/browser_context_keyed_service/browser_context_dependency_manager.h"
#include "components/user_prefs/pref_registry_syncable.h"

#if defined(ENABLE_CONFIGURATION_POLICY)
#if defined(OS_CHROMEOS)
#include "chrome/browser/chromeos/login/user.h"
#include "chrome/browser/chromeos/login/user_manager.h"
#include "chrome/browser/chromeos/policy/user_cloud_policy_manager_chromeos.h"
#include "chrome/browser/chromeos/policy/user_cloud_policy_manager_factory_chromeos.h"
#include "chrome/browser/chromeos/profiles/profile_helper.h"
#else
#include "chrome/browser/policy/cloud/user_cloud_policy_manager.h"
#include "chrome/browser/policy/cloud/user_cloud_policy_manager_factory.h"
#endif
#endif

namespace policy {

// static
ProfilePolicyConnectorFactory* ProfilePolicyConnectorFactory::GetInstance() {
  return Singleton<ProfilePolicyConnectorFactory>::get();
}

// static
ProfilePolicyConnector* ProfilePolicyConnectorFactory::GetForProfile(
    Profile* profile) {
  return GetInstance()->GetForProfileInternal(profile);
}

// static
scoped_ptr<ProfilePolicyConnector>
ProfilePolicyConnectorFactory::CreateForProfile(Profile* profile,
                                                bool force_immediate_load) {
  return GetInstance()->CreateForProfileInternal(profile, force_immediate_load);
}

void ProfilePolicyConnectorFactory::SetServiceForTesting(
    Profile* profile,
    ProfilePolicyConnector* connector) {
  ProfilePolicyConnector*& map_entry = connectors_[profile];
  CHECK(!map_entry);
  map_entry = connector;
}

ProfilePolicyConnectorFactory::ProfilePolicyConnectorFactory()
    : BrowserContextKeyedBaseFactory(
        "ProfilePolicyConnector",
        BrowserContextDependencyManager::GetInstance()) {
#if defined(ENABLE_CONFIGURATION_POLICY)
#if defined(OS_CHROMEOS)
  DependsOn(UserCloudPolicyManagerFactoryChromeOS::GetInstance());
#else
  DependsOn(UserCloudPolicyManagerFactory::GetInstance());
#endif
#endif
}

ProfilePolicyConnectorFactory::~ProfilePolicyConnectorFactory() {
  DCHECK(connectors_.empty());
}

ProfilePolicyConnector*
    ProfilePolicyConnectorFactory::GetForProfileInternal(Profile* profile) {
  // Get the connector for the original Profile, so that the incognito Profile
  // gets managed settings from the same PolicyService.
  ConnectorMap::const_iterator it =
      connectors_.find(profile->GetOriginalProfile());
  CHECK(it != connectors_.end());
  return it->second;
}

scoped_ptr<ProfilePolicyConnector>
ProfilePolicyConnectorFactory::CreateForProfileInternal(
    Profile* profile,
    bool force_immediate_load) {
  DCHECK(connectors_.find(profile) == connectors_.end());
#if defined(ENABLE_CONFIGURATION_POLICY)
#if defined(OS_CHROMEOS)
  chromeos::User* user = NULL;
  if (!chromeos::ProfileHelper::IsSigninProfile(profile)) {
    chromeos::UserManager* user_manager = chromeos::UserManager::Get();
    user = user_manager->GetUserByProfile(profile);
    CHECK(user);
  }
  CloudPolicyManager* user_cloud_policy_manager =
      UserCloudPolicyManagerFactoryChromeOS::GetForProfile(profile);
#else
  CloudPolicyManager* user_cloud_policy_manager =
      UserCloudPolicyManagerFactory::GetForBrowserContext(profile);
#endif
#else
  CloudPolicyManager* user_cloud_policy_manager = NULL;
#endif
  ProfilePolicyConnector* connector = new ProfilePolicyConnector(profile);
  connector->Init(force_immediate_load,
#if defined(ENABLE_CONFIGURATION_POLICY) && defined(OS_CHROMEOS)
                  user,
#endif
                  user_cloud_policy_manager);
  connectors_[profile] = connector;
  return make_scoped_ptr(connector);
}

void ProfilePolicyConnectorFactory::BrowserContextShutdown(
    content::BrowserContext* context) {
  Profile* profile = static_cast<Profile*>(context);
  if (profile->IsOffTheRecord())
    return;
  ConnectorMap::iterator it = connectors_.find(profile);
  if (it != connectors_.end())
    it->second->Shutdown();
}

void ProfilePolicyConnectorFactory::BrowserContextDestroyed(
    content::BrowserContext* context) {
  ConnectorMap::iterator it = connectors_.find(static_cast<Profile*>(context));
  if (it != connectors_.end())
    connectors_.erase(it);
  BrowserContextKeyedBaseFactory::BrowserContextDestroyed(context);
}

void ProfilePolicyConnectorFactory::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
#if defined(OS_CHROMEOS)
  registry->RegisterBooleanPref(
      prefs::kUsedPolicyCertificatesOnce,
      false,
      user_prefs::PrefRegistrySyncable::UNSYNCABLE_PREF);
#endif
#if defined(OS_ANDROID)
  registry->RegisterListPref(
      prefs::kManagedBookmarks,
      user_prefs::PrefRegistrySyncable::UNSYNCABLE_PREF);
#endif
}

void ProfilePolicyConnectorFactory::SetEmptyTestingFactory(
    content::BrowserContext* context) {}

void ProfilePolicyConnectorFactory::CreateServiceNow(
    content::BrowserContext* context) {}

}  // namespace policy
