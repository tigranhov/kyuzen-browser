// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_TEST_BROWSER_SIDEBAR_UI_BROWSERTEST_BASE_H_
#define ARCIUM_TEST_BROWSER_SIDEBAR_UI_BROWSERTEST_BASE_H_

#include "base/auto_reset.h"
#include "chrome/test/base/in_process_browser_test.h"

namespace arcium {

class BrowserSidebarController;
class UrlPillView;

namespace test {

// A real browser with the sidebar up, and short ways to reach the pieces of
// it these tests talk about. Shared by every test of the pill, the extensions
// row and the command box, which all need the same handful of lookups.
class SidebarUiTest : public InProcessBrowserTest {
 public:
  SidebarUiTest();
  ~SidebarUiTest() override;

  void SetUpOnMainThread() override;

 protected:
  BrowserSidebarController* Controller();
  UrlPillView* Pill();

 private:
  // A reveal that fades would make every assertion about what is on screen
  // wait for an animation to land.
  base::AutoReset<bool> no_reveal_animation_;
};

}  // namespace test
}  // namespace arcium

#endif  // ARCIUM_TEST_BROWSER_SIDEBAR_UI_BROWSERTEST_BASE_H_
