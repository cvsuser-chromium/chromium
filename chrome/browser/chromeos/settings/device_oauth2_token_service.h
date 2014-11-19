// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_SETTINGS_DEVICE_OAUTH2_TOKEN_SERVICE_H_
#define CHROME_BROWSER_CHROMEOS_SETTINGS_DEVICE_OAUTH2_TOKEN_SERVICE_H_

#include <set>
#include <string>

#include "base/basictypes.h"
#include "base/gtest_prod_util.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/stl_util.h"
#include "base/time/time.h"
#include "google_apis/gaia/gaia_oauth_client.h"
#include "google_apis/gaia/oauth2_token_service.h"
#include "net/url_request/url_request_context_getter.h"

namespace net {
class URLRequestContextGetter;
}

class GoogleServiceAuthError;
class PrefRegistrySimple;
class PrefService;
class Profile;

namespace chromeos {

class TokenEncryptor;

// DeviceOAuth2TokenService retrieves OAuth2 access tokens for a given
// set of scopes using the device-level OAuth2 any-api refresh token
// obtained during enterprise device enrollment.
//
// See |OAuth2TokenService| for usage details.
//
// When using DeviceOAuth2TokenSerivce, a value of |GetRobotAccountId| should
// be used in places where API expects |account_id|.
//
// Note that requests must be made from the UI thread.
class DeviceOAuth2TokenService : public OAuth2TokenService {
 public:
  // Persist the given refresh token on the device.  Overwrites any previous
  // value.  Should only be called during initial device setup.  Returns false
  // if there was an error encrypting and persisting the value, else true.
  bool SetAndSaveRefreshToken(const std::string& refresh_token);

  static void RegisterPrefs(PrefRegistrySimple* registry);

  // Gets the refresh token used by the service. |account_id| is expected to be
  // a value of |GetRobotAccountId|.
  virtual std::string GetRefreshToken(const std::string& account_id) OVERRIDE;

  // Pull the robot account ID from device policy.
  virtual std::string GetRobotAccountId();

 protected:
  // Implementation of OAuth2TokenService.
  virtual net::URLRequestContextGetter* GetRequestContext() OVERRIDE;
  virtual scoped_ptr<OAuth2TokenService::RequestImpl> CreateRequest(
      const std::string& account_id,
      OAuth2TokenService::Consumer* consumer) OVERRIDE;

 private:
  class ValidatingConsumer;
  friend class ValidatingConsumer;
  friend class DeviceOAuth2TokenServiceFactory;
  friend class DeviceOAuth2TokenServiceTest;
  friend class TestDeviceOAuth2TokenService;

  // Use DeviceOAuth2TokenServiceFactory to get an instance of this class.
  // Ownership of |token_encryptor| will be taken.
  explicit DeviceOAuth2TokenService(net::URLRequestContextGetter* getter,
                                    PrefService* local_state,
                                    TokenEncryptor* token_encryptor);
  virtual ~DeviceOAuth2TokenService();

  void OnValidationComplete(bool token_is_valid);

  bool refresh_token_is_valid_;
  int max_refresh_token_validation_retries_;

  scoped_refptr<net::URLRequestContextGetter> url_request_context_getter_;

  // Cache the decrypted refresh token, so we only decrypt once.
  std::string refresh_token_;
  PrefService* local_state_;

  // Used to encrypt/decrypt the refresh token.
  scoped_ptr<TokenEncryptor> token_encryptor_;

  base::WeakPtrFactory<DeviceOAuth2TokenService> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(DeviceOAuth2TokenService);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_SETTINGS_DEVICE_OAUTH2_TOKEN_SERVICE_H_
