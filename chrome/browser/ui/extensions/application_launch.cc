// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/extensions/application_launch.h"

#include <string>

#include "apps/launcher.h"
#include "base/command_line.h"
#include "base/metrics/field_trial.h"
#include "base/metrics/histogram.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/app_mode/app_mode_utils.h"
#include "chrome/browser/extensions/extension_prefs.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/extensions/tab_helper.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/signin/signin_manager.h"
#include "chrome/browser/signin/signin_manager_factory.h"
#include "chrome/browser/ui/app_list/app_list_service.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/extensions/extension_enable_flow.h"
#include "chrome/browser/ui/extensions/extension_enable_flow_delegate.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/web_applications/web_app.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/manifest_handlers/app_launch_info.h"
#include "chrome/common/extensions/manifest_url_handler.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_view.h"
#include "content/public/common/renderer_preferences.h"
#include "extensions/common/constants.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/window_open_disposition.h"
#include "ui/gfx/rect.h"

#if defined(OS_MACOSX)
#include "chrome/browser/ui/browser_commands_mac.h"
#endif

#if defined(OS_WIN)
#include "win8/util/win8_util.h"
#endif

using content::WebContents;
using extensions::Extension;
using extensions::ExtensionPrefs;

namespace {

// Attempts to launch a packaged app, prompting the user to enable it if
// necessary. If a prompt is required it will be shown inside the AppList.
// This class manages its own lifetime.
class EnableViaAppListFlow : public ExtensionEnableFlowDelegate {
 public:
  EnableViaAppListFlow(ExtensionService* service,
                       Profile* profile,
                       chrome::HostDesktopType desktop_type,
                       const std::string& extension_id,
                       const base::Closure& callback)
      : service_(service),
        profile_(profile),
        desktop_type_(desktop_type),
        extension_id_(extension_id),
        callback_(callback) {
  }

  virtual ~EnableViaAppListFlow() {
  }

  void Run() {
    DCHECK(!service_->IsExtensionEnabled(extension_id_));
    flow_.reset(new ExtensionEnableFlow(profile_, extension_id_, this));
    flow_->StartForCurrentlyNonexistentWindow(
        base::Bind(&EnableViaAppListFlow::ShowAppList, base::Unretained(this)));
  }

 private:
  gfx::NativeWindow ShowAppList() {
    AppListService* app_list_service = AppListService::Get(desktop_type_);
    app_list_service->Show();
    return app_list_service->GetAppListWindow();
  }

  // ExtensionEnableFlowDelegate overrides.
  virtual void ExtensionEnableFlowFinished() OVERRIDE {
    const Extension* extension =
        service_->GetExtensionById(extension_id_, false);
    if (!extension)
      return;
    callback_.Run();
    delete this;
  }

  virtual void ExtensionEnableFlowAborted(bool user_initiated) OVERRIDE {
    delete this;
  }

  ExtensionService* service_;
  Profile* profile_;
  chrome::HostDesktopType desktop_type_;
  std::string extension_id_;
  base::Closure callback_;
  scoped_ptr<ExtensionEnableFlow> flow_;

