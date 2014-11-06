// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/browser/process_manager.h"

#include "base/bind.h"
#include "base/command_line.h"
#include "base/lazy_instance.h"
#include "base/logging.h"
#include "base/message_loop/message_loop.h"
#include "base/metrics/histogram.h"
#include "base/stl_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/extensions/api/runtime/runtime_api.h"
#include "chrome/browser/extensions/extension_host.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/extensions/extension_util.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/extension_messages.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/devtools_agent_host.h"
#include "content/public/browser/devtools_manager.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "content/public/common/renderer_preferences.h"
#include "extensions/browser/extensions_browser_client.h"
#include "extensions/browser/view_type_utils.h"
#include "extensions/common/manifest_handlers/background_info.h"
#include "extensions/common/manifest_handlers/incognito_info.h"
#include "extensions/common/switches.h"

#if defined(OS_MACOSX)
#include "chrome/browser/extensions/extension_host_mac.h"
#endif

using content::BrowserContext;
using content::RenderViewHost;
using content::SiteInstance;
using content::WebContents;

namespace extensions {
class RenderViewHostDestructionObserver;
}
DEFINE_WEB_CONTENTS_USER_DATA_KEY(
    extensions::RenderViewHostDestructionObserver);

namespace extensions {

namespace {

std::string GetExtensionID(RenderViewHost* render_view_host) {
  // This works for both apps and extensions because the site has been
  // normalized to the extension URL for apps.
  if (!render_view_host->GetSiteInstance())
    return std::string();

  return render_view_host->GetSiteInstance()->GetSiteURL().host();
}

void OnRenderViewHostUnregistered(BrowserContext* context,
                                  RenderViewHost* render_view_host) {
  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_EXTENSION_VIEW_UNREGISTERED,
      content::Source<BrowserContext>(context),
      content::Details<RenderViewHost>(render_view_host));
}

// Incognito profiles use this process manager. It is mostly a shim that decides
// whether to fall back on the original profile's ProcessManager based
// on whether a given extension uses "split" or "spanning" incognito behavior.
class IncognitoProcessManager : public ProcessManager {
 public:
  IncognitoProcessManager(BrowserContext* incognito_context,
                          BrowserContext* original_context);
  virtual ~IncognitoProcessManager();
  virtual ExtensionHost* CreateBackgroundHost(const Extension* extension,
                                              const GURL& url) OVERRIDE;
  virtual SiteInstance* GetSiteInstanceForURL(const GURL& url) OVERRIDE;

 private:
  // Returns true if the extension is allowed to run in incognito mode.
  bool IsIncognitoEnabled(const Extension* extension);

  ProcessManager* original_manager_;

  DISALLOW_COPY_AND_ASSIGN(IncognitoProcessManager);
};

static void CreateBackgroundHostForExtensionLoad(
    ProcessManager* manager, const Extension* extension) {
  DVLOG(1) << "CreateBackgroundHostForExtensionLoad";
  if (BackgroundInfo::HasPersistentBackgroundPage(extension))
    manager->CreateBackgroundHost(extension,
                                  BackgroundInfo::GetBackgroundURL(extension));
}

}  // namespace

class RenderViewHostDestructionObserver
    : public content::WebContentsObserver,
      public content::WebContentsUserData<RenderViewHostDestructionObserver> {
 public:
  virtual ~RenderViewHostDestructionObserver() {}

 private:
  explicit RenderViewHostDestructionObserver(WebContents* web_contents)
      : WebContentsObserver(web_contents) {
    BrowserContext* context = web_contents->GetBrowserContext();
    process_manager_ =
        ExtensionSystem::GetForBrowserContext(context)->process_manager();
  }

  friend class content::WebContentsUserData<RenderViewHostDestructionObserver>;

  // content::WebContentsObserver overrides.
  virtual void RenderViewDeleted(RenderViewHost* render_view_host) OVERRIDE {
    process_manager_->UnregisterRenderViewHost(render_view_host);
  }

  ProcessManager* process_manager_;

  DISALLOW_COPY_AND_ASSIGN(RenderViewHostDestructionObserver);
};

