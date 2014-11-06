// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/events/x/events_x_utils.h"

#include <X11/keysym.h>
#include <X11/X.h>
#include <X11/Xlib.h>

#include "base/logging.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_code_conversion_x.h"
#include "ui/events/x/touch_factory_x11.h"

namespace {

// Converts ui::EventType to state for X*Events.
unsigned int XEventState(int flags) {
  return
      ((flags & ui::EF_SHIFT_DOWN) ? ShiftMask : 0) |
      ((flags & ui::EF_CONTROL_DOWN) ? ControlMask : 0) |
      ((flags & ui::EF_ALT_DOWN) ? Mod1Mask : 0) |
      ((flags & ui::EF_CAPS_LOCK_DOWN) ? LockMask : 0) |
      ((flags & ui::EF_LEFT_MOUSE_BUTTON) ? Button1Mask: 0) |
      ((flags & ui::EF_MIDDLE_MOUSE_BUTTON) ? Button2Mask: 0) |
      ((flags & ui::EF_RIGHT_MOUSE_BUTTON) ? Button3Mask: 0);
}

// Converts EventType to XKeyEvent type.
int XKeyEventType(ui::EventType type) {
  switch (type) {
    case ui::ET_KEY_PRESSED:
      return KeyPress;
    case ui::ET_KEY_RELEASED:
      return KeyRelease;
    default:
      return 0;
  }
}

// Converts EventType to XButtonEvent type.
int XButtonEventType(ui::EventType type) {
  switch (type) {
    case ui::ET_MOUSEWHEEL:
    case ui::ET_MOUSE_PRESSED:
      // The button release X events for mouse wheels are dropped by Aura.
      return ButtonPress;
    case ui::ET_MOUSE_RELEASED:
      return ButtonRelease;
    default:
      return 0;
  }
}

// Converts KeyboardCode to XKeyEvent keycode.
unsigned int XKeyEventKeyCode(ui::KeyboardCode key_code,
                              int flags,
                              XDisplay* display) {
  const int keysym = XKeysymForWindowsKeyCode(key_code,
                                              flags & ui::EF_SHIFT_DOWN);
  // Tests assume the keycode for XK_less is equal to the one of XK_comma,
  // but XKeysymToKeycode returns 94 for XK_less while it returns 59 for
  // XK_comma. Here we convert the value for XK_less to the value for XK_comma.
  return (keysym == XK_less) ? 59 : XKeysymToKeycode(display, keysym);
}

// Converts Aura event type and flag to X button event.
unsigned int XButtonEventButton(ui::EventType type,
                                int flags) {
  // Aura events don't keep track of mouse wheel button, so just return
  // the first mouse wheel button.
  if (type == ui::ET_MOUSEWHEEL)
    return Button4;

  switch (flags) {
    case ui::EF_LEFT_MOUSE_BUTTON:
      return Button1;
    case ui::EF_MIDDLE_MOUSE_BUTTON:
      return Button2;
    case ui::EF_RIGHT_MOUSE_BUTTON:
      return Button3;
  }

  return 0;
}

}  // namespace