  DISALLOW_COPY_AND_ASSIGN(EnableViaAppListFlow);
};

// Get the launch URL for a given extension, with optional override/fallback.
// |override_url|, if non-empty, will be preferred over the extension's
// launch url.
GURL UrlForExtension(const Extension* extension,
                     const GURL& override_url) {
  if (!extension)
    return override_url;

  GURL url;
  if (!override_url.is_empty()) {
    DCHECK(extension->web_extent().MatchesURL(override_url) ||
           override_url.GetOrigin() == extension->url());
    url = override_url;
  } else {
    url = extensions::AppLaunchInfo::GetFullLaunchURL(extension);
  }

  // For extensions lacking launch urls, determine a reasonable fallback.
  if (!url.is_valid()) {
    url = extensions::ManifestURL::GetOptionsPage(extension);
    if (!url.is_valid())
      url = GURL(chrome::kChromeUIExtensionsURL);
  }

  return url;
}

ui::WindowShowState DetermineWindowShowState(
    Profile* profile,
    extensions::LaunchContainer container,
    const Extension* extension) {
  if (!extension ||
      container != extensions::LAUNCH_WINDOW) {
    return ui::SHOW_STATE_DEFAULT;
  }

  if (chrome::IsRunningInForcedAppMode())
    return ui::SHOW_STATE_FULLSCREEN;

#if defined(USE_ASH)
  // In ash, LAUNCH_FULLSCREEN launches in a maximized app window and
  // LAUNCH_WINDOW launches in a normal app window.
  ExtensionService* service =
      extensions::ExtensionSystem::Get(profile)->extension_service();
  ExtensionPrefs::LaunchType launch_type =
      service->extension_prefs()->GetLaunchType(
          extension, ExtensionPrefs::LAUNCH_TYPE_DEFAULT);
  if (launch_type == ExtensionPrefs::LAUNCH_TYPE_FULLSCREEN)
    return ui::SHOW_STATE_MAXIMIZED;
  else if (launch_type == ExtensionPrefs::LAUNCH_TYPE_WINDOW)
    return ui::SHOW_STATE_NORMAL;
#endif

  return ui::SHOW_STATE_DEFAULT;
}

WebContents* OpenApplicationWindow(const AppLaunchParams& params) {
  Profile* const profile = params.profile;
  const extensions::Extension* const extension = params.extension;
  const GURL url_input = params.override_url;

  DCHECK(!url_input.is_empty() || extension);
  GURL url = UrlForExtension(extension, url_input);
  Browser::CreateParams browser_params(
      Browser::TYPE_POPUP, profile, params.desktop_type);

  browser_params.app_name = extension ?
      web_app::GenerateApplicationNameFromExtensionId(extension->id()) :
      web_app::GenerateApplicationNameFromURL(url);

  if (!params.override_bounds.IsEmpty()) {
    browser_params.initial_bounds = params.override_bounds;
  } else if (extension) {
    browser_params.initial_bounds.set_width(
        extensions::AppLaunchInfo::GetLaunchWidth(extension));
    browser_params.initial_bounds.set_height(
        extensions::AppLaunchInfo::GetLaunchHeight(extension));
  }

  browser_params.initial_show_state = DetermineWindowShowState(profile,
                                                               params.container,
                                                               extension);

  Browser* browser = NULL;
#if defined(OS_WIN)
  // On Windows 8's single window Metro mode we don't allow multiple Chrome
  // windows to be created. We instead attempt to reuse an existing Browser
  // window.
  if (win8::IsSingleWindowMetroMode())
    browser = chrome::FindBrowserWithProfile(profile, params.desktop_type);

#endif
  if (!browser)
    browser = new Browser(browser_params);

  WebContents* web_contents = chrome::AddSelectedTabWithURL(
      browser, url, content::PAGE_TRANSITION_AUTO_TOPLEVEL);
  web_contents->GetMutableRendererPrefs()->can_accept_load_drops = false;
  web_contents->GetRenderViewHost()->SyncRendererPrefs();

  browser->window()->Show();

  // TODO(jcampan): http://crbug.com/8123 we should not need to set the initial
  //                focus explicitly.
  web_contents->GetView()->SetInitialFocus();
  return web_contents;
}

WebContents* OpenApplicationTab(const AppLaunchParams& launch_params) {
  Profile* const profile = launch_params.profile;
  const extensions::Extension* extension = launch_params.extension;
  WindowOpenDisposition disposition = launch_params.disposition;

  Browser* browser = chrome::FindTabbedBrowser(profile,
                                               false,
                                               launch_params.desktop_type);
  WebContents* contents = NULL;
  if (!browser) {
    // No browser for this profile, need to open a new one.
    browser = new Browser(Browser::CreateParams(Browser::TYPE_TABBED,
                                                profile,
                                                launch_params.desktop_type));
    browser->window()->Show();
    // There's no current tab in this browser window, so add a new one.
    disposition = NEW_FOREGROUND_TAB;
  } else {
    // For existing browser, ensure its window is shown and activated.
    browser->window()->Show();
    browser->window()->Activate();
  }

  // Check the prefs for overridden mode.
  ExtensionService* extension_service =
      extensions::ExtensionSystem::Get(profile)->extension_service();
  DCHECK(extension_service);

  ExtensionPrefs::LaunchType launch_type =
      extension_service->extension_prefs()->GetLaunchType(
          extension, ExtensionPrefs::LAUNCH_TYPE_DEFAULT);
  UMA_HISTOGRAM_ENUMERATION("Extensions.AppTabLaunchType", launch_type, 100);

  int add_type = TabStripModel::ADD_ACTIVE;
  if (launch_type == ExtensionPrefs::LAUNCH_TYPE_PINNED)
    add_type |= TabStripModel::ADD_PINNED;

  GURL extension_url = UrlForExtension(extension, launch_params.override_url);
  chrome::NavigateParams params(browser, extension_url,
                                content::PAGE_TRANSITION_AUTO_TOPLEVEL);
  params.tabstrip_add_types = add_type;
  params.disposition = disposition;

  if (disposition == CURRENT_TAB) {
    WebContents* existing_tab =
        browser->tab_strip_model()->GetActiveWebContents();
    TabStripModel* model = browser->tab_strip_model();
    int tab_index = model->GetIndexOfWebContents(existing_tab);

    existing_tab->OpenURL(content::OpenURLParams(
          extension_url,
          content::Referrer(existing_tab->GetURL(),
                            blink::WebReferrerPolicyDefault),
          disposition, content::PAGE_TRANSITION_LINK, false));
    // Reset existing_tab as OpenURL() may have clobbered it.
    existing_tab = browser->tab_strip_model()->GetActiveWebContents();
    if (params.tabstrip_add_types & TabStripModel::ADD_PINNED) {
      model->SetTabPinned(tab_index, true);
      // Pinning may have moved the tab.
      tab_index = model->GetIndexOfWebContents(existing_tab);
    }
    if (params.tabstrip_add_types & TabStripModel::ADD_ACTIVE)
      model->ActivateTabAt(tab_index, true);

    contents = existing_tab;
  } else {
    chrome::Navigate(&params);
    contents = params.target_contents;
  }

  // On Chrome OS the host desktop type for a browser window is always set to
  // HOST_DESKTOP_TYPE_ASH. On Windows 8 it is only the case for Chrome ASH
  // in metro mode.
  if (browser->host_desktop_type() == chrome::HOST_DESKTOP_TYPE_ASH) {
    // In ash, LAUNCH_FULLSCREEN launches in the OpenApplicationWindow function
    // i.e. it should not reach here.
    DCHECK(launch_type != ExtensionPrefs::LAUNCH_TYPE_FULLSCREEN);
  } else {
    // TODO(skerner):  If we are already in full screen mode, and the user
    // set the app to open as a regular or pinned tab, what should happen?
    // Today we open the tab, but stay in full screen mode.  Should we leave
    // full screen mode in this case?
    if (launch_type == ExtensionPrefs::LAUNCH_TYPE_FULLSCREEN &&
        !browser->window()->IsFullscreen()) {
#if defined(OS_MACOSX)
      chrome::ToggleFullscreenWithChromeOrFallback(browser);
#else
      chrome::ToggleFullscreenMode(browser);
#endif
    }
  }
  return contents;
}

WebContents* OpenEnabledApplication(const AppLaunchParams& params) {
  Profile* profile = params.profile;
  const extensions::Extension* extension = params.extension;

  WebContents* tab = NULL;
  ExtensionPrefs* prefs = extensions::ExtensionSystem::Get(profile)->
      extension_service()->extension_prefs();
  prefs->SetActiveBit(extension->id(), true);

  UMA_HISTOGRAM_ENUMERATION(
      "Extensions.AppLaunchContainer", params.container, 100);

  if (extension->is_platform_app()) {
#if !defined(OS_CHROMEOS)
    SigninManager* signin_manager =
        SigninManagerFactory::GetForProfile(profile);
    if (signin_manager && signin_manager->GetAuthenticatedUsername().empty()) {
      const char kEnforceSigninToUseAppsFieldTrial[] = "EnforceSigninToUseApps";

      std::string field_trial_value =
          base::FieldTrialList::FindFullName(kEnforceSigninToUseAppsFieldTrial);

      // Only enforce signin if the field trial is set.
      if (!field_trial_value.empty()) {
        GURL gurl(l10n_util::GetStringFUTF8(IDS_APP_LAUNCH_NOT_SIGNED_IN_LINK,
                                            UTF8ToUTF16(extension->id())));
        chrome::NavigateParams navigate_params(profile, gurl,
                                               content::PAGE_TRANSITION_LINK);
        navigate_params.host_desktop_type = params.desktop_type;
        chrome::Navigate(&navigate_params);
        return NULL;
      }
    }
#endif

    apps::LaunchPlatformAppWithCommandLine(
        profile, extension, params.command_line, params.current_directory);
    return NULL;
  }

  // Record v1 app launch. Platform app launch is recorded when dispatching
  // the onLaunched event.
  prefs->SetLastLaunchTime(extension->id(), base::Time::Now());

  switch (params.container) {
    case extensions::LAUNCH_NONE: {
      NOTREACHED();
      break;
    }
    case extensions::LAUNCH_PANEL:
    case extensions::LAUNCH_WINDOW:
      tab = OpenApplicationWindow(params);
      break;
    case extensions::LAUNCH_TAB: {
      tab = OpenApplicationTab(params);
      break;
    }
    default:
      NOTREACHED();
      break;
  }
  return tab;
}

}  // namespace