struct ProcessManager::BackgroundPageData {
  // The count of things keeping the lazy background page alive.
  int lazy_keepalive_count;

  // This is used with the ShouldSuspend message, to ensure that the extension
  // remained idle between sending the message and receiving the ack.
  int close_sequence_id;

  // True if the page responded to the ShouldSuspend message and is currently
  // dispatching the suspend event. During this time any events that arrive will
  // cancel the suspend process and an onSuspendCanceled event will be
  // dispatched to the page.
  bool is_closing;

  // Keeps track of when this page was last suspended. Used for perf metrics.
  linked_ptr<base::ElapsedTimer> since_suspended;

  BackgroundPageData()
      : lazy_keepalive_count(0), close_sequence_id(0), is_closing(false) {}
};

//
// ProcessManager
//

// static
ProcessManager* ProcessManager::Create(BrowserContext* context) {
  if (context->IsOffTheRecord()) {
    BrowserContext* original_context =
        ExtensionsBrowserClient::Get()->GetOriginalContext(context);
    return new IncognitoProcessManager(context, original_context);
  }
  return new ProcessManager(context, context);
}

ProcessManager::ProcessManager(BrowserContext* context,
                               BrowserContext* original_context)
  : site_instance_(SiteInstance::Create(context)),
    defer_background_host_creation_(false),
    startup_background_hosts_created_(false),
    devtools_callback_(base::Bind(
        &ProcessManager::OnDevToolsStateChanged,
        base::Unretained(this))),
    weak_ptr_factory_(this) {
  registrar_.Add(this, chrome::NOTIFICATION_EXTENSIONS_READY,
                 content::Source<BrowserContext>(original_context));
  registrar_.Add(this, chrome::NOTIFICATION_EXTENSION_LOADED,
                 content::Source<BrowserContext>(original_context));
  registrar_.Add(this, chrome::NOTIFICATION_EXTENSION_UNLOADED,
                 content::Source<BrowserContext>(original_context));
  registrar_.Add(this, chrome::NOTIFICATION_EXTENSION_HOST_DESTROYED,
                 content::Source<BrowserContext>(context));
  registrar_.Add(this, chrome::NOTIFICATION_EXTENSION_HOST_VIEW_SHOULD_CLOSE,
                 content::Source<BrowserContext>(context));
  registrar_.Add(this, content::NOTIFICATION_RENDER_VIEW_HOST_CHANGED,
                 content::NotificationService::AllSources());
  registrar_.Add(this, content::NOTIFICATION_WEB_CONTENTS_CONNECTED,
                 content::NotificationService::AllSources());
  registrar_.Add(this, chrome::NOTIFICATION_PROFILE_CREATED,
                 content::Source<BrowserContext>(original_context));
  registrar_.Add(this, chrome::NOTIFICATION_PROFILE_DESTROYED,
                 content::Source<BrowserContext>(context));
  if (context->IsOffTheRecord()) {
    registrar_.Add(this, chrome::NOTIFICATION_PROFILE_DESTROYED,
                   content::Source<BrowserContext>(original_context));
  }

  event_page_idle_time_ = base::TimeDelta::FromSeconds(10);
  unsigned idle_time_sec = 0;
  if (base::StringToUint(CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
          extensions::switches::kEventPageIdleTime), &idle_time_sec)) {
    event_page_idle_time_ = base::TimeDelta::FromSeconds(idle_time_sec);
  }
  event_page_suspending_time_ = base::TimeDelta::FromSeconds(5);
  unsigned suspending_time_sec = 0;
  if (base::StringToUint(CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
                             extensions::switches::kEventPageSuspendingTime),
                         &suspending_time_sec)) {
    event_page_suspending_time_ =
        base::TimeDelta::FromSeconds(suspending_time_sec);
  }

  content::DevToolsManager::GetInstance()->AddAgentStateCallback(
      devtools_callback_);
}

