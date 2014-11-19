// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/file_manager/volume_manager.h"

#include <string>
#include <vector>

#include "base/basictypes.h"
#include "base/prefs/pref_service.h"
#include "chrome/browser/chromeos/file_manager/fake_disk_mount_manager.h"
#include "chrome/browser/chromeos/file_manager/volume_manager_observer.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/testing_profile.h"
#include "chromeos/dbus/fake_power_manager_client.h"
#include "chromeos/disks/disk_mount_manager.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace file_manager {
namespace {

class LoggingObserver : public VolumeManagerObserver {
 public:
  struct Event {
    enum EventType {
      DISK_ADDED,
      DISK_REMOVED,
      DEVICE_ADDED,
      DEVICE_REMOVED,
      VOLUME_MOUNTED,
      VOLUME_UNMOUNTED,
      FORMAT_STARTED,
      FORMAT_COMPLETED,
    } type;

    // Available on DEVICE_ADDED, DEVICE_REMOVED, VOLUME_MOUNTED,
    // VOLUME_UNMOUNTED, FORMAT_STARTED and FORMAT_COMPLETED.
    std::string device_path;

    // Available on DISK_ADDED.
    bool mounting;

    // Available on VOLUME_MOUNTED and VOLUME_UNMOUNTED.
    chromeos::MountError mount_error;

    // Available on VOLUME_MOUNTED.
    bool is_remounting;

    // Available on FORMAT_STARTED and FORMAT_COMPLETED.
    bool success;
  };

  LoggingObserver() {}
  virtual ~LoggingObserver() {}

  const std::vector<Event>& events() const { return events_; }

  // VolumeManagerObserver overrides.
  virtual void OnDiskAdded(const chromeos::disks::DiskMountManager::Disk& disk,
                           bool mounting) OVERRIDE {
    Event event;
    event.type = Event::DISK_ADDED;
    event.device_path = disk.device_path();  // Keep only device_path.
    event.mounting = mounting;
    events_.push_back(event);
  }

  virtual void OnDiskRemoved(
      const chromeos::disks::DiskMountManager::Disk& disk) OVERRIDE {
    Event event;
    event.type = Event::DISK_REMOVED;
    event.device_path = disk.device_path();  // Keep only device_path.
    events_.push_back(event);
  }

  virtual void OnDeviceAdded(const std::string& device_path) OVERRIDE {
    Event event;
    event.type = Event::DEVICE_ADDED;
    event.device_path = device_path;
    events_.push_back(event);
  }

  virtual void OnDeviceRemoved(const std::string& device_path) OVERRIDE {
    Event event;
    event.type = Event::DEVICE_REMOVED;
    event.device_path = device_path;
    events_.push_back(event);
  }

  virtual void OnVolumeMounted(chromeos::MountError error_code,
                               const VolumeInfo& volume_info,
                               bool is_remounting) OVERRIDE {
    Event event;
    event.type = Event::VOLUME_MOUNTED;
    event.device_path = volume_info.source_path.AsUTF8Unsafe();
    event.mount_error = error_code;
    event.is_remounting = is_remounting;
    events_.push_back(event);
  }

  virtual void OnVolumeUnmounted(chromeos::MountError error_code,
                                 const VolumeInfo& volume_info) OVERRIDE {
    Event event;
    event.type = Event::VOLUME_UNMOUNTED;
    event.device_path = volume_info.source_path.AsUTF8Unsafe();
    event.mount_error = error_code;
    events_.push_back(event);
  }

  virtual void OnFormatStarted(
      const std::string& device_path, bool success) OVERRIDE {
    Event event;
    event.type = Event::FORMAT_STARTED;
    event.device_path = device_path;
    event.success = success;
    events_.push_back(event);
  }

  virtual void OnFormatCompleted(
      const std::string& device_path, bool success) OVERRIDE {
    Event event;
    event.type = Event::FORMAT_COMPLETED;
    event.device_path = device_path;
    event.success = success;
    events_.push_back(event);
  }

