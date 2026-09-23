// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A sidebar row dragged onto the middle of another row splits the two: the
// row under the pointer tints its right half while the drop would split, and
// the top and bottom of a row stay the boundaries a reorder drops on.

#include <memory>
#include <vector>

#include "arcium/test/sidebar_split_drop_fixture.h"
#include "arcium/ui/playground/fake_sidebar_model.h"
#include "arcium/ui/sidebar/sidebar_model.h"
#include "arcium/ui/sidebar/tab_list_view.h"
#include "arcium/ui/sidebar/tab_row_view.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/views/view.h"

namespace arcium {
namespace {

using SidebarSplitDropTest = test::SplitDropFixture;

TEST_F(SidebarSplitDropTest, TheMiddleOfARowTintsItAndDrawsNoLine) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kToday, true);
  model_.AddTab(u"Two", "https://two.example/", SidebarSection::kToday, false);
  MakeLists();
  TabRowView* one = RowIn(today_, 0);
  TabRowView* two = RowIn(today_, 1);
  std::unique_ptr<ui::OSExchangeData> data = DragDataFrom(one);

  MoveOver(today_, *data, MiddleOf(two));

  EXPECT_EQ(1u, today_->split_target_for_testing());
  EXPECT_TRUE(two->is_split_target());
  EXPECT_FALSE(today_->drop_index_for_testing().has_value());
}

TEST_F(SidebarSplitDropTest, TheEdgeOfARowStillDrawsTheLine) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kToday, true);
  model_.AddTab(u"Two", "https://two.example/", SidebarSection::kToday, false);
  MakeLists();
  TabRowView* one = RowIn(today_, 0);
  TabRowView* two = RowIn(today_, 1);
  std::unique_ptr<ui::OSExchangeData> data = DragDataFrom(one);

  MoveOver(today_, *data, NearTopOf(two));

  EXPECT_FALSE(today_->split_target_for_testing().has_value());
  EXPECT_FALSE(two->is_split_target());
  EXPECT_EQ(1u, today_->drop_index_for_testing());
}

TEST_F(SidebarSplitDropTest, MovingOnClearsTheTint) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kToday, true);
  model_.AddTab(u"Two", "https://two.example/", SidebarSection::kToday, false);
  MakeLists();
  TabRowView* one = RowIn(today_, 0);
  TabRowView* two = RowIn(today_, 1);
  std::unique_ptr<ui::OSExchangeData> data = DragDataFrom(one);
  MoveOver(today_, *data, MiddleOf(two));
  ASSERT_TRUE(two->is_split_target());

  MoveOver(today_, *data, NearTopOf(two));
  EXPECT_FALSE(two->is_split_target());

  MoveOver(today_, *data, MiddleOf(two));
  today_->OnDragExited();
  EXPECT_FALSE(two->is_split_target());
  EXPECT_FALSE(today_->split_target_for_testing().has_value());
}

TEST_F(SidebarSplitDropTest, ARowIsNotItsOwnTarget) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kToday, true);
  model_.AddTab(u"Two", "https://two.example/", SidebarSection::kToday, false);
  MakeLists();
  TabRowView* one = RowIn(today_, 0);
  std::unique_ptr<ui::OSExchangeData> data = DragDataFrom(one);

  MoveOver(today_, *data, MiddleOf(one));

  EXPECT_FALSE(today_->split_target_for_testing().has_value());
  EXPECT_FALSE(one->is_split_target());
}

TEST_F(SidebarSplitDropTest, DroppingOnTheMiddleSplitsTheTwo) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kToday, true);
  model_.AddTab(u"Two", "https://two.example/", SidebarSection::kToday, false);
  model_.AddTab(u"Three", "https://three.example/", SidebarSection::kToday,
                false);
  MakeLists();
  TabRowView* one = RowIn(today_, 0);
  TabRowView* three = RowIn(today_, 2);
  std::unique_ptr<ui::OSExchangeData> data = DragDataFrom(three);

  DropOn(today_, *data, MiddleOf(one));

  EXPECT_TRUE(Share(u"One", u"Three"));
  EXPECT_FALSE(RowNamed(u"Two")->split.has_value());
}

TEST_F(SidebarSplitDropTest, APinnedRowCanBeDroppedOnATodayRow) {
  model_.AddTab(u"Pin", "https://pin.example/", SidebarSection::kPinned, false);
  model_.AddTab(u"Today", "https://today.example/", SidebarSection::kToday,
                true);
  MakeLists();
  TabRowView* pin = RowIn(pinned_, 0);
  TabRowView* today = RowIn(today_, 0);
  std::unique_ptr<ui::OSExchangeData> data = DragDataFrom(pin);

  DropOn(today_, *data, MiddleOf(today));

  EXPECT_TRUE(Share(u"Today", u"Pin"));
}

TEST_F(SidebarSplitDropTest, ARowAlreadySharingIsNotATarget) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kToday, true);
  model_.AddTab(u"Two", "https://two.example/", SidebarSection::kToday, false);
  model_.AddTab(u"Three", "https://three.example/", SidebarSection::kToday,
                false);
  MakeLists();
  model_.SplitRowWithCurrentPage(*RowNamed(u"Two"));
  ASSERT_TRUE(Share(u"One", u"Two"));
  // One and Two are now one row, so Three is the second child.
  TabRowView* three = nullptr;
  TabRowView* joined = nullptr;
  for (views::View* child : today_->children()) {
    if (auto* row = views::AsViewClass<TabRowView>(child)) {
      if (row->row().title == u"Three") {
        three = row;
      } else if (row->row().title == u"One") {
        joined = row;
      }
    }
  }
  ASSERT_TRUE(three);
  ASSERT_TRUE(joined);
  std::unique_ptr<ui::OSExchangeData> data = DragDataFrom(three);

  MoveOver(today_, *data, MiddleOf(joined));

  EXPECT_FALSE(today_->split_target_for_testing().has_value());
  EXPECT_FALSE(joined->is_split_target());
}

}  // namespace
}  // namespace arcium
