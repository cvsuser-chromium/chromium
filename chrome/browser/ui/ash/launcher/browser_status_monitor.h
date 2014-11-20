// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ASH_LAUNCHER_BROWSER_STATUS_MONITOR_H_
#define CHROME_BROWSER_UI_ASH_LAUNCHER_BROWSER_STATUS_MONITOR_H_

#include <map>
#include <string>

#include "ash/shelf/scoped_observer_with_duplicated_sources.h"
#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/scoped_observer.h"
#include "chrome/browser/ui/ash/launcher/chrome_launcher_controller.h"
#include "chrome/browser/ui/browser_list_observer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/aura/client/activation_change_observer.h"
#include "ui/aura/window_observer.h"
#include "ui/gfx/display_observer.h"

namespace aura {
class Window;

namespace client {
class ActivationClient;
}
}  // namespace aura

class Browser;

// BrowserStatusMonitor monitors creation/deletion of Browser and its
// TabStripModel to keep the launcher representation up to date as the
// active tab changes.
class BrowserStatusMonitor : public aura::client::ActivationChangeObserver,
                             public aura::WindowObserver,
                             public chrome::BrowserListObserver,
                             public gfx::DisplayObserver,
                             public TabStripModelObserver {
 public:
  explicit BrowserStatusMonitor(ChromeLauncherController* launcher_controller);
  virtual ~BrowserStatusMonitor();

  // A function which gets called when the current user has changed.
  // Note that this function is called by the ChromeLauncherController to be
  // able to do the activation in a proper order - rather then setting an
  // observer.
  virtual void ActiveUserChanged(const std::string& user_email) {}

  // A shortcut to call the ChromeLauncherController's UpdateAppState().
  void UpdateAppItemState(content::WebContents* contents,
                          ChromeLauncherController::AppState app_state);

  // A shortcut to call the BrowserShortcutLauncherItemController's
  // UpdateBrowserItemState().
  void UpdateBrowserItemState();

  // aura::client::ActivationChangeObserver overrides:
  virtual void OnWindowActivated(aura::Window* gained_active,
                                 aura::Window* lost_active) OVERRIDE;

  // aura::WindowObserver overrides:
  virtual void OnWindowDestroyed(aura::Window* window) OVERRIDE;

  // chrome::BrowserListObserver overrides:
  virtual void OnBrowserAdded(Browser* browser) OVERRIDE;
  virtual void OnBrowserRemoved(Browser* browser) OVERRIDE;

  // gfx::DisplayObserver overrides:
  virtual void OnDisplayBoundsChanged(const gfx::Display& display) OVERRIDE;
  virtual void OnDisplayAdded(const gfx::Display& new_display) OVERRIDE;
  virtual void OnDisplayRemoved(const gfx::Display& old_display) OVERRIDE;

  // TabStripModelObserver overrides:
  virtual void ActiveTabChanged(content::WebContents* old_contents,
                                content::WebContents* new_contents,
                                int index,
                                int reason) OVERRIDE;
  virtual void TabReplacedAt(TabStripModel* tab_strip_model,
                             content::WebContents* old_contents,
                             content::WebContents* new_contents,
                             int index) OVERRIDE;
  virtual void TabInsertedAt(content::WebContents* contents,
                             int index,
                             bool foreground) OVERRIDE;
  virtual void TabClosingAt(TabStripModel* tab_strip_mode,
                            content::WebContents* contents,
                            int index) OVERRIDE;

 protected:
  // Add a V1 application to the shelf. This can get overwritten for multi
  // profile implementations.
  virtual void AddV1AppToShelf(Browser* browser);

  // Remove a V1 application from the shelf. This can get overwritten for multi
  // profile implementations.
  virtual void RemoveV1AppFromShelf(Browser* browser);

  // Check if V1 application is currently in the shelf.
  bool IsV1AppInShelf(Browser* browser);

 private:
  // This class monitors the WebContent of the all tab and notifies a navigation
  // to the BrowserStatusMonitor.
  class LocalWebContentsObserver : public content::WebContentsObserver {
   public:
    LocalWebContentsObserver(content::WebContents* contents,
                             BrowserStatusMonitor* monitor);
    virtual ~LocalWebContentsObserver();

    // content::WebContentsObserver overrides:
    virtual void DidNavigateMainFrame(
        const content::LoadCommittedDetails& details,
        const content::FrameNavigateParams& params) OVERRIDE;

   private:
    BrowserStatusMonitor* monitor_;

    DISALLOW_COPY_AND_ASSIGN(LocalWebContentsObserver);
  };

  typedef std::map<Browser*, std::string> BrowserToAppIDMap;
  typedef std::map<content::WebContents*, LocalWebContentsObserver*>
      WebContentsToObserverMap;

  // Create LocalWebContentsObserver for |contents|.
  void AddWebContentsObserver(content::WebContents* contents);

  // Remove LocalWebContentsObserver for |contents|.
  void RemoveWebContentsObserver(content::WebContents* contents);

  ChromeLauncherController* launcher_controller_;

  // Hold all observed activation clients.
  ScopedObserverWithDuplicatedSources<aura::client::ActivationClient,
      aura::client::ActivationChangeObserver> observed_activation_clients_;

  // Hold all observed root windows.
  ScopedObserver<aura::Window, aura::WindowObserver> observed_root_windows_;

  BrowserToAppIDMap browser_to_app_id_map_;

  WebContentsToObserverMap webcontents_to_observer_map_;

  DISALLOW_COPY_AND_ASSIGN(BrowserStatusMonitor);
};

#endif  // CHROME_BROWSER_UI_ASH_LAUNCHER_BROWSER_STATUS_MONITOR_H_
