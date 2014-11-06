// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/base64.h"
#include "base/files/file_path.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/path_service.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_piece.h"
#include "base/strings/stringprintf.h"
#include "base/synchronization/waitable_event.h"
#include "base/values.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/extensions/api/messaging/incognito_connectability.h"
#include "chrome/browser/extensions/event_router.h"
#include "chrome/browser/extensions/extension_apitest.h"
#include "chrome/browser/extensions/extension_prefs.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/extensions/test_extension_dir.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/extensions/api/runtime.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/notification_service.h"
#include "content/public/test/browser_test_utils.h"
#include "net/cert/asn1_util.h"
#include "net/cert/jwk_serializer.h"
#include "net/dns/mock_host_resolver.h"
#include "net/ssl/server_bound_cert_service.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_getter.h"
#include "url/gurl.h"

namespace extensions {
namespace {

class MessageSender : public content::NotificationObserver {
 public:
  MessageSender() {
    registrar_.Add(this, chrome::NOTIFICATION_EXTENSION_HOST_DID_STOP_LOADING,
                   content::NotificationService::AllSources());
  }

 private:
  static scoped_ptr<base::ListValue> BuildEventArguments(
      const bool last_message,
      const std::string& data) {
    DictionaryValue* event = new DictionaryValue();
    event->SetBoolean("lastMessage", last_message);
    event->SetString("data", data);
    scoped_ptr<base::ListValue> arguments(new base::ListValue());
    arguments->Append(event);
    return arguments.Pass();
  }

  static scoped_ptr<Event> BuildEvent(scoped_ptr<base::ListValue> event_args,
                                      Profile* profile,
                                      GURL event_url) {
    scoped_ptr<Event> event(new Event("test.onMessage", event_args.Pass()));
    event->restrict_to_profile = profile;
    event->event_url = event_url;
    return event.Pass();
  }

  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE {
    EventRouter* event_router = ExtensionSystem::Get(
        content::Source<Profile>(source).ptr())->event_router();

    // Sends four messages to the extension. All but the third message sent
    // from the origin http://b.com/ are supposed to arrive.
    event_router->BroadcastEvent(BuildEvent(
        BuildEventArguments(false, "no restriction"),
        content::Source<Profile>(source).ptr(),
        GURL()));
    event_router->BroadcastEvent(BuildEvent(
        BuildEventArguments(false, "http://a.com/"),
        content::Source<Profile>(source).ptr(),
        GURL("http://a.com/")));
    event_router->BroadcastEvent(BuildEvent(
        BuildEventArguments(false, "http://b.com/"),
        content::Source<Profile>(source).ptr(),
        GURL("http://b.com/")));
    event_router->BroadcastEvent(BuildEvent(
        BuildEventArguments(true, "last message"),
        content::Source<Profile>(source).ptr(),
        GURL()));
  }

  content::NotificationRegistrar registrar_;
};

// Tests that message passing between extensions and content scripts works.
IN_PROC_BROWSER_TEST_F(ExtensionApiTest, Messaging) {
  ASSERT_TRUE(StartEmbeddedTestServer());
  ASSERT_TRUE(RunExtensionTest("messaging/connect")) << message_;
}

// Tests that message passing from one extension to another works.
IN_PROC_BROWSER_TEST_F(ExtensionApiTest, MessagingExternal) {
  ASSERT_TRUE(LoadExtension(
      test_data_dir_.AppendASCII("..").AppendASCII("good")
                    .AppendASCII("Extensions")
                    .AppendASCII("bjafgdebaacbbbecmhlhpofkepfkgcpa")
                    .AppendASCII("1.0")));

  ASSERT_TRUE(RunExtensionTest("messaging/connect_external")) << message_;
}

// Tests that messages with event_urls are only passed to extensions with
// appropriate permissions.
IN_PROC_BROWSER_TEST_F(ExtensionApiTest, MessagingEventURL) {
  MessageSender sender;
  ASSERT_TRUE(RunExtensionTest("messaging/event_url")) << message_;
}

// Tests connecting from a panel to its extension.
class PanelMessagingTest : public ExtensionApiTest {
  virtual void SetUpCommandLine(CommandLine* command_line) OVERRIDE {
    ExtensionApiTest::SetUpCommandLine(command_line);
    command_line->AppendSwitch(switches::kEnablePanels);
  }
};

IN_PROC_BROWSER_TEST_F(PanelMessagingTest, MessagingPanel) {
  ASSERT_TRUE(RunExtensionTest("messaging/connect_panel")) << message_;
}

// Tests externally_connectable between a web page and an extension.
//
// TODO(kalman): Test between extensions. This is already tested in this file,
// but not with externally_connectable set in the manifest.
//
// TODO(kalman): Test with host permissions.
class ExternallyConnectableMessagingTest : public ExtensionApiTest {
 protected:
  // Result codes from the test. These must match up with |results| in
  // c/t/d/extensions/api_test/externally_connectable/assertions.json.
  enum Result {
    OK = 0,
    NAMESPACE_NOT_DEFINED = 1,
    FUNCTION_NOT_DEFINED = 2,
    COULD_NOT_ESTABLISH_CONNECTION_ERROR = 3,
    OTHER_ERROR = 4,
    INCORRECT_RESPONSE_SENDER = 5,
    INCORRECT_RESPONSE_MESSAGE = 6,
  };

