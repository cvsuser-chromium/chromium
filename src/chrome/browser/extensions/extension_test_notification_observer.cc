// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extension_test_notification_observer.h"

#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/common/extensions/extension.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/test/test_utils.h"
#include "extensions/browser/process_manager.h"

using extensions::Extension;

namespace {

bool HasExtensionPageActionCountReachedTarget(LocationBarTesting* location_bar,
                                              int target_page_action_count) {
  VLOG(1) << "Number of page actions: " << location_bar->PageActionCount();
  return location_bar->PageActionCount() == target_page_action_count;
}

bool HasExtensionPageActionVisibilityReachedTarget(
    LocationBarTesting* location_bar,
    int target_visible_page_action_count) {
  VLOG(1) << "Number of visible page actions: "
          << location_bar->PageActionVisibleCount();
  return location_bar->PageActionVisibleCount() ==
         target_visible_page_action_count;
}

}  // namespace

ExtensionTestNotificationObserver::ExtensionTestNotificationObserver(
    Browser* browser)
    : browser_(browser),
      profile_(NULL),
      extension_installs_observed_(0),
      extension_load_errors_observed_(0),
      crx_installers_done_observed_(0) {
}

ExtensionTestNotificationObserver::~ExtensionTestNotificationObserver() {}

Profile* ExtensionTestNotificationObserver::GetProfile() {
  if (!profile_) {
    if (browser_)
      profile_ = browser_->profile();
    else
      profile_ = ProfileManager::GetDefaultProfile();
  }
  return profile_;
}

void ExtensionTestNotificationObserver::WaitForNotification(
    int notification_type) {
  // TODO(bauerb): Using a WindowedNotificationObserver like this can break
  // easily, if the notification we're waiting for is sent before this method.
  // Change it so that the WindowedNotificationObserver is constructed earlier.
  content::NotificationRegistrar registrar;
  registrar.Add(
      this, notification_type, content::NotificationService::AllSources());
  content::WindowedNotificationObserver(
      notification_type, content::NotificationService::AllSources()).Wait();
}

bool ExtensionTestNotificationObserver::WaitForPageActionCountChangeTo(
    int count) {
  LocationBarTesting* location_bar =
      browser_->window()->GetLocationBar()->GetLocationBarForTesting();
  if (!HasExtensionPageActionCountReachedTarget(location_bar, count)) {
    content::WindowedNotificationObserver(
        chrome::NOTIFICATION_EXTENSION_PAGE_ACTION_COUNT_CHANGED,
        base::Bind(
            &HasExtensionPageActionCountReachedTarget, location_bar, count))
        .Wait();
  }
  return HasExtensionPageActionCountReachedTarget(location_bar, count);
}

bool ExtensionTestNotificationObserver::WaitForPageActionVisibilityChangeTo(
    int count) {
  LocationBarTesting* location_bar =
      browser_->window()->GetLocationBar()->GetLocationBarForTesting();
  if (!HasExtensionPageActionVisibilityReachedTarget(location_bar, count)) {
    content::WindowedNotificationObserver(
        chrome::NOTIFICATION_EXTENSION_PAGE_ACTION_VISIBILITY_CHANGED,
        base::Bind(&HasExtensionPageActionVisibilityReachedTarget,
                   location_bar,
                   count)).Wait();
  }
  return HasExtensionPageActionVisibilityReachedTarget(location_bar, count);
}

bool ExtensionTestNotificationObserver::WaitForExtensionViewsToLoad() {
  extensions::ProcessManager* manager =
    extensions::ExtensionSystem::Get(GetProfile())->process_manager();
  extensions::ProcessManager::ViewSet all_views = manager->GetAllViews();
  for (extensions::ProcessManager::ViewSet::const_iterator iter =
         all_views.begin();
       iter != all_views.end();) {
    if (!(*iter)->IsLoading()) {
      ++iter;
    } else {
      // Wait for all the extension render view hosts that exist to finish
      // loading.
      content::WindowedNotificationObserver observer(
          content::NOTIFICATION_LOAD_STOP,
          content::NotificationService::AllSources());
      observer.AddNotificationType(
                                   content::NOTIFICATION_WEB_CONTENTS_DESTROYED,
                                   content::NotificationService::AllSources());
      observer.Wait();

      // Test activity may have modified the set of extension processes during
      // message processing, so re-start the iteration to catch added/removed
      // processes.
      all_views = manager->GetAllViews();
      iter = all_views.begin();
    }
  }
  return true;
}

