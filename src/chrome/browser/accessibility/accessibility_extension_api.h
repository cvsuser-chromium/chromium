// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ACCESSIBILITY_ACCESSIBILITY_EXTENSION_API_H_
#define CHROME_BROWSER_ACCESSIBILITY_ACCESSIBILITY_EXTENSION_API_H_

#include <string>

#include "base/compiler_specific.h"
#include "base/memory/singleton.h"
#include "base/values.h"
#include "chrome/browser/accessibility/accessibility_events.h"
#include "chrome/browser/extensions/chrome_extension_function.h"
#include "ui/base/accessibility/accessibility_types.h"

namespace extensions {
class ExtensionHost;
} // namespace extensions

// Observes the profile and routes accessibility notifications as events
// to the extension system.
class ExtensionAccessibilityEventRouter {
 public:
  typedef base::Callback<void(ui::AccessibilityTypes::Event,
                              const AccessibilityControlInfo*)>
      ControlEventCallback;
  // Single instance of the event router.
  static ExtensionAccessibilityEventRouter* GetInstance();

  // Get the dict representing the last control that received an
  // OnControlFocus event.
  DictionaryValue* last_focused_control_dict() {
    return &last_focused_control_dict_;
  }

  // Accessibility support is disabled until an extension expicitly enables
  // it, so that this extension api has no impact on Chrome's performance
  // otherwise.  These methods handle enabling, disabling, and querying the
  // status.
  void SetAccessibilityEnabled(bool enabled);
  bool IsAccessibilityEnabled() const;

  // Set and remove callbacks (used for testing, to confirm that events are
  // getting through).
  void SetControlEventCallbackForTesting(ControlEventCallback callback);
  void ClearControlEventCallback();

  // Route a window-related accessibility event.
  void HandleWindowEvent(ui::AccessibilityTypes::Event event,
                         const AccessibilityWindowInfo* info);

  // Route a menu-related accessibility event.
  void HandleMenuEvent(ui::AccessibilityTypes::Event event,
                       const AccessibilityMenuInfo* info);

  // Route a control-related accessibility event.
  void HandleControlEvent(ui::AccessibilityTypes::Event event,
                          const AccessibilityControlInfo* info);

  void OnChromeVoxLoadStateChanged(
      Profile* profile,
      bool loading,
      bool make_announcements);

  static void DispatchEventToChromeVox(
      Profile* profile,
      const char* event_name,
      scoped_ptr<base::ListValue> event_args);

 private:
  friend struct DefaultSingletonTraits<ExtensionAccessibilityEventRouter>;

  ExtensionAccessibilityEventRouter();
  virtual ~ExtensionAccessibilityEventRouter();

  void OnWindowOpened(const AccessibilityWindowInfo* details);
  void OnControlFocused(const AccessibilityControlInfo* details);
  void OnControlAction(const AccessibilityControlInfo* details);
  void OnTextChanged(const AccessibilityControlInfo* details);
  void OnMenuOpened(const AccessibilityMenuInfo* details);
  void OnMenuClosed(const AccessibilityMenuInfo* details);

  void DispatchEvent(Profile* profile,
                     const char* event_name,
                     scoped_ptr<base::ListValue> event_args);

  DictionaryValue last_focused_control_dict_;

  bool enabled_;

  // For testing.
  ControlEventCallback control_event_callback_;

  DISALLOW_COPY_AND_ASSIGN(ExtensionAccessibilityEventRouter);
};

// API function that enables or disables accessibility support.  Event
// listeners are only installed when accessibility support is enabled, to
// minimize the impact.
class AccessibilitySetAccessibilityEnabledFunction
    : public ChromeSyncExtensionFunction {
  virtual ~AccessibilitySetAccessibilityEnabledFunction() {}
  virtual bool RunImpl() OVERRIDE;
  DECLARE_EXTENSION_FUNCTION(
      "experimental.accessibility.setAccessibilityEnabled",
      EXPERIMENTAL_ACCESSIBILITY_SETACCESSIBILITYENABLED)
};

// API function that enables or disables web content accessibility support.
class AccessibilitySetNativeAccessibilityEnabledFunction
    : public ChromeSyncExtensionFunction {
  virtual ~AccessibilitySetNativeAccessibilityEnabledFunction() {}
  virtual bool RunImpl() OVERRIDE;
  DECLARE_EXTENSION_FUNCTION(
      "experimental.accessibility.setNativeAccessibilityEnabled",
      EXPERIMENTAL_ACCESSIBILITY_SETNATIVEACCESSIBILITYENABLED)
};

// API function that returns the most recent focused control.
class AccessibilityGetFocusedControlFunction
    : public ChromeSyncExtensionFunction {
  virtual ~AccessibilityGetFocusedControlFunction() {}
  virtual bool RunImpl() OVERRIDE;
  DECLARE_EXTENSION_FUNCTION(
      "experimental.accessibility.getFocusedControl",
      EXPERIMENTAL_ACCESSIBILITY_GETFOCUSEDCONTROL)
};

// API function that returns alerts being shown on the give tab.
class AccessibilityGetAlertsForTabFunction
    : public ChromeSyncExtensionFunction {
  virtual ~AccessibilityGetAlertsForTabFunction() {}
  virtual bool RunImpl() OVERRIDE;
  DECLARE_EXTENSION_FUNCTION(
      "experimental.accessibility.getAlertsForTab",
      EXPERIMENTAL_ACCESSIBILITY_GETALERTSFORTAB)
};

#endif  // CHROME_BROWSER_ACCESSIBILITY_ACCESSIBILITY_EXTENSION_API_H_