ProcessManager::~ProcessManager() {
  CloseBackgroundHosts();
  DCHECK(background_hosts_.empty());
  content::DevToolsManager::GetInstance()->RemoveAgentStateCallback(
      devtools_callback_);
}

const ProcessManager::ViewSet ProcessManager::GetAllViews() const {
  ViewSet result;
  for (ExtensionRenderViews::const_iterator iter =
           all_extension_views_.begin();
       iter != all_extension_views_.end(); ++iter) {
    result.insert(iter->first);
  }
  return result;
}

ExtensionHost* ProcessManager::CreateBackgroundHost(const Extension* extension,
                                                    const GURL& url) {
  DVLOG(1) << "CreateBackgroundHost " << url.spec();
  // Hosted apps are taken care of from BackgroundContentsService. Ignore them
  // here.
  if (extension->is_hosted_app())
    return NULL;

  // Don't create multiple background hosts for an extension.
  if (ExtensionHost* host = GetBackgroundHostForExtension(extension->id()))
    return host;  // TODO(kalman): return NULL here? It might break things...

  ExtensionHost* host =
#if defined(OS_MACOSX)
      new ExtensionHostMac(
          extension, GetSiteInstanceForURL(url), url,
          VIEW_TYPE_EXTENSION_BACKGROUND_PAGE);
#else
      new ExtensionHost(extension, GetSiteInstanceForURL(url), url,
                        VIEW_TYPE_EXTENSION_BACKGROUND_PAGE);
#endif

  host->CreateRenderViewSoon();
  OnBackgroundHostCreated(host);
  return host;
}

ExtensionHost* ProcessManager::GetBackgroundHostForExtension(
    const std::string& extension_id) {
  for (ExtensionHostSet::iterator iter = background_hosts_.begin();
       iter != background_hosts_.end(); ++iter) {
    ExtensionHost* host = *iter;
    if (host->extension_id() == extension_id)
      return host;
  }
  return NULL;
}

std::set<RenderViewHost*> ProcessManager::GetRenderViewHostsForExtension(
    const std::string& extension_id) {
  std::set<RenderViewHost*> result;

  SiteInstance* site_instance = GetSiteInstanceForURL(
      Extension::GetBaseURLFromExtensionId(extension_id));
  if (!site_instance)
    return result;

  // Gather up all the views for that site.
  for (ExtensionRenderViews::iterator view = all_extension_views_.begin();
       view != all_extension_views_.end(); ++view) {
    if (view->first->GetSiteInstance() == site_instance)
      result.insert(view->first);
  }

  return result;
}

const Extension* ProcessManager::GetExtensionForRenderViewHost(
    RenderViewHost* render_view_host) {
  if (!render_view_host->GetSiteInstance())
    return NULL;

  ExtensionService* service = ExtensionSystem::GetForBrowserContext(
      GetBrowserContext())->extension_service();
  if (!service)
    return NULL;

  return service->extensions()->GetByID(GetExtensionID(render_view_host));
}

void ProcessManager::UnregisterRenderViewHost(
    RenderViewHost* render_view_host) {
  ExtensionRenderViews::iterator view =
      all_extension_views_.find(render_view_host);
  if (view == all_extension_views_.end())
    return;

  OnRenderViewHostUnregistered(GetBrowserContext(), render_view_host);
  ViewType view_type = view->second;
  all_extension_views_.erase(view);

  // Keepalive count, balanced in RegisterRenderViewHost.
  if (view_type != VIEW_TYPE_INVALID &&
      view_type != VIEW_TYPE_EXTENSION_BACKGROUND_PAGE) {
    const Extension* extension = GetExtensionForRenderViewHost(
        render_view_host);
    if (extension)
      DecrementLazyKeepaliveCount(extension);
  }
}

