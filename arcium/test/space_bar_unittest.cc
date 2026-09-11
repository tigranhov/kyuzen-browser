// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The space bar and what it draws from. FakeSidebarModel has to report
// spaces() and switch between them the way the real model does, since every
// view test of the bar runs over it.

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include "arcium/test/test_app_activation.h"
#include "arcium/ui/playground/fake_sidebar_model.h"
#include "arcium/ui/sidebar/row_context_menu.h"
#include "arcium/ui/sidebar/sidebar_colors.h"
#include "arcium/ui/sidebar/sidebar_model.h"
#include "arcium/ui/sidebar/sidebar_view.h"
#include "arcium/ui/sidebar/space_bar_view.h"
#include "arcium/ui/sidebar/tint_background.h"
#include "base/functional/callback_helpers.h"
#include "base/strings/string_number_conversions.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animator.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/events/test/test_event.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/gfx/scoped_animation_duration_scale_mode.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/widget/widget.h"

namespace arcium {
namespace {

class CountingObserver : public SidebarModel::Observer {
 public:
  void OnSidebarModelChanged() override { ++count; }
  int count = 0;
};

class SpaceBarTest : public views::ViewsTestBase {
 public:
  void SetUp() override {
    // Before ViewsTestBase::SetUp(), which would otherwise promote this binary
    // to a foreground application and pull the desktop onto the suite's Space.
    arcium::test::SuppressTestAppActivation();
    views::ViewsTestBase::SetUp();
  }

