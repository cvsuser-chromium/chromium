// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/search/search_ipc_router.h"

#include "chrome/browser/search/search.h"
#include "chrome/common/render_messages.h"
#include "content/public/browser/web_contents.h"

SearchIPCRouter::SearchIPCRouter(content::WebContents* web_contents,
                                 Delegate* delegate, scoped_ptr<Policy> policy)
    : WebContentsObserver(web_contents),
      delegate_(delegate),
      policy_(policy.Pass()),
      is_active_tab_(false) {
  DCHECK(web_contents);
  DCHECK(delegate);
  DCHECK(policy_.get());
}

SearchIPCRouter::~SearchIPCRouter() {}

void SearchIPCRouter::DetermineIfPageSupportsInstant() {
  Send(new ChromeViewMsg_DetermineIfPageSupportsInstant(routing_id()));
}

void SearchIPCRouter::SetPromoInformation(bool is_app_launcher_enabled) {
  if (!policy_->ShouldSendSetPromoInformation())
    return;

  Send(new ChromeViewMsg_SearchBoxPromoInformation(routing_id(),
                                                   is_app_launcher_enabled));
}

void SearchIPCRouter::SetDisplayInstantResults() {
  if (!policy_->ShouldSendSetDisplayInstantResults())
    return;

  bool is_search_results_page = !chrome::GetSearchTerms(web_contents()).empty();
  Send(new ChromeViewMsg_SearchBoxSetDisplayInstantResults(
       routing_id(),
       is_search_results_page && chrome::ShouldPrefetchSearchResultsOnSRP()));
}

void SearchIPCRouter::SendThemeBackgroundInfo(
    const ThemeBackgroundInfo& theme_info) {
  if (!policy_->ShouldSendThemeBackgroundInfo())
    return;

  Send(new ChromeViewMsg_SearchBoxThemeChanged(routing_id(), theme_info));
}

void SearchIPCRouter::SendMostVisitedItems(
    const std::vector<InstantMostVisitedItem>& items) {
  if (!policy_->ShouldSendMostVisitedItems())
    return;

  Send(new ChromeViewMsg_SearchBoxMostVisitedItemsChanged(
      routing_id(), items));
}

void SearchIPCRouter::SendChromeIdentityCheckResult(
    const string16& identity,
    bool identity_match) {
  if (!policy_->ShouldProcessChromeIdentityCheck())
    return;

  Send(new ChromeViewMsg_ChromeIdentityCheckResult(
      routing_id(),
      identity,
      identity_match));
}

void SearchIPCRouter::SetSuggestionToPrefetch(
    const InstantSuggestion& suggestion) {
  if (!policy_->ShouldSendSetSuggestionToPrefetch())
    return;

  Send(new ChromeViewMsg_SearchBoxSetSuggestionToPrefetch(routing_id(),
                                                          suggestion));
}

void SearchIPCRouter::Submit(const string16& text) {
  if (!policy_->ShouldSubmitQuery())
    return;

  Send(new ChromeViewMsg_SearchBoxSubmit(routing_id(), text));
}

void SearchIPCRouter::OnTabActivated() {
  is_active_tab_ = true;
}

void SearchIPCRouter::OnTabDeactivated() {
  is_active_tab_ = false;
}

bool SearchIPCRouter::OnMessageReceived(const IPC::Message& message) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(SearchIPCRouter, message)
    IPC_MESSAGE_HANDLER(ChromeViewHostMsg_InstantSupportDetermined,
                        OnInstantSupportDetermined)
    IPC_MESSAGE_HANDLER(ChromeViewHostMsg_SetVoiceSearchSupported,
                        OnVoiceSearchSupportDetermined)
    IPC_MESSAGE_HANDLER(ChromeViewHostMsg_FocusOmnibox, OnFocusOmnibox);
    IPC_MESSAGE_HANDLER(ChromeViewHostMsg_SearchBoxNavigate,
                        OnSearchBoxNavigate);
    IPC_MESSAGE_HANDLER(ChromeViewHostMsg_SearchBoxDeleteMostVisitedItem,
                        OnDeleteMostVisitedItem);
    IPC_MESSAGE_HANDLER(ChromeViewHostMsg_SearchBoxUndoMostVisitedDeletion,
                        OnUndoMostVisitedDeletion);
    IPC_MESSAGE_HANDLER(ChromeViewHostMsg_SearchBoxUndoAllMostVisitedDeletions,
                        OnUndoAllMostVisitedDeletions);
    IPC_MESSAGE_HANDLER(ChromeViewHostMsg_LogEvent, OnLogEvent);
    IPC_MESSAGE_HANDLER(ChromeViewHostMsg_PasteAndOpenDropdown,
                        OnPasteAndOpenDropDown);
    IPC_MESSAGE_HANDLER(ChromeViewHostMsg_ChromeIdentityCheck,
                        OnChromeIdentityCheck);
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void SearchIPCRouter::OnInstantSupportDetermined(int page_id,
                                                 bool instant_support) const {
  if (!web_contents()->IsActiveEntry(page_id))
    return;

  delegate_->OnInstantSupportDetermined(instant_support);
}

