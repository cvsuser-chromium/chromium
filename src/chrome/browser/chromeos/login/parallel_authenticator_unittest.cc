// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/login/parallel_authenticator.h"

#include <string>

#include "base/command_line.h"
#include "base/file_util.h"
#include "base/files/file_path.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop/message_loop.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "chrome/browser/chromeos/login/mock_login_status_consumer.h"
#include "chrome/browser/chromeos/login/mock_url_fetchers.h"
#include "chrome/browser/chromeos/login/mock_user_manager.h"
#include "chrome/browser/chromeos/login/test_attempt_state.h"
#include "chrome/browser/chromeos/login/user.h"
#include "chrome/browser/chromeos/login/user_manager.h"
#include "chrome/browser/chromeos/settings/cros_settings.h"
#include "chrome/browser/chromeos/settings/device_settings_test_helper.h"
#include "chrome/browser/chromeos/settings/stub_cros_settings_provider.h"
#include "chrome/test/base/testing_profile.h"
#include "chromeos/chromeos_switches.h"
#include "chromeos/cryptohome/mock_async_method_caller.h"
#include "chromeos/cryptohome/system_salt_getter.h"
#include "chromeos/dbus/fake_cryptohome_client.h"
#include "chromeos/dbus/fake_dbus_thread_manager.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "google_apis/gaia/mock_url_fetcher_factory.h"
#include "net/base/net_errors.h"
#include "net/url_request/url_request_status.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/cros_system_api/dbus/service_constants.h"
#include "url/gurl.h"

using ::testing::Invoke;
using ::testing::Return;
using ::testing::_;

namespace chromeos {

class ParallelAuthenticatorTest : public testing::Test {
 public:
  ParallelAuthenticatorTest()
      : username_("me@nowhere.org"),
        password_("fakepass"),
        hash_ascii_(ParallelAuthenticator::HashPassword(
            password_,
            SystemSaltGetter::ConvertRawSaltToHexString(
                FakeCryptohomeClient::GetStubSystemSalt()))),
        user_manager_enabler_(new MockUserManager),
        mock_caller_(NULL) {
  }

  virtual ~ParallelAuthenticatorTest() {
    DCHECK(!mock_caller_);
  }

  virtual void SetUp() {
    CommandLine::ForCurrentProcess()->AppendSwitch(switches::kLoginManager);

    mock_caller_ = new cryptohome::MockAsyncMethodCaller;
    cryptohome::AsyncMethodCaller::InitializeForTesting(mock_caller_);

    FakeDBusThreadManager* fake_dbus_thread_manager = new FakeDBusThreadManager;
    fake_cryptohome_client_ = new FakeCryptohomeClient;
    fake_dbus_thread_manager->SetCryptohomeClient(
        scoped_ptr<CryptohomeClient>(fake_cryptohome_client_));
    DBusThreadManager::InitializeForTesting(fake_dbus_thread_manager);

    SystemSaltGetter::Initialize();

    auth_ = new ParallelAuthenticator(&consumer_);
    state_.reset(new TestAttemptState(UserContext(username_,
                                                  password_,
                                                  std::string()),
                                      "",
                                      "",
                                      User::USER_TYPE_REGULAR,
                                      false));
  }

  // Tears down the test fixture.
  virtual void TearDown() {
    SystemSaltGetter::Shutdown();
    DBusThreadManager::Shutdown();

    cryptohome::AsyncMethodCaller::Shutdown();
    mock_caller_ = NULL;
  }

  base::FilePath PopulateTempFile(const char* data, int data_len) {
    base::FilePath out;
    FILE* tmp_file = file_util::CreateAndOpenTemporaryFile(&out);
    EXPECT_NE(tmp_file, static_cast<FILE*>(NULL));
    EXPECT_EQ(file_util::WriteFile(out, data, data_len), data_len);
    EXPECT_TRUE(file_util::CloseFile(tmp_file));
    return out;
  }

  // Allow test to fail and exit gracefully, even if OnLoginFailure()
  // wasn't supposed to happen.
  void FailOnLoginFailure() {
    ON_CALL(consumer_, OnLoginFailure(_))
        .WillByDefault(Invoke(MockConsumer::OnFailQuitAndFail));
  }

