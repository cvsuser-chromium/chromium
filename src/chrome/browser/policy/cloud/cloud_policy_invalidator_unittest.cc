// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "base/basictypes.h"
#include "base/bind.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop/message_loop.h"
#include "base/metrics/histogram.h"
#include "base/metrics/histogram_samples.h"
#include "base/metrics/sample_map.h"
#include "base/metrics/statistics_recorder.h"
#include "base/run_loop.h"
#include "base/test/test_simple_task_runner.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/invalidation/fake_invalidation_service.h"
#include "chrome/browser/policy/cloud/cloud_policy_constants.h"
#include "chrome/browser/policy/cloud/cloud_policy_core.h"
#include "chrome/browser/policy/cloud/cloud_policy_invalidator.h"
#include "chrome/browser/policy/cloud/cloud_policy_refresh_scheduler.h"
#include "chrome/browser/policy/cloud/enterprise_metrics.h"
#include "chrome/browser/policy/cloud/mock_cloud_policy_client.h"
#include "chrome/browser/policy/cloud/mock_cloud_policy_store.h"
#include "chrome/browser/policy/policy_types.h"
#include "chrome/browser/policy/proto/cloud/device_management_backend.pb.h"
#include "policy/policy_constants.h"
#include "sync/notifier/invalidation_util.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace policy {

class CloudPolicyInvalidatorTest : public testing::Test {
 protected:
  // Policy objects which can be used in tests.
  enum PolicyObject {
    POLICY_OBJECT_NONE,
    POLICY_OBJECT_A,
    POLICY_OBJECT_B
  };

  CloudPolicyInvalidatorTest();

  virtual void SetUp() OVERRIDE;

  virtual void TearDown() OVERRIDE;

  // Starts the invalidator which will be tested.
  // |initialize| determines if the invalidator should be initialized.
  // |start_refresh_scheduler| determines if the refresh scheduler should start.
  void StartInvalidator(bool initialize, bool start_refresh_scheduler);
  void StartInvalidator() {
    StartInvalidator(true /* initialize */, true /* start_refresh_scheduler */);
  }

  // Calls Initialize on the invalidator.
  void InitializeInvalidator();

  // Calls Shutdown on the invalidator. Test must call DestroyInvalidator
  // afterwards to prevent Shutdown from being called twice.
  void ShutdownInvalidator();

  // Destroys the invalidator.
  void DestroyInvalidator();

  // Connects the cloud policy core.
  void ConnectCore();

  // Starts the refresh scheduler.
  void StartRefreshScheduler();

  // Disconnects the cloud policy core.
  void DisconnectCore();

  // Simulates storing a new policy to the policy store.
  // |object| determines which policy object the store will report the
  // invalidator should register for. May be POLICY_OBJECT_NONE for no object.
  // |invalidation_version| determines what invalidation the store will report.
  // |policy_changed| determines whether a policy value different from the
  // current value will be stored.
  // |timestamp| determines the response timestamp the store will report.
  void StorePolicy(
      PolicyObject object,
      int64 invalidation_version,
      bool policy_changed,
      int64 timestamp);
  void StorePolicy(
      PolicyObject object,
      int64 invalidation_version,
      bool policy_changed) {
    StorePolicy(object, invalidation_version, policy_changed, ++timestamp_);
  }
  void StorePolicy(PolicyObject object, int64 invalidation_version) {
    StorePolicy(object, invalidation_version, false);
  }
  void StorePolicy(PolicyObject object) {
    StorePolicy(object, 0);
  }

  // Disables the invalidation service. It is enabled by default.
  void DisableInvalidationService();

  // Enables the invalidation service. It is enabled by default.
  void EnableInvalidationService();

  // Causes the invalidation service to fire an invalidation. Returns an ack
  // handle which be used to verify that the invalidation was acknowledged.
  syncer::AckHandle FireInvalidation(
      PolicyObject object,
      int64 version,
      const std::string& payload);

  // Causes the invalidation service to fire an invalidation with unknown
  // version. Returns an ack handle which be used to verify that the
  // invalidation was acknowledged.
  syncer::AckHandle FireUnknownVersionInvalidation(PolicyObject object);

  // Checks the expected value of the currently set invalidation info.
  bool CheckInvalidationInfo(int64 version, const std::string& payload);

  // Checks that the policy was not refreshed due to an invalidation.
  bool CheckPolicyNotRefreshed();

  // Checks that the policy was refreshed due to an invalidation within an
  // appropriate timeframe depending on whether the invalidation had unknown
  // version.
  bool CheckPolicyRefreshed();
  bool CheckPolicyRefreshedWithUnknownVersion();

  // Returns the invalidations enabled state set by the invalidator on the
  // refresh scheduler.
  bool InvalidationsEnabled();

  // Determines if the invalidation with the given ack handle has been
  // acknowledged.
  bool IsInvalidationAcknowledged(const syncer::AckHandle& ack_handle);

  // Determines if the invalidator has registered for an object with the
  // invalidation service.
  bool IsInvalidatorRegistered();

