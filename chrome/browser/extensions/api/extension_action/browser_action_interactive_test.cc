// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/browser_action_test_util.h"
#include "chrome/browser/extensions/extension_action.h"
#include "chrome/browser/extensions/extension_action_manager.h"
#include "chrome/browser/extensions/extension_apitest.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/extensions/extension_tab_util.h"
#include "chrome/browser/extensions/extension_test_message_listener.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/extensions/permissions/permissions_data.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/web_contents.h"

namespace extensions {
namespace {

// chrome.browserAction API tests that interact with the UI in such a way that
// they cannot be run concurrently (i.e. openPopup API tests that require the
// window be focused/active).
class BrowserActionInteractiveTest : public ExtensionApiTest {
 public:
  BrowserActionInteractiveTest() {}
  virtual ~BrowserActionInteractiveTest() {}

 protected:
  // Function to control whether to run popup tests for the current platform.
  // These tests require RunExtensionSubtest to work as expected and the browser
  // window to able to be made active automatically. Returns false for platforms
  // where these conditions are not met.
  bool ShouldRunPopupTest() {
    // TODO(justinlin): http://crbug.com/177163
#if defined(OS_WIN) && !defined(NDEBUG)
    return false;
#elif defined(OS_MACOSX)
    // TODO(justinlin): Browser window do not become active on Mac even when
    // Activate() is called on them. Enable when/if it's possible to fix.
    return false;
#else
    return true;
#endif
  }
};

// Tests opening a popup using the chrome.browserAction.openPopup API. This test
// opens a popup in the starting window, closes the popup, creates a new window
// and opens a popup in the new window. Both popups should succeed in opening.
IN_PROC_BROWSER_TEST_F(BrowserActionInteractiveTest, TestOpenPopup) {
  if (!ShouldRunPopupTest())
    return;

  BrowserActionTestUtil browserActionBar = BrowserActionTestUtil(browser());
  // Setup extension message listener to wait for javascript to finish running.
  ExtensionTestMessageListener listener("ready", true);
  {
    // Setup the notification observer to wait for the popup to finish loading.
    content::WindowedNotificationObserver frame_observer(
        content::NOTIFICATION_LOAD_COMPLETED_MAIN_FRAME,
        content::NotificationService::AllSources());
    // Show first popup in first window and expect it to have loaded.
    ASSERT_TRUE(RunExtensionSubtest("browser_action/open_popup",
                                    "open_popup_succeeds.html")) << message_;
    frame_observer.Wait();
    EXPECT_TRUE(browserActionBar.HasPopup());
    browserActionBar.HidePopup();
  }

  EXPECT_TRUE(listener.WaitUntilSatisfied());
  Browser* new_browser = NULL;
  {
    content::WindowedNotificationObserver frame_observer(
        content::NOTIFICATION_LOAD_COMPLETED_MAIN_FRAME,
        content::NotificationService::AllSources());
    // Open a new window.
    new_browser = chrome::FindBrowserWithWebContents(
        browser()->OpenURL(content::OpenURLParams(
            GURL("about:"), content::Referrer(), NEW_WINDOW,
            content::PAGE_TRANSITION_TYPED, false)));
#if defined(OS_WIN)
    // Hide all the buttons to test that it opens even when browser action is
    // in the overflow bucket.
    // TODO(justinlin): Implement for other platforms.
    browserActionBar.SetIconVisibilityCount(0);
#endif
    frame_observer.Wait();
  }

  EXPECT_TRUE(new_browser != NULL);

// Flaky on non-aura linux http://crbug.com/309749
#if !(defined(OS_LINUX) && !defined(USE_AURA))
  ResultCatcher catcher;
  {
    content::WindowedNotificationObserver frame_observer(
        content::NOTIFICATION_LOAD_COMPLETED_MAIN_FRAME,
        content::NotificationService::AllSources());
    // Show second popup in new window.
    listener.Reply("");
    frame_observer.Wait();
    EXPECT_TRUE(BrowserActionTestUtil(new_browser).HasPopup());
  }
  ASSERT_TRUE(catcher.GetNextResult()) << message_;
#endif
}

// Tests opening a popup in an incognito window.
IN_PROC_BROWSER_TEST_F(BrowserActionInteractiveTest, TestOpenPopupIncognito) {
  if (!ShouldRunPopupTest())
    return;

  content::WindowedNotificationObserver frame_observer(
      content::NOTIFICATION_LOAD_COMPLETED_MAIN_FRAME,
      content::NotificationService::AllSources());
  ASSERT_TRUE(RunExtensionSubtest("browser_action/open_popup",
                                  "open_popup_succeeds.html",
                                  kFlagEnableIncognito | kFlagUseIncognito))
      << message_;
  frame_observer.Wait();
  // Non-Aura Linux uses a singleton for the popup, so it looks like all windows
  // have popups if there is any popup open.
#if !(defined(OS_LINUX) && !defined(USE_AURA))
  // Starting window does not have a popup.
  EXPECT_FALSE(BrowserActionTestUtil(browser()).HasPopup());
#endif
  // Incognito window should have a popup.
  EXPECT_TRUE(BrowserActionTestUtil(BrowserList::GetInstance(
      chrome::GetActiveDesktop())->GetLastActive()).HasPopup());
}

#if defined(OS_LINUX)
#define MAYBE_TestOpenPopupDoesNotCloseOtherPopups DISABLED_TestOpenPopupDoesNotCloseOtherPopups
#else
#define MAYBE_TestOpenPopupDoesNotCloseOtherPopups TestOpenPopupDoesNotCloseOtherPopups
#endif
// Tests if there is already a popup open (by a user click or otherwise), that
// the openPopup API does not override it.
IN_PROC_BROWSER_TEST_F(BrowserActionInteractiveTest,
                       MAYBE_TestOpenPopupDoesNotCloseOtherPopups) {
  if (!ShouldRunPopupTest())
    return;

  // Load a first extension that can open a popup.
  ASSERT_TRUE(LoadExtension(test_data_dir_.AppendASCII(
      "browser_action/popup")));
  const Extension* extension = GetSingleLoadedExtension();
  ASSERT_TRUE(extension) << message_;

  ExtensionTestMessageListener listener("ready", true);
  // Load the test extension which will do nothing except notifyPass() to
  // return control here.
  ASSERT_TRUE(RunExtensionSubtest("browser_action/open_popup",
                                  "open_popup_fails.html")) << message_;
  EXPECT_TRUE(listener.WaitUntilSatisfied());

  content::WindowedNotificationObserver frame_observer(
      content::NOTIFICATION_LOAD_COMPLETED_MAIN_FRAME,
      content::NotificationService::AllSources());
  // Open popup in the first extension.
  BrowserActionTestUtil(browser()).Press(0);
  frame_observer.Wait();
  EXPECT_TRUE(BrowserActionTestUtil(browser()).HasPopup());

  ResultCatcher catcher;
  // Return control to javascript to validate that opening a popup fails now.
  listener.Reply("");
  ASSERT_TRUE(catcher.GetNextResult()) << message_;
}

// Test that openPopup does not grant tab permissions like for browser action
// clicks if the activeTab permission is set.
IN_PROC_BROWSER_TEST_F(BrowserActionInteractiveTest,
                       TestOpenPopupDoesNotGrantTabPermissions) {
  if (!ShouldRunPopupTest())
    return;

  content::WindowedNotificationObserver frame_observer(
      content::NOTIFICATION_LOAD_COMPLETED_MAIN_FRAME,
      content::NotificationService::AllSources());
  ASSERT_TRUE(RunExtensionSubtest("browser_action/open_popup",
                                  "open_popup_succeeds.html")) << message_;
  frame_observer.Wait();

  ExtensionService* service = extensions::ExtensionSystem::Get(
      browser()->profile())->extension_service();
  ASSERT_FALSE(PermissionsData::HasAPIPermissionForTab(
      service->GetExtensionById(last_loaded_extension_id(), false),
      SessionID::IdForTab(browser()->tab_strip_model()->GetActiveWebContents()),
      APIPermission::kTab));
}

}  // namespace
}  // namespace extensions