AppLaunchParams::AppLaunchParams(Profile* profile,
                                 const extensions::Extension* extension,
                                 extensions::LaunchContainer container,
                                 WindowOpenDisposition disposition)
    : profile(profile),
      extension(extension),
      container(container),
      disposition(disposition),
      desktop_type(chrome::GetActiveDesktop()),
      override_url(),
      override_bounds(),
      command_line(NULL) {}

AppLaunchParams::AppLaunchParams(Profile* profile,
                                 const extensions::Extension* extension,
                                 WindowOpenDisposition disposition)
    : profile(profile),
      extension(extension),
      container(extensions::LAUNCH_NONE),
      disposition(disposition),
      desktop_type(chrome::GetActiveDesktop()),
      override_url(),
      override_bounds(),
      command_line(NULL) {
  ExtensionService* service =
      extensions::ExtensionSystem::Get(profile)->extension_service();
  DCHECK(service);

  // Look up the app preference to find out the right launch container. Default
  // is to launch as a regular tab.
  container = service->extension_prefs()->GetLaunchContainer(
      extension, extensions::ExtensionPrefs::LAUNCH_TYPE_REGULAR);
}

AppLaunchParams::AppLaunchParams(Profile* profile,
                                 const extensions::Extension* extension,
                                 int event_flags,
                                 chrome::HostDesktopType desktop_type)
    : profile(profile),
      extension(extension),
      container(extensions::LAUNCH_NONE),
      disposition(ui::DispositionFromEventFlags(event_flags)),
      desktop_type(desktop_type),
      override_url(),
      override_bounds(),
      command_line(NULL) {
  if (disposition == NEW_FOREGROUND_TAB || disposition == NEW_BACKGROUND_TAB) {
    container = extensions::LAUNCH_TAB;
  } else if (disposition == NEW_WINDOW) {
    container = extensions::LAUNCH_WINDOW;
  } else {
    ExtensionService* service =
        extensions::ExtensionSystem::Get(profile)->extension_service();
    DCHECK(service);

    // Look at preference to find the right launch container.  If no preference
    // is set, launch as a regular tab.
    container = service->extension_prefs()->GetLaunchContainer(
        extension, extensions::ExtensionPrefs::LAUNCH_TYPE_DEFAULT);
    disposition = NEW_FOREGROUND_TAB;
  }
}