  bool AppendIframe(const GURL& src) {
    bool result;
    CHECK(content::ExecuteScriptAndExtractBool(
        browser()->tab_strip_model()->GetActiveWebContents(),
        "actions.appendIframe('" + src.spec() + "');", &result));
    return result;
  }

  Result CanConnectAndSendMessages(const std::string& extension_id) {
    return CanConnectAndSendMessages(browser(), extension_id, "");
  }

  Result CanConnectAndSendMessages(const std::string& extension_id,
                                   const char* frame_xpath,
                                   const char* message) {
    return CanConnectAndSendMessages(browser(), extension_id, frame_xpath,
                                     message);
  }

  Result CanConnectAndSendMessages(Browser* browser,
                                   const std::string& extension_id) {
    return CanConnectAndSendMessages(browser, extension_id, "");
  }

  Result CanConnectAndSendMessages(const std::string& extension_id,
                                   const char* frame_xpath) {
    return CanConnectAndSendMessages(browser(), extension_id, frame_xpath);
  }

  Result CanConnectAndSendMessages(Browser* browser,
                                   const std::string& extension_id,
                                   const char* frame_xpath,
                                   const char* message = NULL) {
    int result;
    std::string args = "'" + extension_id + "'";
    if (message)
      args += std::string(", '") + message + "'";
    CHECK(content::ExecuteScriptInFrameAndExtractInt(
        browser->tab_strip_model()->GetActiveWebContents(),
        frame_xpath,
        base::StringPrintf("assertions.canConnectAndSendMessages(%s)",
                           args.c_str()),
        &result));
    return static_cast<Result>(result);
  }

  testing::AssertionResult AreAnyNonWebApisDefined() {
    return AreAnyNonWebApisDefined("");
  }

  testing::AssertionResult AreAnyNonWebApisDefined(const char* frame_xpath) {
    // All runtime API methods are non-web except for sendRequest and connect.
    const char* non_messaging_apis[] = {
        "getBackgroundPage",
        "getManifest",
        "getURL",
        "reload",
        "requestUpdateCheck",
        "restart",
        "connectNative",
        "sendNativeMessage",
        "onStartup",
        "onInstalled",
        "onSuspend",
        "onSuspendCanceled",
        "onUpdateAvailable",
        "onBrowserUpdateAvailable",
        "onConnect",
        "onConnectExternal",
        "onMessage",
        "onMessageExternal",
        "onRestartRequired",
        "id",
    };

    // Turn the array into a JS array, which effectively gets eval()ed.
    std::string as_js_array;
    for (size_t i = 0; i < arraysize(non_messaging_apis); ++i) {
      as_js_array += as_js_array.empty() ? "[" : ",";
      as_js_array += base::StringPrintf("'%s'", non_messaging_apis[i]);
    }
    as_js_array += "]";

    bool any_defined;
    CHECK(content::ExecuteScriptInFrameAndExtractBool(
        browser()->tab_strip_model()->GetActiveWebContents(),
        frame_xpath,
        "assertions.areAnyRuntimePropertiesDefined(" + as_js_array + ")",
        &any_defined));
    return any_defined ?
        testing::AssertionSuccess() : testing::AssertionFailure();
  }

  std::string GetTlsChannelIdFromPortConnect(const std::string& extension_id,
                                             bool include_tls_channel_id,
                                             const char* message = NULL) {
    return GetTlsChannelIdFromAssertion("getTlsChannelIdFromPortConnect",
                                        extension_id,
                                        include_tls_channel_id,
                                        message);
  }

  std::string GetTlsChannelIdFromSendMessage(const std::string& extension_id,
                                             bool include_tls_channel_id,
                                             const char* message = NULL) {
    return GetTlsChannelIdFromAssertion("getTlsChannelIdFromSendMessage",
                                        extension_id,
                                        include_tls_channel_id,
                                        message);
  }

  GURL GetURLForPath(const std::string& host, const std::string& path) {
    std::string port = base::IntToString(embedded_test_server()->port());
    GURL::Replacements replacements;
    replacements.SetHostStr(host);
    replacements.SetPortStr(port);
    return embedded_test_server()->GetURL(path).ReplaceComponents(replacements);
  }

