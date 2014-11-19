// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync_file_system/local/sync_file_system_backend.h"

#include "base/logging.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/sync_file_system/local/local_file_change_tracker.h"
#include "chrome/browser/sync_file_system/local/local_file_sync_context.h"
#include "chrome/browser/sync_file_system/local/syncable_file_system_operation.h"
#include "chrome/browser/sync_file_system/sync_file_system_service.h"
#include "chrome/browser/sync_file_system/sync_file_system_service_factory.h"
#include "chrome/browser/sync_file_system/syncable_file_system_util.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/notification_service.h"
#include "webkit/browser/blob/file_stream_reader.h"
#include "webkit/browser/fileapi/file_stream_writer.h"
#include "webkit/browser/fileapi/file_system_context.h"
#include "webkit/browser/fileapi/file_system_operation.h"
#include "webkit/common/fileapi/file_system_util.h"

using content::BrowserThread;

namespace sync_file_system {

namespace {

bool CalledOnUIThread() {
  // Ensure that these methods are called on the UI thread, except for unittests
  // where a UI thread might not have been created.
  return BrowserThread::CurrentlyOn(BrowserThread::UI) ||
         !BrowserThread::IsMessageLoopValid(BrowserThread::UI);
}

}  // namespace

SyncFileSystemBackend::ProfileHolder::ProfileHolder(Profile* profile)
    : profile_(profile) {
  DCHECK(CalledOnUIThread());
  registrar_.Add(this,
                 chrome::NOTIFICATION_PROFILE_DESTROYED,
                 content::Source<Profile>(profile_));
}

void SyncFileSystemBackend::ProfileHolder::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  DCHECK(CalledOnUIThread());
  DCHECK_EQ(chrome::NOTIFICATION_PROFILE_DESTROYED, type);
  DCHECK_EQ(profile_, content::Source<Profile>(source).ptr());
  profile_ = NULL;
  registrar_.RemoveAll();
}

Profile* SyncFileSystemBackend::ProfileHolder::GetProfile() {
  DCHECK(CalledOnUIThread());
  return profile_;
}

SyncFileSystemBackend::SyncFileSystemBackend(Profile* profile)
    : context_(NULL),
      skip_initialize_syncfs_service_for_testing_(false) {
  DCHECK(CalledOnUIThread());
  if (profile)
    profile_holder_.reset(new ProfileHolder(profile));

  // Register the service name here to enable to crack an URL on SyncFileSystem
  // even if SyncFileSystemService has not started yet.
  RegisterSyncableFileSystem();
}

SyncFileSystemBackend::~SyncFileSystemBackend() {
  RevokeSyncableFileSystem();

  if (change_tracker_) {
    GetDelegate()->file_task_runner()->DeleteSoon(
        FROM_HERE, change_tracker_.release());
  }

  if (profile_holder_ && !CalledOnUIThread()) {
    BrowserThread::DeleteSoon(
        BrowserThread::UI, FROM_HERE, profile_holder_.release());
  }
}

// static
SyncFileSystemBackend* SyncFileSystemBackend::CreateForTesting() {
  DCHECK(CalledOnUIThread());
  SyncFileSystemBackend* backend = new SyncFileSystemBackend(NULL);
  backend->skip_initialize_syncfs_service_for_testing_ = true;
  return backend;
}

bool SyncFileSystemBackend::CanHandleType(
    fileapi::FileSystemType type) const {
  return type == fileapi::kFileSystemTypeSyncable ||
         type == fileapi::kFileSystemTypeSyncableForInternalSync;
}

void SyncFileSystemBackend::Initialize(fileapi::FileSystemContext* context) {
  DCHECK(context);
  DCHECK(!context_);
  context_ = context;

  fileapi::SandboxFileSystemBackendDelegate* delegate = GetDelegate();
  delegate->RegisterQuotaUpdateObserver(fileapi::kFileSystemTypeSyncable);
  delegate->RegisterQuotaUpdateObserver(
      fileapi::kFileSystemTypeSyncableForInternalSync);
}

void SyncFileSystemBackend::OpenFileSystem(
    const GURL& origin_url,
    fileapi::FileSystemType type,
    fileapi::OpenFileSystemMode mode,
    const OpenFileSystemCallback& callback) {
  DCHECK(CanHandleType(type));

  if (skip_initialize_syncfs_service_for_testing_) {
    GetDelegate()->OpenFileSystem(origin_url, type, mode, callback,
                                  GetSyncableFileSystemRootURI(origin_url));
    return;
  }

  // It is safe to pass Unretained(this) since |context_| owns it.
  SyncStatusCallback initialize_callback =
      base::Bind(&SyncFileSystemBackend::DidInitializeSyncFileSystemService,
                 base::Unretained(this), make_scoped_refptr(context_),
                 origin_url, type, mode, callback);
  InitializeSyncFileSystemService(origin_url, initialize_callback);
}

fileapi::AsyncFileUtil* SyncFileSystemBackend::GetAsyncFileUtil(
    fileapi::FileSystemType type) {
  return GetDelegate()->file_util();
}

fileapi::CopyOrMoveFileValidatorFactory*
SyncFileSystemBackend::GetCopyOrMoveFileValidatorFactory(
    fileapi::FileSystemType type,
    base::PlatformFileError* error_code) {
  DCHECK(error_code);
  *error_code = base::PLATFORM_FILE_OK;
  return NULL;
}

