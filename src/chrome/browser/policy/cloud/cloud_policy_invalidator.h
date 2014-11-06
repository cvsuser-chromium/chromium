// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_POLICY_CLOUD_CLOUD_POLICY_INVALIDATOR_H_
#define CHROME_BROWSER_POLICY_CLOUD_CLOUD_POLICY_INVALIDATOR_H_

#include <string>

#include "base/basictypes.h"
#include "base/callback.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/threading/thread_checker.h"
#include "chrome/browser/policy/cloud/cloud_policy_core.h"
#include "chrome/browser/policy/cloud/cloud_policy_store.h"
#include "sync/notifier/invalidation_handler.h"

namespace base {
class SequencedTaskRunner;
}

namespace invalidation {
class InvalidationService;
}

namespace policy {

// Listens for and provides policy invalidations.
class CloudPolicyInvalidator : public syncer::InvalidationHandler,
                               public CloudPolicyCore::Observer,
                               public CloudPolicyStore::Observer {
 public:
  // The number of minutes to delay a policy refresh after receiving an
  // invalidation with no payload.
  static const int kMissingPayloadDelay;

  // The default, min and max values for max_fetch_delay_.
  static const int kMaxFetchDelayDefault;
  static const int kMaxFetchDelayMin;
  static const int kMaxFetchDelayMax;

  // |core| is the cloud policy core which connects the various policy objects.
  // It must remain valid until Shutdown is called.
  // |task_runner| is used for scheduling delayed tasks. It must post tasks to
  // the main policy thread.
  CloudPolicyInvalidator(
      CloudPolicyCore* core,
      const scoped_refptr<base::SequencedTaskRunner>& task_runner);
  virtual ~CloudPolicyInvalidator();

  // Initializes the invalidator. No invalidations will be generated before this
  // method is called. This method must only be called once.
  // |invalidation_service| is the invalidation service to use and must remain
  // valid until Shutdown is called.
  void Initialize(invalidation::InvalidationService* invalidation_service);

  // Shuts down and disables invalidations. It must be called before the object
  // is destroyed.
  void Shutdown();

  // Whether the invalidator currently has the ability to receive invalidations.
  bool invalidations_enabled() {
    return invalidations_enabled_;
  }

  // syncer::InvalidationHandler:
  virtual void OnInvalidatorStateChange(
      syncer::InvalidatorState state) OVERRIDE;
  virtual void OnIncomingInvalidation(
      const syncer::ObjectIdInvalidationMap& invalidation_map) OVERRIDE;

  // CloudPolicyCore::Observer:
  virtual void OnCoreConnected(CloudPolicyCore* core) OVERRIDE;
  virtual void OnRefreshSchedulerStarted(CloudPolicyCore* core) OVERRIDE;
  virtual void OnCoreDisconnecting(CloudPolicyCore* core) OVERRIDE;

  // CloudPolicyStore::Observer:
  virtual void OnStoreLoaded(CloudPolicyStore* store) OVERRIDE;
  virtual void OnStoreError(CloudPolicyStore* store) OVERRIDE;

 private:
  // Handle an invalidation to the policy.
  void HandleInvalidation(const syncer::Invalidation& invalidation);

  // Update object registration with the invalidation service based on the
  // given policy data.
  void UpdateRegistration(const enterprise_management::PolicyData* policy);

  // Registers the given object with the invalidation service.
  void Register(int64 timestamp, const invalidation::ObjectId& object_id);

  // Unregisters the current object with the invalidation service.
  void Unregister();

  // Update |max_fetch_delay_| based on the given policy map.
  void UpdateMaxFetchDelay(const PolicyMap& policy_map);
  void set_max_fetch_delay(int delay);

  // Updates invalidations_enabled_ and calls the invalidation handler if the
  // value changed.
  void UpdateInvalidationsEnabled();

  // Refresh the policy.
  // |is_missing_payload| is set to true if the callback is being invoked in
  // response to an invalidation with a missing payload.
  void RefreshPolicy(bool is_missing_payload);

  // Acknowledge the latest invalidation.
  void AcknowledgeInvalidation();

  // Determines if the given policy is different from the policy passed in the
  // previous call.
  bool IsPolicyChanged(const enterprise_management::PolicyData* policy);

  // Get the kMetricPolicyRefresh histogram metric which should be incremented
  // when a policy is stored.
  int GetPolicyRefreshMetric(bool policy_changed);

  // The state of the object.
  enum State {
    UNINITIALIZED,
    STOPPED,
    STARTED,
    SHUT_DOWN
  };
  State state_;

  // The cloud policy core.
  CloudPolicyCore* core_;

  // Schedules delayed tasks.
  const scoped_refptr<base::SequencedTaskRunner> task_runner_;

  // The invalidation service.
  invalidation::InvalidationService* invalidation_service_;

  // Whether the invalidator currently has the ability to receive invalidations.
  // This is true if the invalidation service is enabled and the invalidator
  // has registered for a policy object.
  bool invalidations_enabled_;

  // Whether the invalidation service is currently enabled.
  bool invalidation_service_enabled_;

  // The timestamp of the PolicyData at which this object registered for policy
  // invalidations. Set to zero if the object has not registered yet.
  int64 registered_timestamp_;

  // The object id representing the policy in the invalidation service.
  invalidation::ObjectId object_id_;

  // Whether the policy is current invalid. This is set to true when an
  // invalidation is received and reset when the policy fetched due to the
  // invalidation is stored.
  bool invalid_;

  // The version of the latest invalidation received. This is compared to
  // the invalidation version of policy stored to determine when the
  // invalidated policy is up-to-date.
  int64 invalidation_version_;

  // The number of invalidations with unknown version received. Since such
  // invalidations do not provide a version number, this count is used to set
  // invalidation_version_ when such invalidations occur.
  int unknown_version_invalidation_count_;

  // The acknowledgment handle for the current invalidation.
  syncer::AckHandle ack_handle_;

  // WeakPtrFactory used to create callbacks to this object.
  base::WeakPtrFactory<CloudPolicyInvalidator> weak_factory_;

  // The maximum random delay, in ms, between receiving an invalidation and
  // fetching the new policy.
  int max_fetch_delay_;

  // The hash value of the current policy. This is used to determine if a new
  // policy is different from the current one.
  uint32 policy_hash_value_;

  // A thread checker to make sure that callbacks are invoked on the correct
  // thread.
  base::ThreadChecker thread_checker_;

  DISALLOW_COPY_AND_ASSIGN(CloudPolicyInvalidator);
};

}  // namespace policy

#endif  // CHROME_BROWSER_POLICY_CLOUD_CLOUD_POLICY_INVALIDATOR_H_