  GURL chromium_org_url() {
    return GetURLForPath("www.chromium.org", "/chromium.org.html");
  }

  GURL google_com_url() {
    return GetURLForPath("www.google.com", "/google.com.html");
  }

  const Extension* LoadChromiumConnectableExtension() {
    const Extension* extension =
        LoadExtensionIntoDir(&web_connectable_dir_, base::StringPrintf(
            "{"
            "  \"name\": \"chromium_connectable\","
            "  %s,"
            "  \"externally_connectable\": {"
            "    \"matches\": [\"*://*.chromium.org:*/*\"]"
            "  }"
            "}",
            common_manifest()));
    CHECK(extension);
    return extension;
  }

  const Extension* LoadNotConnectableExtension() {
    const Extension* extension =
        LoadExtensionIntoDir(&not_connectable_dir_, base::StringPrintf(
            "{"
            "  \"name\": \"not_connectable\","
            "  %s"
            "}",
            common_manifest()));
    CHECK(extension);
    return extension;
  }

  const Extension* LoadChromiumConnectableExtensionWithTlsChannelId() {
    return LoadExtensionIntoDir(&tls_channel_id_connectable_dir_,
                                connectable_with_tls_channel_id_manifest());
  }

  void InitializeTestServer() {
    base::FilePath test_data;
    EXPECT_TRUE(PathService::Get(chrome::DIR_TEST_DATA, &test_data));
    embedded_test_server()->ServeFilesFromDirectory(test_data.AppendASCII(
        "extensions/api_test/messaging/externally_connectable/sites"));
    ASSERT_TRUE(embedded_test_server()->InitializeAndWaitUntilReady());
    host_resolver()->AddRule("*", embedded_test_server()->base_url().host());
  }

  const char* close_background_message() {
    return "closeBackgroundPage";
  }

 private:
  const Extension* LoadExtensionIntoDir(TestExtensionDir* dir,
                                        const std::string& manifest) {
    dir->WriteManifest(manifest);
    dir->WriteFile(FILE_PATH_LITERAL("background.js"),
                   base::StringPrintf(
        "function maybeClose(message) {\n"
        "  if (message.indexOf('%s') >= 0)\n"
        "    window.setTimeout(function() { window.close() }, 0);\n"
        "}\n"
        "chrome.runtime.onMessageExternal.addListener(\n"
        "    function(message, sender, reply) {\n"
        "  reply({ message: message, sender: sender });\n"
        "  maybeClose(message);\n"
        "});\n"
        "chrome.runtime.onConnectExternal.addListener(function(port) {\n"
        "  port.onMessage.addListener(function(message) {\n"
        "    port.postMessage({ message: message, sender: port.sender });\n"
        "    maybeClose(message);\n"
        "  });\n"
        "});\n",
                   close_background_message()));
    return LoadExtension(dir->unpacked_path());
  }

  const char* common_manifest() {
    return "\"version\": \"1.0\","
           "\"background\": {"
           "    \"scripts\": [\"background.js\"],"
           "    \"persistent\": false"
           "},"
           "\"manifest_version\": 2";
  }

  std::string connectable_with_tls_channel_id_manifest() {
    return base::StringPrintf(
        "{"
        "  \"name\": \"chromium_connectable_with_tls_channel_id\","
        "  %s,"
        "  \"externally_connectable\": {"
        "    \"matches\": [\"*://*.chromium.org:*/*\"],"
        "    \"accepts_tls_channel_id\": true"
        "  }"
        "}",
        common_manifest());
  }

  std::string GetTlsChannelIdFromAssertion(const char* method,
                                           const std::string& extension_id,
                                           bool include_tls_channel_id,
                                           const char* message) {
    std::string result;
    std::string args = "'" + extension_id + "', ";
    args += include_tls_channel_id ? "true" : "false";
    if (message)
      args += std::string(", '") + message + "'";
    CHECK(content::ExecuteScriptAndExtractString(
        browser()->tab_strip_model()->GetActiveWebContents(),
        base::StringPrintf("assertions.%s(%s)", method, args.c_str()),
        &result));
    return result;
  }