void ProcessManager::RegisterRenderViewHost(RenderViewHost* render_view_host) {
  const Extension* extension = GetExtensionForRenderViewHost(
      render_view_host);
  if (!extension)
    return;

  WebContents* web_contents = WebContents::FromRenderViewHost(render_view_host);
  all_extension_views_[render_view_host] = GetViewType(web_contents);

  // Keep the lazy background page alive as long as any non-background-page
  // extension views are visible. Keepalive count balanced in
  // UnregisterRenderViewHost.
  IncrementLazyKeepaliveCountForView(render_view_host);
}

SiteInstance* ProcessManager::GetSiteInstanceForURL(const GURL& url) {
  return site_instance_->GetRelatedSiteInstance(url);
}

bool ProcessManager::IsBackgroundHostClosing(const std::string& extension_id) {
  ExtensionHost* host = GetBackgroundHostForExtension(extension_id);
  return (host && background_page_data_[extension_id].is_closing);
}

int ProcessManager::GetLazyKeepaliveCount(const Extension* extension) {
  if (!BackgroundInfo::HasLazyBackgroundPage(extension))
    return 0;

  return background_page_data_[extension->id()].lazy_keepalive_count;
}

int ProcessManager::IncrementLazyKeepaliveCount(const Extension* extension) {
  if (!BackgroundInfo::HasLazyBackgroundPage(extension))
    return 0;

  int& count = background_page_data_[extension->id()].lazy_keepalive_count;
  if (++count == 1)
    OnLazyBackgroundPageActive(extension->id());

  return count;
}

int ProcessManager::DecrementLazyKeepaliveCount(const Extension* extension) {
  if (!BackgroundInfo::HasLazyBackgroundPage(extension))
    return 0;

  int& count = background_page_data_[extension->id()].lazy_keepalive_count;
  DCHECK_GT(count, 0);

  // If we reach a zero keepalive count when the lazy background page is about
  // to be closed, incrementing close_sequence_id will cancel the close
  // sequence and cause the background page to linger. So check is_closing
  // before initiating another close sequence.
  if (--count == 0 && !background_page_data_[extension->id()].is_closing) {
    base::MessageLoop::current()->PostDelayedTask(
        FROM_HERE,
        base::Bind(&ProcessManager::OnLazyBackgroundPageIdle,
                   weak_ptr_factory_.GetWeakPtr(), extension->id(),
                   ++background_page_data_[extension->id()].close_sequence_id),
        event_page_idle_time_);
  }

  return count;
}

void ProcessManager::IncrementLazyKeepaliveCountForView(
    RenderViewHost* render_view_host) {
  WebContents* web_contents =
      WebContents::FromRenderViewHost(render_view_host);
  ViewType view_type = GetViewType(web_contents);
  if (view_type != VIEW_TYPE_INVALID &&
      view_type != VIEW_TYPE_EXTENSION_BACKGROUND_PAGE) {
    const Extension* extension = GetExtensionForRenderViewHost(
        render_view_host);
    if (extension)
      IncrementLazyKeepaliveCount(extension);
  }
}

void ProcessManager::OnLazyBackgroundPageIdle(const std::string& extension_id,
                                              int sequence_id) {
  ExtensionHost* host = GetBackgroundHostForExtension(extension_id);
  if (host && !background_page_data_[extension_id].is_closing &&
      sequence_id == background_page_data_[extension_id].close_sequence_id) {
    // Tell the renderer we are about to close. This is a simple ping that the
    // renderer will respond to. The purpose is to control sequencing: if the
    // extension remains idle until the renderer responds with an ACK, then we
    // know that the extension process is ready to shut down. If our
    // close_sequence_id has already changed, then we would ignore the
    // ShouldSuspendAck, so we don't send the ping.
    host->render_view_host()->Send(new ExtensionMsg_ShouldSuspend(
        extension_id, sequence_id));
  }
}

void ProcessManager::OnLazyBackgroundPageActive(
    const std::string& extension_id) {
  ExtensionHost* host = GetBackgroundHostForExtension(extension_id);
  if (host && !background_page_data_[extension_id].is_closing) {
    // Cancel the current close sequence by changing the close_sequence_id,
    // which causes us to ignore the next ShouldSuspendAck.
    ++background_page_data_[extension_id].close_sequence_id;
  }
}

