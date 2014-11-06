// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extension_sync_service.h"

#include <iterator>

#include "base/basictypes.h"
#include "base/threading/sequenced_worker_pool.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/extensions/app_sync_data.h"
#include "chrome/browser/extensions/extension_error_ui.h"
#include "chrome/browser/extensions/extension_prefs.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_sorting.h"
#include "chrome/browser/extensions/extension_sync_data.h"
#include "chrome/browser/extensions/extension_sync_service_factory.h"
#include "chrome/browser/extensions/extension_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sync/glue/sync_start_util.h"
#include "chrome/browser/sync/sync_prefs.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/sync_helper.h"
#include "content/public/browser/browser_thread.h"
#include "extensions/common/feature_switch.h"
#include "extensions/common/manifest_constants.h"
#include "sync/api/sync_change.h"
#include "sync/api/sync_error_factory.h"

using extensions::Extension;
using extensions::ExtensionPrefs;
using extensions::FeatureSwitch;

ExtensionSyncService::ExtensionSyncService(Profile* profile,
                                           ExtensionPrefs* extension_prefs,
                                           ExtensionService* extension_service)
    : profile_(profile),
      extension_prefs_(extension_prefs),
      extension_service_(extension_service),
      app_sync_bundle_(this),
      extension_sync_bundle_(this),
      pending_app_enables_(
          make_scoped_ptr(new browser_sync::SyncPrefs(
              extension_prefs_->pref_service())),
          &app_sync_bundle_,
          syncer::APPS),
      pending_extension_enables_(
          make_scoped_ptr(new browser_sync::SyncPrefs(
              extension_prefs_->pref_service())),
          &extension_sync_bundle_,
          syncer::EXTENSIONS) {
  SetSyncStartFlare(sync_start_util::GetFlareForSyncableService(
      profile_->GetPath()));

  extension_service_->set_extension_sync_service(this);
  extension_prefs_->extension_sorting()->SetExtensionSyncService(this);
}

ExtensionSyncService::~ExtensionSyncService() {}

// static
ExtensionSyncService* ExtensionSyncService::Get(Profile* profile) {
  return ExtensionSyncServiceFactory::GetForProfile(profile);
}

syncer::SyncChange ExtensionSyncService::PrepareToSyncUninstallExtension(
    const extensions::Extension* extension, bool extensions_ready) {
  // Extract the data we need for sync now, but don't actually sync until we've
  // completed the uninstallation.
  // TODO(tim): If we get here and IsSyncing is false, this will cause
  // "back from the dead" style bugs, because sync will add-back the extension
  // that was uninstalled here when MergeDataAndStartSyncing is called.
  // See crbug.com/256795.
  if (extensions::sync_helper::IsSyncableApp(extension)) {
    if (app_sync_bundle_.IsSyncing())
      return app_sync_bundle_.CreateSyncChangeToDelete(extension);
    else if (extensions_ready && !flare_.is_null())
      flare_.Run(syncer::APPS);  // Tell sync to start ASAP.
  } else if (extensions::sync_helper::IsSyncableExtension(extension)) {
    if (extension_sync_bundle_.IsSyncing())
      return extension_sync_bundle_.CreateSyncChangeToDelete(extension);
    else if (extensions_ready && !flare_.is_null())
      flare_.Run(syncer::EXTENSIONS);  // Tell sync to start ASAP.
  }

  return syncer::SyncChange();
}

void ExtensionSyncService::ProcessSyncUninstallExtension(
    const std::string& extension_id,
    const syncer::SyncChange& sync_change) {
  if (app_sync_bundle_.HasExtensionId(extension_id) &&
      sync_change.sync_data().GetDataType() == syncer::APPS) {
    app_sync_bundle_.ProcessDeletion(extension_id, sync_change);
  } else if (extension_sync_bundle_.HasExtensionId(extension_id) &&
             sync_change.sync_data().GetDataType() == syncer::EXTENSIONS) {
    extension_sync_bundle_.ProcessDeletion(extension_id, sync_change);
  }
}

void ExtensionSyncService::SyncEnableExtension(
    const extensions::Extension& extension) {

  // Syncing may not have started yet, so handle pending enables.
  if (extensions::sync_helper::IsSyncableApp(&extension))
    pending_app_enables_.OnExtensionEnabled(extension.id());

  if (extensions::sync_helper::IsSyncableExtension(&extension))
    pending_extension_enables_.OnExtensionEnabled(extension.id());

  SyncExtensionChangeIfNeeded(extension);
}

