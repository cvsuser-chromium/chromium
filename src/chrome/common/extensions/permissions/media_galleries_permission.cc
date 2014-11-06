// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/common/extensions/permissions/media_galleries_permission.h"

#include <set>
#include <string>

#include "base/logging.h"
#include "base/strings/utf_string_conversions.h"
#include "extensions/common/permissions/permissions_info.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"

namespace {

// copyTo permission requires delete permission as a prerequisite.
// delete permission requires read permission as a prerequisite.
bool IsValidPermissionSet(bool has_read, bool has_copy_to, bool has_delete) {
  if (has_copy_to)
    return has_read && has_delete;
  if (has_delete)
    return has_read;
  return true;
}

}  // namespace

namespace extensions {

const char MediaGalleriesPermission::kAllAutoDetectedPermission[] =
    "allAutoDetected";
const char MediaGalleriesPermission::kReadPermission[] = "read";
const char MediaGalleriesPermission::kCopyToPermission[] = "copyTo";
const char MediaGalleriesPermission::kDeletePermission[] = "delete";

MediaGalleriesPermission::MediaGalleriesPermission(
    const APIPermissionInfo* info)
  : SetDisjunctionPermission<MediaGalleriesPermissionData,
                             MediaGalleriesPermission>(info) {
}

MediaGalleriesPermission::~MediaGalleriesPermission() {
}

bool MediaGalleriesPermission::FromValue(const base::Value* value) {
  if (!SetDisjunctionPermission<MediaGalleriesPermissionData,
                                MediaGalleriesPermission>::FromValue(value)) {
    return false;
  }

  bool has_read = false;
  bool has_copy_to = false;
  bool has_delete = false;
  for (std::set<MediaGalleriesPermissionData>::const_iterator it =
      data_set_.begin(); it != data_set_.end(); ++it) {
    if (it->permission() == kAllAutoDetectedPermission) {
      continue;
    }
    if (it->permission() == kReadPermission) {
      has_read = true;
      continue;
    }
    if (it->permission() == kCopyToPermission) {
      has_copy_to = true;
      continue;
    }
    if (it->permission() == kDeletePermission) {
      has_delete = true;
      continue;
    }

    // No other permissions, so reaching this means
    // MediaGalleriesPermissionData is probably out of sync in some way.
    // Fail so developers notice this.
    NOTREACHED();
    return false;
  }

  return IsValidPermissionSet(has_read, has_copy_to, has_delete);
}

PermissionMessages MediaGalleriesPermission::GetMessages() const {
  DCHECK(HasMessages());
  PermissionMessages result;

  bool has_all_auto_detected = false;
  bool has_read = false;
  bool has_copy_to = false;
  bool has_delete = false;

  for (std::set<MediaGalleriesPermissionData>::const_iterator it =
      data_set_.begin(); it != data_set_.end(); ++it) {
    if (it->permission() == kAllAutoDetectedPermission)
      has_all_auto_detected = true;
    else if (it->permission() == kReadPermission)
      has_read = true;
    else if (it->permission() == kCopyToPermission)
      has_copy_to = true;
    else if (it->permission() == kDeletePermission)
      has_delete = true;
  }

  if (!IsValidPermissionSet(has_read, has_copy_to, has_delete)) {
    NOTREACHED();
    return result;
  }

  // If |has_all_auto_detected| is false, then Chrome will prompt the user at
  // runtime when the extension call the getMediaGalleries API.
  if (!has_all_auto_detected)
    return result;
  // No access permission case.
  if (!has_read)
    return result;

  // Separate PermissionMessage IDs for read, copyTo, and delete. Otherwise an
  // extension can silently gain new access capabilities.
  result.push_back(PermissionMessage(
      PermissionMessage::kMediaGalleriesAllGalleriesRead,
      l10n_util::GetStringUTF16(
          IDS_EXTENSION_PROMPT_WARNING_MEDIA_GALLERIES_READ)));

  // For copyTo and delete, the proper combined permission message will be
  // derived in ChromePermissionMessageProvider::GetWarningMessages(), such
  // that the user get 1 entry for all media galleries access permissions,
  // rather than several separate entries.
  if (has_copy_to) {
    result.push_back(PermissionMessage(
        PermissionMessage::kMediaGalleriesAllGalleriesCopyTo,
        base::string16()));
  }
  if (has_delete) {
    result.push_back(PermissionMessage(
        PermissionMessage::kMediaGalleriesAllGalleriesDelete,
        base::string16()));
  }
  return result;
}

}  // namespace extensions
