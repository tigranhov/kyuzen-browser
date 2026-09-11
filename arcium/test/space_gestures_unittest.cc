// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// How the sidebar reads a two-finger swipe, and which colour mode a space's
// gradient is painted in. Both are decided from what the sidebar is handed,
// not from what it asks the platform: the phases on the scroll events, and
// the colour provider the window's theme produced.

#include <memory>
#include <utility>

#include "arcium/test/test_app_activation.h"
#include "arcium/ui/playground/fake_sidebar_model.h"
#include "arcium/ui/sidebar/sidebar_colors.h"
#include "arcium/ui/sidebar/sidebar_model.h"
#include "arcium/ui/sidebar/sidebar_view.h"
#include "arcium/ui/sidebar/space_gradients.h"
#include "arcium/ui/sidebar/tint_background.h"
#include "base/memory/raw_ptr.h"
#include "base/time/time.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/color/color_provider.h"
#include "ui/events/event.h"
#include "ui/events/event_constants.h"
#include "ui/events/event_sink.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/native_theme/native_theme.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace arcium {
namespace {

using Phase = ui::ScrollEventPhase;
using Momentum = ui::EventMomentumPhase;

class SpaceGesturesTest : public views::ViewsTestBase {
 public:
  void SetUp() override {
    // Before ViewsTestBase::SetUp(), which would otherwise promote this binary
    // to a foreground application and pull the desktop onto the suite's Space.
    arcium::test::SuppressTestAppActivation();
    views::ViewsTestBase::SetUp();
  }
};

ui::ScrollEvent MakeScroll(const gfx::PointF& at,
                           float dx,
                           float dy,
                           Phase phase,
                           Momentum momentum) {
  return ui::ScrollEvent(ui::EventType::kScroll, at, at, base::TimeTicks::Now(),
                         /*flags=*/0, dx, dy, dx, dy, /*finger_count=*/2,
                         momentum, phase);
}

// A trackpad scroll handed straight to the sidebar. The defaults are the
// phases every platform but macOS gives: kBegan, kUpdate, kEnd, with momentum
// marked separately. macOS leaves the scroll phase at kNone and says it all
// with the momentum phase: MAY_BEGIN when the fingers land, NONE while they
// move, END when they lift, INERTIAL_UPDATE while the momentum runs and END
// again when it stops. Returns whether the sidebar kept the event from the
// view under the fingers.
bool Scroll(SidebarView& view,
            float dx,
            float dy,
            Phase phase = Phase::kUpdate,
            Momentum momentum = Momentum::NONE) {
  ui::ScrollEvent event =
      MakeScroll(gfx::PointF(20, 300), dx, dy, phase, momentum);
  view.OnScrollEvent(&event);
  return event.handled();
}

// The same, sent through the widget at `at`, so it reaches the sidebar the
// way a real one does: by whichever view is under the fingers, and through
// the sidebar's handler that sees it first.
bool ScrollAt(views::Widget* widget,
              const gfx::Point& at,
              float dx,
              float dy,
              Phase phase,
              Momentum momentum) {
  ui::ScrollEvent event = MakeScroll(gfx::PointF(at), dx, dy, phase, momentum);
  const ui::EventDispatchDetails details =
      widget->GetEventSink()->OnEventFromSource(&event);
  EXPECT_FALSE(details.dispatcher_destroyed);
  return event.handled();
}

// Fingers moving left bring in the space to the right, the way a page turns;
// fingers moving right go back.
TEST_F(SpaceGesturesTest, AHorizontalSwipeSwitchesToTheNeighbouringSpace) {
  FakeSidebarModel model;
  model.AddSpaceForTesting(u"Work", u"", 0);
  SidebarView view(&model, SidebarView::Delegate());

  Scroll(view, 0, 0, Phase::kBegan);
  Scroll(view, -20, 0);  // not far enough yet
  EXPECT_TRUE(model.spaces()[0].is_active);
  EXPECT_TRUE(Scroll(view, -50, 0));
  EXPECT_TRUE(model.spaces()[1].is_active);
  // The rest of the same gesture is spent: one swipe is one space.
  Scroll(view, -100, 0);
  Scroll(view, 0, 0, Phase::kEnd);
  EXPECT_TRUE(model.spaces()[1].is_active);

  // A mostly vertical scroll is the column's to scroll, not a switch.
  Scroll(view, 0, 0, Phase::kBegan);
  EXPECT_FALSE(Scroll(view, 80, 200));
  Scroll(view, 0, 0, Phase::kEnd);
  EXPECT_TRUE(model.spaces()[1].is_active);

  Scroll(view, 0, 0, Phase::kBegan);
  EXPECT_TRUE(Scroll(view, 80, 0));
  EXPECT_TRUE(model.spaces()[0].is_active);
}

// Fingers scrolling the Today list drift sideways, and over a long list the
// drift adds up to more than a swipe. The gesture's first movement was
// vertical, so it stays a scroll however far it drifts, and the column gets
// every event, the sideways-leaning ones included: taking those would drop
// their vertical part and the list would stutter.
TEST_F(SpaceGesturesTest, AVerticalScrollThatDriftsSidewaysStaysAScroll) {
  FakeSidebarModel model;
  model.AddSpaceForTesting(u"Work", u"", 0);
  SidebarView view(&model, SidebarView::Delegate());

  EXPECT_FALSE(Scroll(view, 0, 0, Phase::kBegan));
  EXPECT_FALSE(Scroll(view, 0, 30));
  for (int i = 0; i < 10; ++i) {
    EXPECT_FALSE(Scroll(view, -10, 4)) << i;
    EXPECT_FALSE(Scroll(view, 0, 30)) << i;
  }
  EXPECT_FALSE(Scroll(view, 0, 0, Phase::kEnd));
  EXPECT_TRUE(model.spaces()[0].is_active);

  // The same scroll as macOS phases it, momentum and all.
  EXPECT_FALSE(Scroll(view, 0, 0, Phase::kNone, Momentum::MAY_BEGIN));
  EXPECT_FALSE(Scroll(view, 0, 30, Phase::kNone));
  for (int i = 0; i < 10; ++i) {
    EXPECT_FALSE(Scroll(view, -10, 4, Phase::kNone)) << i;
    EXPECT_FALSE(Scroll(view, 0, 30, Phase::kNone)) << i;
  }
  EXPECT_FALSE(Scroll(view, 0, 0, Phase::kNone, Momentum::END));
  for (int i = 0; i < 5; ++i) {
    EXPECT_FALSE(Scroll(view, -10, 4, Phase::kNone, Momentum::INERTIAL_UPDATE))
        << i;
  }
  EXPECT_FALSE(Scroll(view, 0, 0, Phase::kNone, Momentum::END));
  EXPECT_TRUE(model.spaces()[0].is_active);
}

// A swipe over the rows, as the widget delivers it. However far the fingers
// and then the momentum carry it, one swipe moves one space: with four spaces
// a second switch would show.
TEST_F(SpaceGesturesTest, ASwipeOverTheRowsSwitchesOnceMomentumIncluded) {
  FakeSidebarModel model;
  const SpaceId work = model.AddSpaceForTesting(u"Work", u"", 0);
  const SpaceId play = model.AddSpaceForTesting(u"Play", u"", 0);
  const SpaceId more = model.AddSpaceForTesting(u"More", u"", 0);
  model.AddTab(u"Home tab", "https://h.example/", SidebarSection::kToday, true);
  model.AddTabInSpaceForTesting(u"Work tab", "https://w.example/", work);
  model.AddTabInSpaceForTesting(u"Play tab", "https://p.example/", play);
  model.AddTabInSpaceForTesting(u"More tab", "https://m.example/", more);
  std::unique_ptr<views::Widget> widget =
      CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  widget->SetBounds(gfx::Rect(0, 0, 250, 600));
  SidebarView* view = widget->SetContentsView(
      std::make_unique<SidebarView>(&model, SidebarView::Delegate()));
  widget->LayoutRootViewIfNecessary();

  // A point in the scrolling column, not on the sidebar's own background:
  // the column would take every scroll there if the sidebar did not see it
  // first.
  views::View* viewport = view->column_for_testing()->parent();
  gfx::Point at = viewport->GetLocalBounds().CenterPoint();
  views::View::ConvertPointToWidget(viewport, &at);
  ASSERT_TRUE(
      viewport->Contains(widget->GetRootView()->GetEventHandlerForPoint(at)));

  // As macOS sends it.
  ScrollAt(widget.get(), at, 0, 0, Phase::kNone, Momentum::MAY_BEGIN);
  EXPECT_TRUE(ScrollAt(widget.get(), at, -20, 0, Phase::kNone, Momentum::NONE));
  EXPECT_TRUE(model.spaces()[0].is_active);
  EXPECT_TRUE(ScrollAt(widget.get(), at, -50, 0, Phase::kNone, Momentum::NONE));
  EXPECT_TRUE(model.spaces()[1].is_active);
  EXPECT_TRUE(
      ScrollAt(widget.get(), at, -100, 0, Phase::kNone, Momentum::NONE));
  EXPECT_TRUE(ScrollAt(widget.get(), at, 0, 0, Phase::kNone, Momentum::END));
  for (int i = 0; i < 3; ++i) {
    EXPECT_TRUE(ScrollAt(widget.get(), at, -60, 0, Phase::kNone,
                         Momentum::INERTIAL_UPDATE))
        << i;
  }
  ScrollAt(widget.get(), at, 0, 0, Phase::kNone, Momentum::END);
  EXPECT_TRUE(model.spaces()[1].is_active);

  // As other platforms send it.
  ScrollAt(widget.get(), at, 0, 0, Phase::kBegan, Momentum::NONE);
  EXPECT_TRUE(
      ScrollAt(widget.get(), at, -30, 0, Phase::kUpdate, Momentum::NONE));
  EXPECT_TRUE(
      ScrollAt(widget.get(), at, -40, 0, Phase::kUpdate, Momentum::NONE));
  EXPECT_TRUE(model.spaces()[2].is_active);
  EXPECT_TRUE(ScrollAt(widget.get(), at, 0, 0, Phase::kEnd, Momentum::NONE));
  for (int i = 0; i < 3; ++i) {
    EXPECT_TRUE(ScrollAt(widget.get(), at, -60, 0, Phase::kNone,
                         Momentum::INERTIAL_UPDATE))
        << i;
  }
  ScrollAt(widget.get(), at, 0, 0, Phase::kNone, Momentum::END);
  EXPECT_TRUE(model.spaces()[2].is_active);
}

// A window whose colours come from a provider the test fills in. The
// browser's window has the same shape: its theme service picks the colour
// mode -- dark when off the record, or whatever the browser's own Light and
// Dark setting says -- and the OS appearance may say the opposite.
class ThemedWidget : public views::Widget {
 public:
  ui::ColorProvider& provider() { return provider_; }

