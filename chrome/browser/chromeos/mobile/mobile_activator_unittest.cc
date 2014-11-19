// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/mobile/mobile_activator.h"

#include "base/message_loop/message_loop.h"
#include "base/values.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "chromeos/network/network_handler.h"
#include "chromeos/network/network_state.h"
#include "content/public/browser/browser_thread.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/cros_system_api/dbus/service_constants.h"

using std::string;

using content::BrowserThread;
using testing::_;
using testing::Eq;
using testing::Invoke;
using testing::Return;

namespace {

const char kTestServicePath[] = "/a/service/path";

const size_t kNumOTASPStates = 3;

chromeos::MobileActivator::PlanActivationState kOTASPStates[kNumOTASPStates] = {
  chromeos::MobileActivator::PLAN_ACTIVATION_TRYING_OTASP,
  chromeos::MobileActivator::PLAN_ACTIVATION_INITIATING_ACTIVATION,
  chromeos::MobileActivator::PLAN_ACTIVATION_OTASP,
};

}  // namespace
namespace chromeos {

class TestMobileActivator : public MobileActivator {
 public:
  explicit TestMobileActivator(NetworkState* cellular_network) :
        cellular_network_(cellular_network) {
    // Provide reasonable defaults for basic things we're usually not testing.
    ON_CALL(*this, DCheckOnThread(_))
        .WillByDefault(Return());
    ON_CALL(*this, GetNetworkState(_))
        .WillByDefault(Return(cellular_network_));
  }
  virtual ~TestMobileActivator() {}

  MOCK_METHOD3(RequestCellularActivation,
               void(const NetworkState*,
                    const base::Closure&,
                    const network_handler::ErrorCallback&));
  MOCK_METHOD3(ChangeState, void(const NetworkState*,
                                 MobileActivator::PlanActivationState,
                                 const std::string&));
  MOCK_METHOD1(GetNetworkState, const NetworkState*(const std::string&));
  MOCK_METHOD1(EvaluateCellularNetwork, void(const NetworkState*));
  MOCK_METHOD0(SignalCellularPlanPayment, void(void));
  MOCK_METHOD0(StartOTASPTimer, void(void));
  MOCK_CONST_METHOD0(HasRecentCellularPlanPayment, bool(void));

  void InvokeChangeState(const NetworkState* network,
                         MobileActivator::PlanActivationState new_state,
                         const std::string& error_description) {
    MobileActivator::ChangeState(network, new_state, error_description);
  }

 private:
  MOCK_CONST_METHOD1(DCheckOnThread, void(const BrowserThread::ID id));

  NetworkState* cellular_network_;

  DISALLOW_COPY_AND_ASSIGN(TestMobileActivator);
};

class MobileActivatorTest : public testing::Test {
 public:
  MobileActivatorTest()
      : cellular_network_(string(kTestServicePath)),
        mobile_activator_(&cellular_network_) {
  }
  virtual ~MobileActivatorTest() {}

 protected:
  virtual void SetUp() {
    DBusThreadManager::InitializeWithStub();
    NetworkHandler::Initialize();
  }
  virtual void TearDown() {
    NetworkHandler::Shutdown();
    DBusThreadManager::Shutdown();
  }

  void set_activator_state(const MobileActivator::PlanActivationState state) {
    mobile_activator_.state_ = state;
  }
  void set_network_activation_state(const std::string& activation_state) {
    cellular_network_.activation_state_ = activation_state;
  }
  void set_connection_state(const std::string& state) {
    cellular_network_.connection_state_ = state;
  }

