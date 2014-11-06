// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/policy/cloud/cloud_policy_invalidator.h"

#include "base/bind.h"
#include "base/command_line.h"
#include "base/hash.h"
#include "base/location.h"
#include "base/metrics/histogram.h"
#include "base/rand_util.h"
#include "base/sequenced_task_runner.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/invalidation/invalidation_service.h"
#include "chrome/browser/policy/cloud/cloud_policy_client.h"
#include "chrome/browser/policy/cloud/cloud_policy_refresh_scheduler.h"
#include "chrome/browser/policy/cloud/enterprise_metrics.h"
#include "components/policy/core/common/policy_switches.h"
#include "policy/policy_constants.h"
#include "sync/notifier/object_id_invalidation_map.h"

namespace policy {

const int CloudPolicyInvalidator::kMissingPayloadDelay = 5;
const int CloudPolicyInvalidator::kMaxFetchDelayDefault = 120000;
const int CloudPolicyInvalidator::kMaxFetchDelayMin = 1000;
const int CloudPolicyInvalidator::kMaxFetchDelayMax = 300000;

CloudPolicyInvalidator::CloudPolicyInvalidator(
    CloudPolicyCore* core,
    const scoped_refptr<base::SequencedTaskRunner>& task_runner)
    : state_(UNINITIALIZED),
      core_(core),
      task_runner_(task_runner),
      invalidation_service_(NULL),
      invalidations_enabled_(false),
      invalidation_service_enabled_(false),
      registered_timestamp_(0),
      invalid_(false),
      invalidation_version_(0),
      unknown_version_invalidation_count_(0),
      ack_handle_(syncer::AckHandle::InvalidAckHandle()),
      weak_factory_(this),
      max_fetch_delay_(kMaxFetchDelayDefault),
      policy_hash_value_(0) {
  DCHECK(core);
  DCHECK(task_runner.get());
}

CloudPolicyInvalidator::~CloudPolicyInvalidator() {
  DCHECK(state_ == SHUT_DOWN);
}

void CloudPolicyInvalidator::Initialize(
    invalidation::InvalidationService* invalidation_service) {
  DCHECK(state_ == UNINITIALIZED);
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(invalidation_service);
  invalidation_service_ = invalidation_service;
  state_ = STOPPED;
  core_->AddObserver(this);
  if (core_->refresh_scheduler())
    OnRefreshSchedulerStarted(core_);
}

void CloudPolicyInvalidator::Shutdown() {
  DCHECK(state_ != SHUT_DOWN);
  DCHECK(thread_checker_.CalledOnValidThread());
  if (state_ == STARTED) {
    if (registered_timestamp_)
      invalidation_service_->UnregisterInvalidationHandler(this);
    core_->store()->RemoveObserver(this);
    weak_factory_.InvalidateWeakPtrs();
  }
  if (state_ != UNINITIALIZED)
    core_->RemoveObserver(this);
  state_ = SHUT_DOWN;
}

void CloudPolicyInvalidator::OnInvalidatorStateChange(
    syncer::InvalidatorState state) {
  DCHECK(state_ == STARTED);
  DCHECK(thread_checker_.CalledOnValidThread());
  invalidation_service_enabled_ = state == syncer::INVALIDATIONS_ENABLED;
  UpdateInvalidationsEnabled();
}

void CloudPolicyInvalidator::OnIncomingInvalidation(
    const syncer::ObjectIdInvalidationMap& invalidation_map) {
  DCHECK(state_ == STARTED);
  DCHECK(thread_checker_.CalledOnValidThread());
  const syncer::SingleObjectInvalidationSet& list =
      invalidation_map.ForObject(object_id_);
  if (list.IsEmpty()) {
    NOTREACHED();
    return;
  }
  HandleInvalidation(list.back());
}

void CloudPolicyInvalidator::OnCoreConnected(CloudPolicyCore* core) {}

void CloudPolicyInvalidator::OnRefreshSchedulerStarted(CloudPolicyCore* core) {
  DCHECK(state_ == STOPPED);
  DCHECK(thread_checker_.CalledOnValidThread());
  state_ = STARTED;
  OnStoreLoaded(core_->store());
  core_->store()->AddObserver(this);
}

void CloudPolicyInvalidator::OnCoreDisconnecting(CloudPolicyCore* core) {
  DCHECK(state_ == STARTED || state_ == STOPPED);
  DCHECK(thread_checker_.CalledOnValidThread());
  if (state_ == STARTED) {
    Unregister();
    core_->store()->RemoveObserver(this);
    state_ = STOPPED;
  }
}

void CloudPolicyInvalidator::OnStoreLoaded(CloudPolicyStore* store) {
  DCHECK(state_ == STARTED);
  DCHECK(thread_checker_.CalledOnValidThread());
  bool policy_changed = IsPolicyChanged(store->policy());

  if (registered_timestamp_) {
    // Update the kMetricPolicyRefresh histogram. In some cases, this object can
    // be constructed during an OnStoreLoaded callback, which causes
    // OnStoreLoaded to be called twice at initialization time, so make sure
    // that the timestamp does not match the timestamp at which registration
    // occurred. We only measure changes which occur after registration.
    if (!store->policy() || !store->policy()->has_timestamp() ||
        store->policy()->timestamp() != registered_timestamp_) {
      UMA_HISTOGRAM_ENUMERATION(
          kMetricPolicyRefresh,
          GetPolicyRefreshMetric(policy_changed),
          METRIC_POLICY_REFRESH_SIZE);
    }

    // If the policy was invalid and the version stored matches the latest
    // invalidation version, acknowledge the latest invalidation.
    if (invalid_ && store->invalidation_version() == invalidation_version_)
      AcknowledgeInvalidation();
  }

  UpdateRegistration(store->policy());
  UpdateMaxFetchDelay(store->policy_map());
}

void CloudPolicyInvalidator::OnStoreError(CloudPolicyStore* store) {}

void CloudPolicyInvalidator::HandleInvalidation(
    const syncer::Invalidation& invalidation) {
  // The invalidation service may send an invalidation more than once if there
  // is a delay in acknowledging it. Duplicate invalidations are ignored.
  if (invalid_ && ack_handle_.Equals(invalidation.ack_handle()))
    return;

  // If there is still a pending invalidation, acknowledge it, since we only
  // care about the latest invalidation.
  if (invalid_)
    AcknowledgeInvalidation();

  // Update invalidation state.
  invalid_ = true;
  ack_handle_ = invalidation.ack_handle();

  // When an invalidation with unknown version is received, use negative
  // numbers based on the number of such invalidations received. This
  // ensures that the version numbers do not collide with "real" versions
  // (which are positive) or previous invalidations with unknown version.
  if (invalidation.is_unknown_version()) {
    invalidation_version_ = -(++unknown_version_invalidation_count_);
  } else {
    invalidation_version_ = invalidation.version();
  }

  // In order to prevent the cloud policy server from becoming overwhelmed when
  // a policy with many users is modified, delay for a random period of time
  // before fetching the policy. Delay for at least 20ms so that if multiple
  // invalidations are received in quick succession, only one fetch will be
  // performed.
  base::TimeDelta delay = base::TimeDelta::FromMilliseconds(
      base::RandInt(20, max_fetch_delay_));

  std::string payload;
  if (!invalidation.is_unknown_version())
    payload = invalidation.payload();

  // If there is a payload, the policy can be refreshed at any time, so set
  // the version and payload on the client immediately. Otherwise, the refresh
  // must only run after at least kMissingPayloadDelay minutes.
  if (!payload.empty())
    core_->client()->SetInvalidationInfo(invalidation_version_, payload);
  else
    delay += base::TimeDelta::FromMinutes(kMissingPayloadDelay);

  // Schedule the policy to be refreshed.
  task_runner_->PostDelayedTask(
      FROM_HERE,
      base::Bind(
          &CloudPolicyInvalidator::RefreshPolicy,
          weak_factory_.GetWeakPtr(),
          payload.empty() /* is_missing_payload */),
      delay);

  // Update the kMetricPolicyInvalidations histogram.
  UMA_HISTOGRAM_BOOLEAN(kMetricPolicyInvalidations, !payload.empty());
}

void CloudPolicyInvalidator::UpdateRegistration(
    const enterprise_management::PolicyData* policy) {
  // Create the ObjectId based on the policy data.
  // If the policy does not specify an the ObjectId, then unregister.
  if (!policy ||
      !policy->has_timestamp() ||
      !policy->has_invalidation_source() ||
      !policy->has_invalidation_name()) {
    Unregister();
    return;
  }
  invalidation::ObjectId object_id(
      policy->invalidation_source(),
      policy->invalidation_name());

  // If the policy object id in the policy data is different from the currently
  // registered object id, update the object registration.
  if (!registered_timestamp_ || !(object_id == object_id_))
    Register(policy->timestamp(), object_id);
}

void CloudPolicyInvalidator::Register(
    int64 timestamp,
    const invalidation::ObjectId& object_id) {
  // Register this handler with the invalidation service if needed.
  if (!registered_timestamp_) {
    OnInvalidatorStateChange(invalidation_service_->GetInvalidatorState());
    invalidation_service_->RegisterInvalidationHandler(this);
  }

  // Update internal state.
  if (invalid_)
    AcknowledgeInvalidation();
  registered_timestamp_ = timestamp;
  object_id_ = object_id;
  UpdateInvalidationsEnabled();

  // Update registration with the invalidation service.
  syncer::ObjectIdSet ids;
  ids.insert(object_id);
  invalidation_service_->UpdateRegisteredInvalidationIds(this, ids);
}

void CloudPolicyInvalidator::Unregister() {
  if (registered_timestamp_) {
    if (invalid_)
      AcknowledgeInvalidation();
    invalidation_service_->UpdateRegisteredInvalidationIds(
        this,
        syncer::ObjectIdSet());
    invalidation_service_->UnregisterInvalidationHandler(this);
    registered_timestamp_ = 0;
    UpdateInvalidationsEnabled();
  }
}

void CloudPolicyInvalidator::UpdateMaxFetchDelay(const PolicyMap& policy_map) {
  int delay;

  // Try reading the delay from the policy.
  const base::Value* delay_policy_value =
      policy_map.GetValue(key::kMaxInvalidationFetchDelay);
  if (delay_policy_value && delay_policy_value->GetAsInteger(&delay)) {
    set_max_fetch_delay(delay);
    return;
  }

  // Try reading the delay from the command line switch.
  std::string delay_string =
      CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
          switches::kCloudPolicyInvalidationDelay);
  if (base::StringToInt(delay_string, &delay)) {
    set_max_fetch_delay(delay);
    return;
  }

