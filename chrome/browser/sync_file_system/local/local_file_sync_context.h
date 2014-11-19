// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SYNC_FILE_SYSTEM_LOCAL_LOCAL_FILE_SYNC_CONTEXT_H_
#define CHROME_BROWSER_SYNC_FILE_SYSTEM_LOCAL_LOCAL_FILE_SYNC_CONTEXT_H_

#include <deque>
#include <map>
#include <set>
#include <string>

#include "base/basictypes.h"
#include "base/callback.h"
#include "base/files/file_path.h"
#include "base/logging.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/timer/timer.h"
#include "chrome/browser/sync_file_system/local/local_file_sync_status.h"
#include "chrome/browser/sync_file_system/sync_callbacks.h"
#include "chrome/browser/sync_file_system/sync_status_code.h"
#include "url/gurl.h"

namespace base {
class SingleThreadTaskRunner;
}

namespace fileapi {
class FileSystemContext;
class FileSystemURL;
}

namespace webkit_blob {
class ScopedFile;
}

namespace sync_file_system {

class FileChange;
class LocalFileChangeTracker;
struct LocalFileSyncInfo;
class LocalOriginChangeObserver;
class SyncableFileOperationRunner;

// This class works as a bridge between LocalFileSyncService (which is a
// per-profile object) and FileSystemContext's (which is a per-storage-partition
// object and may exist multiple in a profile).
// An instance of this class is shared by FileSystemContexts and outlives
// LocalFileSyncService.
class LocalFileSyncContext
    : public base::RefCountedThreadSafe<LocalFileSyncContext>,
      public LocalFileSyncStatus::Observer {
 public:
  enum SyncMode {
    SYNC_EXCLUSIVE,
    SYNC_SNAPSHOT,
  };

  typedef base::Callback<void(
      SyncStatusCode status,
      const LocalFileSyncInfo& sync_file_info,
      webkit_blob::ScopedFile snapshot)>
          LocalFileSyncInfoCallback;

  typedef base::Callback<void(SyncStatusCode status,
                              bool has_pending_changes)>
      HasPendingLocalChangeCallback;

  LocalFileSyncContext(const base::FilePath& base_path,
                       base::SingleThreadTaskRunner* ui_task_runner,
                       base::SingleThreadTaskRunner* io_task_runner);

  // Initializes |file_system_context| for syncable file operations
  // and registers the it into the internal map.
  // Calling this multiple times for the same file_system_context is valid.
  // This method must be called on UI thread.
  void MaybeInitializeFileSystemContext(
      const GURL& source_url,
      fileapi::FileSystemContext* file_system_context,
      const SyncStatusCallback& callback);

  // Called when the corresponding LocalFileSyncService exits.
  // This method must be called on UI thread.
  void ShutdownOnUIThread();

  // Picks a file for next local sync and returns it after disabling writes
  // for the file.
  // This method must be called on UI thread.
  void GetFileForLocalSync(fileapi::FileSystemContext* file_system_context,
                           const LocalFileSyncInfoCallback& callback);

  // TODO(kinuko): Make this private.
  // Clears all pending local changes for |url|. |done_callback| is called
  // when the changes are cleared.
  // This method must be called on UI thread.
  void ClearChangesForURL(fileapi::FileSystemContext* file_system_context,
                          const fileapi::FileSystemURL& url,
                          const base::Closure& done_callback);

  // Finalizes SnapshotSync, which must have been started by
  // PrepareForSync with SYNC_SNAPSHOT.
  // Updates the on-disk dirty flag for |url| in the tracker DB.
  // This will clear the dirty flag if |sync_finish_status| is SYNC_STATUS_OK
  // or SYNC_STATUS_HAS_CONFLICT.
  // |done_callback| is called when the changes are committed.
  void FinalizeSnapshotSync(
      fileapi::FileSystemContext* file_system_context,
      const fileapi::FileSystemURL& url,
      SyncStatusCode sync_finish_status,
      const base::Closure& done_callback);

  // Finalizes ExclusiveSync, which must have been started by
  // PrepareForSync with SYNC_EXCLUSIVE.
  void FinalizeExclusiveSync(
      fileapi::FileSystemContext* file_system_context,
      const fileapi::FileSystemURL& url,
      bool clear_local_changes,
      const base::Closure& done_callback);

  // Prepares for sync |url| by disabling writes on |url|.
  // If the target |url| is being written and cannot start sync it
  // returns SYNC_STATUS_WRITING status code via |callback|.
  // Otherwise returns the current change sets made on |url|.
  //
  // If |sync_mode| is SYNC_EXCLUSIVE this leaves the target file locked.
  // If |sync_mode| is SYNC_SNAPSHOT this creates a snapshot (if the
  // target file is not deleted) and unlocks the file before returning.
  //
  // For SYNC_EXCLUSIVE, caller must call FinalizeExclusiveSync() to finalize
  // sync and unlock the file.
  // For SYNC_SNAPSHOT, caller must call FinalizeSnapshotSync() to finalize
  // sync to reset the mirrored change status and decrement writing count.
  //
  // This method must be called on UI thread.
  void PrepareForSync(fileapi::FileSystemContext* file_system_context,
                      const fileapi::FileSystemURL& url,
                      SyncMode sync_mode,
                      const LocalFileSyncInfoCallback& callback);

  // Registers |url| to wait until sync is enabled for |url|.
  // |on_syncable_callback| is to be called when |url| becomes syncable
  // (i.e. when we have no pending writes and the file is successfully locked
  // for sync).
  //
  // Calling this method again while this already has another URL waiting
  // for sync will overwrite the previously registered URL.
  //
  // This method must be called on UI thread.
  void RegisterURLForWaitingSync(const fileapi::FileSystemURL& url,
                                 const base::Closure& on_syncable_callback);

  // Applies a remote change.
  // This method must be called on UI thread.
  void ApplyRemoteChange(
      fileapi::FileSystemContext* file_system_context,
      const FileChange& change,
      const base::FilePath& local_path,
      const fileapi::FileSystemURL& url,
      const SyncStatusCallback& callback);

  // Records a fake local change in the local change tracker.
  void RecordFakeLocalChange(
      fileapi::FileSystemContext* file_system_context,
      const fileapi::FileSystemURL& url,
      const FileChange& change,
      const SyncStatusCallback& callback);

  // This must be called on UI thread.
  void GetFileMetadata(
      fileapi::FileSystemContext* file_system_context,
      const fileapi::FileSystemURL& url,
      const SyncFileMetadataCallback& callback);

  // Returns true via |callback| if the given file |url| has local pending
  // changes.
  void HasPendingLocalChanges(
      fileapi::FileSystemContext* file_system_context,
      const fileapi::FileSystemURL& url,
      const HasPendingLocalChangeCallback& callback);

  // They must be called on UI thread.
  void AddOriginChangeObserver(LocalOriginChangeObserver* observer);
  void RemoveOriginChangeObserver(LocalOriginChangeObserver* observer);

  // OperationRunner is accessible only on IO thread.
  base::WeakPtr<SyncableFileOperationRunner> operation_runner() const;

  // SyncContext is accessible only on IO thread.
  LocalFileSyncStatus* sync_status() const;

  // For testing; override the duration to notify changes from the
  // default value.
  void set_mock_notify_changes_duration_in_sec(int duration) {
    mock_notify_changes_duration_in_sec_ = duration;
  }

 protected:
  // LocalFileSyncStatus::Observer overrides. They are called on IO thread.
  virtual void OnSyncEnabled(const fileapi::FileSystemURL& url) OVERRIDE;
  virtual void OnWriteEnabled(const fileapi::FileSystemURL& url) OVERRIDE;

 private:
  typedef base::Callback<void(base::PlatformFileError result)> StatusCallback;
  typedef std::deque<SyncStatusCallback> StatusCallbackQueue;
  friend class base::RefCountedThreadSafe<LocalFileSyncContext>;
  friend class CannedSyncableFileSystem;

  virtual ~LocalFileSyncContext();

  void ShutdownOnIOThread();

  // Starts a timer to eventually call NotifyAvailableChangesOnIOThread.
  // The caller is expected to update origins_with_pending_changes_ before
  // calling this.
  void ScheduleNotifyChangesUpdatedOnIOThread();

  // Called by the internal timer on IO thread to notify changes to UI thread.
  void NotifyAvailableChangesOnIOThread();

  // Called from NotifyAvailableChangesOnIOThread.
  void NotifyAvailableChanges(const std::set<GURL>& origins);

  // Helper routines for MaybeInitializeFileSystemContext.
  void InitializeFileSystemContextOnIOThread(
      const GURL& source_url,
      fileapi::FileSystemContext* file_system_context);
  SyncStatusCode InitializeChangeTrackerOnFileThread(
      scoped_ptr<LocalFileChangeTracker>* tracker_ptr,
      fileapi::FileSystemContext* file_system_context,
      std::set<GURL>* origins_with_changes);
  void DidInitializeChangeTrackerOnIOThread(
      scoped_ptr<LocalFileChangeTracker>* tracker_ptr,
      const GURL& source_url,
      fileapi::FileSystemContext* file_system_context,
      std::set<GURL>* origins_with_changes,
      SyncStatusCode status);
  void DidInitialize(
      const GURL& source_url,
      fileapi::FileSystemContext* file_system_context,
      SyncStatusCode status);

  // Helper routines for GetFileForLocalSync.
  void GetNextURLsForSyncOnFileThread(
      fileapi::FileSystemContext* file_system_context,
      std::deque<fileapi::FileSystemURL>* urls);
  void TryPrepareForLocalSync(
      fileapi::FileSystemContext* file_system_context,
      std::deque<fileapi::FileSystemURL>* urls,
      const LocalFileSyncInfoCallback& callback);
  void DidTryPrepareForLocalSync(
      fileapi::FileSystemContext* file_system_context,
      std::deque<fileapi::FileSystemURL>* remaining_urls,
      const LocalFileSyncInfoCallback& callback,
      SyncStatusCode status,
      const LocalFileSyncInfo& sync_file_info,
      webkit_blob::ScopedFile snapshot);

  // Callback routine for PrepareForSync and GetFileForLocalSync.
  void DidGetWritingStatusForSync(
      fileapi::FileSystemContext* file_system_context,
      SyncStatusCode status,
      const fileapi::FileSystemURL& url,
      SyncMode sync_mode,
      const LocalFileSyncInfoCallback& callback);

  // Helper routine for sync/writing flag handling.
  //
  // If |for_snapshot_sync| is true, this increments the writing counter
  // for |url| (after clearing syncing flag), so that other sync activities
  // won't step in while snapshot sync is ongoing.
  // In this case FinalizeSnapshotSyncOnIOThread must be called after the
  // snapshot sync is finished to decrement the writing counter.
  void ClearSyncFlagOnIOThread(const fileapi::FileSystemURL& url,
                               bool for_snapshot_sync);
  void FinalizeSnapshotSyncOnIOThread(const fileapi::FileSystemURL& url);

  void DidRemoveExistingEntryForApplyRemoteChange(
      fileapi::FileSystemContext* file_system_context,
      const FileChange& change,
      const base::FilePath& local_path,
      const fileapi::FileSystemURL& url,
      const SyncStatusCallback& callback,
      base::PlatformFileError error);

  // Callback routine for ApplyRemoteChange.
  void DidApplyRemoteChange(
      const fileapi::FileSystemURL& url,
      const SyncStatusCallback& callback_on_ui,
      base::PlatformFileError file_error);

  void DidGetFileMetadata(
      const SyncFileMetadataCallback& callback,
      base::PlatformFileError file_error,
      const base::PlatformFileInfo& file_info);

  base::TimeDelta NotifyChangesDuration();

  void DidCreateDirectoryForCopyIn(
      fileapi::FileSystemContext* file_system_context,
      const base::FilePath& local_file_path,
      const fileapi::FileSystemURL& dest_url,
      const StatusCallback& callback,
      base::PlatformFileError error);

  const base::FilePath local_base_path_;

  scoped_refptr<base::SingleThreadTaskRunner> ui_task_runner_;
  scoped_refptr<base::SingleThreadTaskRunner> io_task_runner_;

  // Indicates if the sync service is shutdown on UI thread.
  bool shutdown_on_ui_;

  // OperationRunner. This must be accessed only on IO thread.
  scoped_ptr<SyncableFileOperationRunner> operation_runner_;

  // Keeps track of writing/syncing status.
  // This must be accessed only on IO thread.
  scoped_ptr<LocalFileSyncStatus> sync_status_;

  // Pointers to file system contexts that have been initialized for
  // synchronization (i.e. that own this instance).
  // This must be accessed only on UI thread.
  std::set<fileapi::FileSystemContext*> file_system_contexts_;

  // Accessed only on UI thread.
  std::map<fileapi::FileSystemContext*, StatusCallbackQueue>
      pending_initialize_callbacks_;

  // A URL and associated callback waiting for sync is enabled.
  // Accessed only on IO thread.
  fileapi::FileSystemURL url_waiting_sync_on_io_;
  base::Closure url_syncable_callback_;

  // Used only on IO thread for available changes notifications.
  base::Time last_notified_changes_;
  scoped_ptr<base::OneShotTimer<LocalFileSyncContext> > timer_on_io_;
  std::set<GURL> origins_with_pending_changes_;

  ObserverList<LocalOriginChangeObserver> origin_change_observers_;

  int mock_notify_changes_duration_in_sec_;

  DISALLOW_COPY_AND_ASSIGN(LocalFileSyncContext);
};

}  // namespace sync_file_system

#endif  // CHROME_BROWSER_SYNC_FILE_SYSTEM_LOCAL_LOCAL_FILE_SYNC_CONTEXT_H_
