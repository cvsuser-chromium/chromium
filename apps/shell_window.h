// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_SHELL_WINDOW_H_
#define APPS_SHELL_WINDOW_H_

#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/extensions/extension_icon_image.h"
#include "chrome/browser/extensions/extension_keybinding_registry.h"
#include "chrome/browser/sessions/session_id.h"
#include "components/web_modal/web_contents_modal_dialog_manager_delegate.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/common/console_message_level.h"
#include "ui/base/ui_base_types.h"  // WindowShowState
#include "ui/gfx/image/image.h"
#include "ui/gfx/rect.h"

class GURL;
class Profile;
class SkRegion;

namespace content {
class WebContents;
}

namespace extensions {
class Extension;
class PlatformAppBrowserTest;
class WindowController;

struct DraggableRegion;
}

namespace ui {
class BaseWindow;
}

namespace apps {

class NativeAppWindow;

// Manages the web contents for Shell Windows. The implementation for this
// class should create and maintain the WebContents for the window, and handle
// any message passing between the web contents and the extension system or
// native window.
class ShellWindowContents {
 public:
  ShellWindowContents() {}
  virtual ~ShellWindowContents() {}

  // Called to initialize the WebContents, before the app window is created.
  virtual void Initialize(Profile* profile, const GURL& url) = 0;

  // Called to load the contents, after the app window is created.
  virtual void LoadContents(int32 creator_process_id) = 0;

  // Called when the native window changes.
  virtual void NativeWindowChanged(NativeAppWindow* native_app_window) = 0;

  // Called when the native window closes.
  virtual void NativeWindowClosed() = 0;