  TestExtensionDir web_connectable_dir_;
  TestExtensionDir not_connectable_dir_;
  TestExtensionDir tls_channel_id_connectable_dir_;
};

IN_PROC_BROWSER_TEST_F(ExternallyConnectableMessagingTest, NotInstalled) {
  InitializeTestServer();

  const char kFakeId[] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

  ui_test_utils::NavigateToURL(browser(), chromium_org_url());
  EXPECT_EQ(NAMESPACE_NOT_DEFINED, CanConnectAndSendMessages(kFakeId));
  EXPECT_FALSE(AreAnyNonWebApisDefined());

  ui_test_utils::NavigateToURL(browser(), google_com_url());
  EXPECT_EQ(NAMESPACE_NOT_DEFINED, CanConnectAndSendMessages(kFakeId));
  EXPECT_FALSE(AreAnyNonWebApisDefined());
}

// Tests two extensions on the same sites: one web connectable, one not.
IN_PROC_BROWSER_TEST_F(ExternallyConnectableMessagingTest,
                       WebConnectableAndNotConnectable) {
  InitializeTestServer();

  // Install the web connectable extension. chromium.org can connect to it,
  // google.com can't.
  const Extension* chromium_connectable = LoadChromiumConnectableExtension();

  ui_test_utils::NavigateToURL(browser(), chromium_org_url());
  EXPECT_EQ(OK, CanConnectAndSendMessages(chromium_connectable->id()));
  EXPECT_FALSE(AreAnyNonWebApisDefined());

  ui_test_utils::NavigateToURL(browser(), google_com_url());
  EXPECT_EQ(NAMESPACE_NOT_DEFINED,
            CanConnectAndSendMessages(chromium_connectable->id()));
  EXPECT_FALSE(AreAnyNonWebApisDefined());

  // Install the non-connectable extension. Nothing can connect to it.
  const Extension* not_connectable = LoadNotConnectableExtension();

  ui_test_utils::NavigateToURL(browser(), chromium_org_url());
  // Namespace will be defined here because |chromium_connectable| can connect
  // to it - so this will be the "cannot establish connection" error.
  EXPECT_EQ(COULD_NOT_ESTABLISH_CONNECTION_ERROR,
            CanConnectAndSendMessages(not_connectable->id()));
  EXPECT_FALSE(AreAnyNonWebApisDefined());

  ui_test_utils::NavigateToURL(browser(), google_com_url());
  EXPECT_EQ(NAMESPACE_NOT_DEFINED,
            CanConnectAndSendMessages(not_connectable->id()));
  EXPECT_FALSE(AreAnyNonWebApisDefined());
}

// See http://crbug.com/297866
IN_PROC_BROWSER_TEST_F(ExternallyConnectableMessagingTest,
                       DISABLED_BackgroundPageClosesOnMessageReceipt) {
  InitializeTestServer();

  // Install the web connectable extension.
  const Extension* chromium_connectable = LoadChromiumConnectableExtension();

  ui_test_utils::NavigateToURL(browser(), chromium_org_url());
  // If the background page closes after receipt of the message, it will still
  // reply to this message...
  EXPECT_EQ(OK, CanConnectAndSendMessages(chromium_connectable->id(),
                                          "",
                                          close_background_message()));
  // and be re-opened by receipt of a subsequent message.
  EXPECT_EQ(OK, CanConnectAndSendMessages(chromium_connectable->id()));
}

// Tests a web connectable extension that doesn't receive TLS channel id.
IN_PROC_BROWSER_TEST_F(ExternallyConnectableMessagingTest,
                       WebConnectableWithoutTlsChannelId) {
  InitializeTestServer();

  // Install the web connectable extension. chromium.org can connect to it,
  // google.com can't.
  const Extension* chromium_connectable = LoadChromiumConnectableExtension();
  ASSERT_TRUE(chromium_connectable);

  ui_test_utils::NavigateToURL(browser(), chromium_org_url());
  // The web connectable extension doesn't request the TLS channel ID, so it
  // doesn't get it, whether or not the page asks for it.
  EXPECT_EQ(std::string(),
            GetTlsChannelIdFromPortConnect(chromium_connectable->id(), false));
  EXPECT_EQ(std::string(),
            GetTlsChannelIdFromSendMessage(chromium_connectable->id(), true));
  EXPECT_EQ(std::string(),
            GetTlsChannelIdFromPortConnect(chromium_connectable->id(), false));
  EXPECT_EQ(std::string(),
            GetTlsChannelIdFromSendMessage(chromium_connectable->id(), true));
}

// Tests a web connectable extension that receives TLS channel id with a site
// that can't connect to it.
IN_PROC_BROWSER_TEST_F(ExternallyConnectableMessagingTest,
                       WebConnectableWithTlsChannelIdWithNonMatchingSite) {
  InitializeTestServer();

  const Extension* chromium_connectable =
      LoadChromiumConnectableExtensionWithTlsChannelId();
  ASSERT_TRUE(chromium_connectable);

  ui_test_utils::NavigateToURL(browser(), google_com_url());
  // The extension requests the TLS channel ID, but it doesn't get it for a
  // site that can't connect to it, regardless of whether the page asks for it.
  EXPECT_EQ(base::StringPrintf("%d", NAMESPACE_NOT_DEFINED),
            GetTlsChannelIdFromPortConnect(chromium_connectable->id(), false));
  EXPECT_EQ(base::StringPrintf("%d", NAMESPACE_NOT_DEFINED),
            GetTlsChannelIdFromSendMessage(chromium_connectable->id(), true));
  EXPECT_EQ(base::StringPrintf("%d", NAMESPACE_NOT_DEFINED),
            GetTlsChannelIdFromPortConnect(chromium_connectable->id(), false));
  EXPECT_EQ(base::StringPrintf("%d", NAMESPACE_NOT_DEFINED),
            GetTlsChannelIdFromSendMessage(chromium_connectable->id(), true));
}

// Tests a web connectable extension that receives TLS channel id on a site
// that can connect to it, but with no TLS channel ID having been generated.
IN_PROC_BROWSER_TEST_F(ExternallyConnectableMessagingTest,
                       WebConnectableWithTlsChannelIdWithEmptyTlsChannelId) {
  InitializeTestServer();

  const Extension* chromium_connectable =
      LoadChromiumConnectableExtensionWithTlsChannelId();
  ASSERT_TRUE(chromium_connectable);

  ui_test_utils::NavigateToURL(browser(), chromium_org_url());

  // Since the extension requests the TLS channel ID, it gets it for a site that
  // can connect to it, but only if the page also asks to include it.
  EXPECT_EQ(std::string(),
      GetTlsChannelIdFromPortConnect(chromium_connectable->id(), false));
  EXPECT_EQ(std::string(),
      GetTlsChannelIdFromSendMessage(chromium_connectable->id(), false));
  // If the page does ask for it, it isn't empty.
  std::string tls_channel_id =
      GetTlsChannelIdFromPortConnect(chromium_connectable->id(), true);
  // Because the TLS channel ID has never been generated for this domain,
  // no TLS channel ID is reported.
  EXPECT_EQ(std::string(), tls_channel_id);
}

// Flaky on Linux. http://crbug.com/315264
#if defined(OS_LINUX)
#define MAYBE_WebConnectableWithEmptyTlsChannelIdAndClosedBackgroundPage \
    DISABLED_WebConnectableWithEmptyTlsChannelIdAndClosedBackgroundPage
#else
#define MAYBE_WebConnectableWithEmptyTlsChannelIdAndClosedBackgroundPage \
    WebConnectableWithEmptyTlsChannelIdAndClosedBackgroundPage
#endif
// Tests a web connectable extension that receives TLS channel id, but
// immediately closes its background page upon receipt of a message.
IN_PROC_BROWSER_TEST_F(ExternallyConnectableMessagingTest,
    MAYBE_WebConnectableWithEmptyTlsChannelIdAndClosedBackgroundPage) {
  InitializeTestServer();

  const Extension* chromium_connectable =
      LoadChromiumConnectableExtensionWithTlsChannelId();

  ui_test_utils::NavigateToURL(browser(), chromium_org_url());
  // If the page does ask for it, it isn't empty, even if the background page
  // closes upon receipt of the connect.
  std::string tls_channel_id =
      GetTlsChannelIdFromPortConnect(chromium_connectable->id(),
                                     true,
                                     close_background_message());
  // Because the TLS channel ID has never been generated for this domain,
  // no TLS channel ID is reported.
  EXPECT_EQ(std::string(), tls_channel_id);
  // A subsequent connect will still succeed, even if the background page was
  // previously closed.
  tls_channel_id =
      GetTlsChannelIdFromPortConnect(chromium_connectable->id(),
                                     true);
   // And the empty value is still retrieved.
  EXPECT_EQ(std::string(), tls_channel_id);
}

// Tests that enabling and disabling an extension makes the runtime bindings
// appear and disappear.
//
// TODO(kalman): Test with multiple extensions that can be accessed by the same
// host.
IN_PROC_BROWSER_TEST_F(ExternallyConnectableMessagingTest,
                       EnablingAndDisabling) {
  InitializeTestServer();

  const Extension* chromium_connectable = LoadChromiumConnectableExtension();
  const Extension* not_connectable = LoadNotConnectableExtension();

  ui_test_utils::NavigateToURL(browser(), chromium_org_url());
  EXPECT_EQ(OK, CanConnectAndSendMessages(chromium_connectable->id()));
  EXPECT_EQ(COULD_NOT_ESTABLISH_CONNECTION_ERROR,
            CanConnectAndSendMessages(not_connectable->id()));

  DisableExtension(chromium_connectable->id());
  EXPECT_EQ(COULD_NOT_ESTABLISH_CONNECTION_ERROR,
            CanConnectAndSendMessages(chromium_connectable->id()));

  EnableExtension(chromium_connectable->id());
  EXPECT_EQ(OK, CanConnectAndSendMessages(chromium_connectable->id()));
  EXPECT_EQ(COULD_NOT_ESTABLISH_CONNECTION_ERROR,
            CanConnectAndSendMessages(not_connectable->id()));
}

// Tests connection from incognito tabs when the user denies the connection
// request. Spanning mode only.
//
// TODO(kalman): ensure that we exercise split vs spanning incognito logic
// somewhere. This is a test that should be shared with the content script logic
// so it's not really our specific concern for web connectable.
//
// TODO(kalman): test messages from incognito extensions too.
IN_PROC_BROWSER_TEST_F(ExternallyConnectableMessagingTest, FromIncognitoDeny) {
  InitializeTestServer();

  const Extension* chromium_connectable = LoadChromiumConnectableExtension();
  const std::string& id = chromium_connectable->id();

  Browser* incognito_browser = ui_test_utils::OpenURLOffTheRecord(
      profile()->GetOffTheRecordProfile(),
      chromium_org_url());

  // No connection because incognito-enabled hasn't been set for the extension,
  // and the user denied our interactive request.
  {
    IncognitoConnectability::ScopedAlertTracker alert_tracker(
        IncognitoConnectability::ScopedAlertTracker::ALWAYS_DENY);

    EXPECT_EQ(COULD_NOT_ESTABLISH_CONNECTION_ERROR,
              CanConnectAndSendMessages(incognito_browser, id));
    EXPECT_EQ(1, alert_tracker.GetAndResetAlertCount());

    // Try again. User has already denied.
    EXPECT_EQ(COULD_NOT_ESTABLISH_CONNECTION_ERROR,
              CanConnectAndSendMessages(incognito_browser, id));
    EXPECT_EQ(0, alert_tracker.GetAndResetAlertCount());
  }

  // Allowing the extension in incognito mode will bypass the deny.
  ExtensionPrefs::Get(profile())->SetIsIncognitoEnabled(id, true);
  EXPECT_EQ(OK, CanConnectAndSendMessages(incognito_browser, id));
}

// Tests connection from incognito tabs when the user accepts the connection
// request. Spanning mode only.
//
// TODO(kalman): see comment above about split mode.
IN_PROC_BROWSER_TEST_F(ExternallyConnectableMessagingTest, FromIncognitoAllow) {
  InitializeTestServer();

  const Extension* chromium_connectable = LoadChromiumConnectableExtension();
  const std::string& id = chromium_connectable->id();

  Browser* incognito_browser = ui_test_utils::OpenURLOffTheRecord(
      profile()->GetOffTheRecordProfile(),
      chromium_org_url());

  // Connection allowed even with incognito disabled, because the user accepted
  // the interactive request.
  {
    IncognitoConnectability::ScopedAlertTracker alert_tracker(
        IncognitoConnectability::ScopedAlertTracker::ALWAYS_ALLOW);

    EXPECT_EQ(OK, CanConnectAndSendMessages(incognito_browser, id));
    EXPECT_EQ(1, alert_tracker.GetAndResetAlertCount());

    // Try again. User has already allowed.
    EXPECT_EQ(OK, CanConnectAndSendMessages(incognito_browser, id));
    EXPECT_EQ(0, alert_tracker.GetAndResetAlertCount());
  }

  // Allowing the extension in incognito mode will continue to allow.
  ExtensionPrefs::Get(profile())->SetIsIncognitoEnabled(id, true);
  EXPECT_EQ(OK, CanConnectAndSendMessages(incognito_browser, id));
}

// Tests a connection from an iframe within a tab which doesn't have
// permission. Iframe should work.
IN_PROC_BROWSER_TEST_F(ExternallyConnectableMessagingTest,
                       FromIframeWithPermission) {
  InitializeTestServer();

  const Extension* extension = LoadChromiumConnectableExtension();

  ui_test_utils::NavigateToURL(browser(), google_com_url());
  EXPECT_EQ(NAMESPACE_NOT_DEFINED, CanConnectAndSendMessages(extension->id()));
  EXPECT_FALSE(AreAnyNonWebApisDefined());

  ASSERT_TRUE(AppendIframe(chromium_org_url()));

  const char* frame_xpath = "//iframe[1]";
  EXPECT_EQ(OK, CanConnectAndSendMessages(extension->id(), frame_xpath));
  EXPECT_FALSE(AreAnyNonWebApisDefined(frame_xpath));
}

// Tests connection from an iframe without permission within a tab that does.
// Iframe shouldn't work.
IN_PROC_BROWSER_TEST_F(ExternallyConnectableMessagingTest,
                       FromIframeWithoutPermission) {
  InitializeTestServer();

  const Extension* extension = LoadChromiumConnectableExtension();

  ui_test_utils::NavigateToURL(browser(), chromium_org_url());
  EXPECT_EQ(OK, CanConnectAndSendMessages(extension->id()));
  EXPECT_FALSE(AreAnyNonWebApisDefined());

  ASSERT_TRUE(AppendIframe(google_com_url()));

  const char* frame_xpath = "//iframe[1]";
  EXPECT_EQ(NAMESPACE_NOT_DEFINED,
            CanConnectAndSendMessages(extension->id(), frame_xpath));
  EXPECT_FALSE(AreAnyNonWebApisDefined(frame_xpath));
}

// Tests externally_connectable between a web page and an extension with a
// TLS channel ID created for the origin.
class ExternallyConnectableMessagingWithTlsChannelIdTest :
  public ExternallyConnectableMessagingTest {
 public:
  ExternallyConnectableMessagingWithTlsChannelIdTest()
      : tls_channel_id_created_(false, false) {
  }

  std::string CreateTlsChannelId() {
    scoped_refptr<net::URLRequestContextGetter> request_context_getter(
        profile()->GetRequestContext());
  std::string domain_bound_private_key;
  std::string domain_bound_cert;
  net::ServerBoundCertService::RequestHandle request_handle;
    content::BrowserThread::PostTask(
        content::BrowserThread::IO,
        FROM_HERE,
        base::Bind(
            &ExternallyConnectableMessagingWithTlsChannelIdTest::
                CreateDomainBoundCertOnIOThread,
            base::Unretained(this),
            base::Unretained(&domain_bound_private_key),
            base::Unretained(&domain_bound_cert),
            base::Unretained(&request_handle),
            request_context_getter));
    tls_channel_id_created_.Wait();
    // Create the expected value.
    base::StringPiece spki;
    net::asn1::ExtractSPKIFromDERCert(domain_bound_cert, &spki);
    base::DictionaryValue jwk_value;
    net::JwkSerializer::ConvertSpkiFromDerToJwk(spki, &jwk_value);
    std::string tls_channel_id_value;
    base::JSONWriter::Write(&jwk_value, &tls_channel_id_value);
    return tls_channel_id_value;
  }

 private:
  void CreateDomainBoundCertOnIOThread(
      std::string* domain_bound_private_key,
      std::string* domain_bound_cert,
      net::ServerBoundCertService::RequestHandle* request_handle,
      scoped_refptr<net::URLRequestContextGetter> request_context_getter) {
    DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::IO));
    net::ServerBoundCertService* server_bound_cert_service =
        request_context_getter->GetURLRequestContext()->
            server_bound_cert_service();
    int status = server_bound_cert_service->GetOrCreateDomainBoundCert(
        chromium_org_url().host(),
        domain_bound_private_key,
        domain_bound_cert,
        base::Bind(&ExternallyConnectableMessagingWithTlsChannelIdTest::
                   GotDomainBoundCert,
                   base::Unretained(this)),
        request_handle);
    if (status == net::ERR_IO_PENDING)
      return;
    GotDomainBoundCert(status);
  }