  // views::Widget:
  const ui::ColorProvider* GetColorProvider() const override {
    return &provider_;
  }

 private:
  ui::ColorProvider provider_;
};

// Sets the OS appearance `widget`'s native theme reports, and restores it.
class ScopedOsAppearance {
 public:
  ScopedOsAppearance(views::Widget* widget, bool dark)
      : theme_(widget->GetNativeTheme()),
        was_(theme_->preferred_color_scheme()) {
    theme_->set_preferred_color_scheme(
        dark ? ui::NativeTheme::PreferredColorScheme::kDark
             : ui::NativeTheme::PreferredColorScheme::kLight);
  }
  ~ScopedOsAppearance() { theme_->set_preferred_color_scheme(was_); }

 private:
  raw_ptr<ui::NativeTheme> theme_;
  const ui::NativeTheme::PreferredColorScheme was_;
};

// The row text is whatever the colour mixer chose for the window's mode, so a
// preset has to be painted in that same mode, or dark text lands on a dark
// tint. The mixer's own sidebar colour is how the tint knows the mode.
TEST_F(SpaceGesturesTest, APresetIsPaintedInTheModeTheColoursWereMixedFor) {
  FakeSidebarModel model;
  ThemedWidget widget;
  widget.Init(CreateParams(views::Widget::InitParams::CLIENT_OWNS_WIDGET,
                           views::Widget::InitParams::TYPE_WINDOW_FRAMELESS));
  widget.SetBounds(gfx::Rect(0, 0, 250, 600));
  SidebarView* view = widget.SetContentsView(
      std::make_unique<SidebarView>(&model, SidebarView::Delegate()));
  TintBackground tint;
  tint.SetPreset(1);
  const SpaceGradient& preset = SpaceGradients()[1];

  {
    ScopedOsAppearance os(&widget, /*dark=*/false);
    widget.provider().SetColorForTesting(kColorArciumSidebarBackgroundBottom,
                                         SkColorSetRGB(0x17, 0x17, 0x1D));
    const TintBackground::Stops stops = tint.StopsFor(*view);
    EXPECT_EQ(preset.dark_top, stops.top);
    EXPECT_EQ(preset.dark_bottom, stops.bottom);
  }
  {
    ScopedOsAppearance os(&widget, /*dark=*/true);
    widget.provider().SetColorForTesting(kColorArciumSidebarBackgroundBottom,
                                         SkColorSetRGB(0xF2, 0xF2, 0xF6));
    const TintBackground::Stops stops = tint.StopsFor(*view);
    EXPECT_EQ(preset.light_top, stops.top);
    EXPECT_EQ(preset.light_bottom, stops.bottom);
  }
}

}  // namespace
}  // namespace arcium
