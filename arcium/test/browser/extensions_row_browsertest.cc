// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Pinned extensions in the sidebar. The row takes no height when nothing is
// pinned, grows when something is, wraps rather than clipping, and the
// buttons in it still work -- a button that draws and does nothing would be
// worse than no button at all.

#include <string>
#include <vector>

#include "arcium/test/browser/sidebar_ui_browsertest_base.h"
#include "arcium/ui/sidebar/extensions_row_view.h"
#include "arcium/ui/sidebar/sidebar_metrics.h"
#include "chrome/browser/ui/views/extensions/extensions_toolbar_button.h"
#include "chrome/browser/ui/views/extensions/extensions_toolbar_desktop.h"
#include "chrome/browser/ui/views/toolbar/toolbar_action_view.h"
#include "content/public/test/browser_test.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/animation/ink_drop_host.h"
#include "ui/views/layout/animating_layout_manager_test_util.h"
#include "ui/views/test/views_test_utils.h"
#include "ui/views/view.h"

namespace arcium::test {
namespace {

using ExtensionsRowTest = SidebarUiTest;

IN_PROC_BROWSER_TEST_F(ExtensionsRowTest,
                       APinnedExtensionIsAButtonInTheSidebar) {
  ASSERT_TRUE(Row()->has_hosted_view());
  // Nothing pinned: the row takes no height, so an empty row is not a gap.
  EXPECT_EQ(0, Row()->GetPreferredSize(views::SizeBounds()).height());

  const std::string id = LoadTestExtension();
  PinExtension(id);
  RunLoopUntilIdle();
  views::test::WaitForAnimatingLayoutManager(Container());

  EXPECT_GT(Row()->GetPreferredSize(views::SizeBounds()).height(), 0);
  views::View* button = Container()->GetViewForId(id);
  ASSERT_TRUE(button);
  EXPECT_TRUE(button->GetVisible());
  EXPECT_TRUE(Row()->Contains(button));

  // The menu button is not in the row; the pill's button opens the menu. It
  // is not hidden, because the strip decides which of its own buttons show
  // and takes the pinned ones down with it when that answer is argued with.
  // The row puts it where its own bounds cut it off instead.
  EXPECT_FALSE(Row()->GetLocalBounds().Intersects(
      Container()->GetExtensionsButton()->bounds()))
      << "the strip's menu button is drawn inside the row";

  // Nor over the pill while it is lit, as it is while the menu hanging from
  // it is open. A lit button draws on a layer of its own, and the row's edge
  // cut off only what painted into the strip, so a large puzzle piece sat
  // over the pill.
  views::View* menu = Container()->GetExtensionsButton();
  views::InkDrop::Get(menu)->GetInkDrop()->SnapToActivated();
  // The button puts its highlight and its icon on layers of child views.
  std::vector<ui::Layer*> layers;
  std::vector<views::View*> pending = {menu};
  while (!pending.empty()) {
    views::View* view = pending.back();
    pending.pop_back();
    if (view->layer()) {
      layers.push_back(view->layer());
    }
    for (views::View* child : view->children()) {
      pending.push_back(child);
    }
  }
  ASSERT_FALSE(layers.empty()) << "a lit button draws on a layer of its own";
  ASSERT_TRUE(Row()->layer());
  ASSERT_TRUE(Row()->layer()->GetMasksToBounds())
      << "the row does not cut off layers at its edge";
  for (ui::Layer* layer : layers) {
    EXPECT_TRUE(Row()->layer()->Contains(layer))
        << "the lit menu button draws outside the row, over the pill";
  }
}

IN_PROC_BROWSER_TEST_F(ExtensionsRowTest, TheStripDrawsNoOutlineAroundItself) {
  const std::string id = LoadTestExtension();
  PinExtension(id);
  RunLoopUntilIdle();
  views::test::WaitForAnimatingLayoutManager(Container());

  // Chromium outlines the strip while the pointer is over it, on a layer of
  // its own beside the strip's. In a toolbar the outline hugs the icons; here
  // the strip is as wide as the sidebar, so it drew a large, mostly empty box
  // around a few small buttons, with the buttons in its corner.
  EXPECT_TRUE(Container()->GetLayersInOrder(views::ViewLayer::kExclude).empty())
      << "the strip still carries its outline";
}

IN_PROC_BROWSER_TEST_F(ExtensionsRowTest, UnpinningTakesItBackOut) {
  const std::string id = LoadTestExtension();
  PinExtension(id);
  RunLoopUntilIdle();
  views::test::WaitForAnimatingLayoutManager(Container());
  ASSERT_GT(Row()->GetPreferredSize(views::SizeBounds()).height(), 0);

  UnpinExtension(id);
  RunLoopUntilIdle();
  views::test::WaitForAnimatingLayoutManager(Container());
  EXPECT_EQ(0, Row()->GetPreferredSize(views::SizeBounds()).height());
}

IN_PROC_BROWSER_TEST_F(ExtensionsRowTest,
                       NineOfThemWrapRatherThanBeingClipped) {
  std::vector<std::string> ids;
  for (int i = 0; i < 9; ++i) {
    ids.push_back(LoadTestExtension());
    PinExtension(ids.back());
  }
  RunLoopUntilIdle();
  views::test::WaitForAnimatingLayoutManager(Container());
  Row()->SetSize(gfx::Size(metrics::kSidebarWidth, 200));
  views::test::RunScheduledLayout(Row());

  EXPECT_GT(Row()->GetPreferredSize(views::SizeBounds()).height(),
            metrics::kExtensionButtonSize)
      << "nine buttons must take more than one line";
  for (const std::string& id : ids) {
    views::View* button = Container()->GetViewForId(id);
    ASSERT_TRUE(button) << id;
    EXPECT_TRUE(Row()->GetLocalBounds().Contains(button->bounds()))
        << "a pinned extension was clipped out of the row";
  }
}

}  // namespace
}  // namespace arcium::test
