// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Where the pinned-extensions row puts its buttons, inside a whole sidebar.
// Chromium draws every extension icon at 16 points whatever button it is in,
// so the row's job is to fit that icon closely, in the column the favicons
// below it already make.

#include <memory>
#include <vector>

#include "arcium/test/test_app_activation.h"
#include "arcium/ui/playground/fake_sidebar_model.h"
#include "arcium/ui/sidebar/extensions_row_view.h"
#include "arcium/ui/sidebar/sidebar_metrics.h"
#include "arcium/ui/sidebar/sidebar_model.h"
#include "arcium/ui/sidebar/sidebar_view.h"
#include "arcium/ui/sidebar/tab_row_view.h"
#include "arcium/ui/sidebar/url_pill_view.h"
#include "base/memory/raw_ptr.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/test/views_test_utils.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

namespace arcium {
namespace {

class ExtensionsRowLayoutTest : public views::ViewsTestBase {
 public:
  void SetUp() override {
    // Before ViewsTestBase::SetUp(), so the suite does not pull the desktop
    // onto its Space.
    arcium::test::SuppressTestAppActivation();
    views::ViewsTestBase::SetUp();
    model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned,
                  true);
    widget_ = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
    widget_->SetBounds(gfx::Rect(0, 0, metrics::kSidebarWidth, 600));
    sidebar_ = widget_->SetContentsView(
        std::make_unique<SidebarView>(&model_, SidebarView::Delegate()));
    widget_->Show();
  }

  void TearDown() override {
    buttons_.clear();
    sidebar_ = nullptr;
    widget_.reset();
    views::ViewsTestBase::TearDown();
  }

 protected:
  // Stands in for Chromium's strip: a view whose children are the buttons.
  void Pin(int count) {
    auto strip = std::make_unique<views::View>();
    for (int i = 0; i < count; ++i) {
      buttons_.push_back(strip->AddChildView(std::make_unique<views::View>()));
    }
    sidebar_->extensions_row()->SetHostedView(std::move(strip));
    views::test::RunScheduledLayout(widget_.get());
  }

  // Where the 16-point icon Chromium draws centred in `button` lands, in the
  // sidebar's coordinates.
  gfx::Rect IconBounds(const views::View* button) const {
    gfx::Rect cell = button->ConvertRectToWidget(button->GetLocalBounds());
    cell.ClampToCenteredSize(
        gfx::Size(metrics::kFaviconSize, metrics::kFaviconSize));
    return cell;
  }

  gfx::Rect FaviconBounds() const {
    for (const views::View* view : AllDescendants(sidebar_)) {
      if (const auto* row = views::AsViewClass<TabRowView>(view)) {
        auto* favicon = const_cast<TabRowView*>(row)->favicon_for_testing();
        return favicon->ConvertRectToWidget(favicon->GetImageBounds());
      }
    }
    ADD_FAILURE() << "no tab row in the sidebar";
    return gfx::Rect();
  }

  // The first child of the sidebar after the row that takes up room.
  const views::View* NextBelowTheRow() const {
    bool past_row = false;
    for (const views::View* child : sidebar_->children()) {
      if (child == sidebar_->extensions_row()) {
        past_row = true;
      } else if (past_row && child->GetVisible() && !child->size().IsEmpty()) {
        return child;
      }
    }
    return nullptr;
  }

  FakeSidebarModel model_;
  std::unique_ptr<views::Widget> widget_;
  raw_ptr<SidebarView> sidebar_ = nullptr;
  std::vector<raw_ptr<views::View>> buttons_;

 private:
  static std::vector<const views::View*> AllDescendants(
      const views::View* root) {
    std::vector<const views::View*> all;
    std::vector<const views::View*> pending = {root};
    while (!pending.empty()) {
      const views::View* view = pending.back();
      pending.pop_back();
      all.push_back(view);
      for (const views::View* child : view->children()) {
        pending.push_back(child);
      }
    }
    return all;
  }
};

TEST_F(ExtensionsRowLayoutTest, TheFirstIconSitsInTheFaviconsColumn) {
  Pin(1);
  EXPECT_EQ(FaviconBounds().x(), IconBounds(buttons_[0]).x())
      << "the extension icon and the favicons below it start in different "
         "places";
}

TEST_F(ExtensionsRowLayoutTest, EightFitALineAndSitCloseToTheirIcons) {
  Pin(9);
  const int line_one = buttons_[0]->y();
  for (int i = 1; i < 8; ++i) {
    EXPECT_EQ(line_one, buttons_[i]->y()) << "button " << i << " wrapped";
    // What the eye reads as the space between two extensions is the space
    // between their icons, not between their buttons.
    EXPECT_EQ(12,
              IconBounds(buttons_[i]).x() - IconBounds(buttons_[i - 1]).right())
        << "between icons " << i - 1 << " and " << i;
  }
  EXPECT_GT(buttons_[8]->y(), line_one) << "the ninth did not wrap";
  EXPECT_EQ(IconBounds(buttons_[0]).x(), IconBounds(buttons_[8]).x())
      << "the second line starts somewhere else";
  EXPECT_EQ(12, IconBounds(buttons_[8]).y() - IconBounds(buttons_[0]).bottom())
      << "between the lines";
  // And nothing was pushed outside the sidebar to make eight fit.
  EXPECT_LE(
      buttons_[7]->ConvertRectToWidget(buttons_[7]->GetLocalBounds()).right(),
      metrics::kSidebarWidth - metrics::kSidebarPadding);
}

TEST_F(ExtensionsRowLayoutTest, TheRowSitsNearerThePillThanWhatFollows) {
  Pin(1);
  const views::View* row = sidebar_->extensions_row();
  const views::View* below = NextBelowTheRow();
  ASSERT_TRUE(below);
  EXPECT_EQ(4, row->y() - sidebar_->url_pill()->bounds().bottom());
  EXPECT_EQ(6, below->y() - row->bounds().bottom());
}

TEST_F(ExtensionsRowLayoutTest, WithNothingPinnedTheRowTakesNoRoomAtAll) {
  Pin(0);
  const views::View* below = NextBelowTheRow();
  ASSERT_TRUE(below);
  // The same gap as between any two neighbours: the row's own spacing goes
  // with it.
  EXPECT_EQ(6, below->y() - sidebar_->url_pill()->bounds().bottom());
}

}  // namespace
}  // namespace arcium
