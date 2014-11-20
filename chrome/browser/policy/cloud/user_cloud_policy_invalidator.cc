// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/policy/cloud/user_cloud_policy_invalidator.h"

#include "base/bind.h"
#include "base/message_loop/message_loop_proxy.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/invalidation/invalidation_service_factory.h"
#include "chrome/browser/policy/cloud/cloud_policy_manager.h"
#include "content/public/browser/notification_source.h"

namespace policy {

UserCloudPolicyInvalidator::UserCloudPolicyInvalidator(
    Profile* profile,
    CloudPolicyManager* policy_manager)
    : CloudPolicyInvalidator(
          policy_manager->core(),
          base::MessageLoopProxy::current()),
      profile_(profile) {
  DCHECK(profile);

  // Register for notification that profile creation is complete. The
  // invalidator must not be initialized before then because the invalidation
  // service cannot be started because it depends on components initialized
  // after this object is instantiated.
  // TODO(stepco): Delayed initialization can be removed once the request
  // context can be accessed during profile-keyed service creation. Tracked by
  // bug 286209.
  registrar_.Add(this,
                 chrome::NOTIFICATION_PROFILE_ADDED,
                 content::Source<Profile>(profile));
}

void UserCloudPolicyInvalidator::Shutdown() {
  CloudPolicyInvalidator::Shutdown();
}

void UserCloudPolicyInvalidator::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  // Initialize now that profile creation is complete and the invalidation
  // service can safely be initialized.
  DCHECK(type == chrome::NOTIFICATION_PROFILE_ADDED);
  invalidation::InvalidationService* invalidation_service =
      invalidation::InvalidationServiceFactory::GetForProfile(profile_);
  if (invalidation_service)
    Initialize(invalidation_service);
}

}  // namespace policy