 private:
  std::vector<Event> events_;

  DISALLOW_COPY_AND_ASSIGN(LoggingObserver);
};

}  // namespace

class VolumeManagerTest : public testing::Test {
 protected:
  VolumeManagerTest() {
  }
  virtual ~VolumeManagerTest() {
  }

  virtual void SetUp() OVERRIDE {
    power_manager_client_.reset(new chromeos::FakePowerManagerClient);
    disk_mount_manager_.reset(new FakeDiskMountManager);
    profile_.reset(new TestingProfile);
    volume_manager_.reset(new VolumeManager(profile_.get(),
                                            NULL,  // DriveIntegrationService
                                            power_manager_client_.get(),
                                            disk_mount_manager_.get()));
  }

  content::TestBrowserThreadBundle thread_bundle_;
  scoped_ptr<chromeos::FakePowerManagerClient> power_manager_client_;
  scoped_ptr<FakeDiskMountManager> disk_mount_manager_;
  scoped_ptr<TestingProfile> profile_;
  scoped_ptr<VolumeManager> volume_manager_;
};

TEST_F(VolumeManagerTest, OnFileSystemMounted) {
  LoggingObserver observer;
  volume_manager_->AddObserver(&observer);

  volume_manager_->OnFileSystemMounted();

  ASSERT_EQ(1U, observer.events().size());
  const LoggingObserver::Event& event = observer.events()[0];
  EXPECT_EQ(LoggingObserver::Event::VOLUME_MOUNTED, event.type);
  EXPECT_EQ(drive::util::GetDriveMountPointPath().AsUTF8Unsafe(),
            event.device_path);
  EXPECT_EQ(chromeos::MOUNT_ERROR_NONE, event.mount_error);
  EXPECT_FALSE(event.is_remounting);

  volume_manager_->RemoveObserver(&observer);
}

TEST_F(VolumeManagerTest, OnFileSystemBeingUnmounted) {
  LoggingObserver observer;
  volume_manager_->AddObserver(&observer);

  volume_manager_->OnFileSystemBeingUnmounted();

  ASSERT_EQ(1U, observer.events().size());
  const LoggingObserver::Event& event = observer.events()[0];
  EXPECT_EQ(LoggingObserver::Event::VOLUME_UNMOUNTED, event.type);
  EXPECT_EQ(drive::util::GetDriveMountPointPath().AsUTF8Unsafe(),
            event.device_path);
  EXPECT_EQ(chromeos::MOUNT_ERROR_NONE, event.mount_error);

  volume_manager_->RemoveObserver(&observer);
}

TEST_F(VolumeManagerTest, OnDiskEvent_Hidden) {
  LoggingObserver observer;
  volume_manager_->AddObserver(&observer);

  const bool kIsHidden = true;
  const chromeos::disks::DiskMountManager::Disk kDisk(
      "device1", "", "", "", "", "", "", "", "", "", "", "",
      chromeos::DEVICE_TYPE_UNKNOWN, 0, false, false, false, false, kIsHidden);

  volume_manager_->OnDiskEvent(
      chromeos::disks::DiskMountManager::DISK_ADDED, &kDisk);
  EXPECT_EQ(0U, observer.events().size());

  volume_manager_->OnDiskEvent(
      chromeos::disks::DiskMountManager::DISK_REMOVED, &kDisk);
  EXPECT_EQ(0U, observer.events().size());

  volume_manager_->OnDiskEvent(
      chromeos::disks::DiskMountManager::DISK_CHANGED, &kDisk);
  EXPECT_EQ(0U, observer.events().size());

  volume_manager_->RemoveObserver(&observer);
}

