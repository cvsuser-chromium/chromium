// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ANDROID_WEBVIEW_NATIVE_AW_WEB_PREFERENCES_POPULATER_IMPL_H_
#define ANDROID_WEBVIEW_NATIVE_AW_WEB_PREFERENCES_POPULATER_IMPL_H_

#include "android_webview/browser/aw_web_preferences_populater.h"

#include "base/compiler_specific.h"

struct WebPreferences;

namespace content {
class WebContents;
}

namespace android_webview {

class AwSettings;

class AwWebPreferencesPopulaterImpl : public AwWebPreferencesPopulater {
 public:
  AwWebPreferencesPopulaterImpl();
  virtual ~AwWebPreferencesPopulaterImpl();

  // AwWebPreferencesPopulater
  virtual void PopulateFor(content::WebContents* web_contents,
                           WebPreferences* web_prefs) OVERRIDE;
};

}

#endif  // ANDROID_WEBVIEW_NATIVE_AW_WEB_PREFERENCES_POPULATER_IMPL_H_
