// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/ash/launcher/chrome_launcher_controller.h"

#include <vector>

#include "ash/ash_switches.h"
#include "ash/desktop_background/desktop_background_controller.h"
#include "ash/launcher/launcher.h"
#include "ash/launcher/launcher_item_delegate_manager.h"
#include "ash/launcher/launcher_model.h"
#include "ash/multi_profile_uma.h"
#include "ash/root_window_controller.h"
#include "ash/shelf/shelf_layout_manager.h"
#include "ash/shelf/shelf_model_util.h"
#include "ash/shelf/shelf_widget.h"
#include "ash/shell.h"
#include "ash/wm/window_util.h"
#include "base/command_line.h"
#include "base/prefs/scoped_user_pref_update.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/app_mode/app_mode_utils.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/defaults.h"
#include "chrome/browser/extensions/app_icon_loader_impl.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/favicon/favicon_tab_helper.h"
#include "chrome/browser/prefs/incognito_mode_prefs.h"
#include "chrome/browser/prefs/pref_service_syncable.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/ash/app_sync_ui_state.h"
#include "chrome/browser/ui/ash/chrome_launcher_prefs.h"
#include "chrome/browser/ui/ash/launcher/app_shortcut_launcher_item_controller.h"
#include "chrome/browser/ui/ash/launcher/browser_shortcut_launcher_item_controller.h"
#include "chrome/browser/ui/ash/launcher/browser_status_monitor.h"
#include "chrome/browser/ui/ash/launcher/chrome_launcher_app_menu_item.h"
#include "chrome/browser/ui/ash/launcher/chrome_launcher_app_menu_item_browser.h"
#include "chrome/browser/ui/ash/launcher/chrome_launcher_app_menu_item_tab.h"
#include "chrome/browser/ui/ash/launcher/chrome_launcher_types.h"
#include "chrome/browser/ui/ash/launcher/launcher_app_tab_helper.h"
#include "chrome/browser/ui/ash/launcher/launcher_item_controller.h"
#include "chrome/browser/ui/ash/launcher/shell_window_launcher_controller.h"
#include "chrome/browser/ui/ash/launcher/shell_window_launcher_item_controller.h"
#include "chrome/browser/ui/ash/multi_user/multi_user_util.h"
#include "chrome/browser/ui/ash/multi_user/multi_user_window_manager.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/extensions/application_launch.h"
#include "chrome/browser/ui/extensions/extension_enable_flow.h"
#include "chrome/browser/ui/host_desktop.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/web_applications/web_app.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/manifest_handlers/app_launch_info.h"
#include "chrome/common/extensions/manifest_handlers/icons_handler.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/web_contents.h"
#include "extensions/common/extension_resource.h"
#include "extensions/common/url_pattern.h"
#include "grit/ash_resources.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "grit/ui_resources.h"
#include "net/base/url_util.h"
#include "ui/aura/root_window.h"
#include "ui/aura/window.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/views/corewm/window_animations.h"

#if defined(OS_CHROMEOS)
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chromeos/login/user_manager.h"
#include "chrome/browser/chromeos/login/wallpaper_manager.h"
#include "chrome/browser/ui/ash/chrome_shell_delegate.h"
#include "chrome/browser/ui/ash/launcher/multi_profile_browser_status_monitor.h"
#include "chrome/browser/ui/ash/launcher/multi_profile_shell_window_launcher_controller.h"
#endif

using extensions::Extension;
using extensions::UnloadedExtensionInfo;
using extension_misc::kGmailAppId;
using content::WebContents;

// static
ChromeLauncherController* ChromeLauncherController::instance_ = NULL;

namespace {

// This will be used as placeholder in the list of the pinned applciatons.
// Note that this is NOT a valid extension identifier so that pre M31 versions
// will ignore it.
const char kAppLauncherIdPlaceholder[] = "AppLauncherIDPlaceholder--------";

std::string GetPrefKeyForRootWindow(aura::Window* root_window) {
  gfx::Display display = gfx::Screen::GetScreenFor(
      root_window)->GetDisplayNearestWindow(root_window);
  DCHECK(display.is_valid());

  return base::Int64ToString(display.id());
}

void UpdatePerDisplayPref(PrefService* pref_service,
                          aura::Window* root_window,
                          const char* pref_key,
                          const std::string& value) {
  std::string key = GetPrefKeyForRootWindow(root_window);
  if (key.empty())
    return;

  DictionaryPrefUpdate update(pref_service, prefs::kShelfPreferences);
  base::DictionaryValue* shelf_prefs = update.Get();
  base::DictionaryValue* prefs = NULL;
  if (!shelf_prefs->GetDictionary(key, &prefs)) {
    prefs = new base::DictionaryValue();
    shelf_prefs->Set(key, prefs);
  }
  prefs->SetStringWithoutPathExpansion(pref_key, value);
}

// Returns a pref value in |pref_service| for the display of |root_window|. The
// pref value is stored in |local_path| and |path|, but |pref_service| may have
// per-display preferences and the value can be specified by policy. Here is
// the priority:
//  * A value managed by policy. This is a single value that applies to all
//    displays.
//  * A user-set value for the specified display.
//  * A user-set value in |local_path| or |path|, if no per-display settings are
//    ever specified (see http://crbug.com/173719 for why). |local_path| is
//    preferred. See comment in |kShelfAlignment| as to why we consider two
//    prefs and why |local_path| is preferred.
//  * A value recommended by policy. This is a single value that applies to all
//    root windows.
//  * The default value for |local_path| if the value is not recommended by
//    policy.
std::string GetPrefForRootWindow(PrefService* pref_service,
                                 aura::Window* root_window,
                                 const char* local_path,
                                 const char* path) {
  const PrefService::Preference* local_pref =
      pref_service->FindPreference(local_path);
  const std::string value(pref_service->GetString(local_path));
  if (local_pref->IsManaged())
    return value;

  std::string pref_key = GetPrefKeyForRootWindow(root_window);
  bool has_per_display_prefs = false;
  if (!pref_key.empty()) {
    const base::DictionaryValue* shelf_prefs = pref_service->GetDictionary(
        prefs::kShelfPreferences);
    const base::DictionaryValue* display_pref = NULL;
    std::string per_display_value;
    if (shelf_prefs->GetDictionary(pref_key, &display_pref) &&
        display_pref->GetString(path, &per_display_value))
      return per_display_value;

    // If the pref for the specified display is not found, scan the whole prefs
    // and check if the prefs for other display is already specified.
    std::string unused_value;
    for (base::DictionaryValue::Iterator iter(*shelf_prefs);
         !iter.IsAtEnd(); iter.Advance()) {
      const base::DictionaryValue* display_pref = NULL;
      if (iter.value().GetAsDictionary(&display_pref) &&
          display_pref->GetString(path, &unused_value)) {
        has_per_display_prefs = true;
        break;
      }
    }
  }

  if (local_pref->IsRecommended() || !has_per_display_prefs)
    return value;

  const base::Value* default_value =
      pref_service->GetDefaultPrefValue(local_path);
  std::string default_string;
  default_value->GetAsString(&default_string);
  return default_string;
}

// If prefs have synced and no user-set value exists at |local_path|, the value
// from |synced_path| is copied to |local_path|.
void MaybePropagatePrefToLocal(PrefServiceSyncable* pref_service,
                               const char* local_path,
                               const char* synced_path) {
  if (!pref_service->FindPreference(local_path)->HasUserSetting() &&
      pref_service->IsSyncing()) {
    // First time the user is using this machine, propagate from remote to
    // local.
    pref_service->SetString(local_path, pref_service->GetString(synced_path));
  }
}

std::string GetSourceFromAppListSource(ash::LaunchSource source) {
  switch (source) {
    case ash::LAUNCH_FROM_APP_LIST:
      return std::string(extension_urls::kLaunchSourceAppList);
    case ash::LAUNCH_FROM_APP_LIST_SEARCH:
      return std::string(extension_urls::kLaunchSourceAppListSearch);
    default: return std::string();
  }
}

}  // namespace

#if defined(OS_CHROMEOS)
// A class to get events from ChromeOS when a user gets changed or added.
class ChromeLauncherControllerUserSwitchObserverChromeOS
    : public ChromeLauncherControllerUserSwitchObserver,
      public chromeos::UserManager::UserSessionStateObserver,
      content::NotificationObserver {
 public:
  ChromeLauncherControllerUserSwitchObserverChromeOS(
      ChromeLauncherController* controller)
      : controller_(controller) {
    DCHECK(chromeos::UserManager::IsInitialized());
    chromeos::UserManager::Get()->AddSessionStateObserver(this);
    // A UserAddedToSession notification can be sent before a profile is loaded.
    // Since our observers require that we have already a profile, we might have
    // to postpone the notification until the ProfileManager lets us know that
    // the profile for that newly added user was added to the ProfileManager.
    registrar_.Add(this, chrome::NOTIFICATION_PROFILE_ADDED,
                   content::NotificationService::AllSources());
  }
  virtual ~ChromeLauncherControllerUserSwitchObserverChromeOS() {
    chromeos::UserManager::Get()->RemoveSessionStateObserver(this);
  }

  // chromeos::UserManager::UserSessionStateObserver overrides:
  virtual void ActiveUserChanged(const chromeos::User* active_user) OVERRIDE;
  virtual void UserAddedToSession(const chromeos::User* added_user) OVERRIDE;

  // content::NotificationObserver overrides:
  virtual void Observe(int type,
               const content::NotificationSource& source,
               const content::NotificationDetails& details) OVERRIDE;

 private:
  // Add a user to the session.
  void AddUser(Profile* profile);

  // The owning ChromeLauncherController.
  ChromeLauncherController* controller_;

  // The notification registrar to track the Profile creations after a user got
  // added to the session (if required).
  content::NotificationRegistrar registrar_;

  // Users which were just added to the system, but which profiles were not yet
  // (fully) loaded.
  std::set<std::string> added_user_ids_waiting_for_profiles_;

  DISALLOW_COPY_AND_ASSIGN(ChromeLauncherControllerUserSwitchObserverChromeOS);
};

