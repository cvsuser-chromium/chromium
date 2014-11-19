// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/opaque_browser_frame_view_layout.h"

#include "chrome/browser/profiles/profiles_state.h"
#include "chrome/browser/ui/views/new_avatar_button.h"
#include "ui/gfx/font.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/label.h"

#if defined(OS_WIN)
#include "win8/util/win8_util.h"
#endif  // OS_WIN

namespace {

// Besides the frame border, there's another 9 px of empty space atop the
// window in restored mode, to use to drag the window around.
const int kNonClientRestoredExtraThickness = 9;

// The titlebar never shrinks too short to show the caption button plus some
// padding below it.
const int kCaptionButtonHeightWithPadding = 19;

// There is a 5 px gap between the title text and the caption buttons.
const int kTitleLogoSpacing = 5;

// The frame border is only visible in restored mode and is hardcoded to 4 px on
// each side regardless of the system window border size.
const int kFrameBorderThickness = 4;

// The titlebar has a 2 px 3D edge along the top and bottom.
const int kTitlebarTopAndBottomEdgeThickness = 2;

// The icon is inset 2 px from the left frame border.
const int kIconLeftSpacing = 2;

// There is a 4 px gap between the icon and the title text.
const int kIconTitleSpacing = 4;

// The avatar ends 2 px above the bottom of the tabstrip (which, given the
// way the tabstrip draws its bottom edge, will appear like a 1 px gap to the
// user).
const int kAvatarBottomSpacing = 2;

// Space between the frame border and the left edge of the avatar.
const int kAvatarLeftSpacing = 2;

// Space between the right edge of the avatar and the tabstrip.
const int kAvatarRightSpacing = -4;

// How far the new avatar button is from the closest caption button.
const int kNewAvatarButtonOffset = 5;

// In restored mode, the New Tab button isn't at the same height as the caption
// buttons, but the space will look cluttered if it actually slides under them,
// so we stop it when the gap between the two is down to 5 px.
const int kNewTabCaptionRestoredSpacing = 5;

// In maximized mode, where the New Tab button and the caption buttons are at
// similar vertical coordinates, we need to reserve a larger, 16 px gap to avoid
// looking too cluttered.
const int kNewTabCaptionMaximizedSpacing = 16;

// The top 3 px of the tabstrip is shadow; in maximized mode we push this off
// the top of the screen so the tabs appear flush against the screen edge.
const int kTabstripTopShadowThickness = 3;

// How far to indent the tabstrip from the left side of the screen when there
// is no avatar icon.
const int kTabStripIndent = -6;

#if defined(OS_LINUX) && !defined(OS_CHROMEOS)
// Default extra space between the top of the frame and the top of the window
// caption buttons.
const int kExtraCaption = 2;

// Default extra spacing between individual window caption buttons.
const int kCaptionButtonSpacing = 2;
#else
const int kExtraCaption = 0;
const int kCaptionButtonSpacing = 0;
#endif

}  // namespace

///////////////////////////////////////////////////////////////////////////////
// OpaqueBrowserFrameView, public:

OpaqueBrowserFrameViewLayout::OpaqueBrowserFrameViewLayout(
    OpaqueBrowserFrameViewLayoutDelegate* delegate)
    : delegate_(delegate),
      leading_button_start_(0),
      trailing_button_start_(0),
      minimum_size_for_buttons_(0),
      has_leading_buttons_(false),
      has_trailing_buttons_(false),
      extra_caption_y_(kExtraCaption),
      window_caption_spacing_(kCaptionButtonSpacing),
      minimize_button_(NULL),
      maximize_button_(NULL),
      restore_button_(NULL),
      close_button_(NULL),
      window_icon_(NULL),
      window_title_(NULL),
      avatar_label_(NULL),
      avatar_button_(NULL),
      new_avatar_button_(NULL) {
  trailing_buttons_.push_back(views::FRAME_BUTTON_MINIMIZE);
  trailing_buttons_.push_back(views::FRAME_BUTTON_MAXIMIZE);
  trailing_buttons_.push_back(views::FRAME_BUTTON_CLOSE);
}

OpaqueBrowserFrameViewLayout::~OpaqueBrowserFrameViewLayout() {}

// static
bool OpaqueBrowserFrameViewLayout::ShouldAddDefaultCaptionButtons() {
#if defined(OS_WIN)
  return !win8::IsSingleWindowMetroMode();
#endif  // OS_WIN
  return true;
}

