// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/app_list/profile_loader.h"

#include "base/bind.h"
#include "base/files/file_path.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/profiles/profile_manager.h"

ProfileLoader::ProfileLoader(ProfileStore* profile_store,
                             scoped_ptr<KeepAliveService> keep_alive_service)
  : profile_store_(profile_store),
    keep_alive_service_(keep_alive_service.Pass()),
    profile_load_sequence_id_(0),
    pending_profile_loads_(0),
    weak_factory_(this) {
}

ProfileLoader::~ProfileLoader() {
}

bool ProfileLoader::IsAnyProfileLoading() const {
  return pending_profile_loads_ > 0;
}

void ProfileLoader::InvalidatePendingProfileLoads() {
  ++profile_load_sequence_id_;
}

void ProfileLoader::LoadProfileInvalidatingOtherLoads(
    const base::FilePath& profile_file_path,
    base::Callback<void(Profile*)> callback) {
  InvalidatePendingProfileLoads();

  Profile* profile = profile_store_->GetProfileByPath(profile_file_path);
  if (profile) {
    callback.Run(profile);
    return;
  }

  IncrementPendingProfileLoads();
  profile_store_->LoadProfileAsync(
      profile_file_path,
      base::Bind(&ProfileLoader::OnProfileLoaded,
                 weak_factory_.GetWeakPtr(),
                 profile_load_sequence_id_,
                 callback));
}

void ProfileLoader::OnProfileLoaded(int profile_load_sequence_id,
                                    base::Callback<void(Profile*)> callback,
                                    Profile* profile) {
  DecrementPendingProfileLoads();
  if (profile_load_sequence_id == profile_load_sequence_id_)
    callback.Run(profile);
}

void ProfileLoader::IncrementPendingProfileLoads() {
  pending_profile_loads_++;
  if (pending_profile_loads_ == 1)
    keep_alive_service_->EnsureKeepAlive();
}

void ProfileLoader::DecrementPendingProfileLoads() {
  pending_profile_loads_--;
  if (pending_profile_loads_ == 0)
    keep_alive_service_->FreeKeepAlive();
}
