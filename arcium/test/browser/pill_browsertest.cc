// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The address bar behind the pill. It draws nothing of its own, and it is
// still there: the second test is the control, because everything the first
// one asserts is that things are hidden, which is also what hiding the whole
// bar would report.

#include <memory>

#include "arcium/test/browser/sidebar_ui_browsertest_base.h"
#include "arcium/ui/sidebar/url_pill_view.h"
#include "base/run_loop.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/permissions/permission_request_manager.h"
#include "components/permissions/request_type.h"
#include "components/permissions/test/mock_permission_request.h"
#include "content/public/test/browser_test.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/view.h"

namespace arcium::test {
namespace {

using PillTest = SidebarUiTest;

LocationBarView* BarOf(Browser* browser) {
  return BrowserView::GetBrowserViewForBrowser(browser)->GetLocationBarView();
}

IN_PROC_BROWSER_TEST_F(PillTest, TheBarBehindThePillDrawsNothingOfItsOwn) {
  ASSERT_TRUE(embedded_test_server()->Start());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));

  LocationBarView* bar = BarOf(browser());
  ASSERT_TRUE(bar);
  // Still drawn: Chrome's password and page information bubbles anchor here,
  // and a bar that is not drawn sends them to the top of the window.
  EXPECT_TRUE(bar->IsDrawn());
  EXPECT_EQ(nullptr, bar->GetBackground());
  for (views::View* child : bar->children()) {
    EXPECT_FALSE(child->GetVisible())
        << "a hosted bar shows nothing at rest: " << child->GetClassName();
  }
}

IN_PROC_BROWSER_TEST_F(PillTest, APermissionRequestStillHasSomewhereToAppear) {
  ASSERT_TRUE(embedded_test_server()->Start());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));

  content::WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  permissions::PermissionRequestManager* manager =
      permissions::PermissionRequestManager::FromWebContents(contents);
  ASSERT_TRUE(manager);
  manager->AddRequest(contents->GetPrimaryMainFrame(),
                      std::make_unique<permissions::MockPermissionRequest>(
                          permissions::RequestType::kGeolocation));
  base::RunLoop().RunUntilIdle();

  LocationBarView* bar = BarOf(browser());
  bool chip_visible = false;
  for (views::View* child : bar->children()) {
    chip_visible = chip_visible || child->GetVisible();
  }
  EXPECT_TRUE(chip_visible) << "the request has nowhere to appear";
  // And the pill stands aside for it.
  EXPECT_FALSE(Pill()->extensions_button_for_testing()->GetVisible());
}

}  // namespace
}  // namespace arcium::test