  base::MessageLoop message_loop_;
  NetworkState cellular_network_;
  TestMobileActivator mobile_activator_;
 private:
  DISALLOW_COPY_AND_ASSIGN(MobileActivatorTest);
};

TEST_F(MobileActivatorTest, BasicFlowForNewDevices) {
  // In a new device, we aren't connected to Verizon, we start at START
  // because we haven't paid Verizon (ever), and the modem isn't even partially
  // activated.
  std::string error_description;
  set_activator_state(MobileActivator::PLAN_ACTIVATION_START);
  set_connection_state(shill::kStateIdle);
  set_network_activation_state(shill::kActivationStateNotActivated);
  EXPECT_EQ(MobileActivator::PLAN_ACTIVATION_INITIATING_ACTIVATION,
            mobile_activator_.PickNextState(&cellular_network_,
                                            &error_description));
  // Now behave as if ChangeState() has initiated an activation.
  set_activator_state(MobileActivator::PLAN_ACTIVATION_INITIATING_ACTIVATION);
  set_network_activation_state(shill::kActivationStateActivating);
  // We'll sit in this state while we wait for the OTASP to finish.
  EXPECT_EQ(MobileActivator::PLAN_ACTIVATION_INITIATING_ACTIVATION,
            mobile_activator_.PickNextState(&cellular_network_,
                                            &error_description));
  set_network_activation_state(shill::kActivationStatePartiallyActivated);
  // We'll sit in this state until we go online as well.
  EXPECT_EQ(MobileActivator::PLAN_ACTIVATION_INITIATING_ACTIVATION,
            mobile_activator_.PickNextState(&cellular_network_,
                                            &error_description));
  set_connection_state(shill::kStatePortal);
  // After we go online, we go back to START, which acts as a jumping off
  // point for the two types of initial OTASP.
  EXPECT_EQ(MobileActivator::PLAN_ACTIVATION_START,
            mobile_activator_.PickNextState(&cellular_network_,
                                            &error_description));
  set_activator_state(MobileActivator::PLAN_ACTIVATION_START);
  EXPECT_EQ(MobileActivator::PLAN_ACTIVATION_TRYING_OTASP,
            mobile_activator_.PickNextState(&cellular_network_,
                                            &error_description));
  // Very similar things happen while we're trying OTASP.
  set_activator_state(MobileActivator::PLAN_ACTIVATION_TRYING_OTASP);
  set_network_activation_state(shill::kActivationStateActivating);
  EXPECT_EQ(MobileActivator::PLAN_ACTIVATION_TRYING_OTASP,
            mobile_activator_.PickNextState(&cellular_network_,
                                            &error_description));
  set_network_activation_state(shill::kActivationStatePartiallyActivated);
  set_connection_state(shill::kStatePortal);
  // And when we come back online again and aren't activating, load the portal.
  EXPECT_EQ(MobileActivator::PLAN_ACTIVATION_PAYMENT_PORTAL_LOADING,
            mobile_activator_.PickNextState(&cellular_network_,
                                            &error_description));
  // The JS drives us through the payment portal.
  set_activator_state(MobileActivator::PLAN_ACTIVATION_SHOWING_PAYMENT);
  // The JS also calls us to signal that the portal is done.  This triggers us
  // to start our final OTASP via the aptly named StartOTASP().
  EXPECT_CALL(mobile_activator_, SignalCellularPlanPayment());
  EXPECT_CALL(mobile_activator_,
              ChangeState(Eq(&cellular_network_),
                          Eq(MobileActivator::PLAN_ACTIVATION_START_OTASP),
                          _));
  EXPECT_CALL(mobile_activator_,
              EvaluateCellularNetwork(Eq(&cellular_network_)));
  mobile_activator_.HandleSetTransactionStatus(true);
  // Evaluate state will defer to PickNextState to select what to do now that
  // we're in START_ACTIVATION.  PickNextState should decide to start a final
  // OTASP.
  set_activator_state(MobileActivator::PLAN_ACTIVATION_START_OTASP);
  EXPECT_EQ(MobileActivator::PLAN_ACTIVATION_OTASP,
            mobile_activator_.PickNextState(&cellular_network_,
                                            &error_description));
  // Similarly to TRYING_OTASP and INITIATING_OTASP above...
  set_activator_state(MobileActivator::PLAN_ACTIVATION_OTASP);
  set_network_activation_state(shill::kActivationStateActivating);
  EXPECT_EQ(MobileActivator::PLAN_ACTIVATION_OTASP,
            mobile_activator_.PickNextState(&cellular_network_,
                                            &error_description));
  set_network_activation_state(shill::kActivationStateActivated);
  EXPECT_EQ(MobileActivator::PLAN_ACTIVATION_DONE,
            mobile_activator_.PickNextState(&cellular_network_,
                                            &error_description));
}

// A fake for MobileActivator::RequestCellularActivation that always succeeds.
void FakeRequestCellularActivationSuccess(
    const NetworkState* network,
    const base::Closure& success_callback,
    const network_handler::ErrorCallback& error_callback) {
  success_callback.Run();
}

// A fake for MobileActivator::RequestCellularActivation that always fails.
void FakeRequestCellularActivationFailure(
    const NetworkState* network,
    const base::Closure& success_callback,
    const network_handler::ErrorCallback& error_callback) {
  scoped_ptr<base::DictionaryValue> value;
  error_callback.Run("", value.Pass());
}

TEST_F(MobileActivatorTest, OTASPScheduling) {
  const std::string error;
  for (size_t i = 0; i < kNumOTASPStates; ++i) {
    // When activation works, we start a timer to watch for success.
    EXPECT_CALL(mobile_activator_, RequestCellularActivation(_, _, _))
        .Times(1)
        .WillOnce(Invoke(FakeRequestCellularActivationSuccess));
    EXPECT_CALL(mobile_activator_, StartOTASPTimer())
         .Times(1);
    set_activator_state(MobileActivator::PLAN_ACTIVATION_START);
    mobile_activator_.InvokeChangeState(&cellular_network_,
                                        kOTASPStates[i],
                                        error);

    // When activation fails, it's an error, unless we're trying for the final
    // OTASP, in which case we try again via DELAY_OTASP.
    EXPECT_CALL(mobile_activator_, RequestCellularActivation(_, _, _))
        .Times(1)
        .WillOnce(Invoke(FakeRequestCellularActivationFailure));
    if (kOTASPStates[i] == MobileActivator::PLAN_ACTIVATION_OTASP) {
      EXPECT_CALL(mobile_activator_, ChangeState(
          Eq(&cellular_network_),
          Eq(MobileActivator::PLAN_ACTIVATION_DELAY_OTASP),
          _));
    } else {
      EXPECT_CALL(mobile_activator_, ChangeState(
          Eq(&cellular_network_),
          Eq(MobileActivator::PLAN_ACTIVATION_ERROR),
          _));
    }
    set_activator_state(MobileActivator::PLAN_ACTIVATION_START);
    mobile_activator_.InvokeChangeState(&cellular_network_,
                                        kOTASPStates[i],
                                        error);
  }
}

TEST_F(MobileActivatorTest, ReconnectOnDisconnectFromPaymentPortal) {
  // Most states either don't care if we're offline or expect to be offline at
  // some point.  For instance the OTASP states expect to go offline during
  // activation and eventually come back.  There are a few transitions states
  // like START_OTASP and DELAY_OTASP which don't really depend on the state of
  // the modem (offline or online) to work correctly.  A few places however,
  // like when we're displaying the portal care quite a bit about going
  // offline.  Lets test for those cases.
  std::string error_description;
  set_connection_state(shill::kStateFailure);
  set_network_activation_state(shill::kActivationStatePartiallyActivated);
  set_activator_state(MobileActivator::PLAN_ACTIVATION_PAYMENT_PORTAL_LOADING);
  EXPECT_EQ(MobileActivator::PLAN_ACTIVATION_RECONNECTING,
            mobile_activator_.PickNextState(&cellular_network_,
                                            &error_description));
  set_activator_state(MobileActivator::PLAN_ACTIVATION_SHOWING_PAYMENT);
  EXPECT_EQ(MobileActivator::PLAN_ACTIVATION_RECONNECTING,
            mobile_activator_.PickNextState(&cellular_network_,
                                            &error_description));
}

TEST_F(MobileActivatorTest, StartAtStart) {
  EXPECT_CALL(mobile_activator_, HasRecentCellularPlanPayment())
      .WillOnce(Return(false));
  EXPECT_CALL(mobile_activator_,
              EvaluateCellularNetwork(Eq(&cellular_network_)));
  mobile_activator_.StartActivation();
  EXPECT_EQ(mobile_activator_.state(), MobileActivator::PLAN_ACTIVATION_START);
}

}  // namespace chromeos