  void GotDomainBoundCert(int status) {
    ASSERT_TRUE(status == net::OK);
    tls_channel_id_created_.Signal();
  }

  base::WaitableEvent tls_channel_id_created_;
};

// Tests a web connectable extension that receives TLS channel id on a site
// that can connect to it, with a TLS channel ID having been generated.
IN_PROC_BROWSER_TEST_F(ExternallyConnectableMessagingWithTlsChannelIdTest,
                       WebConnectableWithNonEmptyTlsChannelId) {
  InitializeTestServer();
  std::string expected_tls_channel_id_value = CreateTlsChannelId();

  const Extension* chromium_connectable =
      LoadChromiumConnectableExtensionWithTlsChannelId();
  ASSERT_TRUE(chromium_connectable);

  ui_test_utils::NavigateToURL(browser(), chromium_org_url());

  // Since the extension requests the TLS channel ID, it gets it for a site that
  // can connect to it, but only if the page also asks to send it.
  EXPECT_EQ(std::string(),
      GetTlsChannelIdFromPortConnect(chromium_connectable->id(), false));
  EXPECT_EQ(std::string(),
      GetTlsChannelIdFromSendMessage(chromium_connectable->id(), false));

  // If the page does ask to send the TLS channel ID, it's sent and non-empty.
  std::string tls_channel_id_from_port_connect =
      GetTlsChannelIdFromPortConnect(chromium_connectable->id(), true);
  EXPECT_NE(0u, tls_channel_id_from_port_connect.size());

  // The same value is received by both connect and sendMessage.
  std::string tls_channel_id_from_send_message =
      GetTlsChannelIdFromSendMessage(chromium_connectable->id(), true);
  EXPECT_EQ(tls_channel_id_from_port_connect, tls_channel_id_from_send_message);

  // And since a TLS channel ID exists for the domain, the value received is
  // parseable as a JWK. (In particular, it has the same value we created by
  // converting the public key to JWK with net::ConvertSpkiFromDerToJwk.)
  std::string tls_channel_id(tls_channel_id_from_port_connect);
  EXPECT_EQ(expected_tls_channel_id_value, tls_channel_id);

  // The TLS channel ID shouldn't change from one connection to the next...
  std::string tls_channel_id2 =
      GetTlsChannelIdFromPortConnect(chromium_connectable->id(), true);
  EXPECT_EQ(tls_channel_id, tls_channel_id2);
  tls_channel_id2 =
      GetTlsChannelIdFromSendMessage(chromium_connectable->id(), true);
  EXPECT_EQ(tls_channel_id, tls_channel_id2);

  // nor should it change when navigating away, revisiting the page and
  // requesting it again.
  ui_test_utils::NavigateToURL(browser(), google_com_url());
  ui_test_utils::NavigateToURL(browser(), chromium_org_url());
  tls_channel_id2 =
      GetTlsChannelIdFromPortConnect(chromium_connectable->id(), true);
  EXPECT_EQ(tls_channel_id, tls_channel_id2);
  tls_channel_id2 =
      GetTlsChannelIdFromSendMessage(chromium_connectable->id(), true);
  EXPECT_EQ(tls_channel_id, tls_channel_id2);
}