void OpaqueBrowserFrameViewLayout::SetButtonOrdering(
    const std::vector<views::FrameButton>& leading_buttons,
    const std::vector<views::FrameButton>& trailing_buttons) {
  leading_buttons_ = leading_buttons;
  trailing_buttons_ = trailing_buttons;
}

gfx::Rect OpaqueBrowserFrameViewLayout::GetBoundsForTabStrip(
    const gfx::Size& tabstrip_preferred_size,
    int available_width) const {
  available_width -= trailing_button_start_;
  available_width -= leading_button_start_;

  if (delegate_->GetAdditionalReservedSpaceInTabStrip())
    available_width -= delegate_->GetAdditionalReservedSpaceInTabStrip();

  const int caption_spacing = delegate_->IsMaximized() ?
      kNewTabCaptionMaximizedSpacing : kNewTabCaptionRestoredSpacing;
  const int tabstrip_width = available_width - caption_spacing;
  gfx::Rect bounds(leading_button_start_, GetTabStripInsetsTop(false),
                   std::max(0, tabstrip_width),
                   tabstrip_preferred_size.height());

  int leading_tabstrip_indent = kTabStripIndent;
  if (delegate_->ShouldShowAvatar()) {
    if (avatar_label_ && avatar_label_->bounds().width()) {
      // Space between the trailing edge of the avatar label and the tabstrip.
      const int kAvatarLabelRightSpacing = -10;
      leading_tabstrip_indent -= kAvatarLabelRightSpacing;
    } else {
      leading_tabstrip_indent -= kAvatarRightSpacing;
    }
  }
  bounds.Inset(leading_tabstrip_indent, 0, 0, 0);
  return bounds;
}

gfx::Size OpaqueBrowserFrameViewLayout::GetMinimumSize(
    int available_width) const {
  gfx::Size min_size = delegate_->GetBrowserViewMinimumSize();
  int border_thickness = NonClientBorderThickness();
  min_size.Enlarge(2 * border_thickness,
                   NonClientTopBorderHeight(false) + border_thickness);

  // Ensure that we can, at minimum, hold our window controls and avatar icon.
  min_size.set_width(std::max(min_size.width(), minimum_size_for_buttons_));

  // Ensure that the minimum width is enough to hold a minimum width tab strip
  // at its usual insets.
  if (delegate_->IsTabStripVisible()) {
    gfx::Size preferred_size = delegate_->GetTabstripPreferredSize();
    const int min_tabstrip_width = preferred_size.width();
    const int caption_spacing = delegate_->IsMaximized() ?
        kNewTabCaptionMaximizedSpacing : kNewTabCaptionRestoredSpacing;
    min_size.Enlarge(min_tabstrip_width + caption_spacing, 0);
  }

  return min_size;
}

gfx::Rect OpaqueBrowserFrameViewLayout::GetWindowBoundsForClientBounds(
    const gfx::Rect& client_bounds) const {
  int top_height = NonClientTopBorderHeight(false);
  int border_thickness = NonClientBorderThickness();
  return gfx::Rect(std::max(0, client_bounds.x() - border_thickness),
                   std::max(0, client_bounds.y() - top_height),
                   client_bounds.width() + (2 * border_thickness),
                   client_bounds.height() + top_height + border_thickness);
}

int OpaqueBrowserFrameViewLayout::FrameBorderThickness(bool restored) const {
  return (!restored && (delegate_->IsMaximized() ||
                        delegate_->IsFullscreen())) ?
      0 : kFrameBorderThickness;
}

int OpaqueBrowserFrameViewLayout::NonClientBorderThickness() const {
  // When we fill the screen, we don't show a client edge.
  return FrameBorderThickness(false) +
      ((delegate_->IsMaximized() || delegate_->IsFullscreen()) ?
       0 : views::NonClientFrameView::kClientEdgeThickness);
}

int OpaqueBrowserFrameViewLayout::NonClientTopBorderHeight(
    bool restored) const {
  if (delegate_->ShouldShowWindowTitle()) {
    return std::max(FrameBorderThickness(restored) + delegate_->GetIconSize(),
        CaptionButtonY(restored) + kCaptionButtonHeightWithPadding) +
        TitlebarBottomThickness(restored);
  }

  return FrameBorderThickness(restored) -
      ((delegate_->IsTabStripVisible() &&
          !restored && !delegate_->ShouldLeaveOffsetNearTopBorder())
              ? kTabstripTopShadowThickness : 0);
}

