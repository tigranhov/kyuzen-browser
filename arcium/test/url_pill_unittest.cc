// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// What the pill shows and when. At rest it is a domain and nothing else; the
// buttons arrive on hover or on keyboard focus, the warning arrives unasked,
// and everything goes away while the bar behind the pill has a question of
// its own.

#include <memory>
#include <utility>

#include "arcium/test/test_app_activation.h"
#include "arcium/ui/sidebar/sidebar_metrics.h"
#include "arcium/ui/sidebar/url_pill_view.h"
#include "base/auto_reset.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/events/test/event_generator.h"
#include "ui/events/test/test_event.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/test/views_test_utils.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_utils.h"
#include "url/gurl.h"

namespace arcium {
namespace {

class UrlPillTest : public views::ViewsTestBase {
 public:
  void SetUp() override {
    // Before ViewsTestBase::SetUp(), so the pointer test below does not pull
    // the desktop onto the suite's Space.
    arcium::test::SuppressTestAppActivation();
    views::ViewsTestBase::SetUp();
    widget_ = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
    UrlPillView::Actions actions;
    actions.open_box = base::BindRepeating(&UrlPillTest::Count,
                                           base::Unretained(this), &boxes_);
    actions.open_extensions = base::BindRepeating(
        &UrlPillTest::Count, base::Unretained(this), &extensions_);
    actions.copy_link = base::BindRepeating(&UrlPillTest::Count,
                                            base::Unretained(this), &copies_);
    actions.open_site_info = base::BindRepeating(
        &UrlPillTest::Count, base::Unretained(this), &site_infos_);
    pill_ = widget_->SetContentsView(
        std::make_unique<UrlPillView>(std::move(actions)));
    widget_->Show();
  }

  void TearDown() override {
    pill_ = nullptr;
    widget_.reset();
    views::ViewsTestBase::TearDown();
  }

 protected:
  void Count(int* counter) { ++*counter; }

