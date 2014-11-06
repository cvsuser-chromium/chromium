// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync_file_system/drive_backend/remote_to_local_syncer.h"

#include "base/bind.h"
#include "base/callback.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/message_loop/message_loop_proxy.h"
#include "chrome/browser/drive/drive_api_util.h"
#include "chrome/browser/drive/drive_service_interface.h"
#include "chrome/browser/google_apis/drive_api_parser.h"
#include "chrome/browser/google_apis/gdata_wapi_parser.h"
#include "chrome/browser/sync_file_system/drive_backend/metadata_database.h"
#include "chrome/browser/sync_file_system/drive_backend/sync_engine_context.h"
#include "chrome/browser/sync_file_system/syncable_file_system_util.h"
#include "chrome/common/extensions/extension.h"

namespace sync_file_system {
namespace drive_backend {

namespace {

bool BuildFileSystemURL(
    MetadataDatabase* metadata_database,
    const FileTracker& tracker,
    fileapi::FileSystemURL* url) {
  base::FilePath path;
  if (!metadata_database->BuildPathForTracker(
          tracker.tracker_id(), &path))
    return false;

  GURL origin =
      extensions::Extension::GetBaseURLFromExtensionId(tracker.app_id());
  *url = sync_file_system::CreateSyncableFileSystemURL(origin, path);

  return true;
}

}  // namespace

RemoteToLocalSyncer::RemoteToLocalSyncer(SyncEngineContext* sync_context,
                                         int priorities)
    : sync_context_(sync_context),
      priorities_(priorities),
      missing_remote_details_(false),
      missing_synced_details_(false),
      deleted_remote_details_(false),
      deleted_synced_details_(false),
      title_changed_(false),
      content_changed_(false),
      needs_folder_listing_(false),
      missing_parent_(false),
      sync_root_modification_(false),
      weak_ptr_factory_(this) {
}

RemoteToLocalSyncer::~RemoteToLocalSyncer() {
  NOTIMPLEMENTED();
}

void RemoteToLocalSyncer::Run(const SyncStatusCallback& callback) {
  if (priorities_ & PRIORITY_NORMAL) {
    if (metadata_database()->GetNormalPriorityDirtyTracker(&dirty_tracker_)) {
      ResolveRemoteChange(callback);
      return;
    }
  }

  if (priorities_ & PRIORITY_LOW) {
    if (metadata_database()->GetLowPriorityDirtyTracker(&dirty_tracker_)) {
      ResolveRemoteChange(callback);
      return;
    }
  }

  base::MessageLoopProxy::current()->PostTask(
      FROM_HERE,
      base::Bind(callback, SYNC_STATUS_NO_CHANGE_TO_SYNC));
}

void RemoteToLocalSyncer::AnalyzeCurrentDirtyTracker() {
  if (!metadata_database()->FindFileByFileID(
          dirty_tracker_.file_id(), &remote_metadata_)) {
    missing_remote_details_ = true;
    return;
  }
  missing_remote_details_ = false;

  if (dirty_tracker_.has_synced_details() &&
      !dirty_tracker_.synced_details().title().empty()) { // Just in case
    missing_synced_details_ = true;
    return;
  }
  missing_synced_details_ = false;

  const FileDetails& synced_details = dirty_tracker_.synced_details();
  const FileDetails& remote_details = remote_metadata_.details();

  deleted_remote_details_ = remote_details.deleted();
  deleted_synced_details_ = synced_details.deleted();
  title_changed_ = synced_details.title() != remote_details.title();

  switch (dirty_tracker_.synced_details().file_kind()) {
    case FILE_KIND_UNSUPPORTED:
      break;
    case FILE_KIND_FILE:
      content_changed_ = synced_details.md5() != remote_details.md5();
      break;
    case FILE_KIND_FOLDER:
      needs_folder_listing_ = dirty_tracker_.needs_folder_listing();
      break;
  }

  bool unknown_parent = !metadata_database()->FindTrackerByTrackerID(
      dirty_tracker_.parent_tracker_id(), &parent_tracker_);
  if (unknown_parent) {
    DCHECK_EQ(metadata_database()->GetSyncRootTrackerID(),
              dirty_tracker_.tracker_id());
    sync_root_modification_ = true;
  } else {
    missing_parent_ = true;
    std::string parent_folder_id = parent_tracker_.file_id();
    for (int i = 0; i < remote_details.parent_folder_ids_size(); ++i) {
      if (remote_details.parent_folder_ids(i) == parent_folder_id) {
        missing_parent_ = false;
        break;
      }
    }
  }
}

void RemoteToLocalSyncer::ResolveRemoteChange(
    const SyncStatusCallback& callback) {
  AnalyzeCurrentDirtyTracker();

  if (missing_remote_details_) {
    GetRemoteResource(callback);
    return;
  }

  if (missing_synced_details_) {
    if (deleted_remote_details_) {
      SyncCompleted(callback);
      return;
    }
    HandleNewFile(callback);
    return;
  }

  if (!dirty_tracker_.active()) {
    HandleOfflineSolvable(callback);
    return;
  }

  if (deleted_synced_details_) {
    if (deleted_remote_details_) {
      SyncCompleted(callback);
      return;
    }

    HandleNewFile(callback);
    return;
  }

  if (deleted_remote_details_) {
    HandleDeletion(callback);
    return;
  }

  if (title_changed_) {
    HandleRename(callback);
    return;
  }

  if (content_changed_) {
    HandleContentUpdate(callback);
    return;
  }

  if (needs_folder_listing_) {
    ListFolderContent(callback);
    return;
  }

  if (missing_parent_) {
    HandleReorganize(callback);
    return;
  }

  HandleOfflineSolvable(callback);
}

void RemoteToLocalSyncer::GetRemoteResource(
    const SyncStatusCallback& callback) {
  drive_service()->GetResourceEntry(
      dirty_tracker_.file_id(),
      base::Bind(&RemoteToLocalSyncer::DidGetRemoteResource,
                 weak_ptr_factory_.GetWeakPtr(),
                 callback,
                 metadata_database()->GetLargestKnownChangeID()));
}

void RemoteToLocalSyncer::DidGetRemoteResource(
    const SyncStatusCallback& callback,
    int64 change_id,
    google_apis::GDataErrorCode error,
    scoped_ptr<google_apis::ResourceEntry> entry) {
  metadata_database()->UpdateByFileResource(
      change_id,
      *drive::util::ConvertResourceEntryToFileResource(*entry),
      callback);
}

void RemoteToLocalSyncer::HandleDeletion(
    const SyncStatusCallback& callback) {
  Prepare(base::Bind(&RemoteToLocalSyncer::DidPrepareForDeletion,
                     weak_ptr_factory_.GetWeakPtr(), callback));
}

void RemoteToLocalSyncer::DidPrepareForDeletion(
    const SyncStatusCallback& callback,
    SyncStatusCode status) {
  if (status != SYNC_STATUS_OK) {
    callback.Run(status);
    return;
  }

  if (local_changes_.empty()) {
    DeleteLocalFile(callback);
    return;
  }

  // File is locally deleted or locally updated.
  SyncCompleted(callback);
}

void RemoteToLocalSyncer::HandleNewFile(
    const SyncStatusCallback& callback) {
  Prepare(base::Bind(&RemoteToLocalSyncer::DidPrepareForNewFile,
                     weak_ptr_factory_.GetWeakPtr(), callback));
}

void RemoteToLocalSyncer::DidPrepareForNewFile(
    const SyncStatusCallback& callback,
    SyncStatusCode status) {
  NOTIMPLEMENTED();
  callback.Run(SYNC_STATUS_FAILED);
}

void RemoteToLocalSyncer::HandleContentUpdate(
    const SyncStatusCallback& callback) {
  Prepare(base::Bind(&RemoteToLocalSyncer::DidPrepareForContentUpdate,
                     weak_ptr_factory_.GetWeakPtr(), callback));
}

void RemoteToLocalSyncer::DidPrepareForContentUpdate(
    const SyncStatusCallback& callback,
    SyncStatusCode status) {
  NOTIMPLEMENTED();
  callback.Run(SYNC_STATUS_FAILED);
}

void RemoteToLocalSyncer::ListFolderContent(
    const SyncStatusCallback& callback) {
  Prepare(base::Bind(&RemoteToLocalSyncer::DidPrepareForFolderListing,
                     weak_ptr_factory_.GetWeakPtr(), callback));
}

void RemoteToLocalSyncer::DidPrepareForFolderListing(
    const SyncStatusCallback& callback,
    SyncStatusCode status) {
  NOTIMPLEMENTED();
  callback.Run(SYNC_STATUS_FAILED);
}

void RemoteToLocalSyncer::HandleRename(
    const SyncStatusCallback& callback) {
  NOTIMPLEMENTED();
  callback.Run(SYNC_STATUS_FAILED);
}

void RemoteToLocalSyncer::HandleReorganize(
    const SyncStatusCallback& callback) {
  NOTIMPLEMENTED();
  callback.Run(SYNC_STATUS_FAILED);
}

void RemoteToLocalSyncer::HandleOfflineSolvable(
    const SyncStatusCallback& callback) {
  NOTIMPLEMENTED();
  callback.Run(SYNC_STATUS_FAILED);
}

void RemoteToLocalSyncer::SyncCompleted(
    const SyncStatusCallback& callback) {
  NOTIMPLEMENTED();
  callback.Run(SYNC_STATUS_FAILED);

  // TODO(tzik): Clear dirty mark of the |dirty_tracker|, report operation log
  // the observer.
}

void RemoteToLocalSyncer::Prepare(const SyncStatusCallback& callback) {
  bool should_success = BuildFileSystemURL(
      metadata_database(), dirty_tracker_, &url_);
  DCHECK(should_success);
  remote_change_processor()->PrepareForProcessRemoteChange(
      url_,
      base::Bind(&RemoteToLocalSyncer::DidPrepare,
                 weak_ptr_factory_.GetWeakPtr(),
                 callback));
}

void RemoteToLocalSyncer::DidPrepare(const SyncStatusCallback& callback,
                                     SyncStatusCode status,
                                     const SyncFileMetadata& local_metadata,
                                     const FileChangeList& local_changes) {
  if (status != SYNC_STATUS_OK) {
    callback.Run(status);
    return;
  }

  local_metadata_ = local_metadata;
  local_changes_ = local_changes;

  callback.Run(status);
}

void RemoteToLocalSyncer::DeleteLocalFile(const SyncStatusCallback& callback) {
  if (sync_root_modification_) {
    // TODO(tzik): Sync-root is deleted. Needs special handling.
    NOTIMPLEMENTED();
    callback.Run(SYNC_STATUS_FAILED);
    return;
  }

  if (dirty_tracker_.tracker_kind() == TRACKER_KIND_APP_ROOT) {
    // TODO(tzik): Active app-root is deleted. Needs special handling.
    NOTIMPLEMENTED();
    callback.Run(SYNC_STATUS_FAILED);
    return;
  }

  remote_change_processor()->ApplyRemoteChange(
      FileChange(FileChange::FILE_CHANGE_DELETE, SYNC_FILE_TYPE_UNKNOWN),
      base::FilePath(),
      url_,
      base::Bind(&RemoteToLocalSyncer::DidDeleteLocalFile,
                 weak_ptr_factory_.GetWeakPtr(), callback));
}

void RemoteToLocalSyncer::DidDeleteLocalFile(
    const SyncStatusCallback& callback,
    SyncStatusCode status) {
  if (status != SYNC_STATUS_OK) {
    callback.Run(status);
    return;
  }

  SyncCompleted(callback);
}

drive::DriveServiceInterface* RemoteToLocalSyncer::drive_service() {
  return sync_context_->GetDriveService();
}

MetadataDatabase* RemoteToLocalSyncer::metadata_database() {
  return sync_context_->GetMetadataDatabase();
}

RemoteChangeProcessor* RemoteToLocalSyncer::remote_change_processor() {
  DCHECK(sync_context_->GetRemoteChangeProcessor());
  return sync_context_->GetRemoteChangeProcessor();
}

}  // namespace drive_backend
}  // namespace sync_file_system
