// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "android_webview/native/aw_settings.h"

#include "android_webview/browser/renderer_host/aw_render_view_host_ext.h"
#include "android_webview/native/aw_contents.h"
#include "base/android/jni_android.h"
#include "base/android/jni_string.h"
#include "base/supports_user_data.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_client.h"
#include "jni/AwSettings_jni.h"
#include "webkit/common/user_agent/user_agent.h"
#include "webkit/common/webpreferences.h"
#include "webkit/glue/webkit_glue.h"

using base::android::ConvertJavaStringToUTF16;
using base::android::ConvertUTF8ToJavaString;
using base::android::ScopedJavaLocalRef;

namespace android_webview {

const void* kAwSettingsUserDataKey = &kAwSettingsUserDataKey;

class AwSettingsUserData : public base::SupportsUserData::Data {
 public:
  AwSettingsUserData(AwSettings* ptr) : settings_(ptr) {}

  static AwSettings* GetSettings(content::WebContents* web_contents) {
    if (!web_contents)
      return NULL;
    AwSettingsUserData* data = reinterpret_cast<AwSettingsUserData*>(
        web_contents->GetUserData(kAwSettingsUserDataKey));
    return data ? data->settings_ : NULL;
  }

 private:
  AwSettings* settings_;
};

AwSettings::AwSettings(JNIEnv* env, jobject obj, jint web_contents)
    : WebContentsObserver(
          reinterpret_cast<content::WebContents*>(web_contents)),
      aw_settings_(env, obj) {
  reinterpret_cast<content::WebContents*>(web_contents)->
      SetUserData(kAwSettingsUserDataKey, new AwSettingsUserData(this));
}

AwSettings::~AwSettings() {
  if (web_contents()) {
    web_contents()->SetUserData(kAwSettingsUserDataKey, NULL);
  }

  JNIEnv* env = base::android::AttachCurrentThread();
  ScopedJavaLocalRef<jobject> scoped_obj = aw_settings_.get(env);
  jobject obj = scoped_obj.obj();
  if (!obj) return;
  Java_AwSettings_nativeAwSettingsGone(env, obj,
                                       reinterpret_cast<jint>(this));
}

void AwSettings::Destroy(JNIEnv* env, jobject obj) {
  delete this;
}

AwSettings* AwSettings::FromWebContents(content::WebContents* web_contents) {
  return AwSettingsUserData::GetSettings(web_contents);
}

AwRenderViewHostExt* AwSettings::GetAwRenderViewHostExt() {
  if (!web_contents()) return NULL;
  AwContents* contents = AwContents::FromWebContents(web_contents());
  if (!contents) return NULL;
  return contents->render_view_host_ext();
}

void AwSettings::ResetScrollAndScaleState(JNIEnv* env, jobject obj) {
  AwRenderViewHostExt* rvhe = GetAwRenderViewHostExt();
  if (!rvhe) return;
  rvhe->ResetScrollAndScaleState();
}

void AwSettings::UpdateEverything() {
  JNIEnv* env = base::android::AttachCurrentThread();
  CHECK(env);
  ScopedJavaLocalRef<jobject> scoped_obj = aw_settings_.get(env);
  jobject obj = scoped_obj.obj();
  if (!obj) return;
  // Grab the lock and call UpdateEverythingLocked.
  Java_AwSettings_updateEverything(env, obj);
}

void AwSettings::UpdateEverythingLocked(JNIEnv* env, jobject obj) {
  UpdateInitialPageScaleLocked(env, obj);
  UpdateWebkitPreferencesLocked(env, obj);
  UpdateUserAgentLocked(env, obj);
  ResetScrollAndScaleState(env, obj);
  UpdateFormDataPreferencesLocked(env, obj);
}

void AwSettings::UpdateUserAgentLocked(JNIEnv* env, jobject obj) {
  if (!web_contents()) return;

  ScopedJavaLocalRef<jstring> str =
      Java_AwSettings_getUserAgentLocked(env, obj);
  bool ua_overidden = str.obj() != NULL;

  if (ua_overidden) {
    std::string override = base::android::ConvertJavaStringToUTF8(str);
    web_contents()->SetUserAgentOverride(override);
  }

  const content::NavigationController& controller =
      web_contents()->GetController();
  for (int i = 0; i < controller.GetEntryCount(); ++i)
    controller.GetEntryAtIndex(i)->SetIsOverridingUserAgent(ua_overidden);
}

void AwSettings::UpdateWebkitPreferencesLocked(JNIEnv* env, jobject obj) {
  if (!web_contents()) return;
  AwRenderViewHostExt* render_view_host_ext = GetAwRenderViewHostExt();
  if (!render_view_host_ext) return;

  content::RenderViewHost* render_view_host =
      web_contents()->GetRenderViewHost();
  if (!render_view_host) return;
  render_view_host->UpdateWebkitPreferences(
      render_view_host->GetWebkitPreferences());
}

void AwSettings::UpdateInitialPageScaleLocked(JNIEnv* env, jobject obj) {
  AwRenderViewHostExt* rvhe = GetAwRenderViewHostExt();
  if (!rvhe) return;

  float initial_page_scale_percent =
      Java_AwSettings_getInitialPageScalePercentLocked(env, obj);
  if (initial_page_scale_percent == 0) {
    rvhe->SetInitialPageScale(-1);
  } else {
    float dip_scale = static_cast<float>(
        Java_AwSettings_getDIPScaleLocked(env, obj));
    rvhe->SetInitialPageScale(initial_page_scale_percent / dip_scale / 100.0f);
  }
}

void AwSettings::UpdateFormDataPreferencesLocked(JNIEnv* env, jobject obj) {
  if (!web_contents()) return;
  AwContents* contents = AwContents::FromWebContents(web_contents());
  if (!contents) return;

  contents->SetSaveFormData(Java_AwSettings_getSaveFormDataLocked(env, obj));
}

void AwSettings::RenderViewCreated(content::RenderViewHost* render_view_host) {
  // A single WebContents can normally have 0 to many RenderViewHost instances
  // associated with it.
  // This is important since there is only one RenderViewHostExt instance per
  // WebContents (and not one RVHExt per RVH, as you might expect) and updating
  // settings via RVHExt only ever updates the 'current' RVH.
  // In android_webview we don't swap out the RVH on cross-site navigations, so
  // we shouldn't have to deal with the multiple RVH per WebContents case. That
  // in turn means that the newly created RVH is always the 'current' RVH
  // (since we only ever go from 0 to 1 RVH instances) and hence the DCHECK.
  DCHECK(web_contents()->GetRenderViewHost() == render_view_host);

  UpdateEverything();
}

void AwSettings::WebContentsDestroyed(content::WebContents* web_contents) {
  delete this;
}

// static
void AwSettings::PopulateFixedPreferences(WebPreferences* web_prefs) {
  web_prefs->shrinks_standalone_images_to_fit = false;
}

void AwSettings::PopulateWebPreferences(WebPreferences* web_prefs) {
  JNIEnv* env = base::android::AttachCurrentThread();
  CHECK(env);

  AwRenderViewHostExt* render_view_host_ext = GetAwRenderViewHostExt();
  if (!render_view_host_ext) return;

  ScopedJavaLocalRef<jobject> scoped_obj = aw_settings_.get(env);
  jobject obj = scoped_obj.obj();
  if (!obj) return;

  PopulateFixedPreferences(web_prefs);

  web_prefs->text_autosizing_enabled =
      Java_AwSettings_getTextAutosizingEnabledLocked(env, obj);

  int text_size_percent = Java_AwSettings_getTextSizePercentLocked(env, obj);
  if (web_prefs->text_autosizing_enabled) {
    web_prefs->font_scale_factor = text_size_percent / 100.0f;
    web_prefs->force_enable_zoom = text_size_percent >= 130;
    // Use the default zoom factor value when Text Autosizer is turned on.
    render_view_host_ext->SetTextZoomFactor(1);
  } else {
    web_prefs->force_enable_zoom = false;
    render_view_host_ext->SetTextZoomFactor(text_size_percent / 100.0f);
  }

  web_prefs->standard_font_family_map[webkit_glue::kCommonScript] =
      ConvertJavaStringToUTF16(
          Java_AwSettings_getStandardFontFamilyLocked(env, obj));

  web_prefs->fixed_font_family_map[webkit_glue::kCommonScript] =
      ConvertJavaStringToUTF16(
          Java_AwSettings_getFixedFontFamilyLocked(env, obj));

  web_prefs->sans_serif_font_family_map[webkit_glue::kCommonScript] =
      ConvertJavaStringToUTF16(
          Java_AwSettings_getSansSerifFontFamilyLocked(env, obj));

  web_prefs->serif_font_family_map[webkit_glue::kCommonScript] =
      ConvertJavaStringToUTF16(
          Java_AwSettings_getSerifFontFamilyLocked(env, obj));

  web_prefs->cursive_font_family_map[webkit_glue::kCommonScript] =
      ConvertJavaStringToUTF16(
          Java_AwSettings_getCursiveFontFamilyLocked(env, obj));

  web_prefs->fantasy_font_family_map[webkit_glue::kCommonScript] =
      ConvertJavaStringToUTF16(
          Java_AwSettings_getFantasyFontFamilyLocked(env, obj));

  web_prefs->default_encoding = ConvertJavaStringToUTF8(
      Java_AwSettings_getDefaultTextEncodingLocked(env, obj));

  web_prefs->minimum_font_size =
      Java_AwSettings_getMinimumFontSizeLocked(env, obj);

  web_prefs->minimum_logical_font_size =
      Java_AwSettings_getMinimumLogicalFontSizeLocked(env, obj);

  web_prefs->default_font_size =
      Java_AwSettings_getDefaultFontSizeLocked(env, obj);

  web_prefs->default_fixed_font_size =
      Java_AwSettings_getDefaultFixedFontSizeLocked(env, obj);

  // Blink's LoadsImagesAutomatically and ImagesEnabled must be
  // set cris-cross to Android's. See
  // https://code.google.com/p/chromium/issues/detail?id=224317#c26
  web_prefs->loads_images_automatically =
      Java_AwSettings_getImagesEnabledLocked(env, obj);
  web_prefs->images_enabled =
      Java_AwSettings_getLoadsImagesAutomaticallyLocked(env, obj);

  web_prefs->javascript_enabled =
      Java_AwSettings_getJavaScriptEnabledLocked(env, obj);

  web_prefs->allow_universal_access_from_file_urls =
      Java_AwSettings_getAllowUniversalAccessFromFileURLsLocked(env, obj);

  web_prefs->allow_file_access_from_file_urls =
      Java_AwSettings_getAllowFileAccessFromFileURLsLocked(env, obj);

  web_prefs->javascript_can_open_windows_automatically =
      Java_AwSettings_getJavaScriptCanOpenWindowsAutomaticallyLocked(env, obj);

  web_prefs->supports_multiple_windows =
      Java_AwSettings_getSupportMultipleWindowsLocked(env, obj);

  web_prefs->plugins_enabled =
      !Java_AwSettings_getPluginsDisabledLocked(env, obj);

  web_prefs->application_cache_enabled =
      Java_AwSettings_getAppCacheEnabledLocked(env, obj);

  web_prefs->local_storage_enabled =
      Java_AwSettings_getDomStorageEnabledLocked(env, obj);

  web_prefs->databases_enabled =
      Java_AwSettings_getDatabaseEnabledLocked(env, obj);

  web_prefs->wide_viewport_quirk = true;
  web_prefs->double_tap_to_zoom_enabled = web_prefs->use_wide_viewport =
      Java_AwSettings_getUseWideViewportLocked(env, obj);

  web_prefs->initialize_at_minimum_page_scale =
      Java_AwSettings_getLoadWithOverviewModeLocked(env, obj);

  web_prefs->user_gesture_required_for_media_playback =
      Java_AwSettings_getMediaPlaybackRequiresUserGestureLocked(env, obj);

  ScopedJavaLocalRef<jstring> url =
      Java_AwSettings_getDefaultVideoPosterURLLocked(env, obj);
  web_prefs->default_video_poster_url = url.obj() ?
      GURL(ConvertJavaStringToUTF8(url)) : GURL();

  bool support_quirks = Java_AwSettings_getSupportLegacyQuirksLocked(env, obj);
  web_prefs->support_deprecated_target_density_dpi = support_quirks;
  web_prefs->use_legacy_background_size_shorthand_behavior = support_quirks;
  web_prefs->viewport_meta_layout_size_quirk = support_quirks;
  web_prefs->viewport_meta_merge_content_quirk = support_quirks;
  web_prefs->viewport_meta_zero_values_quirk = support_quirks;
  web_prefs->ignore_main_frame_overflow_hidden_quirk = support_quirks;
  web_prefs->report_screen_size_in_physical_pixels_quirk = support_quirks;

  web_prefs->password_echo_enabled =
      Java_AwSettings_getPasswordEchoEnabledLocked(env, obj);
  web_prefs->spatial_navigation_enabled =
      Java_AwSettings_getSpatialNavigationLocked(env, obj);
}

static jint Init(JNIEnv* env,
                 jobject obj,
                 jint web_contents) {
  AwSettings* settings = new AwSettings(env, obj, web_contents);
  return reinterpret_cast<jint>(settings);
}

static jstring GetDefaultUserAgent(JNIEnv* env, jclass clazz) {
  return base::android::ConvertUTF8ToJavaString(
      env, content::GetUserAgent(GURL())).Release();
}

bool RegisterAwSettings(JNIEnv* env) {
  return RegisterNativesImpl(env) >= 0;
}

}  // namespace android_webview
