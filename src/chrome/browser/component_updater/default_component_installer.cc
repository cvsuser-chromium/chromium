// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/bind.h"
#include "base/file_util.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_path.h"
#include "base/values.h"
#include "base/version.h"
// TODO(ddorwin): Find a better place for ReadManifest.
#include "chrome/browser/component_updater/component_unpacker.h"
#include "chrome/browser/component_updater/default_component_installer.h"
#include "content/public/browser/browser_thread.h"

namespace {
// Version "0" corresponds to no installed version. By the server's conventions,
// we represent it as a dotted quad.
const char kNullVersion[] = "0.0.0.0";
}  // namespace

ComponentInstallerTraits::~ComponentInstallerTraits() {
}

DefaultComponentInstaller::DefaultComponentInstaller(
    scoped_ptr<ComponentInstallerTraits> installer_traits)
    : current_version_(kNullVersion) {
  installer_traits_ = installer_traits.Pass();
}

DefaultComponentInstaller::~DefaultComponentInstaller() {
}

void DefaultComponentInstaller::Register(ComponentUpdateService* cus) {
  if (!installer_traits_) {
    NOTREACHED() << "A DefaultComponentInstaller has been created but "
                 << "has no installer traits.";
    return;
  }
  content::BrowserThread::PostBlockingPoolTask(
      FROM_HERE,
      base::Bind(&DefaultComponentInstaller::StartRegistration,
                 base::Unretained(this),
                 cus));
}

void DefaultComponentInstaller::OnUpdateError(int error) {
  NOTREACHED() << "Component update error: " << error;
}

bool DefaultComponentInstaller::InstallHelper(
    const base::DictionaryValue& manifest,
    const base::FilePath& unpack_path,
    const base::FilePath& install_path) {
  if (!base::Move(unpack_path, install_path))
    return false;
  if (!installer_traits_->OnCustomInstall(manifest, install_path))
    return false;
  if (!installer_traits_->VerifyInstallation(install_path))
    return false;
  return true;
}

bool DefaultComponentInstaller::Install(const base::DictionaryValue& manifest,
                                        const base::FilePath& unpack_path) {
  std::string manifest_version;
  manifest.GetStringASCII("version", &manifest_version);
  base::Version version(manifest_version.c_str());
  if (!version.IsValid())
    return false;
  if (current_version_.CompareTo(version) > 0)
    return false;
  base::FilePath install_path =
      installer_traits_->GetBaseDirectory().AppendASCII(version.GetString());
  if (base::PathExists(install_path)) {
    if (!base::DeleteFile(install_path, true))
      return false;
  }
  if (!InstallHelper(manifest, unpack_path, install_path)) {
    base::DeleteFile(install_path, true);
    return false;
  }
  current_version_ = version;
  // TODO(ddorwin): Change the parameter to scoped_ptr<base::DictionaryValue>
  // so we can avoid this DeepCopy.
  current_manifest_.reset(manifest.DeepCopy());
  scoped_ptr<base::DictionaryValue> manifest_copy(
      current_manifest_->DeepCopy());
  content::BrowserThread::PostTask(
      content::BrowserThread::UI, FROM_HERE,
      base::Bind(&ComponentInstallerTraits::ComponentReady,
                 base::Unretained(installer_traits_.get()),
                 current_version_,
                 GetInstallDirectory(),
                 base::Passed(&manifest_copy)));
  return true;
}

bool DefaultComponentInstaller::GetInstalledFile(
    const std::string& file,
    base::FilePath* installed_file) {
  if (current_version_.Equals(base::Version(kNullVersion)))
    return false;  // No component has been installed yet.

  *installed_file =
      installer_traits_->GetBaseDirectory()
          .AppendASCII(current_version_.GetString()).AppendASCII(file);
  return true;
}

void DefaultComponentInstaller::StartRegistration(
    ComponentUpdateService* cus) {
  base::FilePath base_dir = installer_traits_->GetBaseDirectory();
  if (!base::PathExists(base_dir) &&
      !file_util::CreateDirectory(base_dir)) {
    NOTREACHED() << "Could not create the base directory for "
                 << installer_traits_->GetName() << " ("
                 << base_dir.MaybeAsASCII() << ").";
    return;
  }

  base::FilePath latest_dir;
  base::Version latest_version(kNullVersion);
  std::vector<base::FilePath> older_dirs;
  bool found = false;
  base::FileEnumerator file_enumerator(
      base_dir, false, base::FileEnumerator::DIRECTORIES);
  for (base::FilePath path = file_enumerator.Next();
       !path.value().empty();
       path = file_enumerator.Next()) {
    base::Version version(path.BaseName().MaybeAsASCII());
    if (!version.IsValid())
      continue;
    if (found) {
      if (version.CompareTo(latest_version) > 0) {
        older_dirs.push_back(latest_dir);
        latest_dir = path;
        latest_version = version;
      } else {
        older_dirs.push_back(path);
      }
    } else {
      latest_dir = path;
      latest_version = version;
      found = true;
    }
  }

  if (found) {
    current_version_ = latest_version;
    // TODO(ddorwin): Remove these members and pass them directly to
    // FinishRegistration().
    base::ReadFileToString(latest_dir.AppendASCII("manifest.fingerprint"),
                           &current_fingerprint_);
    current_manifest_= ReadManifest(latest_dir);
    if (!current_manifest_) {
      DLOG(ERROR) << "Failed to read manifest for "
                  << installer_traits_->GetName() << " ("
                  << base_dir.MaybeAsASCII() << ").";
      return;
    }
  }

  // Remove older versions of the component. None should be in use during
  // browser startup.
  for (std::vector<base::FilePath>::iterator iter = older_dirs.begin();
       iter != older_dirs.end(); ++iter) {
   base::DeleteFile(*iter, true);
  }

  content::BrowserThread::PostTask(
      content::BrowserThread::UI, FROM_HERE,
      base::Bind(&DefaultComponentInstaller::FinishRegistration,
                 base::Unretained(this),
                 cus));
}

base::FilePath DefaultComponentInstaller::GetInstallDirectory() {
  return installer_traits_->GetBaseDirectory()
      .AppendASCII(current_version_.GetString());
}

void DefaultComponentInstaller::FinishRegistration(
    ComponentUpdateService* cus) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
  if (installer_traits_->CanAutoUpdate()) {
    CrxComponent crx;
    crx.name = installer_traits_->GetName();
    crx.installer = this;
    crx.version = current_version_;
    crx.fingerprint = current_fingerprint_;
    installer_traits_->GetHash(&crx.pk_hash);
    ComponentUpdateService::Status status = cus->RegisterComponent(crx);
    if (status != ComponentUpdateService::kOk &&
        status != ComponentUpdateService::kReplaced) {
      NOTREACHED() << "Component registration failed for "
                   << installer_traits_->GetName();
      return;
    }
  }

  if (current_version_.CompareTo(base::Version(kNullVersion)) > 0) {
    scoped_ptr<base::DictionaryValue> manifest_copy(
        current_manifest_->DeepCopy());
    installer_traits_->ComponentReady(
        current_version_,
        GetInstallDirectory(),
        manifest_copy.Pass());
  }
}