int OpaqueBrowserFrameViewLayout::GetTabStripInsetsTop(bool restored) const {
  return NonClientTopBorderHeight(restored) + ((!restored &&
      (!delegate_->ShouldLeaveOffsetNearTopBorder() ||
      delegate_->IsFullscreen())) ?
      0 : kNonClientRestoredExtraThickness);
}

int OpaqueBrowserFrameViewLayout::TitlebarBottomThickness(bool restored) const {
  return kTitlebarTopAndBottomEdgeThickness +
      ((!restored && delegate_->IsMaximized()) ? 0 :
       views::NonClientFrameView::kClientEdgeThickness);
}

int OpaqueBrowserFrameViewLayout::CaptionButtonY(bool restored) const {
  // Maximized buttons start at window top so that even if their images aren't
  // drawn flush with the screen edge, they still obey Fitts' Law.
  return ((!restored && delegate_->IsMaximized()) ?
      FrameBorderThickness(false) :
          views::NonClientFrameView::kFrameShadowThickness) + extra_caption_y_;
}

gfx::Rect OpaqueBrowserFrameViewLayout::IconBounds() const {
  return window_icon_bounds_;
}

gfx::Rect OpaqueBrowserFrameViewLayout::CalculateClientAreaBounds(
    int width,
    int height) const {
  int top_height = NonClientTopBorderHeight(false);
  int border_thickness = NonClientBorderThickness();
  return gfx::Rect(border_thickness, top_height,
                   std::max(0, width - (2 * border_thickness)),
                   std::max(0, height - top_height - border_thickness));
}

///////////////////////////////////////////////////////////////////////////////
// OpaqueBrowserFrameView, private:

void OpaqueBrowserFrameViewLayout::LayoutWindowControls(views::View* host) {
  if (!ShouldAddDefaultCaptionButtons())
    return;

  int caption_y = CaptionButtonY(false);

  // Keep a list of all buttons that we don't show.
  std::vector<views::FrameButton> buttons_not_shown;
  buttons_not_shown.push_back(views::FRAME_BUTTON_MAXIMIZE);
  buttons_not_shown.push_back(views::FRAME_BUTTON_MINIMIZE);
  buttons_not_shown.push_back(views::FRAME_BUTTON_CLOSE);

  for (std::vector<views::FrameButton>::const_iterator it =
           leading_buttons_.begin(); it != leading_buttons_.end(); ++it) {
    ConfigureButton(host, *it, ALIGN_LEADING, caption_y);
    buttons_not_shown.erase(
        std::remove(buttons_not_shown.begin(), buttons_not_shown.end(), *it),
        buttons_not_shown.end());
  }

  for (std::vector<views::FrameButton>::const_reverse_iterator it =
           trailing_buttons_.rbegin(); it != trailing_buttons_.rend(); ++it) {
    ConfigureButton(host, *it, ALIGN_TRAILING, caption_y);
    buttons_not_shown.erase(
        std::remove(buttons_not_shown.begin(), buttons_not_shown.end(), *it),
        buttons_not_shown.end());
  }

  for (std::vector<views::FrameButton>::const_iterator it =
           buttons_not_shown.begin(); it != buttons_not_shown.end(); ++it) {
    HideButton(*it);
  }
}

