// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/apps/app_browsertest_util.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/extensions/extension_test_message_listener.h"
#include "content/public/browser/notification_service.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/test_utils.h"

using extensions::Extension;
using extensions::PlatformAppBrowserTest;

namespace {

class AppEventPageTest : public PlatformAppBrowserTest {
 protected:
  void TestUnloadEventPage(const char* app_path) {
    // Load and launch the app.
    ExtensionTestMessageListener launched_listener("launched", false);
    const Extension* extension = LoadAndLaunchPlatformApp(app_path);
    ASSERT_TRUE(extension);
    ASSERT_TRUE(launched_listener.WaitUntilSatisfied());

    content::WindowedNotificationObserver event_page_suspended(
        chrome::NOTIFICATION_EXTENSION_HOST_DESTROYED,
        content::NotificationService::AllSources());

    // Close the app window.
    EXPECT_EQ(1U, GetShellWindowCount());
    apps::ShellWindow* shell_window = GetFirstShellWindow();
    ASSERT_TRUE(shell_window);
    CloseShellWindow(shell_window);

    // Verify that the event page is destroyed.
    event_page_suspended.Wait();
  }
};

}  // namespace

// Tests that an app's event page will eventually be unloaded. The onSuspend
// event handler of this app does not make any API calls.
IN_PROC_BROWSER_TEST_F(AppEventPageTest, OnSuspendNoApiUse) {
  TestUnloadEventPage("event_page/suspend_simple");
}

// Tests that an app's event page will eventually be unloaded. The onSuspend
// event handler of this app calls a chrome.storage API function.
// See: http://crbug.com/296834
IN_PROC_BROWSER_TEST_F(AppEventPageTest, OnSuspendUseStorageApi) {
  TestUnloadEventPage("event_page/suspend_storage_api");
}