void ChromeLauncherControllerUserSwitchObserverChromeOS::ActiveUserChanged(
    const chromeos::User* active_user) {
  const std::string& user_email = active_user->email();
  // Forward the OS specific event to the ChromeLauncherController.
  controller_->ActiveUserChanged(user_email);
  // TODO(skuhne): At the moment the login screen does the wallpaper management
  // and wallpapers are not synchronized across multiple desktops.
  if (chromeos::WallpaperManager::Get())
    chromeos::WallpaperManager::Get()->SetUserWallpaper(user_email);
}

void ChromeLauncherControllerUserSwitchObserverChromeOS::UserAddedToSession(
    const chromeos::User* active_user) {
  Profile* profile = multi_user_util::GetProfileFromUserID(
      active_user->email());
  // If we do not have a profile yet, we postpone forwarding the notification
  // until it is loaded.
  if (!profile)
    added_user_ids_waiting_for_profiles_.insert(active_user->email());
  else
    AddUser(profile);
}

void ChromeLauncherControllerUserSwitchObserverChromeOS::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  if (type == chrome::NOTIFICATION_PROFILE_ADDED &&
      !added_user_ids_waiting_for_profiles_.empty()) {
    // Check if the profile is from a user which was on the waiting list.
    Profile* profile = content::Source<Profile>(source).ptr();
    std::string user_id = multi_user_util::GetUserIDFromProfile(profile);
    std::set<std::string>::iterator it = std::find(
        added_user_ids_waiting_for_profiles_.begin(),
        added_user_ids_waiting_for_profiles_.end(),
        user_id);
    if (it != added_user_ids_waiting_for_profiles_.end()) {
      added_user_ids_waiting_for_profiles_.erase(it);
      AddUser(profile->GetOriginalProfile());
    }
  }
}

void ChromeLauncherControllerUserSwitchObserverChromeOS::AddUser(
    Profile* profile) {
  if (chrome::MultiUserWindowManager::GetMultiProfileMode() ==
          chrome::MultiUserWindowManager::MULTI_PROFILE_MODE_SEPARATED)
    chrome::MultiUserWindowManager::GetInstance()->AddUser(profile);
  controller_->AdditionalUserAddedToSession(profile->GetOriginalProfile());
}
#endif

ChromeLauncherController::ChromeLauncherController(
    Profile* profile,
    ash::LauncherModel* model)
    : model_(model),
      item_delegate_manager_(NULL),
      profile_(profile),
      app_sync_ui_state_(NULL),
      ignore_persist_pinned_state_change_(false) {
  if (!profile_) {
    // Use the original profile as on chromeos we may get a temporary off the
    // record profile.
    profile_ = ProfileManager::GetDefaultProfile()->GetOriginalProfile();

    app_sync_ui_state_ = AppSyncUIState::Get(profile_);
    if (app_sync_ui_state_)
      app_sync_ui_state_->AddObserver(this);
  }

  // All profile relevant settings get bound to the current profile.
  AttachProfile(profile_);
  model_->AddObserver(this);

  // In multi profile mode we might have a window manager. We try to create it
  // here. If the instantiation fails, the manager is not needed.
  chrome::MultiUserWindowManager::CreateInstance();

#if defined(OS_CHROMEOS)
  // On Chrome OS using multi profile we want to switch the content of the shelf
  // with a user change. Note that for unit tests the instance can be NULL.
  if (chrome::MultiUserWindowManager::GetMultiProfileMode() !=
          chrome::MultiUserWindowManager::MULTI_PROFILE_MODE_OFF) {
    user_switch_observer_.reset(
        new ChromeLauncherControllerUserSwitchObserverChromeOS(this));
  }

  // Create our v1/v2 application / browser monitors which will inform the
  // launcher of status changes.
  if (chrome::MultiUserWindowManager::GetMultiProfileMode() ==
          chrome::MultiUserWindowManager::MULTI_PROFILE_MODE_SEPARATED) {
    // If running in separated destkop mode, we create the multi profile version
    // of status monitor.
    browser_status_monitor_.reset(new MultiProfileBrowserStatusMonitor(this));
    shell_window_controller_.reset(
        new MultiProfileShellWindowLauncherController(this));
  } else {
    // Create our v1/v2 application / browser monitors which will inform the
    // launcher of status changes.
    browser_status_monitor_.reset(new BrowserStatusMonitor(this));
    shell_window_controller_.reset(new ShellWindowLauncherController(this));
  }
#else
  // Create our v1/v2 application / browser monitors which will inform the
  // launcher of status changes.
  browser_status_monitor_.reset(new BrowserStatusMonitor(this));
  shell_window_controller_.reset(new ShellWindowLauncherController(this));
#endif

  // Right now ash::Shell isn't created for tests.
  // TODO(mukai): Allows it to observe display change and write tests.
  if (ash::Shell::HasInstance()) {
    ash::Shell::GetInstance()->display_controller()->AddObserver(this);
    item_delegate_manager_ =
        ash::Shell::GetInstance()->launcher_item_delegate_manager();
  }

  notification_registrar_.Add(this,
                              chrome::NOTIFICATION_EXTENSION_LOADED,
                              content::Source<Profile>(profile_));
  notification_registrar_.Add(this,
                              chrome::NOTIFICATION_EXTENSION_UNLOADED,
                              content::Source<Profile>(profile_));
}

ChromeLauncherController::~ChromeLauncherController() {
  // Reset the BrowserStatusMonitor as it has a weak pointer to this.
  browser_status_monitor_.reset();

  // Reset the shell window controller here since it has a weak pointer to this.
  shell_window_controller_.reset();

  for (std::set<ash::Launcher*>::iterator iter = launchers_.begin();
       iter != launchers_.end();
       ++iter)
    (*iter)->shelf_widget()->shelf_layout_manager()->RemoveObserver(this);

  model_->RemoveObserver(this);
  if (ash::Shell::HasInstance())
    ash::Shell::GetInstance()->display_controller()->RemoveObserver(this);
  for (IDToItemControllerMap::iterator i = id_to_item_controller_map_.begin();
       i != id_to_item_controller_map_.end(); ++i) {
    int index = model_->ItemIndexByID(i->first);
    // A "browser proxy" is not known to the model and this removal does
    // therefore not need to be propagated to the model.
    if (index != -1 &&
        model_->items()[index].type != ash::TYPE_BROWSER_SHORTCUT)
      model_->RemoveItemAt(index);
  }

  if (ash::Shell::HasInstance())
    ash::Shell::GetInstance()->RemoveShellObserver(this);

  // Release all profile dependent resources.
  ReleaseProfile();
  if (instance_ == this)
    instance_ = NULL;

  // Get rid of the multi user window manager instance.
  chrome::MultiUserWindowManager::DeleteInstance();
}

// static
ChromeLauncherController* ChromeLauncherController::CreateInstance(
    Profile* profile,
    ash::LauncherModel* model) {
  // We do not check here for re-creation of the ChromeLauncherController since
  // it appears that it might be intentional that the ChromeLauncherController
  // can be re-created.
  instance_ = new ChromeLauncherController(profile, model);
  return instance_;
}

void ChromeLauncherController::Init() {
  CreateBrowserShortcutLauncherItem();
  UpdateAppLaunchersFromPref();

  // TODO(sky): update unit test so that this test isn't necessary.
  if (ash::Shell::HasInstance()) {
    SetShelfAutoHideBehaviorFromPrefs();
    SetShelfAlignmentFromPrefs();
    PrefServiceSyncable* prefs = PrefServiceSyncable::FromProfile(profile_);
    if (!prefs->FindPreference(prefs::kShelfAlignmentLocal)->HasUserSetting() ||
        !prefs->FindPreference(prefs::kShelfAutoHideBehaviorLocal)->
            HasUserSetting()) {
      // This causes OnIsSyncingChanged to be called when the value of
      // PrefService::IsSyncing() changes.
      prefs->AddObserver(this);
    }
    ash::Shell::GetInstance()->AddShellObserver(this);
  }
}

ash::LauncherID ChromeLauncherController::CreateAppLauncherItem(
    LauncherItemController* controller,
    const std::string& app_id,
    ash::LauncherItemStatus status) {
  CHECK(controller);
  int index = 0;
  // Panels are inserted on the left so as not to push all existing panels over.
  if (controller->GetLauncherItemType() != ash::TYPE_APP_PANEL)
    index = model_->item_count();
  return InsertAppLauncherItem(controller,
                               app_id,
                               status,
                               index,
                               controller->GetLauncherItemType());
}

void ChromeLauncherController::SetItemStatus(
    ash::LauncherID id,
    ash::LauncherItemStatus status) {
  int index = model_->ItemIndexByID(id);
  ash::LauncherItemStatus old_status = model_->items()[index].status;
  // Since ordinary browser windows are not registered, we might get a negative
  // index here.
  if (index >= 0 && old_status != status) {
    ash::LauncherItem item = model_->items()[index];
    item.status = status;
    model_->Set(index, item);
  }
}

void ChromeLauncherController::SetItemController(
    ash::LauncherID id,
    LauncherItemController* controller) {
  CHECK(controller);
  IDToItemControllerMap::iterator iter = id_to_item_controller_map_.find(id);
  CHECK(iter != id_to_item_controller_map_.end());
  controller->set_launcher_id(id);
  iter->second = controller;
  // Existing controller is destroyed and replaced by registering again.
  SetLauncherItemDelegate(id, controller);
}

