// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_LOGIN_MOCK_USER_MANAGER_H_
#define CHROME_BROWSER_CHROMEOS_LOGIN_MOCK_USER_MANAGER_H_

#include <string>

#include "base/files/file_path.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/chromeos/login/mock_user_image_manager.h"
#include "chrome/browser/chromeos/login/user.h"
#include "chrome/browser/chromeos/login/user_flow.h"
#include "chrome/browser/chromeos/login/user_image.h"
#include "chrome/browser/chromeos/login/user_manager.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace chromeos {

class FakeSupervisedUserManager;

class MockUserManager : public UserManager {
 public:
  MockUserManager();
  virtual ~MockUserManager();

  MOCK_METHOD0(Shutdown, void(void));
  MOCK_CONST_METHOD0(GetUsersAdmittedForMultiProfile, UserList(void));
  MOCK_CONST_METHOD0(GetLoggedInUsers, const UserList&(void));
  MOCK_METHOD0(GetLRULoggedInUsers, const UserList&(void));
  MOCK_METHOD3(UserLoggedIn, void(
      const std::string&, const std::string&, bool));
  MOCK_METHOD1(SwitchActiveUser, void(const std::string& email));
  MOCK_METHOD0(SessionStarted, void(void));
  MOCK_METHOD0(RestoreActiveSessions, void(void));
  MOCK_METHOD2(RemoveUser, void(const std::string&, RemoveUserDelegate*));
  MOCK_METHOD1(RemoveUserFromList, void(const std::string&));
  MOCK_CONST_METHOD1(IsKnownUser, bool(const std::string&));
  MOCK_CONST_METHOD1(FindUser, const User*(const std::string&));
  MOCK_METHOD2(SaveUserOAuthStatus, void(const std::string&,
                                         User::OAuthTokenStatus));
  MOCK_METHOD2(SaveUserDisplayName, void(const std::string&,
                                         const string16&));
  MOCK_METHOD3(UpdateUserAccountData,
               void(const std::string&, const string16&, const std::string&));
  MOCK_CONST_METHOD1(GetUserDisplayName, string16(const std::string&));
  MOCK_METHOD2(SaveUserDisplayEmail, void(const std::string&,
                                          const std::string&));
  MOCK_CONST_METHOD1(GetUserDisplayEmail, std::string(const std::string&));
  MOCK_CONST_METHOD0(IsCurrentUserOwner, bool(void));
  MOCK_CONST_METHOD0(IsCurrentUserNew, bool(void));
  MOCK_CONST_METHOD0(IsCurrentUserNonCryptohomeDataEphemeral, bool(void));
  MOCK_CONST_METHOD0(CanCurrentUserLock, bool(void));
  MOCK_CONST_METHOD0(IsUserLoggedIn, bool(void));
  MOCK_CONST_METHOD0(IsLoggedInAsRegularUser, bool(void));
  MOCK_CONST_METHOD0(IsLoggedInAsDemoUser, bool(void));
  MOCK_CONST_METHOD0(IsLoggedInAsPublicAccount, bool(void));
  MOCK_CONST_METHOD0(IsLoggedInAsGuest, bool(void));
  MOCK_CONST_METHOD0(IsLoggedInAsLocallyManagedUser, bool(void));
  MOCK_CONST_METHOD0(IsLoggedInAsKioskApp, bool(void));
  MOCK_CONST_METHOD0(IsLoggedInAsStub, bool(void));
  MOCK_CONST_METHOD0(IsSessionStarted, bool(void));
  MOCK_CONST_METHOD0(UserSessionsRestored, bool(void));
  MOCK_CONST_METHOD0(HasBrowserRestarted, bool(void));
  MOCK_CONST_METHOD1(IsUserNonCryptohomeDataEphemeral,
                     bool(const std::string&));
  MOCK_METHOD1(AddObserver, void(UserManager::Observer*));
  MOCK_METHOD1(RemoveObserver, void(UserManager::Observer*));
  MOCK_METHOD1(AddSessionStateObserver,
               void(UserManager::UserSessionStateObserver*));
  MOCK_METHOD1(RemoveSessionStateObserver,
               void(UserManager::UserSessionStateObserver*));
  MOCK_METHOD0(NotifyLocalStateChanged, void(void));
  MOCK_METHOD2(SetUserFlow, void(const std::string&, UserFlow*));
  MOCK_METHOD1(ResetUserFlow, void(const std::string&));

  MOCK_METHOD2(GetAppModeChromeClientOAuthInfo, bool(std::string*,
                                                     std::string*));
  MOCK_METHOD2(SetAppModeChromeClientOAuthInfo, void(const std::string&,
                                                     const std::string&));
  MOCK_CONST_METHOD0(AreLocallyManagedUsersAllowed, bool(void));
  MOCK_CONST_METHOD1(GetUserProfileDir,
                     base::FilePath(const std::string& email));

  // You can't mock these functions easily because nobody can create
  // User objects but the UserManagerImpl and us.
  virtual const UserList& GetUsers() const OVERRIDE;
  virtual const User* GetLoggedInUser() const OVERRIDE;
  virtual UserList GetUnlockUsers() const OVERRIDE;
  virtual const std::string& GetOwnerEmail() OVERRIDE;
  virtual User* GetLoggedInUser() OVERRIDE;
  virtual const User* GetActiveUser() const OVERRIDE;
  virtual User* GetActiveUser() OVERRIDE;
  virtual const User* GetPrimaryUser() const OVERRIDE;
  virtual User* GetUserByProfile(Profile* profile) const OVERRIDE;

  virtual UserImageManager* GetUserImageManager() OVERRIDE;
  virtual SupervisedUserManager* GetSupervisedUserManager() OVERRIDE;

  virtual UserFlow* GetCurrentUserFlow() const OVERRIDE;
  virtual UserFlow* GetUserFlow(const std::string&) const OVERRIDE;
  virtual void RespectLocalePreference(Profile* profile, const User* user) const
      OVERRIDE;

  // Sets a new User instance. Users previously created by this MockUserManager
  // become invalid.
  void SetActiveUser(const std::string& email);

  // Creates a new public session user. Users previously created by this
  // MockUserManager become invalid.
  User* CreatePublicAccountUser(const std::string& email);

  // Adds a new User instance to the back of the user list. Users previously
  // created by this MockUserManager remain valid.
  void AddUser(const std::string& email);

  // Clears the user list and the active user. Users previously created by this
  // MockUserManager become invalid.
  void ClearUserList();

  scoped_ptr<UserFlow> user_flow_;
  scoped_ptr<MockUserImageManager> user_image_manager_;
  scoped_ptr<FakeSupervisedUserManager> supervised_user_manager_;
  UserList user_list_;
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_LOGIN_MOCK_USER_MANAGER_H_
