// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/policy/device_local_account_policy_service.h"

#include <vector>

#include "base/bind.h"
#include "base/file_util.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_path.h"
#include "base/logging.h"
#include "base/message_loop/message_loop.h"
#include "base/message_loop/message_loop_proxy.h"
#include "base/path_service.h"
#include "base/sequenced_task_runner.h"
#include "base/strings/string_number_conversions.h"
#include "chrome/browser/chromeos/policy/device_local_account.h"
#include "chrome/browser/chromeos/policy/device_local_account_external_data_service.h"
#include "chrome/browser/chromeos/policy/device_local_account_policy_store.h"
#include "chrome/browser/chromeos/settings/device_settings_service.h"
#include "chrome/browser/policy/cloud/cloud_policy_client.h"
#include "chrome/browser/policy/cloud/cloud_policy_constants.h"
#include "chrome/browser/policy/cloud/cloud_policy_refresh_scheduler.h"
#include "chrome/browser/policy/cloud/device_management_service.h"
#include "chrome/browser/policy/proto/cloud/device_management_backend.pb.h"
#include "chromeos/chromeos_paths.h"
#include "chromeos/dbus/session_manager_client.h"
#include "chromeos/settings/cros_settings_names.h"
#include "chromeos/settings/cros_settings_provider.h"
#include "net/url_request/url_request_context_getter.h"
#include "policy/policy_constants.h"

namespace em = enterprise_management;