void ProcessManager::OnShouldSuspendAck(const std::string& extension_id,
                                        int sequence_id) {
  ExtensionHost* host = GetBackgroundHostForExtension(extension_id);
  if (host &&
      sequence_id == background_page_data_[extension_id].close_sequence_id) {
    host->render_view_host()->Send(new ExtensionMsg_Suspend(extension_id));
  }
}

void ProcessManager::OnSuspendAck(const std::string& extension_id) {
  background_page_data_[extension_id].is_closing = true;
  int sequence_id = background_page_data_[extension_id].close_sequence_id;
  base::MessageLoop::current()->PostDelayedTask(
      FROM_HERE,
      base::Bind(&ProcessManager::CloseLazyBackgroundPageNow,
                 weak_ptr_factory_.GetWeakPtr(), extension_id, sequence_id),
      event_page_suspending_time_);
}

void ProcessManager::CloseLazyBackgroundPageNow(const std::string& extension_id,
                                                int sequence_id) {
  ExtensionHost* host = GetBackgroundHostForExtension(extension_id);
  if (host &&
      sequence_id == background_page_data_[extension_id].close_sequence_id) {
    ExtensionHost* host = GetBackgroundHostForExtension(extension_id);
    if (host)
      CloseBackgroundHost(host);
  }
}

void ProcessManager::OnNetworkRequestStarted(RenderViewHost* render_view_host) {
  ExtensionHost* host = GetBackgroundHostForExtension(
      GetExtensionID(render_view_host));
  if (host && host->render_view_host() == render_view_host)
    IncrementLazyKeepaliveCount(host->extension());
}

void ProcessManager::OnNetworkRequestDone(RenderViewHost* render_view_host) {
  ExtensionHost* host = GetBackgroundHostForExtension(
      GetExtensionID(render_view_host));
  if (host && host->render_view_host() == render_view_host)
    DecrementLazyKeepaliveCount(host->extension());
}

void ProcessManager::CancelSuspend(const Extension* extension) {
  bool& is_closing = background_page_data_[extension->id()].is_closing;
  ExtensionHost* host = GetBackgroundHostForExtension(extension->id());
  if (host && is_closing) {
    is_closing = false;
    host->render_view_host()->Send(
        new ExtensionMsg_CancelSuspend(extension->id()));
    // This increment / decrement is to simulate an instantaneous event. This
    // has the effect of invalidating close_sequence_id, preventing any in
    // progress closes from completing and starting a new close process if
    // necessary.
    IncrementLazyKeepaliveCount(extension);
    DecrementLazyKeepaliveCount(extension);
  }
}

void ProcessManager::DeferBackgroundHostCreation(bool defer) {
  bool previous = defer_background_host_creation_;
  defer_background_host_creation_ = defer;

  // If we were deferred, and we switch to non-deferred, then create the
  // background hosts.
  if (previous && !defer_background_host_creation_)
    CreateBackgroundHostsForProfileStartup();
}

void ProcessManager::OnBrowserWindowReady() {
  ExtensionService* service = ExtensionSystem::GetForBrowserContext(
      GetBrowserContext())->extension_service();
  // On Chrome OS, a login screen is implemented as a browser.
  // This browser has no extension service.  In this case,
  // service will be NULL.
  if (!service || !service->is_ready())
    return;

  CreateBackgroundHostsForProfileStartup();
}

content::BrowserContext* ProcessManager::GetBrowserContext() const {
  return site_instance_->GetBrowserContext();
}

