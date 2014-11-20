// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This file provides utility functions for fileBrowserPrivate API.

#ifndef CHROME_BROWSER_CHROMEOS_EXTENSIONS_FILE_MANAGER_PRIVATE_API_UTIL_H_
#define CHROME_BROWSER_CHROMEOS_EXTENSIONS_FILE_MANAGER_PRIVATE_API_UTIL_H_

#include "base/callback_forward.h"
#include "chrome/browser/google_apis/gdata_wapi_parser.h"
#include "url/gurl.h"

class ExtensionFunctionDispatcher;
class Profile;

namespace content {
class RenderViewHost;
class WebContents;
}

namespace extensions {
namespace api {
namespace file_browser_private {
struct VolumeMetadata;
}
}
}

namespace ui {
struct SelectedFileInfo;
}

namespace file_manager {

struct VolumeInfo;

namespace util {

// Converts the |volume_info| to VolumeMetadata to communicate with JavaScript
// via private API.
void VolumeInfoToVolumeMetadata(
    Profile* profile,
    const VolumeInfo& volume_info,
    extensions::api::file_browser_private::VolumeMetadata* volume_metadata);

// Returns the WebContents of the tab associated with the dispatcher. Returns
// NULL on error.
content::WebContents* GetWebContents(ExtensionFunctionDispatcher* dispatcher);

// Returns the ID of the tab associated with the dispatcher. Returns 0 on
// error.
int32 GetTabId(ExtensionFunctionDispatcher* dispatcher);

// Returns the local FilePath associated with |url|. If the file isn't of the
// type FileSystemBackend handles, returns an empty
// FilePath. |render_view_host| and |profile| are needed to obtain the
// FileSystemContext currently in use.
//
// Local paths will look like "/home/chronos/user/Downloads/foo/bar.txt" or
// "/special/drive/foo/bar.txt".
base::FilePath GetLocalPathFromURL(
    content::RenderViewHost* render_view_host,
    Profile* profile,
    const GURL& url);

// The callback type is used for GetSelectedFileInfo().
typedef base::Callback<void(const std::vector<ui::SelectedFileInfo>&)>
    GetSelectedFileInfoCallback;

// Option enum to control how to set the ui::SelectedFileInfo::local_path
// fields in GetSelectedFileInfo() for Drive files.
// NO_LOCAL_PATH_RESOLUTION:
//   Does nothing. Set the Drive path as-is.
// NEED_LOCAL_PATH_FOR_OPENING:
//   Sets the path to a local cache file.
// NEED_LOCAL_PATH_FOR_SAVING:
//   Sets the path to a local cache file. Modification to the file is monitored
//   and automatically synced to the Drive server.
enum GetSelectedFileInfoLocalPathOption {
  NO_LOCAL_PATH_RESOLUTION,
  NEED_LOCAL_PATH_FOR_OPENING,
  NEED_LOCAL_PATH_FOR_SAVING,
};

// Gets the information for |file_urls|.
void GetSelectedFileInfo(content::RenderViewHost* render_view_host,
                         Profile* profile,
                         const std::vector<GURL>& file_urls,
                         GetSelectedFileInfoLocalPathOption local_path_option,
                         GetSelectedFileInfoCallback callback);

}  // namespace util
}  // namespace file_manager

#endif  // CHROME_BROWSER_CHROMEOS_EXTENSIONS_FILE_MANAGER_PRIVATE_API_UTIL_H_