  // Get the current count for the given metric.
  base::HistogramBase::Count GetCount(MetricPolicyRefresh metric);
  base::HistogramBase::Count GetInvalidationCount(bool with_payload);

 private:
  // Checks that the policy was refreshed due to an invalidation with the given
  // base delay.
  bool CheckPolicyRefreshed(base::TimeDelta delay);

  // Checks that the policy was refreshed the given number of times.
  bool CheckPolicyRefreshCount(int count);

  // Returns the object id of the given policy object.
  const invalidation::ObjectId& GetPolicyObjectId(PolicyObject object) const;

  // Get histogram samples for the given histogram.
  scoped_ptr<base::HistogramSamples> GetHistogramSamples(
      const std::string& name) const;

  base::MessageLoop loop_;

  // Objects the invalidator depends on.
  invalidation::FakeInvalidationService invalidation_service_;
  MockCloudPolicyStore store_;
  CloudPolicyCore core_;
  MockCloudPolicyClient* client_;
  scoped_refptr<base::TestSimpleTaskRunner> task_runner_;

  // The invalidator which will be tested.
  scoped_ptr<CloudPolicyInvalidator> invalidator_;

  // Object ids for the test policy objects.
  invalidation::ObjectId object_id_a_;
  invalidation::ObjectId object_id_b_;

  // Increasing policy timestamp.
  int64 timestamp_;

  // Fake policy values which are alternated to cause the store to report a
  // changed policy.
  const char* policy_value_a_;
  const char* policy_value_b_;

  // The currently used policy value.
  const char* policy_value_cur_;

  // Stores starting histogram counts for kMetricPolicyRefresh.
  scoped_ptr<base::HistogramSamples> refresh_samples_;

  // Stores starting histogram counts for kMetricPolicyInvalidations.
  scoped_ptr<base::HistogramSamples> invalidations_samples_;
};

CloudPolicyInvalidatorTest::CloudPolicyInvalidatorTest()
    : core_(PolicyNamespaceKey(dm_protocol::kChromeUserPolicyType,
                               std::string()),
            &store_,
            loop_.message_loop_proxy()),
      client_(NULL),
      task_runner_(new base::TestSimpleTaskRunner()),
      object_id_a_(135, "asdf"),
      object_id_b_(246, "zxcv"),
      timestamp_(123456),
      policy_value_a_("asdf"),
      policy_value_b_("zxcv"),
      policy_value_cur_(policy_value_a_) {}

void CloudPolicyInvalidatorTest::SetUp() {
  base::StatisticsRecorder::Initialize();
  refresh_samples_ = GetHistogramSamples(kMetricPolicyRefresh);
  invalidations_samples_ = GetHistogramSamples(kMetricPolicyInvalidations);
}

void CloudPolicyInvalidatorTest::TearDown() {
  EXPECT_FALSE(invalidation_service_.ReceivedInvalidAcknowledgement());
  if (invalidator_)
    invalidator_->Shutdown();
  core_.Disconnect();
}

void CloudPolicyInvalidatorTest::StartInvalidator(
    bool initialize,
    bool start_refresh_scheduler) {
  invalidator_.reset(new CloudPolicyInvalidator(&core_, task_runner_));
  if (start_refresh_scheduler) {
    ConnectCore();
    StartRefreshScheduler();
  }
  if (initialize)
    InitializeInvalidator();
}

void CloudPolicyInvalidatorTest::InitializeInvalidator() {
  invalidator_->Initialize(&invalidation_service_);
}

void CloudPolicyInvalidatorTest::ShutdownInvalidator() {
  invalidator_->Shutdown();
}

void CloudPolicyInvalidatorTest::DestroyInvalidator() {
  invalidator_.reset();
}

void CloudPolicyInvalidatorTest::ConnectCore() {
  client_ = new MockCloudPolicyClient();
  client_->SetDMToken("dm");
  core_.Connect(scoped_ptr<CloudPolicyClient>(client_));
}

void CloudPolicyInvalidatorTest::StartRefreshScheduler() {
  core_.StartRefreshScheduler();
}

void CloudPolicyInvalidatorTest::DisconnectCore() {
  client_ = NULL;
  core_.Disconnect();
}

void CloudPolicyInvalidatorTest::StorePolicy(
    PolicyObject object,
    int64 invalidation_version,
    bool policy_changed,
    int64 timestamp) {
  enterprise_management::PolicyData* data =
      new enterprise_management::PolicyData();
  if (object != POLICY_OBJECT_NONE) {
    data->set_invalidation_source(GetPolicyObjectId(object).source());
    data->set_invalidation_name(GetPolicyObjectId(object).name());
  }
  data->set_timestamp(timestamp);
  // Swap the policy value if a policy change is desired.
  if (policy_changed)
    policy_value_cur_ = policy_value_cur_ == policy_value_a_ ?
        policy_value_b_ : policy_value_a_;
  data->set_policy_value(policy_value_cur_);
  store_.invalidation_version_ = invalidation_version;
  store_.policy_.reset(data);
  base::DictionaryValue policies;
  policies.SetInteger(
      key::kMaxInvalidationFetchDelay,
      CloudPolicyInvalidator::kMaxFetchDelayMin);
  store_.policy_map_.LoadFrom(
      &policies,
      POLICY_LEVEL_MANDATORY,
      POLICY_SCOPE_MACHINE);
  store_.NotifyStoreLoaded();
}