WebContents* OpenApplication(const AppLaunchParams& params) {
  return OpenEnabledApplication(params);
}

void OpenApplicationWithReenablePrompt(const AppLaunchParams& params) {
  Profile* profile = params.profile;
  const extensions::Extension* extension = params.extension;

  ExtensionService* service =
      extensions::ExtensionSystem::Get(profile)->extension_service();
  if (!service->IsExtensionEnabled(extension->id())) {
    (new EnableViaAppListFlow(
        service, profile, params.desktop_type, extension->id(),
        base::Bind(base::IgnoreResult(OpenEnabledApplication), params)))->Run();
    return;
  }

  OpenEnabledApplication(params);
}

WebContents* OpenAppShortcutWindow(Profile* profile,
                                   const GURL& url,
                                   const gfx::Rect& override_bounds) {
  AppLaunchParams launch_params(
      profile,
      NULL,  // this is a URL app.  No extension.
      extensions::LAUNCH_WINDOW,
      NEW_WINDOW);
  launch_params.override_url = url;
  launch_params.override_bounds = override_bounds;

  WebContents* tab = OpenApplicationWindow(launch_params);

  if (!tab)
    return NULL;

  // Set UPDATE_SHORTCUT as the pending web app action. This action is picked
  // up in LoadingStateChanged to schedule a GetApplicationInfo. And when
  // the web app info is available, extensions::TabHelper notifies Browser via
  // OnDidGetApplicationInfo, which calls
  // web_app::UpdateShortcutForTabContents when it sees UPDATE_SHORTCUT as
  // pending web app action.
  extensions::TabHelper::FromWebContents(tab)->set_pending_web_app_action(
      extensions::TabHelper::UPDATE_SHORTCUT);

  return tab;
}
