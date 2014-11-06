// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/scoped_ptr.h"
#include "base/prefs/pref_service.h"
#include "base/prefs/testing_pref_store.h"
#include "chrome/browser/extensions/api/signed_in_devices/signed_in_devices_manager.h"
#include "chrome/browser/extensions/event_router.h"
#include "chrome/browser/sync/profile_sync_service_factory.h"
#include "chrome/common/extensions/api/signed_in_devices.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/testing_profile.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace extensions {

namespace {
BrowserContextKeyedService* CreateProfileSyncServiceMock(
    content::BrowserContext* profile) {
  return NULL;
}
}  // namespace

// Adds a listener and removes it.
TEST(SignedInDevicesManager, UpdateListener) {
  scoped_ptr<TestingProfile> profile(new TestingProfile());
  profile->GetPrefs()->SetString(prefs::kGoogleServicesUsername, "foo");
  ProfileSyncServiceFactory::GetInstance()->SetTestingFactory(
      profile.get(), CreateProfileSyncServiceMock);
  SignedInDevicesManager manager(profile.get());

  EventListenerInfo info(
      api::signed_in_devices::OnDeviceInfoChange::kEventName,
      "extension1");

  // Add a listener.
  manager.OnListenerAdded(info);
  EXPECT_EQ(manager.change_observers_.size(), 1U);
  EXPECT_EQ(manager.change_observers_[0]->extension_id(), info.extension_id);

  // Remove the listener.
  manager.OnListenerRemoved(info);
  EXPECT_TRUE(manager.change_observers_.empty());
}
}  // namespace extensions
