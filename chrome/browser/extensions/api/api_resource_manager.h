// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_API_RESOURCE_MANAGER_H_
#define CHROME_BROWSER_EXTENSIONS_API_API_RESOURCE_MANAGER_H_

#include <map>

#include "base/containers/hash_tables.h"
#include "base/lazy_instance.h"
#include "base/memory/linked_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/threading/non_thread_safe.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/extensions/api/profile_keyed_api_factory.h"
#include "chrome/browser/extensions/extension_host.h"
#include "chrome/common/extensions/extension.h"
#include "components/browser_context_keyed_service/browser_context_keyed_service.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/notification_service.h"

namespace extensions {
namespace api {
class TCPServerSocketEventDispatcher;
class TCPSocketEventDispatcher;
class UDPSocketEventDispatcher;
}
}

namespace extensions {

// An ApiResourceManager manages the lifetime of a set of resources that
// ApiFunctions use. Examples are sockets or USB connections.
//
// Users of this class should define kThreadId to be the thread that
// ApiResourceManager to works on. The default is defined in ApiResource.
// The user must also define a static const char* service_name() that returns
// the name of the service, and in order for ApiResourceManager to use
// service_name() friend this class.
//
// In the cc file the user must define a GetFactoryInstance() and manage their
// own instances (typically using LazyInstance or Singleton).
//
// E.g.:
//
// class Resource {
//  public:
//   static const BrowserThread::ID kThreadId = BrowserThread::FILE;
//  private:
//   friend class ApiResourceManager<Resource>;
//   static const char* service_name() {
//     return "ResourceManager";
//    }
// };
//
// In the cc file:
//
// static base::LazyInstance<ProfileKeyedAPIFactory<
//     ApiResourceManager<Resource> > >
//         g_factory = LAZY_INSTANCE_INITIALIZER;
//
//
// template <>
// ProfileKeyedAPIFactory<ApiResourceManager<Resource> >*
// ApiResourceManager<Resource>::GetFactoryInstance() {
//   return &g_factory.Get();
// }
template <class T>
class ApiResourceManager : public ProfileKeyedAPI,
                           public base::NonThreadSafe,
                           public content::NotificationObserver {
 public:
  explicit ApiResourceManager(Profile* profile)
      : thread_id_(T::kThreadId),
        data_(new ApiResourceData(thread_id_)) {
    registrar_.Add(
      this,
      chrome::NOTIFICATION_EXTENSION_UNLOADED,
      content::NotificationService::AllSources());
    registrar_.Add(
      this,
      chrome::NOTIFICATION_EXTENSION_HOST_DESTROYED,
      content::NotificationService::AllSources());
  }

  // For Testing.
  static ApiResourceManager<T>* CreateApiResourceManagerForTest(
      Profile* profile,
      content::BrowserThread::ID thread_id) {
    ApiResourceManager* manager = new ApiResourceManager<T>(profile);
    manager->thread_id_ = thread_id;
    manager->data_ = new ApiResourceData(thread_id);
    return manager;
  }

  virtual ~ApiResourceManager() {
    DCHECK(CalledOnValidThread());
    DCHECK(content::BrowserThread::IsMessageLoopValid(thread_id_)) <<
        "A unit test is using an ApiResourceManager but didn't provide "
        "the thread message loop needed for that kind of resource. "
        "Please ensure that the appropriate message loop is operational.";

    data_->InititateCleanup();
  }

  // ProfileKeyedAPI implementation.
  static ProfileKeyedAPIFactory<ApiResourceManager<T> >* GetFactoryInstance();

  // Convenience method to get the ApiResourceManager for a profile.
  static ApiResourceManager<T>* Get(Profile* profile) {
    return ProfileKeyedAPIFactory<ApiResourceManager<T> >::GetForProfile(
        profile);
  }

  // Takes ownership.
  int Add(T* api_resource) {
    return data_->Add(api_resource);
  }

  void Remove(const std::string& extension_id, int api_resource_id) {
    data_->Remove(extension_id, api_resource_id);
  }

  T* Get(const std::string& extension_id, int api_resource_id) {
    return data_->Get(extension_id, api_resource_id);
  }

  base::hash_set<int>* GetResourceIds(const std::string& extension_id) {
    return data_->GetResourceIds(extension_id);
  }

 protected:
  // content::NotificationObserver:
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE {
    switch (type) {
      case chrome::NOTIFICATION_EXTENSION_UNLOADED: {
        std::string id =
            content::Details<extensions::UnloadedExtensionInfo>(details)->
                extension->id();
        data_->InitiateExtensionUnloadedCleanup(id);
        break;
      }
      case chrome::NOTIFICATION_EXTENSION_HOST_DESTROYED: {
        ExtensionHost* host = content::Details<ExtensionHost>(details).ptr();
        data_->InitiateExtensionSuspendedCleanup(host->extension_id());
        break;
      }
    }
  }

 private:
  friend class api::TCPServerSocketEventDispatcher;
  friend class api::TCPSocketEventDispatcher;
  friend class api::UDPSocketEventDispatcher;
  friend class ProfileKeyedAPIFactory<ApiResourceManager<T> >;
  // ProfileKeyedAPI implementation.
  static const char* service_name() {
    return T::service_name();
  }
  static const bool kServiceHasOwnInstanceInIncognito = true;
  static const bool kServiceIsNULLWhileTesting = true;

  // ApiResourceData class handles resource bookkeeping on a thread
  // where resource lifetime is handled.
  class ApiResourceData : public base::RefCountedThreadSafe<ApiResourceData> {
   public:
    typedef std::map<int, linked_ptr<T> > ApiResourceMap;
    // Lookup map from extension id's to allocated resource id's.
    typedef std::map<std::string, base::hash_set<int> > ExtensionToResourceMap;

    explicit ApiResourceData(const content::BrowserThread::ID thread_id)
        : next_id_(1),
          thread_id_(thread_id) {
    }

    int Add(T* api_resource) {
      DCHECK(content::BrowserThread::CurrentlyOn(thread_id_));
      int id = GenerateId();
      if (id > 0) {
        linked_ptr<T> resource_ptr(api_resource);
        api_resource_map_[id] = resource_ptr;

        const std::string& extension_id = api_resource->owner_extension_id();
        if (extension_resource_map_.find(extension_id) ==
            extension_resource_map_.end()) {
          extension_resource_map_[extension_id] = base::hash_set<int>();
        }
        extension_resource_map_[extension_id].insert(id);

       return id;
     }
     return 0;
    }

    void Remove(const std::string& extension_id, int api_resource_id) {
      DCHECK(content::BrowserThread::CurrentlyOn(thread_id_));
      if (GetOwnedResource(extension_id, api_resource_id) != NULL) {
        DCHECK(extension_resource_map_.find(extension_id) !=
               extension_resource_map_.end());
        extension_resource_map_[extension_id].erase(api_resource_id);
        api_resource_map_.erase(api_resource_id);
      }
    }

    T* Get(const std::string& extension_id, int api_resource_id) {
      DCHECK(content::BrowserThread::CurrentlyOn(thread_id_));
      return GetOwnedResource(extension_id, api_resource_id);
    }

    base::hash_set<int>* GetResourceIds(const std::string& extension_id) {
      DCHECK(content::BrowserThread::CurrentlyOn(thread_id_));
      return GetOwnedResourceIds(extension_id);
    }

    void InitiateExtensionUnloadedCleanup(const std::string& extension_id) {
      content::BrowserThread::PostTask(thread_id_, FROM_HERE,
          base::Bind(&ApiResourceData::CleanupResourcesFromUnloadedExtension,
                     this, extension_id));
    }

    void InitiateExtensionSuspendedCleanup(const std::string& extension_id) {
      content::BrowserThread::PostTask(thread_id_, FROM_HERE,
          base::Bind(&ApiResourceData::CleanupResourcesFromSuspendedExtension,
                     this, extension_id));
    }

    void InititateCleanup() {
      content::BrowserThread::PostTask(thread_id_, FROM_HERE,
          base::Bind(&ApiResourceData::Cleanup, this));
    }

   private:
    friend class base::RefCountedThreadSafe<ApiResourceData>;

    virtual ~ApiResourceData() {}

    T* GetOwnedResource(const std::string& extension_id,
                        int api_resource_id) {
      linked_ptr<T> ptr = api_resource_map_[api_resource_id];
      T* resource = ptr.get();
      if (resource && extension_id == resource->owner_extension_id())
        return resource;
      return NULL;
    }

    base::hash_set<int>* GetOwnedResourceIds(const std::string& extension_id) {
      DCHECK(content::BrowserThread::CurrentlyOn(thread_id_));
      if (extension_resource_map_.find(extension_id) ==
          extension_resource_map_.end())
        return NULL;

      return &extension_resource_map_[extension_id];
    }

    void CleanupResourcesFromUnloadedExtension(
        const std::string& extension_id) {
      CleanupResourcesFromExtension(extension_id, true);
    }

    void CleanupResourcesFromSuspendedExtension(
        const std::string& extension_id) {
      CleanupResourcesFromExtension(extension_id, false);
    }

    void CleanupResourcesFromExtension(const std::string& extension_id,
                                       bool remove_all) {
      DCHECK(content::BrowserThread::CurrentlyOn(thread_id_));

      if (extension_resource_map_.find(extension_id) ==
          extension_resource_map_.end()) {
        return;
      }

      // Remove all resources, or the non persistent ones only if |remove_all|
      // is false.
      base::hash_set<int>& resource_ids =
          extension_resource_map_[extension_id];
      for (base::hash_set<int>::iterator it = resource_ids.begin();
            it != resource_ids.end(); ) {
        bool erase = false;
        if (remove_all) {
          erase = true;
        } else {
          linked_ptr<T> ptr = api_resource_map_[*it];
          T* resource = ptr.get();
          erase = (resource && !resource->IsPersistent());
        }

        if (erase) {
          api_resource_map_.erase(*it);
          resource_ids.erase(it++);
        } else {
          ++it;
        }
      }  // end for

      // Remove extension entry if we removed all its resources.
      if (resource_ids.size() == 0) {
        extension_resource_map_.erase(extension_id);
      }
    }

    void Cleanup() {
      DCHECK(content::BrowserThread::CurrentlyOn(thread_id_));

      api_resource_map_.clear();
      extension_resource_map_.clear();
    }

    int GenerateId() {
      return next_id_++;
    }

    int next_id_;
    const content::BrowserThread::ID thread_id_;
    ApiResourceMap api_resource_map_;
    ExtensionToResourceMap extension_resource_map_;
  };

  content::BrowserThread::ID thread_id_;
  content::NotificationRegistrar registrar_;
  scoped_refptr<ApiResourceData> data_;
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_API_API_RESOURCE_MANAGER_H_
