// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_GLOBAL_SHORTCUT_LISTENER_X11_H_
#define CHROME_BROWSER_EXTENSIONS_GLOBAL_SHORTCUT_LISTENER_X11_H_

#include <set>
#include <X11/Xlib.h>

#include "base/lazy_instance.h"
#include "base/message_loop/message_pump_dispatcher.h"
#include "chrome/browser/extensions/global_shortcut_listener.h"

#if defined(TOOLKIT_GTK)
#include <gtk/gtk.h>
#include "ui/base/gtk/gtk_signal.h"
#endif  // defined(TOOLKIT_GTK)

namespace extensions {

// X11-specific implementation of the GlobalShortcutListener class that
// listens for global shortcuts. Handles basic keyboard intercepting and
// forwards its output to the base class for processing.
class GlobalShortcutListenerX11 : public GlobalShortcutListener,
                                  public base::MessagePumpDispatcher {
 public:
  virtual ~GlobalShortcutListenerX11();

  // GlobalShortcutListener implementation.
  virtual void StartListening() OVERRIDE;
  virtual void StopListening() OVERRIDE;

  // base::MessagePumpDispatcher implementation.
  virtual bool Dispatch(const base::NativeEvent& event) OVERRIDE;

 private:
  friend struct base::DefaultLazyInstanceTraits<GlobalShortcutListenerX11>;

  GlobalShortcutListenerX11();

  // Register an |accelerator| with the particular |observer|.
  virtual void RegisterAccelerator(
      const ui::Accelerator& accelerator,
      GlobalShortcutListener::Observer* observer) OVERRIDE;
  // Unregister an |accelerator| with the particular |observer|.
  virtual void UnregisterAccelerator(
      const ui::Accelerator& accelerator,
      GlobalShortcutListener::Observer* observer) OVERRIDE;

#if defined(TOOLKIT_GTK)
  // Callback for XEvents of the default root window.
  CHROMEG_CALLBACK_1(GlobalShortcutListenerX11, GdkFilterReturn,
                     OnXEvent, GdkXEvent*, GdkEvent*);
#endif

  // Invoked when a global shortcut is pressed.
  void OnXKeyPressEvent(::XEvent* x_event);

  // Whether this object is listening for global shortcuts.
  bool is_listening_;

  // The x11 default display and the native root window.
  ::Display* x_display_;
  ::Window x_root_window_;

  // A set of registered accelerators.
  typedef std::set<ui::Accelerator> RegisteredHotKeys;
  RegisteredHotKeys registered_hot_keys_;

  DISALLOW_COPY_AND_ASSIGN(GlobalShortcutListenerX11);
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_GLOBAL_SHORTCUT_LISTENER_X11_H_