void CloudPolicyInvalidatorTest::DisableInvalidationService() {
  invalidation_service_.SetInvalidatorState(
      syncer::TRANSIENT_INVALIDATION_ERROR);
}

void CloudPolicyInvalidatorTest::EnableInvalidationService() {
  invalidation_service_.SetInvalidatorState(syncer::INVALIDATIONS_ENABLED);
}

syncer::AckHandle CloudPolicyInvalidatorTest::FireInvalidation(
    PolicyObject object,
    int64 version,
    const std::string& payload) {
  syncer::Invalidation invalidation = syncer::Invalidation::Init(
      GetPolicyObjectId(object),
      version,
      payload);
  invalidation_service_.EmitInvalidationForTest(invalidation);
  return invalidation.ack_handle();
}

syncer::AckHandle CloudPolicyInvalidatorTest::FireUnknownVersionInvalidation(
    PolicyObject object) {
  syncer::Invalidation invalidation =
      syncer::Invalidation::InitUnknownVersion(GetPolicyObjectId(object));
  invalidation_service_.EmitInvalidationForTest(invalidation);
  return invalidation.ack_handle();
}

bool CloudPolicyInvalidatorTest::CheckInvalidationInfo(
    int64 version,
    const std::string& payload) {
  MockCloudPolicyClient* client =
      static_cast<MockCloudPolicyClient*>(core_.client());
  return version == client->invalidation_version_ &&
      payload == client->invalidation_payload_;
}

bool CloudPolicyInvalidatorTest::CheckPolicyNotRefreshed() {
  return CheckPolicyRefreshCount(0);
}

bool CloudPolicyInvalidatorTest::CheckPolicyRefreshed() {
  return CheckPolicyRefreshed(base::TimeDelta());
}

bool CloudPolicyInvalidatorTest::CheckPolicyRefreshedWithUnknownVersion() {
  return CheckPolicyRefreshed(base::TimeDelta::FromMinutes(
        CloudPolicyInvalidator::kMissingPayloadDelay));
}

bool CloudPolicyInvalidatorTest::InvalidationsEnabled() {
  return core_.refresh_scheduler()->invalidations_available();
}

bool CloudPolicyInvalidatorTest::IsInvalidationAcknowledged(
    const syncer::AckHandle& ack_handle) {
  return invalidation_service_.IsInvalidationAcknowledged(ack_handle);
}

bool CloudPolicyInvalidatorTest::IsInvalidatorRegistered() {
  return !invalidation_service_.invalidator_registrar()
      .GetRegisteredIds(invalidator_.get()).empty();
}

base::HistogramBase::Count CloudPolicyInvalidatorTest::GetCount(
    MetricPolicyRefresh metric) {
  return GetHistogramSamples(kMetricPolicyRefresh)->GetCount(metric) -
      refresh_samples_->GetCount(metric);
}

base::HistogramBase::Count CloudPolicyInvalidatorTest::GetInvalidationCount(
    bool with_payload) {
  int metric = with_payload ? 1 : 0;
  return GetHistogramSamples(kMetricPolicyInvalidations)->GetCount(metric) -
      invalidations_samples_->GetCount(metric);
}

bool CloudPolicyInvalidatorTest::CheckPolicyRefreshed(base::TimeDelta delay) {
  base::TimeDelta max_delay = delay + base::TimeDelta::FromMilliseconds(
      CloudPolicyInvalidator::kMaxFetchDelayMin);

  if (task_runner_->GetPendingTasks().empty())
    return false;
  base::TimeDelta actual_delay = task_runner_->GetPendingTasks().back().delay;
  EXPECT_GE(actual_delay, delay);
  EXPECT_LE(actual_delay, max_delay);

  return CheckPolicyRefreshCount(1);
}

bool CloudPolicyInvalidatorTest::CheckPolicyRefreshCount(int count) {
  if (!client_) {
    task_runner_->RunUntilIdle();
    return count == 0;
  }

  // Clear any non-invalidation refreshes which may be pending.
  EXPECT_CALL(*client_, FetchPolicy()).Times(testing::AnyNumber());
  base::RunLoop().RunUntilIdle();
  testing::Mock::VerifyAndClearExpectations(client_);

  // Run the invalidator tasks then check for invalidation refreshes.
  EXPECT_CALL(*client_, FetchPolicy()).Times(count);
  task_runner_->RunUntilIdle();
  base::RunLoop().RunUntilIdle();
  return testing::Mock::VerifyAndClearExpectations(client_);
}

