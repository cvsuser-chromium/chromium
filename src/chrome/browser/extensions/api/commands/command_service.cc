// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/commands/command_service.h"

#include "base/lazy_instance.h"
#include "base/prefs/scoped_user_pref_update.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/extensions/api/commands/commands.h"
#include "chrome/browser/extensions/extension_commands_global_registry.h"
#include "chrome/browser/extensions/extension_function_registry.h"
#include "chrome/browser/extensions/extension_keybinding_registry.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/accelerator_utils.h"
#include "chrome/common/extensions/api/commands/commands_handler.h"
#include "chrome/common/pref_names.h"
#include "components/user_prefs/pref_registry_syncable.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_service.h"
#include "extensions/common/feature_switch.h"

using extensions::Extension;
using extensions::ExtensionPrefs;

namespace {

const char kExtension[] = "extension";
const char kCommandName[] = "command_name";
const char kGlobal[] = "global";

// A preference that indicates that the initial keybindings for the given
// extension have been set.
const char kInitialBindingsHaveBeenAssigned[] = "initial_keybindings_set";

std::string GetPlatformKeybindingKeyForAccelerator(
    const ui::Accelerator& accelerator) {
  return extensions::Command::CommandPlatform() + ":" +
         extensions::Command::AcceleratorToString(accelerator);
}

void SetInitialBindingsHaveBeenAssigned(
    ExtensionPrefs* prefs, const std::string& extension_id) {
  prefs->UpdateExtensionPref(extension_id, kInitialBindingsHaveBeenAssigned,
                             new base::FundamentalValue(true));
}

bool InitialBindingsHaveBeenAssigned(
    const ExtensionPrefs* prefs, const std::string& extension_id) {
  bool assigned = false;
  if (!prefs || !prefs->ReadPrefAsBoolean(extension_id,
                                          kInitialBindingsHaveBeenAssigned,
                                          &assigned))
    return false;

  return assigned;
}

bool IsWhitelistedGlobalShortcut(const extensions::Command& command) {
  if (!command.global())
    return true;
  if (!command.accelerator().IsCtrlDown())
    return false;
  if (!command.accelerator().IsShiftDown())
    return false;
  return (command.accelerator().key_code() >= ui::VKEY_0 &&
          command.accelerator().key_code() <= ui::VKEY_9);
}

}  // namespace

namespace extensions {

// static
void CommandService::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterDictionaryPref(
      prefs::kExtensionCommands,
      user_prefs::PrefRegistrySyncable::SYNCABLE_PREF);
}

CommandService::CommandService(Profile* profile)
    : profile_(profile) {
  ExtensionFunctionRegistry::GetInstance()->
      RegisterFunction<GetAllCommandsFunction>();

  registrar_.Add(this, chrome::NOTIFICATION_EXTENSION_INSTALLED,
      content::Source<Profile>(profile));
  registrar_.Add(this, chrome::NOTIFICATION_EXTENSION_UNINSTALLED,
      content::Source<Profile>(profile));
}

CommandService::~CommandService() {
}

static base::LazyInstance<ProfileKeyedAPIFactory<CommandService> >
g_factory = LAZY_INSTANCE_INITIALIZER;

// static
ProfileKeyedAPIFactory<CommandService>* CommandService::GetFactoryInstance() {
  return &g_factory.Get();
}

// static
CommandService* CommandService::Get(Profile* profile) {
  return ProfileKeyedAPIFactory<CommandService>::GetForProfile(profile);
}

bool CommandService::GetBrowserActionCommand(
    const std::string& extension_id,
    QueryType type,
    extensions::Command* command,
    bool* active) {
  return GetExtensionActionCommand(
      extension_id, type, command, active, BROWSER_ACTION);
}

bool CommandService::GetPageActionCommand(
    const std::string& extension_id,
    QueryType type,
    extensions::Command* command,
    bool* active) {
  return GetExtensionActionCommand(
      extension_id, type, command, active, PAGE_ACTION);
}

bool CommandService::GetScriptBadgeCommand(
    const std::string& extension_id,
    QueryType type,
    extensions::Command* command,
    bool* active) {
  return GetExtensionActionCommand(
      extension_id, type, command, active, SCRIPT_BADGE);
}

bool CommandService::GetNamedCommands(const std::string& extension_id,
                                      QueryType type,
                                      CommandScope scope,
                                      extensions::CommandMap* command_map) {
  ExtensionService* extension_service =
      ExtensionSystem::Get(profile_)->extension_service();
  if (!extension_service)
    return false;  // Can occur during testing.
  const ExtensionSet* extensions = extension_service->extensions();
  const Extension* extension = extensions->GetByID(extension_id);
  CHECK(extension);

  command_map->clear();
  const extensions::CommandMap* commands =
      CommandsInfo::GetNamedCommands(extension);
  if (!commands)
    return false;

  extensions::CommandMap::const_iterator iter = commands->begin();
  for (; iter != commands->end(); ++iter) {
    // Look up to see if the user has overridden how the command should work.
    extensions::Command saved_command =
        FindCommandByName(extension_id, iter->second.command_name());
    ui::Accelerator shortcut_assigned = saved_command.accelerator();

    if (type == ACTIVE_ONLY && shortcut_assigned.key_code() == ui::VKEY_UNKNOWN)
      continue;

    extensions::Command command = iter->second;
    if (scope != ANY_SCOPE && ((scope == GLOBAL) != saved_command.global()))
      continue;

    if (shortcut_assigned.key_code() != ui::VKEY_UNKNOWN)
      command.set_accelerator(shortcut_assigned);
    command.set_global(saved_command.global());

    (*command_map)[iter->second.command_name()] = command;
  }

  return true;
}

