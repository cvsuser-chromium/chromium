// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_TEST_SHELF_VIEW_TEST_API_H_
#define ASH_TEST_SHELF_VIEW_TEST_API_H_

#include "ash/launcher/launcher_types.h"
#include "base/basictypes.h"

namespace gfx {
class Rect;
class Size;
}

namespace ash {

class LauncherDelegate;

namespace internal {
class LauncherButton;
class OverflowBubble;
class ShelfView;
}

namespace test {

// Use the api in this class to test ShelfView.
class ShelfViewTestAPI {
 public:
  explicit ShelfViewTestAPI(internal::ShelfView* shelf_view);
  ~ShelfViewTestAPI();

  // Number of icons displayed.
  int GetButtonCount();

  // Retrieve the button at |index|.
  internal::LauncherButton* GetButton(int index);

  // Last visible button index.
  int GetLastVisibleIndex();

  // Gets current/ideal bounds for button at |index|.
  const gfx::Rect& GetBoundsByIndex(int index);
  const gfx::Rect& GetIdealBoundsByIndex(int index);

  // Returns true if overflow button is visible.
  bool IsOverflowButtonVisible();

  // Makes shelf view show its overflow bubble.
  void ShowOverflowBubble();

  // Sets animation duration in milliseconds for test.
  void SetAnimationDuration(int duration_ms);

  // Runs message loop and waits until all add/remove animations are done.
  void RunMessageLoopUntilAnimationsDone();

  // An accessor for |shelf_view|.
  internal::ShelfView* shelf_view() { return shelf_view_; }

  // An accessor for overflow bubble.
  internal::OverflowBubble* overflow_bubble();

  // Returns the preferred size of |shelf_view_|.
  gfx::Size GetPreferredSize();

  // Returns the button size.
  int GetButtonSize();

  // Returns the button space size.
  int GetButtonSpacing();

  // Wrapper for ShelfView::SameDragType.
  bool SameDragType(LauncherItemType typea, LauncherItemType typeb) const;

  // Sets LauncherDelegate.
  void SetLauncherDelegate(LauncherDelegate* delegate);

 private:
  internal::ShelfView* shelf_view_;

  DISALLOW_COPY_AND_ASSIGN(ShelfViewTestAPI);
};

}  // namespace test
}  // namespace ash

#endif  // ASH_TEST_SHELF_VIEW_TEST_API_H_
