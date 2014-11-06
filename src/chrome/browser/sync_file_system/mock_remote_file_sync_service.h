// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SYNC_FILE_SYSTEM_MOCK_REMOTE_FILE_SYNC_SERVICE_H_
#define CHROME_BROWSER_SYNC_FILE_SYSTEM_MOCK_REMOTE_FILE_SYNC_SERVICE_H_

#include <string>

#include "base/memory/scoped_ptr.h"
#include "base/observer_list.h"
#include "base/values.h"
#include "chrome/browser/sync_file_system/file_status_observer.h"
#include "chrome/browser/sync_file_system/mock_local_change_processor.h"
#include "chrome/browser/sync_file_system/remote_change_processor.h"
#include "chrome/browser/sync_file_system/remote_file_sync_service.h"
#include "chrome/browser/sync_file_system/sync_callbacks.h"
#include "chrome/browser/sync_file_system/sync_direction.h"
#include "chrome/browser/sync_file_system/sync_file_metadata.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "url/gurl.h"

namespace sync_file_system {

class MockRemoteFileSyncService : public RemoteFileSyncService {
 public:
  MockRemoteFileSyncService();
  virtual ~MockRemoteFileSyncService();

  // RemoteFileSyncService overrides.
  MOCK_METHOD1(AddServiceObserver,
               void(RemoteFileSyncService::Observer* observer));
  MOCK_METHOD1(AddFileStatusObserver,
               void(FileStatusObserver* observer));
  MOCK_METHOD2(RegisterOrigin,
               void(const GURL& origin, const SyncStatusCallback& callback));
  MOCK_METHOD2(EnableOrigin,
               void(const GURL& origin, const SyncStatusCallback& callback));
  MOCK_METHOD2(DisableOrigin,
               void(const GURL& origin, const SyncStatusCallback& callback));
  MOCK_METHOD3(UninstallOrigin,
               void(const GURL& origin,
                    UninstallFlag flag,
                    const SyncStatusCallback& callback));
  MOCK_METHOD1(ProcessRemoteChange,
               void(const SyncFileCallback& callback));
  MOCK_METHOD1(SetRemoteChangeProcessor,
               void(RemoteChangeProcessor* processor));
  MOCK_METHOD0(GetLocalChangeProcessor, LocalChangeProcessor*());
  MOCK_METHOD1(IsConflicting, bool(const fileapi::FileSystemURL& url));
  MOCK_CONST_METHOD0(GetCurrentState,
                     RemoteServiceState());
  MOCK_METHOD1(GetOriginStatusMap,
               void(RemoteFileSyncService::OriginStatusMap* status_map));
  MOCK_METHOD1(SetSyncEnabled, void(bool enabled));
  MOCK_METHOD1(SetConflictResolutionPolicy,
               SyncStatusCode(ConflictResolutionPolicy));
  MOCK_CONST_METHOD0(GetConflictResolutionPolicy,
                     ConflictResolutionPolicy());
  MOCK_METHOD2(GetRemoteVersions,
               void(const fileapi::FileSystemURL&,
                    const RemoteVersionsCallback&));
  MOCK_METHOD3(DownloadRemoteVersion,
               void(const fileapi::FileSystemURL&,
                    const std::string&,
                    const DownloadVersionCallback&));

  virtual scoped_ptr<base::ListValue> DumpFiles(const GURL& origin) OVERRIDE;

  void SetServiceState(RemoteServiceState state);

  // Send notifications to the observers.
  // Can be used in the mock implementation.
  void NotifyRemoteChangeQueueUpdated(int64 pending_changes);
  void NotifyRemoteServiceStateUpdated(
      RemoteServiceState state,
      const std::string& description);
  void NotifyFileStatusChanged(
      const fileapi::FileSystemURL& url,
      SyncFileStatus sync_status,
      SyncAction action_taken,
      SyncDirection direction);

 private:
  void AddServiceObserverStub(Observer* observer);
  void AddFileStatusObserverStub(FileStatusObserver* observer);
  void RegisterOriginStub(
      const GURL& origin, const SyncStatusCallback& callback);
  void DeleteOriginDirectoryStub(
      const GURL& origin, UninstallFlag flag,
      const SyncStatusCallback& callback);
  void ProcessRemoteChangeStub(const SyncFileCallback& callback);
  SyncStatusCode SetConflictResolutionPolicyStub(
      ConflictResolutionPolicy policy);
  ConflictResolutionPolicy GetConflictResolutionPolicyStub() const;
  RemoteServiceState GetCurrentStateStub() const;

  // For default implementation.
  ::testing::NiceMock<MockLocalChangeProcessor> mock_local_change_processor_;

  ObserverList<Observer> service_observers_;
  ObserverList<FileStatusObserver> file_status_observers_;

  ConflictResolutionPolicy conflict_resolution_policy_;

  RemoteServiceState state_;

  DISALLOW_COPY_AND_ASSIGN(MockRemoteFileSyncService);
};

}  // namespace sync_file_system

#endif  // CHROME_BROWSER_SYNC_FILE_SYSTEM_MOCK_REMOTE_FILE_SYNC_SERVICE_H_