void OpaqueBrowserFrameViewLayout::LayoutTitleBar(views::View* host) {
  bool use_hidden_icon_location = true;

  int size = delegate_->GetIconSize();
  int frame_thickness = FrameBorderThickness(false);
  bool should_show_icon = delegate_->ShouldShowWindowIcon();
  bool should_show_title = delegate_->ShouldShowWindowTitle();

  if (should_show_icon || should_show_title) {
    use_hidden_icon_location = false;

    // Our frame border has a different "3D look" than Windows'.  Theirs has
    // a more complex gradient on the top that they push their icon/title
    // below; then the maximized window cuts this off and the icon/title are
    // centered in the remaining space.  Because the apparent shape of our
    // border is simpler, using the same positioning makes things look
    // slightly uncentered with restored windows, so when the window is
    // restored, instead of calculating the remaining space from below the
    // frame border, we calculate from below the 3D edge.
    int unavailable_px_at_top = delegate_->IsMaximized() ?
        frame_thickness : kTitlebarTopAndBottomEdgeThickness;
    // When the icon is shorter than the minimum space we reserve for the
    // caption button, we vertically center it.  We want to bias rounding to
    // put extra space above the icon, since the 3D edge (+ client edge, for
    // restored windows) below looks (to the eye) more like additional space
    // than does the 3D edge (or nothing at all, for maximized windows)
    // above; hence the +1.
    int y = unavailable_px_at_top + (NonClientTopBorderHeight(false) -
                                     unavailable_px_at_top - size -
                                     TitlebarBottomThickness(false) + 1) / 2;

    window_icon_bounds_ = gfx::Rect(leading_button_start_ + kIconLeftSpacing, y,
                                    size, size);
    leading_button_start_ += size + kIconLeftSpacing;
    minimum_size_for_buttons_ += size + kIconLeftSpacing;
  }

  if (should_show_icon)
    window_icon_->SetBoundsRect(window_icon_bounds_);

  if (window_title_) {
    window_title_->SetVisible(should_show_title);
    if (should_show_title) {
      window_title_->SetText(delegate_->GetWindowTitle());

      int text_width = std::max(
          0, host->width() - trailing_button_start_ - kTitleLogoSpacing -
          leading_button_start_ - kIconTitleSpacing);
      window_title_->SetBounds(leading_button_start_ + kIconTitleSpacing,
                               window_icon_bounds_.y(),
                               text_width, window_icon_bounds_.height());
      leading_button_start_ += text_width + kIconTitleSpacing;
    }
  }

  if (use_hidden_icon_location) {
    if (has_leading_buttons_) {
      // There are window button icons on the left. Don't size the hidden window
      // icon that people can double click on to close the window.
      window_icon_bounds_ = gfx::Rect();
    } else {
      // We set the icon bounds to a small rectangle in the top leading corner
      // if there are no icons on the leading side.
      window_icon_bounds_ = gfx::Rect(
          frame_thickness + kIconLeftSpacing, frame_thickness, size, size);
    }
  }
}

void OpaqueBrowserFrameViewLayout::LayoutNewStyleAvatar(views::View* host) {
  DCHECK(profiles::IsNewProfileManagementEnabled());
  if (!new_avatar_button_)
    return;

  gfx::Size label_size = new_avatar_button_->GetPreferredSize();
  int button_size_with_offset = kNewAvatarButtonOffset + label_size.width();

  int button_x = host->width() - trailing_button_start_ -
      button_size_with_offset;
  int button_y = CaptionButtonY(false);

  trailing_button_start_ += button_size_with_offset;
  minimum_size_for_buttons_ += button_size_with_offset;

  new_avatar_button_->SetBounds(
      button_x,
      button_y,
      label_size.width(),
      button_y + kCaptionButtonHeightWithPadding);
}

void OpaqueBrowserFrameViewLayout::LayoutAvatar() {
  // Even though the avatar is used for both incognito and profiles we always
  // use the incognito icon to layout the avatar button. The profile icon
  // can be customized so we can't depend on its size to perform layout.
  gfx::ImageSkia incognito_icon = delegate_->GetOTRAvatarIcon();

  int avatar_bottom = GetTabStripInsetsTop(false) +
      delegate_->GetTabStripHeight() - kAvatarBottomSpacing;
  int avatar_restored_y = avatar_bottom - incognito_icon.height();
  int avatar_y = delegate_->IsMaximized() ?
      (NonClientTopBorderHeight(false) + kTabstripTopShadowThickness) :
      avatar_restored_y;
  avatar_bounds_.SetRect(leading_button_start_ + kAvatarLeftSpacing,
      avatar_y, incognito_icon.width(),
      delegate_->ShouldShowAvatar() ? (avatar_bottom - avatar_y) : 0);
  if (avatar_button_) {
    avatar_button_->SetBoundsRect(avatar_bounds_);

    if (avatar_label_) {
      // Space between the bottom of the avatar and the bottom of the avatar
      // label.
      const int kAvatarLabelBottomSpacing = 3;
      gfx::Size label_size = avatar_label_->GetPreferredSize();
      // The x-position of the avatar label should be slightly to the left of
      // the avatar menu button. Therefore we use the |leading_button_start_|
      // value directly.
      gfx::Rect label_bounds(
          leading_button_start_,
          avatar_bottom - kAvatarLabelBottomSpacing - label_size.height(),
          label_size.width(),
          delegate_->ShouldShowAvatar() ? label_size.height() : 0);
      avatar_label_->SetBoundsRect(label_bounds);
      leading_button_start_ += label_size.width();
    } else {
      leading_button_start_ += kAvatarLeftSpacing + incognito_icon.width();
    }

    // We just add the avatar button size to the minimum size because clicking
    // the avatar label does the same thing as clicking the avatar button.
    minimum_size_for_buttons_ += kAvatarLeftSpacing + incognito_icon.width();
  }
}