namespace policy {

namespace {

// Creates and initializes a cloud policy client. Returns NULL if the device
// doesn't have credentials in device settings (i.e. is not
// enterprise-enrolled).
scoped_ptr<CloudPolicyClient> CreateClient(
    chromeos::DeviceSettingsService* device_settings_service,
    DeviceManagementService* device_management_service) {
  const em::PolicyData* policy_data = device_settings_service->policy_data();
  if (!policy_data ||
      !policy_data->has_request_token() ||
      !policy_data->has_device_id() ||
      !device_management_service) {
    return scoped_ptr<CloudPolicyClient>();
  }

  scoped_ptr<CloudPolicyClient> client(
      new CloudPolicyClient(std::string(), std::string(),
                            USER_AFFILIATION_MANAGED,
                            NULL, device_management_service));
  client->SetupRegistration(policy_data->request_token(),
                            policy_data->device_id());
  return client.Pass();
}

// Get the subdirectory of the cache directory in which force-installed
// extensions are cached for |account_id|.
std::string GetCacheSubdirectoryForAccountID(const std::string& account_id) {
  return base::HexEncode(account_id.c_str(), account_id.size());
}

// Cleans up the cache directory by removing subdirectories that are not found
// in |subdirectories_to_keep|. Only caches whose cache directory is found in
// |subdirectories_to_keep| may be running while the clean-up is in progress.
void DeleteOrphanedExtensionCaches(
    const std::set<std::string>& subdirectories_to_keep) {
  base::FilePath cache_root_dir;
  CHECK(PathService::Get(chromeos::DIR_DEVICE_LOCAL_ACCOUNT_EXTENSIONS,
                         &cache_root_dir));
  base::FileEnumerator enumerator(cache_root_dir,
                                  false,
                                  base::FileEnumerator::DIRECTORIES);
  for (base::FilePath path = enumerator.Next(); !path.empty();
       path = enumerator.Next()) {
    const std::string subdirectory(path.BaseName().MaybeAsASCII());
    if (subdirectories_to_keep.find(subdirectory) ==
        subdirectories_to_keep.end()) {
      base::DeleteFile(path, true);
    }
  }
}

// Removes the subdirectory belonging to |account_id_to_delete| from the cache
// directory. No cache belonging to |account_id_to_delete| may be running while
// the removal is in progress.
void DeleteObsoleteExtensionCache(const std::string& account_id_to_delete) {
  base::FilePath cache_root_dir;
  CHECK(PathService::Get(chromeos::DIR_DEVICE_LOCAL_ACCOUNT_EXTENSIONS,
                         &cache_root_dir));
  const base::FilePath path = cache_root_dir
      .Append(GetCacheSubdirectoryForAccountID(account_id_to_delete));
  if (base::DirectoryExists(path))
    base::DeleteFile(path, true);
}

}  // namespace

DeviceLocalAccountPolicyBroker::DeviceLocalAccountPolicyBroker(
    const DeviceLocalAccount& account,
    scoped_ptr<DeviceLocalAccountPolicyStore> store,
    scoped_refptr<DeviceLocalAccountExternalDataManager> external_data_manager,
    const scoped_refptr<base::SequencedTaskRunner>& task_runner)
    : account_id_(account.account_id),
      user_id_(account.user_id),
      store_(store.Pass()),
      external_data_manager_(external_data_manager),
      core_(PolicyNamespaceKey(dm_protocol::kChromePublicAccountPolicyType,
                               store_->account_id()),
            store_.get(),
            task_runner) {
  base::FilePath cache_root_dir;
  CHECK(PathService::Get(chromeos::DIR_DEVICE_LOCAL_ACCOUNT_EXTENSIONS,
                         &cache_root_dir));
  extension_loader_ = new chromeos::DeviceLocalAccountExternalPolicyLoader(
      store_.get(),
      cache_root_dir.Append(
          GetCacheSubdirectoryForAccountID(account.account_id)));
}

DeviceLocalAccountPolicyBroker::~DeviceLocalAccountPolicyBroker() {
  external_data_manager_->SetPolicyStore(NULL);
  external_data_manager_->Disconnect();
}

void DeviceLocalAccountPolicyBroker::Initialize() {
  store_->Load();
}

void DeviceLocalAccountPolicyBroker::ConnectIfPossible(
    chromeos::DeviceSettingsService* device_settings_service,
    DeviceManagementService* device_management_service,
    scoped_refptr<net::URLRequestContextGetter> request_context) {
  if (core_.client())
    return;

  scoped_ptr<CloudPolicyClient> client(CreateClient(device_settings_service,
                                                    device_management_service));
  if (!client)
    return;

  core_.Connect(client.Pass());
  external_data_manager_->Connect(request_context);
  core_.StartRefreshScheduler();
  UpdateRefreshDelay();
}

void DeviceLocalAccountPolicyBroker::UpdateRefreshDelay() {
  if (core_.refresh_scheduler()) {
    const Value* policy_value =
        store_->policy_map().GetValue(key::kPolicyRefreshRate);
    int delay = 0;
    if (policy_value && policy_value->GetAsInteger(&delay))
      core_.refresh_scheduler()->SetRefreshDelay(delay);
  }
}

std::string DeviceLocalAccountPolicyBroker::GetDisplayName() const {
  std::string display_name;
  const base::Value* display_name_value =
      store_->policy_map().GetValue(policy::key::kUserDisplayName);
  if (display_name_value)
    display_name_value->GetAsString(&display_name);
  return display_name;
}

DeviceLocalAccountPolicyService::DeviceLocalAccountPolicyService(
    chromeos::SessionManagerClient* session_manager_client,
    chromeos::DeviceSettingsService* device_settings_service,
    chromeos::CrosSettings* cros_settings,
    scoped_refptr<base::SequencedTaskRunner> store_background_task_runner,
    scoped_refptr<base::SequencedTaskRunner> extension_cache_task_runner,
    scoped_refptr<base::SequencedTaskRunner>
        external_data_service_backend_task_runner,
    scoped_refptr<base::SequencedTaskRunner> io_task_runner,
    scoped_refptr<net::URLRequestContextGetter> request_context)
    : session_manager_client_(session_manager_client),
      device_settings_service_(device_settings_service),
      cros_settings_(cros_settings),
      device_management_service_(NULL),
      waiting_for_cros_settings_(false),
      orphan_cache_deletion_state_(NOT_STARTED),
      store_background_task_runner_(store_background_task_runner),
      extension_cache_task_runner_(extension_cache_task_runner),
      request_context_(request_context),
      local_accounts_subscription_(cros_settings_->AddSettingsObserver(
          chromeos::kAccountsPrefDeviceLocalAccounts,
          base::Bind(&DeviceLocalAccountPolicyService::
                         UpdateAccountListIfNonePending,
                     base::Unretained(this)))),
      weak_factory_(this) {
  external_data_service_.reset(new DeviceLocalAccountExternalDataService(
      this,
      external_data_service_backend_task_runner,
      io_task_runner));
  UpdateAccountList();
}

DeviceLocalAccountPolicyService::~DeviceLocalAccountPolicyService() {
  DCHECK(!request_context_);
  DCHECK(policy_brokers_.empty());
}

void DeviceLocalAccountPolicyService::Shutdown() {
  device_management_service_ = NULL;
  request_context_ = NULL;
  DeleteBrokers(&policy_brokers_);
}

void DeviceLocalAccountPolicyService::Connect(
    DeviceManagementService* device_management_service) {
  DCHECK(!device_management_service_);
  device_management_service_ = device_management_service;

  // Connect the brokers.
  for (PolicyBrokerMap::iterator it(policy_brokers_.begin());
       it != policy_brokers_.end(); ++it) {
    it->second->ConnectIfPossible(device_settings_service_,
                                  device_management_service_,
                                  request_context_);
  }
}

DeviceLocalAccountPolicyBroker*
    DeviceLocalAccountPolicyService::GetBrokerForUser(
        const std::string& user_id) {
  PolicyBrokerMap::iterator entry = policy_brokers_.find(user_id);
  if (entry == policy_brokers_.end())
    return NULL;

  return entry->second;
}

bool DeviceLocalAccountPolicyService::IsPolicyAvailableForUser(
    const std::string& user_id) {
  DeviceLocalAccountPolicyBroker* broker = GetBrokerForUser(user_id);
  return broker && broker->core()->store()->is_managed();
}

void DeviceLocalAccountPolicyService::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void DeviceLocalAccountPolicyService::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void DeviceLocalAccountPolicyService::OnStoreLoaded(CloudPolicyStore* store) {
  DeviceLocalAccountPolicyBroker* broker = GetBrokerForStore(store);
  DCHECK(broker);
  if (!broker)
    return;
  broker->UpdateRefreshDelay();
  FOR_EACH_OBSERVER(Observer, observers_, OnPolicyUpdated(broker->user_id()));
}

void DeviceLocalAccountPolicyService::OnStoreError(CloudPolicyStore* store) {
  DeviceLocalAccountPolicyBroker* broker = GetBrokerForStore(store);
  DCHECK(broker);
  if (!broker)
    return;
  FOR_EACH_OBSERVER(Observer, observers_, OnPolicyUpdated(broker->user_id()));
}

bool DeviceLocalAccountPolicyService::IsExtensionCacheDirectoryBusy(
    const std::string& account_id) {
  return busy_extension_cache_directories_.find(account_id) !=
            busy_extension_cache_directories_.end();
}

void DeviceLocalAccountPolicyService::StartExtensionCachesIfPossible() {
  for (PolicyBrokerMap::iterator it = policy_brokers_.begin();
       it != policy_brokers_.end(); ++it) {
    if (!it->second->extension_loader()->IsCacheRunning() &&
        !IsExtensionCacheDirectoryBusy(it->second->account_id())) {
      it->second->extension_loader()->StartCache(extension_cache_task_runner_);
    }
  }
}

bool DeviceLocalAccountPolicyService::StartExtensionCacheForAccountIfPresent(
    const std::string& account_id) {
  for (PolicyBrokerMap::iterator it = policy_brokers_.begin();
       it != policy_brokers_.end(); ++it) {
    if (it->second->account_id() == account_id) {
      DCHECK(!it->second->extension_loader()->IsCacheRunning());
      it->second->extension_loader()->StartCache(extension_cache_task_runner_);
      return true;
    }
  }
  return false;
}

void DeviceLocalAccountPolicyService::OnOrphanedExtensionCachesDeleted() {
  DCHECK_EQ(IN_PROGRESS, orphan_cache_deletion_state_);

  orphan_cache_deletion_state_ = DONE;
  StartExtensionCachesIfPossible();
}

void DeviceLocalAccountPolicyService::OnObsoleteExtensionCacheShutdown(
    const std::string& account_id) {
  DCHECK_NE(NOT_STARTED, orphan_cache_deletion_state_);
  DCHECK(IsExtensionCacheDirectoryBusy(account_id));

  // The account with |account_id| was deleted and the broker for it has shut
  // down completely.

  if (StartExtensionCacheForAccountIfPresent(account_id)) {
    // If another account with the same ID was created in the meantime, its
    // extension cache is started, reusing the cache directory. The directory no
    // longer needs to be marked as busy in this case.
    busy_extension_cache_directories_.erase(account_id);
    return;
  }

  // If no account with |account_id| exists anymore, the cache directory should
  // be removed. The directory must stay marked as busy while the removal is in
  // progress.
  extension_cache_task_runner_->PostTaskAndReply(
      FROM_HERE,
      base::Bind(&DeleteObsoleteExtensionCache, account_id),
      base::Bind(&DeviceLocalAccountPolicyService::
                     OnObsoleteExtensionCacheDeleted,
                 weak_factory_.GetWeakPtr(),
                 account_id));
}

void DeviceLocalAccountPolicyService::OnObsoleteExtensionCacheDeleted(
    const std::string& account_id) {
  DCHECK_EQ(DONE, orphan_cache_deletion_state_);
  DCHECK(IsExtensionCacheDirectoryBusy(account_id));

  // The cache directory for |account_id| has been deleted. The directory no
  // longer needs to be marked as busy.
  busy_extension_cache_directories_.erase(account_id);

  // If another account with the same ID was created in the meantime, start its
  // extension cache, creating a new cache directory.
  StartExtensionCacheForAccountIfPresent(account_id);
}

void DeviceLocalAccountPolicyService::UpdateAccountListIfNonePending() {
  // Avoid unnecessary calls to UpdateAccountList(): If an earlier call is still
  // pending (because the |cros_settings_| are not trusted yet), the updated
  // account list will be processed by that call when it eventually runs.
  if (!waiting_for_cros_settings_)
    UpdateAccountList();
}

void DeviceLocalAccountPolicyService::UpdateAccountList() {
  chromeos::CrosSettingsProvider::TrustedStatus status =
      cros_settings_->PrepareTrustedValues(
          base::Bind(&DeviceLocalAccountPolicyService::UpdateAccountList,
                     weak_factory_.GetWeakPtr()));
  switch (status) {
    case chromeos::CrosSettingsProvider::TRUSTED:
      waiting_for_cros_settings_ = false;
      break;
    case chromeos::CrosSettingsProvider::TEMPORARILY_UNTRUSTED:
      waiting_for_cros_settings_ = true;
      return;
    case chromeos::CrosSettingsProvider::PERMANENTLY_UNTRUSTED:
      waiting_for_cros_settings_ = false;
      return;
  }

  // Update |policy_brokers_|, keeping existing entries.
  PolicyBrokerMap old_policy_brokers;
  policy_brokers_.swap(old_policy_brokers);
  std::set<std::string> subdirectories_to_keep;
  const std::vector<DeviceLocalAccount> device_local_accounts =
      GetDeviceLocalAccounts(cros_settings_);
  for (std::vector<DeviceLocalAccount>::const_iterator it =
           device_local_accounts.begin();
       it != device_local_accounts.end(); ++it) {
    PolicyBrokerMap::iterator broker_it = old_policy_brokers.find(it->user_id);

    scoped_ptr<DeviceLocalAccountPolicyBroker> broker;
    bool broker_initialized = false;
    if (broker_it != old_policy_brokers.end()) {
      // Reuse the existing broker if present.
      broker.reset(broker_it->second);
      old_policy_brokers.erase(broker_it);
      broker_initialized = true;
    } else {
      scoped_ptr<DeviceLocalAccountPolicyStore> store(
          new DeviceLocalAccountPolicyStore(it->account_id,
                                            session_manager_client_,
                                            device_settings_service_,
                                            store_background_task_runner_));
      store->AddObserver(this);
      scoped_refptr<DeviceLocalAccountExternalDataManager>
          external_data_manager =
              external_data_service_->GetExternalDataManager(it->account_id,
                                                             store.get());
      broker.reset(new DeviceLocalAccountPolicyBroker(
          *it,
          store.Pass(),
          external_data_manager,
          base::MessageLoopProxy::current()));
    }

    // Fire up the cloud connection for fetching policy for the account from
    // the cloud if this is an enterprise-managed device.
    broker->ConnectIfPossible(device_settings_service_,
                              device_management_service_,
                              request_context_);

    policy_brokers_[it->user_id] = broker.release();
    if (!broker_initialized) {
      // The broker must be initialized after it has been added to
      // |policy_brokers_|.
      policy_brokers_[it->user_id]->Initialize();
    }

    if (orphan_cache_deletion_state_ == NOT_STARTED) {
      subdirectories_to_keep.insert(
          GetCacheSubdirectoryForAccountID(it->account_id));
    }
  }

  std::set<std::string> obsolete_account_ids;
  for (PolicyBrokerMap::const_iterator it = old_policy_brokers.begin();
       it != old_policy_brokers.end(); ++it) {
    obsolete_account_ids.insert(it->second->account_id());
  }

  if (orphan_cache_deletion_state_ == NOT_STARTED) {
    DCHECK(old_policy_brokers.empty());
    DCHECK(busy_extension_cache_directories_.empty());

    // If this method is running for the first time, no extension caches have
    // been started yet. Take this opportunity to do a clean-up by removing
    // orphaned cache directories not found in |subdirectories_to_keep| from the
    // cache directory.
    orphan_cache_deletion_state_ = IN_PROGRESS;
    extension_cache_task_runner_->PostTaskAndReply(
        FROM_HERE,
        base::Bind(&DeleteOrphanedExtensionCaches, subdirectories_to_keep),
        base::Bind(&DeviceLocalAccountPolicyService::
                       OnOrphanedExtensionCachesDeleted,
                   weak_factory_.GetWeakPtr()));

    // Start the extension caches for all brokers. These belong to accounts in
    // |account_ids| and are not affected by the clean-up.
    StartExtensionCachesIfPossible();
  } else {
    // If this method has run before, obsolete brokers may exist. Shut down
    // their extension caches and delete the brokers.
    DeleteBrokers(&old_policy_brokers);

    if (orphan_cache_deletion_state_ == DONE) {
      // If the initial clean-up of orphaned cache directories has been
      // complete, start any extension caches that are not running yet but can
      // be started now because their cache directories are not busy.
      StartExtensionCachesIfPossible();
    }
  }

  FOR_EACH_OBSERVER(Observer, observers_, OnDeviceLocalAccountsChanged());
}

void DeviceLocalAccountPolicyService::DeleteBrokers(PolicyBrokerMap* map) {
  for (PolicyBrokerMap::iterator it = map->begin(); it != map->end(); ++it) {
    it->second->core()->store()->RemoveObserver(this);
    scoped_refptr<chromeos::DeviceLocalAccountExternalPolicyLoader>
        extension_loader = it->second->extension_loader();
    if (extension_loader->IsCacheRunning()) {
      DCHECK(!IsExtensionCacheDirectoryBusy(it->second->account_id()));
      busy_extension_cache_directories_.insert(it->second->account_id());
      extension_loader->StopCache(base::Bind(
          &DeviceLocalAccountPolicyService::OnObsoleteExtensionCacheShutdown,
          weak_factory_.GetWeakPtr(),
          it->second->account_id()));
    }
    delete it->second;
  }
  map->clear();
}

DeviceLocalAccountPolicyBroker*
    DeviceLocalAccountPolicyService::GetBrokerForStore(
        CloudPolicyStore* store) {
  for (PolicyBrokerMap::iterator it(policy_brokers_.begin());
       it != policy_brokers_.end(); ++it) {
    if (it->second->core()->store() == store)
      return it->second;
  }
  return NULL;
}

}  // namespace policy