  // Allow test to fail and exit gracefully, even if
  // OnRetailModeLoginSuccess() wasn't supposed to happen.
  void FailOnRetailModeLoginSuccess() {
    ON_CALL(consumer_, OnRetailModeLoginSuccess(_))
        .WillByDefault(Invoke(MockConsumer::OnRetailModeSuccessQuitAndFail));
  }

  // Allow test to fail and exit gracefully, even if OnLoginSuccess()
  // wasn't supposed to happen.
  void FailOnLoginSuccess() {
    ON_CALL(consumer_, OnLoginSuccess(_))
        .WillByDefault(Invoke(MockConsumer::OnSuccessQuitAndFail));
  }

  // Allow test to fail and exit gracefully, even if
  // OnOffTheRecordLoginSuccess() wasn't supposed to happen.
  void FailOnGuestLoginSuccess() {
    ON_CALL(consumer_, OnOffTheRecordLoginSuccess())
        .WillByDefault(Invoke(MockConsumer::OnGuestSuccessQuitAndFail));
  }

  void ExpectLoginFailure(const LoginFailure& failure) {
    EXPECT_CALL(consumer_, OnLoginFailure(failure))
        .WillOnce(Invoke(MockConsumer::OnFailQuit))
        .RetiresOnSaturation();
  }

  void ExpectRetailModeLoginSuccess() {
    EXPECT_CALL(consumer_, OnRetailModeLoginSuccess(_))
        .WillOnce(Invoke(MockConsumer::OnRetailModeSuccessQuit))
        .RetiresOnSaturation();
  }

  void ExpectLoginSuccess(const std::string& username,
                          const std::string& password,
                          const std::string& username_hash_,
                          bool pending) {
    EXPECT_CALL(consumer_, OnLoginSuccess(UserContext(username,
                                                      password,
                                                      std::string(),
                                                      username_hash_,
                                                      true /* using_oauth */)))
        .WillOnce(Invoke(MockConsumer::OnSuccessQuit))
        .RetiresOnSaturation();
  }

  void ExpectGuestLoginSuccess() {
    EXPECT_CALL(consumer_, OnOffTheRecordLoginSuccess())
        .WillOnce(Invoke(MockConsumer::OnGuestSuccessQuit))
        .RetiresOnSaturation();
  }

  void ExpectPasswordChange() {
    EXPECT_CALL(consumer_, OnPasswordChangeDetected())
        .WillOnce(Invoke(MockConsumer::OnMigrateQuit))
        .RetiresOnSaturation();
  }

  void RunResolve(ParallelAuthenticator* auth) {
    auth->Resolve();
    base::MessageLoop::current()->RunUntilIdle();
  }

  void SetAttemptState(ParallelAuthenticator* auth, TestAttemptState* state) {
    auth->set_attempt_state(state);
  }

  ParallelAuthenticator::AuthState SetAndResolveState(
      ParallelAuthenticator* auth, TestAttemptState* state) {
    auth->set_attempt_state(state);
    return auth->ResolveState();
  }

  void SetOwnerState(bool owner_check_finished, bool check_result) {
    auth_->SetOwnerState(owner_check_finished, check_result);
  }

  content::TestBrowserThreadBundle thread_bundle_;

  std::string username_;
  std::string password_;
  std::string username_hash_;
  std::string hash_ascii_;

  ScopedDeviceSettingsTestHelper device_settings_test_helper_;
  ScopedTestCrosSettings test_cros_settings_;

  ScopedUserManagerEnabler user_manager_enabler_;

  cryptohome::MockAsyncMethodCaller* mock_caller_;