void ChromeLauncherController::CloseLauncherItem(ash::LauncherID id) {
  CHECK(id);
  if (IsPinned(id)) {
    // Create a new shortcut controller.
    IDToItemControllerMap::iterator iter = id_to_item_controller_map_.find(id);
    CHECK(iter != id_to_item_controller_map_.end());
    SetItemStatus(id, ash::STATUS_CLOSED);
    std::string app_id = iter->second->app_id();
    iter->second = new AppShortcutLauncherItemController(app_id, this);
    iter->second->set_launcher_id(id);
    // Existing controller is destroyed and replaced by registering again.
    SetLauncherItemDelegate(id, iter->second);
  } else {
    LauncherItemClosed(id);
  }
}

void ChromeLauncherController::Pin(ash::LauncherID id) {
  DCHECK(HasItemController(id));

  int index = model_->ItemIndexByID(id);
  DCHECK_GE(index, 0);

  ash::LauncherItem item = model_->items()[index];

  if (item.type == ash::TYPE_PLATFORM_APP ||
      item.type == ash::TYPE_WINDOWED_APP) {
    item.type = ash::TYPE_APP_SHORTCUT;
    model_->Set(index, item);
  } else if (item.type != ash::TYPE_APP_SHORTCUT) {
    return;
  }

  if (CanPin())
    PersistPinnedState();
}

void ChromeLauncherController::Unpin(ash::LauncherID id) {
  DCHECK(HasItemController(id));

  LauncherItemController* controller = id_to_item_controller_map_[id];
  if (controller->type() == LauncherItemController::TYPE_APP ||
      controller->locked()) {
    UnpinRunningAppInternal(model_->ItemIndexByID(id));
  } else {
    LauncherItemClosed(id);
  }
  if (CanPin())
    PersistPinnedState();
}

bool ChromeLauncherController::IsPinned(ash::LauncherID id) {
  int index = model_->ItemIndexByID(id);
  if (index < 0)
    return false;
  ash::LauncherItemType type = model_->items()[index].type;
  return (type == ash::TYPE_APP_SHORTCUT || type == ash::TYPE_BROWSER_SHORTCUT);
}

void ChromeLauncherController::TogglePinned(ash::LauncherID id) {
  if (!HasItemController(id))
    return;  // May happen if item closed with menu open.

  if (IsPinned(id))
    Unpin(id);
  else
    Pin(id);
}

bool ChromeLauncherController::IsPinnable(ash::LauncherID id) const {
  int index = model_->ItemIndexByID(id);
  if (index == -1)
    return false;

  ash::LauncherItemType type = model_->items()[index].type;
  return ((type == ash::TYPE_APP_SHORTCUT ||
           type == ash::TYPE_PLATFORM_APP ||
           type == ash::TYPE_WINDOWED_APP) &&
          CanPin());
}

void ChromeLauncherController::LockV1AppWithID(
    const std::string& app_id) {
  ash::LauncherID id = GetLauncherIDForAppID(app_id);
  if (!IsPinned(id) && !IsWindowedAppInLauncher(app_id)) {
    CreateAppShortcutLauncherItemWithType(app_id,
                                          model_->item_count(),
                                          ash::TYPE_WINDOWED_APP);
    id = GetLauncherIDForAppID(app_id);
  }
  CHECK(id);
  id_to_item_controller_map_[id]->lock();
}

void ChromeLauncherController::UnlockV1AppWithID(
    const std::string& app_id) {
  ash::LauncherID id = GetLauncherIDForAppID(app_id);
  CHECK(IsPinned(id) || IsWindowedAppInLauncher(app_id));
  CHECK(id);
  LauncherItemController* controller = id_to_item_controller_map_[id];
  controller->unlock();
  if (!controller->locked() && !IsPinned(id))
    CloseLauncherItem(id);
}

void ChromeLauncherController::Launch(ash::LauncherID id,
                                      int event_flags) {
  if (!HasItemController(id))
    return;  // In case invoked from menu and item closed while menu up.
  id_to_item_controller_map_[id]->Launch(ash::LAUNCH_FROM_UNKNOWN, event_flags);
}

void ChromeLauncherController::Close(ash::LauncherID id) {
  if (!HasItemController(id))
    return;  // May happen if menu closed.
  id_to_item_controller_map_[id]->Close();
}

bool ChromeLauncherController::IsOpen(ash::LauncherID id) {
  if (!HasItemController(id))
    return false;
  return id_to_item_controller_map_[id]->IsOpen();
}

bool ChromeLauncherController::IsPlatformApp(ash::LauncherID id) {
  if (!HasItemController(id))
    return false;

  std::string app_id = GetAppIDForLauncherID(id);
  const Extension* extension = GetExtensionForAppID(app_id);
  // An extension can be synced / updated at any time and therefore not be
  // available.
  return extension ? extension->is_platform_app() : false;
}

void ChromeLauncherController::LaunchApp(const std::string& app_id,
                                         ash::LaunchSource source,
                                         int event_flags) {
  // |extension| could be NULL when it is being unloaded for updating.
  const Extension* extension = GetExtensionForAppID(app_id);
  if (!extension)
    return;

  const ExtensionService* service =
      extensions::ExtensionSystem::Get(profile_)->extension_service();
  if (!service->IsExtensionEnabledForLauncher(app_id)) {
    // Do nothing if there is already a running enable flow.
    if (extension_enable_flow_)
      return;

    extension_enable_flow_.reset(
        new ExtensionEnableFlow(profile_, app_id, this));
    extension_enable_flow_->StartForNativeWindow(NULL);
    return;
  }

  AppLaunchParams params(
      GetProfileForNewWindows(),
      extension,
      event_flags,
      chrome::HOST_DESKTOP_TYPE_ASH);
  if (source != ash::LAUNCH_FROM_UNKNOWN &&
      app_id == extension_misc::kWebStoreAppId) {
    // Get the corresponding source string.
    std::string source_value = GetSourceFromAppListSource(source);

    // Set an override URL to include the source.
    GURL extension_url = extensions::AppLaunchInfo::GetFullLaunchURL(extension);
    params.override_url = net::AppendQueryParameter(
        extension_url, extension_urls::kWebstoreSourceField, source_value);
  }

  OpenApplication(params);
}

void ChromeLauncherController::ActivateApp(const std::string& app_id,
                                           ash::LaunchSource source,
                                           int event_flags) {
  // If there is an existing non-shortcut controller for this app, open it.
  ash::LauncherID id = GetLauncherIDForAppID(app_id);
  if (id) {
    LauncherItemController* controller = id_to_item_controller_map_[id];
    controller->Activate(source);
    return;
  }

  // Create a temporary application launcher item and use it to see if there are
  // running instances.
  scoped_ptr<AppShortcutLauncherItemController> app_controller(
      new AppShortcutLauncherItemController(app_id, this));
  if (!app_controller->GetRunningApplications().empty())
    app_controller->Activate(source);
  else
    LaunchApp(app_id, source, event_flags);
}

extensions::ExtensionPrefs::LaunchType
    ChromeLauncherController::GetLaunchType(ash::LauncherID id) {
  DCHECK(HasItemController(id));

  const Extension* extension = GetExtensionForAppID(
      id_to_item_controller_map_[id]->app_id());

  // An extension can be unloaded/updated/unavailable at any time.
  if (!extension)
    return extensions::ExtensionPrefs::LAUNCH_TYPE_DEFAULT;

  return profile_->GetExtensionService()->extension_prefs()->GetLaunchType(
      extension,
      extensions::ExtensionPrefs::LAUNCH_TYPE_DEFAULT);
}

ash::LauncherID ChromeLauncherController::GetLauncherIDForAppID(
    const std::string& app_id) {
  for (IDToItemControllerMap::const_iterator i =
           id_to_item_controller_map_.begin();
       i != id_to_item_controller_map_.end(); ++i) {
    if (i->second->type() == LauncherItemController::TYPE_APP_PANEL)
      continue;  // Don't include panels
    if (i->second->app_id() == app_id)
      return i->first;
  }
  return 0;
}

const std::string& ChromeLauncherController::GetAppIDForLauncherID(
    ash::LauncherID id) {
  CHECK(HasItemController(id));
  return id_to_item_controller_map_[id]->app_id();
}

void ChromeLauncherController::SetAppImage(const std::string& id,
                                           const gfx::ImageSkia& image) {
  // TODO: need to get this working for shortcuts.
  for (IDToItemControllerMap::const_iterator i =
           id_to_item_controller_map_.begin();
       i != id_to_item_controller_map_.end(); ++i) {
    LauncherItemController* controller = i->second;
    if (controller->app_id() != id)
      continue;
    if (controller->image_set_by_controller())
      continue;
    int index = model_->ItemIndexByID(i->first);
    if (index == -1)
      continue;
    ash::LauncherItem item = model_->items()[index];
    item.image = image;
    model_->Set(index, item);
    // It's possible we're waiting on more than one item, so don't break.
  }
}

void ChromeLauncherController::OnAutoHideBehaviorChanged(
    aura::Window* root_window,
    ash::ShelfAutoHideBehavior new_behavior) {
  SetShelfAutoHideBehaviorPrefs(new_behavior, root_window);
}

void ChromeLauncherController::SetLauncherItemImage(
    ash::LauncherID launcher_id,
    const gfx::ImageSkia& image) {
  int index = model_->ItemIndexByID(launcher_id);
  if (index == -1)
    return;
  ash::LauncherItem item = model_->items()[index];
  item.image = image;
  model_->Set(index, item);
}

bool ChromeLauncherController::CanPin() const {
  const PrefService::Preference* pref =
      profile_->GetPrefs()->FindPreference(prefs::kPinnedLauncherApps);
  return pref && pref->IsUserModifiable();
}

bool ChromeLauncherController::IsAppPinned(const std::string& app_id) {
  for (IDToItemControllerMap::const_iterator i =
           id_to_item_controller_map_.begin();
       i != id_to_item_controller_map_.end(); ++i) {
    if (IsPinned(i->first) && i->second->app_id() == app_id)
      return true;
  }
  return false;
}