 protected:
  // A widget for the tests that need colours: a view only has a colour
  // provider once it is in one.
  std::unique_ptr<views::Widget> MakeWidget() {
    auto widget =
        CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
    widget->SetBounds(gfx::Rect(0, 0, 250, 600));
    return widget;
  }
};

// The pair TintBackground paints for `preset`, read against `view`'s colour
// provider and colour mode.
std::pair<SkColor, SkColor> TintColorsForTesting(const views::View& view,
                                                 int preset) {
  TintBackground tint;
  tint.SetPreset(preset);
  const TintBackground::Stops stops = tint.StopsFor(view);
  return {stops.top, stops.bottom};
}

// The pair the colour mixer gives, which the sidebar drew before spaces had
// colours of their own.
std::pair<SkColor, SkColor> MixerColorsForTesting(const views::View& view) {
  const ui::ColorProvider* cp = view.GetColorProvider();
  return {cp->GetColor(kColorArciumSidebarBackgroundTop),
          cp->GetColor(kColorArciumSidebarBackgroundBottom)};
}

// Whether a built menu, or any submenu in it, has an item by this label.
bool HasItem(ui::MenuModel* menu, const std::u16string& label) {
  for (size_t i = 0; i < menu->GetItemCount(); ++i) {
    if (menu->GetLabelAt(i) == label) {
      return true;
    }
    ui::MenuModel* submenu = menu->GetSubmenuModelAt(i);
    if (submenu && HasItem(submenu, label)) {
      return true;
    }
  }
  return false;
}

ui::MenuModel* Submenu(ui::MenuModel* menu, const std::u16string& label) {
  for (size_t i = 0; i < menu->GetItemCount(); ++i) {
    if (menu->GetLabelAt(i) == label) {
      return menu->GetSubmenuModelAt(i);
    }
  }
  return nullptr;
}

TEST_F(SpaceBarTest, TheFakeReportsSpacesWithTheActiveOneMarked) {
  FakeSidebarModel model;
  model.AddSpaceForTesting(u"Work", u"💼", 2);
  ASSERT_EQ(2u, model.spaces().size());
  EXPECT_TRUE(model.spaces()[0].is_active);
  EXPECT_EQ(u"Work", model.spaces()[1].name);
  EXPECT_EQ(u"💼", model.spaces()[1].icon);
  EXPECT_EQ(2, model.spaces()[1].gradient);
}

TEST_F(SpaceBarTest, SwitchingTheFakeMovesTheActiveMarkAndNotifies) {
  FakeSidebarModel model;
  CountingObserver observer;
  model.AddObserver(&observer);
  model.AddSpaceForTesting(u"Work", u"", 0);
  model.SwitchToSpace(model.spaces()[1].id);
  EXPECT_TRUE(model.spaces()[1].is_active);
  EXPECT_FALSE(model.spaces()[0].is_active);
  EXPECT_GE(observer.count, 1);
  model.RemoveObserver(&observer);
}

TEST_F(SpaceBarTest, TheFakeRefusesToDeleteItsLastSpace) {
  FakeSidebarModel model;
  model.DeleteSpace(model.spaces().front().id);
  EXPECT_EQ(1u, model.spaces().size());
}

// The list is drawn from rows(), so a fake that handed back every space's
// rows would let a view test pass against a sidebar the browser never shows.
TEST_F(SpaceBarTest, TheFakeDrawsOnlyTheActiveSpacesRows) {
  FakeSidebarModel model;
  model.AddTab(u"A1", "https://a1.example/", SidebarSection::kToday,
               /*active=*/true);
  const SpaceId work = model.AddSpaceForTesting(u"Work", u"", 0);
  model.AddTabInSpaceForTesting(u"W1", "https://w1.example/", work);

  ASSERT_EQ(1u, model.rows().size());
  EXPECT_EQ(u"A1", model.rows()[0].title);
  EXPECT_EQ(1, model.spaces()[1].open_tab_count);

  model.SwitchToSpace(work);
  ASSERT_EQ(1u, model.rows().size());
  EXPECT_EQ(u"W1", model.rows()[0].title);
}

// A moved tab changes space, it is not closed; and only moving the one on
// screen takes the window with it, as the real switcher adopts.
TEST_F(SpaceBarTest, MovingTheFakesActiveTabTakesTheMarkWithIt) {
  FakeSidebarModel model;
  model.AddTab(u"A1", "https://a1.example/", SidebarSection::kToday,
               /*active=*/true);
  model.AddTab(u"A2", "https://a2.example/", SidebarSection::kToday,
               /*active=*/false);
  const SpaceId work = model.AddSpaceForTesting(u"Work", u"", 0);

  model.MoveTabToSpace(1, work);
  EXPECT_TRUE(model.spaces()[0].is_active);
  EXPECT_EQ(1, model.spaces()[1].open_tab_count);
  ASSERT_EQ(1u, model.rows().size());
  EXPECT_EQ(u"A1", model.rows()[0].title);

  model.MoveTabToSpace(0, work);
  EXPECT_TRUE(model.spaces()[1].is_active);
  EXPECT_EQ(2u, model.rows().size());
}

// What a delete confirmation promises: a cold entry is an entry but not an
// open tab, and a moved entry takes its counts with it.
TEST_F(SpaceBarTest, MovingTheFakesEntryMovesItsCounts) {
  FakeSidebarModel model;
  model.AddTab(u"P1", "https://p1.example/", SidebarSection::kPinned,
               /*active=*/false);
  model.AddColdEntry(u"F1", "https://f1.example/", SidebarSection::kFavorites);
  const SpaceId work = model.AddSpaceForTesting(u"Work", u"", 0);
  EntryId pin;
  for (const SidebarRow& row : model.rows()) {
    if (row.title == u"P1") {
      pin = row.entry_id;
    }
  }
  ASSERT_TRUE(pin.is_valid());

  model.MoveEntryToSpace(pin, work);

  EXPECT_EQ(1, model.spaces()[0].entry_count);
  EXPECT_EQ(0, model.spaces()[0].open_tab_count);
  EXPECT_EQ(1, model.spaces()[1].entry_count);
  EXPECT_EQ(1, model.spaces()[1].open_tab_count);
  ASSERT_EQ(1u, model.rows().size());
  EXPECT_EQ(u"F1", model.rows()[0].title);
}

// A new tab opens in the space on screen. Tagged with any other space it
// would be the active row of a list that does not draw it.
TEST_F(SpaceBarTest, TheFakesNewTabOpensInTheActiveSpace) {
  FakeSidebarModel model;
  model.AddTab(u"A1", "https://a1.example/", SidebarSection::kToday,
               /*active=*/true);
  const SpaceId work = model.AddSpaceForTesting(u"Work", u"", 0);
  model.SwitchToSpace(work);

  model.NewTab();

  ASSERT_EQ(1u, model.rows().size());
  EXPECT_EQ(work, model.rows()[0].space);
  EXPECT_TRUE(model.rows()[0].is_active);
  EXPECT_EQ(1, model.spaces()[1].open_tab_count);
}

TEST_F(SpaceBarTest, OneChipPerSpaceWithTheActiveOneMarked) {
  FakeSidebarModel model;
  model.AddSpaceForTesting(u"Work", u"💼", 0);
  SpaceBarView bar(&model);
  EXPECT_EQ(2u, bar.chips_for_testing().size());
  EXPECT_TRUE(bar.chips_for_testing()[0]->is_active());
  EXPECT_FALSE(bar.chips_for_testing()[1]->is_active());
  EXPECT_EQ(u"💼", bar.chips_for_testing()[1]->GetText());
}

TEST_F(SpaceBarTest, ASpaceWithNoIconDrawsItsFirstLetter) {
  FakeSidebarModel model;
  model.AddSpaceForTesting(u"Work", u"", 0);
  SpaceBarView bar(&model);
  EXPECT_EQ(u"W", bar.chips_for_testing()[1]->GetText());
}

TEST_F(SpaceBarTest, PressingAChipSwitchesToThatSpace) {
  FakeSidebarModel model;
  model.AddSpaceForTesting(u"Work", u"", 0);
  SpaceBarView bar(&model);
  views::test::ButtonTestApi(bar.chips_for_testing()[1])
      .NotifyClick(ui::test::TestEvent());
  EXPECT_TRUE(model.spaces()[1].is_active);
}

TEST_F(SpaceBarTest, TheAddButtonMakesASpace) {
  FakeSidebarModel model;
  SpaceBarView bar(&model);
  views::test::ButtonTestApi(bar.add_button_for_testing())
      .NotifyClick(ui::test::TestEvent());
  EXPECT_EQ(2u, model.spaces().size());
}

TEST_F(SpaceBarTest, RenameIconGradientAndReorderAreLiveNow) {
  FakeSidebarModel model;
  model.AddSpaceForTesting(u"Work", u"", 0);
  SpaceBarView bar(&model);
  bar.BuildMenuForTesting(model.spaces()[1].id);
  EXPECT_TRUE(bar.IsCommandIdEnabled(SpaceBarView::kRename));
  EXPECT_TRUE(bar.IsCommandIdEnabled(SpaceBarView::kChangeIcon));
  EXPECT_TRUE(bar.IsCommandIdEnabled(SpaceBarView::kDelete));
  EXPECT_TRUE(bar.IsCommandIdEnabled(SpaceBarView::kMoveLeft));
  // Last space in the row: there is nothing to its right.
  EXPECT_FALSE(bar.IsCommandIdEnabled(SpaceBarView::kMoveRight));

  bar.ExecuteCommand(SpaceBarView::kMoveLeft, 0);
  EXPECT_EQ(u"Work", model.spaces()[0].name);
  bar.ExecuteCommand(SpaceBarView::kGradientFirst + 2, 0);
  EXPECT_EQ(2, model.spaces()[0].gradient);
}

TEST_F(SpaceBarTest, DeleteAsksFirstAndSaysWhatGoes) {
  FakeSidebarModel model;
  model.AddSpaceForTesting(u"Work", u"", 0);
  model.AddTabInSpaceForTesting(u"One", "https://w1.example/",
                                model.spaces()[1].id);
  SpaceBarView bar(&model);
  bar.BuildMenuForTesting(model.spaces()[1].id);
  bar.ExecuteCommand(SpaceBarView::kDelete, 0);
  EXPECT_EQ(2u, model.spaces().size());  // nothing yet
  EXPECT_NE(std::u16string::npos, bar.confirm_text_for_testing().find(u"Work"));
  EXPECT_NE(std::u16string::npos,
            bar.confirm_text_for_testing().find(u"1 tab"));
  bar.ConfirmDeleteForTesting(/*accept=*/true);
  EXPECT_EQ(1u, model.spaces().size());
}

// The one delete that does not come back has to count what goes in a way
// that cannot be misread: one entry is one thing, whichever kind it is, and
// "1 pin and favourite" reads as two.
TEST_F(SpaceBarTest, TheDeleteConfirmationCountsEntriesUnambiguously) {
  FakeSidebarModel model;
  model.AddSpaceForTesting(u"Work", u"", 0);
  model.AddColdEntry(u"Pin", "https://p.example/", SidebarSection::kPinned);
  SpaceBarView bar(&model);
  bar.BuildMenuForTesting(model.spaces()[0].id);

  bar.ExecuteCommand(SpaceBarView::kDelete, 0);
  EXPECT_NE(std::u16string::npos,
            bar.confirm_text_for_testing().find(u"1 pin or favourite"));
  bar.ConfirmDeleteForTesting(/*accept=*/false);

  model.AddColdEntry(u"Fav", "https://f.example/", SidebarSection::kFavorites);
  bar.ExecuteCommand(SpaceBarView::kDelete, 0);
  EXPECT_NE(std::u16string::npos,
            bar.confirm_text_for_testing().find(u"2 pins and favourites"));
  bar.ConfirmDeleteForTesting(/*accept=*/false);
}

// The tabs come back through Cmd+Shift+T because closing them is an
// ordinary tab close; the pins and favourites do not, because they are
// removed from the model itself. The confirmation has to promise only
// that, not that the whole delete is beyond undoing.
TEST_F(SpaceBarTest, TheDeleteConfirmationOnlyPromisesWhatIsTrue) {
  FakeSidebarModel model;
  // AddColdEntry always lands on the front space, so the space under test is
  // the default one, renamed rather than added, and given a live tab the
  // same way.
  const SpaceId work = model.spaces()[0].id;
  model.RenameSpace(work, u"Work");
  model.AddTabInSpaceForTesting(u"One", "https://w1.example/", work);
  model.AddColdEntry(u"Pin", "https://p.example/", SidebarSection::kPinned);
  SpaceBarView bar(&model);
  bar.BuildMenuForTesting(work);

  bar.ExecuteCommand(SpaceBarView::kDelete, 0);
  const std::u16string& text = bar.confirm_text_for_testing();
  EXPECT_NE(std::u16string::npos, text.find(u"Work"));
  EXPECT_NE(std::u16string::npos, text.find(u"1 tab"));
  EXPECT_NE(std::u16string::npos, text.find(u"will close"));
  EXPECT_NE(std::u16string::npos,
            text.find(u"1 pin or favourite will be deleted for good"));
  // The old blanket claim does not survive: the tabs it counted are not
  // gone for good, so the sentence must not say the whole action is.
  EXPECT_EQ(std::u16string::npos, text.find(u"cannot be undone"));
  bar.ConfirmDeleteForTesting(/*accept=*/false);

  model.AddColdEntry(u"Fav", "https://f.example/", SidebarSection::kFavorites);
  bar.ExecuteCommand(SpaceBarView::kDelete, 0);
  EXPECT_NE(std::u16string::npos,
            bar.confirm_text_for_testing().find(
                u"2 pins and favourites will be deleted for good"));
  bar.ConfirmDeleteForTesting(/*accept=*/false);
}

TEST_F(SpaceBarTest, DecliningTheConfirmationKeepsTheSpace) {
  FakeSidebarModel model;
  model.AddSpaceForTesting(u"Work", u"", 0);
  SpaceBarView bar(&model);
  bar.BuildMenuForTesting(model.spaces()[1].id);
  bar.ExecuteCommand(SpaceBarView::kDelete, 0);
  bar.ConfirmDeleteForTesting(/*accept=*/false);
  EXPECT_EQ(2u, model.spaces().size());
}

TEST_F(SpaceBarTest, TheArchiveTimeoutStillReadsTheActiveSpace) {
  FakeSidebarModel model;
  SpaceBarView bar(&model);
  bar.BuildMenuForTesting(model.spaces()[0].id);
  EXPECT_TRUE(bar.IsCommandIdChecked(SpaceBarView::kTimeoutTwelveHours));
  bar.ExecuteCommand(SpaceBarView::kTimeoutSevenDays, 0);
  EXPECT_EQ(ArchiveTimeout::kSevenDays, model.archive_timeout());
}

// The four archive timeouts are chosen from the space bar's menu.
TEST_F(SpaceBarTest, TheSpaceMenuChoosesTheArchiveTimeout) {
  FakeSidebarModel model;
  SpaceBarView bar(&model);
  ASSERT_EQ(ArchiveTimeout::kTwelveHours, model.archive_timeout());
  EXPECT_TRUE(bar.IsCommandIdChecked(SpaceBarView::kTimeoutTwelveHours));

  bar.ExecuteCommand(SpaceBarView::kTimeoutSevenDays, 0);
  EXPECT_EQ(ArchiveTimeout::kSevenDays, model.archive_timeout());
  EXPECT_TRUE(bar.IsCommandIdChecked(SpaceBarView::kTimeoutSevenDays));
  EXPECT_FALSE(bar.IsCommandIdChecked(SpaceBarView::kTimeoutTwelveHours));

  bar.ExecuteCommand(SpaceBarView::kTimeoutNever, 0);
  EXPECT_EQ(ArchiveTimeout::kNever, model.archive_timeout());

  EXPECT_TRUE(bar.IsCommandIdEnabled(SpaceBarView::kTimeoutOneDay));
}

TEST_F(SpaceBarTest, PresetZeroIsTheSidebarsOriginalPair) {
  FakeSidebarModel model;
  std::unique_ptr<views::Widget> widget = MakeWidget();
  SidebarView* view = widget->SetContentsView(
      std::make_unique<SidebarView>(&model, SidebarView::Delegate()));
  // The colours the mixer gives, which is what every existing snapshot and
  // every space that never chose a gradient draws.
  EXPECT_EQ(TintColorsForTesting(*view, /*preset=*/0),
            MixerColorsForTesting(*view));
  EXPECT_NE(TintColorsForTesting(*view, /*preset=*/1),
            MixerColorsForTesting(*view));
  // A preset this build does not have, say from a newer profile, draws the
  // original pair rather than reading past the table.
  EXPECT_EQ(TintColorsForTesting(*view, /*preset=*/99),
            MixerColorsForTesting(*view));
}

TEST_F(SpaceBarTest, TheSidebarPaintsTheActiveSpacesGradient) {
  FakeSidebarModel model;
  model.AddSpaceForTesting(u"Work", u"", 3);
  SidebarView view(&model, SidebarView::Delegate());
  EXPECT_EQ(0, view.tint_preset_for_testing());
  model.SwitchToSpace(model.spaces()[1].id);
  EXPECT_EQ(3, view.tint_preset_for_testing());
}

TEST_F(SpaceBarTest, CtrlDigitSwitchesToTheNthSpace) {
  FakeSidebarModel model;
  model.AddSpaceForTesting(u"Work", u"", 0);
  model.AddSpaceForTesting(u"Play", u"", 0);
  SidebarView view(&model, SidebarView::Delegate());
  EXPECT_TRUE(view.AcceleratorPressed(
      ui::Accelerator(ui::VKEY_3, ui::EF_CONTROL_DOWN)));
  EXPECT_TRUE(model.spaces()[2].is_active);
  // A digit past the last space is not this view's to swallow.
  EXPECT_FALSE(view.AcceleratorPressed(
      ui::Accelerator(ui::VKEY_9, ui::EF_CONTROL_DOWN)));
  EXPECT_TRUE(model.spaces()[2].is_active);
}

// The slide is the sidebar's one animation, and it must not run on the
// ordinary changes a sidebar sees all the time -- a title, a favicon, a load:
// each would move the rows under the pointer for 200 ms.
TEST_F(SpaceBarTest, TheColumnSlidesOnlyWhenTheSpaceChanges) {
  gfx::ScopedAnimationDurationScaleMode normal(
      gfx::ScopedAnimationDurationScaleMode::NORMAL_DURATION);
  FakeSidebarModel model;
  model.AddSpaceForTesting(u"Work", u"", 0);
  std::unique_ptr<views::Widget> widget = MakeWidget();
  SidebarView* view = widget->SetContentsView(
      std::make_unique<SidebarView>(&model, SidebarView::Delegate()));
  ui::Layer* layer = view->column_for_testing()->layer();
  ASSERT_TRUE(layer);

  model.RenameSpace(model.spaces()[0].id, u"Home");
  EXPECT_FALSE(layer->GetAnimator()->is_animating());

  model.SwitchToSpace(model.spaces()[1].id);
  EXPECT_TRUE(layer->GetAnimator()->is_animating());
  // Wherever it starts, it comes to rest where it always sits.
  EXPECT_EQ(gfx::Transform(), layer->GetTargetTransform());
}

// A space that is gone has no place in the bar to slide from. At launch the
// window starts on the placeholder model's space, which the loaded file
// replaces, and deleting the space on screen takes it away in the same
// change that moves the window off it.
TEST_F(SpaceBarTest, NothingSlidesWhenTheSpaceOnScreenIsGone) {
  gfx::ScopedAnimationDurationScaleMode normal(
      gfx::ScopedAnimationDurationScaleMode::NORMAL_DURATION);
  FakeSidebarModel model;
  const SpaceId work = model.AddSpaceForTesting(u"Work", u"", 0);
  model.SwitchToSpace(work);
  std::unique_ptr<views::Widget> widget = MakeWidget();
  SidebarView* view = widget->SetContentsView(
      std::make_unique<SidebarView>(&model, SidebarView::Delegate()));
  ui::Layer* layer = view->column_for_testing()->layer();
  ASSERT_TRUE(layer);
  ASSERT_FALSE(layer->GetAnimator()->is_animating());

  model.DeleteSpace(work);
  ASSERT_TRUE(model.spaces()[0].is_active);
  EXPECT_FALSE(layer->GetAnimator()->is_animating());
}

TEST_F(SpaceBarTest, MoveToSpaceIsOfferedOnEveryRowSection) {
  FakeSidebarModel model;
  model.AddSpaceForTesting(u"Work", u"", 0);
  model.AddTab(u"Fav", "https://f.example/", SidebarSection::kFavorites, false);
  model.AddTab(u"Pin", "https://p.example/", SidebarSection::kPinned, false);
  model.AddTab(u"Today", "https://t.example/", SidebarSection::kToday, true);
  RowContextMenu menu(&model);
  ASSERT_EQ(3u, model.rows().size());
  for (const SidebarRow& row : model.rows()) {
    menu.BuildForRow(row, base::DoNothing());
    EXPECT_TRUE(HasItem(menu.menu(), u"Move to space")) << row.title;
  }
}

// Moving the tab on screen takes the window with it, so the moved row is the
// one the sidebar then draws.
TEST_F(SpaceBarTest, MoveToSpaceOffersEverySpaceButThisOne) {
  FakeSidebarModel model;
  model.AddSpaceForTesting(u"Work", u"", 0);
  model.AddTab(u"Today", "https://t.example/", SidebarSection::kToday, true);
  RowContextMenu menu(&model);
  menu.BuildForRow(model.rows().front(), base::DoNothing());

  ui::MenuModel* targets = Submenu(menu.menu(), u"Move to space");
  ASSERT_TRUE(targets);
  ASSERT_EQ(1u, targets->GetItemCount());
  EXPECT_EQ(u"Work", targets->GetLabelAt(0));
  EXPECT_TRUE(menu.IsCommandIdEnabled(RowContextMenu::kMoveToSpaceFirst));

  menu.ExecuteCommand(RowContextMenu::kMoveToSpaceFirst, 0);
  EXPECT_TRUE(model.spaces()[1].is_active);
  ASSERT_EQ(1u, model.rows().size());
  EXPECT_EQ(u"Today", model.rows()[0].title);
}

// A row that is not on screen leaves, and the window stays where it is.
TEST_F(SpaceBarTest, MovingARowNotOnScreenLeavesTheWindowWhereItIs) {
  FakeSidebarModel model;
  model.AddSpaceForTesting(u"Work", u"", 0);
  model.AddTab(u"A", "https://a.example/", SidebarSection::kToday, true);
  model.AddTab(u"B", "https://b.example/", SidebarSection::kToday, false);
  RowContextMenu menu(&model);
  menu.BuildForRow(model.rows()[1], base::DoNothing());

  menu.ExecuteCommand(RowContextMenu::kMoveToSpaceFirst, 0);
  EXPECT_TRUE(model.spaces()[0].is_active);
  ASSERT_EQ(1u, model.rows().size());
  EXPECT_EQ(u"A", model.rows()[0].title);
  EXPECT_EQ(1, model.spaces()[1].open_tab_count);
}

// A cold entry has no tab, so it can only move by its entry.
TEST_F(SpaceBarTest, AColdEntryMovesToASpaceByItsEntry) {
  FakeSidebarModel model;
  model.AddSpaceForTesting(u"Work", u"", 0);
  model.AddColdEntry(u"F1", "https://f1.example/", SidebarSection::kFavorites);
  RowContextMenu menu(&model);
  menu.BuildForRow(model.rows().front(), base::DoNothing());

  menu.ExecuteCommand(RowContextMenu::kMoveToSpaceFirst, 0);
  EXPECT_TRUE(model.rows().empty());
  EXPECT_EQ(1, model.spaces()[1].entry_count);
}

// With one space there is nowhere to move a row to, so the item is not
// offered at all rather than greyed out on every row's menu.
TEST_F(SpaceBarTest, MoveToSpaceIsAbsentWithOneSpace) {
  FakeSidebarModel model;
  model.AddTab(u"Today", "https://t.example/", SidebarSection::kToday, true);
  RowContextMenu menu(&model);
  menu.BuildForRow(model.rows().front(), base::DoNothing());
  EXPECT_FALSE(HasItem(menu.menu(), u"Move to space"));
}

// Folder items and space items share one command-id space, the folders
// below the spaces. A folder past the end of its range would be read as a
// space and move the row to the wrong place, so the list stops there.
TEST_F(SpaceBarTest, FolderTargetsStopWhereTheSpaceRangeBegins) {
  FakeSidebarModel model;
  model.AddSpaceForTesting(u"Work", u"", 0);
  model.AddTab(u"Pin", "https://p.example/", SidebarSection::kPinned, true);
  // A folder is made around an entry, so each gets a pinned tab of its own.
  for (int i = 0; i < 250; ++i) {
    const std::u16string n = base::NumberToString16(i);
    model.AddTab(u"P" + n, "https://p.example/", SidebarSection::kPinned,
                 false);
    model.AddFolderWith(u"F" + n, {u"P" + n});
  }
  ASSERT_EQ(250u, model.folders().size());
  RowContextMenu menu(&model);
  menu.BuildForRow(model.rows().front(), base::DoNothing());
  ASSERT_EQ(u"Pin", model.rows().front().title);

  ui::MenuModel* folders = Submenu(menu.menu(), u"Move to folder");
  ASSERT_TRUE(folders);
  EXPECT_LT(folders->GetItemCount(), 250u);
  int highest = 0;
  for (size_t i = 0; i < folders->GetItemCount(); ++i) {
    highest = std::max(highest, folders->GetCommandIdAt(i));
  }
  EXPECT_LT(highest, RowContextMenu::kMoveToSpaceFirst);
}

}  // namespace
}  // namespace arcium