  set_max_fetch_delay(kMaxFetchDelayDefault);
}

void CloudPolicyInvalidator::set_max_fetch_delay(int delay) {
  if (delay < kMaxFetchDelayMin)
    max_fetch_delay_ = kMaxFetchDelayMin;
  else if (delay > kMaxFetchDelayMax)
    max_fetch_delay_ = kMaxFetchDelayMax;
  else
    max_fetch_delay_ = delay;
}

void CloudPolicyInvalidator::UpdateInvalidationsEnabled() {
  bool invalidations_enabled =
      invalidation_service_enabled_ && registered_timestamp_;
  if (invalidations_enabled_ != invalidations_enabled) {
    invalidations_enabled_ = invalidations_enabled;
    core_->refresh_scheduler()->SetInvalidationServiceAvailability(
        invalidations_enabled);
  }
}

void CloudPolicyInvalidator::RefreshPolicy(bool is_missing_payload) {
  DCHECK(thread_checker_.CalledOnValidThread());
  // In the missing payload case, the invalidation version has not been set on
  // the client yet, so set it now that the required time has elapsed.
  if (is_missing_payload)
    core_->client()->SetInvalidationInfo(invalidation_version_, std::string());
  core_->refresh_scheduler()->RefreshSoon();
}

void CloudPolicyInvalidator::AcknowledgeInvalidation() {
  DCHECK(invalid_);
  invalid_ = false;
  core_->client()->SetInvalidationInfo(0, std::string());
  invalidation_service_->AcknowledgeInvalidation(object_id_, ack_handle_);
  // Cancel any scheduled policy refreshes.
  weak_factory_.InvalidateWeakPtrs();
}

bool CloudPolicyInvalidator::IsPolicyChanged(
    const enterprise_management::PolicyData* policy) {
  // Determine if the policy changed by comparing its hash value to the
  // previous policy's hash value.
  uint32 new_hash_value = 0;
  if (policy && policy->has_policy_value())
    new_hash_value = base::Hash(policy->policy_value());
  bool changed = new_hash_value != policy_hash_value_;
  policy_hash_value_ = new_hash_value;
  return changed;
}

int CloudPolicyInvalidator::GetPolicyRefreshMetric(bool policy_changed) {
  if (policy_changed) {
    if (invalid_)
      return METRIC_POLICY_REFRESH_INVALIDATED_CHANGED;
    if (invalidations_enabled_)
      return METRIC_POLICY_REFRESH_CHANGED;
    return METRIC_POLICY_REFRESH_CHANGED_NO_INVALIDATIONS;
  }
  if (invalid_)
    return METRIC_POLICY_REFRESH_INVALIDATED_UNCHANGED;
  return METRIC_POLICY_REFRESH_UNCHANGED;
}

}  // namespace policy