const invalidation::ObjectId& CloudPolicyInvalidatorTest::GetPolicyObjectId(
    PolicyObject object) const {
  EXPECT_TRUE(object == POLICY_OBJECT_A || object == POLICY_OBJECT_B);
  return object == POLICY_OBJECT_A ? object_id_a_ : object_id_b_;
}

scoped_ptr<base::HistogramSamples>
    CloudPolicyInvalidatorTest::GetHistogramSamples(
        const std::string& name) const {
  base::HistogramBase* histogram =
      base::StatisticsRecorder::FindHistogram(name);
  if (!histogram)
    return scoped_ptr<base::HistogramSamples>(new base::SampleMap());
  return histogram->SnapshotSamples();
}

TEST_F(CloudPolicyInvalidatorTest, Uninitialized) {
  // No invalidations should be processed if the invalidator is not initialized.
  StartInvalidator(false /* initialize */, true /* start_refresh_scheduler */);
  StorePolicy(POLICY_OBJECT_A);
  EXPECT_FALSE(IsInvalidatorRegistered());
  FireUnknownVersionInvalidation(POLICY_OBJECT_A);
  EXPECT_TRUE(CheckPolicyNotRefreshed());
}

TEST_F(CloudPolicyInvalidatorTest, RefreshSchedulerNotStarted) {
  // No invalidations should be processed if the refresh scheduler is not
  // started.
  StartInvalidator(true /* initialize */, false /* start_refresh_scheduler */);
  StorePolicy(POLICY_OBJECT_A);
  EXPECT_FALSE(IsInvalidatorRegistered());
  FireUnknownVersionInvalidation(POLICY_OBJECT_A);
  EXPECT_TRUE(CheckPolicyNotRefreshed());
}

TEST_F(CloudPolicyInvalidatorTest, DisconnectCoreThenInitialize) {
  // No invalidations should be processed if the core is disconnected before
  // initialization.
  StartInvalidator(false /* initialize */, true /* start_refresh_scheduler */);
  DisconnectCore();
  InitializeInvalidator();
  StorePolicy(POLICY_OBJECT_A);
  EXPECT_FALSE(IsInvalidatorRegistered());
  FireUnknownVersionInvalidation(POLICY_OBJECT_A);
  EXPECT_TRUE(CheckPolicyNotRefreshed());
}

TEST_F(CloudPolicyInvalidatorTest, InitializeThenStartRefreshScheduler) {
  // Make sure registration occurs and invalidations are processed when
  // Initialize is called before starting the refresh scheduler.
  // Note that the reverse case (start refresh scheduler then initialize) is
  // the default behavior for the test fixture, so will be tested in most other
  // tests.
  StartInvalidator(true /* initialize */, false /* start_refresh_scheduler */);
  ConnectCore();
  StartRefreshScheduler();
  StorePolicy(POLICY_OBJECT_A);
  EXPECT_TRUE(IsInvalidatorRegistered());
  FireUnknownVersionInvalidation(POLICY_OBJECT_A);
  EXPECT_TRUE(CheckPolicyRefreshedWithUnknownVersion());
}

TEST_F(CloudPolicyInvalidatorTest, RegisterOnStoreLoaded) {
  // No registration when store is not loaded.
  StartInvalidator();
  EXPECT_FALSE(IsInvalidatorRegistered());
  EXPECT_FALSE(InvalidationsEnabled());
  FireUnknownVersionInvalidation(POLICY_OBJECT_A);
  FireUnknownVersionInvalidation(POLICY_OBJECT_B);
  EXPECT_TRUE(CheckPolicyNotRefreshed());

  // No registration when store is loaded with no invalidation object id.
  StorePolicy(POLICY_OBJECT_NONE);
  EXPECT_FALSE(IsInvalidatorRegistered());
  EXPECT_FALSE(InvalidationsEnabled());
  FireUnknownVersionInvalidation(POLICY_OBJECT_A);
  FireUnknownVersionInvalidation(POLICY_OBJECT_B);
  EXPECT_TRUE(CheckPolicyNotRefreshed());

  // Check registration when store is loaded for object A.
  StorePolicy(POLICY_OBJECT_A);
  EXPECT_TRUE(IsInvalidatorRegistered());
  EXPECT_TRUE(InvalidationsEnabled());
  FireUnknownVersionInvalidation(POLICY_OBJECT_A);
  EXPECT_TRUE(CheckPolicyRefreshedWithUnknownVersion());
  FireUnknownVersionInvalidation(POLICY_OBJECT_B);
  EXPECT_TRUE(CheckPolicyNotRefreshed());
}

