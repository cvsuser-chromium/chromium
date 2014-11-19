// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstddef>
#include <string>

#include "base/basictypes.h"
#include "base/memory/scoped_ptr.h"
#include "base/pickle.h"
#include "base/strings/string16.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "chrome/browser/search/search.h"
#include "chrome/browser/sessions/session_types.h"
#include "components/sessions/serialized_navigation_entry_test_helper.h"
#include "content/public/browser/favicon_status.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/common/page_transition_types.h"
#include "content/public/common/referrer.h"
#include "sync/protocol/session_specifics.pb.h"
#include "sync/util/time.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/WebKit/public/platform/WebReferrerPolicy.h"
#include "url/gurl.h"

namespace {

const content::Referrer kReferrer =
    content::Referrer(GURL("http://www.referrer.com"),
                      blink::WebReferrerPolicyAlways);
const GURL kVirtualURL("http://www.virtual-url.com");
const string16 kTitle = ASCIIToUTF16("title");
const content::PageState kPageState =
    content::PageState::CreateFromEncodedData("page state");
const GURL kOriginalRequestURL("http://www.original-request.com");
const base::Time kTimestamp = syncer::ProtoTimeToTime(100);
const string16 kSearchTerms = ASCIIToUTF16("my search terms");
const GURL kFaviconURL("http://virtual-url.com/favicon.ico");

// Create a typical SessionTab protocol buffer and set an existing
// SessionTab from it.  The data from the protocol buffer should
// clobber the existing data.
TEST(SessionTab, FromSyncData) {
  sync_pb::SessionTab sync_data;
  sync_data.set_tab_id(5);
  sync_data.set_window_id(10);
  sync_data.set_tab_visual_index(13);
  sync_data.set_current_navigation_index(3);
  sync_data.set_pinned(true);
  sync_data.set_extension_app_id("app_id");
  for (int i = 0; i < 5; ++i) {
    sync_pb::TabNavigation* navigation = sync_data.add_navigation();
    navigation->set_virtual_url("http://foo/" + base::IntToString(i));
    navigation->set_referrer("referrer");
    navigation->set_title("title");
    navigation->set_page_transition(sync_pb::SyncEnums_PageTransition_TYPED);
  }

  SessionTab tab;
  tab.window_id.set_id(100);
  tab.tab_id.set_id(100);
  tab.tab_visual_index = 100;
  tab.current_navigation_index = 1000;
  tab.pinned = false;
  tab.extension_app_id = "fake";
  tab.user_agent_override = "fake";
  tab.timestamp = base::Time::FromInternalValue(100);
  tab.navigations.resize(100);
  tab.session_storage_persistent_id = "fake";

  tab.SetFromSyncData(sync_data, base::Time::FromInternalValue(5u));
  EXPECT_EQ(10, tab.window_id.id());
  EXPECT_EQ(5, tab.tab_id.id());
  EXPECT_EQ(13, tab.tab_visual_index);
  EXPECT_EQ(3, tab.current_navigation_index);
  EXPECT_TRUE(tab.pinned);
  EXPECT_EQ("app_id", tab.extension_app_id);
  EXPECT_TRUE(tab.user_agent_override.empty());
  EXPECT_EQ(5u, tab.timestamp.ToInternalValue());
  ASSERT_EQ(5u, tab.navigations.size());
  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(i, tab.navigations[i].index());
    EXPECT_EQ(GURL("referrer"), tab.navigations[i].referrer().url);
    EXPECT_EQ(string16(ASCIIToUTF16("title")), tab.navigations[i].title());
    EXPECT_EQ(content::PAGE_TRANSITION_TYPED,
              tab.navigations[i].transition_type());
    EXPECT_EQ(GURL("http://foo/" + base::IntToString(i)),
              tab.navigations[i].virtual_url());
  }
  EXPECT_TRUE(tab.session_storage_persistent_id.empty());
}

TEST(SessionTab, ToSyncData) {
  SessionTab tab;
  tab.window_id.set_id(10);
  tab.tab_id.set_id(5);
  tab.tab_visual_index = 13;
  tab.current_navigation_index = 3;
  tab.pinned = true;
  tab.extension_app_id = "app_id";
  tab.user_agent_override = "fake";
  tab.timestamp = base::Time::FromInternalValue(100);
  for (int i = 0; i < 5; ++i) {
    tab.navigations.push_back(
        sessions::SerializedNavigationEntryTestHelper::CreateNavigation(
            "http://foo/" + base::IntToString(i), "title"));
  }
  tab.session_storage_persistent_id = "fake";

  const sync_pb::SessionTab& sync_data = tab.ToSyncData();
  EXPECT_EQ(5, sync_data.tab_id());
  EXPECT_EQ(10, sync_data.window_id());
  EXPECT_EQ(13, sync_data.tab_visual_index());
  EXPECT_EQ(3, sync_data.current_navigation_index());
  EXPECT_TRUE(sync_data.pinned());
  EXPECT_EQ("app_id", sync_data.extension_app_id());
  ASSERT_EQ(5, sync_data.navigation_size());
  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(tab.navigations[i].virtual_url().spec(),
              sync_data.navigation(i).virtual_url());
    EXPECT_EQ(UTF16ToUTF8(tab.navigations[i].title()),
              sync_data.navigation(i).title());
  }
  EXPECT_FALSE(sync_data.has_favicon());
  EXPECT_FALSE(sync_data.has_favicon_type());
  EXPECT_FALSE(sync_data.has_favicon_source());
}

}  // namespace
