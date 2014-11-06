// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/shell.h"
#include "ash/test/ash_test_base.h"
#include "ash/test/test_shell_delegate.h"
#include "ash/wm/window_state.h"
#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/logging.h"
#include "chrome/browser/ui/ash/multi_user/multi_user_window_manager.h"
#include "chrome/browser/ui/ash/multi_user/multi_user_window_manager_chromeos.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/test/base/testing_profile.h"
#include "ui/aura/client/activation_client.h"
#include "ui/aura/client/aura_constants.h"
#include "ui/aura/root_window.h"
#include "ui/base/ui_base_types.h"

namespace ash {
namespace test {

// A test class for preparing the chrome::MultiUserWindowManager. It creates
// various windows and instantiates the chrome::MultiUserWindowManager.
class MultiUserWindowManagerChromeOSTest : public AshTestBase {
 public:
  MultiUserWindowManagerChromeOSTest() : multi_user_window_manager_(NULL) {}

  virtual void SetUp() OVERRIDE;
  virtual void TearDown() OVERRIDE;

 protected:
  // Set up the test environment for this many windows.
  void SetUpForThisManyWindows(int windows);

  // Return the window with the given index.
  aura::Window* window(size_t index) {
    DCHECK(index < window_.size());
    return window_[index];
  }

  // The accessor to the MultiWindowManager.
  chrome::MultiUserWindowManagerChromeOS* multi_user_window_manager() {
    return multi_user_window_manager_;
  }

  // Returns a list of all open windows in the following form:
  // "<H(idden)/S(hown)>[<Owner>[,<shownForUser>]], .."
  // Like: "S[B], .." would mean that window#0 is shown and belongs to user B.
  // or "S[B,A], .." would mean that window#0 is shown, belongs to B but is
  // shown by A.
  std::string GetStatus();

 private:
  // These get created for each session.
  std::vector<aura::Window*> window_;

  // The instance of the MultiUserWindowManager.
  chrome::MultiUserWindowManagerChromeOS* multi_user_window_manager_;