bool CommandService::AddKeybindingPref(
    const ui::Accelerator& accelerator,
    std::string extension_id,
    std::string command_name,
    bool allow_overrides,
    bool global) {
  if (accelerator.key_code() == ui::VKEY_UNKNOWN)
    return false;

  DictionaryPrefUpdate updater(profile_->GetPrefs(),
                               prefs::kExtensionCommands);
  base::DictionaryValue* bindings = updater.Get();

  std::string key = GetPlatformKeybindingKeyForAccelerator(accelerator);

  if (!allow_overrides && bindings->HasKey(key))
    return false;  // Already taken.

  base::DictionaryValue* keybinding = new base::DictionaryValue();
  keybinding->SetString(kExtension, extension_id);
  keybinding->SetString(kCommandName, command_name);
  keybinding->SetBoolean(kGlobal, global);

  bindings->Set(key, keybinding);

  std::pair<const std::string, const std::string> details =
      std::make_pair(extension_id, command_name);
  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_EXTENSION_COMMAND_ADDED,
      content::Source<Profile>(profile_),
      content::Details<
          std::pair<const std::string, const std::string> >(&details));

  return true;
}

void CommandService::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  switch (type) {
    case chrome::NOTIFICATION_EXTENSION_INSTALLED:
      AssignInitialKeybindings(
          content::Details<const InstalledExtensionInfo>(details)->extension);
      break;
    case chrome::NOTIFICATION_EXTENSION_UNINSTALLED:
      RemoveKeybindingPrefs(
          content::Details<const Extension>(details)->id(),
          std::string());
      break;
    default:
      NOTREACHED();
      break;
  }
}

void CommandService::UpdateKeybindingPrefs(const std::string& extension_id,
                                           const std::string& command_name,
                                           const std::string& keystroke) {
  extensions::Command command = FindCommandByName(extension_id, command_name);

  // The extension command might be assigned another shortcut. Remove that
  // shortcut before proceeding.
  RemoveKeybindingPrefs(extension_id, command_name);

  ui::Accelerator accelerator =
      Command::StringToAccelerator(keystroke, command_name);
  AddKeybindingPref(accelerator, extension_id, command_name,
                    true, command.global());
}

bool CommandService::SetScope(const std::string& extension_id,
                              const std::string& command_name,
                              bool global) {
  extensions::Command command = FindCommandByName(extension_id, command_name);
  if (global == command.global())
    return false;

  // Pre-existing shortcuts must be removed before proceeding because the
  // handlers for global and non-global extensions are not one and the same.
  RemoveKeybindingPrefs(extension_id, command_name);
  AddKeybindingPref(command.accelerator(), extension_id,
                    command_name, true, global);
  return true;
}

Command CommandService::FindCommandByName(
    const std::string& extension_id, const std::string& command) {
  const base::DictionaryValue* bindings =
      profile_->GetPrefs()->GetDictionary(prefs::kExtensionCommands);
  for (base::DictionaryValue::Iterator it(*bindings); !it.IsAtEnd();
       it.Advance()) {
    const base::DictionaryValue* item = NULL;
    it.value().GetAsDictionary(&item);

    std::string extension;
    item->GetString(kExtension, &extension);
    if (extension != extension_id)
      continue;
    std::string command_name;
    item->GetString(kCommandName, &command_name);
    if (command != command_name)
      continue;
    bool global = false;
    if (FeatureSwitch::global_commands()->IsEnabled())
      item->GetBoolean(kGlobal, &global);

    std::string shortcut = it.key();
    if (StartsWithASCII(shortcut, Command::CommandPlatform() + ":", true))
      shortcut = shortcut.substr(Command::CommandPlatform().length() + 1);

    return Command(command_name, string16(), shortcut, global);
  }

  return Command();
}

