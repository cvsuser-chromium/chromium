// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/options/chromeos/internet_options_handler.h"

#include <ctype.h>

#include <map>
#include <string>
#include <vector>

#include "ash/shell.h"
#include "ash/shell_delegate.h"
#include "ash/system/chromeos/network/network_connect.h"
#include "ash/system/chromeos/network/network_icon.h"
#include "base/base64.h"
#include "base/basictypes.h"
#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/command_line.h"
#include "base/i18n/time_formatting.h"
#include "base/json/json_writer.h"
#include "base/strings/string16.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/chromeos/choose_mobile_network_dialog.h"
#include "chrome/browser/chromeos/enrollment_dialog_view.h"
#include "chrome/browser/chromeos/login/user.h"
#include "chrome/browser/chromeos/login/user_manager.h"
#include "chrome/browser/chromeos/mobile_config.h"
#include "chrome/browser/chromeos/net/onc_utils.h"
#include "chrome/browser/chromeos/options/network_config_view.h"
#include "chrome/browser/chromeos/options/network_property_ui_data.h"
#include "chrome/browser/chromeos/settings/cros_settings.h"
#include "chrome/browser/chromeos/sim_dialog_delegate.h"
#include "chrome/browser/chromeos/ui_proxy_config_service.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/host_desktop.h"
#include "chrome/browser/ui/scoped_tabbed_browser_displayer.h"
#include "chrome/browser/ui/singleton_tabs.h"
#include "chrome/browser/ui/webui/options/chromeos/core_chromeos_options_handler.h"
#include "chromeos/chromeos_switches.h"
#include "chromeos/network/device_state.h"
#include "chromeos/network/favorite_state.h"
#include "chromeos/network/managed_network_configuration_handler.h"
#include "chromeos/network/network_configuration_handler.h"
#include "chromeos/network/network_connection_handler.h"
#include "chromeos/network/network_device_handler.h"
#include "chromeos/network/network_event_log.h"
#include "chromeos/network/network_ip_config.h"
#include "chromeos/network/network_profile.h"
#include "chromeos/network/network_profile_handler.h"
#include "chromeos/network/network_state.h"
#include "chromeos/network/network_state_handler.h"
#include "chromeos/network/network_ui_data.h"
#include "chromeos/network/network_util.h"
#include "chromeos/network/shill_property_util.h"
#include "components/onc/onc_constants.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_view.h"
#include "content/public/browser/web_ui.h"
#include "grit/ash_resources.h"
#include "grit/ash_strings.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "grit/locale_settings.h"
#include "grit/theme_resources.h"
#include "third_party/cros_system_api/dbus/service_constants.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/layout.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/webui/web_ui_util.h"
#include "ui/gfx/display.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/screen.h"
#include "ui/views/widget/widget.h"

