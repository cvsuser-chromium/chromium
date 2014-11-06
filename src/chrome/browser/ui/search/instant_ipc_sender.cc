// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/search/instant_ipc_sender.h"

#include "chrome/common/render_messages.h"

namespace {

// Implementation for regular profiles.
class InstantIPCSenderImpl : public InstantIPCSender {
 public:
  InstantIPCSenderImpl() {}
  virtual ~InstantIPCSenderImpl() {}

 private:
  virtual void SetOmniboxBounds(const gfx::Rect& bounds) OVERRIDE {
    Send(new ChromeViewMsg_SearchBoxMarginChange(
        routing_id(), bounds.x(), bounds.width()));
  }

  virtual void FocusChanged(OmniboxFocusState state,
                    OmniboxFocusChangeReason reason) OVERRIDE {
    Send(new ChromeViewMsg_SearchBoxFocusChanged(routing_id(), state, reason));
  }

  virtual void SetInputInProgress(bool input_in_progress) OVERRIDE {
    Send(new ChromeViewMsg_SearchBoxSetInputInProgress(
        routing_id(), input_in_progress));
  }

  virtual void ToggleVoiceSearch() OVERRIDE {
    Send(new ChromeViewMsg_SearchBoxToggleVoiceSearch(routing_id()));
  }

  DISALLOW_COPY_AND_ASSIGN(InstantIPCSenderImpl);
};

// Implementation for incognito profiles.
class IncognitoInstantIPCSenderImpl : public InstantIPCSender {
 public:
  IncognitoInstantIPCSenderImpl() {}
  virtual ~IncognitoInstantIPCSenderImpl() {}

 private:
  virtual void SetOmniboxBounds(const gfx::Rect& bounds) OVERRIDE {
    Send(new ChromeViewMsg_SearchBoxMarginChange(
        routing_id(), bounds.x(), bounds.width()));
  }

  virtual void ToggleVoiceSearch() OVERRIDE {
    Send(new ChromeViewMsg_SearchBoxToggleVoiceSearch(routing_id()));
  }

  DISALLOW_COPY_AND_ASSIGN(IncognitoInstantIPCSenderImpl);
};

}  // anonymous namespace

// static
scoped_ptr<InstantIPCSender> InstantIPCSender::Create(bool is_incognito) {
  scoped_ptr<InstantIPCSender> sender(
      is_incognito ?
      static_cast<InstantIPCSender*>(new IncognitoInstantIPCSenderImpl()) :
      static_cast<InstantIPCSender*>(new InstantIPCSenderImpl()));
  return sender.Pass();
}

void InstantIPCSender::SetContents(content::WebContents* web_contents) {
  Observe(web_contents);
}
