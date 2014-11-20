// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_BROWSER_INFO_MAP_H_
#define EXTENSIONS_BROWSER_INFO_MAP_H_

#include <string>

#include "base/basictypes.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/time/time.h"
#include "chrome/browser/extensions/process_map.h"
#include "chrome/common/extensions/extension_set.h"
#include "extensions/browser/quota_service.h"

namespace extensions {
class Extension;

// Contains extension data that needs to be accessed on the IO thread. It can
// be created/destroyed on any thread, but all other methods must be called on
// the IO thread.
class InfoMap : public base::RefCountedThreadSafe<InfoMap> {
 public:
  InfoMap();

  const ExtensionSet& extensions() const { return extensions_; }
  const ExtensionSet& disabled_extensions() const {
    return disabled_extensions_;
  }

  const extensions::ProcessMap& process_map() const;

  // Callback for when new extensions are loaded.
  void AddExtension(const extensions::Extension* extension,
                    base::Time install_time,
                    bool incognito_enabled);

  // Callback for when an extension is unloaded.
  void RemoveExtension(const std::string& extension_id,
                       const extensions::UnloadedExtensionInfo::Reason reason);

  // Returns the time the extension was installed, or base::Time() if not found.
  base::Time GetInstallTime(const std::string& extension_id) const;

  // Returns true if the user has allowed this extension to run in incognito
  // mode.
  bool IsIncognitoEnabled(const std::string& extension_id) const;

  // Returns true if the given extension can see events and data from another
  // sub-profile (incognito to original profile, or vice versa).
  bool CanCrossIncognito(const extensions::Extension* extension) const;

  // Adds an entry to process_map_.
  void RegisterExtensionProcess(const std::string& extension_id,
                                int process_id,
                                int site_instance_id);

  // Removes an entry from process_map_.
  void UnregisterExtensionProcess(const std::string& extension_id,
                                  int process_id,
                                  int site_instance_id);
  void UnregisterAllExtensionsInProcess(int process_id);

  // Returns the subset of extensions which has the same |origin| in
  // |process_id| with the specified |permission|.
  void GetExtensionsWithAPIPermissionForSecurityOrigin(
      const GURL& origin,
      int process_id,
      extensions::APIPermission::ID permission,
      ExtensionSet* extensions) const;

  // Returns true if there is exists an extension with the same origin as
  // |origin| in |process_id| with |permission|.
  bool SecurityOriginHasAPIPermission(const GURL& origin,
                                      int process_id,
                                      extensions::APIPermission::ID permission)
      const;

  QuotaService* GetQuotaService();

  // Keep track of the signin process, so we can restrict extension access to
  // it.
  void SetSigninProcess(int process_id);
  bool IsSigninProcess(int process_id) const;

 private:
  friend class base::RefCountedThreadSafe<InfoMap>;

  // Extra dynamic data related to an extension.
  struct ExtraData;
  // Map of extension_id to ExtraData.
  typedef std::map<std::string, ExtraData> ExtraDataMap;

  ~InfoMap();

  ExtensionSet extensions_;
  ExtensionSet disabled_extensions_;

  // Extra data associated with enabled extensions.
  ExtraDataMap extra_data_;

  // Used by dispatchers to limit API quota for individual extensions.
  // The QuotaService is not thread safe. We need to create and destroy it on
  // the IO thread.
  scoped_ptr<QuotaService> quota_service_;

  // Assignment of extensions to processes.
  extensions::ProcessMap process_map_;

  int signin_process_id_;
};

}  // namespace extensions

#endif  // EXTENSIONS_BROWSER_INFO_MAP_H_
