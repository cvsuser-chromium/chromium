// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_UI_NATIVE_APP_WINDOW_H_
#define APPS_UI_NATIVE_APP_WINDOW_H_

#include "apps/shell_window.h"
#include "components/web_modal/web_contents_modal_dialog_host.h"
#include "ui/base/base_window.h"
#include "ui/gfx/insets.h"

namespace apps {

// This is an interface to a native implementation of a shell window, used for
// new-style packaged apps. Shell windows contain a web contents, but no tabs
// or URL bar.
class NativeAppWindow : public ui::BaseWindow,
                        public web_modal::WebContentsModalDialogHost {
 public:
  // Fullscreen changes may be asynchronous on some platforms.
  virtual void SetFullscreen(bool fullscreen) = 0;
  virtual bool IsFullscreenOrPending() const = 0;

  // Returns true if the window is a panel that has been detached.
  virtual bool IsDetached() const = 0;

  // Called when the icon of the window changes.
  virtual void UpdateWindowIcon() = 0;

  // Called when the title of the window changes.
  virtual void UpdateWindowTitle() = 0;

  // Called when the draggable regions are changed.
  virtual void UpdateDraggableRegions(
      const std::vector<extensions::DraggableRegion>& regions) = 0;

  // Returns the region used by frameless windows for dragging. May return NULL.
  virtual SkRegion* GetDraggableRegion() = 0;

  // Called when the region that accepts input events is changed.
  // If |region| is NULL, then the entire window will accept input events.
  virtual void UpdateInputRegion(scoped_ptr<SkRegion> region) = 0;

  // Allows the window to handle unhandled keyboard messages coming back from
  // the renderer.
  virtual void HandleKeyboardEvent(
      const content::NativeWebKeyboardEvent& event) = 0;

  // Returns true if the window has no frame, as for a window opened by
  // chrome.app.window.create with the option 'frame' set to 'none'.
  virtual bool IsFrameless() const = 0;

  // Returns the difference between the window bounds (including titlebar and
  // borders) and the content bounds, if any.
  virtual gfx::Insets GetFrameInsets() const = 0;

  // Hide or show this window as part of hiding or showing the app.
  // This may have different logic to Hide, Show, and ShowInactive as those are
  // called via the AppWindow javascript API.
  virtual void ShowWithApp() = 0;
  virtual void HideWithApp() = 0;

  // Updates the minimum and maximum size of the native window with the current
  // size constraints.
  virtual void UpdateWindowMinMaxSize() = 0;

  virtual ~NativeAppWindow() {}
};

}  // namespace apps

#endif  // APPS_UI_NATIVE_APP_WINDOW_H_
