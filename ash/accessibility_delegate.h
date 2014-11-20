// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_ACCESSIBILITY_DELEGATE_H_
#define ASH_ACCESSIBILITY_DELEGATE_H_

#include "ash/ash_export.h"
#include "ash/magnifier/magnifier_constants.h"

namespace ash {

enum AccessibilityNotificationVisibility {
  A11Y_NOTIFICATION_NONE,
  A11Y_NOTIFICATION_SHOW,
};

// A deletate class to control accessibility features.
class ASH_EXPORT AccessibilityDelegate {
 public:
  virtual ~AccessibilityDelegate() {}

  // Invoked to toggle spoken feedback for accessibility
  virtual void ToggleSpokenFeedback(
      AccessibilityNotificationVisibility notify) = 0;

  // Returns true if spoken feedback is enabled.
  virtual bool IsSpokenFeedbackEnabled() const = 0;

  // Invoked to toggle high contrast mode for accessibility.
  virtual void ToggleHighContrast() = 0;

  // Returns true if high contrast mode is enabled.
  virtual bool IsHighContrastEnabled() const = 0;

  // Invoked to enable the screen magnifier.
  virtual void SetMagnifierEnabled(bool enabled) = 0;

  // Invoked to change the type of the screen magnifier.
  virtual void SetMagnifierType(MagnifierType type) = 0;

  // Returns true if the screen magnifier is enabled or not.
  virtual bool IsMagnifierEnabled() const = 0;

  // Returns the current screen magnifier mode.
  virtual MagnifierType GetMagnifierType() const = 0;

  // Invoked to enable Large Cursor.
  virtual void SetLargeCursorEnabled(bool enabled) = 0;

  // Returns ture if Large Cursor is enabled or not.
  virtual bool IsLargeCursorEnabled() const = 0;

  // Invoked to enable autoclick.
  virtual void SetAutoclickEnabled(bool enabled) = 0;

  // Returns if autoclick is enabled or not.
  virtual bool IsAutoclickEnabled() const = 0;

  // Returns true if the user wants to show accesibility menu even when all the
  // accessibility features are disabled.
  virtual bool ShouldAlwaysShowAccessibilityMenu() const = 0;

  // Cancel all current and queued speech immediately.
  virtual void SilenceSpokenFeedback() const = 0;

  // Saves the zoom scale of the full screen magnifier.
  virtual void SaveScreenMagnifierScale(double scale) = 0;

  // Gets a saved value of the zoom scale of full screen magnifier. If a value
  // is not saved, return a negative value.
  virtual double GetSavedScreenMagnifierScale() = 0;
};

}  // namespace ash

#endif  // ASH_ACCESSIBILITYDELEGATE_H_