void CommandService::AssignInitialKeybindings(const Extension* extension) {
  const extensions::CommandMap* commands =
      CommandsInfo::GetNamedCommands(extension);
  if (!commands)
    return;

  ExtensionService* extension_service =
      ExtensionSystem::Get(profile_)->extension_service();
  ExtensionPrefs* extension_prefs = extension_service->extension_prefs();
  if (InitialBindingsHaveBeenAssigned(extension_prefs, extension->id()))
    return;
  SetInitialBindingsHaveBeenAssigned(extension_prefs, extension->id());

  extensions::CommandMap::const_iterator iter = commands->begin();
  for (; iter != commands->end(); ++iter) {
    if (!chrome::IsChromeAccelerator(
            iter->second.accelerator(), profile_) &&
        IsWhitelistedGlobalShortcut(iter->second)) {
      AddKeybindingPref(iter->second.accelerator(),
                        extension->id(),
                        iter->second.command_name(),
                        false,  // Overwriting not allowed.
                        iter->second.global());
    }
  }

  const extensions::Command* browser_action_command =
      CommandsInfo::GetBrowserActionCommand(extension);
  if (browser_action_command) {
    if (!chrome::IsChromeAccelerator(
        browser_action_command->accelerator(), profile_)) {
      AddKeybindingPref(browser_action_command->accelerator(),
                        extension->id(),
                        browser_action_command->command_name(),
                        false,   // Overwriting not allowed.
                        false);  // Browser actions can't be global.
    }
  }

  const extensions::Command* page_action_command =
      CommandsInfo::GetPageActionCommand(extension);
  if (page_action_command) {
    if (!chrome::IsChromeAccelerator(
        page_action_command->accelerator(), profile_)) {
      AddKeybindingPref(page_action_command->accelerator(),
                        extension->id(),
                        page_action_command->command_name(),
                        false,   // Overwriting not allowed.
                        false);  // Page actions can't be global.
    }
  }

  const extensions::Command* script_badge_command =
      CommandsInfo::GetScriptBadgeCommand(extension);
  if (script_badge_command) {
    if (!chrome::IsChromeAccelerator(
        script_badge_command->accelerator(), profile_)) {
      AddKeybindingPref(script_badge_command->accelerator(),
                        extension->id(),
                        script_badge_command->command_name(),
                        false,   // Overwriting not allowed.
                        false);  // Script badges can't be global.
    }
  }
}

void CommandService::RemoveKeybindingPrefs(const std::string& extension_id,
                                           const std::string& command_name) {
  DictionaryPrefUpdate updater(profile_->GetPrefs(),
                               prefs::kExtensionCommands);
  base::DictionaryValue* bindings = updater.Get();

  typedef std::vector<std::string> KeysToRemove;
  KeysToRemove keys_to_remove;
  for (base::DictionaryValue::Iterator it(*bindings); !it.IsAtEnd();
       it.Advance()) {
    const base::DictionaryValue* item = NULL;
    it.value().GetAsDictionary(&item);

    std::string extension;
    item->GetString(kExtension, &extension);

    if (extension == extension_id) {
      // If |command_name| is specified, delete only that command. Otherwise,
      // delete all commands.
      if (!command_name.empty()) {
        std::string command;
        item->GetString(kCommandName, &command);
        if (command_name != command)
          continue;
      }

      keys_to_remove.push_back(it.key());
    }
  }

  for (KeysToRemove::const_iterator it = keys_to_remove.begin();
       it != keys_to_remove.end(); ++it) {
    std::string key = *it;
    bindings->Remove(key, NULL);

    std::pair<const std::string, const std::string> details =
        std::make_pair(extension_id, command_name);
    content::NotificationService::current()->Notify(
        chrome::NOTIFICATION_EXTENSION_COMMAND_REMOVED,
        content::Source<Profile>(profile_),
        content::Details<
            std::pair<const std::string, const std::string> >(&details));
  }
}

bool CommandService::GetExtensionActionCommand(
    const std::string& extension_id,
    QueryType query_type,
    extensions::Command* command,
    bool* active,
    ExtensionActionType action_type) {
  ExtensionService* service =
      ExtensionSystem::Get(profile_)->extension_service();
  if (!service)
    return false;  // Can happen in tests.
  const ExtensionSet* extensions = service->extensions();
  const Extension* extension = extensions->GetByID(extension_id);
  CHECK(extension);

  if (active)
    *active = false;

  const extensions::Command* requested_command = NULL;
  switch (action_type) {
    case BROWSER_ACTION:
      requested_command = CommandsInfo::GetBrowserActionCommand(extension);
      break;
    case PAGE_ACTION:
      requested_command = CommandsInfo::GetPageActionCommand(extension);
      break;
    case SCRIPT_BADGE:
      requested_command = CommandsInfo::GetScriptBadgeCommand(extension);
      break;
  }
  if (!requested_command)
    return false;

  // Look up to see if the user has overridden how the command should work.
  extensions::Command saved_command =
      FindCommandByName(extension_id, requested_command->command_name());
  ui::Accelerator shortcut_assigned = saved_command.accelerator();

  if (active)
    *active = (shortcut_assigned.key_code() != ui::VKEY_UNKNOWN);

  if (query_type == ACTIVE_ONLY &&
      shortcut_assigned.key_code() == ui::VKEY_UNKNOWN)
    return false;

  *command = *requested_command;
  if (shortcut_assigned.key_code() != ui::VKEY_UNKNOWN)
    command->set_accelerator(shortcut_assigned);

  return true;
}

template <>
void ProfileKeyedAPIFactory<CommandService>::DeclareFactoryDependencies() {
  DependsOn(ExtensionCommandsGlobalRegistry::GetFactoryInstance());
}

}  // namespace extensions