  DISALLOW_COPY_AND_ASSIGN(MultiUserWindowManagerChromeOSTest);
};

void MultiUserWindowManagerChromeOSTest::SetUp() {
  CommandLine::ForCurrentProcess()->AppendSwitch(switches::kMultiProfiles);
  AshTestBase::SetUp();
}

void MultiUserWindowManagerChromeOSTest::SetUpForThisManyWindows(int windows) {
  DCHECK(!window_.size());
  for (int i = 0; i < windows; i++) {
    window_.push_back(CreateTestWindowInShellWithId(i));
    window_[i]->Show();
  }
  multi_user_window_manager_ = new chrome::MultiUserWindowManagerChromeOS("A");
  chrome::MultiUserWindowManager::SetInstanceForTest(multi_user_window_manager_,
        chrome::MultiUserWindowManager::MULTI_PROFILE_MODE_SEPARATED);
  EXPECT_TRUE(multi_user_window_manager_);
}

void MultiUserWindowManagerChromeOSTest::TearDown() {
  // Since the AuraTestBase is needed to create our assets, we have to
  // also delete them before we tear it down.
  while (!window_.empty()) {
    delete *(window_.begin());
    window_.erase(window_.begin());
  }

  AshTestBase::TearDown();
  chrome::MultiUserWindowManager::DeleteInstance();
}

std::string MultiUserWindowManagerChromeOSTest::GetStatus() {
  std::string s;
  for (size_t i = 0; i < window_.size(); i++) {
    if (i)
      s += ", ";
    s += window(i)->IsVisible() ? "S[" : "H[";
    const std::string& owner =
        multi_user_window_manager_->GetWindowOwner(window(i));
    s += owner;
    const std::string& presenter =
        multi_user_window_manager_->GetUserPresentingWindow(window(i));
    if (!owner.empty() && owner != presenter) {
      s += ",";
      s += presenter;
    }
    s += "]";
  }
  return s;
}

// Testing basic assumptions like default state and existence of manager.
TEST_F(MultiUserWindowManagerChromeOSTest, BasicTests) {
  SetUpForThisManyWindows(3);
  // Check the basic assumptions: All windows are visible and there is no owner.
  EXPECT_EQ("S[], S[], S[]", GetStatus());
  EXPECT_TRUE(multi_user_window_manager());
  EXPECT_EQ(multi_user_window_manager(),
            chrome::MultiUserWindowManager::GetInstance());
  EXPECT_FALSE(multi_user_window_manager()->AreWindowsSharedAmongUsers());

  // The owner of an unowned window should be empty and it should be shown on
  // all windows.
  EXPECT_EQ("", multi_user_window_manager()->GetWindowOwner(window(0)));
  EXPECT_EQ("",
      multi_user_window_manager()->GetUserPresentingWindow(window(0)));
  EXPECT_TRUE(
      multi_user_window_manager()->IsWindowOnDesktopOfUser(window(0), "A"));
  EXPECT_TRUE(
      multi_user_window_manager()->IsWindowOnDesktopOfUser(window(0), "B"));

  // Set the owner of one window should remember it as such. It should only be
  // drawn on the owners desktop - not on any other.
  multi_user_window_manager()->SetWindowOwner(window(0), "A");
  EXPECT_EQ("A", multi_user_window_manager()->GetWindowOwner(window(0)));
  EXPECT_EQ("A",
      multi_user_window_manager()->GetUserPresentingWindow(window(0)));
  EXPECT_TRUE(
      multi_user_window_manager()->IsWindowOnDesktopOfUser(window(0), "A"));
  EXPECT_FALSE(
      multi_user_window_manager()->IsWindowOnDesktopOfUser(window(0), "B"));

  // Overriding it with another state should show it on the other user's
  // desktop.
  multi_user_window_manager()->ShowWindowForUser(window(0), "B");
  EXPECT_EQ("A", multi_user_window_manager()->GetWindowOwner(window(0)));
  EXPECT_EQ("B",
      multi_user_window_manager()->GetUserPresentingWindow(window(0)));
  EXPECT_FALSE(
      multi_user_window_manager()->IsWindowOnDesktopOfUser(window(0), "A"));
  EXPECT_TRUE(
      multi_user_window_manager()->IsWindowOnDesktopOfUser(window(0), "B"));
}

// Testing simple owner changes.
TEST_F(MultiUserWindowManagerChromeOSTest, OwnerTests) {
  SetUpForThisManyWindows(5);
  // Set some windows to the active owner.
  multi_user_window_manager()->SetWindowOwner(window(0), "A");
  EXPECT_EQ("S[A], S[], S[], S[], S[]", GetStatus());
  multi_user_window_manager()->SetWindowOwner(window(2), "A");
  EXPECT_EQ("S[A], S[], S[A], S[], S[]", GetStatus());

  // Set some windows to an inactive owner. Note that the windows should hide.
  multi_user_window_manager()->SetWindowOwner(window(1), "B");
  EXPECT_EQ("S[A], H[B], S[A], S[], S[]", GetStatus());
  multi_user_window_manager()->SetWindowOwner(window(3), "B");
  EXPECT_EQ("S[A], H[B], S[A], H[B], S[]", GetStatus());

  // Assume that the user has now changed to C - which should show / hide
  // accordingly.
  multi_user_window_manager()->ActiveUserChanged("C");
  EXPECT_EQ("H[A], H[B], H[A], H[B], S[]", GetStatus());

  // If someone tries to show an inactive window it should only work if it can
  // be shown / hidden.
  multi_user_window_manager()->ActiveUserChanged("A");
  EXPECT_EQ("S[A], H[B], S[A], H[B], S[]", GetStatus());
  window(3)->Show();
  EXPECT_EQ("S[A], H[B], S[A], H[B], S[]", GetStatus());
  window(2)->Hide();
  EXPECT_EQ("S[A], H[B], H[A], H[B], S[]", GetStatus());
  window(2)->Show();
  EXPECT_EQ("S[A], H[B], S[A], H[B], S[]", GetStatus());
}

TEST_F(MultiUserWindowManagerChromeOSTest, CloseWindowTests) {
  SetUpForThisManyWindows(2);
  multi_user_window_manager()->SetWindowOwner(window(0), "B");
  EXPECT_EQ("H[B], S[]", GetStatus());
  multi_user_window_manager()->ShowWindowForUser(window(0), "A");
  EXPECT_EQ("S[B,A], S[]", GetStatus());
  EXPECT_TRUE(multi_user_window_manager()->AreWindowsSharedAmongUsers());

  // Simulate a close of the shared window.
  multi_user_window_manager()->OnWindowDestroyed(window(0));

  // There should be no owner anymore for that window and the shared windows
  // should be gone as well.
  EXPECT_EQ("S[], S[]", GetStatus());
  EXPECT_FALSE(multi_user_window_manager()->AreWindowsSharedAmongUsers());
}

TEST_F(MultiUserWindowManagerChromeOSTest, SharedWindowTests) {
  SetUpForThisManyWindows(5);
  // Set some owners and make sure we got what we asked for.
  multi_user_window_manager()->SetWindowOwner(window(0), "A");
  multi_user_window_manager()->SetWindowOwner(window(1), "A");
  multi_user_window_manager()->SetWindowOwner(window(2), "B");
  multi_user_window_manager()->SetWindowOwner(window(3), "B");
  multi_user_window_manager()->SetWindowOwner(window(4), "C");
  EXPECT_EQ("S[A], S[A], H[B], H[B], H[C]", GetStatus());
  EXPECT_FALSE(multi_user_window_manager()->AreWindowsSharedAmongUsers());

  // For all following tests we override window 2 to be shown by user B.
  multi_user_window_manager()->ShowWindowForUser(window(1), "B");

  // Change window 3 between two users and see that it changes
  // accordingly (or not).
  multi_user_window_manager()->ShowWindowForUser(window(2), "A");
  EXPECT_EQ("S[A], H[A,B], S[B,A], H[B], H[C]", GetStatus());
  EXPECT_TRUE(multi_user_window_manager()->AreWindowsSharedAmongUsers());
  multi_user_window_manager()->ShowWindowForUser(window(2), "C");
  EXPECT_EQ("S[A], H[A,B], H[B,C], H[B], H[C]", GetStatus());
  EXPECT_TRUE(multi_user_window_manager()->AreWindowsSharedAmongUsers());

  // Switch the users and see that the results are correct.
  multi_user_window_manager()->ActiveUserChanged("B");
  EXPECT_EQ("H[A], S[A,B], H[B,C], S[B], H[C]", GetStatus());
  multi_user_window_manager()->ActiveUserChanged("C");
  EXPECT_EQ("H[A], H[A,B], S[B,C], H[B], S[C]", GetStatus());

  // Showing on the desktop of the already owning user should have no impact.
  multi_user_window_manager()->ShowWindowForUser(window(4), "C");
  EXPECT_EQ("H[A], H[A,B], S[B,C], H[B], S[C]", GetStatus());

  // Changing however a shown window back to the original owner should hide it.
  multi_user_window_manager()->ShowWindowForUser(window(2), "B");
  EXPECT_EQ("H[A], H[A,B], H[B], H[B], S[C]", GetStatus());
  EXPECT_TRUE(multi_user_window_manager()->AreWindowsSharedAmongUsers());

  // And the change should be "permanent" - switching somewhere else and coming
  // back.
  multi_user_window_manager()->ActiveUserChanged("B");
  EXPECT_EQ("H[A], S[A,B], S[B], S[B], H[C]", GetStatus());
  multi_user_window_manager()->ActiveUserChanged("C");
  EXPECT_EQ("H[A], H[A,B], H[B], H[B], S[C]", GetStatus());

  // After switching window 2 back to its original desktop, all desktops should
  // be "clean" again.
  multi_user_window_manager()->ShowWindowForUser(window(1), "A");
  EXPECT_FALSE(multi_user_window_manager()->AreWindowsSharedAmongUsers());
}

// Make sure that adding a window to another desktop does not cause harm.
TEST_F(MultiUserWindowManagerChromeOSTest, DoubleSharedWindowTests) {
  SetUpForThisManyWindows(2);
  multi_user_window_manager()->SetWindowOwner(window(0), "B");

  // Add two references to the same window.
  multi_user_window_manager()->ShowWindowForUser(window(0), "A");
  multi_user_window_manager()->ShowWindowForUser(window(0), "A");
  EXPECT_TRUE(multi_user_window_manager()->AreWindowsSharedAmongUsers());

  // Simulate a close of the shared window.
  multi_user_window_manager()->OnWindowDestroyed(window(0));

  // There should be no shares anymore open.
  EXPECT_FALSE(multi_user_window_manager()->AreWindowsSharedAmongUsers());
}

// Tests that the user's desktop visibility changes get respected. These tests
// are required to make sure that our usage of the same feature for showing and
// hiding does not interfere with the "normal operation".
TEST_F(MultiUserWindowManagerChromeOSTest, PreserveWindowVisibilityTests) {
  SetUpForThisManyWindows(5);
  // Set some owners and make sure we got what we asked for.
  // Note that we try to cover all combinations in one go.
  multi_user_window_manager()->SetWindowOwner(window(0), "A");
  multi_user_window_manager()->SetWindowOwner(window(1), "A");
  multi_user_window_manager()->SetWindowOwner(window(2), "B");
  multi_user_window_manager()->SetWindowOwner(window(3), "B");
  multi_user_window_manager()->ShowWindowForUser(window(2), "A");
  multi_user_window_manager()->ShowWindowForUser(window(3), "A");
  EXPECT_EQ("S[A], S[A], S[B,A], S[B,A], S[]", GetStatus());

  // Hiding a window should be respected - no matter if it is owned by that user
  // owned by someone else but shown on that desktop - or not owned.
  window(0)->Hide();
  window(2)->Hide();
  window(4)->Hide();
  EXPECT_EQ("H[A], S[A], H[B,A], S[B,A], H[]", GetStatus());

  // Flipping to another user and back should preserve all show / hide states.
  multi_user_window_manager()->ActiveUserChanged("B");
  EXPECT_EQ("H[A], H[A], H[B,A], H[B,A], H[]", GetStatus());

  multi_user_window_manager()->ActiveUserChanged("A");
  EXPECT_EQ("H[A], S[A], H[B,A], S[B,A], H[]", GetStatus());

  // After making them visible and switching fore and back everything should be
  // visible.
  window(0)->Show();
  window(2)->Show();
  window(4)->Show();
  EXPECT_EQ("S[A], S[A], S[B,A], S[B,A], S[]", GetStatus());

  multi_user_window_manager()->ActiveUserChanged("B");
  EXPECT_EQ("H[A], H[A], H[B,A], H[B,A], S[]", GetStatus());

  multi_user_window_manager()->ActiveUserChanged("A");
  EXPECT_EQ("S[A], S[A], S[B,A], S[B,A], S[]", GetStatus());

  // Now test that making windows visible through "normal operation" while the
  // user's desktop is hidden leads to the correct result.
  multi_user_window_manager()->ActiveUserChanged("B");
  EXPECT_EQ("H[A], H[A], H[B,A], H[B,A], S[]", GetStatus());
  window(0)->Show();
  window(2)->Show();
  window(4)->Show();
  EXPECT_EQ("H[A], H[A], H[B,A], H[B,A], S[]", GetStatus());
  multi_user_window_manager()->ActiveUserChanged("A");
  EXPECT_EQ("S[A], S[A], S[B,A], S[B,A], S[]", GetStatus());
}

// Check that minimizing a window which is owned by another user will move it
// back.
TEST_F(MultiUserWindowManagerChromeOSTest, MinimizeChangesOwnershipBack) {
  SetUpForThisManyWindows(4);
  multi_user_window_manager()->SetWindowOwner(window(0), "A");
  multi_user_window_manager()->SetWindowOwner(window(1), "B");
  multi_user_window_manager()->SetWindowOwner(window(2), "B");
  multi_user_window_manager()->ShowWindowForUser(window(1), "A");
  EXPECT_EQ("S[A], S[B,A], H[B], S[]", GetStatus());
  EXPECT_TRUE(multi_user_window_manager()->IsWindowOnDesktopOfUser(window(1),
                                                                   "A"));
  wm::GetWindowState(window(1))->Minimize();
  EXPECT_EQ("S[A], H[B], H[B], S[]", GetStatus());
  EXPECT_FALSE(multi_user_window_manager()->IsWindowOnDesktopOfUser(window(1),
                                                                    "A"));

  // Change to user B and make sure that minimizing does not change anything.
  multi_user_window_manager()->ActiveUserChanged("B");
  EXPECT_EQ("H[A], H[B], S[B], S[]", GetStatus());
  wm::GetWindowState(window(1))->Minimize();
  EXPECT_EQ("H[A], H[B], S[B], S[]", GetStatus());
}

// Check that we cannot transfer the ownership of a minimized window.
TEST_F(MultiUserWindowManagerChromeOSTest, MinimizeSuppressesViewTransfer) {
  SetUpForThisManyWindows(1);
  multi_user_window_manager()->SetWindowOwner(window(0), "A");
  wm::GetWindowState(window(0))->Minimize();
  EXPECT_EQ("H[A]", GetStatus());

  // Try to transfer the window to user B - which should get ignored.
  multi_user_window_manager()->ShowWindowForUser(window(0), "B");
  EXPECT_EQ("H[A]", GetStatus());
}

// Testing that the activation state changes to the active window.
TEST_F(MultiUserWindowManagerChromeOSTest, ActiveWindowTests) {
  SetUpForThisManyWindows(4);

  aura::client::ActivationClient* activation_client =
      aura::client::GetActivationClient(window(0)->GetRootWindow());

  // Set some windows to the active owner.
  multi_user_window_manager()->SetWindowOwner(window(0), "A");
  multi_user_window_manager()->SetWindowOwner(window(1), "A");
  multi_user_window_manager()->SetWindowOwner(window(2), "B");
  multi_user_window_manager()->SetWindowOwner(window(3), "B");
  EXPECT_EQ("S[A], S[A], H[B], H[B]", GetStatus());

  // Set the active window for user A to be #1
  activation_client->ActivateWindow(window(1));

  // Change to user B and make sure that one of its windows is active.
  multi_user_window_manager()->ActiveUserChanged("B");
  EXPECT_EQ("H[A], H[A], S[B], S[B]", GetStatus());
  EXPECT_TRUE(window(3) == activation_client->GetActiveWindow() ||
              window(2) == activation_client->GetActiveWindow());
  // Set the active window for user B now to be #2
  activation_client->ActivateWindow(window(2));

  multi_user_window_manager()->ActiveUserChanged("A");
  EXPECT_EQ(window(1), activation_client->GetActiveWindow());

  multi_user_window_manager()->ActiveUserChanged("B");
  EXPECT_EQ(window(2), activation_client->GetActiveWindow());

  multi_user_window_manager()->ActiveUserChanged("C");
  EXPECT_EQ(NULL, activation_client->GetActiveWindow());

  // Now test that a minimized window stays minimized upon switch and back.
  multi_user_window_manager()->ActiveUserChanged("A");
  wm::GetWindowState(window(0))->Minimize();

  multi_user_window_manager()->ActiveUserChanged("B");
  multi_user_window_manager()->ActiveUserChanged("A");
  EXPECT_TRUE(wm::GetWindowState(window(0))->IsMinimized());
  EXPECT_EQ(window(1), activation_client->GetActiveWindow());
}

// Test that Transient windows are handled properly.
TEST_F(MultiUserWindowManagerChromeOSTest, TransientWindows) {
  SetUpForThisManyWindows(10);

  // We create a hierarchy like this:
  //    0 (A)  4 (B)   7 (-)   - The top level owned/not owned windows
  //    |      |       |
  //    1      5 - 6   8       - Transient child of the owned windows.
  //    |              |
  //    2              9       - A transtient child of a transient child.
  //    |
  //    3                      - ..
  multi_user_window_manager()->SetWindowOwner(window(0), "A");
  multi_user_window_manager()->SetWindowOwner(window(4), "B");
  window(0)->AddTransientChild(window(1));
  // We first attach 2->3 and then 1->2 to see that the ownership gets
  // properly propagated through the sub tree upon assigning.
  window(2)->AddTransientChild(window(3));
  window(1)->AddTransientChild(window(2));
  window(4)->AddTransientChild(window(5));
  window(4)->AddTransientChild(window(6));
  window(7)->AddTransientChild(window(8));
  window(7)->AddTransientChild(window(9));

  // By now the hierarchy should have updated itself to show all windows of A
  // and hide all windows of B. Unowned windows should remain in what ever state
  // they are in.
  EXPECT_EQ("S[A], S[], S[], S[], H[B], H[], H[], S[], S[], S[]", GetStatus());

  // Trying to show a hidden transient window shouldn't change anything for now.
  window(5)->Show();
  window(6)->Show();
  EXPECT_EQ("S[A], S[], S[], S[], H[B], H[], H[], S[], S[], S[]", GetStatus());

  // Hiding on the other hand a shown window should work and hide also its
  // children. Note that hide will have an immediate impact on itself and all
  // transient children. It furthermore should remember this state when the
  // transient children are removed from its owner later on.
  window(2)->Hide();
  window(9)->Hide();
  EXPECT_EQ("S[A], S[], H[], H[], H[B], H[], H[], S[], S[], H[]", GetStatus());

  // Switching users and switch back should return to the previous state.
  multi_user_window_manager()->ActiveUserChanged("B");
  EXPECT_EQ("H[A], H[], H[], H[], S[B], S[], S[], S[], S[], H[]", GetStatus());
  multi_user_window_manager()->ActiveUserChanged("A");
  EXPECT_EQ("S[A], S[], H[], H[], H[B], H[], H[], S[], S[], H[]", GetStatus());

  // Removing a window from its transient parent should return to the previously
  // set visibility state.
  // Note: Window2 was explicitly hidden above and that state should remain.
  // Note furthermore that Window3 should also be hidden since it was hidden
  // implicitly by hiding Window2.
  // set hidden above).
  //    0 (A)  4 (B)   7 (-)   2(-)   3 (-)    6(-)
  //    |      |       |
  //    1      5       8
  //                   |
  //                   9
  window(2)->RemoveTransientChild(window(3));
  window(4)->RemoveTransientChild(window(6));
  EXPECT_EQ("S[A], S[], H[], H[], H[B], H[], S[], S[], S[], H[]", GetStatus());
  // Before we leave we need to reverse all transient window ownerships.
  window(0)->RemoveTransientChild(window(1));
  window(1)->RemoveTransientChild(window(2));
  window(4)->RemoveTransientChild(window(5));
  window(7)->RemoveTransientChild(window(8));
  window(7)->RemoveTransientChild(window(9));
}

// Test that the initial visibility state gets remembered.
TEST_F(MultiUserWindowManagerChromeOSTest, PreserveInitialVisibility) {
  SetUpForThisManyWindows(4);

  // Set our initial show state before we assign an owner.
  window(0)->Show();
  window(1)->Hide();
  window(2)->Show();
  window(3)->Hide();
  EXPECT_EQ("S[], H[], S[], H[]", GetStatus());

  // First test: The show state gets preserved upon user switch.
  multi_user_window_manager()->SetWindowOwner(window(0), "A");
  multi_user_window_manager()->SetWindowOwner(window(1), "A");
  multi_user_window_manager()->SetWindowOwner(window(2), "B");
  multi_user_window_manager()->SetWindowOwner(window(3), "B");
  EXPECT_EQ("S[A], H[A], H[B], H[B]", GetStatus());
  multi_user_window_manager()->ActiveUserChanged("B");
  EXPECT_EQ("H[A], H[A], S[B], H[B]", GetStatus());
  multi_user_window_manager()->ActiveUserChanged("A");
  EXPECT_EQ("S[A], H[A], H[B], H[B]", GetStatus());

  // Second test: Transferring the window to another desktop preserves the
  // show state.
  multi_user_window_manager()->ShowWindowForUser(window(0), "B");
  multi_user_window_manager()->ShowWindowForUser(window(1), "B");
  multi_user_window_manager()->ShowWindowForUser(window(2), "A");
  multi_user_window_manager()->ShowWindowForUser(window(3), "A");
  EXPECT_EQ("H[A,B], H[A,B], S[B,A], H[B,A]", GetStatus());
  multi_user_window_manager()->ActiveUserChanged("B");
  EXPECT_EQ("S[A,B], H[A,B], H[B,A], H[B,A]", GetStatus());
  multi_user_window_manager()->ActiveUserChanged("A");
  EXPECT_EQ("H[A,B], H[A,B], S[B,A], H[B,A]", GetStatus());
}

}  // namespace test
}  // namespace ash