  virtual content::WebContents* GetWebContents() const = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(ShellWindowContents);
};

// ShellWindow is the type of window used by platform apps. Shell windows
// have a WebContents but none of the chrome of normal browser windows.
class ShellWindow : public content::NotificationObserver,
                    public content::WebContentsDelegate,
                    public content::WebContentsObserver,
                    public web_modal::WebContentsModalDialogManagerDelegate,
                    public extensions::ExtensionKeybindingRegistry::Delegate,
                    public extensions::IconImage::Observer {
 public:
  enum WindowType {
    WINDOW_TYPE_DEFAULT  = 1 << 0,  // Default shell window.
    WINDOW_TYPE_PANEL    = 1 << 1,  // OS controlled panel window (Ash only).
    WINDOW_TYPE_V1_PANEL = 1 << 2,  // For apps v1 support in Ash; deprecate
                                    // with v1 apps.
  };

  enum Frame {
    FRAME_CHROME,  // Chrome-style window frame.
    FRAME_NONE,  // Frameless window.
  };

  class SizeConstraints {
   public:
    // The value SizeConstraints uses to represent an unbounded width or height.
    // This is an enum so that it can be declared inline here.
    enum { kUnboundedSize = 0 };

    SizeConstraints();
    SizeConstraints(const gfx::Size& min_size, const gfx::Size& max_size);
    ~SizeConstraints();

    // Returns the bounds with its size clamped to the min/max size.
    gfx::Size ClampSize(gfx::Size size) const;

    // When gfx::Size is used as a min/max size, a zero represents an unbounded
    // component. This method checks whether either component is specified.
    // Note we can't use gfx::Size::IsEmpty as it returns true if either width
    // or height is zero.
    bool HasMinimumSize() const;
    bool HasMaximumSize() const;

    // This returns true if all components are specified, and min and max are
    // equal.
    bool HasFixedSize() const;

    gfx::Size GetMaximumSize() const;
    gfx::Size GetMinimumSize() const;

    void set_minimum_size(const gfx::Size& min_size);
    void set_maximum_size(const gfx::Size& max_size);

   private:
    gfx::Size minimum_size_;
    gfx::Size maximum_size_;
  };

  struct CreateParams {
    CreateParams();
    ~CreateParams();

    WindowType window_type;
    Frame frame;
    bool transparent_background;  // Only supported on ash.

    // Specify the initial content bounds of the window (excluding any window
    // decorations). INT_MIN designates 'unspecified' for the position
    // components, and 0 for the size components. When unspecified, they should
    // be replaced with a default value.
    gfx::Rect bounds;

    gfx::Size minimum_size;
    gfx::Size maximum_size;

    std::string window_key;

    // The process ID of the process that requested the create.
    int32 creator_process_id;

    // Initial state of the window.
    ui::WindowShowState state;

    // If true, don't show the window after creation.
    bool hidden;

    // If true, the window will be resizable by the user. Defaults to true.
    bool resizable;

    // If true, the window will be focused on creation. Defaults to true.
    bool focused;

    // If true, the window will stay on top of other windows that are not
    // configured to be always on top. Defaults to false.
    bool always_on_top;
  };

  class Delegate {
   public:
    virtual ~Delegate();

    // General initialization.
    virtual void InitWebContents(content::WebContents* web_contents) = 0;
    virtual NativeAppWindow* CreateNativeAppWindow(
        ShellWindow* window,
        const CreateParams& params) = 0;

    // Link handling.
    virtual content::WebContents* OpenURLFromTab(
        Profile* profile,
        content::WebContents* source,
        const content::OpenURLParams& params) = 0;
    virtual void AddNewContents(Profile* profile,
                                content::WebContents* new_contents,
                                WindowOpenDisposition disposition,
                                const gfx::Rect& initial_pos,
                                bool user_gesture,
                                bool* was_blocked) = 0;

    // Feature support.
    virtual content::ColorChooser* ShowColorChooser(
        content::WebContents* web_contents,
        SkColor initial_color) = 0;
    virtual void RunFileChooser(content::WebContents* tab,
                                const content::FileChooserParams& params) = 0;
    virtual void RequestMediaAccessPermission(
        content::WebContents* web_contents,
        const content::MediaStreamRequest& request,
        const content::MediaResponseCallback& callback,
      const extensions::Extension* extension) = 0;
    virtual int PreferredIconSize() = 0;

    // Web contents modal dialog support.
    virtual void SetWebContentsBlocked(content::WebContents* web_contents,
                                       bool blocked) = 0;
    virtual bool IsWebContentsVisible(content::WebContents* web_contents) = 0;
  };

  // Convert draggable regions in raw format to SkRegion format. Caller is
  // responsible for deleting the returned SkRegion instance.
  static SkRegion* RawDraggableRegionsToSkRegion(
      const std::vector<extensions::DraggableRegion>& regions);

  // The constructor and Init methods are public for constructing a ShellWindow
  // with a non-standard render interface (e.g. v1 apps using Ash Panels).
  // Normally ShellWindow::Create should be used.
  // The constructed shell window takes ownership of |delegate|.
  ShellWindow(Profile* profile,
              Delegate* delegate,
              const extensions::Extension* extension);

  // Initializes the render interface, web contents, and native window.
  // |shell_window_contents| will become owned by ShellWindow.
  void Init(const GURL& url,
            ShellWindowContents* shell_window_contents,
            const CreateParams& params);


  const std::string& window_key() const { return window_key_; }
  const SessionID& session_id() const { return session_id_; }
  const extensions::Extension* extension() const { return extension_; }
  const std::string& extension_id() const { return extension_id_; }
  content::WebContents* web_contents() const;
  WindowType window_type() const { return window_type_; }
  bool window_type_is_panel() const {
    return (window_type_ == WINDOW_TYPE_PANEL ||
            window_type_ == WINDOW_TYPE_V1_PANEL);
  }
  Profile* profile() const { return profile_; }
  const gfx::Image& app_icon() const { return app_icon_; }
  const GURL& app_icon_url() { return app_icon_url_; }

  NativeAppWindow* GetBaseWindow();
  gfx::NativeWindow GetNativeWindow();

  // Returns the bounds that should be reported to the renderer.
  gfx::Rect GetClientBounds() const;

  // NativeAppWindows should call this to determine what the window's title
  // is on startup and from within UpdateWindowTitle().
  string16 GetTitle() const;

  // Call to notify ShellRegistry and delete the window. Subclasses should
  // invoke this method instead of using "delete this".
  void OnNativeClose();

  // Should be called by native implementations when the window size, position,
  // or minimized/maximized state has changed.
  void OnNativeWindowChanged();

  // Should be called by native implementations when the window is activated.
  void OnNativeWindowActivated();

  // Specifies a url for the launcher icon.
  void SetAppIconUrl(const GURL& icon_url);

  // Set the region in the window that will accept input events.
  // If |region| is NULL, then the entire window will accept input events.
  void UpdateInputRegion(scoped_ptr<SkRegion> region);

  // Called from the render interface to modify the draggable regions.
  void UpdateDraggableRegions(
      const std::vector<extensions::DraggableRegion>& regions);

  // Updates the app image to |image|. Called internally from the image loader
  // callback. Also called externally for v1 apps using Ash Panels.
  void UpdateAppIcon(const gfx::Image& image);

  // Transitions window into fullscreen, maximized, minimized or restores based
  // on chrome.app.window API.
  void Fullscreen();
  void Maximize();
  void Minimize();
  void Restore();

  // Set the minimum and maximum size that this window is allowed to be.
  void SetMinimumSize(const gfx::Size& min_size);
  void SetMaximumSize(const gfx::Size& max_size);

  enum ShowType {
    SHOW_ACTIVE,
    SHOW_INACTIVE
  };

  // Shows the window if its contents have been painted; otherwise flags the
  // window to be shown as soon as its contents are painted for the first time.
  void Show(ShowType show_type);

  // Hides the window. If the window was previously flagged to be shown on
  // first paint, it will be unflagged.
  void Hide();

  ShellWindowContents* shell_window_contents_for_test() {
    return shell_window_contents_.get();
  }

  // Get the size constraints.
  const SizeConstraints& size_constraints() const {
    return size_constraints_;
  }

 protected:
  virtual ~ShellWindow();

 private:
  // PlatformAppBrowserTest needs access to web_contents()
  friend class extensions::PlatformAppBrowserTest;

  // content::WebContentsDelegate implementation.
  virtual void CloseContents(content::WebContents* contents) OVERRIDE;
  virtual bool ShouldSuppressDialogs() OVERRIDE;
  virtual content::ColorChooser* OpenColorChooser(
      content::WebContents* web_contents, SkColor color) OVERRIDE;
  virtual void RunFileChooser(
      content::WebContents* tab,
      const content::FileChooserParams& params) OVERRIDE;
  virtual bool IsPopupOrPanel(
      const content::WebContents* source) const OVERRIDE;
  virtual void MoveContents(
      content::WebContents* source, const gfx::Rect& pos) OVERRIDE;
  virtual void NavigationStateChanged(const content::WebContents* source,
                                      unsigned changed_flags) OVERRIDE;
  virtual void ToggleFullscreenModeForTab(content::WebContents* source,
                                          bool enter_fullscreen) OVERRIDE;
  virtual bool IsFullscreenForTabOrPending(
      const content::WebContents* source) const OVERRIDE;
  virtual void RequestMediaAccessPermission(
      content::WebContents* web_contents,
      const content::MediaStreamRequest& request,
      const content::MediaResponseCallback& callback) OVERRIDE;
  virtual content::WebContents* OpenURLFromTab(
      content::WebContents* source,
      const content::OpenURLParams& params) OVERRIDE;
  virtual void AddNewContents(content::WebContents* source,
                              content::WebContents* new_contents,
                              WindowOpenDisposition disposition,
                              const gfx::Rect& initial_pos,
                              bool user_gesture,
                              bool* was_blocked) OVERRIDE;
  virtual void HandleKeyboardEvent(
      content::WebContents* source,
      const content::NativeWebKeyboardEvent& event) OVERRIDE;
  virtual void RequestToLockMouse(content::WebContents* web_contents,
                                  bool user_gesture,
                                  bool last_unlocked_by_target) OVERRIDE;

  // content::WebContentsObserver implementation.
  virtual void DidFirstVisuallyNonEmptyPaint(int32 page_id) OVERRIDE;

  // content::NotificationObserver implementation.
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  // web_modal::WebContentsModalDialogManagerDelegate implementation.
  virtual void SetWebContentsBlocked(content::WebContents* web_contents,
                                     bool blocked) OVERRIDE;
  virtual bool IsWebContentsVisible(
      content::WebContents* web_contents) OVERRIDE;

  // Helper method to add a message to the renderer's DevTools console.
  void AddMessageToDevToolsConsole(content::ConsoleMessageLevel level,
                                   const std::string& message);

  // Saves the window geometry/position/screen bounds.
  void SaveWindowPosition();

  // Helper method to adjust the cached bounds so that we can make sure it can
  // be visible on the screen. See http://crbug.com/145752 .
  void AdjustBoundsToBeVisibleOnScreen(
      const gfx::Rect& cached_bounds,
      const gfx::Rect& cached_screen_bounds,
      const gfx::Rect& current_screen_bounds,
      const gfx::Size& minimum_size,
      gfx::Rect* bounds) const;

  // Loads the appropriate default or cached window bounds and constrains them
  // based on screen size and minimum/maximum size. Returns a new CreateParams
  // that should be used to create the window.
  CreateParams LoadDefaultsAndConstrain(CreateParams params) const;

  // Load the app's image, firing a load state change when loaded.
  void UpdateExtensionAppIcon();

  // Called when size_constraints is changed.
  void OnSizeConstraintsChanged();

  // extensions::ExtensionKeybindingRegistry::Delegate implementation.
  virtual extensions::ActiveTabPermissionGranter*
      GetActiveTabPermissionGranter() OVERRIDE;

  // web_modal::WebContentsModalDialogManagerDelegate implementation.
  virtual web_modal::WebContentsModalDialogHost*
      GetWebContentsModalDialogHost() OVERRIDE;

  // Callback from web_contents()->DownloadFavicon.
  void DidDownloadFavicon(int id,
                          int http_status_code,
                          const GURL& image_url,
                          const std::vector<SkBitmap>& bitmaps,
                          const std::vector<gfx::Size>& original_bitmap_sizes);

  // extensions::IconImage::Observer implementation.
  virtual void OnExtensionIconImageChanged(
      extensions::IconImage* image) OVERRIDE;

  Profile* profile_;  // weak pointer - owned by ProfileManager.
  // weak pointer - owned by ExtensionService.
  const extensions::Extension* extension_;
  const std::string extension_id_;

  // Identifier that is used when saving and restoring geometry for this
  // window.
  std::string window_key_;

  const SessionID session_id_;
  WindowType window_type_;
  content::NotificationRegistrar registrar_;

  // Icon shown in the task bar.
  gfx::Image app_icon_;

  // Icon URL to be used for setting the app icon. If not empty, app_icon_ will
  // be fetched and set using this URL.
  GURL app_icon_url_;

  // An object to load the app's icon as an extension resource.
  scoped_ptr<extensions::IconImage> app_icon_image_;

  scoped_ptr<NativeAppWindow> native_app_window_;
  scoped_ptr<ShellWindowContents> shell_window_contents_;
  scoped_ptr<Delegate> delegate_;

  base::WeakPtrFactory<ShellWindow> image_loader_ptr_factory_;

  // Fullscreen entered by app.window api.
  bool fullscreen_for_window_api_;
  // Fullscreen entered by HTML requestFullscreen.
  bool fullscreen_for_tab_;

  // Size constraints on the window.
  SizeConstraints size_constraints_;

  // Show has been called, so the window should be shown once the first visually
  // non-empty paint occurs.
  bool show_on_first_paint_;

  // The first visually non-empty paint has completed.
  bool first_paint_complete_;

  // Whether the delayed Show() call was for an active or inactive window.
  ShowType delayed_show_type_;

  DISALLOW_COPY_AND_ASSIGN(ShellWindow);
};

}  // namespace apps

#endif  // APPS_SHELL_WINDOW_H_
