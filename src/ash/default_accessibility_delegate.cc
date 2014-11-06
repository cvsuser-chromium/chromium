// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/default_accessibility_delegate.h"

#include <limits>

namespace ash {
namespace internal {

DefaultAccessibilityDelegate::DefaultAccessibilityDelegate()
    : spoken_feedback_enabled_(false),
      high_contrast_enabled_(false),
      screen_magnifier_enabled_(false),
      screen_magnifier_type_(kDefaultMagnifierType),
      large_cursor_enabled_(false),
      autoclick_enabled_(false) {
}

DefaultAccessibilityDelegate::~DefaultAccessibilityDelegate() {}

bool DefaultAccessibilityDelegate::IsSpokenFeedbackEnabled() const {
  return spoken_feedback_enabled_;
}

void DefaultAccessibilityDelegate::ToggleHighContrast() {
  high_contrast_enabled_ = !high_contrast_enabled_;
}

bool DefaultAccessibilityDelegate::IsHighContrastEnabled() const {
  return high_contrast_enabled_;
}

void DefaultAccessibilityDelegate::SetMagnifierEnabled(bool enabled) {
  screen_magnifier_enabled_ = enabled;
}

void DefaultAccessibilityDelegate::SetMagnifierType(MagnifierType type) {
  screen_magnifier_type_ = type;
}

bool DefaultAccessibilityDelegate::IsMagnifierEnabled() const {
  return screen_magnifier_enabled_;
}

MagnifierType DefaultAccessibilityDelegate::GetMagnifierType() const {
  return screen_magnifier_type_;
}

void DefaultAccessibilityDelegate::SetLargeCursorEnabled(bool enabled) {
  large_cursor_enabled_ = enabled;
}

bool DefaultAccessibilityDelegate::IsLargeCursorEnabled() const {
  return large_cursor_enabled_;
}

void DefaultAccessibilityDelegate::SetAutoclickEnabled(bool enabled) {
  autoclick_enabled_ = enabled;
}

bool DefaultAccessibilityDelegate::IsAutoclickEnabled() const {
  return autoclick_enabled_;
}

bool DefaultAccessibilityDelegate::ShouldAlwaysShowAccessibilityMenu() const {
  return false;
}

void DefaultAccessibilityDelegate::SilenceSpokenFeedback() const {
}

void DefaultAccessibilityDelegate::ToggleSpokenFeedback(
    AccessibilityNotificationVisibility notify) {
  spoken_feedback_enabled_ = !spoken_feedback_enabled_;
}

void DefaultAccessibilityDelegate::SaveScreenMagnifierScale(double scale) {
}

double DefaultAccessibilityDelegate::GetSavedScreenMagnifierScale() {
  return std::numeric_limits<double>::min();
}

}  // namespace internal
}  // namespace ash
