// Copyright 2026 The Kyuzen Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Moving the window. The sidebar has places to grab it by, but the page runs
// to the window's edges, so beside the sidebar there was nothing at all: a
// window could be dragged only by its sidebar. A thin band along the top of
// the page moves the window too, without the page looking any different.
//
// The tests ask BrowserView::NonClientHitTest, which is what the frame asks,
// rather than the controller's own function, so they fail if the hook in
// patch 0050 is not applied.

#include "arcium/test/browser/sidebar_ui_browsertest_base.h"
#include "arcium/ui/browser/browser_sidebar_controller.h"
#include "arcium/ui/sidebar/sidebar_metrics.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/test/browser_test.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/hit_test.h"
#include "ui/gfx/geometry/point.h"

namespace arcium::test {
namespace {

class WindowDragTest : public SidebarUiTest {
 protected:
  BrowserView* View() {
    return BrowserView::GetBrowserViewForBrowser(browser());
  }

  int HitTestAt(int x, int y) {
    return View()->NonClientHitTest(gfx::Point(x, y));
  }

  // A point beside the sidebar, over the page.
  int PageX() { return Controller()->width() + 100; }
};

IN_PROC_BROWSER_TEST_F(WindowDragTest, TheTopOfTheWindowMovesIt) {
  EXPECT_EQ(HTCAPTION, HitTestAt(PageX(), 0));
  EXPECT_EQ(HTCAPTION, HitTestAt(PageX(), metrics::kWindowTopGrabHeight - 1));
  // Over the sidebar too: its first row is as bare as the page is.
  EXPECT_EQ(HTCAPTION, HitTestAt(Controller()->width() / 2, 0));
}

IN_PROC_BROWSER_TEST_F(WindowDragTest, ThePageItselfStillTakesItsClicks) {
  // One pixel below the band the page is the page again, or a site's own top
  // bar would stop answering.
  EXPECT_NE(HTCAPTION, HitTestAt(PageX(), metrics::kWindowTopGrabHeight));
  EXPECT_NE(HTCAPTION, HitTestAt(PageX(), 200));
}

IN_PROC_BROWSER_TEST_F(WindowDragTest, AFullScreenWindowHasNothingToMove) {
  // Nothing to drag and nowhere to drag it, so the page keeps every pixel.
  ui_test_utils::ToggleFullscreenModeAndWait(browser());
  ASSERT_TRUE(View()->IsFullscreen());
  EXPECT_NE(HTCAPTION, HitTestAt(PageX(), 0));
}

}  // namespace
}  // namespace arcium::test
