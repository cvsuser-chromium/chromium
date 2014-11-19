// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_SIGNED_IN_DEVICES_SIGNED_IN_DEVICES_MANAGER_H__
#define CHROME_BROWSER_EXTENSIONS_API_SIGNED_IN_DEVICES_SIGNED_IN_DEVICES_MANAGER_H__

#include <string>

#include "base/gtest_prod_util.h"
#include "base/memory/scoped_vector.h"
#include "chrome/browser/extensions/api/profile_keyed_api_factory.h"
#include "chrome/browser/extensions/event_router.h"
#include "chrome/browser/sync/glue/synced_device_tracker.h"

class Profile;
namespace content {
  class NotificationDetails;
  class NotificationObserver;
  class NotificationRegistrar;
}  // namespace content

namespace extensions {
class ProfileKeyedAPI;

struct EventListenerInfo;

// An object of this class is created for each extension that has registered
// to be notified for device info change. The objects listen for notification
// from sync on device info change. On receiving the notification the
// new list of devices is constructed and passed back to the extension.
// The extension id is part of this object as it is needed to fill in the
// public ids for devices(public ids for a device, is not the same for
// all extensions).
class SignedInDevicesChangeObserver
    : public browser_sync::SyncedDeviceTracker::Observer {
 public:
  SignedInDevicesChangeObserver(const std::string& extension_id,
                                Profile* profile);
  virtual ~SignedInDevicesChangeObserver();

  virtual void OnDeviceInfoChange() OVERRIDE;

  const std::string& extension_id() {
    return extension_id_;
  }

 private:
  std::string extension_id_;
  Profile* const profile_;
  content::NotificationRegistrar registrar_;
};

class SignedInDevicesManager
    : public ProfileKeyedAPI,
      public content::NotificationObserver,
      public EventRouter::Observer {
 public:
  // Default constructor used for testing.
  SignedInDevicesManager();
  explicit SignedInDevicesManager(Profile* profile);
  virtual ~SignedInDevicesManager();

  // ProfileKeyedAPI implementation.
  static ProfileKeyedAPIFactory<SignedInDevicesManager>* GetFactoryInstance();

  // NotificationObserver:
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  // EventRouter::Observer:
  virtual void OnListenerAdded(const EventListenerInfo& details) OVERRIDE;
  virtual void OnListenerRemoved(const EventListenerInfo& details) OVERRIDE;

 private:
  friend class ProfileKeyedAPIFactory<SignedInDevicesManager>;

  // ProfileKeyedAPI implementation.
  static const char* service_name() {
    return "SignedInDevicesManager";
  }
  static const bool kServiceHasOwnInstanceInIncognito = true;

  void RemoveChangeObserverForExtension(const std::string& extension_id);

  Profile* const profile_;
  content::NotificationRegistrar registrar_;
  ScopedVector<SignedInDevicesChangeObserver> change_observers_;

  FRIEND_TEST_ALL_PREFIXES(SignedInDevicesManager, UpdateListener);

  DISALLOW_COPY_AND_ASSIGN(SignedInDevicesManager);
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_API_SIGNED_IN_DEVICES_SIGNED_IN_DEVICES_MANAGER_H__

