// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_TRANSLATE_TRANSLATE_BUBBLE_MODEL_IMPL_H_
#define CHROME_BROWSER_UI_TRANSLATE_TRANSLATE_BUBBLE_MODEL_IMPL_H_

#include "base/basictypes.h"
#include "base/memory/scoped_ptr.h"
#include "chrome/browser/ui/translate/translate_bubble_model.h"
#include "chrome/browser/ui/translate/translate_bubble_view_state_transition.h"

class TranslateUIDelegate;

// The standard implementation of TranslateBubbleModel.
class TranslateBubbleModelImpl : public TranslateBubbleModel {
 public:
  TranslateBubbleModelImpl(TranslateBubbleModel::ViewState view_type,
                           scoped_ptr<TranslateUIDelegate> ui_delegate);
  virtual ~TranslateBubbleModelImpl();

  // TranslateBubbleModel methods.
  virtual TranslateBubbleModel::ViewState GetViewState() const OVERRIDE;
  virtual void SetViewState(TranslateBubbleModel::ViewState view_state)
      OVERRIDE;
  virtual void GoBackFromAdvanced() OVERRIDE;
  virtual int GetNumberOfLanguages() const OVERRIDE;
  virtual string16 GetLanguageNameAt(int index) const OVERRIDE;
  virtual int GetOriginalLanguageIndex() const OVERRIDE;
  virtual void UpdateOriginalLanguageIndex(int index) OVERRIDE;
  virtual int GetTargetLanguageIndex() const OVERRIDE;
  virtual void UpdateTargetLanguageIndex(int index) OVERRIDE;
  virtual void SetNeverTranslateLanguage(bool value) OVERRIDE;
  virtual void SetNeverTranslateSite(bool value) OVERRIDE;
  virtual bool ShouldAlwaysTranslate() const OVERRIDE;
  virtual void SetAlwaysTranslate(bool value) OVERRIDE;
  virtual void Translate() OVERRIDE;
  virtual void RevertTranslation() OVERRIDE;
  virtual void TranslationDeclined() OVERRIDE;

 private:
  scoped_ptr<TranslateUIDelegate> ui_delegate_;
  TranslateBubbleViewStateTransition view_state_transition_;

  DISALLOW_COPY_AND_ASSIGN(TranslateBubbleModelImpl);
};

#endif  // CHROME_BROWSER_UI_TRANSLATE_TRANSLATE_BUBBLE_MODEL_IMPL_H_
