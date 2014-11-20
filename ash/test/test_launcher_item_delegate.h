// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_TEST_TEST_LAUNCHER_ITEM_DELEGATE_
#define ASH_TEST_TEST_LAUNCHER_ITEM_DELEGATE_

#include "ash/launcher/launcher_item_delegate.h"
#include "base/basictypes.h"
#include "base/compiler_specific.h"

namespace aura {
class Window;
}

namespace ash {
namespace test {

// Test implementation of ash::LauncherItemDelegate.
class TestLauncherItemDelegate : public ash::LauncherItemDelegate {
 public:
  explicit TestLauncherItemDelegate(aura::Window* window);
  virtual ~TestLauncherItemDelegate();

  // ash::LauncherItemDelegate overrides:
  virtual bool ItemSelected(const ui::Event& event) OVERRIDE;
  virtual base::string16 GetTitle() OVERRIDE;
  virtual ui::MenuModel* CreateContextMenu(
      aura::Window* root_window) OVERRIDE;
  virtual ash::LauncherMenuModel* CreateApplicationMenu(
      int event_flags) OVERRIDE;
  virtual bool IsDraggable() OVERRIDE;
  virtual bool ShouldShowTooltip() OVERRIDE;

 private:
  aura::Window* window_;

  DISALLOW_COPY_AND_ASSIGN(TestLauncherItemDelegate);
};

}  // namespace test
}  // namespace ash

#endif  // ASH_TEST_TEST_LAUNCHER_ITEM_DELEGATE_
