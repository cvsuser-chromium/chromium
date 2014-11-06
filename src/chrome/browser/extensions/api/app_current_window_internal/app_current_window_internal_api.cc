// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/app_current_window_internal/app_current_window_internal_api.h"

#include "apps/shell_window.h"
#include "apps/shell_window_registry.h"
#include "apps/ui/native_app_window.h"
#include "base/command_line.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/extensions/api/app_current_window_internal.h"
#include "chrome/common/extensions/api/app_window.h"
#include "chrome/common/extensions/features/feature_channel.h"
#include "chrome/common/extensions/features/simple_feature.h"
#include "extensions/common/switches.h"
#include "third_party/skia/include/core/SkRegion.h"

namespace app_current_window_internal =
    extensions::api::app_current_window_internal;

namespace SetBounds = app_current_window_internal::SetBounds;
namespace SetMinWidth = app_current_window_internal::SetMinWidth;
namespace SetMinHeight = app_current_window_internal::SetMinHeight;
namespace SetMaxWidth = app_current_window_internal::SetMaxWidth;
namespace SetMaxHeight = app_current_window_internal::SetMaxHeight;
namespace SetIcon = app_current_window_internal::SetIcon;
namespace SetInputRegion = app_current_window_internal::SetInputRegion;
namespace SetAlwaysOnTop = app_current_window_internal::SetAlwaysOnTop;

using apps::ShellWindow;
using app_current_window_internal::Bounds;
using app_current_window_internal::Region;
using app_current_window_internal::RegionRect;

