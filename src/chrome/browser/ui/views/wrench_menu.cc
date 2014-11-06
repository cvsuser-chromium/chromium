// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/wrench_menu.h"

#include <algorithm>
#include <cmath>
#include <set>

#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/bookmarks/bookmark_model.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/bookmarks/bookmark_stats.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search/search.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/toolbar/wrench_menu_model.h"
#include "chrome/browser/ui/views/bookmarks/bookmark_menu_delegate.h"
#include "chrome/browser/ui/views/wrench_menu_observer.h"
#include "content/public/browser/host_zoom_map.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/user_metrics.h"
#include "content/public/browser/web_contents.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkPaint.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/layout.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/image/canvas_image_source.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/skia_util.h"
#include "ui/gfx/text_utils.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/button/menu_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_config.h"
#include "ui/views/controls/menu/menu_item_view.h"
#include "ui/views/controls/menu/menu_model_adapter.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/menu/menu_scroll_view_container.h"
#include "ui/views/controls/menu/submenu_view.h"
#include "ui/views/widget/widget.h"

#if defined(USE_AURA)
#include "ui/native_theme/native_theme_aura.h"
#endif

using content::HostZoomMap;
using content::UserMetricsAction;
using content::WebContents;
using ui::MenuModel;
using views::CustomButton;
using views::ImageButton;
using views::Label;
using views::LabelButton;
using views::MenuConfig;
using views::MenuItemView;
using views::View;

namespace {

// Colors used for buttons.
const SkColor kEnabledTouchBackgroundColor = SkColorSetARGB(247, 255, 255, 255);
const SkColor kHoverTouchBackgroundColor = SkColorSetARGB(247, 242, 242, 242);
const SkColor kFocusedTouchBackgroundColor = SkColorSetARGB(247, 235, 235, 235);

const SkColor kTouchButtonText = 0xff5a5a5a;

// Horizontal padding on the edges of the buttons.
const int kHorizontalPadding = 6;
// Horizontal padding for a touch enabled menu.
const int kHorizontalTouchPadding = 15;

// Menu items which have embedded buttons should have this height in pixel.
const int kMenuItemContainingButtonsHeight = 43;

// Returns true if |command_id| identifies a bookmark menu item.
bool IsBookmarkCommand(int command_id) {
  return command_id >= WrenchMenuModel::kMinBookmarkCommandId &&
      command_id <= WrenchMenuModel::kMaxBookmarkCommandId;
}

// Returns true if |command_id| identifies a recent tabs menu item.
bool IsRecentTabsCommand(int command_id) {
  return command_id >= WrenchMenuModel::kMinRecentTabsCommandId &&
      command_id <= WrenchMenuModel::kMaxRecentTabsCommandId;
}

// Subclass of ImageButton whose preferred size includes the size of the border.
class FullscreenButton : public ImageButton {
 public:
  explicit FullscreenButton(views::ButtonListener* listener)
      : ImageButton(listener) { }

  // Overridden from ImageButton.
  virtual gfx::Size GetPreferredSize() OVERRIDE {
    gfx::Size pref = ImageButton::GetPreferredSize();
    if (border()) {
      gfx::Insets insets = border()->GetInsets();
      pref.Enlarge(insets.width(), insets.height());
    }
    return pref;
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(FullscreenButton);
};

// Border for buttons contained in the menu. This is only used for getting the
// insets, the actual painting is done in MenuButtonBackground.
class MenuButtonBorder : public views::Border {
 public:
  MenuButtonBorder(const MenuConfig& config, bool use_new_menu)
      : horizontal_padding_(use_new_menu ?
                            kHorizontalTouchPadding : kHorizontalPadding),
        insets_(config.item_top_margin, horizontal_padding_,
                config.item_bottom_margin, horizontal_padding_) {
  }

  // Overridden from views::Border.
  virtual void Paint(const View& view, gfx::Canvas* canvas) OVERRIDE {
    // Painting of border is done in MenuButtonBackground.
  }

  virtual gfx::Insets GetInsets() const OVERRIDE {
    return insets_;
  }

 private:
  // The horizontal padding dependent on the layout.
  const int horizontal_padding_;

  const gfx::Insets insets_;

  DISALLOW_COPY_AND_ASSIGN(MenuButtonBorder);
};

// Combination border/background for the buttons contained in the menu. The
// painting of the border/background is done here as TextButton does not always
// paint the border.
class MenuButtonBackground : public views::Background {
 public:
  enum ButtonType {
    LEFT_BUTTON,
    CENTER_BUTTON,
    RIGHT_BUTTON,
    SINGLE_BUTTON,
  };

  MenuButtonBackground(ButtonType type, bool use_new_menu)
      : type_(type),
        use_new_menu_(use_new_menu),
        left_button_(NULL),
        right_button_(NULL) {}

  // Used when the type is CENTER_BUTTON to determine if the left/right edge
  // needs to be rendered selected.
  void SetOtherButtons(CustomButton* left_button, CustomButton* right_button) {
    if (base::i18n::IsRTL()) {
      left_button_ = right_button;
      right_button_ = left_button;
    } else {
      left_button_ = left_button;
      right_button_ = right_button;
    }
  }