TEST_F(CloudPolicyInvalidatorTest, ChangeRegistration) {
  // Register for object A.
  StartInvalidator();
  StorePolicy(POLICY_OBJECT_A);
  EXPECT_TRUE(IsInvalidatorRegistered());
  EXPECT_TRUE(InvalidationsEnabled());
  FireUnknownVersionInvalidation(POLICY_OBJECT_A);
  EXPECT_TRUE(CheckPolicyRefreshedWithUnknownVersion());
  FireUnknownVersionInvalidation(POLICY_OBJECT_B);
  EXPECT_TRUE(CheckPolicyNotRefreshed());
  syncer::AckHandle ack = FireUnknownVersionInvalidation(POLICY_OBJECT_A);

  // Check re-registration for object B. Make sure the pending invalidation for
  // object A is acknowledged without making the callback.
  StorePolicy(POLICY_OBJECT_B);
  EXPECT_TRUE(IsInvalidatorRegistered());
  EXPECT_TRUE(InvalidationsEnabled());
  EXPECT_TRUE(IsInvalidationAcknowledged(ack));
  EXPECT_TRUE(CheckPolicyNotRefreshed());

  // Make sure future invalidations for object A are ignored and for object B
  // are processed.
  FireUnknownVersionInvalidation(POLICY_OBJECT_A);
  EXPECT_TRUE(CheckPolicyNotRefreshed());
  FireUnknownVersionInvalidation(POLICY_OBJECT_B);
  EXPECT_TRUE(CheckPolicyRefreshedWithUnknownVersion());
}

TEST_F(CloudPolicyInvalidatorTest, UnregisterOnStoreLoaded) {
  // Register for object A.
  StartInvalidator();
  StorePolicy(POLICY_OBJECT_A);
  EXPECT_TRUE(IsInvalidatorRegistered());
  EXPECT_TRUE(InvalidationsEnabled());
  FireUnknownVersionInvalidation(POLICY_OBJECT_A);
  EXPECT_TRUE(CheckPolicyRefreshedWithUnknownVersion());

  // Check unregistration when store is loaded with no invalidation object id.
  syncer::AckHandle ack = FireUnknownVersionInvalidation(POLICY_OBJECT_A);
  EXPECT_FALSE(IsInvalidationAcknowledged(ack));
  StorePolicy(POLICY_OBJECT_NONE);
  EXPECT_FALSE(IsInvalidatorRegistered());
  EXPECT_TRUE(IsInvalidationAcknowledged(ack));
  EXPECT_FALSE(InvalidationsEnabled());
  FireUnknownVersionInvalidation(POLICY_OBJECT_A);
  FireUnknownVersionInvalidation(POLICY_OBJECT_B);
  EXPECT_TRUE(CheckPolicyNotRefreshed());

  // Check re-registration for object B.
  StorePolicy(POLICY_OBJECT_B);
  EXPECT_TRUE(IsInvalidatorRegistered());
  EXPECT_TRUE(InvalidationsEnabled());
  FireUnknownVersionInvalidation(POLICY_OBJECT_B);
  EXPECT_TRUE(CheckPolicyRefreshedWithUnknownVersion());
}

TEST_F(CloudPolicyInvalidatorTest, HandleInvalidation) {
  // Register and fire invalidation
  StorePolicy(POLICY_OBJECT_A);
  StartInvalidator();
  EXPECT_TRUE(InvalidationsEnabled());
  syncer::AckHandle ack = FireInvalidation(POLICY_OBJECT_A, 12, "test_payload");

  // Make sure client info is set as soon as the invalidation is received.
  EXPECT_TRUE(CheckInvalidationInfo(12, "test_payload"));
  EXPECT_TRUE(CheckPolicyRefreshed());

  // Make sure invalidation is not acknowledged until the store is loaded.
  EXPECT_FALSE(IsInvalidationAcknowledged(ack));
  EXPECT_TRUE(CheckInvalidationInfo(12, "test_payload"));
  StorePolicy(POLICY_OBJECT_A, 12);
  EXPECT_TRUE(IsInvalidationAcknowledged(ack));
  EXPECT_TRUE(CheckInvalidationInfo(0, std::string()));
}

TEST_F(CloudPolicyInvalidatorTest, HandleInvalidationWithUnknownVersion) {
  // Register and fire invalidation with unknown version.
  StorePolicy(POLICY_OBJECT_A);
  StartInvalidator();
  syncer::AckHandle ack = FireUnknownVersionInvalidation(POLICY_OBJECT_A);

  // Make sure client info is not set until after the invalidation callback is
  // made.
  EXPECT_TRUE(CheckInvalidationInfo(0, std::string()));
  EXPECT_TRUE(CheckPolicyRefreshedWithUnknownVersion());
  EXPECT_TRUE(CheckInvalidationInfo(-1, std::string()));

  // Make sure invalidation is not acknowledged until the store is loaded.
  EXPECT_FALSE(IsInvalidationAcknowledged(ack));
  StorePolicy(POLICY_OBJECT_A, -1);
  EXPECT_TRUE(IsInvalidationAcknowledged(ack));
  EXPECT_TRUE(CheckInvalidationInfo(0, std::string()));
}