bool ChromeLauncherController::IsWindowedAppInLauncher(
    const std::string& app_id) {
  int index = model_->ItemIndexByID(GetLauncherIDForAppID(app_id));
  if (index < 0)
    return false;

  ash::LauncherItemType type = model_->items()[index].type;
  return type == ash::TYPE_WINDOWED_APP;
}

void ChromeLauncherController::PinAppWithID(const std::string& app_id) {
  if (CanPin())
    DoPinAppWithID(app_id);
  else
    NOTREACHED();
}

void ChromeLauncherController::SetLaunchType(
    ash::LauncherID id,
    extensions::ExtensionPrefs::LaunchType launch_type) {
  if (!HasItemController(id))
    return;

  profile_->GetExtensionService()->extension_prefs()->SetLaunchType(
      id_to_item_controller_map_[id]->app_id(), launch_type);
}

void ChromeLauncherController::UnpinAppWithID(const std::string& app_id) {
  if (CanPin())
    DoUnpinAppWithID(app_id);
  else
    NOTREACHED();
}

bool ChromeLauncherController::IsLoggedInAsGuest() {
  return ProfileManager::GetDefaultProfileOrOffTheRecord()->IsOffTheRecord();
}

void ChromeLauncherController::CreateNewWindow() {
  chrome::NewEmptyWindow(
      GetProfileForNewWindows(), chrome::HOST_DESKTOP_TYPE_ASH);
}

void ChromeLauncherController::CreateNewIncognitoWindow() {
  chrome::NewEmptyWindow(GetProfileForNewWindows()->GetOffTheRecordProfile(),
                         chrome::HOST_DESKTOP_TYPE_ASH);
}

void ChromeLauncherController::PersistPinnedState() {
  if (ignore_persist_pinned_state_change_)
    return;
  // It is a coding error to call PersistPinnedState() if the pinned apps are
  // not user-editable. The code should check earlier and not perform any
  // modification actions that trigger persisting the state.
  if (!CanPin()) {
    NOTREACHED() << "Can't pin but pinned state being updated";
    return;
  }
  // Mutating kPinnedLauncherApps is going to notify us and trigger us to
  // process the change. We don't want that to happen so remove ourselves as a
  // listener.
  pref_change_registrar_.Remove(prefs::kPinnedLauncherApps);
  {
    ListPrefUpdate updater(profile_->GetPrefs(), prefs::kPinnedLauncherApps);
    updater->Clear();
    for (size_t i = 0; i < model_->items().size(); ++i) {
      if (model_->items()[i].type == ash::TYPE_APP_SHORTCUT) {
        ash::LauncherID id = model_->items()[i].id;
        if (HasItemController(id) && IsPinned(id)) {
          base::DictionaryValue* app_value = ash::CreateAppDict(
              id_to_item_controller_map_[id]->app_id());
          if (app_value)
            updater->Append(app_value);
        }
      } else if (model_->items()[i].type == ash::TYPE_BROWSER_SHORTCUT) {
        PersistChromeItemIndex(i);
      } else if (model_->items()[i].type == ash::TYPE_APP_LIST) {
        base::DictionaryValue* app_value = ash::CreateAppDict(
            kAppLauncherIdPlaceholder);
        if (app_value)
          updater->Append(app_value);
      }
    }
  }
  pref_change_registrar_.Add(
      prefs::kPinnedLauncherApps,
      base::Bind(&ChromeLauncherController::UpdateAppLaunchersFromPref,
                 base::Unretained(this)));
}

ash::LauncherModel* ChromeLauncherController::model() {
  return model_;
}

Profile* ChromeLauncherController::profile() {
  return profile_;
}

ash::ShelfAutoHideBehavior ChromeLauncherController::GetShelfAutoHideBehavior(
    aura::Window* root_window) const {
  // Don't show the shelf in app mode.
  if (chrome::IsRunningInAppMode())
    return ash::SHELF_AUTO_HIDE_ALWAYS_HIDDEN;

  // See comment in |kShelfAlignment| as to why we consider two prefs.
  const std::string behavior_value(
      GetPrefForRootWindow(profile_->GetPrefs(),
                           root_window,
                           prefs::kShelfAutoHideBehaviorLocal,
                           prefs::kShelfAutoHideBehavior));

  // Note: To maintain sync compatibility with old images of chrome/chromeos
  // the set of values that may be encountered includes the now-extinct
  // "Default" as well as "Never" and "Always", "Default" should now
  // be treated as "Never" (http://crbug.com/146773).
  if (behavior_value == ash::kShelfAutoHideBehaviorAlways)
    return ash::SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS;
  return ash::SHELF_AUTO_HIDE_BEHAVIOR_NEVER;
}

bool ChromeLauncherController::CanUserModifyShelfAutoHideBehavior(
    aura::Window* root_window) const {
  return profile_->GetPrefs()->
      FindPreference(prefs::kShelfAutoHideBehaviorLocal)->IsUserModifiable();
}

void ChromeLauncherController::ToggleShelfAutoHideBehavior(
    aura::Window* root_window) {
  ash::ShelfAutoHideBehavior behavior = GetShelfAutoHideBehavior(root_window) ==
      ash::SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS ?
          ash::SHELF_AUTO_HIDE_BEHAVIOR_NEVER :
          ash::SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS;
  SetShelfAutoHideBehaviorPrefs(behavior, root_window);
  return;
}

void ChromeLauncherController::RemoveTabFromRunningApp(
    WebContents* tab,
    const std::string& app_id) {
  web_contents_to_app_id_.erase(tab);
  // BrowserShortcutLauncherItemController::UpdateBrowserItemState() will update
  // the state when no application is associated with the tab.
  if (app_id.empty())
    return;

  AppIDToWebContentsListMap::iterator i_app_id =
      app_id_to_web_contents_list_.find(app_id);
  if (i_app_id != app_id_to_web_contents_list_.end()) {
    WebContentsList* tab_list = &i_app_id->second;
    tab_list->remove(tab);
    ash::LauncherItemStatus status = ash::STATUS_RUNNING;
    if (tab_list->empty()) {
      app_id_to_web_contents_list_.erase(i_app_id);
      status = ash::STATUS_CLOSED;
    }
    ash::LauncherID id = GetLauncherIDForAppID(app_id);
    if (id)
      SetItemStatus(id, status);
  }
}

void ChromeLauncherController::UpdateAppState(content::WebContents* contents,
                                              AppState app_state) {
  std::string app_id = app_tab_helper_->GetAppID(contents);

  // Check if the gMail app is loaded and it matches the given content.
  // This special treatment is needed to address crbug.com/234268.
  if (app_id.empty() && ContentCanBeHandledByGmailApp(contents))
    app_id = kGmailAppId;

  // Check the old |app_id| for a tab. If the contents has changed we need to
  // remove it from the previous app.
  if (web_contents_to_app_id_.find(contents) != web_contents_to_app_id_.end()) {
    std::string last_app_id = web_contents_to_app_id_[contents];
    if (last_app_id != app_id)
      RemoveTabFromRunningApp(contents, last_app_id);
  }

  web_contents_to_app_id_[contents] = app_id;

  if (app_state == APP_STATE_REMOVED) {
    // The tab has gone away.
    RemoveTabFromRunningApp(contents, app_id);
  } else if (!app_id.empty()) {
    WebContentsList& tab_list(app_id_to_web_contents_list_[app_id]);
    WebContentsList::const_iterator i_tab =
        std::find(tab_list.begin(), tab_list.end(), contents);

    if (i_tab == tab_list.end())
      tab_list.push_back(contents);

    if (app_state == APP_STATE_INACTIVE || app_state == APP_STATE_ACTIVE) {
      if (i_tab != tab_list.begin()) {
        // Going to running state, but wasn't the front tab, indicating that a
        // new tab has already become active.
        return;
      }
    }

    if (app_state == APP_STATE_ACTIVE || app_state == APP_STATE_WINDOW_ACTIVE) {
      tab_list.remove(contents);
      tab_list.push_front(contents);
    }

    ash::LauncherID id = GetLauncherIDForAppID(app_id);
    if (id) {
      // If the window is active, mark the app as active.
      SetItemStatus(id, app_state == APP_STATE_WINDOW_ACTIVE ?
          ash::STATUS_ACTIVE : ash::STATUS_RUNNING);
    }
  }
}

void ChromeLauncherController::SetRefocusURLPatternForTest(ash::LauncherID id,
                                                           const GURL& url) {
  DCHECK(HasItemController(id));
  LauncherItemController* controller = id_to_item_controller_map_[id];

  int index = model_->ItemIndexByID(id);
  if (index == -1) {
    NOTREACHED() << "Invalid launcher id";
    return;
  }

  ash::LauncherItemType type = model_->items()[index].type;
  if (type == ash::TYPE_APP_SHORTCUT || type == ash::TYPE_WINDOWED_APP) {
    AppShortcutLauncherItemController* app_controller =
        static_cast<AppShortcutLauncherItemController*>(controller);
    app_controller->set_refocus_url(url);
  } else {
    NOTREACHED() << "Invalid launcher type";
  }
}

const Extension* ChromeLauncherController::GetExtensionForAppID(
    const std::string& app_id) const {
  // Some unit tests do not have a real extension.
  return (profile_->GetExtensionService()) ?
      profile_->GetExtensionService()->GetInstalledExtension(app_id) : NULL;
}