  // Overridden from views::Background.
  virtual void Paint(gfx::Canvas* canvas, View* view) const OVERRIDE {
    CustomButton::ButtonState state =
        (!strcmp(view->GetClassName(), views::Label::kViewClassName)) ?
        CustomButton::STATE_NORMAL : static_cast<CustomButton*>(view)->state();
    int w = view->width();
    int h = view->height();
#if defined(USE_AURA)
    if (use_new_menu_ &&
        view->GetNativeTheme() == ui::NativeThemeAura::instance()) {
      // Normal buttons get a border drawn on the right side and the rest gets
      // filled in. The left button however does not get a line to combine
      // buttons.
      int border = 0;
      if (type_ != RIGHT_BUTTON) {
        border = 1;
        canvas->FillRect(gfx::Rect(0, 0, border, h),
                         BorderColor(view, CustomButton::STATE_NORMAL));
      }
      canvas->FillRect(gfx::Rect(border, 0, w - border, h),
                       touch_background_color(state));
      return;
    }
#endif
    const SkColor background = BackgroundColor(view, state);
    const SkColor border = BorderColor(view, state);
    switch (TypeAdjustedForRTL()) {
      case LEFT_BUTTON:
        canvas->FillRect(gfx::Rect(1, 1, w, h - 2), background);
        canvas->FillRect(gfx::Rect(2, 0, w, 1), border);
        canvas->FillRect(gfx::Rect(1, 1, 1, 1), border);
        canvas->FillRect(gfx::Rect(0, 2, 1, h - 4), border);
        canvas->FillRect(gfx::Rect(1, h - 2, 1, 1), border);
        canvas->FillRect(gfx::Rect(2, h - 1, w, 1), border);
        break;

      case CENTER_BUTTON: {
        canvas->FillRect(gfx::Rect(1, 1, w - 2, h - 2), background);
        SkColor left_color = state != CustomButton::STATE_NORMAL ?
            border : BorderColor(view, left_button_->state());
        canvas->FillRect(gfx::Rect(0, 0, 1, h), left_color);
        canvas->FillRect(gfx::Rect(1, 0, w - 2, 1), border);
        canvas->FillRect(gfx::Rect(1, h - 1, w - 2, 1),
                         border);
        SkColor right_color = state != CustomButton::STATE_NORMAL ?
            border : BorderColor(view, right_button_->state());
        canvas->FillRect(gfx::Rect(w - 1, 0, 1, h), right_color);
        break;
      }

      case RIGHT_BUTTON:
        canvas->FillRect(gfx::Rect(0, 1, w - 1, h - 2), background);
        canvas->FillRect(gfx::Rect(0, 0, w - 2, 1), border);
        canvas->FillRect(gfx::Rect(w - 2, 1, 1, 1), border);
        canvas->FillRect(gfx::Rect(w - 1, 2, 1, h - 4), border);
        canvas->FillRect(gfx::Rect(w - 2, h - 2, 1, 1), border);
        canvas->FillRect(gfx::Rect(0, h - 1, w - 2, 1), border);
        break;

      case SINGLE_BUTTON:
        canvas->FillRect(gfx::Rect(1, 1, w - 2, h - 2), background);
        canvas->FillRect(gfx::Rect(2, 0, w - 4, 1), border);
        canvas->FillRect(gfx::Rect(1, 1, 1, 1), border);
        canvas->FillRect(gfx::Rect(0, 2, 1, h - 4), border);
        canvas->FillRect(gfx::Rect(1, h - 2, 1, 1), border);
        canvas->FillRect(gfx::Rect(2, h - 1, w - 4, 1), border);
        canvas->FillRect(gfx::Rect(w - 2, 1, 1, 1), border);
        canvas->FillRect(gfx::Rect(w - 1, 2, 1, h - 4), border);
        canvas->FillRect(gfx::Rect(w - 2, h - 2, 1, 1), border);
        break;

      default:
        NOTREACHED();
        break;
    }
  }

 private:
  static SkColor BorderColor(View* view, CustomButton::ButtonState state) {
    ui::NativeTheme* theme = view->GetNativeTheme();
    switch (state) {
      case CustomButton::STATE_HOVERED:
        return theme->GetSystemColor(
            ui::NativeTheme::kColorId_HoverMenuButtonBorderColor);
      case CustomButton::STATE_PRESSED:
        return theme->GetSystemColor(
            ui::NativeTheme::kColorId_FocusedMenuButtonBorderColor);
      default:
        return theme->GetSystemColor(
            ui::NativeTheme::kColorId_EnabledMenuButtonBorderColor);
    }
  }

  static SkColor BackgroundColor(View* view, CustomButton::ButtonState state) {
    ui::NativeTheme* theme = view->GetNativeTheme();
    switch (state) {
      case CustomButton::STATE_HOVERED:
        return theme->GetSystemColor(
            ui::NativeTheme::kColorId_HoverMenuItemBackgroundColor);
      case CustomButton::STATE_PRESSED:
        return theme->GetSystemColor(
            ui::NativeTheme::kColorId_FocusedMenuItemBackgroundColor);
      default:
        return theme->GetSystemColor(
            ui::NativeTheme::kColorId_MenuBackgroundColor);
    }
  }

  static SkColor touch_background_color(CustomButton::ButtonState state) {
    switch (state) {
      case CustomButton::STATE_HOVERED: return kHoverTouchBackgroundColor;
      case CustomButton::STATE_PRESSED: return kFocusedTouchBackgroundColor;
      default:                          return kEnabledTouchBackgroundColor;
    }
  }

  ButtonType TypeAdjustedForRTL() const {
    if (!base::i18n::IsRTL())
      return type_;

    switch (type_) {
      case LEFT_BUTTON:   return RIGHT_BUTTON;
      case RIGHT_BUTTON:  return LEFT_BUTTON;
      default:            break;
    }
    return type_;
  }

  const ButtonType type_;
  const bool use_new_menu_;

  // See description above setter for details.
  CustomButton* left_button_;
  CustomButton* right_button_;