void ProcessManager::Observe(int type,
                             const content::NotificationSource& source,
                             const content::NotificationDetails& details) {
  switch (type) {
    case chrome::NOTIFICATION_EXTENSIONS_READY:
    case chrome::NOTIFICATION_PROFILE_CREATED: {
      CreateBackgroundHostsForProfileStartup();
      break;
    }

    case chrome::NOTIFICATION_EXTENSION_LOADED: {
      BrowserContext* context = content::Source<BrowserContext>(source).ptr();
      ExtensionService* service =
          ExtensionSystem::GetForBrowserContext(context)->extension_service();
      if (service->is_ready()) {
        const Extension* extension =
            content::Details<const Extension>(details).ptr();
        CreateBackgroundHostForExtensionLoad(this, extension);
      }
      break;
    }

    case chrome::NOTIFICATION_EXTENSION_UNLOADED: {
      const Extension* extension =
          content::Details<UnloadedExtensionInfo>(details)->extension;
      for (ExtensionHostSet::iterator iter = background_hosts_.begin();
           iter != background_hosts_.end(); ++iter) {
        ExtensionHost* host = *iter;
        if (host->extension_id() == extension->id()) {
          CloseBackgroundHost(host);
          break;
        }
      }
      UnregisterExtension(extension->id());
      break;
    }

    case chrome::NOTIFICATION_EXTENSION_HOST_DESTROYED: {
      ExtensionHost* host = content::Details<ExtensionHost>(details).ptr();
      if (background_hosts_.erase(host)) {
        ClearBackgroundPageData(host->extension()->id());
        background_page_data_[host->extension()->id()].since_suspended.reset(
            new base::ElapsedTimer());
      }
      break;
    }

    case chrome::NOTIFICATION_EXTENSION_HOST_VIEW_SHOULD_CLOSE: {
      ExtensionHost* host = content::Details<ExtensionHost>(details).ptr();
      if (host->extension_host_type() == VIEW_TYPE_EXTENSION_BACKGROUND_PAGE) {
        CloseBackgroundHost(host);
      }
      break;
    }

    case content::NOTIFICATION_RENDER_VIEW_HOST_CHANGED: {
      // We get this notification both for new WebContents and when one
      // has its RenderViewHost replaced (e.g. when a user does a cross-site
      // navigation away from an extension URL). For the replaced case, we must
      // unregister the old RVH so it doesn't count as an active view that would
      // keep the event page alive.
      WebContents* contents = content::Source<WebContents>(source).ptr();
      if (contents->GetBrowserContext() != GetBrowserContext())
        break;

      typedef std::pair<RenderViewHost*, RenderViewHost*> RVHPair;
      RVHPair* switched_details = content::Details<RVHPair>(details).ptr();
      if (switched_details->first)
        UnregisterRenderViewHost(switched_details->first);

      // The above will unregister a RVH when it gets swapped out with a new
      // one. However we need to watch the WebContents to know when a RVH is
      // deleted because the WebContents has gone away.
      RenderViewHostDestructionObserver::CreateForWebContents(contents);
      RegisterRenderViewHost(switched_details->second);
      break;
    }

    case content::NOTIFICATION_WEB_CONTENTS_CONNECTED: {
      WebContents* contents = content::Source<WebContents>(source).ptr();
      if (contents->GetBrowserContext() != GetBrowserContext())
        break;
      const Extension* extension = GetExtensionForRenderViewHost(
          contents->GetRenderViewHost());
      if (!extension)
        return;

      // RegisterRenderViewHost is called too early (before the process is
      // available), so we need to wait until now to notify.
      content::NotificationService::current()->Notify(
          chrome::NOTIFICATION_EXTENSION_VIEW_REGISTERED,
          content::Source<BrowserContext>(GetBrowserContext()),
          content::Details<RenderViewHost>(contents->GetRenderViewHost()));
      break;
    }

    case chrome::NOTIFICATION_PROFILE_DESTROYED: {
      // Close background hosts when the last browser is closed so that they
      // have time to shutdown various objects on different threads. Our
      // destructor is called too late in the shutdown sequence.
      CloseBackgroundHosts();
      break;
    }

    default:
      NOTREACHED();
  }
}

