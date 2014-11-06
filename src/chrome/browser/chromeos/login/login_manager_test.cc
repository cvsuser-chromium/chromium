// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/login/login_manager_test.h"

#include "base/prefs/scoped_user_pref_update.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/chromeos/login/existing_user_controller.h"
#include "chrome/browser/chromeos/login/login_display_host_impl.h"
#include "chrome/browser/chromeos/login/user_manager.h"
#include "chrome/browser/chromeos/login/webui_login_view.h"
#include "chrome/common/chrome_switches.h"
#include "chromeos/chromeos_switches.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace chromeos {

LoginManagerTest::LoginManagerTest(bool should_launch_browser)
    : should_launch_browser_(should_launch_browser),
      web_contents_(NULL) {
  set_exit_when_last_browser_closes(false);
}

void LoginManagerTest::CleanUpOnMainThread() {
  if (LoginDisplayHostImpl::default_host())
    LoginDisplayHostImpl::default_host()->Finalize();
  base::MessageLoop::current()->RunUntilIdle();
}

void LoginManagerTest::SetUpCommandLine(CommandLine* command_line) {
  command_line->AppendSwitch(chromeos::switches::kLoginManager);
  command_line->AppendSwitch(chromeos::switches::kForceLoginManagerInTests);
  command_line->AppendSwitch(::switches::kMultiProfiles);
  InProcessBrowserTest::SetUpCommandLine(command_line);
}

void LoginManagerTest::SetUpInProcessBrowserTestFixture() {
  mock_login_utils_ = new testing::NiceMock<MockLoginUtils>();
  mock_login_utils_->DelegateToFake();
  mock_login_utils_->GetFakeLoginUtils()->set_should_launch_browser(
      should_launch_browser_);
  LoginUtils::Set(mock_login_utils_);
}

void LoginManagerTest::SetUpOnMainThread() {
  content::WindowedNotificationObserver(
      chrome::NOTIFICATION_LOGIN_OR_LOCK_WEBUI_VISIBLE,
      content::NotificationService::AllSources()).Wait();
  InitializeWebContents();
}

void LoginManagerTest::RegisterUser(const std::string& username) {
  ListPrefUpdate users_pref(g_browser_process->local_state(), "LoggedInUsers");
  users_pref->AppendIfNotPresent(new base::StringValue(username));
}

void LoginManagerTest::SetExpectedCredentials(const std::string& username,
                                              const std::string& password) {
  login_utils().GetFakeLoginUtils()->SetExpectedCredentials(username, password);
}

bool LoginManagerTest::TryToLogin(const std::string& username,
                                  const std::string& password) {
  if (!AddUserTosession(username, password))
    return false;
  if (const User* active_user = UserManager::Get()->GetActiveUser())
    return active_user->email() == username;
  return false;
}

bool LoginManagerTest::AddUserTosession(const std::string& username,
                                        const std::string& password) {
  ExistingUserController* controller =
      ExistingUserController::current_controller();
  EXPECT_TRUE(controller != NULL);
  controller->Login(UserContext(username, password, std::string()));
  content::WindowedNotificationObserver(
      chrome::NOTIFICATION_SESSION_STARTED,
      content::NotificationService::AllSources()).Wait();
  const UserList& logged_users = UserManager::Get()->GetLoggedInUsers();
  for (UserList::const_iterator it = logged_users.begin();
       it != logged_users.end(); ++it) {
    if ((*it)->email() == username)
      return true;
  }
  return false;
}

void LoginManagerTest::LoginUser(const std::string& username) {
  SetExpectedCredentials(username, "password");
  EXPECT_TRUE(TryToLogin(username, "password"));
}

void LoginManagerTest::AddUser(const std::string& username) {
  SetExpectedCredentials(username, "password");
  EXPECT_TRUE(AddUserTosession(username, "password"));
}

void LoginManagerTest::JSExpect(const std::string& expression) {
  bool result;
  EXPECT_TRUE(web_contents_ != NULL);
  ASSERT_TRUE(content::ExecuteScriptAndExtractBool(
      web_contents(),
      "window.domAutomationController.send(!!(" + expression + "));",
      &result));
  ASSERT_TRUE(result) << expression;
}

void LoginManagerTest::InitializeWebContents() {
    LoginDisplayHost* host = LoginDisplayHostImpl::default_host();
    EXPECT_TRUE(host != NULL);

    content::WebContents* web_contents =
        host->GetWebUILoginView()->GetWebContents();
    EXPECT_TRUE(web_contents != NULL);
    set_web_contents(web_contents);
  }

}  // namespace chromeos