void ChromeLauncherController::ActivateWindowOrMinimizeIfActive(
    ui::BaseWindow* window,
    bool allow_minimize) {
  // In separated desktop mode we might have to teleport a window back to the
  // current user.
  if (chrome::MultiUserWindowManager::GetMultiProfileMode() ==
          chrome::MultiUserWindowManager::MULTI_PROFILE_MODE_SEPARATED) {
    aura::Window* native_window = window->GetNativeWindow();
    const std::string& current_user =
        multi_user_util::GetUserIDFromProfile(profile());
    chrome::MultiUserWindowManager* manager =
        chrome::MultiUserWindowManager::GetInstance();
    if (!manager->IsWindowOnDesktopOfUser(native_window, current_user)) {
      ash::MultiProfileUMA::RecordTeleportAction(
          ash::MultiProfileUMA::TELEPORT_WINDOW_RETURN_BY_LAUNCHER);
      manager->ShowWindowForUser(native_window, current_user);
      window->Activate();
      return;
    }
  }

  if (window->IsActive() && allow_minimize) {
    if (CommandLine::ForCurrentProcess()->HasSwitch(
            switches::kDisableMinimizeOnSecondLauncherItemClick)) {
      AnimateWindow(window->GetNativeWindow(),
                    views::corewm::WINDOW_ANIMATION_TYPE_BOUNCE);
    } else {
      window->Minimize();
    }
  } else {
    window->Show();
    window->Activate();
  }
}

ash::LauncherID ChromeLauncherController::GetIDByWindow(aura::Window* window) {
  int browser_index =
      ash::GetShelfItemIndexForType(ash::TYPE_BROWSER_SHORTCUT, *model_);
  DCHECK_GE(browser_index, 0);
  ash::LauncherID browser_id = model_->items()[browser_index].id;

  IDToItemControllerMap::const_iterator i = id_to_item_controller_map_.begin();
  for (; i != id_to_item_controller_map_.end(); ++i) {
    // Since a |window| can be used by multiple applications, an explicit
    // application always gets chosen over the generic browser.
    if (i->first != browser_id && i->second->IsCurrentlyShownInWindow(window))
      return i->first;
  }

  if (i == id_to_item_controller_map_.end() &&
      GetBrowserShortcutLauncherItemController()->
          IsCurrentlyShownInWindow(window))
    return browser_id;

  return 0;
}

void ChromeLauncherController::OnLauncherCreated(ash::Launcher* launcher) {
  launchers_.insert(launcher);
  launcher->shelf_widget()->shelf_layout_manager()->AddObserver(this);
}

void ChromeLauncherController::OnLauncherDestroyed(ash::Launcher* launcher) {
  launchers_.erase(launcher);
  // RemoveObserver is not called here, since by the time this method is called
  // Launcher is already in its destructor.
}

void ChromeLauncherController::LauncherItemAdded(int index) {
  // The app list launcher can get added to the shelf after we applied the
  // preferences. In that case the item might be at the wrong spot. As such we
  // call the function again.
  if (model_->items()[index].type == ash::TYPE_APP_LIST &&
      ash::switches::UseAlternateShelfLayout())
    UpdateAppLaunchersFromPref();
}

void ChromeLauncherController::LauncherItemRemoved(int index,
                                                   ash::LauncherID id) {
}

void ChromeLauncherController::LauncherItemMoved(int start_index,
                                                 int target_index) {
  const ash::LauncherItem& item = model_->items()[target_index];
  // We remember the moved item position if it is either pinnable or
  // it is the app list with the alternate shelf layout.
  if ((HasItemController(item.id) && IsPinned(item.id)) ||
      (ash::switches::UseAlternateShelfLayout() &&
       item.type == ash::TYPE_APP_LIST))
    PersistPinnedState();
}

void ChromeLauncherController::LauncherItemChanged(
    int index,
    const ash::LauncherItem& old_item) {
}

void ChromeLauncherController::LauncherStatusChanged() {
}

void ChromeLauncherController::ActiveUserChanged(
    const std::string& user_email) {
  // Coming here the default profile is already switched. All profile specific
  // resources get released and the new profile gets attached instead.
  ReleaseProfile();
  AttachProfile(ProfileManager::GetDefaultProfile());
  // Update the V1 applications.
  browser_status_monitor_->ActiveUserChanged(user_email);
  // Switch the running applications to the new user.
  shell_window_controller_->ActiveUserChanged(user_email);
  // Update the user specific shell properties from the new user profile.
  UpdateAppLaunchersFromPref();
  SetShelfAlignmentFromPrefs();
  SetShelfAutoHideBehaviorFromPrefs();
  SetShelfBehaviorsFromPrefs();
}

void ChromeLauncherController::AdditionalUserAddedToSession(Profile* profile) {
  // Switch the running applications to the new user.
  shell_window_controller_->AdditionalUserAddedToSession(profile);
}

void ChromeLauncherController::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  switch (type) {
    case chrome::NOTIFICATION_EXTENSION_LOADED: {
      const Extension* extension =
          content::Details<const Extension>(details).ptr();
      if (IsAppPinned(extension->id())) {
        // Clear and re-fetch to ensure icon is up-to-date.
        app_icon_loader_->ClearImage(extension->id());
        app_icon_loader_->FetchImage(extension->id());
      }

      UpdateAppLaunchersFromPref();
      break;
    }
    case chrome::NOTIFICATION_EXTENSION_UNLOADED: {
      const content::Details<UnloadedExtensionInfo>& unload_info(details);
      const Extension* extension = unload_info->extension;
      const std::string& id = extension->id();
      // Since we might have windowed apps of this type which might have
      // outstanding locks which needs to be removed.
      if (GetLauncherIDForAppID(id) &&
          unload_info->reason == UnloadedExtensionInfo::REASON_UNINSTALL) {
        CloseWindowedAppsFromRemovedExtension(id);
      }

      if (IsAppPinned(id)) {
        if (unload_info->reason == UnloadedExtensionInfo::REASON_UNINSTALL) {
          DoUnpinAppWithID(id);
          app_icon_loader_->ClearImage(id);
        } else {
          app_icon_loader_->UpdateImage(id);
        }
      }
      break;
    }
    default:
      NOTREACHED() << "Unexpected notification type=" << type;
  }
}

void ChromeLauncherController::OnShelfAlignmentChanged(
    aura::Window* root_window) {
  const char* pref_value = NULL;
  switch (ash::Shell::GetInstance()->GetShelfAlignment(root_window)) {
    case ash::SHELF_ALIGNMENT_BOTTOM:
      pref_value = ash::kShelfAlignmentBottom;
      break;
    case ash::SHELF_ALIGNMENT_LEFT:
      pref_value = ash::kShelfAlignmentLeft;
      break;
    case ash::SHELF_ALIGNMENT_RIGHT:
      pref_value = ash::kShelfAlignmentRight;
      break;
    case ash::SHELF_ALIGNMENT_TOP:
      pref_value = ash::kShelfAlignmentTop;
  }

  UpdatePerDisplayPref(
      profile_->GetPrefs(), root_window, prefs::kShelfAlignment, pref_value);

  if (root_window == ash::Shell::GetPrimaryRootWindow()) {
    // See comment in |kShelfAlignment| about why we have two prefs here.
    profile_->GetPrefs()->SetString(prefs::kShelfAlignmentLocal, pref_value);
    profile_->GetPrefs()->SetString(prefs::kShelfAlignment, pref_value);
  }
}

void ChromeLauncherController::OnDisplayConfigurationChanging() {
}

void ChromeLauncherController::OnDisplayConfigurationChanged() {
  SetShelfBehaviorsFromPrefs();
}

void ChromeLauncherController::OnIsSyncingChanged() {
  PrefServiceSyncable* prefs = PrefServiceSyncable::FromProfile(profile_);
  MaybePropagatePrefToLocal(prefs,
                            prefs::kShelfAlignmentLocal,
                            prefs::kShelfAlignment);
  MaybePropagatePrefToLocal(prefs,
                            prefs::kShelfAutoHideBehaviorLocal,
                            prefs::kShelfAutoHideBehavior);
}

void ChromeLauncherController::OnAppSyncUIStatusChanged() {
  if (app_sync_ui_state_->status() == AppSyncUIState::STATUS_SYNCING)
    model_->SetStatus(ash::LauncherModel::STATUS_LOADING);
  else
    model_->SetStatus(ash::LauncherModel::STATUS_NORMAL);
}

void ChromeLauncherController::ExtensionEnableFlowFinished() {
  LaunchApp(extension_enable_flow_->extension_id(),
            ash::LAUNCH_FROM_UNKNOWN,
            ui::EF_NONE);
  extension_enable_flow_.reset();
}

void ChromeLauncherController::ExtensionEnableFlowAborted(bool user_initiated) {
  extension_enable_flow_.reset();
}

ChromeLauncherAppMenuItems ChromeLauncherController::GetApplicationList(
    const ash::LauncherItem& item,
    int event_flags) {
  // Make sure that there is a controller associated with the id and that the
  // extension itself is a valid application and not a panel.
  if (!HasItemController(item.id) ||
      !GetLauncherIDForAppID(id_to_item_controller_map_[item.id]->app_id()))
    return ChromeLauncherAppMenuItems().Pass();

  return id_to_item_controller_map_[item.id]->GetApplicationList(event_flags);
}

std::vector<content::WebContents*>
ChromeLauncherController::GetV1ApplicationsFromAppId(std::string app_id) {
  ash::LauncherID id = GetLauncherIDForAppID(app_id);

  // If there is no such an item pinned to the launcher, no menu gets created.
  if (id) {
    LauncherItemController* controller = id_to_item_controller_map_[id];
    DCHECK(controller);
    if (controller->type() == LauncherItemController::TYPE_SHORTCUT)
      return GetV1ApplicationsFromController(controller);
  }
  return std::vector<content::WebContents*>();
}

void ChromeLauncherController::ActivateShellApp(const std::string& app_id,
                                                int index) {
  ash::LauncherID id = GetLauncherIDForAppID(app_id);
  if (id) {
    LauncherItemController* controller = id_to_item_controller_map_[id];
    if (controller->type() == LauncherItemController::TYPE_APP) {
      ShellWindowLauncherItemController* shell_window_controller =
          static_cast<ShellWindowLauncherItemController*>(controller);
      shell_window_controller->ActivateIndexedApp(index);
    }
  }
}

