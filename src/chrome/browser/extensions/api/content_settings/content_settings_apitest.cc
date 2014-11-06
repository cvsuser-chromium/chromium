// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/prefs/pref_service.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/content_settings/cookie_settings.h"
#include "chrome/browser/content_settings/host_content_settings_map.h"
#include "chrome/browser/extensions/api/content_settings/content_settings_api.h"
#include "chrome/browser/extensions/extension_apitest.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/plugin_service.h"
#include "content/public/common/webplugininfo.h"
#include "content/public/test/test_utils.h"

namespace {

void ReleaseBrowserProcessModule() {
  g_browser_process->ReleaseModule();
}

}  // namespace

namespace extensions {

class ExtensionContentSettingsApiTest : public ExtensionApiTest {
 public:
  ExtensionContentSettingsApiTest() : profile_(NULL) {}

  virtual void SetUpCommandLine(CommandLine* command_line) OVERRIDE {
    ExtensionApiTest::SetUpCommandLine(command_line);
    command_line->AppendSwitch(switches::kDisablePluginsDiscovery);
  }

  virtual void SetUpOnMainThread() OVERRIDE {
    ExtensionApiTest::SetUpOnMainThread();

    // The browser might get closed later (and therefore be destroyed), so we
    // save the profile.
    profile_ = browser()->profile();

    // Closing the last browser window also releases a module reference. Make
    // sure it's not the last one, so the message loop doesn't quit
    // unexpectedly.
    g_browser_process->AddRefModule();
  }

  virtual void CleanUpOnMainThread() OVERRIDE {
    // ReleaseBrowserProcessModule() needs to be called in a message loop, so we
    // post a task to do it, then run the message loop.
    base::MessageLoop::current()->PostTask(
        FROM_HERE, base::Bind(&ReleaseBrowserProcessModule));
    content::RunAllPendingInMessageLoop();

    ExtensionApiTest::CleanUpOnMainThread();
  }

 protected:
  void CheckContentSettingsSet() {
    HostContentSettingsMap* map =
        profile_->GetHostContentSettingsMap();
    CookieSettings* cookie_settings =
        CookieSettings::Factory::GetForProfile(profile_).get();

    // Check default content settings by using an unknown URL.
    GURL example_url("http://www.example.com");
    EXPECT_TRUE(cookie_settings->IsReadingCookieAllowed(
        example_url, example_url));
    EXPECT_TRUE(cookie_settings->IsSettingCookieAllowed(
        example_url, example_url));
    EXPECT_TRUE(cookie_settings->IsCookieSessionOnly(example_url));
    EXPECT_EQ(CONTENT_SETTING_ALLOW,
              map->GetContentSetting(example_url,
                                     example_url,
                                     CONTENT_SETTINGS_TYPE_IMAGES,
                                     std::string()));
    EXPECT_EQ(CONTENT_SETTING_BLOCK,
              map->GetContentSetting(example_url,
                                     example_url,
                                     CONTENT_SETTINGS_TYPE_JAVASCRIPT,
                                     std::string()));
    EXPECT_EQ(CONTENT_SETTING_ALLOW,
              map->GetContentSetting(example_url,
                                     example_url,
                                     CONTENT_SETTINGS_TYPE_PLUGINS,
                                     std::string()));
    EXPECT_EQ(CONTENT_SETTING_BLOCK,
              map->GetContentSetting(example_url,
                                     example_url,
                                     CONTENT_SETTINGS_TYPE_POPUPS,
                                     std::string()));
#if 0
    // TODO(bauerb): Enable once geolocation settings are integrated into the
    // HostContentSettingsMap.
    EXPECT_EQ(CONTENT_SETTING_ALLOW,
              map->GetContentSetting(example_url,
                                     example_url,
                                     CONTENT_SETTINGS_TYPE_GEOLOCATION,
                                     std::string()));
#endif
    EXPECT_EQ(CONTENT_SETTING_ASK,
              map->GetContentSetting(example_url,
                                     example_url,
                                     CONTENT_SETTINGS_TYPE_NOTIFICATIONS,
                                     std::string()));

    // Check content settings for www.google.com
    GURL url("http://www.google.com");
    EXPECT_FALSE(cookie_settings->IsReadingCookieAllowed(url, url));
    EXPECT_EQ(CONTENT_SETTING_ALLOW,
              map->GetContentSetting(
                  url, url, CONTENT_SETTINGS_TYPE_IMAGES, std::string()));
    EXPECT_EQ(CONTENT_SETTING_BLOCK,
              map->GetContentSetting(
                  url, url, CONTENT_SETTINGS_TYPE_JAVASCRIPT, std::string()));
    EXPECT_EQ(CONTENT_SETTING_BLOCK,
              map->GetContentSetting(
                  url, url, CONTENT_SETTINGS_TYPE_PLUGINS, std::string()));
    EXPECT_EQ(CONTENT_SETTING_ALLOW,
              map->GetContentSetting(
                  url, url, CONTENT_SETTINGS_TYPE_POPUPS, std::string()));
#if 0
    EXPECT_EQ(CONTENT_SETTING_BLOCK,
              map->GetContentSetting(
                  url, url, CONTENT_SETTINGS_TYPE_GEOLOCATION, std::string()));
#endif
    EXPECT_EQ(
        CONTENT_SETTING_BLOCK,
        map->GetContentSetting(
            url, url, CONTENT_SETTINGS_TYPE_NOTIFICATIONS, std::string()));
  }

