// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/app_list/win/activation_tracker_win.h"

#include "base/time/time.h"

namespace {

static const wchar_t kJumpListClassName[] = L"DV2ControlHost";
static const wchar_t kTrayClassName[] = L"Shell_TrayWnd";
static const int kFocusCheckIntervalMS = 250;

}  // namespace

ActivationTrackerWin::ActivationTrackerWin(
    app_list::AppListView* view,
    const base::Closure& on_should_dismiss)
    : view_(view),
      on_should_dismiss_(on_should_dismiss),
      reactivate_on_next_focus_loss_(false),
      taskbar_has_focus_(false) {
  view_->AddObserver(this);
}

ActivationTrackerWin::~ActivationTrackerWin() {
  view_->RemoveObserver(this);
  timer_.Stop();
}

void ActivationTrackerWin::OnActivationChanged(
    views::Widget* /*widget*/, bool active) {
  if (active) {
    timer_.Stop();
    return;
  }

  taskbar_has_focus_ = false;
  timer_.Start(FROM_HERE,
               base::TimeDelta::FromMilliseconds(kFocusCheckIntervalMS), this,
               &ActivationTrackerWin::MaybeDismissAppList);
}

void ActivationTrackerWin::OnViewHidden() {
  timer_.Stop();
}

void ActivationTrackerWin::MaybeDismissAppList() {
  if (!ShouldDismissAppList())
    return;

  if (reactivate_on_next_focus_loss_) {
    // Instead of dismissing the app launcher, re-activate it.
    reactivate_on_next_focus_loss_ = false;
    view_->GetWidget()->Activate();
    return;
  }

  on_should_dismiss_.Run();
}

bool ActivationTrackerWin::ShouldDismissAppList() {
  // The app launcher should be hidden when it loses focus, except for the cases
  // necessary to allow the launcher to be pinned or closed via the taskbar
  // context menu. This will return true to dismiss the app launcher unless one
  // of the following conditions are met:
  // - the app launcher is focused, or
  // - the taskbar's jump list is focused, or
  // - the taskbar is focused with the right mouse button pressed.

  // Remember if the taskbar had focus without the right mouse button being
  // down.
  bool taskbar_had_focus = taskbar_has_focus_;
  taskbar_has_focus_ = false;

  // First get the taskbar and jump lists windows (the jump list is the
  // context menu which the taskbar uses).
  HWND jump_list_hwnd = FindWindow(kJumpListClassName, NULL);
  HWND taskbar_hwnd = FindWindow(kTrayClassName, NULL);

  // First work out if the left or right button is currently down.
  int swapped = GetSystemMetrics(SM_SWAPBUTTON);
  int left_button = swapped ? VK_RBUTTON : VK_LBUTTON;
  bool left_button_down = GetAsyncKeyState(left_button) < 0;
  int right_button = swapped ? VK_LBUTTON : VK_RBUTTON;
  bool right_button_down = GetAsyncKeyState(right_button) < 0;

  // Now get the window that currently has focus.
  HWND focused_hwnd = GetForegroundWindow();
  if (!focused_hwnd) {
    // Sometimes the focused window is NULL. This can happen when the focus is
    // changing due to a mouse button press. Dismiss the launcher if and only if
    // no button is being pressed.
    return !right_button_down && !left_button_down;
  }

  while (focused_hwnd) {
    // If the focused window is the right click menu (called a jump list) or
    // the app list, don't hide the launcher.
    if (focused_hwnd == jump_list_hwnd || focused_hwnd == view_->GetHWND())
      return false;

    if (focused_hwnd == taskbar_hwnd) {
      // If the focused window is the taskbar, and the right button is down,
      // don't hide the launcher as the user might be bringing up the menu.
      if (right_button_down)
        return false;

      // There is a short period between the right mouse button being down
      // and the menu gaining focus, where the taskbar has focus and no button
      // is down. If the taskbar is observed in this state one time, the
      // launcher is not dismissed. If it happens for two consecutive timer
      // ticks, it is dismissed.
      if (!taskbar_had_focus) {
        taskbar_has_focus_ = true;
        return false;
      }
      return true;
    }
    focused_hwnd = GetParent(focused_hwnd);
  }

  // If we get here, the focused window is not the taskbar, its context menu, or
  // the app list.
  return true;
}