bool ChromeLauncherController::IsWebContentHandledByApplication(
    content::WebContents* web_contents,
    const std::string& app_id) {
  if ((web_contents_to_app_id_.find(web_contents) !=
       web_contents_to_app_id_.end()) &&
      (web_contents_to_app_id_[web_contents] == app_id))
    return true;
  return (app_id == kGmailAppId && ContentCanBeHandledByGmailApp(web_contents));
}

bool ChromeLauncherController::ContentCanBeHandledByGmailApp(
    content::WebContents* web_contents) {
  ash::LauncherID id = GetLauncherIDForAppID(kGmailAppId);
  if (id) {
    const GURL url = web_contents->GetURL();
    // We need to extend the application matching for the gMail app beyond the
    // manifest file's specification. This is required because of the namespace
    // overlap with the offline app ("/mail/mu/").
    if (!MatchPattern(url.path(), "/mail/mu/*") &&
        MatchPattern(url.path(), "/mail/*") &&
        GetExtensionForAppID(kGmailAppId) &&
        GetExtensionForAppID(kGmailAppId)->OverlapsWithOrigin(url))
      return true;
  }
  return false;
}

gfx::Image ChromeLauncherController::GetAppListIcon(
    content::WebContents* web_contents) const {
  ResourceBundle& rb = ResourceBundle::GetSharedInstance();
  if (IsIncognito(web_contents))
    return rb.GetImageNamed(IDR_AURA_LAUNCHER_LIST_INCOGNITO_BROWSER);
  FaviconTabHelper* favicon_tab_helper =
      FaviconTabHelper::FromWebContents(web_contents);
  gfx::Image result = favicon_tab_helper->GetFavicon();
  if (result.IsEmpty())
    return rb.GetImageNamed(IDR_DEFAULT_FAVICON);
  return result;
}

string16 ChromeLauncherController::GetAppListTitle(
    content::WebContents* web_contents) const {
  string16 title = web_contents->GetTitle();
  if (!title.empty())
    return title;
  WebContentsToAppIDMap::const_iterator iter =
      web_contents_to_app_id_.find(web_contents);
  if (iter != web_contents_to_app_id_.end()) {
    std::string app_id = iter->second;
    const extensions::Extension* extension = GetExtensionForAppID(app_id);
    if (extension)
      return UTF8ToUTF16(extension->name());
  }
  return l10n_util::GetStringUTF16(IDS_NEW_TAB_TITLE);
}

ash::LauncherID ChromeLauncherController::CreateAppShortcutLauncherItem(
    const std::string& app_id,
    int index) {
  return CreateAppShortcutLauncherItemWithType(app_id,
                                               index,
                                               ash::TYPE_APP_SHORTCUT);
}

void ChromeLauncherController::SetAppTabHelperForTest(AppTabHelper* helper) {
  app_tab_helper_.reset(helper);
}

void ChromeLauncherController::SetAppIconLoaderForTest(
    extensions::AppIconLoader* loader) {
  app_icon_loader_.reset(loader);
}

const std::string& ChromeLauncherController::GetAppIdFromLauncherIdForTest(
    ash::LauncherID id) {
  return id_to_item_controller_map_[id]->app_id();
}

void ChromeLauncherController::SetLauncherItemDelegateManagerForTest(
    ash::LauncherItemDelegateManager* manager) {
  item_delegate_manager_ = manager;
}

ash::LauncherID ChromeLauncherController::CreateAppShortcutLauncherItemWithType(
    const std::string& app_id,
    int index,
    ash::LauncherItemType launcher_item_type) {
  AppShortcutLauncherItemController* controller =
      new AppShortcutLauncherItemController(app_id, this);
  ash::LauncherID launcher_id = InsertAppLauncherItem(
      controller, app_id, ash::STATUS_CLOSED, index, launcher_item_type);
  return launcher_id;
}

LauncherItemController* ChromeLauncherController::GetLauncherItemController(
    const ash::LauncherID id) {
  if (!HasItemController(id))
    return NULL;
  return id_to_item_controller_map_[id];
}

bool ChromeLauncherController::IsBrowserFromActiveUser(Browser* browser) {
  // If running multi user mode with separate desktops, we have to check if the
  // browser is from the active user.
  if (chrome::MultiUserWindowManager::GetMultiProfileMode() !=
          chrome::MultiUserWindowManager::MULTI_PROFILE_MODE_SEPARATED)
    return true;
  return multi_user_util::IsProfileFromActiveUser(browser->profile());
}

Profile* ChromeLauncherController::GetProfileForNewWindows() {
  return ProfileManager::GetDefaultProfileOrOffTheRecord();
}

void ChromeLauncherController::LauncherItemClosed(ash::LauncherID id) {
  IDToItemControllerMap::iterator iter = id_to_item_controller_map_.find(id);
  CHECK(iter != id_to_item_controller_map_.end());
  CHECK(iter->second);
  app_icon_loader_->ClearImage(iter->second->app_id());
  id_to_item_controller_map_.erase(iter);
  int index = model_->ItemIndexByID(id);
  // A "browser proxy" is not known to the model and this removal does
  // therefore not need to be propagated to the model.
  if (index != -1)
    model_->RemoveItemAt(index);
}

void ChromeLauncherController::DoPinAppWithID(const std::string& app_id) {
  // If there is an item, do nothing and return.
  if (IsAppPinned(app_id))
    return;

  ash::LauncherID launcher_id = GetLauncherIDForAppID(app_id);
  if (launcher_id) {
    // App item exists, pin it
    Pin(launcher_id);
  } else {
    // Otherwise, create a shortcut item for it.
    CreateAppShortcutLauncherItem(app_id, model_->item_count());
    if (CanPin())
      PersistPinnedState();
  }
}

void ChromeLauncherController::DoUnpinAppWithID(const std::string& app_id) {
  ash::LauncherID launcher_id = GetLauncherIDForAppID(app_id);
  if (launcher_id && IsPinned(launcher_id))
    Unpin(launcher_id);
}

int ChromeLauncherController::PinRunningAppInternal(
    int index,
    ash::LauncherID launcher_id) {
  int running_index = model_->ItemIndexByID(launcher_id);
  ash::LauncherItem item = model_->items()[running_index];
  DCHECK(item.type == ash::TYPE_WINDOWED_APP ||
         item.type == ash::TYPE_PLATFORM_APP);
  item.type = ash::TYPE_APP_SHORTCUT;
  model_->Set(running_index, item);
  // The |LauncherModel|'s weight system might reposition the item to a
  // new index, so we get the index again.
  running_index = model_->ItemIndexByID(launcher_id);
  if (running_index < index)
    --index;
  if (running_index != index)
    model_->Move(running_index, index);
  return index;
}

void ChromeLauncherController::UnpinRunningAppInternal(int index) {
  DCHECK_GE(index, 0);
  ash::LauncherItem item = model_->items()[index];
  DCHECK_EQ(item.type, ash::TYPE_APP_SHORTCUT);
  item.type = ash::TYPE_WINDOWED_APP;
  // A platform app and a windowed app are sharing TYPE_APP_SHORTCUT. As such
  // we have to check here what this was before it got a shortcut.
  if (HasItemController(item.id) &&
      id_to_item_controller_map_[item.id]->type() ==
          LauncherItemController::TYPE_APP)
    item.type = ash::TYPE_PLATFORM_APP;
  model_->Set(index, item);
}