TEST_F(VolumeManagerTest, OnDiskEvent_Added) {
  // Enable external storage.
  profile_->GetPrefs()->SetBoolean(prefs::kExternalStorageDisabled, false);

  LoggingObserver observer;
  volume_manager_->AddObserver(&observer);

  const chromeos::disks::DiskMountManager::Disk kEmptyDevicePathDisk(
      "",  // empty device path.
      "", "", "", "", "", "", "", "", "", "", "",
      chromeos::DEVICE_TYPE_UNKNOWN, 0, false, false, false, false, false);
  volume_manager_->OnDiskEvent(
      chromeos::disks::DiskMountManager::DISK_ADDED, &kEmptyDevicePathDisk);
  EXPECT_EQ(0U, observer.events().size());

  const bool kHasMedia = true;
  const chromeos::disks::DiskMountManager::Disk kMediaDisk(
      "device1", "", "", "", "", "", "", "", "", "", "", "",
      chromeos::DEVICE_TYPE_UNKNOWN, 0, false, false, kHasMedia, false, false);
  volume_manager_->OnDiskEvent(
      chromeos::disks::DiskMountManager::DISK_ADDED, &kMediaDisk);
  ASSERT_EQ(1U, observer.events().size());
  const LoggingObserver::Event& event = observer.events()[0];
  EXPECT_EQ(LoggingObserver::Event::DISK_ADDED, event.type);
  EXPECT_EQ("device1", event.device_path);
  EXPECT_TRUE(event.mounting);

  ASSERT_EQ(1U, disk_mount_manager_->mount_requests().size());
  const FakeDiskMountManager::MountRequest& mount_request =
      disk_mount_manager_->mount_requests()[0];
  EXPECT_EQ("device1", mount_request.source_path);
  EXPECT_EQ("", mount_request.source_format);
  EXPECT_EQ("", mount_request.mount_label);
  EXPECT_EQ(chromeos::MOUNT_TYPE_DEVICE, mount_request.type);

  volume_manager_->RemoveObserver(&observer);
}

TEST_F(VolumeManagerTest, OnDiskEvent_AddedNonMounting) {
  // Enable external storage.
  profile_->GetPrefs()->SetBoolean(prefs::kExternalStorageDisabled, false);

  // Device which is already mounted.
  {
    LoggingObserver observer;
    volume_manager_->AddObserver(&observer);

    const bool kHasMedia = true;
    const chromeos::disks::DiskMountManager::Disk kMountedMediaDisk(
        "device1", "mounted", "", "", "", "", "", "", "", "", "", "",
        chromeos::DEVICE_TYPE_UNKNOWN, 0, false, false,
        kHasMedia, false, false);
    volume_manager_->OnDiskEvent(
        chromeos::disks::DiskMountManager::DISK_ADDED, &kMountedMediaDisk);
    ASSERT_EQ(1U, observer.events().size());
    const LoggingObserver::Event& event = observer.events()[0];
    EXPECT_EQ(LoggingObserver::Event::DISK_ADDED, event.type);
    EXPECT_EQ("device1", event.device_path);
    EXPECT_FALSE(event.mounting);

    ASSERT_EQ(0U, disk_mount_manager_->mount_requests().size());

    volume_manager_->RemoveObserver(&observer);
  }

  // Device without media.
  {
    LoggingObserver observer;
    volume_manager_->AddObserver(&observer);

    const bool kWithoutMedia = false;
    const chromeos::disks::DiskMountManager::Disk kNoMediaDisk(
        "device1", "", "", "", "", "", "", "", "", "", "", "",
        chromeos::DEVICE_TYPE_UNKNOWN, 0, false, false,
        kWithoutMedia, false, false);
    volume_manager_->OnDiskEvent(
        chromeos::disks::DiskMountManager::DISK_ADDED, &kNoMediaDisk);
    ASSERT_EQ(1U, observer.events().size());
    const LoggingObserver::Event& event = observer.events()[0];
    EXPECT_EQ(LoggingObserver::Event::DISK_ADDED, event.type);
    EXPECT_EQ("device1", event.device_path);
    EXPECT_FALSE(event.mounting);

    ASSERT_EQ(0U, disk_mount_manager_->mount_requests().size());

    volume_manager_->RemoveObserver(&observer);
  }

  // External storage is disabled.
  {
    profile_->GetPrefs()->SetBoolean(prefs::kExternalStorageDisabled, true);

    LoggingObserver observer;
    volume_manager_->AddObserver(&observer);

    const bool kHasMedia = true;
    const chromeos::disks::DiskMountManager::Disk kMediaDisk(
        "device1", "", "", "", "", "", "", "", "", "", "", "",
        chromeos::DEVICE_TYPE_UNKNOWN, 0, false, false,
        kHasMedia, false, false);
    volume_manager_->OnDiskEvent(
        chromeos::disks::DiskMountManager::DISK_ADDED, &kMediaDisk);
    ASSERT_EQ(1U, observer.events().size());
    const LoggingObserver::Event& event = observer.events()[0];
    EXPECT_EQ(LoggingObserver::Event::DISK_ADDED, event.type);
    EXPECT_EQ("device1", event.device_path);
    EXPECT_FALSE(event.mounting);

    ASSERT_EQ(0U, disk_mount_manager_->mount_requests().size());

    volume_manager_->RemoveObserver(&observer);
  }
}

