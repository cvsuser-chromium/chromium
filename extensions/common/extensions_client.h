// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_COMMON_EXTENSIONS_CLIENT_H_
#define EXTENSIONS_COMMON_EXTENSIONS_CLIENT_H_

#include <set>
#include <string>
#include <vector>

namespace extensions {

class APIPermissionSet;
class Extension;
class FeatureProvider;
class PermissionMessage;
class PermissionMessageProvider;
class PermissionsProvider;
class URLPatternSet;

// Sets up global state for the extensions system. Should be Set() once in each
// process. This should be implemented by the client of the extensions system.
class ExtensionsClient {
 public:
  typedef std::vector<std::string> ScriptingWhitelist;

  // Initializes global state. Not done in the constructor because unit tests
  // can create additional ExtensionsClients because the utility thread runs
  // in-process.
  virtual void Initialize() = 0;

  // Returns a PermissionsProvider to initialize the permissions system.
  virtual const PermissionsProvider& GetPermissionsProvider() const = 0;

  // Returns the global PermissionMessageProvider to use to provide permission
  // warning strings.
  virtual const PermissionMessageProvider& GetPermissionMessageProvider()
      const = 0;

  // Gets a feature provider for a specific feature type.
  virtual FeatureProvider* GetFeatureProviderByName(const std::string& name)
      const = 0;

  // Takes the list of all hosts and filters out those with special
  // permission strings. Adds the regular hosts to |new_hosts|,
  // and adds the special permission messages to |messages|.
  virtual void FilterHostPermissions(
      const URLPatternSet& hosts,
      URLPatternSet* new_hosts,
      std::set<PermissionMessage>* messages) const = 0;

  // Replaces the scripting whitelist with |whitelist|. Used in the renderer;
  // only used for testing in the browser process.
  virtual void SetScriptingWhitelist(const ScriptingWhitelist& whitelist) = 0;

  // Return the whitelist of extensions that can run content scripts on
  // any origin.
  virtual const ScriptingWhitelist& GetScriptingWhitelist() const = 0;

  // Get the set of chrome:// hosts that |extension| can run content scripts on.
  virtual URLPatternSet GetPermittedChromeSchemeHosts(
      const Extension* extension,
      const APIPermissionSet& api_permissions) const = 0;

  // Return the extensions client.
  static ExtensionsClient* Get();

  // Initialize the extensions system with this extensions client.
  static void Set(ExtensionsClient* client);
};

}  // namespace extensions

#endif  // EXTENSIONS_COMMON_EXTENSIONS_CLIENT_H_
