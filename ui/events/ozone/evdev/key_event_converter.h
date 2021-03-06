// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_EVENTS_OZONE_EVDEV_KEY_EVENT_CONVERTER_EVDEV_H_
#define UI_EVENTS_OZONE_EVDEV_KEY_EVENT_CONVERTER_EVDEV_H_

#include "ui/events/event.h"
#include "ui/events/ozone/evdev/event_modifiers.h"
#include "ui/events/ozone/event_converter_ozone.h"

struct input_event;

namespace ui {

class KeyEventConverterEvdev : public EventConverterOzone {
 public:
  KeyEventConverterEvdev(EventModifiersEvdev* modifiers);
  virtual ~KeyEventConverterEvdev();

  // Overidden from base::MessagePumpLibevent::Watcher.
  virtual void OnFileCanReadWithoutBlocking(int fd) OVERRIDE;
  virtual void OnFileCanWriteWithoutBlocking(int fd) OVERRIDE;

  void ProcessEvents(const struct input_event* inputs, int count);

 private:
  EventModifiersEvdev* modifiers_;

  void ConvertKeyEvent(int key, int value);

  DISALLOW_COPY_AND_ASSIGN(KeyEventConverterEvdev);
};

}  // namspace ui

#endif  // UI_EVENTS_OZONE_EVDEV_KEY_EVENT_CONVERTER_EVDEV_H_

