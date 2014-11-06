// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/public/test/browser_test_utils.h"

#include "base/command_line.h"
#include "base/json/json_reader.h"
#include "base/path_service.h"
#include "base/process/kill.h"
#include "base/rand_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/synchronization/waitable_event.h"
#include "base/test/test_timeouts.h"
#include "base/values.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/dom_operation_notification_details.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_view.h"
#include "content/public/test/test_utils.h"
#include "grit/webui_resources.h"
#include "net/base/net_util.h"
#include "net/cookies/cookie_store.h"
#include "net/test/python_utils.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_getter.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/keycodes/keycode_converter.h"
#include "ui/base/resource/resource_bundle.h"

namespace content {
namespace {

class DOMOperationObserver : public NotificationObserver,
                             public WebContentsObserver {
 public:
  explicit DOMOperationObserver(RenderViewHost* rvh)
      : WebContentsObserver(WebContents::FromRenderViewHost(rvh)),
        did_respond_(false) {
    registrar_.Add(this, NOTIFICATION_DOM_OPERATION_RESPONSE,
                   Source<RenderViewHost>(rvh));
    message_loop_runner_ = new MessageLoopRunner;
  }

  virtual void Observe(int type,
                       const NotificationSource& source,
                       const NotificationDetails& details) OVERRIDE {
    DCHECK(type == NOTIFICATION_DOM_OPERATION_RESPONSE);
    Details<DomOperationNotificationDetails> dom_op_details(details);
    response_ = dom_op_details->json;
    did_respond_ = true;
    message_loop_runner_->Quit();
  }

  // Overridden from WebContentsObserver:
  virtual void RenderProcessGone(base::TerminationStatus status) OVERRIDE {
    message_loop_runner_->Quit();
  }

  bool WaitAndGetResponse(std::string* response) WARN_UNUSED_RESULT {
    message_loop_runner_->Run();
    *response = response_;
    return did_respond_;
  }

 private:
  NotificationRegistrar registrar_;
  std::string response_;
  bool did_respond_;
  scoped_refptr<MessageLoopRunner> message_loop_runner_;

  DISALLOW_COPY_AND_ASSIGN(DOMOperationObserver);
};

// Specifying a prototype so that we can add the WARN_UNUSED_RESULT attribute.
bool ExecuteScriptHelper(RenderViewHost* render_view_host,
                         const std::string& frame_xpath,
                         const std::string& original_script,
                         scoped_ptr<Value>* result) WARN_UNUSED_RESULT;

// Executes the passed |original_script| in the frame pointed to by
// |frame_xpath|.  If |result| is not NULL, stores the value that the evaluation
// of the script in |result|.  Returns true on success.
bool ExecuteScriptHelper(RenderViewHost* render_view_host,
                         const std::string& frame_xpath,
                         const std::string& original_script,
                         scoped_ptr<Value>* result) {
  // TODO(jcampan): we should make the domAutomationController not require an
  //                automation id.
  std::string script =
      "window.domAutomationController.setAutomationId(0);" + original_script;
  DOMOperationObserver dom_op_observer(render_view_host);
  render_view_host->ExecuteJavascriptInWebFrame(UTF8ToUTF16(frame_xpath),
                                                UTF8ToUTF16(script));
  std::string json;
  if (!dom_op_observer.WaitAndGetResponse(&json)) {
    DLOG(ERROR) << "Cannot communicate with DOMOperationObserver.";
    return false;
  }

  // Nothing more to do for callers that ignore the returned JS value.
  if (!result)
    return true;

  base::JSONReader reader(base::JSON_ALLOW_TRAILING_COMMAS);
  result->reset(reader.ReadToValue(json));
  if (!result->get()) {
    DLOG(ERROR) << reader.GetErrorMessage();
    return false;
  }

  return true;
}

void BuildSimpleWebKeyEvent(blink::WebInputEvent::Type type,
                            ui::KeyboardCode key_code,
                            int native_key_code,
                            int modifiers,
                            NativeWebKeyboardEvent* event) {
  event->nativeKeyCode = native_key_code;
  event->windowsKeyCode = key_code;
  event->setKeyIdentifierFromWindowsKeyCode();
  event->type = type;
  event->modifiers = modifiers;
  event->isSystemKey = false;
  event->timeStampSeconds = base::Time::Now().ToDoubleT();
  event->skip_in_browser = true;

  if (type == blink::WebInputEvent::Char ||
      type == blink::WebInputEvent::RawKeyDown) {
    event->text[0] = key_code;
    event->unmodifiedText[0] = key_code;
  }
}

void InjectRawKeyEvent(WebContents* web_contents,
                       blink::WebInputEvent::Type type,
                       ui::KeyboardCode key_code,
                       int native_key_code,
                       int modifiers) {
  NativeWebKeyboardEvent event;
  BuildSimpleWebKeyEvent(type, key_code, native_key_code, modifiers, &event);
  web_contents->GetRenderViewHost()->ForwardKeyboardEvent(event);
}

void GetCookiesCallback(std::string* cookies_out,
                        base::WaitableEvent* event,
                        const std::string& cookies) {
  *cookies_out = cookies;
  event->Signal();
}

void GetCookiesOnIOThread(const GURL& url,
                          net::URLRequestContextGetter* context_getter,
                          base::WaitableEvent* event,
                          std::string* cookies) {
  net::CookieStore* cookie_store =
      context_getter->GetURLRequestContext()->cookie_store();
  cookie_store->GetCookiesWithOptionsAsync(
      url, net::CookieOptions(),
      base::Bind(&GetCookiesCallback, cookies, event));
}

void SetCookieCallback(bool* result,
                       base::WaitableEvent* event,
                       bool success) {
  *result = success;
  event->Signal();
}

void SetCookieOnIOThread(const GURL& url,
                         const std::string& value,
                         net::URLRequestContextGetter* context_getter,
                         base::WaitableEvent* event,
                         bool* result) {
  net::CookieStore* cookie_store =
      context_getter->GetURLRequestContext()->cookie_store();
  cookie_store->SetCookieWithOptionsAsync(
      url, value, net::CookieOptions(),
      base::Bind(&SetCookieCallback, result, event));
}

}  // namespace


GURL GetFileUrlWithQuery(const base::FilePath& path,
                         const std::string& query_string) {
  GURL url = net::FilePathToFileURL(path);
  if (!query_string.empty()) {
    GURL::Replacements replacements;
    replacements.SetQueryStr(query_string);
    return url.ReplaceComponents(replacements);
  }
  return url;
}

void WaitForLoadStop(WebContents* web_contents) {
    WindowedNotificationObserver load_stop_observer(
    NOTIFICATION_LOAD_STOP,
    Source<NavigationController>(&web_contents->GetController()));
  // In many cases, the load may have finished before we get here.  Only wait if
  // the tab still has a pending navigation.
  if (!web_contents->IsLoading())
    return;
  load_stop_observer.Wait();
}

void CrashTab(WebContents* web_contents) {
  RenderProcessHost* rph = web_contents->GetRenderProcessHost();
  WindowedNotificationObserver observer(
      NOTIFICATION_RENDERER_PROCESS_CLOSED,
      Source<RenderProcessHost>(rph));
  base::KillProcess(rph->GetHandle(), 0, false);
  observer.Wait();
}

void SimulateMouseClick(WebContents* web_contents,
                        int modifiers,
                        blink::WebMouseEvent::Button button) {
  int x = web_contents->GetView()->GetContainerSize().width() / 2;
  int y = web_contents->GetView()->GetContainerSize().height() / 2;
  SimulateMouseClickAt(web_contents, modifiers, button, gfx::Point(x, y));
}

void SimulateMouseClickAt(WebContents* web_contents,
                          int modifiers,
                          blink::WebMouseEvent::Button button,
                          const gfx::Point& point) {
  blink::WebMouseEvent mouse_event;
  mouse_event.type = blink::WebInputEvent::MouseDown;
  mouse_event.button = button;
  mouse_event.x = point.x();
  mouse_event.y = point.y();
  mouse_event.modifiers = modifiers;
  // Mac needs globalX/globalY for events to plugins.
  gfx::Rect offset;
  web_contents->GetView()->GetContainerBounds(&offset);
  mouse_event.globalX = point.x() + offset.x();
  mouse_event.globalY = point.y() + offset.y();
  mouse_event.clickCount = 1;
  web_contents->GetRenderViewHost()->ForwardMouseEvent(mouse_event);
  mouse_event.type = blink::WebInputEvent::MouseUp;
  web_contents->GetRenderViewHost()->ForwardMouseEvent(mouse_event);
}

void SimulateMouseEvent(WebContents* web_contents,
                        blink::WebInputEvent::Type type,
                        const gfx::Point& point) {
  blink::WebMouseEvent mouse_event;
  mouse_event.type = type;
  mouse_event.x = point.x();
  mouse_event.y = point.y();
  web_contents->GetRenderViewHost()->ForwardMouseEvent(mouse_event);
}

void SimulateKeyPress(WebContents* web_contents,
                      ui::KeyboardCode key_code,
                      bool control,
                      bool shift,
                      bool alt,
                      bool command) {
  SimulateKeyPressWithCode(
      web_contents, key_code, NULL, control, shift, alt, command);
}

void SimulateKeyPressWithCode(WebContents* web_contents,
                              ui::KeyboardCode key_code,
                              const char* code,
                              bool control,
                              bool shift,
                              bool alt,
                              bool command) {
  ui::KeycodeConverter* key_converter = ui::KeycodeConverter::GetInstance();
  int native_key_code = key_converter->CodeToNativeKeycode(code);

  int modifiers = 0;

  // The order of these key down events shouldn't matter for our simulation.
  // For our simulation we can use either the left keys or the right keys.
  if (control) {
    modifiers |= blink::WebInputEvent::ControlKey;
    InjectRawKeyEvent(
        web_contents,
        blink::WebInputEvent::RawKeyDown,
        ui::VKEY_CONTROL,
        key_converter->CodeToNativeKeycode("ControlLeft"),
        modifiers);
  }

  if (shift) {
    modifiers |= blink::WebInputEvent::ShiftKey;
    InjectRawKeyEvent(
        web_contents,
        blink::WebInputEvent::RawKeyDown,
        ui::VKEY_SHIFT,
        key_converter->CodeToNativeKeycode("ShiftLeft"),
        modifiers);
  }

  if (alt) {
    modifiers |= blink::WebInputEvent::AltKey;
    InjectRawKeyEvent(
        web_contents,
        blink::WebInputEvent::RawKeyDown,
        ui::VKEY_MENU,
        key_converter->CodeToNativeKeycode("AltLeft"),
        modifiers);
  }

  if (command) {
    modifiers |= blink::WebInputEvent::MetaKey;
    InjectRawKeyEvent(
        web_contents,
        blink::WebInputEvent::RawKeyDown,
        ui::VKEY_COMMAND,
        key_converter->CodeToNativeKeycode("OSLeft"),
        modifiers);
  }

  InjectRawKeyEvent(
      web_contents,
      blink::WebInputEvent::RawKeyDown,
      key_code,
      native_key_code,
      modifiers);

  InjectRawKeyEvent(
      web_contents,
      blink::WebInputEvent::Char,
      key_code,
      native_key_code,
      modifiers);

  InjectRawKeyEvent(
      web_contents,
      blink::WebInputEvent::KeyUp,
      key_code,
      native_key_code,
      modifiers);

  // The order of these key releases shouldn't matter for our simulation.
  if (control) {
    modifiers &= ~blink::WebInputEvent::ControlKey;
    InjectRawKeyEvent(
        web_contents,
        blink::WebInputEvent::KeyUp,
        ui::VKEY_CONTROL,
        key_converter->CodeToNativeKeycode("ControlLeft"),
        modifiers);
  }

  if (shift) {
    modifiers &= ~blink::WebInputEvent::ShiftKey;
    InjectRawKeyEvent(
        web_contents,
        blink::WebInputEvent::KeyUp,
        ui::VKEY_SHIFT,
        key_converter->CodeToNativeKeycode("ShiftLeft"),
        modifiers);
  }

  if (alt) {
    modifiers &= ~blink::WebInputEvent::AltKey;
    InjectRawKeyEvent(
        web_contents,
        blink::WebInputEvent::KeyUp,
        ui::VKEY_MENU,
        key_converter->CodeToNativeKeycode("AltLeft"),
        modifiers);
  }

  if (command) {
    modifiers &= ~blink::WebInputEvent::MetaKey;
    InjectRawKeyEvent(
        web_contents,
        blink::WebInputEvent::KeyUp,
        ui::VKEY_COMMAND,
        key_converter->CodeToNativeKeycode("OSLeft"),
        modifiers);
  }

  ASSERT_EQ(modifiers, 0);
}

namespace internal {

ToRenderViewHost::ToRenderViewHost(WebContents* web_contents)
    : render_view_host_(web_contents->GetRenderViewHost()) {
}

ToRenderViewHost::ToRenderViewHost(RenderViewHost* render_view_host)
    : render_view_host_(render_view_host) {
}

}  // namespace internal

bool ExecuteScriptInFrame(const internal::ToRenderViewHost& adapter,
                          const std::string& frame_xpath,
                          const std::string& original_script) {
  std::string script =
      original_script + ";window.domAutomationController.send(0);";
  return ExecuteScriptHelper(adapter.render_view_host(), frame_xpath, script,
                             NULL);
}

bool ExecuteScriptInFrameAndExtractInt(
    const internal::ToRenderViewHost& adapter,
    const std::string& frame_xpath,
    const std::string& script,
    int* result) {
  DCHECK(result);
  scoped_ptr<Value> value;
  if (!ExecuteScriptHelper(adapter.render_view_host(), frame_xpath, script,
                           &value) || !value.get())
    return false;

  return value->GetAsInteger(result);
}

bool ExecuteScriptInFrameAndExtractBool(
    const internal::ToRenderViewHost& adapter,
    const std::string& frame_xpath,
    const std::string& script,
    bool* result) {
  DCHECK(result);
  scoped_ptr<Value> value;
  if (!ExecuteScriptHelper(adapter.render_view_host(), frame_xpath, script,
                           &value) || !value.get())
    return false;

  return value->GetAsBoolean(result);
}

bool ExecuteScriptInFrameAndExtractString(
    const internal::ToRenderViewHost& adapter,
    const std::string& frame_xpath,
    const std::string& script,
    std::string* result) {
  DCHECK(result);
  scoped_ptr<Value> value;
  if (!ExecuteScriptHelper(adapter.render_view_host(), frame_xpath, script,
                           &value) || !value.get())
    return false;

  return value->GetAsString(result);
}

bool ExecuteScript(const internal::ToRenderViewHost& adapter,
                   const std::string& script) {
  return ExecuteScriptInFrame(adapter, std::string(), script);
}

bool ExecuteScriptAndExtractInt(const internal::ToRenderViewHost& adapter,
                                const std::string& script, int* result) {
  return ExecuteScriptInFrameAndExtractInt(adapter, std::string(), script,
                                           result);
}

bool ExecuteScriptAndExtractBool(const internal::ToRenderViewHost& adapter,
                                 const std::string& script, bool* result) {
  return ExecuteScriptInFrameAndExtractBool(adapter, std::string(), script,
                                            result);
}

bool ExecuteScriptAndExtractString(const internal::ToRenderViewHost& adapter,
                                   const std::string& script,
                                   std::string* result) {
  return ExecuteScriptInFrameAndExtractString(adapter, std::string(), script,
                                              result);
}

bool ExecuteWebUIResourceTest(
    const internal::ToRenderViewHost& adapter,
    const std::vector<int>& js_resource_ids) {
  // Inject WebUI test runner script first prior to other scripts required to
  // run the test as scripts may depend on it being declared.
  std::vector<int> ids;
  ids.push_back(IDR_WEBUI_JS_WEBUI_RESOURCE_TEST);
  ids.insert(ids.end(), js_resource_ids.begin(), js_resource_ids.end());

  std::string script;
  for (std::vector<int>::iterator iter = ids.begin();
       iter != ids.end();
       ++iter) {
    ResourceBundle::GetSharedInstance().GetRawDataResource(*iter)
        .AppendToString(&script);
    script.append("\n");
  }
  if (!content::ExecuteScript(adapter, script))
    return false;

  content::DOMMessageQueue message_queue;
  if (!content::ExecuteScript(adapter, "runTests()"))
    return false;

  std::string message;
  do {
    if (!message_queue.WaitForMessage(&message))
      return false;
  } while (message.compare("\"PENDING\"") == 0);

  return message.compare("\"SUCCESS\"") == 0;
}

std::string GetCookies(BrowserContext* browser_context, const GURL& url) {
  std::string cookies;
  base::WaitableEvent event(true, false);
  net::URLRequestContextGetter* context_getter =
      browser_context->GetRequestContext();

  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(&GetCookiesOnIOThread, url,
                 make_scoped_refptr(context_getter), &event, &cookies));
  event.Wait();
  return cookies;
}

