// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/shell/minimal_shell.h"

#include "ui/aura/client/default_activation_client.h"
#include "ui/aura/client/default_capture_client.h"
#include "ui/aura/env.h"
#include "ui/aura/root_window.h"
#include "ui/aura/test/test_focus_client.h"
#include "ui/views/corewm/compound_event_filter.h"
#include "ui/views/corewm/input_method_event_filter.h"

namespace shell {

MinimalShell::MinimalShell(const gfx::Size& default_window_size) {
  aura::Env::CreateInstance();
  root_window_.reset(new aura::RootWindow(
      aura::RootWindow::CreateParams(
          gfx::Rect(default_window_size))));
  root_window_->Init();
  aura::client::SetWindowTreeClient(root_window_->window(), this);

  focus_client_.reset(new aura::test::TestFocusClient);
  aura::client::SetFocusClient(root_window_->window(), focus_client_.get());

  root_window_event_filter_ = new views::corewm::CompoundEventFilter;
  // Pass ownership of the filter to the root_window.
  root_window_->window()->SetEventFilter(root_window_event_filter_);

  input_method_filter_.reset(new views::corewm::InputMethodEventFilter(
      root_window_->GetAcceleratedWidget()));
  input_method_filter_->SetInputMethodPropertyInRootWindow(
      root_window_->window());
  root_window_event_filter_->AddHandler(input_method_filter_.get());

  activation_client_.reset(
      new aura::client::DefaultActivationClient(root_window_->window()));

  capture_client_.reset(
      new aura::client::DefaultCaptureClient(root_window_->window()));
}

MinimalShell::~MinimalShell() {
  root_window_event_filter_->RemoveHandler(input_method_filter_.get());
}

aura::Window* MinimalShell::GetDefaultParent(
    aura::Window* context,
    aura::Window* window,
    const gfx::Rect& bounds) {
  return root_window_->window();
}

}  // namespace shell
