// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/shell.h"
#include "ash/system/system_notifier.h"
#include "ash/test/ash_test_base.h"
#include "base/command_line.h"
#include "chrome/browser/notifications/login_state_notification_blocker_chromeos.h"
#include "chromeos/login/login_state.h"
#include "ui/message_center/message_center.h"

class LoginStateNotificationBlockerChromeOSTest
    : public ash::test::AshTestBase,
      public message_center::NotificationBlocker::Observer {
 public:
  LoginStateNotificationBlockerChromeOSTest()
      : state_changed_count_(0) {}
  virtual ~LoginStateNotificationBlockerChromeOSTest() {}

  // ash::tests::AshTestBase overrides:
  virtual void SetUp() OVERRIDE {
    chromeos::LoginState::Initialize();
    chromeos::LoginState::Get()->set_always_logged_in(false);
    ash::test::AshTestBase::SetUp();
    blocker_.reset(new LoginStateNotificationBlockerChromeOS(
        message_center::MessageCenter::Get()));
    blocker_->AddObserver(this);
  }

  virtual void TearDown() OVERRIDE {
    blocker_->RemoveObserver(this);
    blocker_.reset();
    ash::test::AshTestBase::TearDown();
    chromeos::LoginState::Shutdown();
  }

  message_center::NotificationBlocker* blocker() { return blocker_.get(); }

  // message_center::NotificationBlocker::Observer ovverrides:
  virtual void OnBlockingStateChanged() OVERRIDE {
    state_changed_count_++;
  }

  int GetStateChangedCountAndReset() {
    int result = state_changed_count_;
    state_changed_count_ = 0;
    return result;
  }

 private:
  int state_changed_count_;
  scoped_ptr<LoginStateNotificationBlockerChromeOS> blocker_;

  DISALLOW_COPY_AND_ASSIGN(LoginStateNotificationBlockerChromeOSTest);
};

TEST_F(LoginStateNotificationBlockerChromeOSTest, BaseTest) {
  // Default status: OOBE.
  message_center::NotifierId notifier_id;
  EXPECT_FALSE(blocker()->ShouldShowNotificationAsPopup(notifier_id));

  // Login screen.
  chromeos::LoginState::Get()->SetLoggedInState(
      chromeos::LoginState::LOGGED_IN_NONE,
      chromeos::LoginState::LOGGED_IN_USER_NONE);
  EXPECT_EQ(1, GetStateChangedCountAndReset());
  EXPECT_FALSE(blocker()->ShouldShowNotificationAsPopup(notifier_id));

  // Logged in as a normal user.
  chromeos::LoginState::Get()->SetLoggedInState(
      chromeos::LoginState::LOGGED_IN_ACTIVE,
      chromeos::LoginState::LOGGED_IN_USER_REGULAR);
  EXPECT_EQ(1, GetStateChangedCountAndReset());
  EXPECT_TRUE(blocker()->ShouldShowNotificationAsPopup(notifier_id));

  // Lock.
  ash::Shell::GetInstance()->OnLockStateChanged(true);
  EXPECT_EQ(1, GetStateChangedCountAndReset());
  EXPECT_FALSE(blocker()->ShouldShowNotificationAsPopup(notifier_id));

  // Unlock.
  ash::Shell::GetInstance()->OnLockStateChanged(false);
  EXPECT_EQ(1, GetStateChangedCountAndReset());
  EXPECT_TRUE(blocker()->ShouldShowNotificationAsPopup(notifier_id));
}

TEST_F(LoginStateNotificationBlockerChromeOSTest, AlwaysAllowedNotifier) {
  // NOTIFIER_DISPLAY is allowed to shown in the login screen.
  message_center::NotifierId notifier_id(
      ash::system_notifier::NOTIFIER_DISPLAY);

  // Default status: OOBE.
  EXPECT_TRUE(blocker()->ShouldShowNotificationAsPopup(notifier_id));

  // Login screen.
  chromeos::LoginState::Get()->SetLoggedInState(
      chromeos::LoginState::LOGGED_IN_NONE,
      chromeos::LoginState::LOGGED_IN_USER_NONE);
  EXPECT_EQ(1, GetStateChangedCountAndReset());
  EXPECT_TRUE(blocker()->ShouldShowNotificationAsPopup(notifier_id));

  // Logged in as a normal user.
  chromeos::LoginState::Get()->SetLoggedInState(
      chromeos::LoginState::LOGGED_IN_ACTIVE,
      chromeos::LoginState::LOGGED_IN_USER_REGULAR);
  EXPECT_EQ(1, GetStateChangedCountAndReset());
  EXPECT_TRUE(blocker()->ShouldShowNotificationAsPopup(notifier_id));

  // Lock.
  ash::Shell::GetInstance()->OnLockStateChanged(true);
  EXPECT_EQ(1, GetStateChangedCountAndReset());
  EXPECT_TRUE(blocker()->ShouldShowNotificationAsPopup(notifier_id));

  // Unlock.
  ash::Shell::GetInstance()->OnLockStateChanged(false);
  EXPECT_EQ(1, GetStateChangedCountAndReset());
  EXPECT_TRUE(blocker()->ShouldShowNotificationAsPopup(notifier_id));
}