TEST_F(CloudPolicyInvalidatorTest, HandleMultipleInvalidations) {
  // Generate multiple invalidations.
  StorePolicy(POLICY_OBJECT_A);
  StartInvalidator();
  syncer::AckHandle ack1 = FireInvalidation(POLICY_OBJECT_A, 1, "test1");
  EXPECT_TRUE(CheckInvalidationInfo(1, "test1"));
  syncer::AckHandle ack2 = FireInvalidation(POLICY_OBJECT_A, 2, "test2");
  EXPECT_TRUE(CheckInvalidationInfo(2, "test2"));
  syncer::AckHandle ack3= FireInvalidation(POLICY_OBJECT_A, 3, "test3");
  EXPECT_TRUE(CheckInvalidationInfo(3, "test3"));

  // Make sure the replaced invalidations are acknowledged.
  EXPECT_TRUE(IsInvalidationAcknowledged(ack1));
  EXPECT_TRUE(IsInvalidationAcknowledged(ack2));

  // Make sure the policy is refreshed once.
  EXPECT_TRUE(CheckPolicyRefreshed());

  // Make sure that the last invalidation is only acknowledged after the store
  // is loaded with the latest version.
  StorePolicy(POLICY_OBJECT_A, 1);
  EXPECT_FALSE(IsInvalidationAcknowledged(ack3));
  StorePolicy(POLICY_OBJECT_A, 2);
  EXPECT_FALSE(IsInvalidationAcknowledged(ack3));
  StorePolicy(POLICY_OBJECT_A, 3);
  EXPECT_TRUE(IsInvalidationAcknowledged(ack3));
}

TEST_F(CloudPolicyInvalidatorTest,
       HandleMultipleInvalidationsWithUnknownVersion) {
  // Validate that multiple invalidations with unknown version each generate
  // unique invalidation version numbers.
  StorePolicy(POLICY_OBJECT_A);
  StartInvalidator();
  syncer::AckHandle ack1 = FireUnknownVersionInvalidation(POLICY_OBJECT_A);
  EXPECT_TRUE(CheckInvalidationInfo(0, std::string()));
  EXPECT_TRUE(CheckPolicyRefreshedWithUnknownVersion());
  EXPECT_TRUE(CheckInvalidationInfo(-1, std::string()));
  syncer::AckHandle ack2 = FireUnknownVersionInvalidation(POLICY_OBJECT_A);
  EXPECT_TRUE(CheckInvalidationInfo(0, std::string()));
  EXPECT_TRUE(CheckPolicyRefreshedWithUnknownVersion());
  EXPECT_TRUE(CheckInvalidationInfo(-2, std::string()));
  syncer::AckHandle ack3 = FireUnknownVersionInvalidation(POLICY_OBJECT_A);
  EXPECT_TRUE(CheckInvalidationInfo(0, std::string()));
  EXPECT_TRUE(CheckPolicyRefreshedWithUnknownVersion());
  EXPECT_TRUE(CheckInvalidationInfo(-3, std::string()));

  // Make sure the replaced invalidations are acknowledged.
  EXPECT_TRUE(IsInvalidationAcknowledged(ack1));
  EXPECT_TRUE(IsInvalidationAcknowledged(ack2));

  // Make sure that the last invalidation is only acknowledged after the store
  // is loaded with the last unknown version.
  StorePolicy(POLICY_OBJECT_A, -1);
  EXPECT_FALSE(IsInvalidationAcknowledged(ack3));
  StorePolicy(POLICY_OBJECT_A, -2);
  EXPECT_FALSE(IsInvalidationAcknowledged(ack3));
  StorePolicy(POLICY_OBJECT_A, -3);
  EXPECT_TRUE(IsInvalidationAcknowledged(ack3));
}

TEST_F(CloudPolicyInvalidatorTest, AcknowledgeBeforeRefresh) {
  // Generate an invalidation.
  StorePolicy(POLICY_OBJECT_A);
  StartInvalidator();
  syncer::AckHandle ack = FireInvalidation(POLICY_OBJECT_A, 3, "test");

  // Ensure that the policy is not refreshed and the invalidation is
  // acknowledged if the store is loaded with the latest version before the
  // refresh can occur.
  StorePolicy(POLICY_OBJECT_A, 3);
  EXPECT_TRUE(IsInvalidationAcknowledged(ack));
  EXPECT_TRUE(CheckPolicyNotRefreshed());
}

TEST_F(CloudPolicyInvalidatorTest, NoCallbackAfterShutdown) {
  // Generate an invalidation.
  StorePolicy(POLICY_OBJECT_A);
  StartInvalidator();
  syncer::AckHandle ack = FireInvalidation(POLICY_OBJECT_A, 3, "test");

  // Ensure that the policy refresh is not made after the invalidator is shut
  // down.
  ShutdownInvalidator();
  EXPECT_TRUE(CheckPolicyNotRefreshed());
  DestroyInvalidator();
}