  std::unique_ptr<views::Widget> widget_;
  raw_ptr<UrlPillView> pill_ = nullptr;
  int boxes_ = 0;
  int extensions_ = 0;
  int copies_ = 0;
  int site_infos_ = 0;
  base::AutoReset<bool> no_animation_ =
      UrlPillView::DisableRevealAnimationForTesting();
};

TEST_F(UrlPillTest, AtRestItIsTextAndNothingElse) {
  pill_->SetUrl(GURL("https://www.google.com/"));
  pill_->SetConnectionSecure(true);
  EXPECT_EQ(u"google.com", pill_->label_for_testing());
  EXPECT_FALSE(pill_->site_button_for_testing()->GetVisible());
  EXPECT_FALSE(pill_->extensions_button_for_testing()->GetVisible());
  EXPECT_FALSE(pill_->copy_button_for_testing()->GetVisible());
}

TEST_F(UrlPillTest, HoveringRevealsAllThree) {
  pill_->SetUrl(GURL("https://www.google.com/"));
  pill_->SetConnectionSecure(true);
  pill_->SetRevealedForTesting(true);
  EXPECT_TRUE(pill_->site_button_for_testing()->GetVisible());
  EXPECT_TRUE(pill_->extensions_button_for_testing()->GetVisible());
  EXPECT_TRUE(pill_->copy_button_for_testing()->GetVisible());

  // And they go again. Without this the test would pass against a pill that
  // reveals once and never hides.
  pill_->SetRevealedForTesting(false);
  EXPECT_FALSE(pill_->extensions_button_for_testing()->GetVisible());
}

TEST_F(UrlPillTest, KeyboardFocusRevealsThemToo) {
  pill_->SetUrl(GURL("https://www.google.com/"));
  ASSERT_FALSE(pill_->extensions_button_for_testing()->GetVisible());
  // A control a pointer is the only way to reach is a control some people
  // cannot reach at all.
  pill_->RequestFocus();
  EXPECT_TRUE(pill_->extensions_button_for_testing()->GetVisible());
}

TEST_F(UrlPillTest, AnInsecureConnectionSaysSoWithoutBeingAskedTo) {
  pill_->SetUrl(GURL("http://example.com/"));
  pill_->SetConnectionSecure(false);
  EXPECT_TRUE(pill_->site_button_for_testing()->GetVisible());
  // Only that one: the other two still wait to be hovered.
  EXPECT_FALSE(pill_->extensions_button_for_testing()->GetVisible());
}

TEST_F(UrlPillTest, WhenTheBarHasSomethingToSayThePillGetsOutOfTheWay) {
  pill_->SetUrl(GURL("http://example.com/"));
  pill_->SetConnectionSecure(false);
  pill_->SetRevealedForTesting(true);
  pill_->SetHostedBarSpeaking(true);
  EXPECT_FALSE(pill_->site_button_for_testing()->GetVisible());
  EXPECT_FALSE(pill_->extensions_button_for_testing()->GetVisible());
  EXPECT_FALSE(pill_->copy_button_for_testing()->GetVisible());

  pill_->SetHostedBarSpeaking(false);
  EXPECT_TRUE(pill_->extensions_button_for_testing()->GetVisible());
}

TEST_F(UrlPillTest, ReachingForAButtonDoesNotHideIt) {
  pill_->SetUrl(GURL("https://www.google.com/"));
  widget_->SetBounds(
      gfx::Rect(0, 0, metrics::kSidebarWidth, metrics::kUrlPillHeight));
  // Real routing, not SetRevealedForTesting: the defect is in which view the
  // event processor calls entered and exited on, and a direct call skips it.
  ui::test::EventGenerator generator(views::GetRootWindow(widget_.get()));
  views::test::RunScheduledLayout(widget_.get());
  generator.MoveMouseTo(pill_->GetBoundsInScreen().CenterPoint());
  views::test::RunScheduledLayout(widget_.get());

  views::ImageButton* copy = pill_->copy_button_for_testing();
  ASSERT_TRUE(copy->GetVisible()) << "hovering the pill must reveal it";
  const gfx::Point on_button = copy->GetBoundsInScreen().CenterPoint();
  ASSERT_FALSE(copy->GetBoundsInScreen().Contains(
      pill_->GetBoundsInScreen().CenterPoint()))
      << "the button must sit somewhere the pill's centre is not";

  // The pill read the pointer arriving on its own button as the pointer
  // leaving, and hid the buttons -- which put the pointer back on the pill,
  // which showed them again, for as long as it moved.
  generator.MoveMouseTo(on_button);
  views::test::RunScheduledLayout(widget_.get());
  EXPECT_TRUE(copy->GetVisible())
      << "the button hid itself when the pointer reached it";
}

TEST_F(UrlPillTest, TheAddressStaysPutWhenTheButtonsArrive) {
  pill_->SetUrl(GURL("https://www.google.com/"));
  pill_->SetConnectionSecure(true);
  widget_->SetBounds(
      gfx::Rect(0, 0, metrics::kSidebarWidth, metrics::kUrlPillHeight));
  views::test::RunScheduledLayout(widget_.get());
  const int at_rest = pill_->text_for_testing()->x();

  // The site button arrives at the start of the pill, and it used to push the
  // address along to make room for itself.
  pill_->SetRevealedForTesting(true);
  views::test::RunScheduledLayout(widget_.get());
  ASSERT_TRUE(pill_->site_button_for_testing()->GetVisible());
  EXPECT_EQ(at_rest, pill_->text_for_testing()->x())
      << "the address moved when the buttons arrived";

  // A warning shows unasked, and the address sits where it always does.
  pill_->SetRevealedForTesting(false);
  pill_->SetConnectionSecure(false);
  views::test::RunScheduledLayout(widget_.get());
  EXPECT_EQ(at_rest, pill_->text_for_testing()->x())
      << "the address sits elsewhere beside a warning";
}

TEST_F(UrlPillTest, AQuestionFromTheBarIsClickableAllTheWayAcross) {
  widget_->SetBounds(
      gfx::Rect(0, 0, metrics::kSidebarWidth, metrics::kUrlPillHeight));
  views::View* bar = pill_->SetHostedView(std::make_unique<views::View>());
  pill_->SetHostedBarSpeaking(true);
  views::test::RunScheduledLayout(widget_.get());

  // The bar asks its question from the start of the pill, where the site
  // button's room is kept. That room must not take the click meant for it.
  const gfx::Point at_start(metrics::kPillButtonSize / 2 + 8,
                            metrics::kUrlPillHeight / 2);
  EXPECT_EQ(bar, pill_->GetEventHandlerForPoint(at_start))
      << "the pill's own room at its start covers the bar's question";
}

TEST_F(UrlPillTest, EachButtonDoesItsOwnJob) {
  pill_->SetUrl(GURL("https://www.google.com/"));
  pill_->SetRevealedForTesting(true);
  views::test::ButtonTestApi(pill_->site_button_for_testing())
      .NotifyClick(ui::test::TestEvent());
  views::test::ButtonTestApi(pill_->extensions_button_for_testing())
      .NotifyClick(ui::test::TestEvent());
  views::test::ButtonTestApi(pill_->copy_button_for_testing())
      .NotifyClick(ui::test::TestEvent());
  EXPECT_EQ(1, site_infos_);
  EXPECT_EQ(1, extensions_);
  EXPECT_EQ(1, copies_);
  EXPECT_EQ(0, boxes_);
}

}  // namespace
}  // namespace arcium
