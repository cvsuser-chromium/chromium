// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Implements the Chrome Extensions Tab Capture API.

#include "chrome/browser/extensions/api/tab_capture/tab_capture_api.h"

#include <set>
#include <string>
#include <vector>

#include "base/command_line.h"
#include "base/strings/stringprintf.h"
#include "base/values.h"
#include "chrome/browser/extensions/api/tab_capture/tab_capture_registry.h"
#include "chrome/browser/extensions/event_names.h"
#include "chrome/browser/extensions/extension_renderer_state.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/session_tab_helper.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/extensions/features/simple_feature.h"
#include "chrome/common/extensions/permissions/permissions_data.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_view_host.h"
#include "extensions/common/features/feature.h"
#include "extensions/common/features/feature_provider.h"

using extensions::api::tab_capture::MediaStreamConstraint;

namespace TabCapture = extensions::api::tab_capture;
namespace GetCapturedTabs = TabCapture::GetCapturedTabs;

namespace extensions {

namespace {

const char kCapturingSameTab[] = "Cannot capture a tab with an active stream.";
const char kFindingTabError[] = "Error finding tab to capture.";
const char kNoAudioOrVideo[] = "Capture failed. No audio or video requested.";
const char kGrantError[] =
    "Extension has not been invoked for the current page (see activeTab "
    "permission). Chrome pages cannot be captured.";

// Keys/values for media stream constraints.
const char kMediaStreamSource[] = "chromeMediaSource";
const char kMediaStreamSourceId[] = "chromeMediaSourceId";
const char kMediaStreamSourceTab[] = "tab";

// Whitelisted extensions that do not check for a browser action grant because
// they provide API's.
const char* whitelisted_extensions[] = {
  "enhhojjnijigcajfphajepfemndkmdlo",  // Dev
  "pkedcjkdefgpdelpbcmbmeomcjbeemfm",  // Trusted Tester
  "fmfcbgogabcbclcofgocippekhfcmgfj",  // Staging
  "hfaagokkkhdbgiakmmlclaapfelnkoah",  // Canary
  "F155646B5D1CA545F7E1E4E20D573DFDD44C2540",  // Trusted Tester (public)
  "16CA7A47AAE4BE49B1E75A6B960C3875E945B264"   // Release
};

}  // namespace

bool TabCaptureCaptureFunction::RunImpl() {
  scoped_ptr<api::tab_capture::Capture::Params> params =
      TabCapture::Capture::Params::Create(*args_);
  EXTENSION_FUNCTION_VALIDATE(params.get());

  // Figure out the active WebContents and retrieve the needed ids.
  Browser* target_browser = chrome::FindAnyBrowser(
      GetProfile(), include_incognito(), chrome::GetActiveDesktop());
  if (!target_browser) {
    error_ = kFindingTabError;
    return false;
  }

  content::WebContents* target_contents =
      target_browser->tab_strip_model()->GetActiveWebContents();
  if (!target_contents) {
    error_ = kFindingTabError;
    return false;
  }

  const Extension* extension = GetExtension();
  const std::string& extension_id = extension->id();

  const int tab_id = SessionID::IdForTab(target_contents);

  // Make sure either we have been granted permission to capture through an
  // extension icon click or our extension is whitelisted.
  if (!PermissionsData::HasAPIPermissionForTab(
          extension, tab_id, APIPermission::kTabCaptureForTab) &&
      CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
          switches::kWhitelistedExtensionID) != extension_id &&
      !SimpleFeature::IsIdInWhitelist(
          extension_id,
          std::set<std::string>(
              whitelisted_extensions,
              whitelisted_extensions + arraysize(whitelisted_extensions)))) {
    error_ = kGrantError;
    return false;
  }

  content::RenderViewHost* const rvh = target_contents->GetRenderViewHost();
  int render_process_id = rvh->GetProcess()->GetID();
  int routing_id = rvh->GetRoutingID();

  // Create a constraints vector. We will modify all the constraints in this
  // vector to append our chrome specific constraints.
  std::vector<MediaStreamConstraint*> constraints;
  bool has_audio = params->options.audio.get() && *params->options.audio.get();
  bool has_video = params->options.video.get() && *params->options.video.get();

  if (!has_audio && !has_video) {
    error_ = kNoAudioOrVideo;
    return false;
  }

  if (has_audio) {
    if (!params->options.audio_constraints.get())
      params->options.audio_constraints.reset(new MediaStreamConstraint);

    constraints.push_back(params->options.audio_constraints.get());
  }
  if (has_video) {
    if (!params->options.video_constraints.get())
      params->options.video_constraints.reset(new MediaStreamConstraint);

    constraints.push_back(params->options.video_constraints.get());
  }

  // Device id we use for Tab Capture.
  std::string device_id =
      base::StringPrintf("%i:%i", render_process_id, routing_id);

  // Append chrome specific tab constraints.
  for (std::vector<MediaStreamConstraint*>::iterator it = constraints.begin();
       it != constraints.end(); ++it) {
    base::DictionaryValue* constraint = &(*it)->mandatory.additional_properties;
    constraint->SetString(kMediaStreamSource, kMediaStreamSourceTab);
    constraint->SetString(kMediaStreamSourceId, device_id);
  }

  extensions::TabCaptureRegistry* registry =
      extensions::TabCaptureRegistry::Get(GetProfile());
  if (!registry->AddRequest(render_process_id,
                            routing_id,
                            extension_id,
                            tab_id,
                            tab_capture::TAB_CAPTURE_STATE_NONE)) {
    error_ = kCapturingSameTab;
    return false;
  }

  // Copy the result from our modified input parameters. This will be
  // intercepted by custom bindings which will build and send the special
  // WebRTC user media request.
  base::DictionaryValue* result = new base::DictionaryValue();
  result->MergeDictionary(params->options.ToValue().get());

  SetResult(result);
  return true;
}

bool TabCaptureGetCapturedTabsFunction::RunImpl() {
  extensions::TabCaptureRegistry* registry =
      extensions::TabCaptureRegistry::Get(GetProfile());

  const TabCaptureRegistry::RegistryCaptureInfo& captured_tabs =
      registry->GetCapturedTabs(GetExtension()->id());

  base::ListValue *list = new base::ListValue();
  for (TabCaptureRegistry::RegistryCaptureInfo::const_iterator it =
       captured_tabs.begin(); it != captured_tabs.end(); ++it) {
    scoped_ptr<tab_capture::CaptureInfo> info(new tab_capture::CaptureInfo());
    info->tab_id = it->first;
    info->status = it->second;
    list->Append(info->ToValue().release());
  }

  SetResult(list);
  return true;
}

}  // namespace extensions