void ProcessManager::OnDevToolsStateChanged(
    content::DevToolsAgentHost* agent_host,
    bool attached) {
  RenderViewHost* rvh = agent_host->GetRenderViewHost();
  // Ignore unrelated notifications.
  if (!rvh ||
      rvh->GetSiteInstance()->GetProcess()->GetBrowserContext() !=
          GetBrowserContext())
    return;
  if (GetViewType(WebContents::FromRenderViewHost(rvh)) !=
      VIEW_TYPE_EXTENSION_BACKGROUND_PAGE)
    return;
  const Extension* extension = GetExtensionForRenderViewHost(rvh);
  if (!extension)
    return;
  if (attached) {
    // Keep the lazy background page alive while it's being inspected.
    CancelSuspend(extension);
    IncrementLazyKeepaliveCount(extension);
  } else {
    DecrementLazyKeepaliveCount(extension);
  }
}

void ProcessManager::CreateBackgroundHostsForProfileStartup() {
  if (startup_background_hosts_created_)
    return;

  // Don't load background hosts now if the loading should be deferred.
  // Instead they will be loaded when a browser window for this profile
  // (or an incognito profile from this profile) is ready, or when
  // DeferBackgroundHostCreation is called with false.
  if (DeferLoadingBackgroundHosts())
    return;

  ExtensionService* service = ExtensionSystem::GetForBrowserContext(
      GetBrowserContext())->extension_service();
  DCHECK(service);
  for (ExtensionSet::const_iterator extension = service->extensions()->begin();
       extension != service->extensions()->end(); ++extension) {
    CreateBackgroundHostForExtensionLoad(this, extension->get());

    RuntimeEventRouter::DispatchOnStartupEvent(GetBrowserContext(),
                                               (*extension)->id());
  }
  startup_background_hosts_created_ = true;

  // Background pages should only be loaded once. To prevent any further loads
  // occurring, we remove the notification listeners.
  BrowserContext* original_context =
      ExtensionsBrowserClient::Get()->GetOriginalContext(GetBrowserContext());
  if (registrar_.IsRegistered(
          this,
          chrome::NOTIFICATION_PROFILE_CREATED,
          content::Source<BrowserContext>(original_context))) {
    registrar_.Remove(this,
                      chrome::NOTIFICATION_PROFILE_CREATED,
                      content::Source<BrowserContext>(original_context));
  }
  if (registrar_.IsRegistered(
          this,
          chrome::NOTIFICATION_EXTENSIONS_READY,
          content::Source<BrowserContext>(original_context))) {
    registrar_.Remove(this,
                      chrome::NOTIFICATION_EXTENSIONS_READY,
                      content::Source<BrowserContext>(original_context));
  }
}

void ProcessManager::OnBackgroundHostCreated(ExtensionHost* host) {
  DCHECK_EQ(GetBrowserContext(), host->browser_context());
  background_hosts_.insert(host);

  if (BackgroundInfo::HasLazyBackgroundPage(host->extension())) {
    linked_ptr<base::ElapsedTimer> since_suspended(
        background_page_data_[host->extension()->id()].
            since_suspended.release());
    if (since_suspended.get()) {
      UMA_HISTOGRAM_LONG_TIMES("Extensions.EventPageIdleTime",
                               since_suspended->Elapsed());
    }
  }
}

void ProcessManager::CloseBackgroundHost(ExtensionHost* host) {
  CHECK(host->extension_host_type() ==
        VIEW_TYPE_EXTENSION_BACKGROUND_PAGE);
  delete host;
  // |host| should deregister itself from our structures.
  CHECK(background_hosts_.find(host) == background_hosts_.end());
}

void ProcessManager::CloseBackgroundHosts() {
  for (ExtensionHostSet::iterator iter = background_hosts_.begin();
       iter != background_hosts_.end(); ) {
    ExtensionHostSet::iterator current = iter++;
    delete *current;
  }
}

