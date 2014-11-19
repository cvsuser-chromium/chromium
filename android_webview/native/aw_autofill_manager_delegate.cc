// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "android_webview/native/aw_autofill_manager_delegate.h"

#include "android_webview/browser/aw_browser_context.h"
#include "android_webview/browser/aw_content_browser_client.h"
#include "android_webview/browser/aw_pref_store.h"
#include "android_webview/native/aw_contents.h"
#include "base/android/jni_android.h"
#include "base/android/jni_string.h"
#include "base/android/scoped_java_ref.h"
#include "base/logging.h"
#include "base/prefs/pref_registry_simple.h"
#include "base/prefs/pref_service.h"
#include "base/prefs/pref_service_builder.h"
#include "components/autofill/core/browser/autofill_popup_delegate.h"
#include "components/autofill/core/common/autofill_pref_names.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_view.h"
#include "jni/AwAutofillManagerDelegate_jni.h"

using base::android::AttachCurrentThread;
using base::android::ConvertUTF16ToJavaString;
using base::android::ScopedJavaLocalRef;
using content::WebContents;

DEFINE_WEB_CONTENTS_USER_DATA_KEY(android_webview::AwAutofillManagerDelegate);

namespace android_webview {

// Ownership: The native object is created (if autofill enabled) and owned by
// AwContents. The native object creates the java peer which handles most
// autofill functionality at the java side. The java peer is owned by Java
// AwContents. The native object only maintains a weak ref to it.
AwAutofillManagerDelegate::AwAutofillManagerDelegate(WebContents* contents)
    : web_contents_(contents),
      save_form_data_(false) {
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> delegate;
  delegate.Reset(
      Java_AwAutofillManagerDelegate_create(env, reinterpret_cast<jint>(this)));

  AwContents* aw_contents = AwContents::FromWebContents(web_contents_);
  aw_contents->SetAwAutofillManagerDelegate(delegate.obj());
  java_ref_ = JavaObjectWeakGlobalRef(env, delegate.obj());
}

AwAutofillManagerDelegate::~AwAutofillManagerDelegate() {
  HideAutofillPopup();
}

void AwAutofillManagerDelegate::SetSaveFormData(bool enabled) {
  save_form_data_ = enabled;
}

bool AwAutofillManagerDelegate::GetSaveFormData() {
  return save_form_data_;
}

PrefService* AwAutofillManagerDelegate::GetPrefs() {
  return user_prefs::UserPrefs::Get(
      AwContentBrowserClient::GetAwBrowserContext());
}

autofill::PersonalDataManager*
AwAutofillManagerDelegate::GetPersonalDataManager() {
  return NULL;
}

void AwAutofillManagerDelegate::ShowAutofillPopup(
    const gfx::RectF& element_bounds,
    base::i18n::TextDirection text_direction,
    const std::vector<string16>& values,
    const std::vector<string16>& labels,
    const std::vector<string16>& icons,
    const std::vector<int>& identifiers,
    base::WeakPtr<autofill::AutofillPopupDelegate> delegate) {

  values_ = values;
  identifiers_ = identifiers;
  delegate_ = delegate;

  // Convert element_bounds to be in screen space.
  gfx::Rect client_area;
  web_contents_->GetView()->GetContainerBounds(&client_area);
  gfx::RectF element_bounds_in_screen_space =
      element_bounds + client_area.OffsetFromOrigin();

  ShowAutofillPopupImpl(element_bounds_in_screen_space,
                        values,
                        labels,
                        identifiers);
}

void AwAutofillManagerDelegate::ShowAutofillPopupImpl(
    const gfx::RectF& element_bounds,
    const std::vector<string16>& values,
    const std::vector<string16>& labels,
    const std::vector<int>& identifiers) {
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> obj = java_ref_.get(env);
  if (obj.is_null())
    return;

  // We need an array of AutofillSuggestion.
  size_t count = values.size();

  ScopedJavaLocalRef<jobjectArray> data_array =
      Java_AwAutofillManagerDelegate_createAutofillSuggestionArray(env, count);

  for (size_t i = 0; i < count; ++i) {
    ScopedJavaLocalRef<jstring> name = ConvertUTF16ToJavaString(env, values[i]);
    ScopedJavaLocalRef<jstring> label =
        ConvertUTF16ToJavaString(env, labels[i]);
    Java_AwAutofillManagerDelegate_addToAutofillSuggestionArray(
        env,
        data_array.obj(),
        i,
        name.obj(),
        label.obj(),
        identifiers[i]);
  }

  Java_AwAutofillManagerDelegate_showAutofillPopup(
      env,
      obj.obj(),
      element_bounds.x(),
      element_bounds.y(), element_bounds.width(),
      element_bounds.height(), data_array.obj());
}

void AwAutofillManagerDelegate::UpdateAutofillPopupDataListValues(
    const std::vector<base::string16>& values,
    const std::vector<base::string16>& labels) {
  // Leaving as an empty method since updating autofill popup window
  // dynamically does not seem to be a useful feature for android webview.
  // See crrev.com/18102002 if need to implement.
}

void AwAutofillManagerDelegate::HideAutofillPopup() {
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> obj = java_ref_.get(env);
  if (obj.is_null())
    return;
  delegate_.reset();
  Java_AwAutofillManagerDelegate_hideAutofillPopup(env, obj.obj());
}

bool AwAutofillManagerDelegate::IsAutocompleteEnabled() {
  return GetSaveFormData();
}

void AwAutofillManagerDelegate::DetectAccountCreationForms(
    const std::vector<autofill::FormStructure*>& forms) {}

void AwAutofillManagerDelegate::SuggestionSelected(JNIEnv* env,
                                                   jobject object,
                                                   jint position) {
  if (delegate_)
    delegate_->DidAcceptSuggestion(values_[position], identifiers_[position]);
}

void AwAutofillManagerDelegate::HideRequestAutocompleteDialog() {
  NOTIMPLEMENTED();
}

void AwAutofillManagerDelegate::ShowAutofillSettings() {
  NOTIMPLEMENTED();
}

void AwAutofillManagerDelegate::ConfirmSaveCreditCard(
    const autofill::AutofillMetrics& metric_logger,
    const autofill::CreditCard& credit_card,
    const base::Closure& save_card_callback) {
  NOTIMPLEMENTED();
}

void AwAutofillManagerDelegate::ShowRequestAutocompleteDialog(
    const autofill::FormData& form,
    const GURL& source_url,
    const base::Callback<void(const autofill::FormStructure*)>& callback) {
  NOTIMPLEMENTED();
}

bool RegisterAwAutofillManagerDelegate(JNIEnv* env) {
  return RegisterNativesImpl(env) >= 0;
}

} // namespace android_webview
