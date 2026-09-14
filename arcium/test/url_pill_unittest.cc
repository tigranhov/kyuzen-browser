// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// What the pill shows and when. At rest it is a domain and nothing else; the
// buttons arrive on hover or on keyboard focus, the warning arrives unasked,
// and everything goes away while the bar behind the pill has a question of
// its own.

#include <memory>
#include <utility>

#include "arcium/ui/sidebar/url_pill_view.h"
#include "base/auto_reset.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/events/test/test_event.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace arcium {
namespace {

class UrlPillTest : public views::ViewsTestBase {
 public:
  void SetUp() override {
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
  EXPECT_EQ(u"google.com", pill_->domain_for_testing());
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
