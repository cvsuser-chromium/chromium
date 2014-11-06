// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/signin/signin_tracker.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/compiler_specific.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/signin/fake_auth_status_provider.h"
#include "chrome/browser/signin/fake_profile_oauth2_token_service.h"
#include "chrome/browser/signin/fake_signin_manager.h"
#include "chrome/browser/signin/profile_oauth2_token_service.h"
#include "chrome/browser/signin/profile_oauth2_token_service_factory.h"
#include "chrome/browser/signin/signin_manager.h"
#include "chrome/browser/signin/signin_manager_factory.h"
#include "chrome/browser/sync/profile_sync_service_factory.h"
#include "chrome/browser/sync/profile_sync_service_mock.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/browser/notification_service.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "google_apis/gaia/gaia_constants.h"
#include "google_apis/gaia/google_service_auth_error.h"

#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::Mock;
using ::testing::Return;
using ::testing::ReturnRef;

namespace {

class MockObserver : public SigninTracker::Observer {
 public:
  MockObserver() {}
  ~MockObserver() {}

  MOCK_METHOD1(SigninFailed, void(const GoogleServiceAuthError&));
  MOCK_METHOD0(SigninSuccess, void(void));
};

}  // namespace

class SigninTrackerTest : public testing::Test {
 public:
  SigninTrackerTest() {}
  virtual void SetUp() OVERRIDE {
    TestingProfile::Builder builder;
    builder.AddTestingFactory(ProfileOAuth2TokenServiceFactory::GetInstance(),
                              FakeProfileOAuth2TokenService::Build);

    profile_ = builder.Build();

    fake_oauth2_token_service_ =
        static_cast<FakeProfileOAuth2TokenService*>(
            ProfileOAuth2TokenServiceFactory::GetForProfile(profile_.get()));

    mock_signin_manager_ = static_cast<FakeSigninManagerBase*>(
        SigninManagerFactory::GetInstance()->SetTestingFactoryAndUse(
            profile_.get(), FakeSigninManagerBase::Build));
    mock_signin_manager_->Initialize(profile_.get(), NULL);

    tracker_.reset(new SigninTracker(profile_.get(), &observer_));
  }
  virtual void TearDown() OVERRIDE {
    tracker_.reset();
    profile_.reset();
  }

  content::TestBrowserThreadBundle thread_bundle_;
  scoped_ptr<SigninTracker> tracker_;
  scoped_ptr<TestingProfile> profile_;
  FakeSigninManagerBase* mock_signin_manager_;
  FakeProfileOAuth2TokenService* fake_oauth2_token_service_;
  MockObserver observer_;
};

TEST_F(SigninTrackerTest, SignInFails) {
  // SIGNIN_FAILED notification should result in a SigninFailed callback.
  const GoogleServiceAuthError error(
      GoogleServiceAuthError::INVALID_GAIA_CREDENTIALS);
  EXPECT_CALL(observer_, SigninSuccess()).Times(0);
  EXPECT_CALL(observer_, SigninFailed(error));

  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_GOOGLE_SIGNIN_FAILED,
      content::Source<Profile>(profile_.get()),
      content::Details<const GoogleServiceAuthError>(&error));
}

TEST_F(SigninTrackerTest, SignInSucceeds) {
  EXPECT_CALL(observer_, SigninSuccess());
  EXPECT_CALL(observer_, SigninFailed(_)).Times(0);

  mock_signin_manager_->SetAuthenticatedUsername("user@gmail.com");
  fake_oauth2_token_service_->IssueRefreshToken("refresh_token");
}