void ChromeLauncherController::UpdateAppLaunchersFromPref() {
  // There are various functions which will trigger a |PersistPinnedState| call
  // like a direct call to |DoPinAppWithID|, or an indirect call to the menu
  // model which will use weights to re-arrange the icons to new positions.
  // Since this function is meant to synchronize the "is state" with the
  // "sync state", it makes no sense to store any changes by this function back
  // into the pref state. Therefore we tell |persistPinnedState| to ignore any
  // invocations while we are running.
  base::AutoReset<bool> auto_reset(&ignore_persist_pinned_state_change_, true);
  std::vector<std::string> pinned_apps = GetListOfPinnedAppsAndBrowser();

  int index = 0;
  int max_index = model_->item_count();

  // When one of the two special items cannot be moved (and we do not know where
  // yet), we remember the current location in one of these variables.
  int chrome_index = -1;
  int app_list_index = -1;

  // Walk the model and |pinned_apps| from the pref lockstep, adding and
  // removing items as necessary. NB: This code uses plain old indexing instead
  // of iterators because of model mutations as part of the loop.
  std::vector<std::string>::const_iterator pref_app_id(pinned_apps.begin());
  for (; index < max_index && pref_app_id != pinned_apps.end(); ++index) {
    // Check if we have an item which we need to handle.
    if (*pref_app_id == extension_misc::kChromeAppId ||
        *pref_app_id == kAppLauncherIdPlaceholder ||
        IsAppPinned(*pref_app_id)) {
      for (; index < max_index; ++index) {
        const ash::LauncherItem& item(model_->items()[index]);
        bool is_app_list = item.type == ash::TYPE_APP_LIST;
        bool is_chrome = item.type == ash::TYPE_BROWSER_SHORTCUT;
        if (item.type != ash::TYPE_APP_SHORTCUT && !is_app_list && !is_chrome)
          continue;
        IDToItemControllerMap::const_iterator entry =
            id_to_item_controller_map_.find(item.id);
        if ((kAppLauncherIdPlaceholder == *pref_app_id && is_app_list) ||
            (extension_misc::kChromeAppId == *pref_app_id && is_chrome) ||
            (entry != id_to_item_controller_map_.end() &&
             entry->second->app_id() == *pref_app_id)) {
          // Check if an item needs to be moved here.
          MoveChromeOrApplistToFinalPosition(
              is_chrome, is_app_list, index, &chrome_index, &app_list_index);
          ++pref_app_id;
          break;
        } else {
          if (is_chrome || is_app_list) {
            // We cannot delete any of these shortcuts. As such we remember
            // their positions and move them later where they belong.
            if (is_chrome)
              chrome_index = index;
            else
              app_list_index = index;
            // And skip the item - or exit the loop if end is reached (note that
            // in that case we will reduce the index again by one and this only
            // compensates for it).
            if (index >= max_index - 1)
              break;
            ++index;
          } else {
            // Check if this is a platform or a windowed app.
            if (item.type == ash::TYPE_APP_SHORTCUT &&
                (id_to_item_controller_map_[item.id]->locked() ||
                 id_to_item_controller_map_[item.id]->type() ==
                     LauncherItemController::TYPE_APP)) {
              // Note: This will not change the amount of items (|max_index|).
              // Even changes to the actual |index| due to item weighting
              // changes should be fine.
              UnpinRunningAppInternal(index);
            } else {
              LauncherItemClosed(item.id);
              --max_index;
            }
          }
          --index;
        }
      }
      // If the item wasn't found, that means id_to_item_controller_map_
      // is out of sync.
      DCHECK(index <= max_index);
    } else {
      // Check if the item was already running but not yet pinned.
      ash::LauncherID launcher_id = GetLauncherIDForAppID(*pref_app_id);
      if (launcher_id) {
        // This app is running but not yet pinned. So pin and move it.
        index = PinRunningAppInternal(index, launcher_id);
      } else {
        // This app wasn't pinned before, insert a new entry.
        launcher_id = CreateAppShortcutLauncherItem(*pref_app_id, index);
        ++max_index;
        index = model_->ItemIndexByID(launcher_id);
      }
      ++pref_app_id;
    }
  }

  // Remove any trailing existing items.
  while (index < model_->item_count()) {
    const ash::LauncherItem& item(model_->items()[index]);
    if (item.type == ash::TYPE_APP_SHORTCUT) {
      if (id_to_item_controller_map_[item.id]->locked() ||
          id_to_item_controller_map_[item.id]->type() ==
              LauncherItemController::TYPE_APP)
        UnpinRunningAppInternal(index);
      else
        LauncherItemClosed(item.id);
    } else {
      if (item.type == ash::TYPE_BROWSER_SHORTCUT)
        chrome_index = index;
      else if (item.type == ash::TYPE_APP_LIST)
        app_list_index = index;
      ++index;
    }
  }

  // Append unprocessed items from the pref to the end of the model.
  for (; pref_app_id != pinned_apps.end(); ++pref_app_id) {
    // All items but the chrome and / or app list shortcut needs to be added.
    bool is_chrome = *pref_app_id == extension_misc::kChromeAppId;
    bool is_app_list = *pref_app_id == kAppLauncherIdPlaceholder;
    // Coming here we know the next item which can be finalized, either the
    // chrome item or the app launcher. The final position is the end of the
    // list. The menu model will make sure that the item is grouped according
    // to its weight (which we do not know here).
    if (!is_chrome && !is_app_list) {
      DoPinAppWithID(*pref_app_id);
      int target_index = FindInsertionPoint(false);
      ash::LauncherID id = GetLauncherIDForAppID(*pref_app_id);
      int source_index = model_->ItemIndexByID(id);
      if (source_index != target_index)
        model_->Move(source_index, target_index);

      // Needed for the old layout - the weight might force it to be lower in
      // rank.
      if (app_list_index != -1 && target_index <= app_list_index)
        ++app_list_index;
    } else {
      int target_index = FindInsertionPoint(is_app_list);
      MoveChromeOrApplistToFinalPosition(
          is_chrome, is_app_list, target_index, &chrome_index, &app_list_index);
    }
  }
}

void ChromeLauncherController::SetShelfAutoHideBehaviorPrefs(
    ash::ShelfAutoHideBehavior behavior,
    aura::Window* root_window) {
  const char* value = NULL;
  switch (behavior) {
    case ash::SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS:
      value = ash::kShelfAutoHideBehaviorAlways;
      break;
    case ash::SHELF_AUTO_HIDE_BEHAVIOR_NEVER:
      value = ash::kShelfAutoHideBehaviorNever;
      break;
    case ash::SHELF_AUTO_HIDE_ALWAYS_HIDDEN:
      // This one should not be a valid preference option for now. We only want
      // to completely hide it when we run app mode.
      NOTREACHED();
      return;
  }

  UpdatePerDisplayPref(
      profile_->GetPrefs(), root_window, prefs::kShelfAutoHideBehavior, value);

  if (root_window == ash::Shell::GetPrimaryRootWindow()) {
    // See comment in |kShelfAlignment| about why we have two prefs here.
    profile_->GetPrefs()->SetString(prefs::kShelfAutoHideBehaviorLocal, value);
    profile_->GetPrefs()->SetString(prefs::kShelfAutoHideBehavior, value);
  }
}

void ChromeLauncherController::SetShelfAutoHideBehaviorFromPrefs() {
  aura::Window::Windows root_windows = ash::Shell::GetAllRootWindows();

  for (aura::Window::Windows::const_iterator iter = root_windows.begin();
       iter != root_windows.end(); ++iter) {
    ash::Shell::GetInstance()->SetShelfAutoHideBehavior(
        GetShelfAutoHideBehavior(*iter), *iter);
  }
}

void ChromeLauncherController::SetShelfAlignmentFromPrefs() {
  if (!ash::ShelfWidget::ShelfAlignmentAllowed())
    return;

  aura::Window::Windows root_windows = ash::Shell::GetAllRootWindows();

  for (aura::Window::Windows::const_iterator iter = root_windows.begin();
       iter != root_windows.end(); ++iter) {
    // See comment in |kShelfAlignment| as to why we consider two prefs.
    const std::string alignment_value(
        GetPrefForRootWindow(profile_->GetPrefs(),
                             *iter,
                             prefs::kShelfAlignmentLocal,
                             prefs::kShelfAlignment));
    ash::ShelfAlignment alignment = ash::SHELF_ALIGNMENT_BOTTOM;
    if (alignment_value == ash::kShelfAlignmentLeft)
      alignment = ash::SHELF_ALIGNMENT_LEFT;
    else if (alignment_value == ash::kShelfAlignmentRight)
      alignment = ash::SHELF_ALIGNMENT_RIGHT;
    else if (alignment_value == ash::kShelfAlignmentTop)
      alignment = ash::SHELF_ALIGNMENT_TOP;
    ash::Shell::GetInstance()->SetShelfAlignment(alignment, *iter);
  }
}

void ChromeLauncherController::SetShelfBehaviorsFromPrefs() {
  SetShelfAutoHideBehaviorFromPrefs();
  SetShelfAlignmentFromPrefs();
}

WebContents* ChromeLauncherController::GetLastActiveWebContents(
    const std::string& app_id) {
  AppIDToWebContentsListMap::const_iterator i =
      app_id_to_web_contents_list_.find(app_id);
  if (i == app_id_to_web_contents_list_.end())
    return NULL;
  DCHECK_GT(i->second.size(), 0u);
  return *i->second.begin();
}

ash::LauncherID ChromeLauncherController::InsertAppLauncherItem(
    LauncherItemController* controller,
    const std::string& app_id,
    ash::LauncherItemStatus status,
    int index,
    ash::LauncherItemType launcher_item_type) {
  ash::LauncherID id = model_->next_id();
  CHECK(!HasItemController(id));
  CHECK(controller);
  id_to_item_controller_map_[id] = controller;
  controller->set_launcher_id(id);

  ash::LauncherItem item;
  item.type = launcher_item_type;
  item.image = extensions::IconsInfo::GetDefaultAppIcon();

  WebContents* active_tab = GetLastActiveWebContents(app_id);
  if (active_tab) {
    Browser* browser = chrome::FindBrowserWithWebContents(active_tab);
    DCHECK(browser);
    if (browser->window()->IsActive())
      status = ash::STATUS_ACTIVE;
    else
      status = ash::STATUS_RUNNING;
  }
  item.status = status;

  model_->AddAt(index, item);

  app_icon_loader_->FetchImage(app_id);

  SetLauncherItemDelegate(id, controller);

  return id;
}

bool ChromeLauncherController::HasItemController(ash::LauncherID id) const {
  return id_to_item_controller_map_.find(id) !=
         id_to_item_controller_map_.end();
}

std::vector<content::WebContents*>
ChromeLauncherController::GetV1ApplicationsFromController(
    LauncherItemController* controller) {
  DCHECK(controller->type() == LauncherItemController::TYPE_SHORTCUT);
  AppShortcutLauncherItemController* app_controller =
      static_cast<AppShortcutLauncherItemController*>(controller);
  return app_controller->GetRunningApplications();
}

BrowserShortcutLauncherItemController*
ChromeLauncherController::GetBrowserShortcutLauncherItemController() {
  for (IDToItemControllerMap::iterator i = id_to_item_controller_map_.begin();
      i != id_to_item_controller_map_.end(); ++i) {
    int index = model_->ItemIndexByID(i->first);
    const ash::LauncherItem& item = model_->items()[index];
    if (item.type == ash::TYPE_BROWSER_SHORTCUT)
      return static_cast<BrowserShortcutLauncherItemController*>(i->second);
  }
  // Create a LauncherItemController for the Browser shortcut if it does not
  // exist yet.
  ash::LauncherID id = CreateBrowserShortcutLauncherItem();
  DCHECK(id_to_item_controller_map_[id]);
  return static_cast<BrowserShortcutLauncherItemController*>(
      id_to_item_controller_map_[id]);
}

ash::LauncherID ChromeLauncherController::CreateBrowserShortcutLauncherItem() {
  ash::LauncherItem browser_shortcut;
  browser_shortcut.type = ash::TYPE_BROWSER_SHORTCUT;
  ResourceBundle& rb = ResourceBundle::GetSharedInstance();
  browser_shortcut.image = *rb.GetImageSkiaNamed(IDR_PRODUCT_LOGO_32);
  ash::LauncherID id = model_->next_id();
  size_t index = GetChromeIconIndexForCreation();
  model_->AddAt(index, browser_shortcut);
  id_to_item_controller_map_[id] =
      new BrowserShortcutLauncherItemController(this);
  id_to_item_controller_map_[id]->set_launcher_id(id);
  // LauncherItemDelegateManager owns BrowserShortcutLauncherItemController.
  SetLauncherItemDelegate(id, id_to_item_controller_map_[id]);
  return id;
}

