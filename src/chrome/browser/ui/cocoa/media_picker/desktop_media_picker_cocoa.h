// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_COCOA_MEDIA_PICKER_DESKTOP_MEDIA_PICKER_COCOA_H_
#define CHROME_BROWSER_UI_COCOA_MEDIA_PICKER_DESKTOP_MEDIA_PICKER_COCOA_H_

#import "base/mac/scoped_nsobject.h"
#include "chrome/browser/media/desktop_media_picker.h"

@class DesktopMediaPickerController;

// Cocoa's DesktopMediaPicker implementation.
class DesktopMediaPickerCocoa : public DesktopMediaPicker {
 public:
  DesktopMediaPickerCocoa();
  virtual ~DesktopMediaPickerCocoa();

  // Overridden from DesktopMediaPicker:
  virtual void Show(gfx::NativeWindow context,
                    gfx::NativeWindow parent,
                    const string16& app_name,
                    scoped_ptr<DesktopMediaPickerModel> model,
                    const DoneCallback& done_callback) OVERRIDE;

 private:
  base::scoped_nsobject<DesktopMediaPickerController> controller_;
};

#endif  // CHROME_BROWSER_UI_COCOA_MEDIA_PICKER_DESKTOP_MEDIA_PICKER_COCOA_H_
