// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SYNC_FILE_SYSTEM_DRIVE_BACKEND_DRIVE_BACKEND_CONSTANTS_H_
#define CHROME_BROWSER_SYNC_FILE_SYSTEM_DRIVE_BACKEND_DRIVE_BACKEND_CONSTANTS_H_

#include "base/files/file_path.h"

namespace sync_file_system {
namespace drive_backend {

extern const char kSyncRootFolderTitle[];
extern const base::FilePath::CharType kDatabaseName[];

extern const char kDatabaseVersionKey[];
extern const int64 kCurrentDatabaseVersion;
extern const char kServiceMetadataKey[];
extern const char kFileMetadataKeyPrefix[];
extern const char kFileTrackerKeyPrefix[];

extern const int kMaxRetry;
extern const int64 kListChangesRetryDelaySeconds;

}  // namespace drive_backend
}  // namespace sync_file_system

#endif  // CHROME_BROWSER_SYNC_FILE_SYSTEM_DRIVE_BACKEND_DRIVE_BACKEND_CONSTANTS_H_