TEST_F(CloudPolicyInvalidatorTest, StateChanged) {
  // Test invalidation service state changes while not registered.
  StartInvalidator();
  DisableInvalidationService();
  EnableInvalidationService();
  EXPECT_FALSE(InvalidationsEnabled());

  // Test invalidation service state changes while registered.
  StorePolicy(POLICY_OBJECT_A);
  EXPECT_TRUE(InvalidationsEnabled());
  DisableInvalidationService();
  EXPECT_FALSE(InvalidationsEnabled());
  DisableInvalidationService();
  EXPECT_FALSE(InvalidationsEnabled());
  EnableInvalidationService();
  EXPECT_TRUE(InvalidationsEnabled());
  EnableInvalidationService();
  EXPECT_TRUE(InvalidationsEnabled());

  // Test registration changes with invalidation service enabled.
  StorePolicy(POLICY_OBJECT_NONE);
  EXPECT_FALSE(InvalidationsEnabled());
  StorePolicy(POLICY_OBJECT_NONE);
  EXPECT_FALSE(InvalidationsEnabled());
  StorePolicy(POLICY_OBJECT_A);
  EXPECT_TRUE(InvalidationsEnabled());
  StorePolicy(POLICY_OBJECT_A);
  EXPECT_TRUE(InvalidationsEnabled());

  // Test registration changes with invalidation service disabled.
  DisableInvalidationService();
  EXPECT_FALSE(InvalidationsEnabled());
  StorePolicy(POLICY_OBJECT_NONE);
  StorePolicy(POLICY_OBJECT_A);
  EXPECT_FALSE(InvalidationsEnabled());
}

TEST_F(CloudPolicyInvalidatorTest, Disconnect) {
  // Generate an invalidation.
  StorePolicy(POLICY_OBJECT_A);
  StartInvalidator();
  syncer::AckHandle ack = FireInvalidation(POLICY_OBJECT_A, 1, "test");
  EXPECT_TRUE(InvalidationsEnabled());

  // Ensure that the policy is not refreshed after disconnecting the core, but
  // a call to indicate that invalidations are disabled is made.
  DisconnectCore();
  EXPECT_TRUE(CheckPolicyNotRefreshed());

  // Ensure that invalidation service events do not cause refreshes while the
  // invalidator is stopped.
  FireInvalidation(POLICY_OBJECT_A, 2, "test");
  EXPECT_TRUE(CheckPolicyNotRefreshed());
  DisableInvalidationService();
  EnableInvalidationService();

  // Connect and disconnect without starting the refresh scheduler.
  ConnectCore();
  FireInvalidation(POLICY_OBJECT_A, 3, "test");
  EXPECT_TRUE(CheckPolicyNotRefreshed());
  DisconnectCore();
  FireInvalidation(POLICY_OBJECT_A, 4, "test");
  EXPECT_TRUE(CheckPolicyNotRefreshed());

  // Ensure that the invalidator returns to normal after reconnecting.
  ConnectCore();
  StartRefreshScheduler();
  EXPECT_TRUE(CheckPolicyNotRefreshed());
  EXPECT_TRUE(InvalidationsEnabled());
  FireInvalidation(POLICY_OBJECT_A, 5, "test");
  EXPECT_TRUE(CheckInvalidationInfo(5, "test"));
  EXPECT_TRUE(CheckPolicyRefreshed());
  DisableInvalidationService();
  EXPECT_FALSE(InvalidationsEnabled());
}

TEST_F(CloudPolicyInvalidatorTest, RefreshMetricsUnregistered) {
  // Store loads occurring before invalidation registration are not counted.
  StartInvalidator();
  StorePolicy(POLICY_OBJECT_NONE, 0, false /* policy_changed */);
  StorePolicy(POLICY_OBJECT_NONE, 0, true /* policy_changed */);
  EXPECT_EQ(0, GetCount(METRIC_POLICY_REFRESH_CHANGED));
  EXPECT_EQ(0, GetCount(METRIC_POLICY_REFRESH_CHANGED_NO_INVALIDATIONS));
  EXPECT_EQ(0, GetCount(METRIC_POLICY_REFRESH_UNCHANGED));
  EXPECT_EQ(0, GetCount(METRIC_POLICY_REFRESH_INVALIDATED_CHANGED));
  EXPECT_EQ(0, GetCount(METRIC_POLICY_REFRESH_INVALIDATED_UNCHANGED));
}

TEST_F(CloudPolicyInvalidatorTest, RefreshMetricsNoInvalidations) {
  // Store loads occurring while registered should be differentiated depending
  // on whether the invalidation service was enabled or not.
  StorePolicy(POLICY_OBJECT_A);
  StartInvalidator();
  StorePolicy(POLICY_OBJECT_A, 0, false /* policy_changed */);
  StorePolicy(POLICY_OBJECT_A, 0, true /* policy_changed */);
  DisableInvalidationService();
  StorePolicy(POLICY_OBJECT_A, 0, false /* policy_changed */);
  StorePolicy(POLICY_OBJECT_A, 0, true /* policy_changed */);
  StorePolicy(POLICY_OBJECT_A, 0, false /* policy_changed */);
  StorePolicy(POLICY_OBJECT_A, 0, true /* policy_changed */);
  EXPECT_EQ(1, GetCount(METRIC_POLICY_REFRESH_CHANGED));
  EXPECT_EQ(2, GetCount(METRIC_POLICY_REFRESH_CHANGED_NO_INVALIDATIONS));
  EXPECT_EQ(3, GetCount(METRIC_POLICY_REFRESH_UNCHANGED));
  EXPECT_EQ(0, GetCount(METRIC_POLICY_REFRESH_INVALIDATED_CHANGED));
  EXPECT_EQ(0, GetCount(METRIC_POLICY_REFRESH_INVALIDATED_UNCHANGED));
}

