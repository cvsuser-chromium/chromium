// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_GTK_EXTENSIONS_MEDIA_GALLERIES_DIALOG_GTK_H_
#define CHROME_BROWSER_UI_GTK_EXTENSIONS_MEDIA_GALLERIES_DIALOG_GTK_H_

#include <gtk/gtk.h>

#include <map>

#include "base/compiler_specific.h"
#include "base/gtest_prod_util.h"
#include "chrome/browser/media_galleries/media_galleries_dialog_controller.h"
#include "chrome/browser/ui/gtk/constrained_window_gtk.h"
#include "ui/base/gtk/gtk_signal.h"
#include "ui/gfx/scoped_gobject.h"

class MediaGalleriesDialogController;
class MediaGalleriesDialogTest;

// The media galleries configuration view for Gtk. It will immediately show
// upon construction.
class MediaGalleriesDialogGtk : public MediaGalleriesDialog {
 public:
  explicit MediaGalleriesDialogGtk(MediaGalleriesDialogController* controller);
  virtual ~MediaGalleriesDialogGtk();

  // MediaGalleriesDialog implementation:
  virtual void UpdateGalleries() OVERRIDE;

  // Event callbacks.
  CHROMEGTK_CALLBACK_0(MediaGalleriesDialogGtk, void, OnToggled);
  CHROMEGTK_CALLBACK_0(MediaGalleriesDialogGtk, void, OnAddFolder);
  CHROMEGTK_CALLBACK_0(MediaGalleriesDialogGtk, void, OnConfirm);
  CHROMEGTK_CALLBACK_0(MediaGalleriesDialogGtk, void, OnCancel);

 private:
  FRIEND_TEST_ALL_PREFIXES(MediaGalleriesDialogTest, InitializeCheckboxes);
  FRIEND_TEST_ALL_PREFIXES(MediaGalleriesDialogTest, ToggleCheckboxes);
  FRIEND_TEST_ALL_PREFIXES(MediaGalleriesDialogTest, UpdateAdds);
  FRIEND_TEST_ALL_PREFIXES(MediaGalleriesDialogTest, ForgetDeletes);

  typedef std::map<MediaGalleryPrefId, GtkWidget*> CheckboxMap;
  typedef std::map<GtkWidget*, MediaGalleryPrefInfo> NewCheckboxMap;

  // Creates the widget hierarchy.
  void InitWidgets();

  virtual void UpdateGalleryInContainer(const MediaGalleryPrefInfo& gallery,
                                        bool permitted,
                                        GtkWidget* checkbox_container);

  // Updates the state of the confirm button. It will be disabled when
  void UpdateConfirmButtonState();

  CHROMEGTK_CALLBACK_0(MediaGalleriesDialogGtk, void, OnDestroy);

  MediaGalleriesDialogController* controller_;
  GtkWidget* window_;

  // The root widget for the dialog.
  ui::ScopedGObject<GtkWidget>::Type contents_;

  // The confirm button.
  GtkWidget* confirm_;

  // A map from MediaGalleryPrefId to the GtkCheckButton that controls it.
  CheckboxMap checkbox_map_;

  // Map for checkboxes of newly-added galleries to their MediaGalleryPrefInfo.
  NewCheckboxMap new_checkbox_map_;

  // True if the user has pressed accept.
  bool accepted_;

  DISALLOW_COPY_AND_ASSIGN(MediaGalleriesDialogGtk);
};

#endif  // CHROME_BROWSER_UI_GTK_EXTENSIONS_MEDIA_GALLERIES_DIALOG_GTK_H_