TEST_F(VolumeManagerTest, OnDiskEvent_Removed) {
  LoggingObserver observer;
  volume_manager_->AddObserver(&observer);

  const chromeos::disks::DiskMountManager::Disk kMountedDisk(
      "device1", "mount_path", "", "", "", "", "", "", "", "", "", "",
      chromeos::DEVICE_TYPE_UNKNOWN, 0, false, false, false, false, false);
  volume_manager_->OnDiskEvent(
      chromeos::disks::DiskMountManager::DISK_REMOVED, &kMountedDisk);

  ASSERT_EQ(1U, observer.events().size());
  const LoggingObserver::Event& event = observer.events()[0];
  EXPECT_EQ(LoggingObserver::Event::DISK_REMOVED, event.type);
  EXPECT_EQ("device1", event.device_path);

  ASSERT_EQ(1U, disk_mount_manager_->unmount_requests().size());
  const FakeDiskMountManager::UnmountRequest& unmount_request =
      disk_mount_manager_->unmount_requests()[0];
  EXPECT_EQ("mount_path", unmount_request.mount_path);
  EXPECT_EQ(chromeos::UNMOUNT_OPTIONS_LAZY, unmount_request.options);

  volume_manager_->RemoveObserver(&observer);
}

TEST_F(VolumeManagerTest, OnDiskEvent_RemovedNotMounted) {
  LoggingObserver observer;
  volume_manager_->AddObserver(&observer);

  const chromeos::disks::DiskMountManager::Disk kNotMountedDisk(
      "device1", "", "", "", "", "", "", "", "", "", "", "",
      chromeos::DEVICE_TYPE_UNKNOWN, 0, false, false, false, false, false);
  volume_manager_->OnDiskEvent(
      chromeos::disks::DiskMountManager::DISK_REMOVED, &kNotMountedDisk);

  ASSERT_EQ(1U, observer.events().size());
  const LoggingObserver::Event& event = observer.events()[0];
  EXPECT_EQ(LoggingObserver::Event::DISK_REMOVED, event.type);
  EXPECT_EQ("device1", event.device_path);

  ASSERT_EQ(0U, disk_mount_manager_->unmount_requests().size());

  volume_manager_->RemoveObserver(&observer);
}

TEST_F(VolumeManagerTest, OnDiskEvent_Changed) {
  // Changed event is just ignored.
  LoggingObserver observer;
  volume_manager_->AddObserver(&observer);

  const chromeos::disks::DiskMountManager::Disk kDisk(
      "device1", "", "", "", "", "", "", "", "", "", "", "",
      chromeos::DEVICE_TYPE_UNKNOWN, 0, false, false, false, false, false);
  volume_manager_->OnDiskEvent(
      chromeos::disks::DiskMountManager::DISK_CHANGED, &kDisk);

  EXPECT_EQ(0U, observer.events().size());
  EXPECT_EQ(0U, disk_mount_manager_->mount_requests().size());
  EXPECT_EQ(0U, disk_mount_manager_->unmount_requests().size());

  volume_manager_->RemoveObserver(&observer);
}

