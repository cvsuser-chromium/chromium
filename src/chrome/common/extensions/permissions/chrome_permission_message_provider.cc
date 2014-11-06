// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/common/extensions/permissions/chrome_permission_message_provider.h"

#include "base/stl_util.h"
#include "chrome/common/extensions/permissions/permission_message_util.h"
#include "extensions/common/extensions_client.h"
#include "extensions/common/permissions/permission_message.h"
#include "extensions/common/permissions/permission_set.h"
#include "extensions/common/url_pattern_set.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"

namespace extensions {

ChromePermissionMessageProvider::ChromePermissionMessageProvider() {
}

ChromePermissionMessageProvider::~ChromePermissionMessageProvider() {
}

// static
PermissionMessages ChromePermissionMessageProvider::GetPermissionMessages(
    const PermissionSet* permissions,
    Manifest::Type extension_type) const {
  PermissionMessages messages;

  if (permissions->HasEffectiveFullAccess()) {
    messages.push_back(PermissionMessage(
        PermissionMessage::kFullAccess,
        l10n_util::GetStringUTF16(IDS_EXTENSION_PROMPT_WARNING_FULL_ACCESS)));
    return messages;
  }

  std::set<PermissionMessage> host_msgs =
      GetHostPermissionMessages(permissions, extension_type);
  std::set<PermissionMessage> api_msgs = GetAPIPermissionMessages(permissions);
  messages.insert(messages.end(), host_msgs.begin(), host_msgs.end());
  messages.insert(messages.end(), api_msgs.begin(), api_msgs.end());

  return messages;
}

// static
std::vector<string16> ChromePermissionMessageProvider::GetWarningMessages(
    const PermissionSet* permissions,
    Manifest::Type extension_type) const {
  std::vector<string16> message_strings;
  PermissionMessages messages =
      GetPermissionMessages(permissions, extension_type);

  bool audio_capture = false;
  bool video_capture = false;
  bool media_galleries_read = false;
  bool media_galleries_copy_to = false;
  bool media_galleries_delete = false;
  for (PermissionMessages::const_iterator i = messages.begin();
       i != messages.end(); ++i) {
    switch (i->id()) {
      case PermissionMessage::kAudioCapture:
        audio_capture = true;
        break;
      case PermissionMessage::kVideoCapture:
        video_capture = true;
        break;
      case PermissionMessage::kMediaGalleriesAllGalleriesRead:
        media_galleries_read = true;
        break;
      case PermissionMessage::kMediaGalleriesAllGalleriesCopyTo:
        media_galleries_copy_to = true;
        break;
      case PermissionMessage::kMediaGalleriesAllGalleriesDelete:
        media_galleries_delete = true;
        break;
      default:
        break;
    }
  }

  for (PermissionMessages::const_iterator i = messages.begin();
       i != messages.end(); ++i) {
    int id = i->id();
    if (audio_capture && video_capture) {
      if (id == PermissionMessage::kAudioCapture) {
        message_strings.push_back(l10n_util::GetStringUTF16(
            IDS_EXTENSION_PROMPT_WARNING_AUDIO_AND_VIDEO_CAPTURE));
        continue;
      } else if (id == PermissionMessage::kVideoCapture) {
        // The combined message will be pushed above.
        continue;
      }
    }
    if (media_galleries_read &&
        (media_galleries_copy_to || media_galleries_delete)) {
      if (id == PermissionMessage::kMediaGalleriesAllGalleriesRead) {
        int m_id = media_galleries_copy_to ?
            IDS_EXTENSION_PROMPT_WARNING_MEDIA_GALLERIES_READ_WRITE :
            IDS_EXTENSION_PROMPT_WARNING_MEDIA_GALLERIES_READ_DELETE;
        message_strings.push_back(l10n_util::GetStringUTF16(m_id));
        continue;
      } else if (id == PermissionMessage::kMediaGalleriesAllGalleriesCopyTo ||
                 id == PermissionMessage::kMediaGalleriesAllGalleriesDelete) {
        // The combined message will be pushed above.
        continue;
      }
    }

    message_strings.push_back(i->message());
  }

  return message_strings;
}

// static
std::vector<string16>
ChromePermissionMessageProvider::GetWarningMessagesDetails(
    const PermissionSet* permissions,
    Manifest::Type extension_type) const {
  std::vector<string16> message_strings;
  PermissionMessages messages =
      GetPermissionMessages(permissions, extension_type);

  for (PermissionMessages::const_iterator i = messages.begin();
       i != messages.end(); ++i)
    message_strings.push_back(i->details());

  return message_strings;
}

// static
bool ChromePermissionMessageProvider::IsPrivilegeIncrease(
    const PermissionSet* old_permissions,
    const PermissionSet* new_permissions,
    Manifest::Type extension_type) const {
  // Things can't get worse than native code access.
  if (old_permissions->HasEffectiveFullAccess())
    return false;

  // Otherwise, it's a privilege increase if the new one has full access.
  if (new_permissions->HasEffectiveFullAccess())
    return true;

  if (IsHostPrivilegeIncrease(old_permissions, new_permissions, extension_type))
    return true;

  if (IsAPIPrivilegeIncrease(old_permissions, new_permissions))
    return true;

  return false;
}

std::set<PermissionMessage>
ChromePermissionMessageProvider::GetAPIPermissionMessages(
    const PermissionSet* permissions) const {
  std::set<PermissionMessage> messages;
  for (APIPermissionSet::const_iterator permission_it =
           permissions->apis().begin();
       permission_it != permissions->apis().end(); ++permission_it) {
    if (permission_it->HasMessages()) {
      PermissionMessages new_messages = permission_it->GetMessages();
      messages.insert(new_messages.begin(), new_messages.end());
    }
  }

  // A special hack: If kFileSystemWriteDirectory would be displayed, hide
  // kFileSystemDirectory and and kFileSystemWrite as the write directory
  // message implies the other two.
  // TODO(sammc): Remove this. See http://crbug.com/284849.
  std::set<PermissionMessage>::iterator write_directory_message =
      messages.find(PermissionMessage(
          PermissionMessage::kFileSystemWriteDirectory, string16()));
  if (write_directory_message != messages.end()) {
    messages.erase(
        PermissionMessage(PermissionMessage::kFileSystemWrite, string16()));
    messages.erase(
        PermissionMessage(PermissionMessage::kFileSystemDirectory, string16()));
  }

  // A special hack: The warning message for declarativeWebRequest
  // permissions speaks about blocking parts of pages, which is a
  // subset of what the "<all_urls>" access allows. Therefore we
  // display only the "<all_urls>" warning message if both permissions
  // are required.
  if (permissions->HasEffectiveAccessToAllHosts()) {
    messages.erase(
        PermissionMessage(
            PermissionMessage::kDeclarativeWebRequest, string16()));
  }

  return messages;
}

std::set<PermissionMessage>
ChromePermissionMessageProvider::GetHostPermissionMessages(
    const PermissionSet* permissions,
    Manifest::Type extension_type) const {
  std::set<PermissionMessage> messages;
  // Since platform apps always use isolated storage, they can't (silently)
  // access user data on other domains, so there's no need to prompt.
  // Note: this must remain consistent with IsHostPrivilegeIncrease.
  // See crbug.com/255229.
  if (extension_type == Manifest::TYPE_PLATFORM_APP)
    return messages;

  if (permissions->HasEffectiveAccessToAllHosts()) {
    messages.insert(PermissionMessage(
        PermissionMessage::kHostsAll,
        l10n_util::GetStringUTF16(IDS_EXTENSION_PROMPT_WARNING_ALL_HOSTS)));
  } else {
    URLPatternSet regular_hosts;
    ExtensionsClient::Get()->FilterHostPermissions(
        permissions->effective_hosts(), &regular_hosts, &messages);

    std::set<std::string> hosts =
        permission_message_util::GetDistinctHosts(regular_hosts, true, true);
    if (!hosts.empty())
      messages.insert(permission_message_util::CreateFromHostList(hosts));
  }
  return messages;
}

bool ChromePermissionMessageProvider::IsAPIPrivilegeIncrease(
    const PermissionSet* old_permissions,
    const PermissionSet* new_permissions) const {
  if (new_permissions == NULL)
    return false;

  typedef std::set<PermissionMessage> PermissionMsgSet;
  PermissionMsgSet old_warnings = GetAPIPermissionMessages(old_permissions);
  PermissionMsgSet new_warnings = GetAPIPermissionMessages(new_permissions);
  PermissionMsgSet delta_warnings =
      base::STLSetDifference<PermissionMsgSet>(new_warnings, old_warnings);

  // A special hack: kFileSystemWriteDirectory implies kFileSystemDirectory and
  // kFileSystemWrite.
  // TODO(sammc): Remove this. See http://crbug.com/284849.
  if (old_warnings.find(PermissionMessage(
          PermissionMessage::kFileSystemWriteDirectory, string16())) !=
      old_warnings.end()) {
    delta_warnings.erase(
        PermissionMessage(PermissionMessage::kFileSystemDirectory, string16()));
    delta_warnings.erase(
        PermissionMessage(PermissionMessage::kFileSystemWrite, string16()));
  }

  // We have less privileges if there are additional warnings present.
  return !delta_warnings.empty();
}

bool ChromePermissionMessageProvider::IsHostPrivilegeIncrease(
    const PermissionSet* old_permissions,
    const PermissionSet* new_permissions,
    Manifest::Type extension_type) const {
  // Platform apps host permission changes do not count as privilege increases.
  // Note: this must remain consistent with GetHostPermissionMessages.
  if (extension_type == Manifest::TYPE_PLATFORM_APP)
    return false;

  // If the old permission set can access any host, then it can't be elevated.
  if (old_permissions->HasEffectiveAccessToAllHosts())
    return false;

  // Likewise, if the new permission set has full host access, then it must be
  // a privilege increase.
  if (new_permissions->HasEffectiveAccessToAllHosts())
    return true;

  const URLPatternSet& old_list = old_permissions->effective_hosts();
  const URLPatternSet& new_list = new_permissions->effective_hosts();

  // TODO(jstritar): This is overly conservative with respect to subdomains.
  // For example, going from *.google.com to www.google.com will be
  // considered an elevation, even though it is not (http://crbug.com/65337).
  std::set<std::string> new_hosts_set(
      permission_message_util::GetDistinctHosts(new_list, false, false));
  std::set<std::string> old_hosts_set(
      permission_message_util::GetDistinctHosts(old_list, false, false));
  std::set<std::string> new_hosts_only =
      base::STLSetDifference<std::set<std::string> >(new_hosts_set,
                                                     old_hosts_set);

  return !new_hosts_only.empty();
}

}  // namespace extensions