bool SetCookie(BrowserContext* browser_context,
               const GURL& url,
               const std::string& value) {
  bool result = false;
  base::WaitableEvent event(true, false);
  net::URLRequestContextGetter* context_getter =
      browser_context->GetRequestContext();

  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(&SetCookieOnIOThread, url, value,
                 make_scoped_refptr(context_getter), &event, &result));
  event.Wait();
  return result;
}

TitleWatcher::TitleWatcher(WebContents* web_contents,
                           const string16& expected_title)
    : WebContentsObserver(web_contents),
      message_loop_runner_(new MessageLoopRunner) {
  EXPECT_TRUE(web_contents != NULL);
  expected_titles_.push_back(expected_title);
}

void TitleWatcher::AlsoWaitForTitle(const string16& expected_title) {
  expected_titles_.push_back(expected_title);
}

TitleWatcher::~TitleWatcher() {
}

const string16& TitleWatcher::WaitAndGetTitle() {
  message_loop_runner_->Run();
  return observed_title_;
}

void TitleWatcher::DidStopLoading(RenderViewHost* render_view_host) {
  // When navigating through the history, the restored NavigationEntry's title
  // will be used. If the entry ends up having the same title after we return
  // to it, as will usually be the case, then WebContentsObserver::TitleSet
  // will then be suppressed, since the NavigationEntry's title hasn't changed.
  TestTitle();
}