TEST_F(VolumeManagerTest, OnDeviceEvent_Added) {
  LoggingObserver observer;
  volume_manager_->AddObserver(&observer);

  volume_manager_->OnDeviceEvent(
      chromeos::disks::DiskMountManager::DEVICE_ADDED, "device1");

  ASSERT_EQ(1U, observer.events().size());
  const LoggingObserver::Event& event = observer.events()[0];
  EXPECT_EQ(LoggingObserver::Event::DEVICE_ADDED, event.type);
  EXPECT_EQ("device1", event.device_path);

  volume_manager_->RemoveObserver(&observer);
}

TEST_F(VolumeManagerTest, OnDeviceEvent_Removed) {
  LoggingObserver observer;
  volume_manager_->AddObserver(&observer);

  volume_manager_->OnDeviceEvent(
      chromeos::disks::DiskMountManager::DEVICE_REMOVED, "device1");

  ASSERT_EQ(1U, observer.events().size());
  const LoggingObserver::Event& event = observer.events()[0];
  EXPECT_EQ(LoggingObserver::Event::DEVICE_REMOVED, event.type);
  EXPECT_EQ("device1", event.device_path);

  volume_manager_->RemoveObserver(&observer);
}

TEST_F(VolumeManagerTest, OnDeviceEvent_Scanned) {
  LoggingObserver observer;
  volume_manager_->AddObserver(&observer);

  volume_manager_->OnDeviceEvent(
      chromeos::disks::DiskMountManager::DEVICE_SCANNED, "device1");

  // SCANNED event is just ignored.
  EXPECT_EQ(0U, observer.events().size());

  volume_manager_->RemoveObserver(&observer);
}

TEST_F(VolumeManagerTest, OnMountEvent_Mounting) {
  LoggingObserver observer;
  volume_manager_->AddObserver(&observer);

  const chromeos::disks::DiskMountManager::MountPointInfo kMountPoint(
      "device1",
      "mount1",
      chromeos::MOUNT_TYPE_DEVICE,
      chromeos::disks::MOUNT_CONDITION_NONE);

  volume_manager_->OnMountEvent(chromeos::disks::DiskMountManager::MOUNTING,
                                chromeos::MOUNT_ERROR_NONE,
                                kMountPoint);

  ASSERT_EQ(1U, observer.events().size());
  const LoggingObserver::Event& event = observer.events()[0];
  EXPECT_EQ(LoggingObserver::Event::VOLUME_MOUNTED, event.type);
  EXPECT_EQ("device1", event.device_path);
  EXPECT_EQ(chromeos::MOUNT_ERROR_NONE, event.mount_error);
  EXPECT_FALSE(event.is_remounting);

  volume_manager_->RemoveObserver(&observer);
}