  DISALLOW_COPY_AND_ASSIGN(MenuButtonBackground);
};

string16 GetAccessibleNameForWrenchMenuItem(
      MenuModel* model, int item_index, int accessible_string_id) {
  string16 accessible_name = l10n_util::GetStringUTF16(accessible_string_id);
  string16 accelerator_text;

  ui::Accelerator menu_accelerator;
  if (model->GetAcceleratorAt(item_index, &menu_accelerator)) {
    accelerator_text =
        ui::Accelerator(menu_accelerator.key_code(),
                        menu_accelerator.modifiers()).GetShortcutText();
  }

  return MenuItemView::GetAccessibleNameForMenuItem(
      accessible_name, accelerator_text);
}

// WrenchMenuView is a view that can contain label buttons.
class WrenchMenuView : public views::View,
                       public views::ButtonListener,
                       public WrenchMenuObserver {
 public:
  WrenchMenuView(WrenchMenu* menu, MenuModel* menu_model)
      : menu_(menu),
        menu_model_(menu_model) {
    menu_->AddObserver(this);
  }

  virtual ~WrenchMenuView() {
    if (menu_)
      menu_->RemoveObserver(this);
  }

  // Overridden from views::View.
  virtual void SchedulePaintInRect(const gfx::Rect& r) OVERRIDE {
    // Normally when the mouse enters/exits a button the buttons invokes
    // SchedulePaint. As part of the button border (MenuButtonBackground) is
    // rendered by the button to the left/right of it SchedulePaint on the the
    // button may not be enough, so this forces a paint all.
    View::SchedulePaintInRect(gfx::Rect(size()));
  }

  LabelButton* CreateAndConfigureButton(int string_id,
                                       MenuButtonBackground::ButtonType type,
                                       int index,
                                       MenuButtonBackground** background) {
    return CreateButtonWithAccName(
      string_id, type, index, background, string_id);
  }

  LabelButton* CreateButtonWithAccName(int string_id,
                                       MenuButtonBackground::ButtonType type,
                                       int index,
                                       MenuButtonBackground** background,
                                       int acc_string_id) {
    // Should only be invoked during construction when |menu_| is valid.
    DCHECK(menu_);
    LabelButton* button = new LabelButton(this, gfx::RemoveAcceleratorChar(
        l10n_util::GetStringUTF16(string_id), '&', NULL, NULL));
    button->SetAccessibleName(
        GetAccessibleNameForWrenchMenuItem(menu_model_, index, acc_string_id));
    button->set_focusable(true);
    button->set_request_focus_on_press(false);
    button->set_tag(index);
    button->SetEnabled(menu_model_->IsEnabledAt(index));
    MenuButtonBackground* bg =
        new MenuButtonBackground(type, menu_->use_new_menu());
    button->set_background(bg);
    const MenuConfig& menu_config = menu_->GetMenuConfig();
    button->SetTextColor(views::Button::STATE_NORMAL, menu_config.text_color);
    if (background)
      *background = bg;
    button->set_border(
        new MenuButtonBorder(menu_config, menu_->use_new_menu()));
    button->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    button->SetFont(menu_config.font);
    AddChildView(button);
    return button;
  }

  // Overridden from WrenchMenuObserver:
  virtual void WrenchMenuDestroyed() OVERRIDE {
    menu_->RemoveObserver(this);
    menu_ = NULL;
    menu_model_ = NULL;
  }

 protected:
  WrenchMenu* menu() { return menu_; }
  MenuModel* menu_model() { return menu_model_; }

 private:
  // Hosting WrenchMenu.
  // WARNING: this may be NULL during shutdown.
  WrenchMenu* menu_;

  // The menu model containing the increment/decrement/reset items.
  // WARNING: this may be NULL during shutdown.
  MenuModel* menu_model_;

  DISALLOW_COPY_AND_ASSIGN(WrenchMenuView);
};

class ButtonContainerMenuItemView : public MenuItemView {
 public:
  // Constructor for use with button containing menu items which have a
  // different height then normal items.
  ButtonContainerMenuItemView(MenuItemView* parent, int command_id, int height)
      : MenuItemView(parent, command_id, MenuItemView::NORMAL),
        height_(height) {
  };

  // Overridden from MenuItemView.
  virtual gfx::Size GetChildPreferredSize() OVERRIDE {
    gfx::Size size = MenuItemView::GetChildPreferredSize();
    // When there is a height override given, we need to deduct our spacing
    // above and below to get to the correct height to return here for the
    // child item.
    int height = height_ - GetTopMargin() - GetBottomMargin();
    if (height > size.height())
      size.set_height(height);
    return size;
  }

 private:
  int height_;

  DISALLOW_COPY_AND_ASSIGN(ButtonContainerMenuItemView);
};

}  // namespace

// CutCopyPasteView ------------------------------------------------------------

// CutCopyPasteView is the view containing the cut/copy/paste buttons.
class WrenchMenu::CutCopyPasteView : public WrenchMenuView {
 public:
  CutCopyPasteView(WrenchMenu* menu,
                   MenuModel* menu_model,
                   const ui::NativeTheme* native_theme,
                   int cut_index,
                   int copy_index,
                   int paste_index)
      : WrenchMenuView(menu, menu_model) {
    LabelButton* cut = CreateAndConfigureButton(
        IDS_CUT, MenuButtonBackground::LEFT_BUTTON, cut_index, NULL);

    MenuButtonBackground* copy_background = NULL;
    LabelButton* copy = CreateAndConfigureButton(
        IDS_COPY, MenuButtonBackground::CENTER_BUTTON, copy_index,
        &copy_background);

    LabelButton* paste = CreateAndConfigureButton(
        IDS_PASTE,
        menu->use_new_menu() && menu->supports_new_separators_ ?
            MenuButtonBackground::CENTER_BUTTON :
            MenuButtonBackground::RIGHT_BUTTON,
        paste_index,
        NULL);
    if (menu->use_new_menu()) {
      cut->SetTextColor(views::Button::STATE_NORMAL, kTouchButtonText);
      copy->SetTextColor(views::Button::STATE_NORMAL, kTouchButtonText);
      paste->SetTextColor(views::Button::STATE_NORMAL, kTouchButtonText);
    } else {
      SkColor text_color = native_theme->GetSystemColor(
          ui::NativeTheme::kColorId_EnabledMenuItemForegroundColor);
      cut->SetTextColor(views::Button::STATE_NORMAL, text_color);
      copy->SetTextColor(views::Button::STATE_NORMAL, text_color);
      paste->SetTextColor(views::Button::STATE_NORMAL, text_color);
    }
    copy_background->SetOtherButtons(cut, paste);
  }

  // Overridden from View.
  virtual gfx::Size GetPreferredSize() OVERRIDE {
    // Returned height doesn't matter as MenuItemView forces everything to the
    // height of the menuitemview.
    return gfx::Size(GetMaxChildViewPreferredWidth() * child_count(), 0);
  }

  virtual void Layout() OVERRIDE {
    // All buttons are given the same width.
    int width = GetMaxChildViewPreferredWidth();
    for (int i = 0; i < child_count(); ++i)
      child_at(i)->SetBounds(i * width, 0, width, height());
  }

  // Overridden from ButtonListener.
  virtual void ButtonPressed(views::Button* sender,
                             const ui::Event& event) OVERRIDE {
    menu()->CancelAndEvaluate(menu_model(), sender->tag());
  }

 private:
  // Returns the max preferred width of all the children.
  int GetMaxChildViewPreferredWidth() {
    int width = 0;
    for (int i = 0; i < child_count(); ++i)
      width = std::max(width, child_at(i)->GetPreferredSize().width());
    return width;
  }