void OpaqueBrowserFrameViewLayout::ConfigureButton(
    views::View* host,
    views::FrameButton button_id,
    ButtonAlignment alignment,
    int caption_y) {
  switch (button_id) {
    case views::FRAME_BUTTON_MINIMIZE: {
      minimize_button_->SetVisible(true);
      SetBoundsForButton(host, minimize_button_, alignment, caption_y);
      break;
    }
    case views::FRAME_BUTTON_MAXIMIZE: {
      // When the window is restored, we show a maximized button; otherwise, we
      // show a restore button.
      bool is_restored = !delegate_->IsMaximized() && !delegate_->IsMinimized();
      views::ImageButton* invisible_button = is_restored ?
          restore_button_ : maximize_button_;
      invisible_button->SetVisible(false);

      views::ImageButton* visible_button = is_restored ?
          maximize_button_ : restore_button_;
      visible_button->SetVisible(true);
      SetBoundsForButton(host, visible_button, alignment, caption_y);
      break;
    }
    case views::FRAME_BUTTON_CLOSE: {
      close_button_->SetVisible(true);
      SetBoundsForButton(host, close_button_, alignment, caption_y);
      break;
    }
  }
}

void OpaqueBrowserFrameViewLayout::HideButton(views::FrameButton button_id) {
  switch (button_id) {
    case views::FRAME_BUTTON_MINIMIZE:
      minimize_button_->SetVisible(false);
      break;
    case views::FRAME_BUTTON_MAXIMIZE:
      restore_button_->SetVisible(false);
      maximize_button_->SetVisible(false);
      break;
    case views::FRAME_BUTTON_CLOSE:
      close_button_->SetVisible(false);
      break;
  }
}

void OpaqueBrowserFrameViewLayout::SetBoundsForButton(
    views::View* host,
    views::ImageButton* button,
    ButtonAlignment alignment,
    int caption_y) {
  gfx::Size button_size = button->GetPreferredSize();

  button->SetImageAlignment(
      (alignment == ALIGN_LEADING)  ?
          views::ImageButton::ALIGN_RIGHT : views::ImageButton::ALIGN_LEFT,
      views::ImageButton::ALIGN_BOTTOM);

  // There should always be the same number of non-shadow pixels visible to the
  // side of the caption buttons.  In maximized mode we extend the rightmost
  // button to the screen corner to obey Fitts' Law.
  bool is_maximized = delegate_->IsMaximized();

  // When we are the first button on the leading side and are the close
  // button, we must flip ourselves, because the close button assets have
  // a little notch to fit in the rounded frame.
  button->SetDrawImageMirrored(alignment == ALIGN_LEADING &&
                               !has_leading_buttons_ &&
                               button == close_button_);

  switch (alignment) {
    case ALIGN_LEADING: {
      if (has_leading_buttons_)
        leading_button_start_ += window_caption_spacing_;

      // If we're the first button on the left and maximized, add with to the
      // right hand side of the screen.
      int extra_width = (is_maximized && !has_leading_buttons_) ?
        (kFrameBorderThickness -
         views::NonClientFrameView::kFrameShadowThickness) : 0;

      button->SetBounds(
          leading_button_start_ - extra_width,
          caption_y,
          button_size.width() + extra_width,
          button_size.height());

      leading_button_start_ += extra_width + button_size.width();
      minimum_size_for_buttons_ += extra_width + button_size.width();
      has_leading_buttons_ = true;
      break;
    }
    case ALIGN_TRAILING: {
      if (has_trailing_buttons_)
        trailing_button_start_ += window_caption_spacing_;

      // If we're the first button on the right and maximized, add with to the
      // right hand side of the screen.
      int extra_width = (is_maximized && !has_trailing_buttons_) ?
        (kFrameBorderThickness -
         views::NonClientFrameView::kFrameShadowThickness) : 0;

      button->SetBounds(
          host->width() - trailing_button_start_ - extra_width -
              button_size.width(),
          caption_y,
          button_size.width() + extra_width,
          button_size.height());

      trailing_button_start_ += extra_width + button_size.width();
      minimum_size_for_buttons_ += extra_width + button_size.width();
      has_trailing_buttons_ = true;
      break;
    }
  }
}

