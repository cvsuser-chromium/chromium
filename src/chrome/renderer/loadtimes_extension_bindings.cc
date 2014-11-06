// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/renderer/loadtimes_extension_bindings.h"

#include <math.h>

#include "base/time/time.h"
#include "content/public/renderer/document_state.h"
#include "net/http/http_response_info.h"
#include "third_party/WebKit/public/web/WebFrame.h"
#include "v8/include/v8.h"

using blink::WebDataSource;
using blink::WebFrame;
using blink::WebNavigationType;
using content::DocumentState;

// Values for CSI "tran" property
const int kTransitionLink = 0;
const int kTransitionForwardBack = 6;
const int kTransitionOther = 15;
const int kTransitionReload = 16;

namespace extensions_v8 {

static const char* const kLoadTimesExtensionName = "v8/LoadTimes";

class LoadTimesExtensionWrapper : public v8::Extension {
 public:
  // Creates an extension which adds a new function, chromium.GetLoadTimes()
  // This function returns an object containing the following members:
  // requestTime: The time the request to load the page was received
  // loadTime: The time the renderer started the load process
  // finishDocumentLoadTime: The time the document itself was loaded
  //                         (this is before the onload() method is fired)
  // finishLoadTime: The time all loading is done, after the onload()
  //                 method and all resources
  // navigationType: A string describing what user action initiated the load
  LoadTimesExtensionWrapper() :
    v8::Extension(kLoadTimesExtensionName,
      "var chrome;"
      "if (!chrome)"
      "  chrome = {};"
      "chrome.loadTimes = function() {"
      "  native function GetLoadTimes();"
      "  return GetLoadTimes();"
      "};"
      "chrome.csi = function() {"
      "  native function GetCSI();"
      "  return GetCSI();"
      "}") {}

  virtual v8::Handle<v8::FunctionTemplate> GetNativeFunction(
      v8::Handle<v8::String> name) OVERRIDE {
    if (name->Equals(v8::String::New("GetLoadTimes"))) {
      return v8::FunctionTemplate::New(GetLoadTimes);
    } else if (name->Equals(v8::String::New("GetCSI"))) {
      return v8::FunctionTemplate::New(GetCSI);
    }
    return v8::Handle<v8::FunctionTemplate>();
  }

  static const char* GetNavigationType(WebNavigationType nav_type) {
    switch (nav_type) {
      case blink::WebNavigationTypeLinkClicked:
        return "LinkClicked";
      case blink::WebNavigationTypeFormSubmitted:
        return "FormSubmitted";
      case blink::WebNavigationTypeBackForward:
        return "BackForward";
      case blink::WebNavigationTypeReload:
        return "Reload";
      case blink::WebNavigationTypeFormResubmitted:
        return "Resubmitted";
      case blink::WebNavigationTypeOther:
        return "Other";
    }
    return "";
  }

  static int GetCSITransitionType(WebNavigationType nav_type) {
    switch (nav_type) {
      case blink::WebNavigationTypeLinkClicked:
      case blink::WebNavigationTypeFormSubmitted:
      case blink::WebNavigationTypeFormResubmitted:
        return kTransitionLink;
      case blink::WebNavigationTypeBackForward:
        return kTransitionForwardBack;
      case blink::WebNavigationTypeReload:
        return kTransitionReload;
      case blink::WebNavigationTypeOther:
        return kTransitionOther;
    }
    return kTransitionOther;
  }

  static void GetLoadTimes(const v8::FunctionCallbackInfo<v8::Value>& args) {
    WebFrame* frame = WebFrame::frameForCurrentContext();
    if (frame) {
      WebDataSource* data_source = frame->dataSource();
      if (data_source) {
        DocumentState* document_state =
            DocumentState::FromDataSource(data_source);
        v8::Local<v8::Object> load_times = v8::Object::New();
        load_times->Set(
            v8::String::New("requestTime"),
            v8::Number::New(document_state->request_time().ToDoubleT()));
        load_times->Set(
            v8::String::New("startLoadTime"),
            v8::Number::New(document_state->start_load_time().ToDoubleT()));
        load_times->Set(
            v8::String::New("commitLoadTime"),
            v8::Number::New(document_state->commit_load_time().ToDoubleT()));
        load_times->Set(
            v8::String::New("finishDocumentLoadTime"),
            v8::Number::New(
                document_state->finish_document_load_time().ToDoubleT()));
        load_times->Set(
            v8::String::New("finishLoadTime"),
            v8::Number::New(document_state->finish_load_time().ToDoubleT()));
        load_times->Set(
            v8::String::New("firstPaintTime"),
            v8::Number::New(document_state->first_paint_time().ToDoubleT()));
        load_times->Set(
            v8::String::New("firstPaintAfterLoadTime"),
            v8::Number::New(
                document_state->first_paint_after_load_time().ToDoubleT()));
        load_times->Set(
            v8::String::New("navigationType"),
            v8::String::New(GetNavigationType(data_source->navigationType())));
        load_times->Set(
            v8::String::New("wasFetchedViaSpdy"),
            v8::Boolean::New(document_state->was_fetched_via_spdy()));
        load_times->Set(
            v8::String::New("wasNpnNegotiated"),
            v8::Boolean::New(document_state->was_npn_negotiated()));
        load_times->Set(
            v8::String::New("npnNegotiatedProtocol"),
            v8::String::New(document_state->npn_negotiated_protocol().c_str()));
        load_times->Set(
            v8::String::New("wasAlternateProtocolAvailable"),
            v8::Boolean::New(
                document_state->was_alternate_protocol_available()));
        load_times->Set(
            v8::String::New("connectionInfo"),
            v8::String::New(
                net::HttpResponseInfo::ConnectionInfoToString(
                    document_state->connection_info()).c_str()));
        args.GetReturnValue().Set(load_times);
        return;
      }
    }
    args.GetReturnValue().SetNull();
  }

  static void GetCSI(const v8::FunctionCallbackInfo<v8::Value>& args) {
    WebFrame* frame = WebFrame::frameForCurrentContext();
    if (frame) {
      WebDataSource* data_source = frame->dataSource();
      if (data_source) {
        DocumentState* document_state =
            DocumentState::FromDataSource(data_source);
        v8::Local<v8::Object> csi = v8::Object::New();
        base::Time now = base::Time::Now();
        base::Time start = document_state->request_time().is_null() ?
            document_state->start_load_time() :
            document_state->request_time();
        base::Time onload = document_state->finish_document_load_time();
        base::TimeDelta page = now - start;
        csi->Set(
            v8::String::New("startE"),
            v8::Number::New(floor(start.ToDoubleT() * 1000)));
        csi->Set(
            v8::String::New("onloadT"),
            v8::Number::New(floor(onload.ToDoubleT() * 1000)));
        csi->Set(
          v8::String::New("pageT"),
          v8::Number::New(page.InMillisecondsF()));
        csi->Set(
            v8::String::New("tran"),
            v8::Number::New(
                GetCSITransitionType(data_source->navigationType())));

        args.GetReturnValue().Set(csi);
        return;
      }
    }
    args.GetReturnValue().SetNull();
    return;
  }
};

v8::Extension* LoadTimesExtension::Get() {
  return new LoadTimesExtensionWrapper();
}

}  // namespace extensions_v8