void ChromeLauncherController::PersistChromeItemIndex(int index) {
  profile_->GetPrefs()->SetInteger(prefs::kShelfChromeIconIndex, index);
}

int ChromeLauncherController::GetChromeIconIndexFromPref() const {
  size_t index = profile_->GetPrefs()->GetInteger(prefs::kShelfChromeIconIndex);
  const base::ListValue* pinned_apps_pref =
      profile_->GetPrefs()->GetList(prefs::kPinnedLauncherApps);
  return std::max(static_cast<size_t>(0),
                  std::min(pinned_apps_pref->GetSize(), index));
}

void ChromeLauncherController::MoveChromeOrApplistToFinalPosition(
    bool is_chrome,
    bool is_app_list,
    int target_index,
    int* chrome_index,
    int* app_list_index) {
  if (is_chrome && *chrome_index != -1) {
    model_->Move(*chrome_index, target_index);
    if (*app_list_index != -1 &&
        *chrome_index < *app_list_index &&
        target_index > *app_list_index)
      --(*app_list_index);
    *chrome_index = -1;
  } else if (is_app_list && *app_list_index != -1) {
    model_->Move(*app_list_index, target_index);
    if (*chrome_index != -1 &&
        *app_list_index < *chrome_index &&
        target_index > *chrome_index)
      --(*chrome_index);
    *app_list_index = -1;
  }
}

int ChromeLauncherController::FindInsertionPoint(bool is_app_list) {
  bool alternate = ash::switches::UseAlternateShelfLayout();
  for (int i = model_->item_count() - 1; i > 0; --i) {
    ash::LauncherItemType type = model_->items()[i].type;
    if (type == ash::TYPE_APP_SHORTCUT ||
        ((is_app_list || alternate) && type == ash::TYPE_APP_LIST) ||
        type == ash::TYPE_BROWSER_SHORTCUT ||
        type == ash::TYPE_WINDOWED_APP)
      return i;
  }
  return 0;
}

int ChromeLauncherController::GetChromeIconIndexForCreation() {
  // We get the list of pinned apps as they currently would get pinned.
  // Within this list the chrome icon will be the correct location.
  std::vector<std::string> pinned_apps = GetListOfPinnedAppsAndBrowser();

  std::vector<std::string>::iterator it =
      std::find(pinned_apps.begin(),
                pinned_apps.end(),
                std::string(extension_misc::kChromeAppId));
  DCHECK(it != pinned_apps.end());
  int index = it - pinned_apps.begin();

  // We should do here a comparison between the is state and the "want to be"
  // state since some apps might be able to pin but are not yet. Instead - for
  // the time being we clamp against the amount of known items and wait for the
  // next |UpdateAppLaunchersFromPref()| call to correct it - it will come since
  // the pinning will be done then.
  return std::min(model_->item_count(), index);
}

std::vector<std::string>
ChromeLauncherController::GetListOfPinnedAppsAndBrowser() {
  // Adding the app list item to the list of items requires that the ID is not
  // a valid and known ID for the extension system. The ID was constructed that
  // way - but just to make sure...
  DCHECK(!app_tab_helper_->IsValidIDForCurrentUser(kAppLauncherIdPlaceholder));

  std::vector<std::string> pinned_apps;

  // Get the new incarnation of the list.
  const base::ListValue* pinned_apps_pref =
      profile_->GetPrefs()->GetList(prefs::kPinnedLauncherApps);

  // Keep track of the addition of the chrome and the app list icon.
  bool chrome_icon_added = false;
  bool app_list_icon_added = false;
  size_t chrome_icon_index = GetChromeIconIndexFromPref();

  // See if the chrome string is already in the pinned list and remove it if
  // needed.
  base::Value* chrome_app = ash::CreateAppDict(extension_misc::kChromeAppId);
  if (chrome_app) {
    chrome_icon_added = pinned_apps_pref->Find(*chrome_app) !=
        pinned_apps_pref->end();
    delete chrome_app;
  }

  for (size_t index = 0; index < pinned_apps_pref->GetSize(); ++index) {
    // We need to position the chrome icon relative to it's place in the pinned
    // preference list - even if an item of that list isn't shown yet.
    if (index == chrome_icon_index && !chrome_icon_added) {
      pinned_apps.push_back(extension_misc::kChromeAppId);
      chrome_icon_added = true;
    }
    const DictionaryValue* app = NULL;
    std::string app_id;
    if (pinned_apps_pref->GetDictionary(index, &app) &&
        app->GetString(ash::kPinnedAppsPrefAppIDPath, &app_id) &&
        (std::find(pinned_apps.begin(), pinned_apps.end(), app_id) ==
             pinned_apps.end())) {
      if (app_id == extension_misc::kChromeAppId) {
        chrome_icon_added = true;
        pinned_apps.push_back(extension_misc::kChromeAppId);
      } else if (app_id == kAppLauncherIdPlaceholder) {
        app_list_icon_added = true;
        pinned_apps.push_back(kAppLauncherIdPlaceholder);
      } else if (app_tab_helper_->IsValidIDForCurrentUser(app_id)) {
        // Note: In multi profile scenarios we only want to show pinnable apps
        // here which is correct. Running applications from the other users will
        // continue to run. So no need for multi profile modifications.
        pinned_apps.push_back(app_id);
      }
    }
  }

  // If not added yet, the chrome item will be the last item in the list.
  if (!chrome_icon_added)
    pinned_apps.push_back(extension_misc::kChromeAppId);

  // If not added yet, add the app list item either at the end or at the
  // beginning - depending on the shelf layout.
  if (!app_list_icon_added) {
    if (ash::switches::UseAlternateShelfLayout())
      pinned_apps.insert(pinned_apps.begin(), kAppLauncherIdPlaceholder);
    else
      pinned_apps.push_back(kAppLauncherIdPlaceholder);
  }
  return pinned_apps;
}

bool ChromeLauncherController::IsIncognito(
    const content::WebContents* web_contents) const {
  const Profile* profile =
      Profile::FromBrowserContext(web_contents->GetBrowserContext());
  return profile->IsOffTheRecord() && !profile->IsGuestSession();
}

void ChromeLauncherController::CloseWindowedAppsFromRemovedExtension(
    const std::string& app_id) {
  // This function cannot rely on the controller's enumeration functionality
  // since the extension has already be unloaded.
  const BrowserList* ash_browser_list =
      BrowserList::GetInstance(chrome::HOST_DESKTOP_TYPE_ASH);
  std::vector<Browser*> browser_to_close;
  for (BrowserList::const_reverse_iterator
           it = ash_browser_list->begin_last_active();
       it != ash_browser_list->end_last_active(); ++it) {
    Browser* browser = *it;
    if (!browser->is_type_tabbed() &&
        browser->is_type_popup() &&
        browser->is_app() &&
        app_id == web_app::GetExtensionIdFromApplicationName(
            browser->app_name())) {
      browser_to_close.push_back(browser);
    }
  }
  while (!browser_to_close.empty()) {
    TabStripModel* tab_strip = browser_to_close.back()->tab_strip_model();
    tab_strip->CloseWebContentsAt(0, TabStripModel::CLOSE_NONE);
    browser_to_close.pop_back();
  }
}

void ChromeLauncherController::SetLauncherItemDelegate(
    ash::LauncherID id,
    ash::LauncherItemDelegate* item_delegate) {
  DCHECK_GT(id, 0);
  DCHECK(item_delegate);
  DCHECK(item_delegate_manager_);
  item_delegate_manager_->SetLauncherItemDelegate(id,
      scoped_ptr<ash::LauncherItemDelegate>(item_delegate).Pass());
}

void ChromeLauncherController::AttachProfile(Profile* profile) {
  profile_ = profile;
  // Either add the profile to the list of known profiles and make it the active
  // one for some functions of AppTabHelper or create a new one.
  if (!app_tab_helper_.get())
    app_tab_helper_.reset(new LauncherAppTabHelper(profile_));
  else
    app_tab_helper_->SetCurrentUser(profile_);
  // TODO(skuhne): The AppIconLoaderImpl has the same problem. Each loaded
  // image is associated with a profile (it's loader requires the profile).
  // Since icon size changes are possible, the icon could be requested to be
  // reloaded. However - having it not multi profile aware would cause problems
  // if the icon cache gets deleted upon user switch.
  app_icon_loader_.reset(new extensions::AppIconLoaderImpl(
      profile_, extension_misc::EXTENSION_ICON_SMALL, this));

  pref_change_registrar_.Init(profile_->GetPrefs());
  pref_change_registrar_.Add(
      prefs::kPinnedLauncherApps,
      base::Bind(&ChromeLauncherController::UpdateAppLaunchersFromPref,
                 base::Unretained(this)));
  pref_change_registrar_.Add(
      prefs::kShelfAlignmentLocal,
      base::Bind(&ChromeLauncherController::SetShelfAlignmentFromPrefs,
                 base::Unretained(this)));
  pref_change_registrar_.Add(
      prefs::kShelfAutoHideBehaviorLocal,
      base::Bind(&ChromeLauncherController::
                     SetShelfAutoHideBehaviorFromPrefs,
                 base::Unretained(this)));
  pref_change_registrar_.Add(
      prefs::kShelfPreferences,
      base::Bind(&ChromeLauncherController::SetShelfBehaviorsFromPrefs,
                 base::Unretained(this)));
}

void ChromeLauncherController::ReleaseProfile() {
  if (app_sync_ui_state_)
    app_sync_ui_state_->RemoveObserver(this);

  PrefServiceSyncable::FromProfile(profile_)->RemoveObserver(this);

  pref_change_registrar_.RemoveAll();
}