namespace chromeos {
namespace options {

namespace {

// Keys for the network description dictionary passed to the web ui. Make sure
// to keep the strings in sync with what the JavaScript side uses.
const char kNetworkInfoKeyConnectable[] = "connectable";
const char kNetworkInfoKeyConnected[] = "connected";
const char kNetworkInfoKeyConnecting[] = "connecting";
const char kNetworkInfoKeyIconURL[] = "iconURL";
const char kNetworkInfoKeyNetworkName[] = "networkName";
const char kNetworkInfoKeyNetworkType[] = "networkType";
const char kNetworkInfoKeyServicePath[] = "servicePath";
const char kNetworkInfoKeyPolicyManaged[] = "policyManaged";

// These are keys for getting IP information from the web ui.
const char kIpConfigAddress[] = "address";
const char kIpConfigPrefixLength[] = "prefixLength";
const char kIpConfigNetmask[] = "netmask";
const char kIpConfigGateway[] = "gateway";
const char kIpConfigNameServers[] = "nameServers";
const char kIpConfigAutoConfig[] = "ipAutoConfig";
const char kIpConfigWebProxyAutoDiscoveryUrl[] = "webProxyAutoDiscoveryUrl";

// These are types of name server selections from the web ui.
const char kNameServerTypeAutomatic[] = "automatic";
const char kNameServerTypeGoogle[] = "google";
const char kNameServerTypeUser[] = "user";

// These are dictionary names used to send data to the web ui.
const char kDictionaryIpConfig[] = "ipconfig";
const char kDictionaryStaticIp[] = "staticIP";
const char kDictionarySavedIp[] = "savedIP";

// Google public name servers (DNS).
const char kGoogleNameServers[] = "8.8.4.4,8.8.8.8";

// Functions we call in JavaScript.
const char kRefreshNetworkDataFunction[] =
    "options.network.NetworkList.refreshNetworkData";
const char kSetDefaultNetworkIconsFunction[] =
    "options.network.NetworkList.setDefaultNetworkIcons";
const char kShowDetailedInfoFunction[] =
    "options.internet.DetailsInternetPage.showDetailedInfo";
const char kUpdateConnectionDataFunction[] =
    "options.internet.DetailsInternetPage.updateConnectionData";
const char kUpdateCarrierFunction[] =
    "options.internet.DetailsInternetPage.updateCarrier";
const char kUpdateLoggedInUserTypeFunction[] =
    "options.network.NetworkList.updateLoggedInUserType";
const char kUpdateSecurityTabFunction[] =
    "options.internet.DetailsInternetPage.updateSecurityTab";

// These are used to register message handlers with JavaScript.
const char kBuyDataPlanMessage[] = "buyDataPlan";
const char kChangePinMessage[] = "changePin";
const char kDisableCellularMessage[] = "disableCellular";
const char kDisableWifiMessage[] = "disableWifi";
const char kDisableWimaxMessage[] = "disableWimax";
const char kEnableCellularMessage[] = "enableCellular";
const char kEnableWifiMessage[] = "enableWifi";
const char kEnableWimaxMessage[] = "enableWimax";
const char kNetworkCommandMessage[] = "networkCommand";
const char kRefreshNetworksMessage[] = "refreshNetworks";
const char kSetApnMessage[] = "setApn";
const char kSetAutoConnectMessage[] = "setAutoConnect";
const char kSetCarrierMessage[] = "setCarrier";
const char kSetIPConfigMessage[] = "setIPConfig";
const char kSetPreferNetworkMessage[] = "setPreferNetwork";
const char kSetServerHostname[] = "setServerHostname";
const char kSetSimCardLockMessage[] = "setSimCardLock";
const char kShowMorePlanInfoMessage[] = "showMorePlanInfo";

// These are strings used to communicate with JavaScript.
const char kTagActivate[] = "activate";
const char kTagActivationState[] = "activationState";
const char kTagAddConnection[] = "add";
const char kTagApn[] = "apn";
const char kTagAutoConnect[] = "autoConnect";
const char kTagBssid[] = "bssid";
const char kTagCarrierSelectFlag[] = "showCarrierSelect";
const char kTagCarrierUrl[] = "carrierUrl";
const char kTagCellular[] = "cellular";
const char kTagCellularAvailable[] = "cellularAvailable";
const char kTagCellularEnabled[] = "cellularEnabled";
const char kTagCellularSupportsScan[] = "cellularSupportsScan";
const char kTagConfigure[] = "configure";
const char kTagConnect[] = "connect";
const char kTagConnected[] = "connected";
const char kTagConnecting[] = "connecting";
const char kTagConnectionState[] = "connectionState";
const char kTagControlledBy[] = "controlledBy";
const char kTagDeviceConnected[] = "deviceConnected";
const char kTagDisableConnectButton[] = "disableConnectButton";
const char kTagDisconnect[] = "disconnect";
const char kTagEncryption[] = "encryption";
const char kTagErrorState[] = "errorState";
const char kTagEsn[] = "esn";
const char kTagFirmwareRevision[] = "firmwareRevision";
const char kTagForget[] = "forget";
const char kTagFrequency[] = "frequency";
const char kTagGsm[] = "gsm";
const char kTagHardwareAddress[] = "hardwareAddress";
const char kTagHardwareRevision[] = "hardwareRevision";
const char kTagIdentity[] = "identity";
const char kTagIccid[] = "iccid";
const char kTagImei[] = "imei";
const char kTagImsi[] = "imsi";
const char kTagLanguage[] = "language";
const char kTagLastGoodApn[] = "lastGoodApn";
const char kTagLocalizedName[] = "localizedName";
const char kTagManufacturer[] = "manufacturer";
const char kTagMdn[] = "mdn";
const char kTagMeid[] = "meid";
const char kTagMin[] = "min";
const char kTagModelId[] = "modelId";
const char kTagName[] = "name";
const char kTagNameServersGoogle[] = "nameServersGoogle";
const char kTagNameServerType[] = "nameServerType";
const char kTagNetworkId[] = "networkId";
const char kTagNetworkName[] = "networkName";
const char kTagNetworkTechnology[] = "networkTechnology";
const char kTagOperatorCode[] = "operatorCode";
const char kTagOperatorName[] = "operatorName";
const char kTagOptions[] = "options";
const char kTagPassword[] = "password";
const char kTagPolicy[] = "policy";
const char kTagPreferred[] = "preferred";
const char kTagPrlVersion[] = "prlVersion";
const char kTagProviderType[] = "providerType";
const char kTagProviderApnList[] = "providerApnList";
const char kTagRecommended[] = "recommended";
const char kTagRecommendedValue[] = "recommendedValue";
const char kTagRemembered[] = "remembered";
const char kTagRememberedList[] = "rememberedList";
const char kTagRestrictedPool[] = "restrictedPool";
const char kTagRoamingState[] = "roamingState";
const char kTagServerHostname[] = "serverHostname";
const char kTagCarriers[] = "carriers";
const char kTagCurrentCarrierIndex[] = "currentCarrierIndex";
const char kTagServiceName[] = "serviceName";
const char kTagServicePath[] = "servicePath";
const char kTagShared[] = "shared";
const char kTagShowActivateButton[] = "showActivateButton";
const char kTagShowPreferred[] = "showPreferred";
const char kTagShowProxy[] = "showProxy";
const char kTagShowStaticIPConfig[] = "showStaticIPConfig";
const char kTagShowViewAccountButton[] = "showViewAccountButton";
const char kTagSimCardLockEnabled[] = "simCardLockEnabled";
const char kTagSsid[] = "ssid";
const char kTagStrength[] = "strength";
const char kTagSupportUrl[] = "supportUrl";
const char kTagTrue[] = "true";
const char kTagType[] = "type";
const char kTagUsername[] = "username";
const char kTagValue[] = "value";
const char kTagVpn[] = "vpn";
const char kTagVpnList[] = "vpnList";
const char kTagWifi[] = "wifi";
const char kTagWifiAvailable[] = "wifiAvailable";
const char kTagWifiEnabled[] = "wifiEnabled";
const char kTagWimaxAvailable[] = "wimaxAvailable";
const char kTagWimaxEnabled[] = "wimaxEnabled";
const char kTagWiredList[] = "wiredList";
const char kTagWirelessList[] = "wirelessList";

const int kPreferredPriority = 1;

void ShillError(const std::string& function,
                const std::string& error_name,
                scoped_ptr<base::DictionaryValue> error_data) {
  // UpdateConnectionData may send requests for stale services; ignore
  // these errors.
  if (function == "UpdateConnectionData" &&
      error_name == network_handler::kDBusFailedError)
    return;
  NET_LOG_ERROR("Shill Error from InternetOptionsHandler: " + error_name,
                function);
}

const NetworkState* GetNetworkState(const std::string& service_path) {
  return NetworkHandler::Get()->network_state_handler()->
      GetNetworkState(service_path);
}

void SetNetworkProperty(const std::string& service_path,
                        const std::string& property,
                        base::Value* value) {
  NET_LOG_EVENT("SetNetworkProperty: " + property, service_path);
  base::DictionaryValue properties;
  properties.SetWithoutPathExpansion(property, value);
  NetworkHandler::Get()->network_configuration_handler()->SetProperties(
      service_path, properties,
      base::Bind(&base::DoNothing),
      base::Bind(&ShillError, "SetNetworkProperty"));
}

std::string ActivationStateString(const std::string& activation_state) {
  int id;
  if (activation_state == shill::kActivationStateActivated)
    id = IDS_CHROMEOS_NETWORK_ACTIVATION_STATE_ACTIVATED;
  else if (activation_state == shill::kActivationStateActivating)
    id = IDS_CHROMEOS_NETWORK_ACTIVATION_STATE_ACTIVATING;
  else if (activation_state == shill::kActivationStateNotActivated)
    id = IDS_CHROMEOS_NETWORK_ACTIVATION_STATE_NOT_ACTIVATED;
  else if (activation_state == shill::kActivationStatePartiallyActivated)
    id = IDS_CHROMEOS_NETWORK_ACTIVATION_STATE_PARTIALLY_ACTIVATED;
  else
    id = IDS_CHROMEOS_NETWORK_ACTIVATION_STATE_UNKNOWN;
  return l10n_util::GetStringUTF8(id);
}

std::string RoamingStateString(const std::string& roaming_state) {
  int id;
  if (roaming_state == shill::kRoamingStateHome)
    id = IDS_CHROMEOS_NETWORK_ROAMING_STATE_HOME;
  else if (roaming_state == shill::kRoamingStateRoaming)
    id = IDS_CHROMEOS_NETWORK_ROAMING_STATE_ROAMING;
  else
    id = IDS_CHROMEOS_NETWORK_ROAMING_STATE_UNKNOWN;
  return l10n_util::GetStringUTF8(id);
}

std::string ConnectionStateString(const std::string& state) {
  int id;
  if (state == shill::kUnknownString)
    id = IDS_CHROMEOS_NETWORK_STATE_UNKNOWN;
  else if (state == shill::kStateIdle)
    id = IDS_CHROMEOS_NETWORK_STATE_IDLE;
  else if (state == shill::kStateCarrier)
    id = IDS_CHROMEOS_NETWORK_STATE_CARRIER;
  else if (state == shill::kStateAssociation)
    id = IDS_CHROMEOS_NETWORK_STATE_ASSOCIATION;
  else if (state == shill::kStateConfiguration)
    id = IDS_CHROMEOS_NETWORK_STATE_CONFIGURATION;
  else if (state == shill::kStateReady)
    id = IDS_CHROMEOS_NETWORK_STATE_READY;
  else if (state == shill::kStateDisconnect)
    id = IDS_CHROMEOS_NETWORK_STATE_DISCONNECT;
  else if (state == shill::kStateFailure)
    id = IDS_CHROMEOS_NETWORK_STATE_FAILURE;
  else if (state == shill::kStateActivationFailure)
    id = IDS_CHROMEOS_NETWORK_STATE_ACTIVATION_FAILURE;
  else if (state == shill::kStatePortal)
    id = IDS_CHROMEOS_NETWORK_STATE_PORTAL;
  else if (state == shill::kStateOnline)
    id = IDS_CHROMEOS_NETWORK_STATE_ONLINE;
  else
    id = IDS_CHROMEOS_NETWORK_STATE_UNRECOGNIZED;
  return l10n_util::GetStringUTF8(id);
}

std::string LoggedInUserTypeToString(
    LoginState::LoggedInUserType type) {
  switch (type) {
    case LoginState::LOGGED_IN_USER_NONE:
      return "none";
    case LoginState::LOGGED_IN_USER_REGULAR:
      return "regular";
    case LoginState::LOGGED_IN_USER_OWNER:
      return "owner";
    case LoginState::LOGGED_IN_USER_GUEST:
      return "guest";
    case LoginState::LOGGED_IN_USER_RETAIL_MODE:
      return "retail-mode";
    case LoginState::LOGGED_IN_USER_PUBLIC_ACCOUNT:
      return "public-account";
    case LoginState::LOGGED_IN_USER_LOCALLY_MANAGED:
      return "locally-managed";
    case LoginState::LOGGED_IN_USER_KIOSK_APP:
      return "kiosk-app";
    default:
      return "";
  }
}

std::string EncryptionString(const std::string& security,
                             const std::string& eap_method) {
  if (security == shill::kSecurityNone)
    return "";
  if (security == shill::kSecurityWpa)
    return "WPA";
  if (security == shill::kSecurityWep)
    return "WEP";
  if (security == shill::kSecurityRsn)
    return "RSN";
  if (security == shill::kSecurityPsk)
    return "PSK";
  if (security == shill::kSecurity8021x) {
    std::string result = "8021X";
    if (eap_method == shill::kEapMethodPEAP)
      result += "PEAP";
    else if (eap_method == shill::kEapMethodTLS)
      result += "TLS";
    else if (eap_method == shill::kEapMethodTTLS)
      result += "TTLS";
    else if (eap_method == shill::kEapMethodLEAP)
      result += "LEAP";
    return result;
  }
  return "Unknown";
}

std::string ProviderTypeString(
    const std::string& provider_type,
    const base::DictionaryValue& provider_properties) {
  int id;
  if (provider_type == shill::kProviderL2tpIpsec) {
    std::string client_cert_id;
    provider_properties.GetStringWithoutPathExpansion(
        shill::kL2tpIpsecClientCertIdProperty, &client_cert_id);
    if (client_cert_id.empty())
      id = IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_L2TP_IPSEC_PSK;
    else
      id = IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_L2TP_IPSEC_USER_CERT;
  } else if (provider_type == shill::kProviderOpenVpn) {
    id = IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_OPEN_VPN;
  } else {
    id = IDS_CHROMEOS_NETWORK_ERROR_UNKNOWN;
  }
  return l10n_util::GetStringUTF8(id);
}

bool HasPolicyForFavorite(const FavoriteState* favorite,
                          const PrefService* profile_prefs) {
  return onc::HasPolicyForFavoriteNetwork(
      profile_prefs, g_browser_process->local_state(), *favorite);
}

bool HasPolicyForNetwork(const NetworkState* network,
                         const PrefService* profile_prefs) {
  const FavoriteState* favorite =
      NetworkHandler::Get()->network_state_handler()->GetFavoriteState(
          network->path());
  if (!favorite)
    return false;
  return HasPolicyForFavorite(favorite, profile_prefs);
}

void SetCommonNetworkInfo(const ManagedState* state,
                          const gfx::ImageSkia& icon,
                          ui::ScaleFactor icon_scale_factor,
                          base::DictionaryValue* network_info) {
  gfx::ImageSkiaRep image_rep =
      icon.GetRepresentation(ui::GetImageScale(icon_scale_factor));
  std::string icon_url =
      icon.isNull() ? "" : webui::GetBitmapDataUrl(image_rep.sk_bitmap());
  network_info->SetString(kNetworkInfoKeyIconURL, icon_url);

  std::string name = state->name();
  if (state->Matches(NetworkTypePattern::Ethernet()))
    name = l10n_util::GetStringUTF8(IDS_STATUSBAR_NETWORK_DEVICE_ETHERNET);
  network_info->SetString(kNetworkInfoKeyNetworkName, name);
  network_info->SetString(kNetworkInfoKeyNetworkType, state->type());
  network_info->SetString(kNetworkInfoKeyServicePath, state->path());
}

// Builds a dictionary with network information and an icon used for the
// NetworkList on the settings page. Ownership of the returned pointer is
// transferred to the caller.
base::DictionaryValue* BuildNetworkDictionary(
    const NetworkState* network,
    ui::ScaleFactor icon_scale_factor,
    const PrefService* profile_prefs) {
  scoped_ptr<base::DictionaryValue> network_info(new base::DictionaryValue());
  network_info->SetBoolean(kNetworkInfoKeyConnectable, network->connectable());
  network_info->SetBoolean(kNetworkInfoKeyConnected,
                           network->IsConnectedState());
  network_info->SetBoolean(kNetworkInfoKeyConnecting,
                           network->IsConnectingState());
  network_info->SetBoolean(kNetworkInfoKeyPolicyManaged,
                           HasPolicyForNetwork(network, profile_prefs));

  gfx::ImageSkia icon = ash::network_icon::GetImageForNetwork(
      network, ash::network_icon::ICON_TYPE_LIST);
  SetCommonNetworkInfo(network, icon, icon_scale_factor, network_info.get());
  return network_info.release();
}

base::DictionaryValue* BuildFavoriteDictionary(
    const FavoriteState* favorite,
    ui::ScaleFactor icon_scale_factor,
    const PrefService* profile_prefs) {
  scoped_ptr<base::DictionaryValue> network_info(new base::DictionaryValue());
  network_info->SetBoolean(kNetworkInfoKeyConnectable, false);
  network_info->SetBoolean(kNetworkInfoKeyConnected, false);
  network_info->SetBoolean(kNetworkInfoKeyConnecting, false);
  network_info->SetBoolean(kNetworkInfoKeyPolicyManaged,
                           HasPolicyForFavorite(favorite, profile_prefs));

  gfx::ImageSkia icon = ash::network_icon::GetImageForDisconnectedNetwork(
      ash::network_icon::ICON_TYPE_LIST, favorite->type());
  SetCommonNetworkInfo(favorite, icon, icon_scale_factor, network_info.get());
  return network_info.release();
}

// Pulls IP information out of a shill service properties dictionary. If
// |static_ip| is true, then it fetches "StaticIP.*" properties. If not, then it
// fetches "SavedIP.*" properties. Caller must take ownership of returned
// dictionary.  If non-NULL, |ip_parameters_set| returns a count of the number
// of IP routing parameters that get set.
base::DictionaryValue* BuildIPInfoDictionary(
    const base::DictionaryValue& shill_properties,
    bool static_ip,
    int* routing_parameters_set) {
  std::string address_key;
  std::string prefix_len_key;
  std::string gateway_key;
  std::string name_servers_key;
  if (static_ip) {
    address_key = shill::kStaticIPAddressProperty;
    prefix_len_key = shill::kStaticIPPrefixlenProperty;
    gateway_key = shill::kStaticIPGatewayProperty;
    name_servers_key = shill::kStaticIPNameServersProperty;
  } else {
    address_key = shill::kSavedIPAddressProperty;
    prefix_len_key = shill::kSavedIPPrefixlenProperty;
    gateway_key = shill::kSavedIPGatewayProperty;
    name_servers_key = shill::kSavedIPNameServersProperty;
  }

  scoped_ptr<base::DictionaryValue> ip_info_dict(new base::DictionaryValue);
  std::string address;
  int routing_parameters = 0;
  if (shill_properties.GetStringWithoutPathExpansion(address_key, &address)) {
    ip_info_dict->SetString(kIpConfigAddress, address);
    VLOG(2) << "Found " << address_key << ": " << address;
    routing_parameters++;
  }
  int prefix_len = -1;
  if (shill_properties.GetIntegerWithoutPathExpansion(
      prefix_len_key, &prefix_len)) {
    ip_info_dict->SetInteger(kIpConfigPrefixLength, prefix_len);
    std::string netmask = network_util::PrefixLengthToNetmask(prefix_len);
    ip_info_dict->SetString(kIpConfigNetmask, netmask);
    VLOG(2) << "Found " << prefix_len_key << ": "
            <<  prefix_len << " (" << netmask << ")";
    routing_parameters++;
  }
  std::string gateway;
  if (shill_properties.GetStringWithoutPathExpansion(gateway_key, &gateway)) {
    ip_info_dict->SetString(kIpConfigGateway, gateway);
    VLOG(2) << "Found " << gateway_key << ": " << gateway;
    routing_parameters++;
  }
  if (routing_parameters_set)
    *routing_parameters_set = routing_parameters;

  std::string name_servers;
  if (shill_properties.GetStringWithoutPathExpansion(
      name_servers_key, &name_servers)) {
    ip_info_dict->SetString(kIpConfigNameServers, name_servers);
    VLOG(2) << "Found " << name_servers_key << ": " << name_servers;
  }

  return ip_info_dict.release();
}

bool CanForgetNetworkType(const std::string& type) {
  return type == shill::kTypeWifi ||
         type == shill::kTypeWimax ||
         type == shill::kTypeVPN;
}

bool CanAddNetworkType(const std::string& type) {
  return type == shill::kTypeWifi ||
         type == shill::kTypeVPN ||
         type == shill::kTypeCellular;
}

// Decorate dictionary |value_dict| with policy information from |ui_data|.
void DecorateValueDictionary(const NetworkPropertyUIData& ui_data,
                             const base::Value& value,
                             base::DictionaryValue* value_dict) {
  const base::Value* recommended_value = ui_data.default_value();
  if (ui_data.IsManaged())
    value_dict->SetString(kTagControlledBy, kTagPolicy);
  else if (recommended_value && recommended_value->Equals(&value))
    value_dict->SetString(kTagControlledBy, kTagRecommended);

  if (recommended_value)
    value_dict->Set(kTagRecommendedValue, recommended_value->DeepCopy());
}

// Decorate pref value as CoreOptionsHandler::CreateValueForPref() does and
// store it under |key| in |settings|. Takes ownership of |value|.
void SetValueDictionary(base::DictionaryValue* settings,
                        const char* key,
                        base::Value* value,
                        const NetworkPropertyUIData& ui_data) {
  base::DictionaryValue* dict = new base::DictionaryValue();
  // DictionaryValue::Set() takes ownership of |value|.
  dict->Set(kTagValue, value);
  settings->Set(key, dict);
  DecorateValueDictionary(ui_data, *value, dict);
}

// Creates a decorated dictionary like SetValueDictionary does, but extended for
// the Autoconnect property, which respects additionally global network policy.
void SetAutoconnectValueDictionary(bool network_is_private,
                                   ::onc::ONCSource onc_source,
                                   bool current_autoconnect,
                                   const NetworkPropertyUIData& ui_data,
                                   base::DictionaryValue* settings) {
  base::DictionaryValue* dict = new base::DictionaryValue();
  base::Value* value = new base::FundamentalValue(current_autoconnect);
  // DictionaryValue::Set() takes ownership of |value|.
  dict->Set(kTagValue, value);
  settings->Set(kTagAutoConnect, dict);
  if (onc_source != ::onc::ONC_SOURCE_USER_POLICY &&
      onc_source != ::onc::ONC_SOURCE_DEVICE_POLICY) {
    // Autoconnect can be controlled by the GlobalNetworkConfiguration of the
    // ONC policy.
    bool only_policy_autoconnect =
        onc::PolicyAllowsOnlyPolicyNetworksToAutoconnect(network_is_private);
    if (only_policy_autoconnect) {
      dict->SetString(kTagControlledBy, kTagPolicy);
      return;
    }
  }
  DecorateValueDictionary(ui_data, *value, dict);
}

std::string CopyStringFromDictionary(const base::DictionaryValue& source,
                                     const std::string& src_key,
                                     const std::string& dest_key,
                                     base::DictionaryValue* dest) {
  std::string string_value;
  if (source.GetStringWithoutPathExpansion(src_key, &string_value))
    dest->SetStringWithoutPathExpansion(dest_key, string_value);
  return string_value;
}

void CopyIntegerFromDictionary(const base::DictionaryValue& source,
                               const std::string& src_key,
                               const std::string& dest_key,
                               bool as_string,
                               base::DictionaryValue* dest) {
  int int_value;
  if (!source.GetIntegerWithoutPathExpansion(src_key, &int_value))
    return;
  if (as_string) {
    std::string str = base::StringPrintf("%d", int_value);
    dest->SetStringWithoutPathExpansion(dest_key, str);
  } else {
    dest->SetIntegerWithoutPathExpansion(dest_key, int_value);
  }
}

// Fills |dictionary| with the configuration details of |vpn|. |onc| is required
// for augmenting the policy-managed information.
void PopulateVPNDetails(const NetworkState* vpn,
                        const base::DictionaryValue& shill_properties,
                        base::DictionaryValue* dictionary) {
  // Name and Remembered are set in PopulateConnectionDetails().
  // Provider properties are stored in the "Provider" dictionary.
  const base::DictionaryValue* provider_properties = NULL;
  if (!shill_properties.GetDictionaryWithoutPathExpansion(
          shill::kProviderProperty, &provider_properties)) {
    LOG(ERROR) << "No provider properties for VPN: " << vpn->path();
    return;
  }
  std::string provider_type;
  provider_properties->GetStringWithoutPathExpansion(
      shill::kTypeProperty, &provider_type);
  dictionary->SetString(kTagProviderType,
                        ProviderTypeString(provider_type,
                                           *provider_properties));

  std::string username;
  if (provider_type == shill::kProviderOpenVpn) {
    provider_properties->GetStringWithoutPathExpansion(
        shill::kOpenVPNUserProperty, &username);
  } else {
    provider_properties->GetStringWithoutPathExpansion(
        shill::kL2tpIpsecUserProperty, &username);
  }
  dictionary->SetString(kTagUsername, username);

  ::onc::ONCSource onc_source = ::onc::ONC_SOURCE_NONE;
  const base::DictionaryValue* onc =
      onc::FindPolicyForActiveUser(vpn->guid(), &onc_source);

  NetworkPropertyUIData hostname_ui_data;
  hostname_ui_data.ParseOncProperty(
      onc_source,
      onc,
      base::StringPrintf("%s.%s", ::onc::network_config::kVPN,
                         ::onc::vpn::kHost));
  std::string provider_host;
  provider_properties->GetStringWithoutPathExpansion(
      shill::kHostProperty, &provider_host);
  SetValueDictionary(dictionary, kTagServerHostname,
                     new base::StringValue(provider_host),
                     hostname_ui_data);

  // Disable 'Connect' for VPN unless connected to a non-VPN network.
  const NetworkState* connected_network =
      NetworkHandler::Get()->network_state_handler()->ConnectedNetworkByType(
          NetworkTypePattern::NonVirtual());
  dictionary->SetBoolean(kTagDisableConnectButton, !connected_network);
}

// Given a list of supported carrier's by the device, return the index of
// the carrier the device is currently using.
int FindCurrentCarrierIndex(const base::ListValue* carriers,
                            const DeviceState* device) {
  DCHECK(carriers);
  DCHECK(device);
  bool gsm = (device->technology_family() == shill::kTechnologyFamilyGsm);
  int index = 0;
  for (base::ListValue::const_iterator it = carriers->begin();
       it != carriers->end(); ++it, ++index) {
    std::string value;
    if (!(*it)->GetAsString(&value))
      continue;
    // For GSM devices the device name will be empty, so simply select
    // the Generic UMTS carrier option if present.
    if (gsm && (value == shill::kCarrierGenericUMTS))
      return index;
    // For other carriers, the service name will match the carrier name.
    if (value == device->carrier())
      return index;
  }
  return -1;
}

void PopulateWifiDetails(const NetworkState* wifi,
                         const base::DictionaryValue& shill_properties,
                         base::DictionaryValue* dictionary);
// TODO(stevenjb): Move implementation here.

void PopulateWimaxDetails(const NetworkState* wimax,
                          const base::DictionaryValue& shill_properties,
                          base::DictionaryValue* dictionary);
// TODO(stevenjb): Move implementation here.

void CreateDictionaryFromCellularApn(const base::DictionaryValue* apn,
                                     base::DictionaryValue* dictionary);
// TODO(stevenjb): Move implementation here.

void PopulateCellularDetails(const NetworkState* cellular,
                             const base::DictionaryValue& shill_properties,
                             base::DictionaryValue* dictionary);
// TODO(stevenjb): Move implementation here.

void PopulateConnectionDetails(const NetworkState* network,
                               const base::DictionaryValue& shill_properties,
                               base::DictionaryValue* dictionary);
// TODO(stevenjb): Move implementation here.

// Helper methods for SetIPConfigProperties
bool AppendPropertyKeyIfPresent(const std::string& key,
                                const base::DictionaryValue& old_properties,
                                std::vector<std::string>* property_keys) {
  if (old_properties.HasKey(key)) {
    property_keys->push_back(key);
    return true;
  }
  return false;
}

bool AddStringPropertyIfChanged(const std::string& key,
                                const std::string& new_value,
                                const base::DictionaryValue& old_properties,
                                base::DictionaryValue* new_properties) {
  std::string old_value;
  if (!old_properties.GetStringWithoutPathExpansion(key, &old_value) ||
      new_value != old_value) {
    new_properties->SetStringWithoutPathExpansion(key, new_value);
    return true;
  }
  return false;
}

bool AddIntegerPropertyIfChanged(const std::string& key,
                                 int new_value,
                                 const base::DictionaryValue& old_properties,
                                 base::DictionaryValue* new_properties) {
  int old_value;
  if (!old_properties.GetIntegerWithoutPathExpansion(key, &old_value) ||
      new_value != old_value) {
    new_properties->SetIntegerWithoutPathExpansion(key, new_value);
    return true;
  }
  return false;
}

void RequestReconnect(const std::string& service_path,
                      gfx::NativeWindow owning_window) {
  NetworkHandler::Get()->network_connection_handler()->DisconnectNetwork(
      service_path,
      base::Bind(&ash::network_connect::ConnectToNetwork,
                 service_path, owning_window),
      base::Bind(&ShillError, "RequestReconnect"));
}

}  // namespace

InternetOptionsHandler::InternetOptionsHandler()
    : weak_factory_(this) {
  registrar_.Add(this, chrome::NOTIFICATION_REQUIRE_PIN_SETTING_CHANGE_ENDED,
                 content::NotificationService::AllSources());
  registrar_.Add(this, chrome::NOTIFICATION_ENTER_PIN_ENDED,
                 content::NotificationService::AllSources());
  NetworkHandler::Get()->network_state_handler()->AddObserver(this, FROM_HERE);
  LoginState::Get()->AddObserver(this);
}

InternetOptionsHandler::~InternetOptionsHandler() {
  if (NetworkHandler::IsInitialized()) {
    NetworkHandler::Get()->network_state_handler()->RemoveObserver(
        this, FROM_HERE);
  }
  if (LoginState::Get()->IsInitialized())
    LoginState::Get()->RemoveObserver(this);
}

void InternetOptionsHandler::GetLocalizedValues(
    base::DictionaryValue* localized_strings) {
  DCHECK(localized_strings);

  static OptionsStringResource resources[] = {

    // Main settings page.

    { "ethernetTitle", IDS_STATUSBAR_NETWORK_DEVICE_ETHERNET },
    { "wifiTitle", IDS_OPTIONS_SETTINGS_SECTION_TITLE_WIFI_NETWORK },
    { "wimaxTitle", IDS_OPTIONS_SETTINGS_SECTION_TITLE_WIMAX_NETWORK },
    { "cellularTitle", IDS_OPTIONS_SETTINGS_SECTION_TITLE_CELLULAR_NETWORK },
    { "vpnTitle", IDS_OPTIONS_SETTINGS_SECTION_TITLE_PRIVATE_NETWORK },
    { "networkNotConnected", IDS_OPTIONS_SETTINGS_NETWORK_NOT_CONNECTED },
    { "networkConnected", IDS_CHROMEOS_NETWORK_STATE_READY },
    { "joinOtherNetwork", IDS_OPTIONS_SETTINGS_NETWORK_OTHER },
    { "networkOffline", IDS_OPTIONS_SETTINGS_NETWORK_OFFLINE },
    { "networkDisabled", IDS_OPTIONS_SETTINGS_NETWORK_DISABLED },
    { "networkOnline", IDS_OPTIONS_SETTINGS_NETWORK_ONLINE },
    { "networkOptions", IDS_OPTIONS_SETTINGS_NETWORK_OPTIONS },
    { "turnOffWifi", IDS_OPTIONS_SETTINGS_NETWORK_DISABLE_WIFI },
    { "turnOffWimax", IDS_OPTIONS_SETTINGS_NETWORK_DISABLE_WIMAX },
    { "turnOffCellular", IDS_OPTIONS_SETTINGS_NETWORK_DISABLE_CELLULAR },
    { "disconnectNetwork", IDS_OPTIONS_SETTINGS_DISCONNECT },
    { "preferredNetworks", IDS_OPTIONS_SETTINGS_PREFERRED_NETWORKS_LABEL },
    { "preferredNetworksPage", IDS_OPTIONS_SETTINGS_PREFERRED_NETWORKS_TITLE },
    { "useSharedProxies", IDS_OPTIONS_SETTINGS_USE_SHARED_PROXIES },
    { "addConnectionTitle",
      IDS_OPTIONS_SETTINGS_SECTION_TITLE_ADD_CONNECTION },
    { "addConnectionWifi", IDS_OPTIONS_SETTINGS_ADD_CONNECTION_WIFI },
    { "addConnectionVPN", IDS_STATUSBAR_NETWORK_ADD_VPN },
    { "otherCellularNetworks", IDS_OPTIONS_SETTINGS_OTHER_CELLULAR_NETWORKS },
    { "enableDataRoaming", IDS_OPTIONS_SETTINGS_ENABLE_DATA_ROAMING },
    { "disableDataRoaming", IDS_OPTIONS_SETTINGS_DISABLE_DATA_ROAMING },
    { "dataRoamingDisableToggleTooltip",
      IDS_OPTIONS_SETTINGS_TOGGLE_DATA_ROAMING_RESTRICTION },
    { "activateNetwork", IDS_STATUSBAR_NETWORK_DEVICE_ACTIVATE },

    // Internet details dialog.

    { "changeProxyButton",
      IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_CHANGE_PROXY_BUTTON },
    { "managedNetwork", IDS_OPTIONS_SETTINGS_MANAGED_NETWORK },
    { "wifiNetworkTabLabel", IDS_OPTIONS_SETTINGS_INTERNET_TAB_CONNECTION },
    { "vpnTabLabel", IDS_OPTIONS_SETTINGS_INTERNET_TAB_VPN },
    { "cellularConnTabLabel", IDS_OPTIONS_SETTINGS_INTERNET_TAB_CONNECTION },
    { "cellularDeviceTabLabel", IDS_OPTIONS_SETTINGS_INTERNET_TAB_DEVICE },
    { "networkTabLabel", IDS_OPTIONS_SETTINGS_INTERNET_TAB_NETWORK },
    { "securityTabLabel", IDS_OPTIONS_SETTINGS_INTERNET_TAB_SECURITY },
    { "proxyTabLabel", IDS_OPTIONS_SETTINGS_INTERNET_TAB_PROXY },
    { "connectionState", IDS_OPTIONS_SETTINGS_INTERNET_CONNECTION_STATE },
    { "inetAddress", IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_ADDRESS },
    { "inetNetmask", IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_SUBNETMASK },
    { "inetGateway", IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_GATEWAY },
    { "inetNameServers", IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_DNSSERVER },
    { "ipAutomaticConfiguration",
        IDS_OPTIONS_SETTINGS_INTERNET_IP_AUTOMATIC_CONFIGURATION },
    { "automaticNameServers",
        IDS_OPTIONS_SETTINGS_INTERNET_AUTOMATIC_NAME_SERVERS },
    { "userNameServer1", IDS_OPTIONS_SETTINGS_INTERNET_USER_NAME_SERVER_1 },
    { "userNameServer2", IDS_OPTIONS_SETTINGS_INTERNET_USER_NAME_SERVER_2 },
    { "userNameServer3", IDS_OPTIONS_SETTINGS_INTERNET_USER_NAME_SERVER_3 },
    { "userNameServer4", IDS_OPTIONS_SETTINGS_INTERNET_USER_NAME_SERVER_4 },
    { "googleNameServers", IDS_OPTIONS_SETTINGS_INTERNET_GOOGLE_NAME_SERVERS },
    { "userNameServers", IDS_OPTIONS_SETTINGS_INTERNET_USER_NAME_SERVERS },
    { "hardwareAddress",
      IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_HARDWARE_ADDRESS },
    { "detailsInternetDismiss", IDS_CLOSE },
    { "activateButton", IDS_OPTIONS_SETTINGS_ACTIVATE },
    { "buyplanButton", IDS_OPTIONS_SETTINGS_BUY_PLAN },
    { "connectButton", IDS_OPTIONS_SETTINGS_CONNECT },
    { "configureButton", IDS_OPTIONS_SETTINGS_CONFIGURE },
    { "disconnectButton", IDS_OPTIONS_SETTINGS_DISCONNECT },
    { "viewAccountButton", IDS_STATUSBAR_NETWORK_VIEW_ACCOUNT },
    { "wimaxConnTabLabel", IDS_OPTIONS_SETTINGS_INTERNET_TAB_WIMAX },

    // Wifi Tab.

    { "inetSsid", IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_NETWORK_ID },
    { "inetBssid", IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_NETWORK_BSSID },
    { "inetEncryption",
      IDS_OPTIONS_SETTIGNS_INTERNET_OPTIONS_NETWORK_ENCRYPTION },
    { "inetFrequency",
      IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_NETWORK_FREQUENCY },
    { "inetFrequencyFormat",
      IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_NETWORK_FREQUENCY_MHZ },
    { "inetSignalStrength",
      IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_NETWORK_STRENGTH },
    { "inetSignalStrengthFormat",
      IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_NETWORK_STRENGTH_PERCENTAGE },
    { "inetPassProtected",
      IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_NET_PROTECTED },
    { "inetNetworkShared",
      IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_NETWORK_SHARED },
    { "inetPreferredNetwork",
      IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_PREFER_NETWORK },
    { "inetAutoConnectNetwork",
      IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_AUTO_CONNECT },
    { "inetLogin", IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_LOGIN },
    { "inetShowPass", IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_SHOWPASSWORD },
    { "inetPassPrompt", IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_PASSWORD },
    { "inetSsidPrompt", IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_SSID },
    { "inetStatus", IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_STATUS_TITLE },
    { "inetConnect", IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_CONNECT_TITLE },

    // VPN Tab.

    { "inetServiceName",
      IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_VPN_SERVICE_NAME },
    { "inetServerHostname",
      IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_VPN_SERVER_HOSTNAME },
    { "inetProviderType",
      IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_VPN_PROVIDER_TYPE },
    { "inetUsername", IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_VPN_USERNAME },

    // Cellular Tab.

    { "serviceName", IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_SERVICE_NAME },
    { "networkTechnology",
      IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_NETWORK_TECHNOLOGY },
    { "operatorName", IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_OPERATOR },
    { "operatorCode", IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_OPERATOR_CODE },
    { "activationState",
      IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_ACTIVATION_STATE },
    { "roamingState", IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_ROAMING_STATE },
    { "restrictedPool",
      IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_RESTRICTED_POOL },
    { "errorState", IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_ERROR_STATE },
    { "manufacturer", IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_MANUFACTURER },
    { "modelId", IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_MODEL_ID },
    { "firmwareRevision",
      IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_FIRMWARE_REVISION },
    { "hardwareRevision",
      IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_HARDWARE_REVISION },
    { "prlVersion", IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_PRL_VERSION },
    { "cellularApnLabel", IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_APN },
    { "cellularApnOther", IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_APN_OTHER },
    { "cellularApnUsername",
      IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_APN_USERNAME },
    { "cellularApnPassword",
      IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_APN_PASSWORD },
    { "cellularApnUseDefault",
      IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_APN_CLEAR },
    { "cellularApnSet", IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_APN_SET },
    { "cellularApnCancel", IDS_CANCEL },

    // Security Tab.

    { "accessSecurityTabLink",
      IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_ACCESS_SECURITY_TAB },
    { "lockSimCard", IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_LOCK_SIM_CARD },
    { "changePinButton",
      IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_CHANGE_PIN_BUTTON },

    // Proxy Tab.
    { "webProxyAutoDiscoveryUrl", IDS_PROXY_WEB_PROXY_AUTO_DISCOVERY },
  };

  RegisterStrings(localized_strings, resources, arraysize(resources));

  std::string owner;
  chromeos::CrosSettings::Get()->GetString(chromeos::kDeviceOwner, &owner);
  localized_strings->SetString("ownerUserId", UTF8ToUTF16(owner));

  base::DictionaryValue* network_dictionary = new base::DictionaryValue;
  FillNetworkInfo(network_dictionary);
  localized_strings->Set("networkData", network_dictionary);
}

void InternetOptionsHandler::InitializePage() {
  base::DictionaryValue dictionary;
  dictionary.SetString(kTagCellular,
      GetIconDataUrl(IDR_AURA_UBER_TRAY_NETWORK_BARS_DARK));
  dictionary.SetString(kTagWifi,
      GetIconDataUrl(IDR_AURA_UBER_TRAY_NETWORK_ARCS_DARK));
  dictionary.SetString(kTagVpn,
      GetIconDataUrl(IDR_AURA_UBER_TRAY_NETWORK_VPN));
  web_ui()->CallJavascriptFunction(kSetDefaultNetworkIconsFunction,
                                   dictionary);
  NetworkHandler::Get()->network_state_handler()->RequestScan();
  RefreshNetworkData();
  UpdateLoggedInUserType();
}

void InternetOptionsHandler::RegisterMessages() {
  // Setup handlers specific to this panel.
  web_ui()->RegisterMessageCallback(kNetworkCommandMessage,
      base::Bind(&InternetOptionsHandler::NetworkCommandCallback,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(kRefreshNetworksMessage,
      base::Bind(&InternetOptionsHandler::RefreshNetworksCallback,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(kSetPreferNetworkMessage,
      base::Bind(&InternetOptionsHandler::SetPreferNetworkCallback,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(kSetAutoConnectMessage,
      base::Bind(&InternetOptionsHandler::SetAutoConnectCallback,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(kSetIPConfigMessage,
      base::Bind(&InternetOptionsHandler::SetIPConfigCallback,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(kEnableWifiMessage,
      base::Bind(&InternetOptionsHandler::EnableWifiCallback,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(kDisableWifiMessage,
      base::Bind(&InternetOptionsHandler::DisableWifiCallback,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(kEnableCellularMessage,
      base::Bind(&InternetOptionsHandler::EnableCellularCallback,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(kDisableCellularMessage,
      base::Bind(&InternetOptionsHandler::DisableCellularCallback,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(kEnableWimaxMessage,
      base::Bind(&InternetOptionsHandler::EnableWimaxCallback,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(kDisableWimaxMessage,
      base::Bind(&InternetOptionsHandler::DisableWimaxCallback,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(kBuyDataPlanMessage,
      base::Bind(&InternetOptionsHandler::BuyDataPlanCallback,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(kShowMorePlanInfoMessage,
      base::Bind(&InternetOptionsHandler::ShowMorePlanInfoCallback,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(kSetApnMessage,
      base::Bind(&InternetOptionsHandler::SetApnCallback,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(kSetCarrierMessage,
      base::Bind(&InternetOptionsHandler::SetCarrierCallback,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(kSetSimCardLockMessage,
      base::Bind(&InternetOptionsHandler::SetSimCardLockCallback,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(kChangePinMessage,
      base::Bind(&InternetOptionsHandler::ChangePinCallback,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(kSetServerHostname,
      base::Bind(&InternetOptionsHandler::SetServerHostnameCallback,
                 base::Unretained(this)));
}

void InternetOptionsHandler::EnableWifiCallback(const base::ListValue* args) {
  NetworkHandler::Get()->network_state_handler()->SetTechnologyEnabled(
      NetworkTypePattern::WiFi(), true,
      base::Bind(&ShillError, "EnableWifiCallback"));
}

void InternetOptionsHandler::DisableWifiCallback(const base::ListValue* args) {
  NetworkHandler::Get()->network_state_handler()->SetTechnologyEnabled(
      NetworkTypePattern::WiFi(), false,
      base::Bind(&ShillError, "DisableWifiCallback"));
}

void InternetOptionsHandler::EnableCellularCallback(
    const base::ListValue* args) {
  NetworkStateHandler* handler = NetworkHandler::Get()->network_state_handler();
  const DeviceState* device =
      handler->GetDeviceStateByType(NetworkTypePattern::Cellular());
  if (!device) {
    LOG(ERROR) << "Mobile device not found.";
    return;
  }
  if (!device->sim_lock_type().empty()) {
    SimDialogDelegate::ShowDialog(GetNativeWindow(),
                                  SimDialogDelegate::SIM_DIALOG_UNLOCK);
    return;
  }
  if (!handler->IsTechnologyEnabled(NetworkTypePattern::Cellular())) {
    handler->SetTechnologyEnabled(
        NetworkTypePattern::Cellular(), true,
        base::Bind(&ShillError, "EnableCellularCallback"));
    return;
  }
  if (device->IsSimAbsent()) {
    MobileConfig* config = MobileConfig::GetInstance();
    if (config->IsReady()) {
      const MobileConfig::LocaleConfig* locale_config =
          config->GetLocaleConfig();
      if (locale_config) {
        std::string setup_url = locale_config->setup_url();
        if (!setup_url.empty()) {
          chrome::ScopedTabbedBrowserDisplayer displayer(
               ProfileManager::GetDefaultProfileOrOffTheRecord(),
               chrome::HOST_DESKTOP_TYPE_ASH);
          chrome::ShowSingletonTab(displayer.browser(), GURL(setup_url));
          return;
        }
      }
    }
    // TODO(nkostylev): Show generic error message. http://crosbug.com/15444
  }
  LOG(ERROR) << "EnableCellularCallback called for enabled mobile device";
}

void InternetOptionsHandler::DisableCellularCallback(
    const base::ListValue* args) {
  NetworkHandler::Get()->network_state_handler()->SetTechnologyEnabled(
      NetworkTypePattern::Mobile(), false,
      base::Bind(&ShillError, "DisableCellularCallback"));
}

void InternetOptionsHandler::EnableWimaxCallback(const base::ListValue* args) {
  NetworkHandler::Get()->network_state_handler()->SetTechnologyEnabled(
      NetworkTypePattern::Wimax(), true,
      base::Bind(&ShillError, "EnableWimaxCallback"));
}

void InternetOptionsHandler::DisableWimaxCallback(const base::ListValue* args) {
  NetworkHandler::Get()->network_state_handler()->SetTechnologyEnabled(
      NetworkTypePattern::Wimax(), false,
      base::Bind(&ShillError, "DisableWimaxCallback"));
}

void InternetOptionsHandler::ShowMorePlanInfoCallback(
    const base::ListValue* args) {
  if (!web_ui())
    return;
  std::string service_path;
  if (args->GetSize() != 1 || !args->GetString(0, &service_path)) {
    NOTREACHED();
    return;
  }
  ash::network_connect::ShowMobileSetup(service_path);
}

void InternetOptionsHandler::BuyDataPlanCallback(const base::ListValue* args) {
  if (!web_ui())
    return;
  std::string service_path;
  if (args->GetSize() != 1 || !args->GetString(0, &service_path)) {
    NOTREACHED();
    return;
  }
  ash::network_connect::ShowMobileSetup(service_path);
}

void InternetOptionsHandler::SetApnCallback(const base::ListValue* args) {
  std::string service_path;
  if (!args->GetString(0, &service_path)) {
    NOTREACHED();
    return;
  }
  NetworkHandler::Get()->network_configuration_handler()->GetProperties(
      service_path,
      base::Bind(&InternetOptionsHandler::SetApnProperties,
                 weak_factory_.GetWeakPtr(), base::Owned(args->DeepCopy())),
      base::Bind(&ShillError, "SetApnCallback"));
}

void InternetOptionsHandler::SetApnProperties(
    const base::ListValue* args,
    const std::string& service_path,
    const base::DictionaryValue& shill_properties) {
  std::string apn, username, password;
  if (!args->GetString(1, &apn) ||
      !args->GetString(2, &username) ||
      !args->GetString(3, &password)) {
    NOTREACHED();
    return;
  }
  NET_LOG_EVENT("SetApnCallback", service_path);

  if (apn.empty()) {
    std::vector<std::string> properties_to_clear;
    properties_to_clear.push_back(shill::kCellularApnProperty);
    NetworkHandler::Get()->network_configuration_handler()->ClearProperties(
      service_path, properties_to_clear,
      base::Bind(&base::DoNothing),
      base::Bind(&ShillError, "ClearCellularApnProperties"));
    return;
  }

  const base::DictionaryValue* shill_apn_dict = NULL;
  std::string network_id;
  if (shill_properties.GetDictionaryWithoutPathExpansion(
          shill::kCellularApnProperty, &shill_apn_dict)) {
    shill_apn_dict->GetStringWithoutPathExpansion(
        shill::kApnNetworkIdProperty, &network_id);
  }
  base::DictionaryValue properties;
  base::DictionaryValue* apn_dict = new base::DictionaryValue;
  apn_dict->SetStringWithoutPathExpansion(shill::kApnProperty, apn);
  apn_dict->SetStringWithoutPathExpansion(shill::kApnNetworkIdProperty,
                                          network_id);
  apn_dict->SetStringWithoutPathExpansion(shill::kApnUsernameProperty,
                                          username);
  apn_dict->SetStringWithoutPathExpansion(shill::kApnPasswordProperty,
                                          password);
  properties.SetWithoutPathExpansion(shill::kCellularApnProperty, apn_dict);
  NetworkHandler::Get()->network_configuration_handler()->SetProperties(
      service_path, properties,
      base::Bind(&base::DoNothing),
      base::Bind(&ShillError, "SetApnProperties"));
}

void InternetOptionsHandler::CarrierStatusCallback() {
  NetworkStateHandler* handler = NetworkHandler::Get()->network_state_handler();
  const DeviceState* device =
      handler->GetDeviceStateByType(NetworkTypePattern::Cellular());
  if (device && (device->carrier() == shill::kCarrierSprint)) {
    const NetworkState* network =
        handler->FirstNetworkByType(NetworkTypePattern::Cellular());
    if (network) {
      ash::network_connect::ActivateCellular(network->path());
      UpdateConnectionData(network->path());
    }
  }
  UpdateCarrier();
}

void InternetOptionsHandler::SetCarrierCallback(const base::ListValue* args) {
  std::string service_path;
  std::string carrier;
  if (args->GetSize() != 2 ||
      !args->GetString(0, &service_path) ||
      !args->GetString(1, &carrier)) {
    NOTREACHED();
    return;
  }
  const DeviceState* device = NetworkHandler::Get()->network_state_handler()->
      GetDeviceStateByType(NetworkTypePattern::Cellular());
  if (!device) {
    LOG(WARNING) << "SetCarrierCallback with no cellular device.";
    return;
  }
  NetworkHandler::Get()->network_device_handler()->SetCarrier(
      device->path(),
      carrier,
      base::Bind(&InternetOptionsHandler::CarrierStatusCallback,
                 weak_factory_.GetWeakPtr()),
      base::Bind(&ShillError, "SetCarrierCallback"));
}

void InternetOptionsHandler::SetSimCardLockCallback(
    const base::ListValue* args) {
  bool require_pin_new_value;
  if (!args->GetBoolean(0, &require_pin_new_value)) {
    NOTREACHED();
    return;
  }
  // 1. Bring up SIM unlock dialog, pass new RequirePin setting in URL.
  // 2. Dialog will ask for current PIN in any case.
  // 3. If card is locked it will first call PIN unlock operation
  // 4. Then it will call Set RequirePin, passing the same PIN.
  // 5. We'll get notified by REQUIRE_PIN_SETTING_CHANGE_ENDED notification.
  SimDialogDelegate::SimDialogMode mode;
  if (require_pin_new_value)
    mode = SimDialogDelegate::SIM_DIALOG_SET_LOCK_ON;
  else
    mode = SimDialogDelegate::SIM_DIALOG_SET_LOCK_OFF;
  SimDialogDelegate::ShowDialog(GetNativeWindow(), mode);
}

void InternetOptionsHandler::ChangePinCallback(const base::ListValue* args) {
  SimDialogDelegate::ShowDialog(GetNativeWindow(),
      SimDialogDelegate::SIM_DIALOG_CHANGE_PIN);
}

void InternetOptionsHandler::RefreshNetworksCallback(
    const base::ListValue* args) {
  NetworkHandler::Get()->network_state_handler()->RequestScan();
}

std::string InternetOptionsHandler::GetIconDataUrl(int resource_id) const {
  gfx::ImageSkia* icon =
      ResourceBundle::GetSharedInstance().GetImageSkiaNamed(resource_id);
  gfx::ImageSkiaRep image_rep = icon->GetRepresentation(
      ui::GetImageScale(web_ui()->GetDeviceScaleFactor()));
  return webui::GetBitmapDataUrl(image_rep.sk_bitmap());
}

void InternetOptionsHandler::RefreshNetworkData() {
  base::DictionaryValue dictionary;
  FillNetworkInfo(&dictionary);
  web_ui()->CallJavascriptFunction(
      kRefreshNetworkDataFunction, dictionary);
}

void InternetOptionsHandler::UpdateConnectionData(
    const std::string& service_path) {
  NetworkHandler::Get()->network_configuration_handler()->GetProperties(
      service_path,
      base::Bind(&InternetOptionsHandler::UpdateConnectionDataCallback,
                 weak_factory_.GetWeakPtr()),
      base::Bind(&ShillError, "UpdateConnectionData"));
}

void InternetOptionsHandler::UpdateConnectionDataCallback(
    const std::string& service_path,
    const base::DictionaryValue& shill_properties) {
  const NetworkState* network = GetNetworkState(service_path);
  if (!network)
    return;
  base::DictionaryValue dictionary;
  PopulateConnectionDetails(network, shill_properties, &dictionary);
  web_ui()->CallJavascriptFunction(kUpdateConnectionDataFunction, dictionary);
}

void InternetOptionsHandler::UpdateCarrier() {
  web_ui()->CallJavascriptFunction(kUpdateCarrierFunction);
}

void InternetOptionsHandler::DeviceListChanged() {
  if (!web_ui())
    return;
  RefreshNetworkData();
}

void InternetOptionsHandler::NetworkListChanged() {
  if (!web_ui())
    return;
  RefreshNetworkData();
}

void InternetOptionsHandler::NetworkConnectionStateChanged(
    const NetworkState* network) {
  if (!web_ui())
    return;
  // Update the connection data for the detailed view when the connection state
  // of any network changes.
  if (!details_path_.empty())
    UpdateConnectionData(details_path_);
}

void InternetOptionsHandler::NetworkPropertiesUpdated(
    const NetworkState* network) {
  if (!web_ui())
    return;
  RefreshNetworkData();
  UpdateConnectionData(network->path());
}

void InternetOptionsHandler::LoggedInStateChanged() {
  UpdateLoggedInUserType();
}

void InternetOptionsHandler::UpdateLoggedInUserType() {
  if (!web_ui())
    return;
  base::StringValue login_type(
      LoggedInUserTypeToString(LoginState::Get()->GetLoggedInUserType()));
  web_ui()->CallJavascriptFunction(
      kUpdateLoggedInUserTypeFunction, login_type);
}

void InternetOptionsHandler::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  OptionsPageUIHandler::Observe(type, source, details);
  if (type == chrome::NOTIFICATION_REQUIRE_PIN_SETTING_CHANGE_ENDED) {
    base::FundamentalValue require_pin(*content::Details<bool>(details).ptr());
    web_ui()->CallJavascriptFunction(
        kUpdateSecurityTabFunction, require_pin);
  } else if (type == chrome::NOTIFICATION_ENTER_PIN_ENDED) {
    // We make an assumption (which is valid for now) that the SIM
    // unlock dialog is put up only when the user is trying to enable
    // mobile data.
    bool cancelled = *content::Details<bool>(details).ptr();
    if (cancelled)
      RefreshNetworkData();
    // The case in which the correct PIN was entered and the SIM is
    // now unlocked is handled in NetworkMenuButton.
  }
}

void InternetOptionsHandler::SetServerHostnameCallback(
    const base::ListValue* args) {
  std::string service_path, server_hostname;
  if (args->GetSize() < 2 ||
      !args->GetString(0, &service_path) ||
      !args->GetString(1, &server_hostname)) {
    NOTREACHED();
    return;
  }
  SetNetworkProperty(service_path, shill::kProviderHostProperty,
                     base::Value::CreateStringValue(server_hostname));
}

void InternetOptionsHandler::SetPreferNetworkCallback(
    const base::ListValue* args) {
  std::string service_path, prefer_network_str;
  if (args->GetSize() < 2 ||
      !args->GetString(0, &service_path) ||
      !args->GetString(1, &prefer_network_str)) {
    NOTREACHED();
    return;
  }
  int priority = (prefer_network_str == kTagTrue) ? kPreferredPriority : 0;
  SetNetworkProperty(service_path, shill::kPriorityProperty,
                     base::Value::CreateIntegerValue(priority));
}

void InternetOptionsHandler::SetAutoConnectCallback(
    const base::ListValue* args) {
  std::string service_path, auto_connect_str;
  if (args->GetSize() < 2 ||
      !args->GetString(0, &service_path) ||
      !args->GetString(1, &auto_connect_str)) {
    NOTREACHED();
    return;
  }
  bool auto_connect = auto_connect_str == kTagTrue;
  SetNetworkProperty(service_path, shill::kAutoConnectProperty,
                     base::Value::CreateBooleanValue(auto_connect));
}

void InternetOptionsHandler::SetIPConfigCallback(const base::ListValue* args) {
  std::string service_path;
  if (!args->GetString(0, &service_path)) {
    NOTREACHED();
    return;
  }
  NetworkHandler::Get()->network_configuration_handler()->GetProperties(
      service_path,
      base::Bind(&InternetOptionsHandler::SetIPConfigProperties,
                 weak_factory_.GetWeakPtr(), base::Owned(args->DeepCopy())),
      base::Bind(&ShillError, "SetIPConfigCallback"));
}

void InternetOptionsHandler::SetIPConfigProperties(
    const base::ListValue* args,
    const std::string& service_path,
    const base::DictionaryValue& shill_properties) {
  std::string address, netmask, gateway, name_server_type, name_servers;
  bool dhcp_for_ip;
  if (!args->GetBoolean(1, &dhcp_for_ip) ||
      !args->GetString(2, &address) ||
      !args->GetString(3, &netmask) ||
      !args->GetString(4, &gateway) ||
      !args->GetString(5, &name_server_type) ||
      !args->GetString(6, &name_servers)) {
    NOTREACHED();
    return;
  }
  NET_LOG_USER("SetIPConfigProperties", service_path);

  bool request_reconnect = false;
  std::vector<std::string> properties_to_clear;
  base::DictionaryValue properties_to_set;

  if (dhcp_for_ip) {
    request_reconnect |= AppendPropertyKeyIfPresent(
        shill::kStaticIPAddressProperty,
        shill_properties, &properties_to_clear);
    request_reconnect |= AppendPropertyKeyIfPresent(
        shill::kStaticIPPrefixlenProperty,
        shill_properties, &properties_to_clear);
    request_reconnect |= AppendPropertyKeyIfPresent(
        shill::kStaticIPGatewayProperty,
        shill_properties, &properties_to_clear);
  } else {
    request_reconnect |= AddStringPropertyIfChanged(
        shill::kStaticIPAddressProperty,
        address, shill_properties, &properties_to_set);
    int prefixlen = network_util::NetmaskToPrefixLength(netmask);
    if (prefixlen < 0) {
      LOG(ERROR) << "Invalid prefix length for: " << service_path
                 << " with netmask " << netmask;
      prefixlen = 0;
    }
    request_reconnect |= AddIntegerPropertyIfChanged(
        shill::kStaticIPPrefixlenProperty,
        prefixlen, shill_properties, &properties_to_set);
    request_reconnect |= AddStringPropertyIfChanged(
        shill::kStaticIPGatewayProperty,
        gateway, shill_properties, &properties_to_set);
  }

  if (name_server_type == kNameServerTypeAutomatic) {
    AppendPropertyKeyIfPresent(shill::kStaticIPNameServersProperty,
                               shill_properties, &properties_to_clear);
  } else {
    if (name_server_type == kNameServerTypeGoogle)
      name_servers = kGoogleNameServers;
    AddStringPropertyIfChanged(
        shill::kStaticIPNameServersProperty,
        name_servers, shill_properties, &properties_to_set);
  }

  if (!properties_to_clear.empty()) {
    NetworkHandler::Get()->network_configuration_handler()->ClearProperties(
      service_path, properties_to_clear,
      base::Bind(&base::DoNothing),
      base::Bind(&ShillError, "ClearIPConfigProperties"));
  }
  if (!properties_to_set.empty()) {
    NetworkHandler::Get()->network_configuration_handler()->SetProperties(
        service_path, properties_to_set,
        base::Bind(&base::DoNothing),
        base::Bind(&ShillError, "SetIPConfigProperties"));
  }
  std::string device_path;
  shill_properties.GetStringWithoutPathExpansion(
      shill::kDeviceProperty, &device_path);
  if (!device_path.empty()) {
    base::Closure callback = base::Bind(&base::DoNothing);
    // If auto config or a static IP property changed, we need to reconnect
    // to the network.
    if (request_reconnect)
      callback = base::Bind(&RequestReconnect, service_path, GetNativeWindow());
    NetworkHandler::Get()->network_device_handler()->RequestRefreshIPConfigs(
        device_path,
        callback,
        base::Bind(&ShillError, "RequestRefreshIPConfigs"));
  }
}

void InternetOptionsHandler::PopulateDictionaryDetailsCallback(
    const std::string& service_path,
    const base::DictionaryValue& shill_properties) {
  const NetworkState* network = GetNetworkState(service_path);
  if (!network) {
    LOG(ERROR) << "Network properties not found: " << service_path;
    return;
  }

  details_path_ = service_path;

  ::onc::ONCSource onc_source = ::onc::ONC_SOURCE_NONE;
  const base::DictionaryValue* onc =
      onc::FindPolicyForActiveUser(network->guid(), &onc_source);
  const NetworkPropertyUIData property_ui_data(onc_source);

  base::DictionaryValue dictionary;

  // Device hardware address
  const DeviceState* device = NetworkHandler::Get()->network_state_handler()->
      GetDeviceState(network->device_path());
  if (device)
    dictionary.SetString(kTagHardwareAddress, device->mac_address());

  // IP config
  scoped_ptr<base::DictionaryValue> ipconfig_dhcp(new base::DictionaryValue);
  ipconfig_dhcp->SetString(kIpConfigAddress, network->ip_address());
  ipconfig_dhcp->SetString(kIpConfigNetmask, network->GetNetmask());
  ipconfig_dhcp->SetString(kIpConfigGateway, network->gateway());
  std::string ipconfig_name_servers = network->GetDnsServersAsString();
  ipconfig_dhcp->SetString(kIpConfigNameServers, ipconfig_name_servers);
  ipconfig_dhcp->SetString(kIpConfigWebProxyAutoDiscoveryUrl,
                           network->web_proxy_auto_discovery_url().spec());
  SetValueDictionary(&dictionary,
                     kDictionaryIpConfig,
                     ipconfig_dhcp.release(),
                     property_ui_data);

  std::string name_server_type = kNameServerTypeAutomatic;
  int automatic_ip_config = 0;
  scoped_ptr<base::DictionaryValue> static_ip_dict(
      BuildIPInfoDictionary(shill_properties, true, &automatic_ip_config));
  dictionary.SetBoolean(kIpConfigAutoConfig, automatic_ip_config == 0);
  DCHECK(automatic_ip_config == 3 || automatic_ip_config == 0)
      << "UI doesn't support automatic specification of individual "
      << "static IP parameters.";
  scoped_ptr<base::DictionaryValue> saved_ip_dict(
      BuildIPInfoDictionary(shill_properties, false, NULL));
  dictionary.Set(kDictionarySavedIp, saved_ip_dict.release());

  // Determine what kind of name server setting we have by comparing the
  // StaticIP and Google values with the ipconfig values.
  std::string static_ip_nameservers;
  static_ip_dict->GetString(kIpConfigNameServers, &static_ip_nameservers);
  if (!static_ip_nameservers.empty() &&
      static_ip_nameservers == ipconfig_name_servers) {
    name_server_type = kNameServerTypeUser;
  }
  if (ipconfig_name_servers == kGoogleNameServers) {
    name_server_type = kNameServerTypeGoogle;
  }
  SetValueDictionary(&dictionary,
                     kDictionaryStaticIp,
                     static_ip_dict.release(),
                     property_ui_data);

  std::string type = network->type();
  dictionary.SetString(kTagType, type);
  dictionary.SetString(kTagServicePath, network->path());
  dictionary.SetString(kTagNameServerType, name_server_type);
  dictionary.SetString(kTagNameServersGoogle, kGoogleNameServers);

  // Only show proxy for remembered networks.
  dictionary.SetBoolean(kTagShowProxy, !network->profile_path().empty());

  // Enable static ip config for Ethernet or WiFi.
  bool staticIPConfig = network->Matches(NetworkTypePattern::Ethernet()) ||
                        type == shill::kTypeWifi;
  dictionary.SetBoolean(kTagShowStaticIPConfig, staticIPConfig);

  dictionary.SetBoolean(kTagShowPreferred, !network->profile_path().empty());
  int priority = 0;
  shill_properties.GetIntegerWithoutPathExpansion(
      shill::kPriorityProperty, &priority);
  bool preferred = priority > 0;
  SetValueDictionary(&dictionary, kTagPreferred,
                     new base::FundamentalValue(preferred),
                     property_ui_data);

  NetworkPropertyUIData auto_connect_ui_data(onc_source);
  std::string onc_path_to_auto_connect;
  if (type == shill::kTypeWifi) {
    onc_path_to_auto_connect = base::StringPrintf(
        "%s.%s",
        ::onc::network_config::kWiFi,
        ::onc::wifi::kAutoConnect);
  } else if (type == shill::kTypeVPN) {
    onc_path_to_auto_connect = base::StringPrintf(
        "%s.%s",
        ::onc::network_config::kVPN,
        ::onc::vpn::kAutoConnect);
  }
  if (!onc_path_to_auto_connect.empty()) {
    auto_connect_ui_data.ParseOncProperty(
        onc_source, onc, onc_path_to_auto_connect);
  }
  bool auto_connect = false;
  shill_properties.GetBooleanWithoutPathExpansion(
      shill::kAutoConnectProperty, &auto_connect);
  SetAutoconnectValueDictionary(network->IsPrivate(),
                                onc_source,
                                auto_connect,
                                auto_connect_ui_data,
                                &dictionary);

  PopulateConnectionDetails(network, shill_properties, &dictionary);

  // Show details dialog
  web_ui()->CallJavascriptFunction(kShowDetailedInfoFunction, dictionary);
}

namespace {

void PopulateConnectionDetails(const NetworkState* network,
                               const base::DictionaryValue& shill_properties,
                               base::DictionaryValue* dictionary) {
  dictionary->SetString(kNetworkInfoKeyServicePath, network->path());
  dictionary->SetString(kTagServiceName, network->name());
  dictionary->SetBoolean(kTagConnecting, network->IsConnectingState());
  dictionary->SetBoolean(kTagConnected, network->IsConnectedState());
  dictionary->SetString(kTagConnectionState,
                        ConnectionStateString(network->connection_state()));
  dictionary->SetString(kTagNetworkName, network->name());
  dictionary->SetString(
      kTagErrorState,
      ash::network_connect::ErrorString(network->error(), network->path()));

  dictionary->SetBoolean(kTagRemembered, !network->profile_path().empty());
  bool shared = !network->IsPrivate();
  dictionary->SetBoolean(kTagShared, shared);

  const std::string& type = network->type();
  const NetworkState* connected_network =
      NetworkHandler::Get()->network_state_handler()->ConnectedNetworkByType(
          NetworkTypePattern::Primitive(type));

  dictionary->SetBoolean(kTagDeviceConnected, connected_network != NULL);

  if (type == shill::kTypeWifi)
    PopulateWifiDetails(network, shill_properties, dictionary);
  else if (type == shill::kTypeWimax)
    PopulateWimaxDetails(network, shill_properties, dictionary);
  else if (type == shill::kTypeCellular)
    PopulateCellularDetails(network, shill_properties, dictionary);
  else if (type == shill::kTypeVPN)
    PopulateVPNDetails(network, shill_properties, dictionary);
}

void PopulateWifiDetails(const NetworkState* wifi,
                         const base::DictionaryValue& shill_properties,
                         base::DictionaryValue* dictionary) {
  dictionary->SetString(kTagSsid, wifi->name());
  dictionary->SetInteger(kTagStrength, wifi->signal_strength());
  dictionary->SetString(kTagEncryption,
                        EncryptionString(wifi->security(), wifi->eap_method()));
  CopyStringFromDictionary(shill_properties, shill::kWifiBSsid,
                           kTagBssid, dictionary);
  CopyIntegerFromDictionary(shill_properties, shill::kWifiFrequency,
                            kTagFrequency, false, dictionary);
}

void PopulateWimaxDetails(const NetworkState* wimax,
                          const base::DictionaryValue& shill_properties,
                          base::DictionaryValue* dictionary) {
  dictionary->SetInteger(kTagStrength, wimax->signal_strength());
  CopyStringFromDictionary(shill_properties, shill::kEapIdentityProperty,
                           kTagIdentity, dictionary);
}

void CreateDictionaryFromCellularApn(const base::DictionaryValue* apn,
                                     base::DictionaryValue* dictionary) {
  CopyStringFromDictionary(*apn, shill::kApnProperty,
                           kTagApn, dictionary);
  CopyStringFromDictionary(*apn, shill::kApnNetworkIdProperty,
                           kTagNetworkId, dictionary);
  CopyStringFromDictionary(*apn, shill::kApnUsernameProperty,
                           kTagUsername, dictionary);
  CopyStringFromDictionary(*apn, shill::kApnPasswordProperty,
                           kTagPassword, dictionary);
  CopyStringFromDictionary(*apn, shill::kApnNameProperty,
                           kTagName, dictionary);
  CopyStringFromDictionary(*apn, shill::kApnLocalizedNameProperty,
                           kTagLocalizedName, dictionary);
  CopyStringFromDictionary(*apn, shill::kApnLanguageProperty,
                           kTagLanguage, dictionary);
}

void PopulateCellularDetails(const NetworkState* cellular,
                             const base::DictionaryValue& shill_properties,
                             base::DictionaryValue* dictionary) {
  dictionary->SetBoolean(kTagCarrierSelectFlag,
                         CommandLine::ForCurrentProcess()->HasSwitch(
                             chromeos::switches::kEnableCarrierSwitching));
  // Cellular network / connection settings.
  dictionary->SetString(kTagNetworkTechnology, cellular->network_technology());
  dictionary->SetString(kTagActivationState,
                        ActivationStateString(cellular->activation_state()));
  dictionary->SetString(kTagRoamingState,
                        RoamingStateString(cellular->roaming()));
  bool restricted = cellular->connection_state() == shill::kStatePortal;
  dictionary->SetString(kTagRestrictedPool,
                        restricted ?
                        l10n_util::GetStringUTF8(
                            IDS_CONFIRM_MESSAGEBOX_YES_BUTTON_LABEL) :
                        l10n_util::GetStringUTF8(
                            IDS_CONFIRM_MESSAGEBOX_NO_BUTTON_LABEL));

  const base::DictionaryValue* serving_operator = NULL;
  if (shill_properties.GetDictionaryWithoutPathExpansion(
          shill::kServingOperatorProperty, &serving_operator)) {
    CopyStringFromDictionary(*serving_operator, shill::kOperatorNameKey,
                             kTagOperatorName, dictionary);
    CopyStringFromDictionary(*serving_operator, shill::kOperatorCodeKey,
                             kTagOperatorCode, dictionary);
  }

  const base::DictionaryValue* olp = NULL;
  if (shill_properties.GetDictionaryWithoutPathExpansion(
          shill::kPaymentPortalProperty, &olp)) {
    std::string url;
    olp->GetStringWithoutPathExpansion(shill::kPaymentPortalURL, &url);
    dictionary->SetString(kTagSupportUrl, url);
  }

  base::DictionaryValue* apn = new base::DictionaryValue;
  const base::DictionaryValue* source_apn = NULL;
  if (shill_properties.GetDictionaryWithoutPathExpansion(
          shill::kCellularApnProperty, &source_apn)) {
    CreateDictionaryFromCellularApn(source_apn, apn);
  }
  dictionary->Set(kTagApn, apn);

  base::DictionaryValue* last_good_apn = new base::DictionaryValue;
  if (shill_properties.GetDictionaryWithoutPathExpansion(
          shill::kCellularLastGoodApnProperty, &source_apn)) {
    CreateDictionaryFromCellularApn(source_apn, last_good_apn);
  }
  dictionary->Set(kTagLastGoodApn, last_good_apn);

  // These default to empty and are only set if device != NULL.
  std::string carrier_id;
  std::string mdn;

  // Device settings.
  const DeviceState* device = NetworkHandler::Get()->network_state_handler()->
      GetDeviceState(cellular->device_path());
  if (device) {
    // TODO(stevenjb): Add NetworkDeviceHandler::GetProperties() and use that
    // to retrieve the complete dictionary of device properties, instead of
    // caching them (will be done for the new UI).
    const base::DictionaryValue& device_properties = device->properties();
    const NetworkPropertyUIData cellular_property_ui_data(
        cellular->ui_data().onc_source());
    CopyStringFromDictionary(device_properties, shill::kManufacturerProperty,
                            kTagManufacturer, dictionary);
    CopyStringFromDictionary(device_properties, shill::kModelIDProperty,
                            kTagModelId, dictionary);
    CopyStringFromDictionary(device_properties,
                            shill::kFirmwareRevisionProperty,
                            kTagFirmwareRevision, dictionary);
    CopyStringFromDictionary(device_properties,
                            shill::kHardwareRevisionProperty,
                            kTagHardwareRevision, dictionary);
    CopyIntegerFromDictionary(device_properties, shill::kPRLVersionProperty,
                             kTagPrlVersion, true, dictionary);
    CopyStringFromDictionary(device_properties, shill::kMeidProperty,
                            kTagMeid, dictionary);
    CopyStringFromDictionary(device_properties, shill::kIccidProperty,
                            kTagIccid, dictionary);
    CopyStringFromDictionary(device_properties, shill::kImeiProperty,
                            kTagImei, dictionary);
    mdn = CopyStringFromDictionary(device_properties, shill::kMdnProperty,
                                   kTagMdn, dictionary);
    CopyStringFromDictionary(device_properties, shill::kImsiProperty,
                            kTagImsi, dictionary);
    CopyStringFromDictionary(device_properties, shill::kEsnProperty,
                            kTagEsn, dictionary);
    CopyStringFromDictionary(device_properties, shill::kMinProperty,
                            kTagMin, dictionary);
    std::string family;
    device_properties.GetStringWithoutPathExpansion(
        shill::kTechnologyFamilyProperty, &family);
    dictionary->SetBoolean(kTagGsm, family == shill::kNetworkTechnologyGsm);

    SetValueDictionary(
        dictionary, kTagSimCardLockEnabled,
        new base::FundamentalValue(device->sim_lock_enabled()),
        cellular_property_ui_data);

    carrier_id = device->home_provider_id();

    MobileConfig* config = MobileConfig::GetInstance();
    if (config->IsReady()) {
      const MobileConfig::Carrier* carrier = config->GetCarrier(carrier_id);
      if (carrier && !carrier->top_up_url().empty())
        dictionary->SetString(kTagCarrierUrl, carrier->top_up_url());
    }

    base::ListValue* apn_list_value = new base::ListValue();
    const base::ListValue* apn_list;
    if (device_properties.GetListWithoutPathExpansion(
            shill::kCellularApnListProperty, &apn_list)) {
      for (base::ListValue::const_iterator iter = apn_list->begin();
           iter != apn_list->end(); ++iter) {
        const base::DictionaryValue* dict;
        if ((*iter)->GetAsDictionary(&dict)) {
          base::DictionaryValue* apn = new base::DictionaryValue;
          CreateDictionaryFromCellularApn(dict, apn);
          apn_list_value->Append(apn);
        }
      }
    }
    SetValueDictionary(dictionary, kTagProviderApnList, apn_list_value,
                       cellular_property_ui_data);
    if (CommandLine::ForCurrentProcess()->HasSwitch(
            chromeos::switches::kEnableCarrierSwitching)) {
      const base::ListValue* supported_carriers;
      if (device_properties.GetListWithoutPathExpansion(
              shill::kSupportedCarriersProperty, &supported_carriers)) {
        dictionary->Set(kTagCarriers, supported_carriers->DeepCopy());
        dictionary->SetInteger(kTagCurrentCarrierIndex,
                               FindCurrentCarrierIndex(supported_carriers,
                                                       device));
      } else {
        // In case of any error, set the current carrier tag to -1 indicating
        // to the JS code to fallback to a single carrier.
        dictionary->SetInteger(kTagCurrentCarrierIndex, -1);
      }
    }
  }

  // Set Cellular Buttons Visibility
  dictionary->SetBoolean(
      kTagDisableConnectButton,
      cellular->activation_state() == shill::kActivationStateActivating ||
      cellular->IsConnectingState());

  // Don't show any account management related buttons if the activation
  // state is unknown or no payment portal URL is available.
  std::string support_url;
  if (cellular->activation_state() == shill::kActivationStateUnknown ||
      !dictionary->GetString(kTagSupportUrl, &support_url) ||
      support_url.empty()) {
    VLOG(2) << "No support URL is available. Don't display buttons.";
    return;
  }

  if (cellular->activation_state() != shill::kActivationStateActivating &&
      cellular->activation_state() != shill::kActivationStateActivated) {
    dictionary->SetBoolean(kTagShowActivateButton, true);
  } else {
    const MobileConfig::Carrier* carrier =
        MobileConfig::GetInstance()->GetCarrier(carrier_id);
    if (carrier && carrier->show_portal_button()) {
      // The button should be shown for a LTE network even when the LTE network
      // is not connected, but CrOS is online. This is done to enable users to
      // update their plan even if they are out of credits.
      // The button should not be shown when the device's mdn is not set,
      // because the network's proper portal url cannot be generated without it
      const NetworkState* default_network =
          NetworkHandler::Get()->network_state_handler()->DefaultNetwork();
      const std::string& technology = cellular->network_technology();
      bool force_show_view_account_button =
          (technology == shill::kNetworkTechnologyLte ||
           technology == shill::kNetworkTechnologyLteAdvanced) &&
          default_network &&
          !mdn.empty();

      // The button will trigger ShowMorePlanInfoCallback() which will open
      // carrier specific portal.
      if (cellular->IsConnectedState() || force_show_view_account_button)
        dictionary->SetBoolean(kTagShowViewAccountButton, true);
    }
  }
}

}  // namespace

gfx::NativeWindow InternetOptionsHandler::GetNativeWindow() const {
  return web_ui()->GetWebContents()->GetView()->GetTopLevelNativeWindow();
}

void InternetOptionsHandler::NetworkCommandCallback(
    const base::ListValue* args) {
  std::string type;
  std::string service_path;
  std::string command;
  if (args->GetSize() != 3 ||
      !args->GetString(0, &type) ||
      !args->GetString(1, &service_path) ||
      !args->GetString(2, &command)) {
    NOTREACHED();
    return;
  }

  // Process commands that do not require an existing network.
  if (command == kTagAddConnection) {
    if (CanAddNetworkType(type))
      AddConnection(type);
  } else if (command == kTagForget) {
    if (CanForgetNetworkType(type)) {
      NetworkHandler::Get()->network_configuration_handler()->
          RemoveConfiguration(
              service_path,
              base::Bind(&base::DoNothing),
              base::Bind(&ShillError, "NetworkCommand: " + command));
    }
  } else if (command == kTagOptions) {
    NetworkHandler::Get()->network_configuration_handler()->GetProperties(
        service_path,
        base::Bind(&InternetOptionsHandler::PopulateDictionaryDetailsCallback,
                   weak_factory_.GetWeakPtr()),
        base::Bind(&ShillError, "NetworkCommand: " + command));
  } else if (command == kTagConnect) {
    ash::network_connect::ConnectToNetwork(service_path, GetNativeWindow());
  } else if (command == kTagDisconnect) {
    NetworkHandler::Get()->network_connection_handler()->DisconnectNetwork(
        service_path,
        base::Bind(&base::DoNothing),
        base::Bind(&ShillError, "NetworkCommand: " + command));
  } else if (command == kTagConfigure) {
    NetworkConfigView::Show(service_path, GetNativeWindow());
  } else if (command == kTagActivate && type == shill::kTypeCellular) {
    ash::network_connect::ActivateCellular(service_path);
    // Activation may update network properties (e.g. ActivationState), so
    // request them here in case they change.
    UpdateConnectionData(service_path);
  } else {
    VLOG(1) << "Unknown command: " << command;
    NOTREACHED();
  }
}

void InternetOptionsHandler::AddConnection(const std::string& type) {
  if (type == shill::kTypeWifi)
    NetworkConfigView::ShowForType(shill::kTypeWifi, GetNativeWindow());
  else if (type == shill::kTypeVPN)
    NetworkConfigView::ShowForType(shill::kTypeVPN, GetNativeWindow());
  else if (type == shill::kTypeCellular)
    ChooseMobileNetworkDialog::ShowDialog(GetNativeWindow());
  else
    NOTREACHED();
}

base::ListValue* InternetOptionsHandler::GetWiredList() {
  base::ListValue* list = new base::ListValue();
  const NetworkState* network = NetworkHandler::Get()->network_state_handler()->
      FirstNetworkByType(NetworkTypePattern::Ethernet());
  if (!network)
    return list;
  list->Append(
      BuildNetworkDictionary(network,
                             web_ui()->GetDeviceScaleFactor(),
                             Profile::FromWebUI(web_ui())->GetPrefs()));
  return list;
}

base::ListValue* InternetOptionsHandler::GetWirelessList() {
  base::ListValue* list = new base::ListValue();

  NetworkStateHandler::NetworkStateList networks;
  NetworkHandler::Get()->network_state_handler()->GetNetworkListByType(
      NetworkTypePattern::Wireless(), &networks);
  for (NetworkStateHandler::NetworkStateList::const_iterator iter =
           networks.begin(); iter != networks.end(); ++iter) {
    list->Append(
        BuildNetworkDictionary(*iter,
                               web_ui()->GetDeviceScaleFactor(),
                               Profile::FromWebUI(web_ui())->GetPrefs()));
  }

  return list;
}

base::ListValue* InternetOptionsHandler::GetVPNList() {
  base::ListValue* list = new base::ListValue();

  NetworkStateHandler::NetworkStateList networks;
  NetworkHandler::Get()->network_state_handler()->GetNetworkListByType(
      NetworkTypePattern::VPN(), &networks);
  for (NetworkStateHandler::NetworkStateList::const_iterator iter =
           networks.begin(); iter != networks.end(); ++iter) {
    list->Append(
        BuildNetworkDictionary(*iter,
                               web_ui()->GetDeviceScaleFactor(),
                               Profile::FromWebUI(web_ui())->GetPrefs()));
  }

  return list;
}

base::ListValue* InternetOptionsHandler::GetRememberedList() {
  base::ListValue* list = new base::ListValue();

  NetworkStateHandler::FavoriteStateList favorites;
  NetworkHandler::Get()->network_state_handler()->GetFavoriteList(&favorites);
  for (NetworkStateHandler::FavoriteStateList::const_iterator iter =
           favorites.begin(); iter != favorites.end(); ++iter) {
    const FavoriteState* favorite = *iter;
    if (favorite->type() != shill::kTypeWifi &&
        favorite->type() != shill::kTypeVPN)
      continue;
    list->Append(
        BuildFavoriteDictionary(favorite,
                                web_ui()->GetDeviceScaleFactor(),
                                Profile::FromWebUI(web_ui())->GetPrefs()));
  }

  return list;
}

void InternetOptionsHandler::FillNetworkInfo(
    base::DictionaryValue* dictionary) {
  NetworkStateHandler* handler = NetworkHandler::Get()->network_state_handler();
  dictionary->Set(kTagWiredList, GetWiredList());
  dictionary->Set(kTagWirelessList, GetWirelessList());
  dictionary->Set(kTagVpnList, GetVPNList());
  dictionary->Set(kTagRememberedList, GetRememberedList());

  dictionary->SetBoolean(
      kTagWifiAvailable,
      handler->IsTechnologyAvailable(NetworkTypePattern::WiFi()));
  dictionary->SetBoolean(
      kTagWifiEnabled,
      handler->IsTechnologyEnabled(NetworkTypePattern::WiFi()));

  dictionary->SetBoolean(
      kTagCellularAvailable,
      handler->IsTechnologyAvailable(NetworkTypePattern::Mobile()));
  dictionary->SetBoolean(
      kTagCellularEnabled,
      handler->IsTechnologyEnabled(NetworkTypePattern::Mobile()));
  const DeviceState* cellular =
      handler->GetDeviceStateByType(NetworkTypePattern::Mobile());
  dictionary->SetBoolean(
      kTagCellularSupportsScan,
      cellular && cellular->support_network_scan());

  dictionary->SetBoolean(
      kTagWimaxAvailable,
      handler->IsTechnologyAvailable(NetworkTypePattern::Wimax()));
  dictionary->SetBoolean(
      kTagWimaxEnabled,
      handler->IsTechnologyEnabled(NetworkTypePattern::Wimax()));
}

}  // namespace options
}  // namespace chromeos