bool ExtensionTestNotificationObserver::WaitForExtensionInstall() {
  int before = extension_installs_observed_;
  WaitForNotification(chrome::NOTIFICATION_EXTENSION_INSTALLED);
  return extension_installs_observed_ == (before + 1);
}

bool ExtensionTestNotificationObserver::WaitForExtensionInstallError() {
  int before = extension_installs_observed_;
  content::WindowedNotificationObserver(
      chrome::NOTIFICATION_EXTENSION_INSTALL_ERROR,
      content::NotificationService::AllSources()).Wait();
  return extension_installs_observed_ == before;
}

void ExtensionTestNotificationObserver::WaitForExtensionLoad() {
  WaitForNotification(chrome::NOTIFICATION_EXTENSION_LOADED);
}

void ExtensionTestNotificationObserver::WaitForExtensionAndViewLoad() {
  this->WaitForExtensionLoad();
  WaitForExtensionViewsToLoad();
}

bool ExtensionTestNotificationObserver::WaitForExtensionLoadError() {
  int before = extension_load_errors_observed_;
  WaitForNotification(chrome::NOTIFICATION_EXTENSION_LOAD_ERROR);
  return extension_load_errors_observed_ != before;
}

bool ExtensionTestNotificationObserver::WaitForExtensionCrash(
    const std::string& extension_id) {
  ExtensionService* service = extensions::ExtensionSystem::Get(
      GetProfile())->extension_service();

  if (!service->GetExtensionById(extension_id, true)) {
    // The extension is already unloaded, presumably due to a crash.
    return true;
  }
  content::WindowedNotificationObserver(
      chrome::NOTIFICATION_EXTENSION_PROCESS_TERMINATED,
      content::NotificationService::AllSources()).Wait();
  return (service->GetExtensionById(extension_id, true) == NULL);
}

bool ExtensionTestNotificationObserver::WaitForCrxInstallerDone() {
  int before = crx_installers_done_observed_;
  WaitForNotification(chrome::NOTIFICATION_CRX_INSTALLER_DONE);
  return crx_installers_done_observed_ == (before + 1);
}

void ExtensionTestNotificationObserver::Watch(
    int type,
    const content::NotificationSource& source) {
  CHECK(!observer_);
  observer_.reset(new content::WindowedNotificationObserver(type, source));
  registrar_.Add(this, type, source);
}

void ExtensionTestNotificationObserver::Wait() {
  observer_->Wait();

  registrar_.RemoveAll();
  observer_.reset();
}

void ExtensionTestNotificationObserver::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  switch (type) {
  case chrome::NOTIFICATION_EXTENSION_LOADED:
      last_loaded_extension_id_ =
        content::Details<const Extension>(details).ptr()->id();
      VLOG(1) << "Got EXTENSION_LOADED notification.";
      break;

  case chrome::NOTIFICATION_CRX_INSTALLER_DONE:
    VLOG(1) << "Got CRX_INSTALLER_DONE notification.";
    {
        const Extension* extension =
          content::Details<const Extension>(details).ptr();
        if (extension)
          last_loaded_extension_id_ = extension->id();
        else
          last_loaded_extension_id_.clear();
    }
    ++crx_installers_done_observed_;
    break;

  case chrome::NOTIFICATION_EXTENSION_INSTALLED:
    VLOG(1) << "Got EXTENSION_INSTALLED notification.";
    ++extension_installs_observed_;
    break;

  case chrome::NOTIFICATION_EXTENSION_LOAD_ERROR:
    VLOG(1) << "Got EXTENSION_LOAD_ERROR notification.";
    ++extension_load_errors_observed_;
    break;

  default:
    NOTREACHED();
    break;
  }
}