  DISALLOW_COPY_AND_ASSIGN(CutCopyPasteView);
};

// ZoomView --------------------------------------------------------------------

// Padding between the increment buttons and the reset button.
static const int kZoomPadding = 6;
static const int kTouchZoomPadding = 14;

// ZoomView contains the various zoom controls: two buttons to increase/decrease
// the zoom, a label showing the current zoom percent, and a button to go
// full-screen.
class WrenchMenu::ZoomView : public WrenchMenuView {
 public:
  ZoomView(WrenchMenu* menu,
           MenuModel* menu_model,
           const ui::NativeTheme* native_theme,
           int decrement_index,
           int increment_index,
           int fullscreen_index)
      : WrenchMenuView(menu, menu_model),
        fullscreen_index_(fullscreen_index),
        increment_button_(NULL),
        zoom_label_(NULL),
        decrement_button_(NULL),
        fullscreen_button_(NULL),
        zoom_label_width_(0) {
    zoom_subscription_ = HostZoomMap::GetForBrowserContext(
        menu->browser_->profile())->AddZoomLevelChangedCallback(
            base::Bind(&WrenchMenu::ZoomView::OnZoomLevelChanged,
                       base::Unretained(this)));

    decrement_button_ = CreateButtonWithAccName(
        IDS_ZOOM_MINUS2, MenuButtonBackground::LEFT_BUTTON, decrement_index,
        NULL, IDS_ACCNAME_ZOOM_MINUS2);

    zoom_label_ = new Label(
        l10n_util::GetStringFUTF16Int(IDS_ZOOM_PERCENT, 100));
    zoom_label_->SetAutoColorReadabilityEnabled(false);
    zoom_label_->SetHorizontalAlignment(gfx::ALIGN_RIGHT);

    MenuButtonBackground* center_bg = new MenuButtonBackground(
        menu->use_new_menu() && menu->supports_new_separators_ ?
            MenuButtonBackground::RIGHT_BUTTON :
            MenuButtonBackground::CENTER_BUTTON,
        menu->use_new_menu());
    zoom_label_->set_background(center_bg);
    const MenuConfig& menu_config(menu->GetMenuConfig());
    zoom_label_->set_border(
        new MenuButtonBorder(menu_config, menu->use_new_menu()));
    zoom_label_->SetFont(menu_config.font);

    AddChildView(zoom_label_);
    zoom_label_width_ = MaxWidthForZoomLabel();

    increment_button_ = CreateButtonWithAccName(
        IDS_ZOOM_PLUS2, MenuButtonBackground::RIGHT_BUTTON, increment_index,
        NULL, IDS_ACCNAME_ZOOM_PLUS2);

    center_bg->SetOtherButtons(decrement_button_, increment_button_);

    fullscreen_button_ = new FullscreenButton(this);
    gfx::ImageSkia* full_screen_image =
        ui::ResourceBundle::GetSharedInstance().GetImageSkiaNamed(
            IDR_FULLSCREEN_MENU_BUTTON);
    fullscreen_button_->SetImage(ImageButton::STATE_NORMAL, full_screen_image);
    if (menu->use_new_menu()) {
      zoom_label_->SetEnabledColor(kTouchButtonText);
      decrement_button_->SetTextColor(views::Button::STATE_NORMAL,
                                      kTouchButtonText);
      increment_button_->SetTextColor(views::Button::STATE_NORMAL,
                                      kTouchButtonText);
    } else {
      SkColor enabled_text_color = native_theme->GetSystemColor(
          ui::NativeTheme::kColorId_EnabledMenuItemForegroundColor);
      zoom_label_->SetEnabledColor(enabled_text_color);
      decrement_button_->SetTextColor(views::Button::STATE_NORMAL,
                                      enabled_text_color);
      increment_button_->SetTextColor(views::Button::STATE_NORMAL,
                                      enabled_text_color);
      SkColor disabled_text_color = native_theme->GetSystemColor(
          ui::NativeTheme::kColorId_DisabledMenuItemForegroundColor);
      decrement_button_->SetTextColor(views::Button::STATE_DISABLED,
                                      disabled_text_color);
      increment_button_->SetTextColor(views::Button::STATE_DISABLED,
                                      disabled_text_color);
    }

    fullscreen_button_->set_focusable(true);
    fullscreen_button_->set_request_focus_on_press(false);
    fullscreen_button_->set_tag(fullscreen_index);
    fullscreen_button_->SetImageAlignment(
        ImageButton::ALIGN_CENTER, ImageButton::ALIGN_MIDDLE);
    int horizontal_padding =
        menu->use_new_menu() ? kHorizontalTouchPadding : kHorizontalPadding;
    fullscreen_button_->set_border(views::Border::CreateEmptyBorder(
        0, horizontal_padding, 0, horizontal_padding));
    fullscreen_button_->set_background(
        new MenuButtonBackground(MenuButtonBackground::SINGLE_BUTTON,
                                 menu->use_new_menu()));
    fullscreen_button_->SetAccessibleName(
        GetAccessibleNameForWrenchMenuItem(
            menu_model, fullscreen_index, IDS_ACCNAME_FULLSCREEN));
    AddChildView(fullscreen_button_);

    UpdateZoomControls();
  }

  virtual ~ZoomView() {}

  // Overridden from View.
  virtual gfx::Size GetPreferredSize() OVERRIDE {
    // The increment/decrement button are forced to the same width.
    int button_width = std::max(increment_button_->GetPreferredSize().width(),
                                decrement_button_->GetPreferredSize().width());
    int zoom_padding = menu()->use_new_menu() ?
        kTouchZoomPadding : kZoomPadding;
    int fullscreen_width = fullscreen_button_->GetPreferredSize().width() +
                           zoom_padding;
    // Returned height doesn't matter as MenuItemView forces everything to the
    // height of the menuitemview. Note that we have overridden the height when
    // constructing the menu.
    return gfx::Size(button_width + zoom_label_width_ + button_width +
                     fullscreen_width, 0);
  }