namespace ui {

void InitXKeyEventForTesting(EventType type,
                             KeyboardCode key_code,
                             int flags,
                             XEvent* event) {
  CHECK(event);
  XDisplay* display = gfx::GetXDisplay();
  XKeyEvent key_event;
  key_event.type = XKeyEventType(type);
  CHECK_NE(0, key_event.type);
  key_event.serial = 0;
  key_event.send_event = 0;
  key_event.display = display;
  key_event.time = 0;
  key_event.window = 0;
  key_event.root = 0;
  key_event.subwindow = 0;
  key_event.x = 0;
  key_event.y = 0;
  key_event.x_root = 0;
  key_event.y_root = 0;
  key_event.state = XEventState(flags);
  key_event.keycode = XKeyEventKeyCode(key_code, flags, display);
  key_event.same_screen = 1;
  event->type = key_event.type;
  event->xkey = key_event;
}

void InitXButtonEventForTesting(EventType type,
                                int flags,
                                XEvent* event) {
  CHECK(event);
  XDisplay* display = gfx::GetXDisplay();
  XButtonEvent button_event;
  button_event.type = XButtonEventType(type);
  CHECK_NE(0, button_event.type);
  button_event.serial = 0;
  button_event.send_event = 0;
  button_event.display = display;
  button_event.time = 0;
  button_event.window = 0;
  button_event.root = 0;
  button_event.subwindow = 0;
  button_event.x = 0;
  button_event.y = 0;
  button_event.x_root = 0;
  button_event.y_root = 0;
  button_event.state = XEventState(flags);
  button_event.button = XButtonEventButton(type, flags);
  button_event.same_screen = 1;
  event->type = button_event.type;
  event->xbutton = button_event;
}

EVENTS_EXPORT void InitXMouseWheelEventForTesting(int wheel_delta,
                                                  int flags,
                                                  XEvent* event) {
  InitXButtonEventForTesting(ui::ET_MOUSEWHEEL, flags, event);
  // MouseWheelEvents are not taking horizontal scrolls into account
  // at the moment.
  event->xbutton.button = wheel_delta > 0 ? Button4 : Button5;
}

#if defined(USE_XI2_MT)
const int kValuatorNum = 3;
const int kTouchValuatorMap[kValuatorNum][4] = {
  // { valuator_index, valuator_type, min_val, max_val }
  { 0, DeviceDataManager::DT_TOUCH_MAJOR, 0, 1000},
  { 1, DeviceDataManager::DT_TOUCH_ORIENTATION, 0, 1},
  { 2, DeviceDataManager::DT_TOUCH_PRESSURE, 0, 1000},
};

XScopedTouchEvent::XScopedTouchEvent(XEvent* event) : event_(event) {
}

XScopedTouchEvent::~XScopedTouchEvent() {
  XIDeviceEvent* xiev =
      static_cast<XIDeviceEvent*>(event_->xcookie.data);
  if (xiev) {
    delete[] xiev->valuators.mask;
    delete[] xiev->valuators.values;
    delete xiev;
  }
}

XEvent* CreateTouchEvent(int deviceid,
                         int evtype,
                         int tracking_id,
                         const gfx::Point& location,
                         const std::vector<Valuator>& valuators) {
  XEvent* event = new XEvent;
  memset(event, 0, sizeof(*event));
  event->type = GenericEvent;
  event->xcookie.data = new XIDeviceEvent;
  XIDeviceEvent* xiev =
      static_cast<XIDeviceEvent*>(event->xcookie.data);
  memset(xiev, 0, sizeof(XIDeviceEvent));
  xiev->deviceid = deviceid;
  xiev->sourceid = deviceid;
  xiev->evtype = evtype;
  xiev->detail = tracking_id;
  xiev->event_x = location.x();
  xiev->event_y = location.y();

  xiev->valuators.mask_len = (valuators.size() / 8) + 1;
  xiev->valuators.mask = new unsigned char[xiev->valuators.mask_len];
  memset(xiev->valuators.mask, 0, xiev->valuators.mask_len);
  xiev->valuators.values = new double[valuators.size()];

  int val_count = 0;
  for (int i = 0; i < kValuatorNum; i++) {
    for(size_t j = 0; j < valuators.size(); j++) {
      if (valuators[j].data_type == kTouchValuatorMap[i][1]) {
        XISetMask(xiev->valuators.mask, kTouchValuatorMap[i][0]);
        xiev->valuators.values[val_count++] = valuators[j].value;
      }
    }
  }

  return event;
}

void SetupTouchDevicesForTest(const std::vector<unsigned int>& devices) {
  TouchFactory::GetInstance()->SetTouchDeviceForTest(devices);
  ui::DeviceDataManager* manager = ui::DeviceDataManager::GetInstance();
  manager->SetDeviceListForTest(devices);
  for (size_t i = 0; i < devices.size(); i++) {
    for (int j = 0; j < kValuatorNum; j++) {
      manager->SetDeviceValuatorForTest(
          devices[i],
          kTouchValuatorMap[j][0],
          static_cast<DeviceDataManager::DataType>(kTouchValuatorMap[j][1]),
          kTouchValuatorMap[j][2],
          kTouchValuatorMap[j][3]);
    }
  }
}
#endif // defined(USE_XI2_MT)

}  // namespace ui