// Tests a web connectable extension that receives TLS channel id, but
// immediately closes its background page upon receipt of a message.
// Same flakiness seen in http://crbug.com/297866
IN_PROC_BROWSER_TEST_F(ExternallyConnectableMessagingWithTlsChannelIdTest,
    DISABLED_WebConnectableWithNonEmptyTlsChannelIdAndClosedBackgroundPage) {
  InitializeTestServer();
  std::string expected_tls_channel_id_value = CreateTlsChannelId();

  const Extension* chromium_connectable =
      LoadChromiumConnectableExtensionWithTlsChannelId();

  ui_test_utils::NavigateToURL(browser(), chromium_org_url());
  // If the page does ask for it, it isn't empty, even if the background page
  // closes upon receipt of the connect.
  std::string tls_channel_id =
      GetTlsChannelIdFromPortConnect(chromium_connectable->id(),
                                     true,
                                     close_background_message());
  EXPECT_EQ(expected_tls_channel_id_value, tls_channel_id);
  // A subsequent connect will still succeed, even if the background page was
  // previously closed.
  tls_channel_id =
      GetTlsChannelIdFromPortConnect(chromium_connectable->id(),
                                     true);
   // And the expected value is still retrieved.
  EXPECT_EQ(expected_tls_channel_id_value, tls_channel_id);
}

