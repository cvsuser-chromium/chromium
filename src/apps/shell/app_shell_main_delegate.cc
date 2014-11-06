// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/shell/app_shell_main_delegate.h"

#include "apps/shell/app_shell_content_browser_client.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "content/public/browser/browser_main_runner.h"
#include "ui/base/resource/resource_bundle.h"

namespace apps {

AppShellMainDelegate::AppShellMainDelegate() {
}

AppShellMainDelegate::~AppShellMainDelegate() {
}

bool AppShellMainDelegate::BasicStartupComplete(int* exit_code) {
  // TODO(jamescook): Initialize logging here.
  SetContentClient(&content_client_);
  return false;
}

void AppShellMainDelegate::PreSandboxStartup() {
  InitializeResourceBundle();
}

content::ContentBrowserClient*
AppShellMainDelegate::CreateContentBrowserClient() {
  browser_client_.reset(new apps::AppShellContentBrowserClient);
  return browser_client_.get();
}

void AppShellMainDelegate::InitializeResourceBundle() {
  ui::ResourceBundle::InitSharedInstanceWithLocale("en-US", NULL);
}

}  // namespace apps