void ProcessManager::UnregisterExtension(const std::string& extension_id) {
  // The lazy_keepalive_count may be greater than zero at this point because
  // RenderViewHosts are still alive. During extension reloading, they will
  // decrement the lazy_keepalive_count to negative for the new extension
  // instance when they are destroyed. Since we are erasing the background page
  // data for the unloaded extension, unregister the RenderViewHosts too.
  BrowserContext* context = GetBrowserContext();
  for (ExtensionRenderViews::iterator it = all_extension_views_.begin();
       it != all_extension_views_.end(); ) {
    if (GetExtensionID(it->first) == extension_id) {
      OnRenderViewHostUnregistered(context, it->first);
      all_extension_views_.erase(it++);
    } else {
      ++it;
    }
  }

  background_page_data_.erase(extension_id);
}

void ProcessManager::ClearBackgroundPageData(const std::string& extension_id) {
  background_page_data_.erase(extension_id);

  // Re-register all RenderViews for this extension. We do this to restore
  // the lazy_keepalive_count (if any) to properly reflect the number of open
  // views.
  for (ExtensionRenderViews::const_iterator it = all_extension_views_.begin();
       it != all_extension_views_.end(); ++it) {
    if (GetExtensionID(it->first) == extension_id)
      IncrementLazyKeepaliveCountForView(it->first);
  }
}

bool ProcessManager::DeferLoadingBackgroundHosts() const {
  // Don't load background hosts now if the loading should be deferred.
  if (defer_background_host_creation_)
    return true;

  // The extensions embedder may have special rules about background hosts.
  return ExtensionsBrowserClient::Get()->DeferLoadingBackgroundHosts(
      GetBrowserContext());
}

//
// IncognitoProcessManager
//

IncognitoProcessManager::IncognitoProcessManager(
    BrowserContext* incognito_context,
    BrowserContext* original_context)
    : ProcessManager(incognito_context, original_context),
      original_manager_(ExtensionSystem::GetForBrowserContext(
          original_context)->process_manager()) {
  DCHECK(incognito_context->IsOffTheRecord());

  // The original profile will have its own ProcessManager to
  // load the background pages of the spanning extensions. This process
  // manager need only worry about the split mode extensions, which is handled
  // in the NOTIFICATION_BROWSER_WINDOW_READY notification handler.
  registrar_.Remove(this, chrome::NOTIFICATION_EXTENSIONS_READY,
                    content::Source<BrowserContext>(original_context));
  registrar_.Remove(this, chrome::NOTIFICATION_PROFILE_CREATED,
                    content::Source<BrowserContext>(original_context));
}

IncognitoProcessManager::~IncognitoProcessManager() {
  // TODO(yoz): This cleanup code belongs in the MenuManager.
  // Remove "incognito" "split" mode context menu items.
  ExtensionService* service = ExtensionSystem::GetForBrowserContext(
      GetBrowserContext())->extension_service();
  if (service)
    service->menu_manager()->RemoveAllIncognitoContextItems();
}

ExtensionHost* IncognitoProcessManager::CreateBackgroundHost(
    const Extension* extension, const GURL& url) {
  if (IncognitoInfo::IsSplitMode(extension)) {
    if (IsIncognitoEnabled(extension))
      return ProcessManager::CreateBackgroundHost(extension, url);
  } else {
    // Do nothing. If an extension is spanning, then its original-profile
    // background page is shared with incognito, so we don't create another.
  }
  return NULL;
}

SiteInstance* IncognitoProcessManager::GetSiteInstanceForURL(const GURL& url) {
  ExtensionService* service = ExtensionSystem::GetForBrowserContext(
      GetBrowserContext())->extension_service();
  if (service) {
    const Extension* extension =
        service->extensions()->GetExtensionOrAppByURL(url);
    if (extension && !IncognitoInfo::IsSplitMode(extension)) {
      return original_manager_->GetSiteInstanceForURL(url);
    }
  }
  return ProcessManager::GetSiteInstanceForURL(url);
}

bool IncognitoProcessManager::IsIncognitoEnabled(const Extension* extension) {
  // Keep in sync with duplicate in extension_info_map.cc.
  ExtensionService* service = ExtensionSystem::GetForBrowserContext(
      GetBrowserContext())->extension_service();
  return extension_util::IsIncognitoEnabled(extension->id(), service);
}

}  // namespace extensions