TEST_F(CloudPolicyInvalidatorTest, RefreshMetricsStoreSameTimestamp) {
  // Store loads with the same timestamp as the load which causes registration
  // are not counted.
  StartInvalidator();
  StorePolicy(
      POLICY_OBJECT_A, 0, false /* policy_changed */, 12 /* timestamp */);
  StorePolicy(
      POLICY_OBJECT_A, 0, false /* policy_changed */, 12 /* timestamp */);
  StorePolicy(
      POLICY_OBJECT_A, 0, true /* policy_changed */, 12 /* timestamp */);

  // The next load with a different timestamp counts.
  StorePolicy(
      POLICY_OBJECT_A, 0, true /* policy_changed */, 13 /* timestamp */);

  EXPECT_EQ(1, GetCount(METRIC_POLICY_REFRESH_CHANGED));
  EXPECT_EQ(0, GetCount(METRIC_POLICY_REFRESH_CHANGED_NO_INVALIDATIONS));
  EXPECT_EQ(0, GetCount(METRIC_POLICY_REFRESH_UNCHANGED));
  EXPECT_EQ(0, GetCount(METRIC_POLICY_REFRESH_INVALIDATED_CHANGED));
  EXPECT_EQ(0, GetCount(METRIC_POLICY_REFRESH_INVALIDATED_UNCHANGED));
}

TEST_F(CloudPolicyInvalidatorTest, RefreshMetricsInvalidation) {
  // Store loads after an invalidation are counted as invalidated, even if
  // the loads do not result in the invalidation being acknowledged.
  StartInvalidator();
  StorePolicy(POLICY_OBJECT_A);
  FireInvalidation(POLICY_OBJECT_A, 5, "test");
  StorePolicy(POLICY_OBJECT_A, 0, false /* policy_changed */);
  StorePolicy(POLICY_OBJECT_A, 0, true /* policy_changed */);
  StorePolicy(POLICY_OBJECT_A, 5, true /* policy_changed */);

  // Store loads after the invalidation is complete are not counted as
  // invalidated.
  StorePolicy(POLICY_OBJECT_A, 0, false /* policy_changed */);
  StorePolicy(POLICY_OBJECT_A, 0, true /* policy_changed */);
  StorePolicy(POLICY_OBJECT_A, 0, false /* policy_changed */);
  StorePolicy(POLICY_OBJECT_A, 0, true /* policy_changed */);
  StorePolicy(POLICY_OBJECT_A, 0, false /* policy_changed */);
  StorePolicy(POLICY_OBJECT_A, 0, true /* policy_changed */);
  StorePolicy(POLICY_OBJECT_A, 0, false /* policy_changed */);

  EXPECT_EQ(3, GetCount(METRIC_POLICY_REFRESH_CHANGED));
  EXPECT_EQ(0, GetCount(METRIC_POLICY_REFRESH_CHANGED_NO_INVALIDATIONS));
  EXPECT_EQ(4, GetCount(METRIC_POLICY_REFRESH_UNCHANGED));
  EXPECT_EQ(2, GetCount(METRIC_POLICY_REFRESH_INVALIDATED_CHANGED));
  EXPECT_EQ(1, GetCount(METRIC_POLICY_REFRESH_INVALIDATED_UNCHANGED));
}

TEST_F(CloudPolicyInvalidatorTest, InvalidationMetrics) {
  // Generate a mix of versioned and unknown-version invalidations.
  StorePolicy(POLICY_OBJECT_A);
  StartInvalidator();
  FireUnknownVersionInvalidation(POLICY_OBJECT_B);
  FireUnknownVersionInvalidation(POLICY_OBJECT_A);
  FireInvalidation(POLICY_OBJECT_B, 1, "test");
  FireInvalidation(POLICY_OBJECT_A, 1, "test");
  FireInvalidation(POLICY_OBJECT_A, 2, "test");
  FireUnknownVersionInvalidation(POLICY_OBJECT_A);
  FireUnknownVersionInvalidation(POLICY_OBJECT_A);
  FireInvalidation(POLICY_OBJECT_A, 3, "test");
  FireInvalidation(POLICY_OBJECT_A, 4, "test");

  // Verify that received invalidations metrics are correct.
  EXPECT_EQ(3, GetInvalidationCount(false /* with_payload */));
  EXPECT_EQ(4, GetInvalidationCount(true /* with_payload */));
}

}  // namespace policy
