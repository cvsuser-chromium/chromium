// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync_file_system/drive_backend/uninstall_app_task.h"

#include "base/bind.h"
#include "base/location.h"
#include "chrome/browser/drive/drive_api_util.h"
#include "chrome/browser/drive/drive_service_interface.h"
#include "chrome/browser/google_apis/drive_api_parser.h"
#include "chrome/browser/google_apis/gdata_wapi_parser.h"
#include "chrome/browser/sync_file_system/drive_backend/drive_backend_constants.h"
#include "chrome/browser/sync_file_system/drive_backend/metadata_database.h"
#include "chrome/browser/sync_file_system/drive_backend/metadata_database.pb.h"
#include "chrome/browser/sync_file_system/drive_backend/sync_engine_context.h"
#include "chrome/browser/sync_file_system/drive_backend/tracker_set.h"
#include "chrome/browser/sync_file_system/syncable_file_system_util.h"

namespace sync_file_system {
namespace drive_backend {

UninstallAppTask::UninstallAppTask(SyncEngineContext* sync_context,
                                   const std::string& app_id,
                                   UninstallFlag uninstall_flag)
    : sync_context_(sync_context),
      app_id_(app_id),
      uninstall_flag_(uninstall_flag),
      app_root_tracker_id_(0),
      weak_ptr_factory_(this) {
}

UninstallAppTask::~UninstallAppTask() {
}

void UninstallAppTask::Run(const SyncStatusCallback& callback) {
  if (!metadata_database() || !drive_service()) {
    RunSoon(FROM_HERE, base::Bind(callback, SYNC_STATUS_FAILED));
    return;
  }

  if (uninstall_flag_ == RemoteFileSyncService::UNINSTALL_AND_KEEP_REMOTE) {
    metadata_database()->UnregisterApp(app_id_, callback);
    return;
  }
  DCHECK_EQ(RemoteFileSyncService::UNINSTALL_AND_PURGE_REMOTE, uninstall_flag_);

  int64 sync_root_tracker_id = metadata_database()->GetSyncRootTrackerID();
  TrackerSet trackers;
  if (!metadata_database()->FindTrackersByParentAndTitle(
          sync_root_tracker_id, app_id_, &trackers) ||
      !trackers.has_active()) {
    RunSoon(FROM_HERE, base::Bind(callback, SYNC_STATUS_OK));
    return;
  }

  FileTracker* app_root_tracker = trackers.active_tracker();
  app_root_tracker_id_ = app_root_tracker->tracker_id();
  DCHECK(app_root_tracker->has_synced_details());

  drive_service()->DeleteResource(
      app_root_tracker->file_id(),
      std::string(),  // etag
      base::Bind(&UninstallAppTask::DidDeleteAppRoot,
                 weak_ptr_factory_.GetWeakPtr(),
                 callback,
                 metadata_database()->GetLargestKnownChangeID()));
}

void UninstallAppTask::DidDeleteAppRoot(const SyncStatusCallback& callback,
                                        int64 change_id,
                                        google_apis::GDataErrorCode error) {
  if (error != google_apis::HTTP_SUCCESS &&
      error != google_apis::HTTP_NOT_FOUND) {
    callback.Run(SYNC_STATUS_FAILED);
    return;
  }

  metadata_database()->UnregisterApp(app_id_, callback);
}

MetadataDatabase* UninstallAppTask::metadata_database() {
  return sync_context_->GetMetadataDatabase();
}

drive::DriveServiceInterface* UninstallAppTask::drive_service() {
  return sync_context_->GetDriveService();
}

}  // namespace drive_backend
}  // namespace sync_file_system
