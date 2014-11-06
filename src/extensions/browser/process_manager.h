// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_BROWSER_PROCESS_MANAGER_H_
#define EXTENSIONS_BROWSER_PROCESS_MANAGER_H_

#include <map>
#include <set>
#include <string>

#include "base/callback.h"
#include "base/compiler_specific.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "extensions/common/view_type.h"

class GURL;

namespace content {
class BrowserContext;
class DevToolsAgentHost;
class RenderViewHost;
class SiteInstance;
};

namespace extensions {

class Extension;
class ExtensionHost;

// Manages dynamic state of running Chromium extensions. There is one instance
// of this class per Profile. OTR Profiles have a separate instance that keeps
// track of split-mode extensions only.
class ProcessManager : public content::NotificationObserver {
 public:
  typedef std::set<extensions::ExtensionHost*> ExtensionHostSet;
  typedef ExtensionHostSet::const_iterator const_iterator;

  static ProcessManager* Create(content::BrowserContext* context);
  virtual ~ProcessManager();

  const ExtensionHostSet& background_hosts() const {
    return background_hosts_;
  }

  typedef std::set<content::RenderViewHost*> ViewSet;
  const ViewSet GetAllViews() const;

  // Creates a new UI-less extension instance.  Like CreateViewHost, but not
  // displayed anywhere.
  virtual ExtensionHost* CreateBackgroundHost(const Extension* extension,
                                              const GURL& url);

  // Gets the ExtensionHost for the background page for an extension, or NULL if
  // the extension isn't running or doesn't have a background page.
  ExtensionHost* GetBackgroundHostForExtension(const std::string& extension_id);

  // Returns the SiteInstance that the given URL belongs to.
  // TODO(aa): This only returns correct results for extensions and packaged
  // apps, not hosted apps.
  virtual content::SiteInstance* GetSiteInstanceForURL(const GURL& url);

  // Unregisters a RenderViewHost as hosting any extension.
  void UnregisterRenderViewHost(content::RenderViewHost* render_view_host);

  // Returns all RenderViewHosts that are registered for the specified
  // extension.
  std::set<content::RenderViewHost*> GetRenderViewHostsForExtension(
      const std::string& extension_id);

  // Returns the extension associated with the specified RenderViewHost, or
  // NULL.
  const Extension* GetExtensionForRenderViewHost(
      content::RenderViewHost* render_view_host);

  // Returns true if the (lazy) background host for the given extension has
  // already been sent the unload event and is shutting down.
  bool IsBackgroundHostClosing(const std::string& extension_id);

  // Getter and setter for the lazy background page's keepalive count. This is
  // the count of how many outstanding "things" are keeping the page alive.
  // When this reaches 0, we will begin the process of shutting down the page.
  // "Things" include pending events, resource loads, and API calls.
  int GetLazyKeepaliveCount(const Extension* extension);
  int IncrementLazyKeepaliveCount(const Extension* extension);
  int DecrementLazyKeepaliveCount(const Extension* extension);

  void IncrementLazyKeepaliveCountForView(
      content::RenderViewHost* render_view_host);

  // Handles a response to the ShouldSuspend message, used for lazy background
  // pages.
  void OnShouldSuspendAck(const std::string& extension_id, int sequence_id);

  // Same as above, for the Suspend message.
  void OnSuspendAck(const std::string& extension_id);

  // Tracks network requests for a given RenderViewHost, used to know
  // when network activity is idle for lazy background pages.
  void OnNetworkRequestStarted(content::RenderViewHost* render_view_host);
  void OnNetworkRequestDone(content::RenderViewHost* render_view_host);

  // Prevents |extension|'s background page from being closed and sends the
  // onSuspendCanceled() event to it.
  void CancelSuspend(const Extension* extension);

  // If |defer| is true background host creation is to be deferred until this is
  // called again with |defer| set to false, at which point all deferred
  // background hosts will be created.  Defaults to false.
  void DeferBackgroundHostCreation(bool defer);

  // Ensures background hosts are loaded for a new browser window.
  void OnBrowserWindowReady();

  // Gets the BrowserContext associated with site_instance_ and all other
  // related SiteInstances.
  content::BrowserContext* GetBrowserContext() const;

 protected:
  // If |context| is incognito pass the master context as |original_context|.
  // Otherwise pass the same context for both.
  ProcessManager(content::BrowserContext* context,
                 content::BrowserContext* original_context);

  // Called on browser shutdown to close our extension hosts.
  void CloseBackgroundHosts();

  // content::NotificationObserver:
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  // Load all background pages once the profile data is ready and the pages
  // should be loaded.
  void CreateBackgroundHostsForProfileStartup();

  content::NotificationRegistrar registrar_;

  // The set of ExtensionHosts running viewless background extensions.
  ExtensionHostSet background_hosts_;

  // A SiteInstance related to the SiteInstance for all extensions in
  // this profile.  We create it in such a way that a new
  // browsing instance is created.  This controls process grouping.
  scoped_refptr<content::SiteInstance> site_instance_;

 private:
  friend class ProcessManagerTest;

  // Extra information we keep for each extension's background page.
  struct BackgroundPageData;
  typedef std::string ExtensionId;
  typedef std::map<ExtensionId, BackgroundPageData> BackgroundPageDataMap;
  typedef std::map<content::RenderViewHost*,
      extensions::ViewType> ExtensionRenderViews;

  // Called just after |host| is created so it can be registered in our lists.
  void OnBackgroundHostCreated(ExtensionHost* host);

  // Close the given |host| iff it's a background page.
  void CloseBackgroundHost(ExtensionHost* host);

  // These are called when the extension transitions between idle and active.
  // They control the process of closing the background page when idle.
  void OnLazyBackgroundPageIdle(const std::string& extension_id,
                                int sequence_id);
  void OnLazyBackgroundPageActive(const std::string& extension_id);
  void CloseLazyBackgroundPageNow(const std::string& extension_id,
                                  int sequence_id);

  // Potentially registers a RenderViewHost, if it is associated with an
  // extension. Does nothing if this is not an extension renderer.
  void RegisterRenderViewHost(content::RenderViewHost* render_view_host);

  // Unregister RenderViewHosts and clear background page data for an extension
  // which has been unloaded.
  void UnregisterExtension(const std::string& extension_id);

  // Clears background page data for this extension.
  void ClearBackgroundPageData(const std::string& extension_id);

  // Returns true if loading background pages should be deferred.
  bool DeferLoadingBackgroundHosts() const;

  void OnDevToolsStateChanged(content::DevToolsAgentHost*, bool attached);

  // Contains all active extension-related RenderViewHost instances for all
  // extensions. We also keep a cache of the host's view type, because that
  // information is not accessible at registration/deregistration time.
  ExtensionRenderViews all_extension_views_;

  BackgroundPageDataMap background_page_data_;

  // The time to delay between an extension becoming idle and
  // sending a ShouldSuspend message; read from command-line switch.
  base::TimeDelta event_page_idle_time_;

  // The time to delay between sending a ShouldSuspend message and
  // sending a Suspend message; read from command-line switch.
  base::TimeDelta event_page_suspending_time_;

  // If true, then creation of background hosts is suspended.
  bool defer_background_host_creation_;

  // True if we have created the startup set of background hosts.
  bool startup_background_hosts_created_;

  base::Callback<void(content::DevToolsAgentHost*, bool)> devtools_callback_;

  base::WeakPtrFactory<ProcessManager> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(ProcessManager);
};

}  // namespace extensions

#endif  // EXTENSIONS_BROWSER_PROCESS_MANAGER_H_
