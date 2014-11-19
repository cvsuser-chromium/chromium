// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/search/instant_page.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search/search.h"
#include "chrome/browser/ui/search/search_model.h"
#include "chrome/browser/ui/search/search_tab_helper.h"
#include "chrome/common/render_messages.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_details.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/frame_navigate_params.h"

InstantPage::Delegate::~Delegate() {
}

InstantPage::~InstantPage() {
  if (contents())
    SearchTabHelper::FromWebContents(contents())->model()->RemoveObserver(this);
}

bool InstantPage::supports_instant() const {
  return contents() ?
      SearchTabHelper::FromWebContents(contents())->SupportsInstant() : false;
}

const std::string& InstantPage::instant_url() const {
  return instant_url_;
}

bool InstantPage::IsLocal() const {
  return contents() &&
      contents()->GetURL() == GURL(chrome::kChromeSearchLocalNtpUrl);
}

InstantPage::InstantPage(Delegate* delegate, const std::string& instant_url,
                         Profile* profile, bool is_incognito)
    : profile_(profile),
      delegate_(delegate),
      ipc_sender_(InstantIPCSender::Create(is_incognito)),
      instant_url_(instant_url),
      is_incognito_(is_incognito) {
}

void InstantPage::SetContents(content::WebContents* web_contents) {
  ClearContents();

  if (!web_contents)
    return;

  sender()->SetContents(web_contents);
  Observe(web_contents);
  SearchModel* model = SearchTabHelper::FromWebContents(contents())->model();
  model->AddObserver(this);

  // Already know whether the page supports instant.
  if (model->instant_support() != INSTANT_SUPPORT_UNKNOWN)
    InstantSupportDetermined(model->instant_support() == INSTANT_SUPPORT_YES);
}

bool InstantPage::ShouldProcessAboutToNavigateMainFrame() {
  return false;
}

void InstantPage::DidCommitProvisionalLoadForFrame(
    int64 /* frame_id */,
    const string16& frame_unique_name,
    bool is_main_frame,
    const GURL& url,
    content::PageTransition /* transition_type */,
    content::RenderViewHost* /* render_view_host */) {
  if (is_main_frame && ShouldProcessAboutToNavigateMainFrame())
    delegate_->InstantPageAboutToNavigateMainFrame(contents(), url);
}

void InstantPage::DidNavigateMainFrame(
    const content::LoadCommittedDetails& details,
    const content::FrameNavigateParams& /* params */) {
  // A 204 can be sent by the search provider as a lightweight signal
  // to fall back to the local page, and we obviously want to fall back
  // if we get any response code that indicates an error.
  if (details.http_status_code == 204 || details.http_status_code >= 400)
    delegate_->InstantPageLoadFailed(contents());
}

void InstantPage::DidFailProvisionalLoad(
    int64 /* frame_id */,
    const string16& frame_unique_name,
    bool is_main_frame,
    const GURL& /* validated_url */,
    int /* error_code */,
    const string16& /* error_description */,
    content::RenderViewHost* /* render_view_host */) {
  if (is_main_frame)
    delegate_->InstantPageLoadFailed(contents());
}

void InstantPage::ModelChanged(const SearchModel::State& old_state,
                               const SearchModel::State& new_state) {
  if (old_state.instant_support != new_state.instant_support)
    InstantSupportDetermined(new_state.instant_support == INSTANT_SUPPORT_YES);
}

void InstantPage::InstantSupportDetermined(bool supports_instant) {
  delegate_->InstantSupportDetermined(contents(), supports_instant);

  // If the page doesn't support Instant, stop listening to it.
  if (!supports_instant)
    ClearContents();
}

void InstantPage::ClearContents() {
  if (contents())
    SearchTabHelper::FromWebContents(contents())->model()->RemoveObserver(this);

  sender()->SetContents(NULL);
  Observe(NULL);
}
