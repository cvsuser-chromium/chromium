// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This file provides task related API functions.

#ifndef CHROME_BROWSER_CHROMEOS_EXTENSIONS_FILE_MANAGER_PRIVATE_API_MOUNT_H_
#define CHROME_BROWSER_CHROMEOS_EXTENSIONS_FILE_MANAGER_PRIVATE_API_MOUNT_H_

#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "chrome/browser/chromeos/drive/file_errors.h"
#include "chrome/browser/chromeos/extensions/file_manager/private_api_base.h"

namespace ui {
struct SelectedFileInfo;
}

namespace extensions {

// Implements chrome.fileBrowserPrivate.addMount method.
// Mounts a device or a file.
class FileBrowserPrivateAddMountFunction : public LoggedAsyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("fileBrowserPrivate.addMount",
                             FILEBROWSERPRIVATE_ADDMOUNT)

 protected:
  virtual ~FileBrowserPrivateAddMountFunction() {}

  // AsyncExtensionFunction overrides.
  virtual bool RunImpl() OVERRIDE;

 private:
  // Part of Run(). Called after MarkCacheFielAsMounted for Drive File System.
  // (or directly called from RunImpl() for other file system).
  void RunAfterMarkCacheFileAsMounted(const base::FilePath& display_name,
                                      drive::FileError error,
                                      const base::FilePath& file_path);
};

// Implements chrome.fileBrowserPrivate.removeMount method.
// Unmounts selected device. Expects mount point path as an argument.
class FileBrowserPrivateRemoveMountFunction
    : public LoggedAsyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("fileBrowserPrivate.removeMount",
                             FILEBROWSERPRIVATE_REMOVEMOUNT)

 protected:
  virtual ~FileBrowserPrivateRemoveMountFunction() {}

  // AsyncExtensionFunction overrides.
  virtual bool RunImpl() OVERRIDE;

 private:
  // A callback method to handle the result of GetSelectedFileInfo.
  void GetSelectedFileInfoResponse(
      const std::vector<ui::SelectedFileInfo>& files);
};

// Implements chrome.fileBrowserPrivate.getVolumeMetadataList method.
class FileBrowserPrivateGetVolumeMetadataListFunction
    : public LoggedAsyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("fileBrowserPrivate.getVolumeMetadataList",
                             FILEBROWSERPRIVATE_GETVOLUMEMETADATALIST)

 protected:
  virtual ~FileBrowserPrivateGetVolumeMetadataListFunction() {}

  // AsyncExtensionFunction overrides.
  virtual bool RunImpl() OVERRIDE;
};

}  // namespace extensions

#endif  // CHROME_BROWSER_CHROMEOS_EXTENSIONS_FILE_MANAGER_PRIVATE_API_MOUNT_H_