fileapi::FileSystemOperation*
SyncFileSystemBackend::CreateFileSystemOperation(
    const fileapi::FileSystemURL& url,
    fileapi::FileSystemContext* context,
    base::PlatformFileError* error_code) const {
  DCHECK(CanHandleType(url.type()));
  DCHECK(context);
  DCHECK(error_code);

  scoped_ptr<fileapi::FileSystemOperationContext> operation_context =
      GetDelegate()->CreateFileSystemOperationContext(url, context, error_code);
  if (!operation_context)
    return NULL;

  if (url.type() == fileapi::kFileSystemTypeSyncableForInternalSync) {
    return fileapi::FileSystemOperation::Create(
        url, context, operation_context.Pass());
  }

  return new SyncableFileSystemOperation(
      url, context, operation_context.Pass());
}

scoped_ptr<webkit_blob::FileStreamReader>
SyncFileSystemBackend::CreateFileStreamReader(
    const fileapi::FileSystemURL& url,
    int64 offset,
    const base::Time& expected_modification_time,
    fileapi::FileSystemContext* context) const {
  DCHECK(CanHandleType(url.type()));
  return GetDelegate()->CreateFileStreamReader(
      url, offset, expected_modification_time, context);
}

scoped_ptr<fileapi::FileStreamWriter>
SyncFileSystemBackend::CreateFileStreamWriter(
    const fileapi::FileSystemURL& url,
    int64 offset,
    fileapi::FileSystemContext* context) const {
  DCHECK(CanHandleType(url.type()));
  return GetDelegate()->CreateFileStreamWriter(
      url, offset, context, fileapi::kFileSystemTypeSyncableForInternalSync);
}

fileapi::FileSystemQuotaUtil* SyncFileSystemBackend::GetQuotaUtil() {
  return GetDelegate();
}

// static
SyncFileSystemBackend* SyncFileSystemBackend::GetBackend(
    const fileapi::FileSystemContext* file_system_context) {
  DCHECK(file_system_context);
  return static_cast<SyncFileSystemBackend*>(
      file_system_context->GetFileSystemBackend(
          fileapi::kFileSystemTypeSyncable));
}

void SyncFileSystemBackend::SetLocalFileChangeTracker(
    scoped_ptr<LocalFileChangeTracker> tracker) {
  DCHECK(!change_tracker_);
  DCHECK(tracker);
  change_tracker_ = tracker.Pass();

  fileapi::SandboxFileSystemBackendDelegate* delegate = GetDelegate();
  delegate->AddFileUpdateObserver(
      fileapi::kFileSystemTypeSyncable,
      change_tracker_.get(),
      delegate->file_task_runner());
  delegate->AddFileChangeObserver(
      fileapi::kFileSystemTypeSyncable,
      change_tracker_.get(),
      delegate->file_task_runner());
}

void SyncFileSystemBackend::set_sync_context(
    LocalFileSyncContext* sync_context) {
  DCHECK(!sync_context_);
  sync_context_ = sync_context;
}

fileapi::SandboxFileSystemBackendDelegate*
SyncFileSystemBackend::GetDelegate() const {
  DCHECK(context_);
  DCHECK(context_->sandbox_delegate());
  return context_->sandbox_delegate();
}

void SyncFileSystemBackend::InitializeSyncFileSystemService(
    const GURL& origin_url,
    const SyncStatusCallback& callback) {
  // Repost to switch from IO thread to UI thread.
  if (!BrowserThread::CurrentlyOn(BrowserThread::UI)) {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
    // It is safe to pass Unretained(this) (see comments in OpenFileSystem()).
    BrowserThread::PostTask(
        BrowserThread::UI, FROM_HERE,
        base::Bind(&SyncFileSystemBackend::InitializeSyncFileSystemService,
                   base::Unretained(this), origin_url, callback));
    return;
  }

  if (!profile_holder_->GetProfile()) {
    // Profile was destroyed.
    callback.Run(SYNC_FILE_ERROR_FAILED);
    return;
  }

  SyncFileSystemService* service = SyncFileSystemServiceFactory::GetForProfile(
          profile_holder_->GetProfile());
  DCHECK(service);
  service->InitializeForApp(context_, origin_url, callback);
}

void SyncFileSystemBackend::DidInitializeSyncFileSystemService(
    fileapi::FileSystemContext* context,
    const GURL& origin_url,
    fileapi::FileSystemType type,
    fileapi::OpenFileSystemMode mode,
    const OpenFileSystemCallback& callback,
    SyncStatusCode status) {
  // Repost to switch from UI thread to IO thread.
  if (!BrowserThread::CurrentlyOn(BrowserThread::IO)) {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
    // It is safe to pass Unretained(this) since |context| owns it.
    BrowserThread::PostTask(
        BrowserThread::IO, FROM_HERE,
        base::Bind(&SyncFileSystemBackend::DidInitializeSyncFileSystemService,
                   base::Unretained(this), make_scoped_refptr(context),
                   origin_url, type, mode, callback, status));
    return;
  }

  if (status != sync_file_system::SYNC_STATUS_OK) {
    callback.Run(GURL(), std::string(),
                 SyncStatusCodeToPlatformFileError(status));
    return;
  }

  GetDelegate()->OpenFileSystem(origin_url, type, mode, callback,
                                GetSyncableFileSystemRootURI(origin_url));
}

}  // namespace sync_file_system