void TitleWatcher::TitleWasSet(NavigationEntry* entry, bool explicit_set) {
  TestTitle();
}

void TitleWatcher::TestTitle() {
  std::vector<string16>::const_iterator it =
      std::find(expected_titles_.begin(),
                expected_titles_.end(),
                web_contents()->GetTitle());
  if (it == expected_titles_.end())
    return;

  observed_title_ = *it;
  message_loop_runner_->Quit();
}

WebContentsDestroyedWatcher::WebContentsDestroyedWatcher(
    WebContents* web_contents)
    : WebContentsObserver(web_contents),
      message_loop_runner_(new MessageLoopRunner) {
  EXPECT_TRUE(web_contents != NULL);
}

WebContentsDestroyedWatcher::~WebContentsDestroyedWatcher() {
}

void WebContentsDestroyedWatcher::Wait() {
  message_loop_runner_->Run();
}

void WebContentsDestroyedWatcher::WebContentsDestroyed(
    WebContents* web_contents) {
  message_loop_runner_->Quit();
}

DOMMessageQueue::DOMMessageQueue() : waiting_for_message_(false) {
  registrar_.Add(this, NOTIFICATION_DOM_OPERATION_RESPONSE,
                 NotificationService::AllSources());
}

DOMMessageQueue::~DOMMessageQueue() {}

void DOMMessageQueue::Observe(int type,
                              const NotificationSource& source,
                              const NotificationDetails& details) {
  Details<DomOperationNotificationDetails> dom_op_details(details);
  Source<RenderViewHost> sender(source);
  message_queue_.push(dom_op_details->json);
  if (waiting_for_message_) {
    waiting_for_message_ = false;
    message_loop_runner_->Quit();
  }
}

void DOMMessageQueue::ClearQueue() {
  message_queue_ = std::queue<std::string>();
}

bool DOMMessageQueue::WaitForMessage(std::string* message) {
  if (message_queue_.empty()) {
    waiting_for_message_ = true;
    // This will be quit when a new message comes in.
    message_loop_runner_ = new MessageLoopRunner;
    message_loop_runner_->Run();
  }
  // The queue should not be empty, unless we were quit because of a timeout.
  if (message_queue_.empty())
    return false;
  if (message)
    *message = message_queue_.front();
  message_queue_.pop();
  return true;
}

}  // namespace content
