// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/global_shortcut_listener_x11.h"

#include "base/x11/x11_error_tracker.h"
#include "content/public/browser/browser_thread.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/events/keycodes/keyboard_code_conversion_x.h"
#include "ui/gfx/x/x11_types.h"

#if defined(TOOLKIT_GTK)
#include <gdk/gdkx.h>
#else
#include "base/message_loop/message_pump_x11.h"
#endif

using content::BrowserThread;

namespace {

static base::LazyInstance<extensions::GlobalShortcutListenerX11> instance =
    LAZY_INSTANCE_INITIALIZER;

// The modifiers masks used for grabing keys. Due to XGrabKey only working on
// exact modifiers, we need to grab all key combination including zero or more
// of the following: Num lock, Caps lock and Scroll lock. So that we can make
// sure the behavior of global shortcuts is consistent on all platforms.
static const unsigned int kModifiersMasks[] = {
  0,                                // No additional modifier.
  Mod2Mask,                         // Num lock
  LockMask,                         // Caps lock
  Mod5Mask,                         // Scroll lock
  Mod2Mask | LockMask,
  Mod2Mask | Mod5Mask,
  LockMask | Mod5Mask,
  Mod2Mask | LockMask | Mod5Mask
};

int GetNativeModifiers(const ui::Accelerator& accelerator) {
  int modifiers = 0;
  modifiers |= accelerator.IsShiftDown() ? ShiftMask : 0;
  modifiers |= accelerator.IsCtrlDown() ? ControlMask : 0;
  modifiers |= accelerator.IsAltDown() ? Mod1Mask : 0;

  return modifiers;
}

}  // namespace

namespace extensions {

// static
GlobalShortcutListener* GlobalShortcutListener::GetInstance() {
  CHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  return instance.Pointer();
}

GlobalShortcutListenerX11::GlobalShortcutListenerX11()
    : is_listening_(false),
      x_display_(gfx::GetXDisplay()),
      x_root_window_(DefaultRootWindow(x_display_)) {
  CHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
}

GlobalShortcutListenerX11::~GlobalShortcutListenerX11() {
  if (is_listening_)
    StopListening();
}

void GlobalShortcutListenerX11::StartListening() {
  DCHECK(!is_listening_);  // Don't start twice.
  DCHECK(!registered_hot_keys_.empty());  // Also don't start if no hotkey is
                                          // registered.
#if defined(TOOLKIT_GTK)
  gdk_window_add_filter(gdk_get_default_root_window(),
                        &GlobalShortcutListenerX11::OnXEventThunk,
                        this);
#else
  base::MessagePumpX11::Current()->AddDispatcherForRootWindow(this);
#endif

  is_listening_ = true;
}

void GlobalShortcutListenerX11::StopListening() {
  DCHECK(is_listening_);  // No point if we are not already listening.
  DCHECK(registered_hot_keys_.empty());  // Make sure the set is clean before
                                         // ending.

#if defined(TOOLKIT_GTK)
  gdk_window_remove_filter(NULL,
                           &GlobalShortcutListenerX11::OnXEventThunk,
                           this);
#else
  base::MessagePumpX11::Current()->RemoveDispatcherForRootWindow(this);
#endif

  is_listening_ = false;
}

bool GlobalShortcutListenerX11::Dispatch(const base::NativeEvent& event) {
  if (event->type == KeyPress)
    OnXKeyPressEvent(event);

  return true;
}

void GlobalShortcutListenerX11::RegisterAccelerator(
    const ui::Accelerator& accelerator,
    GlobalShortcutListener::Observer* observer) {
  int modifiers = GetNativeModifiers(accelerator);
  KeyCode keycode = XKeysymToKeycode(x_display_, accelerator.key_code());
  base::X11ErrorTracker err_tracker;

  // Because XGrabKey only works on the exact modifiers mask, we should register
  // our hot keys with modifiers that we want to ignore, including Num lock,
  // Caps lock, Scroll lock. See comment about |kModifiersMasks|.
  for (size_t i = 0; i < arraysize(kModifiersMasks); ++i) {
    XGrabKey(x_display_, keycode, modifiers | kModifiersMasks[i],
             x_root_window_, False, GrabModeAsync, GrabModeAsync);
  }

  if (err_tracker.FoundNewError()) {
    LOG(ERROR) << "X failed to grab global hotkey: "
               << accelerator.GetShortcutText();

    // We may have part of the hotkeys registered, clean up.
    for (size_t i = 0; i < arraysize(kModifiersMasks); ++i) {
      XUngrabKey(x_display_, keycode, modifiers | kModifiersMasks[i],
                 x_root_window_);
    }
  } else {
    registered_hot_keys_.insert(accelerator);
    GlobalShortcutListener::RegisterAccelerator(accelerator, observer);
  }
}

void GlobalShortcutListenerX11::UnregisterAccelerator(
    const ui::Accelerator& accelerator,
    GlobalShortcutListener::Observer* observer) {
  if (registered_hot_keys_.find(accelerator) == registered_hot_keys_.end())
    return;

  int modifiers = GetNativeModifiers(accelerator);
  KeyCode keycode = XKeysymToKeycode(x_display_, accelerator.key_code());

  for (size_t i = 0; i < arraysize(kModifiersMasks); ++i) {
    XUngrabKey(x_display_, keycode, modifiers | kModifiersMasks[i],
               x_root_window_);
  }
  registered_hot_keys_.erase(accelerator);
  GlobalShortcutListener::UnregisterAccelerator(accelerator, observer);
}

#if defined(TOOLKIT_GTK)
GdkFilterReturn GlobalShortcutListenerX11::OnXEvent(GdkXEvent* gdk_x_event,
                                                    GdkEvent* gdk_event) {
  XEvent* x_event = static_cast<XEvent*>(gdk_x_event);
  if (x_event->type == KeyPress)
    OnXKeyPressEvent(x_event);

  return GDK_FILTER_CONTINUE;
}
#endif

void GlobalShortcutListenerX11::OnXKeyPressEvent(::XEvent* x_event) {
  DCHECK(x_event->type == KeyPress);
  int modifiers = 0;
  modifiers |= (x_event->xkey.state & ShiftMask) ? ui::EF_SHIFT_DOWN : 0;
  modifiers |= (x_event->xkey.state & ControlMask) ? ui::EF_CONTROL_DOWN : 0;
  modifiers |= (x_event->xkey.state & Mod1Mask) ? ui::EF_ALT_DOWN : 0;

  ui::Accelerator accelerator(
      ui::KeyboardCodeFromXKeyEvent(x_event), modifiers);
  if (registered_hot_keys_.find(accelerator) != registered_hot_keys_.end())
    instance.Get().NotifyKeyPressed(accelerator);
}

}  // namespace extensions
