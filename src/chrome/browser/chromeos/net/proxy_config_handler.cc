// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/net/proxy_config_handler.h"

#include "base/bind.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/prefs/pref_registry_simple.h"
#include "base/values.h"
#include "chrome/browser/chromeos/net/onc_utils.h"
#include "chrome/browser/prefs/proxy_config_dictionary.h"
#include "chrome/common/pref_names.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "chromeos/dbus/shill_service_client.h"
#include "chromeos/network/favorite_state.h"
#include "chromeos/network/network_handler_callbacks.h"
#include "chromeos/network/network_profile.h"
#include "chromeos/network/network_profile_handler.h"
#include "chromeos/network/network_state_handler.h"
#include "components/user_prefs/pref_registry_syncable.h"
#include "dbus/object_path.h"
#include "third_party/cros_system_api/dbus/service_constants.h"

namespace chromeos {

namespace {

void NotifyNetworkStateHandler(const std::string& service_path) {
  if (NetworkHandler::IsInitialized()) {
    NetworkHandler::Get()->network_state_handler()->RequestUpdateForNetwork(
        service_path);
  }
}

}  // namespace

namespace proxy_config {

scoped_ptr<ProxyConfigDictionary> GetProxyConfigForFavoriteNetwork(
    const PrefService* profile_prefs,
    const PrefService* local_state_prefs,
    const FavoriteState& network,
    ::onc::ONCSource* onc_source) {
  const base::DictionaryValue* network_policy =
      onc::GetPolicyForFavoriteNetwork(
          profile_prefs, local_state_prefs, network, onc_source);

  if (network_policy) {
    const base::DictionaryValue* proxy_policy = NULL;
    network_policy->GetDictionaryWithoutPathExpansion(
        ::onc::network_config::kProxySettings, &proxy_policy);
    if (!proxy_policy) {
      // This policy doesn't set a proxy for this network. Nonetheless, this
      // disallows changes by the user.
      return scoped_ptr<ProxyConfigDictionary>();
    }

    scoped_ptr<base::DictionaryValue> proxy_dict =
        onc::ConvertOncProxySettingsToProxyConfig(*proxy_policy);
    return make_scoped_ptr(new ProxyConfigDictionary(proxy_dict.get()));
  }

  if (network.profile_path().empty())
    return scoped_ptr<ProxyConfigDictionary>();

  const NetworkProfile* profile = NetworkHandler::Get()
      ->network_profile_handler()->GetProfileForPath(network.profile_path());
  if (!profile) {
    LOG(WARNING) << "Unknown profile_path '" << network.profile_path() << "'.";
    return scoped_ptr<ProxyConfigDictionary>();
  }
  if (!profile_prefs && profile->type() == NetworkProfile::TYPE_USER) {
    // This case occurs, for example, if called from the proxy config tracker
    // created for the system request context and the signin screen. Both don't
    // use profile prefs and shouldn't depend on the user's not shared proxy
    // settings.
    VLOG(1)
        << "Don't use unshared settings for system context or signin screen.";
    return scoped_ptr<ProxyConfigDictionary>();
  }

  // No policy set for this network, read instead the user's (shared or
  // unshared) configuration.
  // The user's proxy setting is not stored in the Chrome preference yet. We
  // still rely on Shill storing it.
  const base::DictionaryValue& value = network.proxy_config();
  if (value.empty())
    return scoped_ptr<ProxyConfigDictionary>();
  return make_scoped_ptr(new ProxyConfigDictionary(&value));
}

void SetProxyConfigForFavoriteNetwork(const ProxyConfigDictionary& proxy_config,
                                      const FavoriteState& network) {
  chromeos::ShillServiceClient* shill_service_client =
      DBusThreadManager::Get()->GetShillServiceClient();

  // The user's proxy setting is not stored in the Chrome preference yet. We
  // still rely on Shill storing it.
  ProxyPrefs::ProxyMode mode;
  if (!proxy_config.GetMode(&mode) || mode == ProxyPrefs::MODE_DIRECT) {
    // Return empty string for direct mode for portal check to work correctly.
    // TODO(pneubeck): Consider removing this legacy code.
    shill_service_client->ClearProperty(
        dbus::ObjectPath(network.path()),
        shill::kProxyConfigProperty,
        base::Bind(&NotifyNetworkStateHandler, network.path()),
        base::Bind(&network_handler::ShillErrorCallbackFunction,
                   "SetProxyConfig.ClearProperty Failed",
                   network.path(),
                   network_handler::ErrorCallback()));
  } else {
    std::string proxy_config_str;
    base::JSONWriter::Write(&proxy_config.GetDictionary(), &proxy_config_str);
    shill_service_client->SetProperty(
        dbus::ObjectPath(network.path()),
        shill::kProxyConfigProperty,
        base::StringValue(proxy_config_str),
        base::Bind(&NotifyNetworkStateHandler, network.path()),
        base::Bind(&network_handler::ShillErrorCallbackFunction,
                   "SetProxyConfig.SetProperty Failed",
                   network.path(),
                   network_handler::ErrorCallback()));
  }
}

void RegisterPrefs(PrefRegistrySimple* registry) {
  registry->RegisterListPref(prefs::kDeviceOpenNetworkConfiguration);
}

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterBooleanPref(
      prefs::kUseSharedProxies,
      false,
      user_prefs::PrefRegistrySyncable::UNSYNCABLE_PREF);

  registry->RegisterListPref(prefs::kOpenNetworkConfiguration,
                             user_prefs::PrefRegistrySyncable::UNSYNCABLE_PREF);
}

}  // namespace proxy_config

}  // namespace chromeos