void ExtensionSyncService::SyncDisableExtension(
    const extensions::Extension& extension) {

  // Syncing may not have started yet, so handle pending enables.
  if (extensions::sync_helper::IsSyncableApp(&extension))
    pending_app_enables_.OnExtensionDisabled(extension.id());

  if (extensions::sync_helper::IsSyncableExtension(&extension))
    pending_extension_enables_.OnExtensionDisabled(extension.id());

  SyncExtensionChangeIfNeeded(extension);
}

syncer::SyncMergeResult ExtensionSyncService::MergeDataAndStartSyncing(
    syncer::ModelType type,
    const syncer::SyncDataList& initial_sync_data,
    scoped_ptr<syncer::SyncChangeProcessor> sync_processor,
    scoped_ptr<syncer::SyncErrorFactory> sync_error_factory) {
  CHECK(sync_processor.get());
  CHECK(sync_error_factory.get());

  switch (type) {
    case syncer::EXTENSIONS:
      extension_sync_bundle_.SetupSync(sync_processor.release(),
                                       sync_error_factory.release(),
                                       initial_sync_data);
      pending_extension_enables_.OnSyncStarted(extension_service_);
      break;

    case syncer::APPS:
      app_sync_bundle_.SetupSync(sync_processor.release(),
                                 sync_error_factory.release(),
                                 initial_sync_data);
      pending_app_enables_.OnSyncStarted(extension_service_);
      break;

    default:
      LOG(FATAL) << "Got " << type << " ModelType";
  }

  // Process local extensions.
  // TODO(yoz): Determine whether pending extensions should be considered too.
  //            See crbug.com/104399.
  syncer::SyncDataList sync_data_list = GetAllSyncData(type);
  syncer::SyncChangeList sync_change_list;
  for (syncer::SyncDataList::const_iterator i = sync_data_list.begin();
       i != sync_data_list.end();
       ++i) {
    switch (type) {
        case syncer::EXTENSIONS:
          sync_change_list.push_back(
              extension_sync_bundle_.CreateSyncChange(*i));
          break;
        case syncer::APPS:
          sync_change_list.push_back(app_sync_bundle_.CreateSyncChange(*i));
          break;
      default:
        LOG(FATAL) << "Got " << type << " ModelType";
    }
  }


  if (type == syncer::EXTENSIONS) {
    extension_sync_bundle_.ProcessSyncChangeList(sync_change_list);
  } else if (type == syncer::APPS) {
    app_sync_bundle_.ProcessSyncChangeList(sync_change_list);
  }

  return syncer::SyncMergeResult(type);
}

void ExtensionSyncService::StopSyncing(syncer::ModelType type) {
  if (type == syncer::APPS) {
    app_sync_bundle_.Reset();
  } else if (type == syncer::EXTENSIONS) {
    extension_sync_bundle_.Reset();
  }
}

syncer::SyncDataList ExtensionSyncService::GetAllSyncData(
    syncer::ModelType type) const {
  if (type == syncer::EXTENSIONS)
    return extension_sync_bundle_.GetAllSyncData();
  if (type == syncer::APPS)
    return app_sync_bundle_.GetAllSyncData();

  // We should only get sync data for extensions and apps.
  NOTREACHED();

  return syncer::SyncDataList();
}

syncer::SyncError ExtensionSyncService::ProcessSyncChanges(
    const tracked_objects::Location& from_here,
    const syncer::SyncChangeList& change_list) {
  for (syncer::SyncChangeList::const_iterator i = change_list.begin();
      i != change_list.end();
      ++i) {
    syncer::ModelType type = i->sync_data().GetDataType();
    if (type == syncer::EXTENSIONS) {
      extension_sync_bundle_.ProcessSyncChange(
          extensions::ExtensionSyncData(*i));
    } else if (type == syncer::APPS) {
      app_sync_bundle_.ProcessSyncChange(extensions::AppSyncData(*i));
    }
  }

  extension_prefs_->extension_sorting()->FixNTPOrdinalCollisions();

  return syncer::SyncError();
}