  void CheckContentSettingsDefault() {
    HostContentSettingsMap* map =
        profile_->GetHostContentSettingsMap();
    CookieSettings* cookie_settings =
        CookieSettings::Factory::GetForProfile(profile_).get();

    // Check content settings for www.google.com
    GURL url("http://www.google.com");
    EXPECT_TRUE(cookie_settings->IsReadingCookieAllowed(url, url));
    EXPECT_TRUE(cookie_settings->IsSettingCookieAllowed(url, url));
    EXPECT_FALSE(cookie_settings->IsCookieSessionOnly(url));
    EXPECT_EQ(CONTENT_SETTING_ALLOW,
              map->GetContentSetting(
                  url, url, CONTENT_SETTINGS_TYPE_IMAGES, std::string()));
    EXPECT_EQ(CONTENT_SETTING_ALLOW,
              map->GetContentSetting(
                  url, url, CONTENT_SETTINGS_TYPE_JAVASCRIPT, std::string()));
    EXPECT_EQ(CONTENT_SETTING_ALLOW,
              map->GetContentSetting(
                  url, url, CONTENT_SETTINGS_TYPE_PLUGINS, std::string()));
    EXPECT_EQ(CONTENT_SETTING_BLOCK,
              map->GetContentSetting(
                  url, url, CONTENT_SETTINGS_TYPE_POPUPS, std::string()));
#if 0
    // TODO(bauerb): Enable once geolocation settings are integrated into the
    // HostContentSettingsMap.
    EXPECT_EQ(CONTENT_SETTING_ALLOW,
              map->GetContentSetting(
                  url, url, CONTENT_SETTINGS_TYPE_GEOLOCATION, std::string()));
#endif
    EXPECT_EQ(
        CONTENT_SETTING_ASK,
        map->GetContentSetting(
            url, url, CONTENT_SETTINGS_TYPE_NOTIFICATIONS, std::string()));
  }

 private:
  Profile* profile_;
};

// http://crbug.com/177163
#if defined(OS_WIN) && !defined(NDEBUG)
#define MAYBE_Standard DISABLED_Standard
#else
#define MAYBE_Standard Standard
#endif
IN_PROC_BROWSER_TEST_F(ExtensionContentSettingsApiTest, MAYBE_Standard) {
  CheckContentSettingsDefault();

  const char kExtensionPath[] = "content_settings/standard";

  EXPECT_TRUE(RunExtensionSubtest(kExtensionPath, "test.html")) << message_;
  CheckContentSettingsSet();

  // The settings should not be reset when the extension is reloaded.
  ReloadExtension(last_loaded_extension_id());
  CheckContentSettingsSet();

  // Uninstalling and installing the extension (without running the test that
  // calls the extension API) should clear the settings.
  content::WindowedNotificationObserver observer(
      chrome::NOTIFICATION_EXTENSION_UNINSTALLED,
      content::NotificationService::AllSources());
  UninstallExtension(last_loaded_extension_id());
  observer.Wait();
  CheckContentSettingsDefault();

  LoadExtension(test_data_dir_.AppendASCII(kExtensionPath));
  CheckContentSettingsDefault();
}

// Flaky on the trybots. See http://crbug.com/96725.
IN_PROC_BROWSER_TEST_F(ExtensionContentSettingsApiTest,
                       DISABLED_GetResourceIdentifiers) {
  base::FilePath::CharType kFooPath[] =
      FILE_PATH_LITERAL("/plugins/foo.plugin");
  base::FilePath::CharType kBarPath[] =
      FILE_PATH_LITERAL("/plugins/bar.plugin");
  const char* kFooName = "Foo Plugin";
  const char* kBarName = "Bar Plugin";

  content::PluginService::GetInstance()->RegisterInternalPlugin(
      content::WebPluginInfo(ASCIIToUTF16(kFooName),
                             base::FilePath(kFooPath),
                             ASCIIToUTF16("1.2.3"),
                             ASCIIToUTF16("foo")),
      false);
  content::PluginService::GetInstance()->RegisterInternalPlugin(
    content::WebPluginInfo(ASCIIToUTF16(kBarName),
                           base::FilePath(kBarPath),
                           ASCIIToUTF16("2.3.4"),
                           ASCIIToUTF16("bar")),
      false);

  EXPECT_TRUE(RunExtensionTest("content_settings/getresourceidentifiers"))
      << message_;
}

}  // namespace extensions