void OpaqueBrowserFrameViewLayout::SetView(int id, views::View* view) {
  // Why do things this way instead of having an Init() method, where we're
  // passed the views we'll handle? Because OpaqueBrowserFrameView doesn't own
  // all the views which are part of it. The avatar stuff, for example, will be
  // added and removed by the base class of OpaqueBrowserFrameView.
  switch (id) {
    case VIEW_ID_MINIMIZE_BUTTON:
      if (view) {
        DCHECK_EQ(std::string(views::ImageButton::kViewClassName),
                  view->GetClassName());
      }
      minimize_button_ = static_cast<views::ImageButton*>(view);
      break;
    case VIEW_ID_MAXIMIZE_BUTTON:
      if (view) {
        DCHECK_EQ(std::string(views::ImageButton::kViewClassName),
                  view->GetClassName());
      }
      maximize_button_ = static_cast<views::ImageButton*>(view);
      break;
    case VIEW_ID_RESTORE_BUTTON:
      if (view) {
        DCHECK_EQ(std::string(views::ImageButton::kViewClassName),
                  view->GetClassName());
      }
      restore_button_ = static_cast<views::ImageButton*>(view);
      break;
    case VIEW_ID_CLOSE_BUTTON:
      if (view) {
        DCHECK_EQ(std::string(views::ImageButton::kViewClassName),
                  view->GetClassName());
      }
      close_button_ = static_cast<views::ImageButton*>(view);
      break;
    case VIEW_ID_WINDOW_ICON:
      window_icon_ = view;
      break;
    case VIEW_ID_WINDOW_TITLE:
      if (view) {
        DCHECK_EQ(std::string(views::Label::kViewClassName),
                  view->GetClassName());
      }
      window_title_ = static_cast<views::Label*>(view);
      break;
    case VIEW_ID_AVATAR_LABEL:
      avatar_label_ = view;
      break;
    case VIEW_ID_AVATAR_BUTTON:
      avatar_button_ = view;
      break;
    case VIEW_ID_NEW_AVATAR_BUTTON:
      new_avatar_button_ = static_cast<NewAvatarButton*>(view);
      break;
    default:
      NOTIMPLEMENTED() << "Unknown view id " << id;
      break;
  }
}

///////////////////////////////////////////////////////////////////////////////
// OpaqueBrowserFrameView, views::LayoutManager:

void OpaqueBrowserFrameViewLayout::Layout(views::View* host) {
  // Reset all our data so that everything is invisible.
  int thickness = FrameBorderThickness(false);
  leading_button_start_ = thickness;
  trailing_button_start_ = thickness;
  minimum_size_for_buttons_ = leading_button_start_ + trailing_button_start_;
  has_leading_buttons_ = false;
  has_trailing_buttons_ = false;

  LayoutWindowControls(host);
  LayoutTitleBar(host);

  // We now add a single pixel to the leading spacing. We do this because the
  // avatar and tab strip start one pixel inward compared to where things start
  // on the trailing side.
  leading_button_start_++;

  if (delegate_->IsRegularOrGuestSession() &&
      profiles::IsNewProfileManagementEnabled())
    LayoutNewStyleAvatar(host);
  else
    LayoutAvatar();

  client_view_bounds_ = CalculateClientAreaBounds(
      host->width(), host->height());
}

gfx::Size OpaqueBrowserFrameViewLayout::GetPreferredSize(views::View* host) {
  // This is never used; NonClientView::GetPreferredSize() will be called
  // instead.
  NOTREACHED();
  return gfx::Size();
}

void OpaqueBrowserFrameViewLayout::ViewAdded(views::View* host,
                                             views::View* view) {
  SetView(view->id(), view);
}

void OpaqueBrowserFrameViewLayout::ViewRemoved(views::View* host,
                                               views::View* view) {
  SetView(view->id(), NULL);
}
