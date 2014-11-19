// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/network/onc/onc_translation_tables.h"

#include <cstddef>

#include "base/logging.h"
#include "components/onc/onc_constants.h"
#include "third_party/cros_system_api/dbus/service_constants.h"

namespace chromeos {
namespace onc {

// CertificatePattern is converted with function CreateUIData(...) to UIData
// stored in Shill.
//
// Proxy settings are converted to Shill by function
// ConvertOncProxySettingsToProxyConfig(...).
//
// Translation of IPConfig objects is not supported, yet.

namespace {

const FieldTranslationEntry eap_fields[] = {
    { ::onc::eap::kAnonymousIdentity, shill::kEapAnonymousIdentityProperty},
    { ::onc::eap::kIdentity, shill::kEapIdentityProperty},
    // This field is converted during translation, see onc_translator_*.
    // { ::onc::eap::kInner, shill::kEapPhase2AuthProperty },

    // This field is converted during translation, see onc_translator_*.
    // { ::onc::eap::kOuter, shill::kEapMethodProperty },
    { ::onc::eap::kPassword, shill::kEapPasswordProperty},
    { ::onc::eap::kSaveCredentials, shill::kSaveCredentialsProperty},
    { ::onc::eap::kServerCAPEMs, shill::kEapCaCertPemProperty},
    { ::onc::eap::kUseSystemCAs, shill::kEapUseSystemCasProperty},
    {NULL}};

const FieldTranslationEntry ipsec_fields[] = {
    // Ignored by Shill, not necessary to synchronize.
    // { ::onc::ipsec::kAuthenticationType, shill::kL2tpIpsecAuthenticationType
    // },
    { ::onc::ipsec::kGroup, shill::kL2tpIpsecTunnelGroupProperty},
    // Ignored by Shill, not necessary to synchronize.
    // { ::onc::ipsec::kIKEVersion, shill::kL2tpIpsecIkeVersion },
    { ::onc::ipsec::kPSK, shill::kL2tpIpsecPskProperty},
    { ::onc::vpn::kSaveCredentials, shill::kSaveCredentialsProperty},
    { ::onc::ipsec::kServerCAPEMs, shill::kL2tpIpsecCaCertPemProperty},
    {NULL}};

const FieldTranslationEntry l2tp_fields[] = {
    { ::onc::vpn::kPassword, shill::kL2tpIpsecPasswordProperty},
    // We don't synchronize l2tp's SaveCredentials field for now, as Shill
    // doesn't
    // support separate settings for ipsec and l2tp.
    // { ::onc::vpn::kSaveCredentials, &kBoolSignature },
    { ::onc::vpn::kUsername, shill::kL2tpIpsecUserProperty}, {NULL}};

const FieldTranslationEntry openvpn_fields[] = {
    { ::onc::openvpn::kAuth, shill::kOpenVPNAuthProperty},
    { ::onc::openvpn::kAuthNoCache, shill::kOpenVPNAuthNoCacheProperty},
    { ::onc::openvpn::kAuthRetry, shill::kOpenVPNAuthRetryProperty},
    { ::onc::openvpn::kCipher, shill::kOpenVPNCipherProperty},
    { ::onc::openvpn::kCompLZO, shill::kOpenVPNCompLZOProperty},
    { ::onc::openvpn::kCompNoAdapt, shill::kOpenVPNCompNoAdaptProperty},
    { ::onc::openvpn::kKeyDirection, shill::kOpenVPNKeyDirectionProperty},
    { ::onc::openvpn::kNsCertType, shill::kOpenVPNNsCertTypeProperty},
    { ::onc::vpn::kPassword, shill::kOpenVPNPasswordProperty},
    { ::onc::openvpn::kPort, shill::kOpenVPNPortProperty},
    { ::onc::openvpn::kProto, shill::kOpenVPNProtoProperty},
    { ::onc::openvpn::kPushPeerInfo, shill::kOpenVPNPushPeerInfoProperty},
    { ::onc::openvpn::kRemoteCertEKU, shill::kOpenVPNRemoteCertEKUProperty},
    // This field is converted during translation, see onc_translator_*.
    // { ::onc::openvpn::kRemoteCertKU, shill::kOpenVPNRemoteCertKUProperty },
    { ::onc::openvpn::kRemoteCertTLS, shill::kOpenVPNRemoteCertTLSProperty},
    { ::onc::openvpn::kRenegSec, shill::kOpenVPNRenegSecProperty},
    { ::onc::vpn::kSaveCredentials, shill::kSaveCredentialsProperty},
    { ::onc::openvpn::kServerCAPEMs, shill::kOpenVPNCaCertPemProperty},
    { ::onc::openvpn::kServerPollTimeout,
      shill::kOpenVPNServerPollTimeoutProperty},
    { ::onc::openvpn::kShaper, shill::kOpenVPNShaperProperty},
    { ::onc::openvpn::kStaticChallenge, shill::kOpenVPNStaticChallengeProperty},
    { ::onc::openvpn::kTLSAuthContents, shill::kOpenVPNTLSAuthContentsProperty},
    { ::onc::openvpn::kTLSRemote, shill::kOpenVPNTLSRemoteProperty},
    { ::onc::vpn::kUsername, shill::kOpenVPNUserProperty}, {NULL}};

const FieldTranslationEntry vpn_fields[] = {
    { ::onc::vpn::kAutoConnect, shill::kAutoConnectProperty},
    { ::onc::vpn::kHost, shill::kProviderHostProperty},
    // This field is converted during translation, see onc_translator_*.
    // { ::onc::vpn::kType, shill::kProviderTypeProperty },
    {NULL}};

const FieldTranslationEntry wifi_fields[] = {
    { ::onc::wifi::kAutoConnect, shill::kAutoConnectProperty},
    { ::onc::wifi::kBSSID, shill::kWifiBSsid},
    { ::onc::wifi::kFrequency, shill::kWifiFrequency},
    { ::onc::wifi::kFrequencyList, shill::kWifiFrequencyListProperty},
    { ::onc::wifi::kHiddenSSID, shill::kWifiHiddenSsid},
    { ::onc::wifi::kPassphrase, shill::kPassphraseProperty},
    // This field is converted during translation, see onc_translator_*.
    // { ::onc::wifi::kSSID, shill::kWifiHexSsid},
    // This field is converted during translation, see onc_translator_*.
    // { ::onc::wifi::kSecurity, shill::kSecurityProperty },
    { ::onc::wifi::kSignalStrength, shill::kSignalStrengthProperty},
    {NULL}};

const FieldTranslationEntry cellular_apn_fields[] = {
    { ::onc::cellular_apn::kName, shill::kApnProperty},
    { ::onc::cellular_apn::kUsername, shill::kApnUsernameProperty},
    { ::onc::cellular_apn::kPassword, shill::kApnPasswordProperty},
    {NULL}};

const FieldTranslationEntry cellular_provider_fields[] = {
    { ::onc::cellular_provider::kCode, shill::kOperatorCodeKey},
    { ::onc::cellular_provider::kCountry, shill::kOperatorCountryKey},
    { ::onc::cellular_provider::kName, shill::kOperatorNameKey},
    {NULL}};

const FieldTranslationEntry cellular_fields[] = {
    { ::onc::cellular::kActivateOverNonCellularNetwork,
      shill::kActivateOverNonCellularNetworkProperty},
    { ::onc::cellular::kActivationState, shill::kActivationStateProperty},
    { ::onc::cellular::kAllowRoaming, shill::kCellularAllowRoamingProperty},
    { ::onc::cellular::kCarrier, shill::kCarrierProperty},
    { ::onc::cellular::kESN, shill::kEsnProperty},
    { ::onc::cellular::kFamily, shill::kTechnologyFamilyProperty},
    { ::onc::cellular::kFirmwareRevision, shill::kFirmwareRevisionProperty},
    { ::onc::cellular::kFoundNetworks, shill::kFoundNetworksProperty},
    { ::onc::cellular::kHardwareRevision, shill::kHardwareRevisionProperty},
    { ::onc::cellular::kICCID, shill::kIccidProperty},
    { ::onc::cellular::kIMEI, shill::kImeiProperty},
    { ::onc::cellular::kIMSI, shill::kImsiProperty},
    { ::onc::cellular::kManufacturer, shill::kManufacturerProperty},
    { ::onc::cellular::kMDN, shill::kMdnProperty},
    { ::onc::cellular::kMEID, shill::kMeidProperty},
    { ::onc::cellular::kMIN, shill::kMinProperty},
    { ::onc::cellular::kModelID, shill::kModelIDProperty},
    { ::onc::cellular::kNetworkTechnology, shill::kNetworkTechnologyProperty},
    { ::onc::cellular::kPRLVersion, shill::kPRLVersionProperty},
    { ::onc::cellular::kProviderRequiresRoaming,
      shill::kProviderRequiresRoamingProperty},
    { ::onc::cellular::kRoamingState, shill::kRoamingStateProperty},
    { ::onc::cellular::kSelectedNetwork, shill::kSelectedNetworkProperty},
    { ::onc::cellular::kSIMLockStatus, shill::kSIMLockStatusProperty},
    { ::onc::cellular::kSIMPresent, shill::kSIMPresentProperty},
    { ::onc::cellular::kSupportedCarriers, shill::kSupportedCarriersProperty},
    { ::onc::cellular::kSupportNetworkScan, shill::kSupportNetworkScanProperty},
    {NULL}};

const FieldTranslationEntry network_fields[] = {
    // Shill doesn't allow setting the name for non-VPN networks.
    // This field is conditionally translated, see onc_translator_*.
    // { ::onc::network_config::kName, shill::kNameProperty },
    { ::onc::network_config::kGUID, shill::kGuidProperty},
    // This field is converted during translation, see onc_translator_*.
    // { ::onc::network_config::kType, shill::kTypeProperty },

    // This field is converted during translation, see
    // onc_translator_shill_to_onc.cc. It is only converted when going from
    // Shill->ONC, and ignored otherwise.
    // { ::onc::network_config::kConnectionState, shill::kStateProperty },
    {NULL}};

struct OncValueTranslationEntry {
  const OncValueSignature* onc_signature;
  const FieldTranslationEntry* field_translation_table;
};

const OncValueTranslationEntry onc_value_translation_table[] = {
  { &kEAPSignature, eap_fields },
  { &kIPsecSignature, ipsec_fields },
  { &kL2TPSignature, l2tp_fields },
  { &kOpenVPNSignature, openvpn_fields },
  { &kVPNSignature, vpn_fields },
  { &kWiFiSignature, wifi_fields },
  { &kWiFiWithStateSignature, wifi_fields },
  { &kCellularApnSignature, cellular_apn_fields },
  { &kCellularProviderSignature, cellular_provider_fields },
  { &kCellularSignature, cellular_fields },
  { &kCellularWithStateSignature, cellular_fields },
  { &kNetworkWithStateSignature, network_fields },
  { &kNetworkConfigurationSignature, network_fields },
  { NULL }
};

struct NestedShillDictionaryEntry {
  const OncValueSignature* onc_signature;
  // NULL terminated list of Shill property keys.
  const char* const* shill_property_path;
};

const char* cellular_apn_property_path_entries[] = {
  shill::kCellularApnProperty,
  NULL
};

const NestedShillDictionaryEntry nested_shill_dictionaries[] = {
  { &kCellularApnSignature, cellular_apn_property_path_entries },
  { NULL }
};

}  // namespace

const StringTranslationEntry kNetworkTypeTable[] = {
    // This mapping is ensured in the translation code.
    //  { network_type::kEthernet, shill::kTypeEthernet },
    //  { network_type::kEthernet, shill::kTypeEthernetEap },
    { ::onc::network_type::kWiFi, shill::kTypeWifi},
    { ::onc::network_type::kCellular, shill::kTypeCellular},
    { ::onc::network_type::kVPN, shill::kTypeVPN},
    {NULL}};

const StringTranslationEntry kVPNTypeTable[] = {
    { ::onc::vpn::kTypeL2TP_IPsec, shill::kProviderL2tpIpsec},
    { ::onc::vpn::kOpenVPN, shill::kProviderOpenVpn}, {NULL}};

// The first matching line is chosen.
const StringTranslationEntry kWiFiSecurityTable[] = {
    { ::onc::wifi::kNone, shill::kSecurityNone},
    { ::onc::wifi::kWEP_PSK, shill::kSecurityWep},
    { ::onc::wifi::kWPA_PSK, shill::kSecurityPsk},
    { ::onc::wifi::kWPA_EAP, shill::kSecurity8021x},
    { ::onc::wifi::kWPA_PSK, shill::kSecurityRsn},
    { ::onc::wifi::kWPA_PSK, shill::kSecurityWpa},
    {NULL}};

const StringTranslationEntry kEAPOuterTable[] = {
    { ::onc::eap::kPEAP, shill::kEapMethodPEAP},
    { ::onc::eap::kEAP_TLS, shill::kEapMethodTLS},
    { ::onc::eap::kEAP_TTLS, shill::kEapMethodTTLS},
    { ::onc::eap::kLEAP, shill::kEapMethodLEAP},
    {NULL}};

// Translation of the EAP.Inner field in case of EAP.Outer == PEAP
const StringTranslationEntry kEAP_PEAP_InnerTable[] = {
    { ::onc::eap::kMD5, shill::kEapPhase2AuthPEAPMD5},
    { ::onc::eap::kMSCHAPv2, shill::kEapPhase2AuthPEAPMSCHAPV2}, {NULL}};

// Translation of the EAP.Inner field in case of EAP.Outer == TTLS
const StringTranslationEntry kEAP_TTLS_InnerTable[] = {
    { ::onc::eap::kMD5, shill::kEapPhase2AuthTTLSMD5},
    { ::onc::eap::kMSCHAPv2, shill::kEapPhase2AuthTTLSMSCHAPV2},
    { ::onc::eap::kPAP, shill::kEapPhase2AuthTTLSPAP},
    {NULL}};

const FieldTranslationEntry* GetFieldTranslationTable(
    const OncValueSignature& onc_signature) {
  for (const OncValueTranslationEntry* it = onc_value_translation_table;
       it->onc_signature != NULL; ++it) {
    if (it->onc_signature == &onc_signature)
      return it->field_translation_table;
  }
  return NULL;
}

std::vector<std::string> GetPathToNestedShillDictionary(
    const OncValueSignature& onc_signature) {
  std::vector<std::string> shill_property_path;
  for (const NestedShillDictionaryEntry* it = nested_shill_dictionaries;
       it->onc_signature != NULL; ++it) {
    if (it->onc_signature == &onc_signature) {
      for (const char* const* key = it->shill_property_path; *key != NULL;
           ++key) {
        shill_property_path.push_back(std::string(*key));
      }
      break;
    }
  }
  return shill_property_path;
}

bool GetShillPropertyName(const std::string& onc_field_name,
                          const FieldTranslationEntry table[],
                          std::string* shill_property_name) {
  for (const FieldTranslationEntry* it = table;
       it->onc_field_name != NULL; ++it) {
    if (it->onc_field_name != onc_field_name)
      continue;
    *shill_property_name = it->shill_property_name;
    return true;
  }
  return false;
}

bool TranslateStringToShill(const StringTranslationEntry table[],
                            const std::string& onc_value,
                            std::string* shill_value) {
  for (int i = 0; table[i].onc_value != NULL; ++i) {
    if (onc_value != table[i].onc_value)
      continue;
    *shill_value = table[i].shill_value;
    return true;
  }
  LOG(ERROR) << "Value '" << onc_value << "' cannot be translated to Shill";
  return false;
}

bool TranslateStringToONC(const StringTranslationEntry table[],
                          const std::string& shill_value,
                          std::string* onc_value) {
  for (int i = 0; table[i].shill_value != NULL; ++i) {
    if (shill_value != table[i].shill_value)
      continue;
    *onc_value = table[i].onc_value;
    return true;
  }
  LOG(ERROR) << "Value '" << shill_value << "' cannot be translated to ONC";
  return false;
}

}  // namespace onc
}  // namespace chromeos