  virtual void Layout() OVERRIDE {
    int x = 0;
    int button_width = std::max(increment_button_->GetPreferredSize().width(),
                                decrement_button_->GetPreferredSize().width());
    gfx::Rect bounds(0, 0, button_width, height());

    decrement_button_->SetBoundsRect(bounds);

    x += bounds.width();
    bounds.set_x(x);
    bounds.set_width(zoom_label_width_);
    zoom_label_->SetBoundsRect(bounds);

    x += bounds.width();
    bounds.set_x(x);
    bounds.set_width(button_width);
    increment_button_->SetBoundsRect(bounds);

    x += bounds.width() + (menu()->use_new_menu() ? 0 : kZoomPadding);
    bounds.set_x(x);
    bounds.set_width(fullscreen_button_->GetPreferredSize().width() +
                     (menu()->use_new_menu() ? kTouchZoomPadding : 0));
    fullscreen_button_->SetBoundsRect(bounds);
  }

  // Overridden from ButtonListener.
  virtual void ButtonPressed(views::Button* sender,
                             const ui::Event& event) OVERRIDE {
    if (sender->tag() == fullscreen_index_) {
      menu()->CancelAndEvaluate(menu_model(), sender->tag());
    } else {
      // Zoom buttons don't close the menu.
      menu_model()->ActivatedAt(sender->tag());
    }
  }

  // Overridden from WrenchMenuObserver.
  virtual void WrenchMenuDestroyed() OVERRIDE {
    WrenchMenuView::WrenchMenuDestroyed();
  }

 private:
  void OnZoomLevelChanged(const HostZoomMap::ZoomLevelChange& change) {
    UpdateZoomControls();
  }

  void UpdateZoomControls() {
    bool enable_increment = false;
    bool enable_decrement = false;
    WebContents* selected_tab =
        menu()->browser_->tab_strip_model()->GetActiveWebContents();
    int zoom = 100;
    if (selected_tab)
      zoom = selected_tab->GetZoomPercent(&enable_increment, &enable_decrement);
    increment_button_->SetEnabled(enable_increment);
    decrement_button_->SetEnabled(enable_decrement);
    zoom_label_->SetText(
        l10n_util::GetStringFUTF16Int(IDS_ZOOM_PERCENT, zoom));

    zoom_label_width_ = MaxWidthForZoomLabel();
  }

  // Calculates the max width the zoom string can be.
  int MaxWidthForZoomLabel() {
    gfx::Font font = zoom_label_->font();
    int border_width =
        zoom_label_->border() ? zoom_label_->border()->GetInsets().width() : 0;

    int max_w = 0;

    WebContents* selected_tab =
        menu()->browser_->tab_strip_model()->GetActiveWebContents();
    if (selected_tab) {
      int min_percent = selected_tab->GetMinimumZoomPercent();
      int max_percent = selected_tab->GetMaximumZoomPercent();

      int step = (max_percent - min_percent) / 10;
      for (int i = min_percent; i <= max_percent; i += step) {
        int w = font.GetStringWidth(
            l10n_util::GetStringFUTF16Int(IDS_ZOOM_PERCENT, i));
        max_w = std::max(w, max_w);
      }
    } else {
      max_w = font.GetStringWidth(
          l10n_util::GetStringFUTF16Int(IDS_ZOOM_PERCENT, 100));
    }

    return max_w + border_width;
  }

  // Index of the fullscreen menu item in the model.
  const int fullscreen_index_;

  scoped_ptr<content::HostZoomMap::Subscription> zoom_subscription_;
  content::NotificationRegistrar registrar_;

  // Button for incrementing the zoom.
  LabelButton* increment_button_;

  // Label showing zoom as a percent.
  Label* zoom_label_;

  // Button for decrementing the zoom.
  LabelButton* decrement_button_;

  ImageButton* fullscreen_button_;

  // Width given to |zoom_label_|. This is the width at 100%.
  int zoom_label_width_;

  DISALLOW_COPY_AND_ASSIGN(ZoomView);
};

// RecentTabsMenuModelDelegate  ------------------------------------------------

// Provides the ui::MenuModelDelegate implementation for RecentTabsSubMenuModel
// items.
class WrenchMenu::RecentTabsMenuModelDelegate : public ui::MenuModelDelegate {
 public:
  RecentTabsMenuModelDelegate(WrenchMenu* wrench_menu,
                              ui::MenuModel* model,
                              views::MenuItemView* menu_item)
      : wrench_menu_(wrench_menu),
        model_(model),
        menu_item_(menu_item) {
    model_->SetMenuModelDelegate(this);
  }

  virtual ~RecentTabsMenuModelDelegate() {
    model_->SetMenuModelDelegate(NULL);
  }

  // Return the specific menu width of recent tabs submenu if |menu| is the
  // recent tabs submenu, else return -1.
  int GetMaxWidthForMenu(views::MenuItemView* menu) {
    if (!menu_item_->HasSubmenu())
      return -1;
    const int kMaxMenuItemWidth = 320;
    return menu->GetCommand() == menu_item_->GetCommand() ?
        kMaxMenuItemWidth : -1;
  }

  const gfx::Font* GetLabelFontAt(int index) const {
    return model_->GetLabelFontAt(index);
  }

  bool GetForegroundColorAt(int index,
                            bool is_hovered,
                            SkColor* override_color) const {
    // The items for which we get a font, should be shown in black.
    if (GetLabelFontAt(index)) {
      *override_color = SK_ColorBLACK;
      return true;
    }
    return false;
  }

  // ui::MenuModelDelegate implementation:

  virtual void OnIconChanged(int index) OVERRIDE {
    int command_id = model_->GetCommandIdAt(index);
    views::MenuItemView* item = menu_item_->GetMenuItemByID(command_id);
    DCHECK(item);
    gfx::Image icon;
    model_->GetIconAt(index, &icon);
    item->SetIcon(*icon.ToImageSkia());
  }

  virtual void OnMenuStructureChanged() OVERRIDE {
    if (menu_item_->HasSubmenu()) {
      // Remove all menu items from submenu.
      views::SubmenuView* submenu = menu_item_->GetSubmenu();
      while (submenu->child_count() > 0)
        menu_item_->RemoveMenuItemAt(submenu->child_count() - 1);

      // Remove all elements in |WrenchMenu::command_id_to_entry_| that map to
      // |model_|.
      WrenchMenu::CommandIDToEntry::iterator iter =
          wrench_menu_->command_id_to_entry_.begin();
      while (iter != wrench_menu_->command_id_to_entry_.end()) {
        if (iter->second.first == model_)
          wrench_menu_->command_id_to_entry_.erase(iter++);
        else
          ++iter;
      }
    }

    // Add all menu items from |model| to submenu.
    for (int i = 0; i < model_->GetItemCount(); ++i) {
      wrench_menu_->AddMenuItem(menu_item_, i, model_, i, model_->GetTypeAt(i),
                                0);
    }

    // In case recent tabs submenu was open when items were changing, force a
    // ChildrenChanged().
    menu_item_->ChildrenChanged();
  }

