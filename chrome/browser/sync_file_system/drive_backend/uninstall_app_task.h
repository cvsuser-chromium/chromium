// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SYNC_FILE_SYSTEM_DRIVE_BACKEND_UNINSTALL_APP_TASK_H_
#define CHROME_BROWSER_SYNC_FILE_SYSTEM_DRIVE_BACKEND_UNINSTALL_APP_TASK_H_

#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/google_apis/gdata_errorcode.h"
#include "chrome/browser/sync_file_system/remote_file_sync_service.h"
#include "chrome/browser/sync_file_system/sync_callbacks.h"
#include "chrome/browser/sync_file_system/sync_task.h"

namespace drive {
class DriveServiceInterface;
}

namespace google_apis {
class ResourceEntry;
class ResourceList;
}

namespace sync_file_system {
namespace drive_backend {

class FileTracker;
class MetadataDatabase;
class SyncEngineContext;
class TrackerSet;

class UninstallAppTask : public SyncTask {
 public:
  typedef RemoteFileSyncService::UninstallFlag UninstallFlag;
  UninstallAppTask(SyncEngineContext* sync_context,
                   const std::string& app_id,
                   UninstallFlag uninstall_flag);
  virtual ~UninstallAppTask();

  virtual void Run(const SyncStatusCallback& callback) OVERRIDE;

 private:
  void DidDeleteAppRoot(const SyncStatusCallback& callback,
                        int64 change_id,
                        google_apis::GDataErrorCode error);

  MetadataDatabase* metadata_database();
  drive::DriveServiceInterface* drive_service();

  SyncEngineContext* sync_context_;  // Not owned.

  std::string app_id_;
  UninstallFlag uninstall_flag_;
  int64 app_root_tracker_id_;

  base::WeakPtrFactory<UninstallAppTask> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(UninstallAppTask);
};

}  // namespace drive_backend
}  // namespace sync_file_system

#endif  // CHROME_BROWSER_SYNC_FILE_SYSTEM_DRIVE_BACKEND_UNINSTALL_APP_TASK_H_