  MockConsumer consumer_;
  scoped_refptr<ParallelAuthenticator> auth_;
  scoped_ptr<TestAttemptState> state_;
  FakeCryptohomeClient* fake_cryptohome_client_;
};

TEST_F(ParallelAuthenticatorTest, OnLoginSuccess) {
  EXPECT_CALL(consumer_, OnLoginSuccess(UserContext(username_,
                                                    password_,
                                                    std::string(),
                                                    username_hash_,
                                                    true /* using oauth */)))
      .Times(1)
      .RetiresOnSaturation();

  SetAttemptState(auth_.get(), state_.release());
  auth_->OnLoginSuccess();
}

TEST_F(ParallelAuthenticatorTest, OnPasswordChangeDetected) {
  EXPECT_CALL(consumer_, OnPasswordChangeDetected())
      .Times(1)
      .RetiresOnSaturation();
  SetAttemptState(auth_.get(), state_.release());
  auth_->OnPasswordChangeDetected();
}

TEST_F(ParallelAuthenticatorTest, ResolveNothingDone) {
  EXPECT_EQ(ParallelAuthenticator::CONTINUE,
            SetAndResolveState(auth_.get(), state_.release()));
}


TEST_F(ParallelAuthenticatorTest, ResolvePossiblePwChangeToFailedMount) {
  // Set up state as though a cryptohome mount attempt has occurred
  // and been rejected.
  state_->PresetCryptohomeStatus(false, cryptohome::MOUNT_ERROR_KEY_FAILURE);

  // When there is no online attempt and online results, POSSIBLE_PW_CHANGE
  EXPECT_EQ(ParallelAuthenticator::FAILED_MOUNT,
            SetAndResolveState(auth_.get(), state_.release()));
}

TEST_F(ParallelAuthenticatorTest, ResolveNeedOldPw) {
  // Set up state as though a cryptohome mount attempt has occurred
  // and been rejected because of unmatched key; additionally,
  // an online auth attempt has completed successfully.
  state_->PresetCryptohomeStatus(false, cryptohome::MOUNT_ERROR_KEY_FAILURE);
  state_->PresetOnlineLoginStatus(LoginFailure::LoginFailureNone());

  EXPECT_EQ(ParallelAuthenticator::NEED_OLD_PW,
            SetAndResolveState(auth_.get(), state_.release()));
}

TEST_F(ParallelAuthenticatorTest, ResolveOwnerNeededDirectFailedMount) {
  // Set up state as though a cryptohome mount attempt has occurred
  // and succeeded but we are in safe mode and the current user is not owner.
  // This is a high level test to verify the proper transitioning in this mode
  // only. It is not testing that we properly verify that the user is an owner
  // or that we really are in "safe-mode".
  state_->PresetCryptohomeStatus(true, cryptohome::MOUNT_ERROR_NONE);
  SetOwnerState(true, false);

  EXPECT_EQ(ParallelAuthenticator::OWNER_REQUIRED,
            SetAndResolveState(auth_.get(), state_.release()));
}

TEST_F(ParallelAuthenticatorTest, ResolveOwnerNeededMount) {
  // Set up state as though a cryptohome mount attempt has occurred
  // and succeeded but we are in safe mode and the current user is not owner.
  // This test will check that the "safe-mode" policy is not set and will let
  // the mount finish successfully.
  state_->PresetCryptohomeStatus(true, cryptohome::MOUNT_ERROR_NONE);
  SetOwnerState(false, false);
  // and test that the mount has succeeded.
  state_.reset(new TestAttemptState(UserContext(username_,
                                                password_,
                                                std::string()),
                                    "",
                                    "",
                                    User::USER_TYPE_REGULAR,
                                    false));
  state_->PresetCryptohomeStatus(true, cryptohome::MOUNT_ERROR_NONE);
  EXPECT_EQ(ParallelAuthenticator::OFFLINE_LOGIN,
            SetAndResolveState(auth_.get(), state_.release()));
}

TEST_F(ParallelAuthenticatorTest, ResolveOwnerNeededFailedMount) {
  FailOnLoginSuccess();  // Set failing on success as the default...
  LoginFailure failure = LoginFailure(LoginFailure::OWNER_REQUIRED);
  ExpectLoginFailure(failure);

  fake_cryptohome_client_->set_unmount_result(true);

  CrosSettingsProvider* device_settings_provider;
  StubCrosSettingsProvider stub_settings_provider;
  // Set up state as though a cryptohome mount attempt has occurred
  // and succeeded but we are in safe mode and the current user is not owner.
  state_->PresetCryptohomeStatus(true, cryptohome::MOUNT_ERROR_NONE);
  SetOwnerState(false, false);
  // Remove the real DeviceSettingsProvider and replace it with a stub.
  device_settings_provider =
      CrosSettings::Get()->GetProvider(chromeos::kReportDeviceVersionInfo);
  EXPECT_TRUE(device_settings_provider != NULL);
  EXPECT_TRUE(
      CrosSettings::Get()->RemoveSettingsProvider(device_settings_provider));
  CrosSettings::Get()->AddSettingsProvider(&stub_settings_provider);
  CrosSettings::Get()->SetBoolean(kPolicyMissingMitigationMode, true);

  // Initialize login state for this test to verify the login state is changed
  // to SAFE_MODE.
  LoginState::Initialize();

  EXPECT_EQ(ParallelAuthenticator::CONTINUE,
            SetAndResolveState(auth_.get(), state_.release()));
  EXPECT_TRUE(LoginState::Get()->IsInSafeMode());

  // Simulate certificates load event. The exact certificates loaded are not
  // actually used by the DeviceSettingsService, so it is OK to pass an empty
  // list.
  DeviceSettingsService::Get()->OnCertificatesLoaded(net::CertificateList(),
                                                     true);
  // Flush all the pending operations. The operations should induce an owner
  // verification.
  device_settings_test_helper_.Flush();
  // and test that the mount has succeeded.
  state_.reset(new TestAttemptState(UserContext(username_,
                                                password_,
                                                std::string()),
                                    "",
                                    "",
                                    User::USER_TYPE_REGULAR,
                                    false));
  state_->PresetCryptohomeStatus(true, cryptohome::MOUNT_ERROR_NONE);
  EXPECT_EQ(ParallelAuthenticator::OWNER_REQUIRED,
            SetAndResolveState(auth_.get(), state_.release()));

  // Unset global objects used by this test.
  LoginState::Shutdown();
  EXPECT_TRUE(
      CrosSettings::Get()->RemoveSettingsProvider(&stub_settings_provider));
  CrosSettings::Get()->AddSettingsProvider(device_settings_provider);
}

TEST_F(ParallelAuthenticatorTest, DriveFailedMount) {
  FailOnLoginSuccess();
  ExpectLoginFailure(LoginFailure(LoginFailure::COULD_NOT_MOUNT_CRYPTOHOME));

  // Set up state as though a cryptohome mount attempt has occurred
  // and failed.
  state_->PresetCryptohomeStatus(false, cryptohome::MOUNT_ERROR_NONE);
  SetAttemptState(auth_.get(), state_.release());

  RunResolve(auth_.get());
}

TEST_F(ParallelAuthenticatorTest, DriveGuestLogin) {
  ExpectGuestLoginSuccess();
  FailOnLoginFailure();

  // Set up mock async method caller to respond as though a tmpfs mount
  // attempt has occurred and succeeded.
  mock_caller_->SetUp(true, cryptohome::MOUNT_ERROR_NONE);
  EXPECT_CALL(*mock_caller_, AsyncMountGuest(_))
      .Times(1)
      .RetiresOnSaturation();

  auth_->LoginOffTheRecord();
  base::MessageLoop::current()->Run();
}

TEST_F(ParallelAuthenticatorTest, DriveGuestLoginButFail) {
  FailOnGuestLoginSuccess();
  ExpectLoginFailure(LoginFailure(LoginFailure::COULD_NOT_MOUNT_TMPFS));

  // Set up mock async method caller to respond as though a tmpfs mount
  // attempt has occurred and failed.
  mock_caller_->SetUp(false, cryptohome::MOUNT_ERROR_NONE);
  EXPECT_CALL(*mock_caller_, AsyncMountGuest(_))
      .Times(1)
      .RetiresOnSaturation();

  auth_->LoginOffTheRecord();
  base::MessageLoop::current()->Run();
}

TEST_F(ParallelAuthenticatorTest, DriveRetailModeUserLogin) {
  ExpectRetailModeLoginSuccess();
  FailOnLoginFailure();

  // Set up mock async method caller to respond as though a tmpfs mount
  // attempt has occurred and succeeded.
  mock_caller_->SetUp(true, cryptohome::MOUNT_ERROR_NONE);
  EXPECT_CALL(*mock_caller_, AsyncMountGuest(_))
      .Times(1)
      .RetiresOnSaturation();

  auth_->LoginRetailMode();
  base::MessageLoop::current()->Run();
}

TEST_F(ParallelAuthenticatorTest, DriveRetailModeLoginButFail) {
  FailOnRetailModeLoginSuccess();
  ExpectLoginFailure(LoginFailure(LoginFailure::COULD_NOT_MOUNT_TMPFS));

  // Set up mock async method caller to respond as though a tmpfs mount
  // attempt has occurred and failed.
  mock_caller_->SetUp(false, cryptohome::MOUNT_ERROR_NONE);
  EXPECT_CALL(*mock_caller_, AsyncMountGuest(_))
      .Times(1)
      .RetiresOnSaturation();

  auth_->LoginRetailMode();
  base::MessageLoop::current()->Run();
}

TEST_F(ParallelAuthenticatorTest, DriveDataResync) {
  ExpectLoginSuccess(username_,
                     password_,
                     cryptohome::MockAsyncMethodCaller::kFakeSanitizedUsername,
                     false);
  FailOnLoginFailure();

  // Set up mock async method caller to respond successfully to a cryptohome
  // remove attempt and a cryptohome create attempt (indicated by the
  // |CREATE_IF_MISSING| flag to AsyncMount).
  mock_caller_->SetUp(true, cryptohome::MOUNT_ERROR_NONE);
  EXPECT_CALL(*mock_caller_, AsyncRemove(username_, _))
      .Times(1)
      .RetiresOnSaturation();
  EXPECT_CALL(*mock_caller_, AsyncMount(username_, hash_ascii_,
                                        cryptohome::CREATE_IF_MISSING, _))
      .Times(1)
      .RetiresOnSaturation();
  EXPECT_CALL(*mock_caller_, AsyncGetSanitizedUsername(username_, _))
      .Times(1)
      .RetiresOnSaturation();

  state_->PresetOnlineLoginStatus(LoginFailure::LoginFailureNone());
  SetAttemptState(auth_.get(), state_.release());

  auth_->ResyncEncryptedData();
  base::MessageLoop::current()->Run();
}

TEST_F(ParallelAuthenticatorTest, DriveResyncFail) {
  FailOnLoginSuccess();
  ExpectLoginFailure(LoginFailure(LoginFailure::DATA_REMOVAL_FAILED));

  // Set up mock async method caller to fail a cryptohome remove attempt.
  mock_caller_->SetUp(false, cryptohome::MOUNT_ERROR_NONE);
  EXPECT_CALL(*mock_caller_, AsyncRemove(username_, _))
      .Times(1)
      .RetiresOnSaturation();

  SetAttemptState(auth_.get(), state_.release());

  auth_->ResyncEncryptedData();
  base::MessageLoop::current()->Run();
}

TEST_F(ParallelAuthenticatorTest, DriveRequestOldPassword) {
  FailOnLoginSuccess();
  ExpectPasswordChange();

  state_->PresetCryptohomeStatus(false, cryptohome::MOUNT_ERROR_KEY_FAILURE);
  state_->PresetOnlineLoginStatus(LoginFailure::LoginFailureNone());
  SetAttemptState(auth_.get(), state_.release());

  RunResolve(auth_.get());
}

TEST_F(ParallelAuthenticatorTest, DriveDataRecover) {
  ExpectLoginSuccess(username_,
                     password_,
                     cryptohome::MockAsyncMethodCaller::kFakeSanitizedUsername,
                     false);
  FailOnLoginFailure();

  // Set up mock async method caller to respond successfully to a key migration.
  mock_caller_->SetUp(true, cryptohome::MOUNT_ERROR_NONE);
  EXPECT_CALL(*mock_caller_, AsyncMigrateKey(username_, _, hash_ascii_, _))
      .Times(1)
      .RetiresOnSaturation();
  EXPECT_CALL(*mock_caller_, AsyncMount(username_, hash_ascii_,
                                        cryptohome::MOUNT_FLAGS_NONE, _))
      .Times(1)
      .RetiresOnSaturation();
  EXPECT_CALL(*mock_caller_, AsyncGetSanitizedUsername(username_, _))
        .Times(1)
        .RetiresOnSaturation();

  state_->PresetOnlineLoginStatus(LoginFailure::LoginFailureNone());
  SetAttemptState(auth_.get(), state_.release());

  auth_->RecoverEncryptedData(std::string());
  base::MessageLoop::current()->Run();
}

TEST_F(ParallelAuthenticatorTest, DriveDataRecoverButFail) {
  FailOnLoginSuccess();
  ExpectPasswordChange();

  // Set up mock async method caller to fail a key migration attempt,
  // asserting that the wrong password was used.
  mock_caller_->SetUp(false, cryptohome::MOUNT_ERROR_KEY_FAILURE);
  EXPECT_CALL(*mock_caller_, AsyncMigrateKey(username_, _, hash_ascii_, _))
      .Times(1)
      .RetiresOnSaturation();

  SetAttemptState(auth_.get(), state_.release());

  auth_->RecoverEncryptedData(std::string());
  base::MessageLoop::current()->Run();
}

TEST_F(ParallelAuthenticatorTest, ResolveNoMountToFailedMount) {
  // Set up state as though a cryptohome mount attempt has occurred
  // and been rejected because the user doesn't exist.
  state_->PresetCryptohomeStatus(false,
                                 cryptohome::MOUNT_ERROR_USER_DOES_NOT_EXIST);

  // When there is no online attempt and online results, NO_MOUNT will be
  // resolved to FAILED_MOUNT.
  EXPECT_EQ(ParallelAuthenticator::FAILED_MOUNT,
            SetAndResolveState(auth_.get(), state_.release()));
}

TEST_F(ParallelAuthenticatorTest, ResolveCreateNew) {
  // Set up state as though a cryptohome mount attempt has occurred
  // and been rejected because the user doesn't exist; additionally,
  // an online auth attempt has completed successfully.
  state_->PresetCryptohomeStatus(false,
                                 cryptohome::MOUNT_ERROR_USER_DOES_NOT_EXIST);
  state_->PresetOnlineLoginStatus(LoginFailure::LoginFailureNone());

  EXPECT_EQ(ParallelAuthenticator::CREATE_NEW,
            SetAndResolveState(auth_.get(), state_.release()));
}

TEST_F(ParallelAuthenticatorTest, DriveCreateForNewUser) {
  ExpectLoginSuccess(username_,
                     password_,
                     cryptohome::MockAsyncMethodCaller::kFakeSanitizedUsername,
                     false);
  FailOnLoginFailure();

  // Set up mock async method caller to respond successfully to a cryptohome
  // create attempt (indicated by the |CREATE_IF_MISSING| flag to AsyncMount).
  mock_caller_->SetUp(true, cryptohome::MOUNT_ERROR_NONE);
  EXPECT_CALL(*mock_caller_, AsyncMount(username_, hash_ascii_,
                                        cryptohome::CREATE_IF_MISSING, _))
      .Times(1)
      .RetiresOnSaturation();
  EXPECT_CALL(*mock_caller_, AsyncGetSanitizedUsername(username_, _))
      .Times(1)
      .RetiresOnSaturation();

  // Set up state as though a cryptohome mount attempt has occurred
  // and been rejected because the user doesn't exist; additionally,
  // an online auth attempt has completed successfully.
  state_->PresetCryptohomeStatus(false,
                                 cryptohome::MOUNT_ERROR_USER_DOES_NOT_EXIST);
  state_->PresetOnlineLoginStatus(LoginFailure::LoginFailureNone());
  SetAttemptState(auth_.get(), state_.release());

  RunResolve(auth_.get());
}

TEST_F(ParallelAuthenticatorTest, DriveOfflineLogin) {
  ExpectLoginSuccess(username_, password_, username_hash_, false);
  FailOnLoginFailure();

  // Set up state as though a cryptohome mount attempt has occurred and
  // succeeded.
  state_->PresetCryptohomeStatus(true, cryptohome::MOUNT_ERROR_NONE);
  SetAttemptState(auth_.get(), state_.release());

  RunResolve(auth_.get());
}

TEST_F(ParallelAuthenticatorTest, DriveOnlineLogin) {
  ExpectLoginSuccess(username_, password_, username_hash_, false);
  FailOnLoginFailure();

  // Set up state as though a cryptohome mount attempt has occurred and
  // succeeded.
  state_->PresetCryptohomeStatus(true, cryptohome::MOUNT_ERROR_NONE);
  state_->PresetOnlineLoginStatus(LoginFailure::LoginFailureNone());
  SetAttemptState(auth_.get(), state_.release());

  RunResolve(auth_.get());
}

TEST_F(ParallelAuthenticatorTest, DriveUnlock) {
  ExpectLoginSuccess(username_, std::string(), std::string(), false);
  FailOnLoginFailure();

  // Set up mock async method caller to respond successfully to a cryptohome
  // key-check attempt.
  mock_caller_->SetUp(true, cryptohome::MOUNT_ERROR_NONE);
  EXPECT_CALL(*mock_caller_, AsyncCheckKey(username_, _, _))
      .Times(1)
      .RetiresOnSaturation();

  auth_->AuthenticateToUnlock(UserContext(username_,
                                          std::string(),
                                          std::string()));
  base::MessageLoop::current()->Run();
}

}  // namespace chromeos