void SearchIPCRouter::OnVoiceSearchSupportDetermined(
    int page_id,
    bool supports_voice_search) const {
  if (!web_contents()->IsActiveEntry(page_id))
    return;

  delegate_->OnInstantSupportDetermined(true);
  if (!policy_->ShouldProcessSetVoiceSearchSupport())
    return;

  delegate_->OnSetVoiceSearchSupport(supports_voice_search);
}

void SearchIPCRouter::OnFocusOmnibox(int page_id,
                                     OmniboxFocusState state) const {
  if (!web_contents()->IsActiveEntry(page_id))
    return;

  delegate_->OnInstantSupportDetermined(true);
  if (!policy_->ShouldProcessFocusOmnibox(is_active_tab_))
    return;

  delegate_->FocusOmnibox(state);
}

void SearchIPCRouter::OnSearchBoxNavigate(
    int page_id,
    const GURL& url,
    WindowOpenDisposition disposition,
    bool is_most_visited_item_url) const {
  if (!web_contents()->IsActiveEntry(page_id))
    return;

  delegate_->OnInstantSupportDetermined(true);
  if (!policy_->ShouldProcessNavigateToURL(is_active_tab_))
    return;

  delegate_->NavigateToURL(url, disposition, is_most_visited_item_url);
}

void SearchIPCRouter::OnDeleteMostVisitedItem(int page_id,
                                              const GURL& url) const {
  if (!web_contents()->IsActiveEntry(page_id))
    return;

  delegate_->OnInstantSupportDetermined(true);
  if (!policy_->ShouldProcessDeleteMostVisitedItem())
    return;

  delegate_->OnDeleteMostVisitedItem(url);
}

void SearchIPCRouter::OnUndoMostVisitedDeletion(int page_id,
                                                const GURL& url) const {
  if (!web_contents()->IsActiveEntry(page_id))
    return;

  delegate_->OnInstantSupportDetermined(true);
  if (!policy_->ShouldProcessUndoMostVisitedDeletion())
    return;

  delegate_->OnUndoMostVisitedDeletion(url);
}

void SearchIPCRouter::OnUndoAllMostVisitedDeletions(int page_id) const {
  if (!web_contents()->IsActiveEntry(page_id))
    return;

  delegate_->OnInstantSupportDetermined(true);
  if (!policy_->ShouldProcessUndoAllMostVisitedDeletions())
    return;

  delegate_->OnUndoAllMostVisitedDeletions();
}

void SearchIPCRouter::OnLogEvent(int page_id, NTPLoggingEventType event) const {
  if (!web_contents()->IsActiveEntry(page_id))
    return;

  delegate_->OnInstantSupportDetermined(true);
  if (!policy_->ShouldProcessLogEvent())
    return;

  delegate_->OnLogEvent(event);
}

void SearchIPCRouter::OnPasteAndOpenDropDown(int page_id,
                                             const string16& text) const {
  if (!web_contents()->IsActiveEntry(page_id))
    return;

  delegate_->OnInstantSupportDetermined(true);
  if (!policy_->ShouldProcessPasteIntoOmnibox(is_active_tab_))
    return;

  delegate_->PasteIntoOmnibox(text);
}

void SearchIPCRouter::OnChromeIdentityCheck(int page_id,
                                            const string16& identity) const {
  if (!web_contents()->IsActiveEntry(page_id))
    return;

  delegate_->OnInstantSupportDetermined(true);
  if (!policy_->ShouldProcessChromeIdentityCheck())
    return;

  delegate_->OnChromeIdentityCheck(identity);
}

void SearchIPCRouter::set_delegate(Delegate* delegate) {
  DCHECK(delegate);
  delegate_ = delegate;
}

void SearchIPCRouter::set_policy(scoped_ptr<Policy> policy) {
  DCHECK(policy.get());
  policy_.reset(policy.release());
}
