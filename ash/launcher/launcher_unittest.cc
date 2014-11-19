// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/launcher/launcher.h"
#include "ash/launcher/launcher_button.h"
#include "ash/launcher/launcher_item_delegate_manager.h"
#include "ash/launcher/launcher_model.h"
#include "ash/shelf/shelf_view.h"
#include "ash/shelf/shelf_widget.h"
#include "ash/shell.h"
#include "ash/test/ash_test_base.h"
#include "ash/test/launcher_test_api.h"
#include "ash/test/shelf_view_test_api.h"
#include "ash/test/test_launcher_item_delegate.h"
#include "ash/wm/window_util.h"
#include "ui/aura/root_window.h"
#include "ui/gfx/display.h"
#include "ui/gfx/screen.h"
#include "ui/views/corewm/corewm_switches.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

#if defined(OS_WIN)
#include "base/win/windows_version.h"
#endif

typedef ash::test::AshTestBase LauncherTest;
using ash::internal::ShelfView;
using ash::internal::LauncherButton;

namespace ash {

class LauncherTest : public ash::test::AshTestBase {
 public:
  LauncherTest() : launcher_(NULL),
                   shelf_view_(NULL),
                   launcher_model_(NULL),
                   item_delegate_manager_(NULL) {
  }

  virtual ~LauncherTest() {}

  virtual void SetUp() {
    test::AshTestBase::SetUp();

    launcher_ = Launcher::ForPrimaryDisplay();
    ASSERT_TRUE(launcher_);

    ash::test::LauncherTestAPI test(launcher_);
    shelf_view_ = test.shelf_view();
    launcher_model_ = shelf_view_->model();
    item_delegate_manager_ =
        Shell::GetInstance()->launcher_item_delegate_manager();

    test_.reset(new ash::test::ShelfViewTestAPI(shelf_view_));
  }

  virtual void TearDown() OVERRIDE {
    test::AshTestBase::TearDown();
  }

  Launcher* launcher() {
    return launcher_;
  }

  ShelfView* shelf_view() {
    return shelf_view_;
  }

  LauncherModel* launcher_model() {
    return launcher_model_;
  }

  LauncherItemDelegateManager* item_manager() {
    return item_delegate_manager_;
  }

  ash::test::ShelfViewTestAPI* test_api() {
    return test_.get();
  }

 private:
  Launcher* launcher_;
  ShelfView* shelf_view_;
  LauncherModel* launcher_model_;
  LauncherItemDelegateManager* item_delegate_manager_;
  scoped_ptr<ash::test::ShelfViewTestAPI> test_;

  DISALLOW_COPY_AND_ASSIGN(LauncherTest);
};

// Confirms that LauncherItem reflects the appropriated state.
TEST_F(LauncherTest, StatusReflection) {
  // Initially we have the app list.
  int button_count = test_api()->GetButtonCount();

  // Add running platform app.
  LauncherItem item;
  item.type = TYPE_PLATFORM_APP;
  item.status = STATUS_RUNNING;
  int index = launcher_model()->Add(item);
  ASSERT_EQ(++button_count, test_api()->GetButtonCount());
  LauncherButton* button = test_api()->GetButton(index);
  EXPECT_EQ(LauncherButton::STATE_RUNNING, button->state());

  // Remove it.
  launcher_model()->RemoveItemAt(index);
  ASSERT_EQ(--button_count, test_api()->GetButtonCount());
}

// Confirm that using the menu will clear the hover attribute. To avoid another
// browser test we check this here.
TEST_F(LauncherTest, checkHoverAfterMenu) {
  // Initially we have the app list.
  int button_count = test_api()->GetButtonCount();

  // Add running platform app.
  LauncherItem item;
  item.type = TYPE_PLATFORM_APP;
  item.status = STATUS_RUNNING;
  int index = launcher_model()->Add(item);

  scoped_ptr<LauncherItemDelegate> delegate(
      new ash::test::TestLauncherItemDelegate(NULL));
  item_manager()->SetLauncherItemDelegate(launcher_model()->items()[index].id,
                                          delegate.Pass());

  ASSERT_EQ(++button_count, test_api()->GetButtonCount());
  LauncherButton* button = test_api()->GetButton(index);
  button->AddState(LauncherButton::STATE_HOVERED);
  button->ShowContextMenu(gfx::Point(), ui::MENU_SOURCE_MOUSE);
  EXPECT_FALSE(button->state() & LauncherButton::STATE_HOVERED);

  // Remove it.
  launcher_model()->RemoveItemAt(index);
}

TEST_F(LauncherTest, ShowOverflowBubble) {
  LauncherID first_item_id = launcher_model()->next_id();

  // Add platform app button until overflow.
  int items_added = 0;
  while (!test_api()->IsOverflowButtonVisible()) {
    LauncherItem item;
    item.type = TYPE_PLATFORM_APP;
    item.status = STATUS_RUNNING;
    launcher_model()->Add(item);

    ++items_added;
    ASSERT_LT(items_added, 10000);
  }

  // Shows overflow bubble.
  test_api()->ShowOverflowBubble();
  EXPECT_TRUE(launcher()->IsShowingOverflowBubble());

  // Removes the first item in main shelf view.
  launcher_model()->RemoveItemAt(
      launcher_model()->ItemIndexByID(first_item_id));

  // Waits for all transitions to finish and there should be no crash.
  test_api()->RunMessageLoopUntilAnimationsDone();
  EXPECT_FALSE(launcher()->IsShowingOverflowBubble());
}

}  // namespace ash
