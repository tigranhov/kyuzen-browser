// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The space bar and what it draws from. FakeSidebarModel has to report
// spaces() and switch between them the way the real model does, since every
// view test of the bar runs over it.

#include "arcium/ui/playground/fake_sidebar_model.h"
#include "arcium/ui/sidebar/sidebar_model.h"
#include "testing/gtest/include/gtest/gtest.h"
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

}  // namespace
}  // namespace arcium