extensions::ExtensionSyncData ExtensionSyncService::GetExtensionSyncData(
    const Extension& extension) const {
  return extensions::ExtensionSyncData(
      extension,
      extension_service_->IsExtensionEnabled(extension.id()),
      extension_util::IsIncognitoEnabled(extension.id(), extension_service_));
}

extensions::AppSyncData ExtensionSyncService::GetAppSyncData(
    const Extension& extension) const {
  return extensions::AppSyncData(
      extension,
      extension_service_->IsExtensionEnabled(extension.id()),
      extension_util::IsIncognitoEnabled(extension.id(), extension_service_),
      extension_prefs_->extension_sorting()->GetAppLaunchOrdinal(
          extension.id()),
      extension_prefs_->extension_sorting()->GetPageOrdinal(extension.id()));
}

std::vector<extensions::ExtensionSyncData>
  ExtensionSyncService::GetExtensionSyncDataList() const {
  std::vector<extensions::ExtensionSyncData> extension_sync_list;
  extension_sync_bundle_.GetExtensionSyncDataListHelper(
      extension_service_->extensions(), &extension_sync_list);
  extension_sync_bundle_.GetExtensionSyncDataListHelper(
      extension_service_->disabled_extensions(), &extension_sync_list);
  extension_sync_bundle_.GetExtensionSyncDataListHelper(
      extension_service_->terminated_extensions(), &extension_sync_list);

  std::vector<extensions::ExtensionSyncData> pending_extensions =
      extension_sync_bundle_.GetPendingData();
  extension_sync_list.insert(extension_sync_list.begin(),
                             pending_extensions.begin(),
                             pending_extensions.end());

  return extension_sync_list;
}

std::vector<extensions::AppSyncData> ExtensionSyncService::GetAppSyncDataList()
    const {
  std::vector<extensions::AppSyncData> app_sync_list;
  app_sync_bundle_.GetAppSyncDataListHelper(
      extension_service_->extensions(), &app_sync_list);
  app_sync_bundle_.GetAppSyncDataListHelper(
      extension_service_->disabled_extensions(), &app_sync_list);
  app_sync_bundle_.GetAppSyncDataListHelper(
      extension_service_->terminated_extensions(), &app_sync_list);

  std::vector<extensions::AppSyncData> pending_apps =
      app_sync_bundle_.GetPendingData();
  app_sync_list.insert(app_sync_list.begin(),
                       pending_apps.begin(),
                       pending_apps.end());

  return app_sync_list;
}

bool ExtensionSyncService::ProcessExtensionSyncData(
    const extensions::ExtensionSyncData& extension_sync_data) {
  if (!ProcessExtensionSyncDataHelper(extension_sync_data,
                                      syncer::EXTENSIONS)) {
    extension_sync_bundle_.AddPendingExtension(extension_sync_data.id(),
                                               extension_sync_data);
    extension_service_->CheckForUpdatesSoon();
    return false;
  }

  return true;
}

bool ExtensionSyncService::ProcessAppSyncData(
    const extensions::AppSyncData& app_sync_data) {
  const std::string& id = app_sync_data.id();

  if (app_sync_data.app_launch_ordinal().IsValid() &&
      app_sync_data.page_ordinal().IsValid()) {
    extension_prefs_->extension_sorting()->SetAppLaunchOrdinal(
        id,
        app_sync_data.app_launch_ordinal());
    extension_prefs_->extension_sorting()->SetPageOrdinal(
        id,
        app_sync_data.page_ordinal());
  }

  if (!ProcessExtensionSyncDataHelper(app_sync_data.extension_sync_data(),
                                      syncer::APPS)) {
    app_sync_bundle_.AddPendingApp(id, app_sync_data);
    extension_service_->CheckForUpdatesSoon();
    return false;
  }

  return true;
}

void ExtensionSyncService::SyncOrderingChange(const std::string& extension_id) {
  const extensions::Extension* ext = extension_service_->GetInstalledExtension(
      extension_id);

  if (ext)
    SyncExtensionChangeIfNeeded(*ext);
}

void ExtensionSyncService::SetSyncStartFlare(
    const syncer::SyncableService::StartSyncFlare& flare) {
  flare_ = flare;
}

