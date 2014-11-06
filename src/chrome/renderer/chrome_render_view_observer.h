// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_RENDERER_CHROME_RENDER_VIEW_OBSERVER_H_
#define CHROME_RENDERER_CHROME_RENDER_VIEW_OBSERVER_H_

#include <set>
#include <string>
#include <vector>

#include "base/basictypes.h"
#include "base/memory/linked_ptr.h"
#include "base/memory/scoped_ptr.h"
#include "base/timer/timer.h"
#include "content/public/common/top_controls_state.h"
#include "content/public/renderer/render_view_observer.h"
#include "extensions/common/permissions/api_permission.h"
#include "third_party/WebKit/public/web/WebPermissionClient.h"
#include "ui/gfx/size.h"
#include "url/gurl.h"

class ChromeRenderProcessObserver;
class ContentSettingsObserver;
class ExternalHostBindings;
class SkBitmap;
class TranslateHelper;
class WebViewColorOverlay;
class WebViewAnimatingOverlay;

namespace extensions {
class Dispatcher;
class Extension;
}

namespace blink {
class WebView;
struct WebWindowFeatures;
}

namespace safe_browsing {
class PhishingClassifierDelegate;
}

// This class holds the Chrome specific parts of RenderView, and has the same
// lifetime.
class ChromeRenderViewObserver : public content::RenderViewObserver,
                                 public blink::WebPermissionClient {
 public:
  // translate_helper can be NULL.
  ChromeRenderViewObserver(
      content::RenderView* render_view,
      ContentSettingsObserver* content_settings,
      ChromeRenderProcessObserver* chrome_render_process_observer,
      extensions::Dispatcher* extension_dispatcher);
  virtual ~ChromeRenderViewObserver();

 private:
  // Holds the information received in OnWebUIJavaScript for later use
  // to call EvaluateScript() to preload javascript for WebUI tests.
  struct WebUIJavaScript {
    string16 frame_xpath;
    string16 jscript;
    int id;
    bool notify_result;
  };

  // RenderViewObserver implementation.
  virtual bool OnMessageReceived(const IPC::Message& message) OVERRIDE;
  virtual void DidStartLoading() OVERRIDE;
  virtual void DidStopLoading() OVERRIDE;
  virtual void DidCommitProvisionalLoad(blink::WebFrame* frame,
                                        bool is_new_navigation) OVERRIDE;
  virtual void DidClearWindowObject(blink::WebFrame* frame) OVERRIDE;
  virtual void DidHandleGestureEvent(
      const blink::WebGestureEvent& event) OVERRIDE;
  virtual void DetailedConsoleMessageAdded(const base::string16& message,
                                           const base::string16& source,
                                           const base::string16& stack_trace,
                                           int32 line_number,
                                           int32 severity_level) OVERRIDE;

  // blink::WebPermissionClient implementation.
  virtual bool allowDatabase(blink::WebFrame* frame,
                             const blink::WebString& name,
                             const blink::WebString& display_name,
                             unsigned long estimated_size);
  virtual bool allowFileSystem(blink::WebFrame* frame);
  virtual bool allowImage(blink::WebFrame* frame,
                          bool enabled_per_settings,
                          const blink::WebURL& image_url);
  virtual bool allowIndexedDB(blink::WebFrame* frame,
                              const blink::WebString& name,
                              const blink::WebSecurityOrigin& origin);
  virtual bool allowPlugins(blink::WebFrame* frame,
                            bool enabled_per_settings);
  virtual bool allowScript(blink::WebFrame* frame,
                           bool enabled_per_settings);
  virtual bool allowScriptFromSource(blink::WebFrame* frame,
                                     bool enabled_per_settings,
                                     const blink::WebURL& script_url);
  virtual bool allowStorage(blink::WebFrame* frame, bool local);
  virtual bool allowReadFromClipboard(blink::WebFrame* frame,
                                      bool default_value);
  virtual bool allowWriteToClipboard(blink::WebFrame* frame,
                                     bool default_value);
  virtual bool allowWebComponents(const blink::WebDocument&, bool);
  virtual bool allowHTMLNotifications(
      const blink::WebDocument& document);
  virtual bool allowMutationEvents(const blink::WebDocument&,
                                   bool default_value);
  virtual bool allowPushState(const blink::WebDocument&);
  virtual bool allowWebGLDebugRendererInfo(blink::WebFrame* frame);
  virtual void didNotAllowPlugins(blink::WebFrame* frame);
  virtual void didNotAllowScript(blink::WebFrame* frame);
  virtual bool allowDisplayingInsecureContent(
      blink::WebFrame* frame,
      bool allowed_per_settings,
      const blink::WebSecurityOrigin& context,
      const blink::WebURL& url);
  virtual bool allowRunningInsecureContent(
      blink::WebFrame* frame,
      bool allowed_per_settings,
      const blink::WebSecurityOrigin& context,
      const blink::WebURL& url);
  virtual void Navigate(const GURL& url) OVERRIDE;

  void OnWebUIJavaScript(const string16& frame_xpath,
                         const string16& jscript,
                         int id,
                         bool notify_result);
  void OnHandleMessageFromExternalHost(const std::string& message,
                                       const std::string& origin,
                                       const std::string& target);
  void OnJavaScriptStressTestControl(int cmd, int param);
  void OnSetIsPrerendering(bool is_prerendering);
  void OnSetAllowDisplayingInsecureContent(bool allow);
  void OnSetAllowRunningInsecureContent(bool allow);
  void OnSetClientSidePhishingDetection(bool enable_phishing_detection);
  void OnSetVisuallyDeemphasized(bool deemphasized);
  void OnRequestThumbnailForContextNode(int thumbnail_min_area_pixels,
                                        gfx::Size thumbnail_max_size_pixels);
  void OnGetFPS();
  void OnAddStrictSecurityHost(const std::string& host);
  void OnNPAPINotSupported();
#if defined(OS_ANDROID)
  void OnUpdateTopControlsState(content::TopControlsState constraints,
                                content::TopControlsState current,
                                bool animate);
  void OnRetrieveWebappInformation(const GURL& expected_url);
#endif
  void OnSetWindowFeatures(const blink::WebWindowFeatures& window_features);

  void CapturePageInfoLater(int page_id,
                            bool preliminary_capture,
                            base::TimeDelta delay);

  // Captures the thumbnail and text contents for indexing for the given load
  // ID.  Kicks off analysis of the captured text.
  void CapturePageInfo(int page_id, bool preliminary_capture);

  // Retrieves the text from the given frame contents, the page text up to the
  // maximum amount kMaxIndexChars will be placed into the given buffer.
  void CaptureText(blink::WebFrame* frame, string16* contents);

  ExternalHostBindings* GetExternalHostBindings();

  // Determines if a host is in the strict security host set.
  bool IsStrictSecurityHost(const std::string& host);

  // If |origin| corresponds to an installed extension, returns that extension.
  // Otherwise returns NULL.
  const extensions::Extension* GetExtension(
      const blink::WebSecurityOrigin& origin) const;

  // Checks if a page contains <meta http-equiv="refresh" ...> tag.
  bool HasRefreshMetaTag(blink::WebFrame* frame);

  // Save the JavaScript to preload if a ViewMsg_WebUIJavaScript is received.
  scoped_ptr<WebUIJavaScript> webui_javascript_;

  // Owned by ChromeContentRendererClient and outlive us.
  ChromeRenderProcessObserver* chrome_render_process_observer_;
  extensions::Dispatcher* extension_dispatcher_;

  // Have the same lifetime as us.
  ContentSettingsObserver* content_settings_;
  TranslateHelper* translate_helper_;
  safe_browsing::PhishingClassifierDelegate* phishing_classifier_;

  // Page_id from the last page we indexed. This prevents us from indexing the
  // same page twice in a row.
  int32 last_indexed_page_id_;
  // The toplevel URL that was last indexed.  This is used together with the
  // page id to decide whether to reindex in certain cases like history
  // replacement.
  GURL last_indexed_url_;

  // Insecure content may be permitted for the duration of this render view.
  bool allow_displaying_insecure_content_;
  bool allow_running_insecure_content_;
  std::set<std::string> strict_security_hosts_;

  // External host exposed through automation controller.
  scoped_ptr<ExternalHostBindings> external_host_bindings_;

  // A color page overlay when visually de-emaphasized.
  scoped_ptr<WebViewColorOverlay> dimmed_color_overlay_;

  // Used to delay calling CapturePageInfo.
  base::Timer capture_timer_;

  DISALLOW_COPY_AND_ASSIGN(ChromeRenderViewObserver);
};

#endif  // CHROME_RENDERER_CHROME_RENDER_VIEW_OBSERVER_H_
