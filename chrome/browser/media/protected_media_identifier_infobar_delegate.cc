// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/media/protected_media_identifier_infobar_delegate.h"

#include "chrome/browser/content_settings/permission_queue_controller.h"
#include "content/public/browser/navigation_details.h"
#include "content/public/browser/navigation_entry.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "net/base/net_util.h"
#include "ui/base/l10n/l10n_util.h"

#if defined(OS_ANDROID)
#include "chrome/browser/android/chromium_application.h"
#endif

// static
InfoBarDelegate* ProtectedMediaIdentifierInfoBarDelegate::Create(
    InfoBarService* infobar_service,
    PermissionQueueController* controller,
    const PermissionRequestID& id,
    const GURL& requesting_frame,
    const std::string& display_languages) {
  const content::NavigationEntry* committed_entry =
      infobar_service->web_contents()->GetController().GetLastCommittedEntry();
  return infobar_service->AddInfoBar(
      scoped_ptr<InfoBarDelegate>(new ProtectedMediaIdentifierInfoBarDelegate(
          infobar_service,
          controller,
          id,
          requesting_frame,
          committed_entry ? committed_entry->GetUniqueID() : 0,
          display_languages)));
}


ProtectedMediaIdentifierInfoBarDelegate::
    ProtectedMediaIdentifierInfoBarDelegate(
        InfoBarService* infobar_service,
        PermissionQueueController* controller,
        const PermissionRequestID& id,
        const GURL& requesting_frame,
        int contents_unique_id,
        const std::string& display_languages)
    : ConfirmInfoBarDelegate(infobar_service),
      controller_(controller),
      id_(id),
      requesting_frame_(requesting_frame),
      contents_unique_id_(contents_unique_id),
      display_languages_(display_languages) {}

ProtectedMediaIdentifierInfoBarDelegate::
    ~ProtectedMediaIdentifierInfoBarDelegate() {}

bool ProtectedMediaIdentifierInfoBarDelegate::Accept() {
  SetPermission(true, true);
  return true;
}

void ProtectedMediaIdentifierInfoBarDelegate::SetPermission(
    bool update_content_setting,
    bool allowed) {
  controller_->OnPermissionSet(id_,
                               requesting_frame_,
                               web_contents()->GetLastCommittedURL(),
                               update_content_setting,
                               allowed);
}

void ProtectedMediaIdentifierInfoBarDelegate::InfoBarDismissed() {
  SetPermission(false, false);
}

int ProtectedMediaIdentifierInfoBarDelegate::GetIconID() const {
  return IDR_PROTECTED_MEDIA_IDENTIFIER_INFOBAR_ICON;
}

InfoBarDelegate::Type ProtectedMediaIdentifierInfoBarDelegate::GetInfoBarType()
    const {
  return PAGE_ACTION_TYPE;
}

bool ProtectedMediaIdentifierInfoBarDelegate::ShouldExpireInternal(
    const content::LoadCommittedDetails& details) const {
  // This implementation matches InfoBarDelegate::ShouldExpireInternal(), but
  // uses the unique ID we set in the constructor instead of that stored in the
  // base class.
  return (contents_unique_id_ != details.entry->GetUniqueID()) ||
      (content::PageTransitionStripQualifier(
          details.entry->GetTransitionType()) ==
              content::PAGE_TRANSITION_RELOAD);
}

string16 ProtectedMediaIdentifierInfoBarDelegate::GetMessageText() const {
  return l10n_util::GetStringFUTF16(
      IDS_PROTECTED_MEDIA_IDENTIFIER_INFOBAR_QUESTION,
      net::FormatUrl(requesting_frame_.GetOrigin(), display_languages_));
}

string16 ProtectedMediaIdentifierInfoBarDelegate::GetButtonLabel(
    InfoBarButton button) const {
  return l10n_util::GetStringUTF16(
      (button == BUTTON_OK) ? IDS_PROTECTED_MEDIA_IDENTIFIER_ALLOW_BUTTON
                            : IDS_PROTECTED_MEDIA_IDENTIFIER_DENY_BUTTON);
}

bool ProtectedMediaIdentifierInfoBarDelegate::Cancel() {
  SetPermission(true, false);
  return true;
}

string16 ProtectedMediaIdentifierInfoBarDelegate::GetLinkText() const {
#if defined(OS_ANDROID)
  return l10n_util::GetStringUTF16(
      IDS_PROTECTED_MEDIA_IDENTIFIER_SETTINGS_LINK);
#else
  NOTIMPLEMENTED();
  return string16();
#endif
}

bool ProtectedMediaIdentifierInfoBarDelegate::LinkClicked(
    WindowOpenDisposition disposition) {
#if defined(OS_ANDROID)
  chrome::android::ChromiumApplication::OpenProtectedContentSettings();
#else
  NOTIMPLEMENTED();
#endif
  return false; // Do not dismiss the info bar.
}