 private:
  WrenchMenu* wrench_menu_;
  ui::MenuModel* model_;
  views::MenuItemView* menu_item_;

  DISALLOW_COPY_AND_ASSIGN(RecentTabsMenuModelDelegate);
};

// WrenchMenu ------------------------------------------------------------------

WrenchMenu::WrenchMenu(Browser* browser,
                       bool use_new_menu,
                       bool supports_new_separators)
    : root_(NULL),
      browser_(browser),
      selected_menu_model_(NULL),
      selected_index_(0),
      bookmark_menu_(NULL),
      feedback_menu_item_(NULL),
      use_new_menu_(use_new_menu),
      supports_new_separators_(supports_new_separators) {
  registrar_.Add(this, chrome::NOTIFICATION_GLOBAL_ERRORS_CHANGED,
                 content::Source<Profile>(browser_->profile()));
}

WrenchMenu::~WrenchMenu() {
  if (bookmark_menu_delegate_.get()) {
    BookmarkModel* model = BookmarkModelFactory::GetForProfile(
        browser_->profile());
    if (model)
      model->RemoveObserver(this);
  }
  FOR_EACH_OBSERVER(WrenchMenuObserver, observer_list_, WrenchMenuDestroyed());
}

void WrenchMenu::Init(ui::MenuModel* model) {
  DCHECK(!root_);
  root_ = new MenuItemView(this);
  root_->set_has_icons(true);  // We have checks, radios and icons, set this
                               // so we get the taller menu style.
  PopulateMenu(root_, model);

#if defined(DEBUG)
  // Verify that the reserved command ID's for bookmarks menu are not used.
  for (int i = WrenchMenuModel:kMinBookmarkCommandId;
       i <= WrenchMenuModel::kMaxBookmarkCommandId; ++i)
    DCHECK(command_id_to_entry_.find(i) == command_id_to_entry_.end());
#endif  // defined(DEBUG)

  menu_runner_.reset(new views::MenuRunner(root_));
}

void WrenchMenu::RunMenu(views::MenuButton* host) {
  gfx::Point screen_loc;
  views::View::ConvertPointToScreen(host, &screen_loc);
  gfx::Rect bounds(screen_loc, host->size());
  content::RecordAction(UserMetricsAction("ShowAppMenu"));
  if (menu_runner_->RunMenuAt(host->GetWidget(), host, bounds,
          MenuItemView::TOPRIGHT, ui::MENU_SOURCE_NONE,
          views::MenuRunner::HAS_MNEMONICS) ==
      views::MenuRunner::MENU_DELETED)
    return;
  if (bookmark_menu_delegate_.get()) {
    BookmarkModel* model = BookmarkModelFactory::GetForProfile(
        browser_->profile());
    if (model)
      model->RemoveObserver(this);
  }
  if (selected_menu_model_)
    selected_menu_model_->ActivatedAt(selected_index_);
}

bool WrenchMenu::IsShowing() {
  return menu_runner_.get() && menu_runner_->IsRunning();
}

const ui::NativeTheme* WrenchMenu::GetNativeTheme() const {
  views::Widget* browser_widget = views::Widget::GetWidgetForNativeView(
      browser_->window()->GetNativeWindow());
  DCHECK(browser_widget);
  return browser_widget->GetNativeTheme();
}

const views::MenuConfig& WrenchMenu::GetMenuConfig() const {
  return MenuConfig::instance(GetNativeTheme());
}

void WrenchMenu::AddObserver(WrenchMenuObserver* observer) {
  observer_list_.AddObserver(observer);
}

void WrenchMenu::RemoveObserver(WrenchMenuObserver* observer) {
  observer_list_.RemoveObserver(observer);
}

const gfx::Font* WrenchMenu::GetLabelFont(int command_id) const {
  if (IsRecentTabsCommand(command_id)) {
    return recent_tabs_menu_model_delegate_->GetLabelFontAt(
        ModelIndexFromCommandId(command_id));
  }
  return NULL;
}

bool WrenchMenu::GetForegroundColor(int command_id,
                                    bool is_hovered,
                                    SkColor* override_color) const {
  if (IsRecentTabsCommand(command_id)) {
    return recent_tabs_menu_model_delegate_->GetForegroundColorAt(
        ModelIndexFromCommandId(command_id), is_hovered, override_color);
  }
  return false;
}

string16 WrenchMenu::GetTooltipText(int command_id,
                                    const gfx::Point& p) const {
  return IsBookmarkCommand(command_id) ?
      bookmark_menu_delegate_->GetTooltipText(command_id, p) : string16();
}

bool WrenchMenu::IsTriggerableEvent(views::MenuItemView* menu,
                                    const ui::Event& e) {
  return IsBookmarkCommand(menu->GetCommand()) ?
      bookmark_menu_delegate_->IsTriggerableEvent(menu, e) :
      MenuDelegate::IsTriggerableEvent(menu, e);
}

bool WrenchMenu::GetDropFormats(
      MenuItemView* menu,
      int* formats,
      std::set<ui::OSExchangeData::CustomFormat>* custom_formats) {
  CreateBookmarkMenu();
  return bookmark_menu_delegate_.get() &&
      bookmark_menu_delegate_->GetDropFormats(menu, formats, custom_formats);
}

bool WrenchMenu::AreDropTypesRequired(MenuItemView* menu) {
  CreateBookmarkMenu();
  return bookmark_menu_delegate_.get() &&
      bookmark_menu_delegate_->AreDropTypesRequired(menu);
}

bool WrenchMenu::CanDrop(MenuItemView* menu,
                         const ui::OSExchangeData& data) {
  CreateBookmarkMenu();
  return bookmark_menu_delegate_.get() &&
      bookmark_menu_delegate_->CanDrop(menu, data);
}

int WrenchMenu::GetDropOperation(
    MenuItemView* item,
    const ui::DropTargetEvent& event,
    DropPosition* position) {
  return IsBookmarkCommand(item->GetCommand()) ?
      bookmark_menu_delegate_->GetDropOperation(item, event, position) :
      ui::DragDropTypes::DRAG_NONE;
}

int WrenchMenu::OnPerformDrop(MenuItemView* menu,
                              DropPosition position,
                              const ui::DropTargetEvent& event) {
  if (!IsBookmarkCommand(menu->GetCommand()))
    return ui::DragDropTypes::DRAG_NONE;

  int result = bookmark_menu_delegate_->OnPerformDrop(menu, position, event);
  return result;
}

bool WrenchMenu::ShowContextMenu(MenuItemView* source,
                                 int command_id,
                                 const gfx::Point& p,
                                 ui::MenuSourceType source_type) {
  return IsBookmarkCommand(command_id) ?
      bookmark_menu_delegate_->ShowContextMenu(source, command_id, p,
                                               source_type) :
      false;
}

bool WrenchMenu::CanDrag(MenuItemView* menu) {
  return IsBookmarkCommand(menu->GetCommand()) ?
      bookmark_menu_delegate_->CanDrag(menu) : false;
}

void WrenchMenu::WriteDragData(MenuItemView* sender,
                               ui::OSExchangeData* data) {
  DCHECK(IsBookmarkCommand(sender->GetCommand()));
  return bookmark_menu_delegate_->WriteDragData(sender, data);
}

int WrenchMenu::GetDragOperations(MenuItemView* sender) {
  return IsBookmarkCommand(sender->GetCommand()) ?
      bookmark_menu_delegate_->GetDragOperations(sender) :
      MenuDelegate::GetDragOperations(sender);
}

int WrenchMenu::GetMaxWidthForMenu(MenuItemView* menu) {
  if (IsBookmarkCommand(menu->GetCommand()))
    return bookmark_menu_delegate_->GetMaxWidthForMenu(menu);
  int max_width = -1;
  // If recent tabs menu is available, it will decide if |menu| is one of recent
  // tabs; if yes, it would return the menu width for recent tabs.
  // otherwise, it would return -1.
  if (recent_tabs_menu_model_delegate_.get())
    max_width = recent_tabs_menu_model_delegate_->GetMaxWidthForMenu(menu);
  if (max_width == -1)
    max_width = MenuDelegate::GetMaxWidthForMenu(menu);
  return max_width;
}

bool WrenchMenu::IsItemChecked(int command_id) const {
  if (IsBookmarkCommand(command_id))
    return false;

  const Entry& entry = command_id_to_entry_.find(command_id)->second;
  return entry.first->IsItemCheckedAt(entry.second);
}

bool WrenchMenu::IsCommandEnabled(int command_id) const {
  if (IsBookmarkCommand(command_id))
    return true;

  if (command_id == 0)
    return false;  // The root item.

  // The items representing the cut menu (cut/copy/paste) and zoom menu
  // (increment/decrement/reset) are always enabled. The child views of these
  // items enabled state updates appropriately.
  if (command_id == IDC_CUT || command_id == IDC_ZOOM_MINUS)
    return true;

  const Entry& entry = command_id_to_entry_.find(command_id)->second;
  return entry.first->IsEnabledAt(entry.second);
}

void WrenchMenu::ExecuteCommand(int command_id, int mouse_event_flags) {
  if (IsBookmarkCommand(command_id)) {
    bookmark_menu_delegate_->ExecuteCommand(command_id, mouse_event_flags);
    return;
  }

  if (command_id == IDC_CUT || command_id == IDC_ZOOM_MINUS) {
    // These items are represented by child views. If ExecuteCommand is invoked
    // it means the user clicked on the area around the buttons and we should
    // not do anyting.
    return;
  }

  const Entry& entry = command_id_to_entry_.find(command_id)->second;
  return entry.first->ActivatedAt(entry.second, mouse_event_flags);
}

bool WrenchMenu::GetAccelerator(int command_id, ui::Accelerator* accelerator) {
  if (IsBookmarkCommand(command_id))
    return false;

  if (command_id == IDC_CUT || command_id == IDC_ZOOM_MINUS) {
    // These have special child views; don't show the accelerator for them.
    return false;
  }

  CommandIDToEntry::iterator ix = command_id_to_entry_.find(command_id);
  const Entry& entry = ix->second;
  ui::Accelerator menu_accelerator;
  if (!entry.first->GetAcceleratorAt(entry.second, &menu_accelerator))
    return false;

  *accelerator = ui::Accelerator(menu_accelerator.key_code(),
                                 menu_accelerator.modifiers());
  return true;
}

void WrenchMenu::WillShowMenu(MenuItemView* menu) {
  if (menu == bookmark_menu_)
    CreateBookmarkMenu();
}

void WrenchMenu::WillHideMenu(MenuItemView* menu) {
  // Turns off the fade out animation of the wrench menus if
  // |feedback_menu_item_| is selected.  This excludes the wrench menu itself
  // from the snapshot in the feedback UI.
  if (menu->HasSubmenu() && feedback_menu_item_ &&
      feedback_menu_item_->IsSelected()) {
    // It's okay to just turn off the animation and no to take care the
    // animation back because the menu widget will be recreated next time
    // it's opened. See ToolbarView::RunMenu() and Init() of this class.
    menu->GetSubmenu()->GetWidget()->
        SetVisibilityChangedAnimationsEnabled(false);
  }
}

void WrenchMenu::BookmarkModelChanged() {
  DCHECK(bookmark_menu_delegate_.get());
  if (!bookmark_menu_delegate_->is_mutating_model())
    root_->Cancel();
}

void WrenchMenu::Observe(int type,
                         const content::NotificationSource& source,
                         const content::NotificationDetails& details) {
  switch (type) {
    case chrome::NOTIFICATION_GLOBAL_ERRORS_CHANGED:
      // A change in the global errors list can add or remove items from the
      // menu. Close the menu to avoid have a stale menu on-screen.
      root_->Cancel();
      break;
    default:
      NOTREACHED();
  }
}

void WrenchMenu::PopulateMenu(MenuItemView* parent,
                              MenuModel* model) {
  for (int i = 0, max = model->GetItemCount(); i < max; ++i) {
    // The button container menu items have a special height which we have to
    // use instead of the normal height.
    int height = 0;
    if (use_new_menu_ &&
        (model->GetCommandIdAt(i) == IDC_CUT ||
         model->GetCommandIdAt(i) == IDC_ZOOM_MINUS))
      height = kMenuItemContainingButtonsHeight;

    // Add the menu item at the end.
    int menu_index = parent->HasSubmenu() ?
        parent->GetSubmenu()->child_count() : 0;
    MenuItemView* item = AddMenuItem(
        parent, menu_index, model, i, model->GetTypeAt(i), height);

    if (model->GetTypeAt(i) == MenuModel::TYPE_SUBMENU)
      PopulateMenu(item, model->GetSubmenuModelAt(i));

    const ui::NativeTheme* native_theme = GetNativeTheme();

    switch (model->GetCommandIdAt(i)) {
      case IDC_CUT:
        DCHECK_EQ(MenuModel::TYPE_COMMAND, model->GetTypeAt(i));
        DCHECK_LT(i + 2, max);
        DCHECK_EQ(IDC_COPY, model->GetCommandIdAt(i + 1));
        DCHECK_EQ(IDC_PASTE, model->GetCommandIdAt(i + 2));
        item->SetTitle(l10n_util::GetStringUTF16(IDS_EDIT2));
        item->AddChildView(new CutCopyPasteView(this, model, native_theme,
                                                i, i + 1, i + 2));
        i += 2;
        break;

      case IDC_ZOOM_MINUS:
        DCHECK_EQ(MenuModel::TYPE_COMMAND, model->GetTypeAt(i));
        DCHECK_EQ(IDC_ZOOM_PLUS, model->GetCommandIdAt(i + 1));
        DCHECK_EQ(IDC_FULLSCREEN, model->GetCommandIdAt(i + 2));
        item->SetTitle(l10n_util::GetStringUTF16(IDS_ZOOM_MENU2));
        item->AddChildView(new ZoomView(this, model, native_theme,
                                        i, i + 1, i + 2));
        i += 2;
        break;

      case IDC_BOOKMARKS_MENU:
        DCHECK(!bookmark_menu_);
        bookmark_menu_ = item;
        break;

#if defined(GOOGLE_CHROME_BUILD)
      case IDC_FEEDBACK:
        DCHECK(!feedback_menu_item_);
        feedback_menu_item_ = item;
        break;
#endif

      case IDC_RECENT_TABS_MENU:
        DCHECK(!recent_tabs_menu_model_delegate_.get());
        recent_tabs_menu_model_delegate_.reset(
            new RecentTabsMenuModelDelegate(this, model->GetSubmenuModelAt(i),
                                            item));
        break;

      default:
        break;
    }
  }
}

MenuItemView* WrenchMenu::AddMenuItem(MenuItemView* parent,
                                      int menu_index,
                                      MenuModel* model,
                                      int model_index,
                                      MenuModel::ItemType menu_type,
                                      int height) {
  int command_id = model->GetCommandIdAt(model_index);
  DCHECK(command_id > -1 ||
         (command_id == -1 &&
          model->GetTypeAt(model_index) == MenuModel::TYPE_SEPARATOR));

  if (command_id > -1) {  // Don't add separators to |command_id_to_entry_|.
    // All command ID's should be unique except for IDC_SHOW_HISTORY which is
    // in both wrench menu and RecentTabs submenu,
    if (command_id != IDC_SHOW_HISTORY) {
      DCHECK(command_id_to_entry_.find(command_id) ==
             command_id_to_entry_.end())
          << "command ID " << command_id << " already exists!";
    }
    command_id_to_entry_[command_id].first = model;
    command_id_to_entry_[command_id].second = model_index;
  }

  MenuItemView* menu_item = NULL;
  if (height > 0) {
    // For menu items with a special menu height we use our special class to be
    // able to modify the item height.
    menu_item = new ButtonContainerMenuItemView(parent, command_id, height);
    parent->GetSubmenu()->AddChildViewAt(menu_item, menu_index);
  } else {
    // For all other cases we use the more generic way to add menu items.
    menu_item = views::MenuModelAdapter::AddMenuItemFromModelAt(
        model, model_index, parent, menu_index, command_id);
  }

  if (menu_item) {
    // Flush all buttons to the right side of the menu for the new menu type.
    menu_item->set_use_right_margin(!use_new_menu_);
    menu_item->SetVisible(model->IsVisibleAt(model_index));

    if (menu_type == MenuModel::TYPE_COMMAND && model->HasIcons()) {
      gfx::Image icon;
      if (model->GetIconAt(model_index, &icon))
        menu_item->SetIcon(*icon.ToImageSkia());
    }
  }

  return menu_item;
}

void WrenchMenu::CancelAndEvaluate(MenuModel* model, int index) {
  selected_menu_model_ = model;
  selected_index_ = index;
  root_->Cancel();
}

void WrenchMenu::CreateBookmarkMenu() {
  if (bookmark_menu_delegate_.get())
    return;  // Already created the menu.

  BookmarkModel* model =
      BookmarkModelFactory::GetForProfile(browser_->profile());
  if (!model->loaded())
    return;

  model->AddObserver(this);

  // TODO(oshima): Replace with views only API.
  views::Widget* parent = views::Widget::GetWidgetForNativeWindow(
      browser_->window()->GetNativeWindow());
  bookmark_menu_delegate_.reset(
      new BookmarkMenuDelegate(browser_,
                               browser_,
                               parent,
                               WrenchMenuModel::kMinBookmarkCommandId,
                               WrenchMenuModel::kMaxBookmarkCommandId));
  bookmark_menu_delegate_->Init(this,
                                bookmark_menu_,
                                model->bookmark_bar_node(),
                                0,
                                BookmarkMenuDelegate::SHOW_PERMANENT_FOLDERS,
                                BOOKMARK_LAUNCH_LOCATION_WRENCH_MENU);
}

int WrenchMenu::ModelIndexFromCommandId(int command_id) const {
  CommandIDToEntry::const_iterator ix = command_id_to_entry_.find(command_id);
  DCHECK(ix != command_id_to_entry_.end());
  return ix->second.second;
}