TEST_F(VolumeManagerTest, OnMountEvent_Remounting) {
  scoped_ptr<chromeos::disks::DiskMountManager::Disk> disk(
      new chromeos::disks::DiskMountManager::Disk(
          "device1", "", "", "", "", "", "", "", "", "", "uuid1", "",
          chromeos::DEVICE_TYPE_UNKNOWN, 0,
          false, false, false, false, false));
  disk_mount_manager_->AddDiskForTest(disk.release());
  disk_mount_manager_->MountPath(
      "device1", "", "", chromeos::MOUNT_TYPE_DEVICE);

  // Emulate system suspend and then resume.
  {
    power_manager_client_->SendSuspendImminent();
    power_manager::SuspendState state;
    state.set_type(power_manager::SuspendState_Type_SUSPEND_TO_MEMORY);
    state.set_wall_time(0);
    power_manager_client_->SendSuspendStateChanged(state);
    state.set_type(power_manager::SuspendState_Type_RESUME);
    power_manager_client_->SendSuspendStateChanged(state);

    // After resume, the device is unmounted and then mounted.
    disk_mount_manager_->UnmountPath(
        "device1", chromeos::UNMOUNT_OPTIONS_NONE,
        chromeos::disks::DiskMountManager::UnmountPathCallback());
    disk_mount_manager_->MountPath(
        "device1", "", "", chromeos::MOUNT_TYPE_DEVICE);
  }

  LoggingObserver observer;
  volume_manager_->AddObserver(&observer);

  const chromeos::disks::DiskMountManager::MountPointInfo kMountPoint(
      "device1",
      "mount1",
      chromeos::MOUNT_TYPE_DEVICE,
      chromeos::disks::MOUNT_CONDITION_NONE);

  volume_manager_->OnMountEvent(chromeos::disks::DiskMountManager::MOUNTING,
                                chromeos::MOUNT_ERROR_NONE,
                                kMountPoint);

  ASSERT_EQ(1U, observer.events().size());
  const LoggingObserver::Event& event = observer.events()[0];
  EXPECT_EQ(LoggingObserver::Event::VOLUME_MOUNTED, event.type);
  EXPECT_EQ("device1", event.device_path);
  EXPECT_EQ(chromeos::MOUNT_ERROR_NONE, event.mount_error);
  EXPECT_TRUE(event.is_remounting);

  volume_manager_->RemoveObserver(&observer);
}

TEST_F(VolumeManagerTest, OnMountEvent_Unmounting) {
  LoggingObserver observer;
  volume_manager_->AddObserver(&observer);

  const chromeos::disks::DiskMountManager::MountPointInfo kMountPoint(
      "device1",
      "mount1",
      chromeos::MOUNT_TYPE_DEVICE,
      chromeos::disks::MOUNT_CONDITION_NONE);

  volume_manager_->OnMountEvent(chromeos::disks::DiskMountManager::UNMOUNTING,
                                chromeos::MOUNT_ERROR_NONE,
                                kMountPoint);

  ASSERT_EQ(1U, observer.events().size());
  const LoggingObserver::Event& event = observer.events()[0];
  EXPECT_EQ(LoggingObserver::Event::VOLUME_UNMOUNTED, event.type);
  EXPECT_EQ("device1", event.device_path);
  EXPECT_EQ(chromeos::MOUNT_ERROR_NONE, event.mount_error);

  volume_manager_->RemoveObserver(&observer);
}

TEST_F(VolumeManagerTest, OnFormatEvent_Started) {
  LoggingObserver observer;
  volume_manager_->AddObserver(&observer);

  volume_manager_->OnFormatEvent(
      chromeos::disks::DiskMountManager::FORMAT_STARTED,
      chromeos::FORMAT_ERROR_NONE,
      "device1");

  ASSERT_EQ(1U, observer.events().size());
  const LoggingObserver::Event& event = observer.events()[0];
  EXPECT_EQ(LoggingObserver::Event::FORMAT_STARTED, event.type);
  EXPECT_EQ("device1", event.device_path);
  EXPECT_TRUE(event.success);

  volume_manager_->RemoveObserver(&observer);
}

TEST_F(VolumeManagerTest, OnFormatEvent_StartFailed) {
  LoggingObserver observer;
  volume_manager_->AddObserver(&observer);

  volume_manager_->OnFormatEvent(
      chromeos::disks::DiskMountManager::FORMAT_STARTED,
      chromeos::FORMAT_ERROR_UNKNOWN,
      "device1");

  ASSERT_EQ(1U, observer.events().size());
  const LoggingObserver::Event& event = observer.events()[0];
  EXPECT_EQ(LoggingObserver::Event::FORMAT_STARTED, event.type);
  EXPECT_EQ("device1", event.device_path);
  EXPECT_FALSE(event.success);

  volume_manager_->RemoveObserver(&observer);
}