namespace extensions {

namespace {

const char kNoAssociatedShellWindow[] =
    "The context from which the function was called did not have an "
    "associated shell window.";

const char kDevChannelOnly[] =
    "This function is currently only available in the Dev channel.";

const int kUnboundedSize = apps::ShellWindow::SizeConstraints::kUnboundedSize;

}  // namespace

bool AppCurrentWindowInternalExtensionFunction::RunImpl() {
  apps::ShellWindowRegistry* registry =
      apps::ShellWindowRegistry::Get(GetProfile());
  DCHECK(registry);
  content::RenderViewHost* rvh = render_view_host();
  if (!rvh)
    // No need to set an error, since we won't return to the caller anyway if
    // there's no RVH.
    return false;
  ShellWindow* window = registry->GetShellWindowForRenderViewHost(rvh);
  if (!window) {
    error_ = kNoAssociatedShellWindow;
    return false;
  }
  return RunWithWindow(window);
}

bool AppCurrentWindowInternalFocusFunction::RunWithWindow(ShellWindow* window) {
  window->GetBaseWindow()->Activate();
  return true;
}

bool AppCurrentWindowInternalFullscreenFunction::RunWithWindow(
    ShellWindow* window) {
  window->Fullscreen();
  return true;
}

bool AppCurrentWindowInternalMaximizeFunction::RunWithWindow(
    ShellWindow* window) {
  window->Maximize();
  return true;
}

bool AppCurrentWindowInternalMinimizeFunction::RunWithWindow(
    ShellWindow* window) {
  window->Minimize();
  return true;
}

bool AppCurrentWindowInternalRestoreFunction::RunWithWindow(
    ShellWindow* window) {
  window->Restore();
  return true;
}

bool AppCurrentWindowInternalDrawAttentionFunction::RunWithWindow(
    ShellWindow* window) {
  window->GetBaseWindow()->FlashFrame(true);
  return true;
}

bool AppCurrentWindowInternalClearAttentionFunction::RunWithWindow(
    ShellWindow* window) {
  window->GetBaseWindow()->FlashFrame(false);
  return true;
}

bool AppCurrentWindowInternalShowFunction::RunWithWindow(
    ShellWindow* window) {
  window->Show(ShellWindow::SHOW_ACTIVE);
  return true;
}

bool AppCurrentWindowInternalHideFunction::RunWithWindow(
    ShellWindow* window) {
  window->Hide();
  return true;
}

bool AppCurrentWindowInternalSetBoundsFunction::RunWithWindow(
    ShellWindow* window) {
  // Start with the current bounds, and change any values that are specified in
  // the incoming parameters.
  gfx::Rect bounds = window->GetClientBounds();
  scoped_ptr<SetBounds::Params> params(SetBounds::Params::Create(*args_));
  CHECK(params.get());
  if (params->bounds.left)
    bounds.set_x(*(params->bounds.left));
  if (params->bounds.top)
    bounds.set_y(*(params->bounds.top));
  if (params->bounds.width)
    bounds.set_width(*(params->bounds.width));
  if (params->bounds.height)
    bounds.set_height(*(params->bounds.height));

  bounds.Inset(-window->GetBaseWindow()->GetFrameInsets());
  window->GetBaseWindow()->SetBounds(bounds);
  return true;
}

bool AppCurrentWindowInternalSetMinWidthFunction::RunWithWindow(
    ShellWindow* window) {
  scoped_ptr<SetMinWidth::Params> params(SetMinWidth::Params::Create(*args_));
  CHECK(params.get());
  gfx::Size min_size = window->size_constraints().GetMinimumSize();
  min_size.set_width(params->min_width.get() ?
      *(params->min_width) : kUnboundedSize);
  window->SetMinimumSize(min_size);
  return true;
}

bool AppCurrentWindowInternalSetMinHeightFunction::RunWithWindow(
    ShellWindow* window) {
  scoped_ptr<SetMinHeight::Params> params(SetMinHeight::Params::Create(*args_));
  CHECK(params.get());
  gfx::Size min_size = window->size_constraints().GetMinimumSize();
  min_size.set_height(params->min_height.get() ?
      *(params->min_height) : kUnboundedSize);
  window->SetMinimumSize(min_size);
  return true;
}

bool AppCurrentWindowInternalSetMaxWidthFunction::RunWithWindow(
    ShellWindow* window) {
  scoped_ptr<SetMaxWidth::Params> params(SetMaxWidth::Params::Create(*args_));
  CHECK(params.get());
  gfx::Size max_size = window->size_constraints().GetMaximumSize();
  max_size.set_width(params->max_width.get() ?
      *(params->max_width) : kUnboundedSize);
  window->SetMaximumSize(max_size);
  return true;
}

bool AppCurrentWindowInternalSetMaxHeightFunction::RunWithWindow(
    ShellWindow* window) {
  scoped_ptr<SetMaxHeight::Params> params(SetMaxHeight::Params::Create(*args_));
  CHECK(params.get());
  gfx::Size max_size = window->size_constraints().GetMaximumSize();
  max_size.set_height(params->max_height.get() ?
      *(params->max_height) : kUnboundedSize);
  window->SetMaximumSize(max_size);
  return true;
}

bool AppCurrentWindowInternalSetIconFunction::RunWithWindow(
    ShellWindow* window) {
  if (GetCurrentChannel() > chrome::VersionInfo::CHANNEL_DEV &&
      GetExtension()->location() != extensions::Manifest::COMPONENT) {
    error_ = kDevChannelOnly;
    return false;
  }

  scoped_ptr<SetIcon::Params> params(SetIcon::Params::Create(*args_));
  CHECK(params.get());
  // The |icon_url| parameter may be a blob url (e.g. an image fetched with an
  // XMLHttpRequest) or a resource url.
  GURL url(params->icon_url);
  if (!url.is_valid())
    url = GetExtension()->GetResourceURL(params->icon_url);

  window->SetAppIconUrl(url);
  return true;
}

bool AppCurrentWindowInternalSetInputRegionFunction::RunWithWindow(
    ShellWindow* window) {

  const char* whitelist[] = {
    "EBA908206905323CECE6DC4B276A58A0F4AC573F",
    "2775E568AC98F9578791F1EAB65A1BF5F8CEF414",
    "4AA3C5D69A4AECBD236CAD7884502209F0F5C169",
    "E410CDAB2C6E6DD408D731016CECF2444000A912",
    "9E930B2B5EABA6243AE6C710F126E54688E8FAF6"
  };
  if (GetCurrentChannel() > chrome::VersionInfo::CHANNEL_DEV &&
      !SimpleFeature::IsIdInWhitelist(
          GetExtension()->id(),
          std::set<std::string>(whitelist,
                                whitelist + arraysize(whitelist)))) {
    error_ = kDevChannelOnly;
    return false;
  }

  scoped_ptr<SetInputRegion::Params> params(
      SetInputRegion::Params::Create(*args_));
  const Region& inputRegion = params->region;

  // Build a region from the supplied list of rects.
  // If |rects| is missing, then the input region is removed. This clears the
  // input region so that the entire window accepts input events.
  // To specify an empty input region (so the window ignores all input),
  // |rects| should be an empty list.
  scoped_ptr<SkRegion> region(new SkRegion);
  if (inputRegion.rects) {
    for (std::vector<linked_ptr<RegionRect> >::const_iterator i =
             inputRegion.rects->begin();
         i != inputRegion.rects->end();
         ++i) {
      const RegionRect& inputRect = **i;
      int32_t x = inputRect.left;
      int32_t y = inputRect.top;
      int32_t width = inputRect.width;
      int32_t height = inputRect.height;

      SkIRect rect = SkIRect::MakeXYWH(x, y, width, height);
      region->op(rect, SkRegion::kUnion_Op);
    }
  } else {
    region.reset(NULL);
  }

  window->UpdateInputRegion(region.Pass());

  return true;
}

bool AppCurrentWindowInternalSetAlwaysOnTopFunction::RunWithWindow(
    ShellWindow* window) {
  scoped_ptr<SetAlwaysOnTop::Params> params(
      SetAlwaysOnTop::Params::Create(*args_));
  CHECK(params.get());
  window->GetBaseWindow()->SetAlwaysOnTop(params->always_on_top);
  return true;
}

}  // namespace extensions