IN_PROC_BROWSER_TEST_F(ExtensionApiTest, MessagingUserGesture) {
  const char kManifest[] = "{"
                          "  \"name\": \"user_gesture\","
                          "  \"version\": \"1.0\","
                          "  \"background\": {"
                          "    \"scripts\": [\"background.js\"]"
                          "  },"
                          "  \"manifest_version\": 2"
                          "}";

  TestExtensionDir receiver_dir;
  receiver_dir.WriteManifest(kManifest);
  receiver_dir.WriteFile(FILE_PATH_LITERAL("background.js"),
      "chrome.runtime.onMessageExternal.addListener(\n"
      "    function(msg, sender, reply) {\n"
      "      reply({result:chrome.test.isProcessingUserGesture()});\n"
      "    });");
  const Extension* receiver = LoadExtension(receiver_dir.unpacked_path());
  ASSERT_TRUE(receiver);

  TestExtensionDir sender_dir;
  sender_dir.WriteManifest(kManifest);
  sender_dir.WriteFile(FILE_PATH_LITERAL("background.js"), "");
  const Extension* sender = LoadExtension(sender_dir.unpacked_path());
  ASSERT_TRUE(sender);

  EXPECT_EQ("false",
      ExecuteScriptInBackgroundPage(sender->id(),
                                    base::StringPrintf(
          "chrome.test.runWithoutUserGesture(function() {\n"
          "  chrome.runtime.sendMessage('%s', {}, function(response)  {\n"
          "    window.domAutomationController.send('' + response.result);\n"
          "  });\n"
          "});", receiver->id().c_str())));

  EXPECT_EQ("true",
      ExecuteScriptInBackgroundPage(sender->id(),
                                    base::StringPrintf(
          "chrome.test.runWithUserGesture(function() {\n"
          "  chrome.runtime.sendMessage('%s', {}, function(response)  {\n"
          "    window.domAutomationController.send('' + response.result);\n"
          "  });\n"
          "});", receiver->id().c_str())));
}

}  // namespace
};  // namespace extensions