TEST_F(VolumeManagerTest, OnFormatEvent_Completed) {
  LoggingObserver observer;
  volume_manager_->AddObserver(&observer);

  volume_manager_->OnFormatEvent(
      chromeos::disks::DiskMountManager::FORMAT_COMPLETED,
      chromeos::FORMAT_ERROR_NONE,
      "device1");

  ASSERT_EQ(1U, observer.events().size());
  const LoggingObserver::Event& event = observer.events()[0];
  EXPECT_EQ(LoggingObserver::Event::FORMAT_COMPLETED, event.type);
  EXPECT_EQ("device1", event.device_path);
  EXPECT_TRUE(event.success);

  // When "format" is successfully done, VolumeManager requests to mount it.
  ASSERT_EQ(1U, disk_mount_manager_->mount_requests().size());
  const FakeDiskMountManager::MountRequest& mount_request =
      disk_mount_manager_->mount_requests()[0];
  EXPECT_EQ("device1", mount_request.source_path);
  EXPECT_EQ("", mount_request.source_format);
  EXPECT_EQ("", mount_request.mount_label);
  EXPECT_EQ(chromeos::MOUNT_TYPE_DEVICE, mount_request.type);

  volume_manager_->RemoveObserver(&observer);
}

TEST_F(VolumeManagerTest, OnFormatEvent_CompletedFailed) {
  LoggingObserver observer;
  volume_manager_->AddObserver(&observer);

  volume_manager_->OnFormatEvent(
      chromeos::disks::DiskMountManager::FORMAT_COMPLETED,
      chromeos::FORMAT_ERROR_UNKNOWN,
      "device1");

  ASSERT_EQ(1U, observer.events().size());
  const LoggingObserver::Event& event = observer.events()[0];
  EXPECT_EQ(LoggingObserver::Event::FORMAT_COMPLETED, event.type);
  EXPECT_EQ("device1", event.device_path);
  EXPECT_FALSE(event.success);

  EXPECT_EQ(0U, disk_mount_manager_->mount_requests().size());

  volume_manager_->RemoveObserver(&observer);
}

TEST_F(VolumeManagerTest, OnExternalStorageDisabledChanged) {
  // Here create two mount points.
  disk_mount_manager_->MountPath(
      "mount1", "", "", chromeos::MOUNT_TYPE_DEVICE);
  disk_mount_manager_->MountPath(
      "mount2", "", "", chromeos::MOUNT_TYPE_DEVICE);

  // Initially, there are two mount points.
  ASSERT_EQ(2U, disk_mount_manager_->mount_points().size());
  ASSERT_EQ(0U, disk_mount_manager_->unmount_requests().size());

  // Emulate to set kExternalStorageDisabled to false.
  profile_->GetPrefs()->SetBoolean(prefs::kExternalStorageDisabled, false);
  volume_manager_->OnExternalStorageDisabledChanged();

  // Expect no effects.
  EXPECT_EQ(2U, disk_mount_manager_->mount_points().size());
  EXPECT_EQ(0U, disk_mount_manager_->unmount_requests().size());

  // Emulate to set kExternalStorageDisabled to true.
  profile_->GetPrefs()->SetBoolean(prefs::kExternalStorageDisabled, true);
  volume_manager_->OnExternalStorageDisabledChanged();

  // The all mount points should be unmounted.
  EXPECT_EQ(0U, disk_mount_manager_->mount_points().size());

  EXPECT_EQ(2U, disk_mount_manager_->unmount_requests().size());
  const FakeDiskMountManager::UnmountRequest& unmount_request1 =
      disk_mount_manager_->unmount_requests()[0];
  EXPECT_EQ("mount1", unmount_request1.mount_path);

  const FakeDiskMountManager::UnmountRequest& unmount_request2 =
      disk_mount_manager_->unmount_requests()[1];
  EXPECT_EQ("mount2", unmount_request2.mount_path);
}

}  // namespace file_manager
