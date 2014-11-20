// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/extensions/file_manager/private_api_mount.h"

#include "base/format_macros.h"
#include "base/values.h"
#include "chrome/browser/chromeos/drive/file_system_interface.h"
#include "chrome/browser/chromeos/drive/file_system_util.h"
#include "chrome/browser/chromeos/drive/logging.h"
#include "chrome/browser/chromeos/extensions/file_manager/event_router.h"
#include "chrome/browser/chromeos/extensions/file_manager/file_browser_private_api.h"
#include "chrome/browser/chromeos/extensions/file_manager/private_api_util.h"
#include "chrome/browser/chromeos/file_manager/fileapi_util.h"
#include "chrome/browser/chromeos/file_manager/volume_manager.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/extensions/api/file_browser_private.h"
#include "chromeos/disks/disk_mount_manager.h"
#include "content/public/browser/browser_thread.h"
#include "ui/shell_dialogs/selected_file_info.h"

using chromeos::disks::DiskMountManager;
using content::BrowserThread;
namespace file_browser_private = extensions::api::file_browser_private;

namespace extensions {

bool FileBrowserPrivateAddMountFunction::RunImpl() {
  using file_browser_private::AddMount::Params;
  const scoped_ptr<Params> params(Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params);

  drive::util::Log(logging::LOG_INFO,
                   "%s[%d] called. (source: '%s')",
                   name().c_str(),
                   request_id(),
                   params->source.empty() ? "(none)" : params->source.c_str());
  set_log_on_completion(true);

  const base::FilePath path = file_manager::util::GetLocalPathFromURL(
      render_view_host(), GetProfile(), GURL(params->source));

  if (path.empty())
    return false;

  // Check if the source path is under Drive cache directory.
  if (drive::util::IsUnderDriveMountPoint(path)) {
    drive::FileSystemInterface* file_system =
        drive::util::GetFileSystemByProfile(GetProfile());
    if (!file_system)
      return false;

    file_system->MarkCacheFileAsMounted(
        drive::util::ExtractDrivePath(path),
        base::Bind(
            &FileBrowserPrivateAddMountFunction::RunAfterMarkCacheFileAsMounted,
            this, path.BaseName()));
  } else {
    RunAfterMarkCacheFileAsMounted(
        path.BaseName(), drive::FILE_ERROR_OK, path);
  }
  return true;
}

void FileBrowserPrivateAddMountFunction::RunAfterMarkCacheFileAsMounted(
    const base::FilePath& display_name,
    drive::FileError error,
    const base::FilePath& file_path) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  if (error != drive::FILE_ERROR_OK) {
    SendResponse(false);
    return;
  }

  // Pass back the actual source path of the mount point.
  SetResult(new base::StringValue(file_path.AsUTF8Unsafe()));
  SendResponse(true);

  // MountPath() takes a std::string.
  DiskMountManager* disk_mount_manager = DiskMountManager::GetInstance();
  disk_mount_manager->MountPath(
      file_path.AsUTF8Unsafe(),
      base::FilePath(display_name.Extension()).AsUTF8Unsafe(),
      display_name.AsUTF8Unsafe(),
      chromeos::MOUNT_TYPE_ARCHIVE);
}

bool FileBrowserPrivateRemoveMountFunction::RunImpl() {
  using file_browser_private::RemoveMount::Params;
  const scoped_ptr<Params> params(Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params);

  drive::util::Log(logging::LOG_INFO,
                   "%s[%d] called. (mount_path: '%s')",
                   name().c_str(),
                   request_id(),
                   params->mount_path.c_str());
  set_log_on_completion(true);

  std::vector<GURL> file_paths;
  file_paths.push_back(GURL(params->mount_path));
  file_manager::util::GetSelectedFileInfo(
      render_view_host(),
      GetProfile(),
      file_paths,
      file_manager::util::NEED_LOCAL_PATH_FOR_OPENING,
      base::Bind(
          &FileBrowserPrivateRemoveMountFunction::GetSelectedFileInfoResponse,
          this));
  return true;
}

void FileBrowserPrivateRemoveMountFunction::GetSelectedFileInfoResponse(
    const std::vector<ui::SelectedFileInfo>& files) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  if (files.size() != 1) {
    SendResponse(false);
    return;
  }

  // TODO(tbarzic): Send response when callback is received, it would make more
  // sense than remembering issued unmount requests in file manager and showing
  // errors for them when MountCompleted event is received.
  DiskMountManager::GetInstance()->UnmountPath(
      files[0].local_path.value(),
      chromeos::UNMOUNT_OPTIONS_NONE,
      DiskMountManager::UnmountPathCallback());
  SendResponse(true);
}

bool FileBrowserPrivateGetVolumeMetadataListFunction::RunImpl() {
  if (args_->GetSize())
    return false;

  const std::vector<file_manager::VolumeInfo>& volume_info_list =
      file_manager::VolumeManager::Get(GetProfile())->GetVolumeInfoList();

  std::string log_string;
  std::vector<linked_ptr<file_browser_private::VolumeMetadata> > result;
  for (size_t i = 0; i < volume_info_list.size(); ++i) {
    linked_ptr<file_browser_private::VolumeMetadata> volume_metadata(
        new file_browser_private::VolumeMetadata);
    file_manager::util::VolumeInfoToVolumeMetadata(
        GetProfile(), volume_info_list[i], volume_metadata.get());
    result.push_back(volume_metadata);
    if (!log_string.empty())
      log_string += ", ";
    log_string += volume_info_list[i].mount_path.AsUTF8Unsafe();
  }

  drive::util::Log(
      logging::LOG_INFO,
      "%s[%d] succeeded. (results: '[%s]', %" PRIuS " mount points)",
      name().c_str(), request_id(), log_string.c_str(), result.size());

  results_ =
      file_browser_private::GetVolumeMetadataList::Results::Create(result);
  SendResponse(true);
  return true;
}

}  // namespace extensions
