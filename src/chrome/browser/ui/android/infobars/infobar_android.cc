// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/android/infobars/infobar_android.h"

#include "base/android/jni_android.h"
#include "base/android/jni_string.h"
#include "base/strings/string_util.h"
#include "chrome/browser/android/resource_mapper.h"
#include "chrome/browser/infobars/infobar.h"
#include "chrome/browser/infobars/infobar_delegate.h"
#include "chrome/browser/infobars/infobar_service.h"
#include "jni/InfoBar_jni.h"


// InfoBar --------------------------------------------------------------------

// Static constants defined in infobar.h.  We don't really use them for anything
// but they are required.  The values are copied from the GTK implementation.
const int InfoBar::kSeparatorLineHeight = 1;
const int InfoBar::kDefaultArrowTargetHeight = 9;
const int InfoBar::kMaximumArrowTargetHeight = 24;
const int InfoBar::kDefaultArrowTargetHalfWidth = kDefaultArrowTargetHeight;
const int InfoBar::kMaximumArrowTargetHalfWidth = 14;
const int InfoBar::kDefaultBarTargetHeight = 36;


// InfoBarAndroid -------------------------------------------------------------

InfoBarAndroid::InfoBarAndroid(InfoBarService* owner, InfoBarDelegate* delegate)
    : InfoBar(owner, delegate),
      delegate_(delegate) {
  DCHECK(delegate_);
  DCHECK(delegate_->owner());
}

InfoBarAndroid::~InfoBarAndroid() {
}

void InfoBarAndroid::ReassignJavaInfoBar(InfoBarAndroid* replacement) {
  DCHECK(replacement);
  if (!java_info_bar_.is_null()) {
    replacement->set_java_infobar(java_info_bar_);
    java_info_bar_.Reset();
  }
}

void InfoBarAndroid::set_java_infobar(
    const base::android::JavaRef<jobject>& java_info_bar) {
  DCHECK(java_info_bar_.is_null());
  java_info_bar_.Reset(java_info_bar);
}

bool InfoBarAndroid::HasSetJavaInfoBar() const {
  return !java_info_bar_.is_null();
}

void InfoBarAndroid::OnButtonClicked(JNIEnv* env,
                                     jobject obj,
                                     jint action,
                                     jstring action_value) {
  DCHECK(delegate_);
  std::string value = base::android::ConvertJavaStringToUTF8(env, action_value);
  ProcessButton(action, value);
}

void InfoBarAndroid::OnCloseButtonClicked(JNIEnv* env, jobject obj) {
  delegate_->InfoBarDismissed();
  RemoveSelf();
}

void InfoBarAndroid::CloseJavaInfoBar() {
  if (!java_info_bar_.is_null()) {
    JNIEnv* env = base::android::AttachCurrentThread();
    Java_InfoBar_closeInfoBar(env, java_info_bar_.obj());
  }
}

int InfoBarAndroid::GetEnumeratedIconId() {
  DCHECK(delegate_);
  return ResourceMapper::MapFromChromiumId(delegate_->GetIconID());
}


// Native JNI methods ---------------------------------------------------------

bool RegisterNativeInfoBar(JNIEnv* env) {
  return RegisterNativesImpl(env);
}
