// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_APP_LIST_APP_LIST_CONTROLLER_DELEGATE_H_
#define CHROME_BROWSER_UI_APP_LIST_APP_LIST_CONTROLLER_DELEGATE_H_

#include <string>

#include "chrome/browser/extensions/extension_prefs.h"
#include "ui/gfx/native_widget_types.h"

class ExtensionSet;
class Profile;

namespace base {
class FilePath;
}

namespace extensions {
class Extension;
class InstallTracker;
}

namespace gfx {
class ImageSkia;
}

// Interface to allow the view delegate to call out to whatever is controlling
// the app list. This will have different implementations for different
// platforms.
class AppListControllerDelegate {
 public:
  // Indicates the source of an app list activation, for tracking purposes.
  enum AppListSource {
    LAUNCH_FROM_UNKNOWN,
    LAUNCH_FROM_APP_LIST,
    LAUNCH_FROM_APP_LIST_SEARCH
  };

  // Whether apps can be pinned, and whether pinned apps are editable or fixed.
  enum Pinnable {
    NO_PIN,
    PIN_EDITABLE,
    PIN_FIXED
  };

  virtual ~AppListControllerDelegate();

  // Whether to force the use of a native desktop widget when the app list
  // window is first created.
  virtual bool ForceNativeDesktop() const;

  // Dismisses the view.
  virtual void DismissView() = 0;

  // Handle the view being closed.
  virtual void ViewClosing();

  // Get app list window.
  virtual gfx::NativeWindow GetAppListWindow() = 0;

  // Get the application icon to be used, if any, for the app list.
  virtual gfx::ImageSkia GetWindowIcon() = 0;

  // Control of pinning apps.
  virtual bool IsAppPinned(const std::string& extension_id) = 0;
  virtual void PinApp(const std::string& extension_id) = 0;
  virtual void UnpinApp(const std::string& extension_id) = 0;
  virtual Pinnable GetPinnable() = 0;

  // Be aware of the extension prompt (either uninstalling flow or enable flow).
  virtual void OnShowExtensionPrompt();
  virtual void OnCloseExtensionPrompt();

  // Whether the controller supports a Create Shortcuts flow.
  virtual bool CanDoCreateShortcutsFlow() = 0;

  // Show the dialog to create shortcuts. Call only if
  // CanDoCreateShortcutsFlow() returns true.
  virtual void DoCreateShortcutsFlow(Profile* profile,
                                     const std::string& extension_id) = 0;

  // Handle the "create window" context menu items of Chrome App.
  // |incognito| is true to create an incognito window.
  virtual void CreateNewWindow(Profile* profile, bool incognito) = 0;

  // Show the app's most recent window, or launch it if it is not running.
  virtual void ActivateApp(Profile* profile,
                           const extensions::Extension* extension,
                           AppListSource source,
                           int event_flags) = 0;

  // Launch the app.
  virtual void LaunchApp(Profile* profile,
                         const extensions::Extension* extension,
                         AppListSource source,
                         int event_flags) = 0;

  // Show the app list for the profile specified by |profile_path|.
  virtual void ShowForProfileByPath(const base::FilePath& profile_path) = 0;

  // Whether or not the icon indicating which user is logged in should be
  // visible.
  virtual bool ShouldShowUserIcon() = 0;

  static std::string AppListSourceToString(AppListSource source);

  // True if the user has permission to modify the given app's settings.
  bool UserMayModifySettings(Profile* profile, const std::string& app_id);

  // Uninstall the app identified by |app_id| from |profile|.
  void UninstallApp(Profile* profile, const std::string& app_id);

  // True if the app was installed from the web store.
  bool IsAppFromWebStore(Profile* profile,
                         const std::string& app_id);

  // Shows the user the webstore site for the given app.
  void ShowAppInWebStore(Profile* profile,
                         const std::string& app_id,
                         bool is_search_result);

  // True if the given extension has an options page.
  bool HasOptionsPage(Profile* profile, const std::string& app_id);

  // Shows the user the options page for the app.
  void ShowOptionsPage(Profile* profile, const std::string& app_id);

  // Gets/sets the launch type for an app.
  // The launch type specifies whether a hosted app should launch as a separate
  // window, fullscreened or as a tab.
  extensions::ExtensionPrefs::LaunchType GetExtensionLaunchType(
      Profile* profile, const std::string& app_id);
  virtual void SetExtensionLaunchType(
      Profile* profile,
      const std::string& extension_id,
      extensions::ExtensionPrefs::LaunchType launch_type);

  // Returns true if the given extension is installed.
  bool IsExtensionInstalled(Profile* profile, const std::string& app_id);

  extensions::InstallTracker* GetInstallTrackerFor(Profile* profile);

  // Get the list of installed apps for the given profile.
  void GetApps(Profile* profile, ExtensionSet* out_apps);
};

#endif  // CHROME_BROWSER_UI_APP_LIST_APP_LIST_CONTROLLER_DELEGATE_H_