bool ExtensionSyncService::IsCorrectSyncType(const Extension& extension,
                                         syncer::ModelType type) const {
  if (type == syncer::EXTENSIONS &&
      extensions::sync_helper::IsSyncableExtension(&extension)) {
    return true;
  }

  if (type == syncer::APPS &&
      extensions::sync_helper::IsSyncableApp(&extension)) {
    return true;
  }

  return false;
}

bool ExtensionSyncService::IsPendingEnable(
    const std::string& extension_id) const {
  return pending_app_enables_.Contains(extension_id) ||
      pending_extension_enables_.Contains(extension_id);
}

bool ExtensionSyncService::ProcessExtensionSyncDataHelper(
    const extensions::ExtensionSyncData& extension_sync_data,
    syncer::ModelType type) {
  const std::string& id = extension_sync_data.id();
  const Extension* extension = extension_service_->GetInstalledExtension(id);

  // TODO(bolms): we should really handle this better.  The particularly bad
  // case is where an app becomes an extension or vice versa, and we end up with
  // a zombie extension that won't go away.
  if (extension && !IsCorrectSyncType(*extension, type))
    return true;

  // Handle uninstalls first.
  if (extension_sync_data.uninstalled()) {
    if (!extension_service_->UninstallExtensionHelper(extension_service_, id)) {
      LOG(WARNING) << "Could not uninstall extension " << id
                   << " for sync";
    }
    return true;
  }

  // Extension from sync was uninstalled by the user as external extensions.
  // Honor user choice and skip installation/enabling.
  if (extension_service_->IsExternalExtensionUninstalled(id)) {
    LOG(WARNING) << "Extension with id " << id
                 << " from sync was uninstalled as external extension";
    return true;
  }

  // Set user settings.
  // If the extension has been disabled from sync, it may not have
  // been installed yet, so we don't know if the disable reason was a
  // permissions increase.  That will be updated once CheckPermissionsIncrease
  // is called for it.
  if (extension_sync_data.enabled())
    extension_service_->EnableExtension(id);
  else if (!IsPendingEnable(id))
    extension_service_->DisableExtension(
        id, Extension::DISABLE_UNKNOWN_FROM_SYNC);

  // We need to cache some version information here because setting the
  // incognito flag invalidates the |extension| pointer (it reloads the
  // extension).
  bool extension_installed = (extension != NULL);
  int result = extension ?
      extension->version()->CompareTo(extension_sync_data.version()) : 0;
  extension_util::SetIsIncognitoEnabled(
      id, extension_service_, extension_sync_data.incognito_enabled());
  extension = NULL;  // No longer safe to use.

  if (extension_installed) {
    // If the extension is already installed, check if it's outdated.
    if (result < 0) {
      // Extension is outdated.
      return false;
    }
  } else {
    // TODO(akalin): Replace silent update with a list of enabled
    // permissions.
    const bool kInstallSilently = true;

    CHECK(type == syncer::EXTENSIONS || type == syncer::APPS);
    extensions::PendingExtensionInfo::ShouldAllowInstallPredicate filter =
        (type == syncer::APPS) ? extensions::sync_helper::IsSyncableApp :
                                 extensions::sync_helper::IsSyncableExtension;

    if (!extension_service_->pending_extension_manager()->AddFromSync(
            id,
            extension_sync_data.update_url(),
            filter,
            kInstallSilently)) {
      LOG(WARNING) << "Could not add pending extension for " << id;
      // This means that the extension is already pending installation, with a
      // non-INTERNAL location.  Add to pending_sync_data, even though it will
      // never be removed (we'll never install a syncable version of the
      // extension), so that GetAllSyncData() continues to send it.
    }
    // Track pending extensions so that we can return them in GetAllSyncData().
    return false;
  }

  return true;
}

void ExtensionSyncService::SyncExtensionChangeIfNeeded(
    const Extension& extension) {
  if (extensions::sync_helper::IsSyncableApp(&extension)) {
    if (app_sync_bundle_.IsSyncing())
      app_sync_bundle_.SyncChangeIfNeeded(extension);
    else if (extension_service_->is_ready() && !flare_.is_null())
      flare_.Run(syncer::APPS);
  } else if (extensions::sync_helper::IsSyncableExtension(&extension)) {
    if (extension_sync_bundle_.IsSyncing())
      extension_sync_bundle_.SyncChangeIfNeeded(extension);
    else if (extension_service_->is_ready() && !flare_.is_null())
      flare_.Run(syncer::EXTENSIONS);
  }
}
