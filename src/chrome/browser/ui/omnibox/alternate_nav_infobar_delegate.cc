// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/omnibox/alternate_nav_infobar_delegate.h"

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/history/history_service.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/history/shortcuts_backend.h"
#include "chrome/browser/history/shortcuts_backend_factory.h"
#include "chrome/browser/infobars/infobar_service.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/web_contents.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "ui/base/l10n/l10n_util.h"


// static
void AlternateNavInfoBarDelegate::Create(
    content::WebContents* web_contents,
    const string16& text,
    const AutocompleteMatch& match,
    const GURL& search_url) {
  InfoBarService* infobar_service =
      InfoBarService::FromWebContents(web_contents);
  infobar_service->AddInfoBar(scoped_ptr<InfoBarDelegate>(
      new AlternateNavInfoBarDelegate(
          infobar_service,
          Profile::FromBrowserContext(web_contents->GetBrowserContext()), text,
          match, search_url)));
}

AlternateNavInfoBarDelegate::AlternateNavInfoBarDelegate(
    InfoBarService* owner,
    Profile* profile,
    const string16& text,
    const AutocompleteMatch& match,
    const GURL& search_url)
    : InfoBarDelegate(owner),
      profile_(profile),
      text_(text),
      match_(match),
      search_url_(search_url) {
  DCHECK(match_.destination_url.is_valid());
  DCHECK(search_url_.is_valid());
}

AlternateNavInfoBarDelegate::~AlternateNavInfoBarDelegate() {
}

string16 AlternateNavInfoBarDelegate::GetMessageTextWithOffset(
    size_t* link_offset) const {
  const string16 label = l10n_util::GetStringFUTF16(
      IDS_ALTERNATE_NAV_URL_VIEW_LABEL, string16(), link_offset);
  return label;
}

string16 AlternateNavInfoBarDelegate::GetLinkText() const {
  return UTF8ToUTF16(match_.destination_url.spec());
}

bool AlternateNavInfoBarDelegate::LinkClicked(
    WindowOpenDisposition disposition) {
  // Tell the shortcuts backend to remove the shortcut it added for the original
  // search and instead add one reflecting this navigation.
  scoped_refptr<history::ShortcutsBackend> shortcuts_backend(
      ShortcutsBackendFactory::GetForProfile(profile_));
  if (shortcuts_backend) {  // May be NULL in incognito.
    shortcuts_backend->DeleteShortcutsWithUrl(search_url_);
    shortcuts_backend->AddOrUpdateShortcut(text_, match_);
  }

  // Tell the history system to remove any saved search term for the search.
  HistoryService* const history_service =
      HistoryServiceFactory::GetForProfile(profile_, Profile::IMPLICIT_ACCESS);
  if (history_service)
    history_service->DeleteKeywordSearchTermForURL(search_url_);

  // Pretend the user typed this URL, so that navigating to it will be the
  // default action when it's typed again in the future.
  web_contents()->OpenURL(content::OpenURLParams(
      match_.destination_url, content::Referrer(), disposition,
      content::PAGE_TRANSITION_TYPED, false));

  // We should always close, even if the navigation did not occur within this
  // WebContents.
  return true;
}

int AlternateNavInfoBarDelegate::GetIconID() const {
  return IDR_INFOBAR_ALT_NAV_URL;
}

InfoBarDelegate::Type AlternateNavInfoBarDelegate::GetInfoBarType() const {
  return PAGE_ACTION_TYPE;
}
