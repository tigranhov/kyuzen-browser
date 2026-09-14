// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_TEST_BROWSER_SIDEBAR_UI_BROWSERTEST_BASE_H_
#define ARCIUM_TEST_BROWSER_SIDEBAR_UI_BROWSERTEST_BASE_H_

#include <string>

#include "base/auto_reset.h"
#include "base/files/scoped_temp_dir.h"
#include "chrome/test/base/in_process_browser_test.h"

class ExtensionsToolbarDesktop;

namespace arcium {

class BrowserSidebarController;
class ExtensionsRowView;
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
  ExtensionsRowView* Row();
  ExtensionsToolbarDesktop* Container();

  // Writes a minimal extension with a button of its own and loads it
  // unpacked, so these tests need no store account and no network. Each call
  // writes a differently named one, which is what the wrapping test needs.
  std::string LoadTestExtension();
  void PinExtension(const std::string& id);
  void UnpinExtension(const std::string& id);
  void RunLoopUntilIdle();

 private:
  // A reveal that fades would make every assertion about what is on screen
  // wait for an animation to land.
  base::AutoReset<bool> no_reveal_animation_;
  base::ScopedTempDir extensions_dir_;
  int extensions_written_ = 0;
};

}  // namespace test
}  // namespace arcium

#endif  // ARCIUM_TEST_BROWSER_SIDEBAR_UI_BROWSERTEST_BASE_H_
