// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_EXTENSIONS_MEDIA_GALLERIES_DIALOG_VIEWS_H_
#define CHROME_BROWSER_UI_VIEWS_EXTENSIONS_MEDIA_GALLERIES_DIALOG_VIEWS_H_

#include <map>

#include "base/compiler_specific.h"
#include "chrome/browser/media_galleries/media_galleries_dialog_controller.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/window/dialog_delegate.h"

namespace ui {
class MenuModel;
}

namespace views {
class Checkbox;
class LabelButton;
class MenuRunner;
class Widget;
}

// The media galleries configuration view for Views. It will immediately show
// upon construction.
class MediaGalleriesDialogViews : public MediaGalleriesDialog,
                                  public views::ButtonListener,
                                  public views::ContextMenuController,
                                  public views::DialogDelegate {
 public:
  explicit MediaGalleriesDialogViews(
      MediaGalleriesDialogController* controller);
  virtual ~MediaGalleriesDialogViews();

  // MediaGalleriesDialog implementation:
  virtual void UpdateGalleries() OVERRIDE;

  // views::DialogDelegate implementation:
  virtual string16 GetWindowTitle() const OVERRIDE;
  virtual bool ShouldShowWindowTitle() const OVERRIDE;
  virtual void DeleteDelegate() OVERRIDE;
  virtual views::Widget* GetWidget() OVERRIDE;
  virtual const views::Widget* GetWidget() const OVERRIDE;
  virtual views::View* GetContentsView() OVERRIDE;
  virtual string16 GetDialogButtonLabel(ui::DialogButton button) const OVERRIDE;
  virtual bool IsDialogButtonEnabled(ui::DialogButton button) const OVERRIDE;
  virtual ui::ModalType GetModalType() const OVERRIDE;
  virtual views::View* CreateExtraView() OVERRIDE;
  virtual bool Cancel() OVERRIDE;
  virtual bool Accept() OVERRIDE;
  virtual views::NonClientFrameView* CreateNonClientFrameView(
      views::Widget* widget) OVERRIDE;

  // views::ButtonListener implementation:
  virtual void ButtonPressed(views::Button* sender,
                             const ui::Event& event) OVERRIDE;

  // views::ContextMenuController implementation:
  virtual void ShowContextMenuForView(views::View* source,
                                      const gfx::Point& point,
                                      ui::MenuSourceType source_type) OVERRIDE;

 private:
  typedef std::map<MediaGalleryPrefId, views::Checkbox*> CheckboxMap;
  typedef std::map<views::Checkbox*, MediaGalleryPrefInfo> NewCheckboxMap;

  void InitChildViews();

  // Adds a checkbox or updates an existing checkbox. Returns true if a new one
  // was added.
  bool AddOrUpdateGallery(const MediaGalleryPrefInfo& gallery,
                          bool permitted,
                          views::View* container,
                          int trailing_vertical_space);

  void ShowContextMenu(const gfx::Point& point,
                       ui::MenuSourceType source_type,
                       MediaGalleryPrefId id);

  MediaGalleriesDialogController* controller_;

  // The containing window (a weak pointer).
  views::Widget* window_;

  // The contents of the dialog. Owned by |window_|'s RootView.
  views::View* contents_;

  // A map from media gallery ID to views::Checkbox view.
  CheckboxMap checkbox_map_;

  NewCheckboxMap new_checkbox_map_;

  // Pointer to the button to add a new gallery. Owned by parent in
  // the dialog views tree.
  views::LabelButton* add_gallery_button_;

  // This tracks whether the confirm button can be clicked. It starts as false
  // if no checkboxes are ticked. After there is any interaction, or some
  // checkboxes start checked, this will be true.
  bool confirm_available_;

  // True if the user has pressed accept.
  bool accepted_;

  scoped_ptr<views::MenuRunner> context_menu_runner_;

  DISALLOW_COPY_AND_ASSIGN(MediaGalleriesDialogViews);
};

#endif  // CHROME_BROWSER_UI_VIEWS_EXTENSIONS_MEDIA_GALLERIES_DIALOG_VIEWS_H_
