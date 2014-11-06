// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/app_list/search/webstore/webstore_installer.h"

#include "chrome/browser/extensions/extension_install_prompt.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/scoped_tabbed_browser_displayer.h"

namespace app_list {

WebstoreInstaller::WebstoreInstaller(const std::string& webstore_item_id,
                                     Profile* profile,
                                     gfx::NativeWindow parent_window,
                                     const Callback& callback)
    : WebstoreStartupInstaller(webstore_item_id, profile, true, callback),
      profile_(profile),
      parent_window_(parent_window) {
  set_install_source(
      extensions::WebstoreInstaller::INSTALL_SOURCE_APP_LAUNCHER);
}

WebstoreInstaller::~WebstoreInstaller() {}

scoped_ptr<ExtensionInstallPrompt> WebstoreInstaller::CreateInstallUI() {
  return make_scoped_ptr(
      new ExtensionInstallPrompt(profile_, parent_window_, this));
}

content::WebContents* WebstoreInstaller::OpenURL(
    const content::OpenURLParams& params) {
  chrome::ScopedTabbedBrowserDisplayer displayer(
      profile_, chrome::GetActiveDesktop());
  return displayer.browser()->OpenURL(params);
}

}  // namespace app_list
