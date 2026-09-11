// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The space bar and what it draws from. FakeSidebarModel has to report
// spaces() and switch between them the way the real model does, since every
// view test of the bar runs over it.

#include "arcium/ui/playground/fake_sidebar_model.h"
#include "arcium/ui/sidebar/sidebar_model.h"
#include "arcium/ui/sidebar/space_bar_view.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/events/test/test_event.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/test/views_test_base.h"

namespace arcium {
namespace {

class CountingObserver : public SidebarModel::Observer {
 public:
  void OnSidebarModelChanged() override { ++count; }
  int count = 0;
};

class SpaceBarTest : public views::ViewsTestBase {};

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

// R2.3 names four archive timeouts, and the space bar's menu is where they are
// chosen.
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

}  // namespace
}  // namespace arcium
