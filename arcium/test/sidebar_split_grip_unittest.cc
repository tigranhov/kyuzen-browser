// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The grip between a split row's halves: dragging it moves the split whole,
// and dragging either half pulls that half out and ends the split wherever it
// lands, in Today and Pinned alike.

#include <memory>
#include <string>
#include <vector>

#include "arcium/test/sidebar_split_drop_fixture.h"
#include "arcium/ui/playground/fake_sidebar_model.h"
#include "arcium/ui/sidebar/favorites_grid_view.h"
#include "arcium/ui/sidebar/folder_header_view.h"
#include "arcium/ui/sidebar/sidebar_metrics.h"
#include "arcium/ui/sidebar/sidebar_model.h"
#include "arcium/ui/sidebar/split_grip_view.h"
#include "arcium/ui/sidebar/tab_list_view.h"
#include "arcium/ui/sidebar/tab_row_view.h"
#include "base/time/time.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/events/event.h"
#include "ui/events/event_constants.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/scoped_animation_duration_scale_mode.h"
#include "ui/views/test/views_test_utils.h"
#include "ui/views/view.h"

namespace arcium {
namespace {

class SidebarSplitGripTest : public test::SplitDropFixture {
 protected:
  // Splits the rows titled `a` and `b`, `a` being the one on screen.
  void Split(const std::u16string& a, const std::u16string& b) {
    ASSERT_TRUE(RowNamed(a).has_value());
    ASSERT_TRUE(RowNamed(a)->is_active);
    model_.SplitRowWithCurrentPage(*RowNamed(b));
    ASSERT_TRUE(Share(a, b));
    views::test::RunScheduledLayout(widget_.get());
  }

  TabRowView* RowTitled(TabListView* list, const std::u16string& title) {
    views::test::RunScheduledLayout(widget_.get());
    for (views::View* child : list->children()) {
      if (auto* row = views::AsViewClass<TabRowView>(child)) {
        if (row->row().title == title) {
          return row;
        }
      }
    }
    return nullptr;
  }

  std::unique_ptr<ui::OSExchangeData> DragDataFromGrip(SplitGripView* grip) {
    auto data = std::make_unique<ui::OSExchangeData>();
    grip->WriteDragDataForView(grip, gfx::Point(), data.get());
    return data;
  }

  void Hover(views::View* view, bool on) {
    ui::MouseEvent event(
        on ? ui::EventType::kMouseEntered : ui::EventType::kMouseExited,
        gfx::Point(), gfx::Point(), base::TimeTicks(), 0, 0);
    on ? view->OnMouseEntered(event) : view->OnMouseExited(event);
  }

  FolderHeaderView* HeaderIn(TabListView* list) {
    views::test::RunScheduledLayout(widget_.get());
    for (views::View* child : list->children()) {
      if (auto* header = views::AsViewClass<FolderHeaderView>(child)) {
        return header;
      }
    }
    return nullptr;
  }

  // The room between a split row's halves, as laid out.
  int Gap(TabRowView* left, TabRowView* right) {
    views::test::RunScheduledLayout(widget_.get());
    return right->x() - left->bounds().right();
  }

  std::vector<std::u16string> Titles(SidebarSection section) {
    std::vector<std::u16string> titles;
    for (const SidebarRow& row : model_.rows()) {
      if (row.section == section) {
        titles.push_back(row.title);
      }
    }
    return titles;
  }
};

TEST_F(SidebarSplitGripTest, ASplitRowShowsItsGripBetweenItsHalvesOnHover) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kToday, true);
  model_.AddTab(u"Two", "https://two.example/", SidebarSection::kToday, false);
  MakeLists();
  Split(u"One", u"Two");

  ASSERT_EQ(1u, today_->grips_for_testing().size());
  SplitGripView* grip = today_->grips_for_testing()[0];
  TabRowView* one = RowTitled(today_, u"One");
  TabRowView* two = RowTitled(today_, u"Two");
  ASSERT_TRUE(one && two);
  // In the gap between the halves, taking nothing from either.
  EXPECT_EQ(one->bounds().right(), grip->x());
  EXPECT_EQ(two->x(), grip->bounds().right());
  EXPECT_EQ(one->y(), grip->y());
  EXPECT_FALSE(grip->shown());

  Hover(two, true);
  EXPECT_TRUE(grip->shown());
  Hover(two, false);
  EXPECT_FALSE(grip->shown());
}

// At rest the halves sit together. Pointing at the row pushes them apart to
// make room for the grip, a step at a time, and leaving closes them again.
TEST_F(SidebarSplitGripTest, ASplitRowOpensRoomForItsGripOnlyUnderThePointer) {
  const auto rich = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_ENABLED);
  // Views tests on Mac run every animation at zero length unless told not to.
  gfx::ScopedAnimationDurationScaleMode normal_speed(
      gfx::ScopedAnimationDurationScaleMode::NORMAL_DURATION);
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kToday, true);
  model_.AddTab(u"Two", "https://two.example/", SidebarSection::kToday, false);
  MakeLists();
  Split(u"One", u"Two");
  SplitGripView* grip = today_->grips_for_testing()[0];
  TabRowView* one = RowTitled(today_, u"One");
  TabRowView* two = RowTitled(today_, u"Two");
  ASSERT_TRUE(one && two);
  const int left = one->x();
  const int right = two->bounds().right();
  EXPECT_EQ(metrics::kSplitHalfGap, Gap(one, two));

  Hover(two, true);
  const base::TimeTicks start = base::TimeTicks::Now();
  gfx::AnimationTestApi opening(grip->animation_for_testing());
  opening.SetStartTime(start);
  opening.Step(start + base::Milliseconds(40));
  EXPECT_GT(Gap(one, two), metrics::kSplitHalfGap);
  EXPECT_LT(Gap(one, two), metrics::kSplitHalfGapOpen);
  opening.Step(start + base::Seconds(1));
  EXPECT_EQ(metrics::kSplitHalfGapOpen, Gap(one, two));
  // The grip fills the room, and the row keeps its outer edges.
  EXPECT_EQ(one->bounds().right(), grip->x());
  EXPECT_EQ(two->x(), grip->bounds().right());
  EXPECT_EQ(left, one->x());
  EXPECT_EQ(right, two->bounds().right());

  Hover(two, false);
  gfx::AnimationTestApi closing(grip->animation_for_testing());
  closing.SetStartTime(start + base::Seconds(2));
  closing.Step(start + base::Seconds(3));
  EXPECT_EQ(metrics::kSplitHalfGap, Gap(one, two));
}

TEST_F(SidebarSplitGripTest, WithReducedMotionTheRoomOpensAtOnce) {
  const auto still = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_DISABLED);
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kToday, true);
  model_.AddTab(u"Two", "https://two.example/", SidebarSection::kToday, false);
  MakeLists();
  Split(u"One", u"Two");
  TabRowView* one = RowTitled(today_, u"One");
  TabRowView* two = RowTitled(today_, u"Two");
  ASSERT_TRUE(one && two);

  Hover(one, true);
  EXPECT_EQ(metrics::kSplitHalfGapOpen, Gap(one, two));
  Hover(one, false);
  EXPECT_EQ(metrics::kSplitHalfGap, Gap(one, two));
}

TEST_F(SidebarSplitGripTest, ARowSharingNothingHasNoGrip) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kToday, true);
  model_.AddTab(u"Two", "https://two.example/", SidebarSection::kToday, false);
  MakeLists();

  EXPECT_TRUE(today_->grips_for_testing().empty());
}

TEST_F(SidebarSplitGripTest, AHalfDroppedRightBelowItsRowLeavesTheSplit) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kToday, true);
  model_.AddTab(u"Two", "https://two.example/", SidebarSection::kToday, false);
  model_.AddTab(u"Three", "https://three.example/", SidebarSection::kToday,
                false);
  MakeLists();
  Split(u"One", u"Two");
  TabRowView* two = RowTitled(today_, u"Two");
  TabRowView* three = RowTitled(today_, u"Three");
  ASSERT_TRUE(two && three);
  std::unique_ptr<ui::OSExchangeData> data = DragDataFrom(two);

  // The line just below the split row, which puts Two where it already is.
  DropOn(today_, *data, NearTopOf(three));

  EXPECT_FALSE(RowNamed(u"One")->split.has_value());
  EXPECT_FALSE(RowNamed(u"Two")->split.has_value());
}

TEST_F(SidebarSplitGripTest, APairDraggedByItsGripMovesWholeAndStaysSplit) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kToday, true);
  model_.AddTab(u"Two", "https://two.example/", SidebarSection::kToday, false);
  model_.AddTab(u"Three", "https://three.example/", SidebarSection::kToday,
                false);
  model_.AddTab(u"Four", "https://four.example/", SidebarSection::kToday,
                false);
  MakeLists();
  Split(u"One", u"Two");
  ASSERT_EQ(1u, today_->grips_for_testing().size());
  std::unique_ptr<ui::OSExchangeData> data =
      DragDataFromGrip(today_->grips_for_testing()[0]);
  TabRowView* four = RowTitled(today_, u"Four");
  ASSERT_TRUE(four);

  DropOn(today_, *data, NearTopOf(four));

  EXPECT_EQ((std::vector<std::u16string>{u"Three", u"One", u"Two", u"Four"}),
            Titles(SidebarSection::kToday));
  EXPECT_TRUE(Share(u"One", u"Two"));
}

TEST_F(SidebarSplitGripTest, APinnedPairDraggedDownLandsInTheGapItWasDropped) {
  model_.AddTab(u"A", "https://a.example/", SidebarSection::kPinned, true);
  model_.AddTab(u"B", "https://b.example/", SidebarSection::kPinned, false);
  model_.AddTab(u"C", "https://c.example/", SidebarSection::kPinned, false);
  model_.AddTab(u"D", "https://d.example/", SidebarSection::kPinned, false);
  MakeLists();
  Split(u"A", u"B");
  ASSERT_EQ(1u, pinned_->grips_for_testing().size());
  std::unique_ptr<ui::OSExchangeData> data =
      DragDataFromGrip(pinned_->grips_for_testing()[0]);
  TabRowView* d = RowTitled(pinned_, u"D");
  ASSERT_TRUE(d);

  // Between C and D. Both halves are lifted out before the pair goes back
  // in, so counting only one of them would land it after D.
  DropOn(pinned_, *data, NearTopOf(d));

  EXPECT_EQ((std::vector<std::u16string>{u"C", u"A", u"B", u"D"}),
            Titles(SidebarSection::kPinned));
  EXPECT_TRUE(Share(u"A", u"B"));
}

TEST_F(SidebarSplitGripTest, APinnedHalfDraggedWithinPinnedLeavesTheSplit) {
  model_.AddTab(u"A", "https://a.example/", SidebarSection::kPinned, true);
  model_.AddTab(u"B", "https://b.example/", SidebarSection::kPinned, false);
  model_.AddTab(u"C", "https://c.example/", SidebarSection::kPinned, false);
  MakeLists();
  Split(u"A", u"B");
  TabRowView* b = RowTitled(pinned_, u"B");
  TabRowView* c = RowTitled(pinned_, u"C");
  ASSERT_TRUE(b && c);
  std::unique_ptr<ui::OSExchangeData> data = DragDataFrom(b);

  DropOn(pinned_, *data, NearTopOf(c));

  EXPECT_FALSE(Share(u"A", u"B"));
  EXPECT_EQ((std::vector<std::u16string>{u"A", u"B", u"C"}),
            Titles(SidebarSection::kPinned));
}

TEST_F(SidebarSplitGripTest, APairIsNeverASplitTarget) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kToday, true);
  model_.AddTab(u"Two", "https://two.example/", SidebarSection::kToday, false);
  model_.AddTab(u"Three", "https://three.example/", SidebarSection::kToday,
                false);
  MakeLists();
  Split(u"One", u"Two");
  std::unique_ptr<ui::OSExchangeData> data =
      DragDataFromGrip(today_->grips_for_testing()[0]);
  TabRowView* three = RowTitled(today_, u"Three");
  ASSERT_TRUE(three);

  MoveOver(today_, *data, MiddleOf(three));

  EXPECT_FALSE(today_->split_target_for_testing().has_value());
}

TEST_F(SidebarSplitGripTest, AHalfDroppedOnFavouritesLeavesTheSplit) {
  model_.AddTab(u"Fav", "https://fav.example/", SidebarSection::kFavorites,
                false);
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kToday, true);
  model_.AddTab(u"Two", "https://two.example/", SidebarSection::kToday, false);
  MakeLists();
  MakeGrid();
  Split(u"One", u"Two");
  TabRowView* two = RowTitled(today_, u"Two");
  ASSERT_TRUE(two);
  std::unique_ptr<ui::OSExchangeData> data = DragDataFrom(two);

  DropOn(grid_, *data, gfx::Point(0, 0));

  EXPECT_EQ((std::vector<std::u16string>{u"Two", u"Fav"}),
            Titles(SidebarSection::kFavorites));
  EXPECT_FALSE(RowNamed(u"One")->split.has_value());
  EXPECT_FALSE(RowNamed(u"Two")->split.has_value());
}

// A favourite is one page, and a pair is two.
TEST_F(SidebarSplitGripTest, FavouritesRefuseAPair) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kToday, true);
  model_.AddTab(u"Two", "https://two.example/", SidebarSection::kToday, false);
  MakeLists();
  MakeGrid();
  Split(u"One", u"Two");
  std::unique_ptr<ui::OSExchangeData> data =
      DragDataFromGrip(today_->grips_for_testing()[0]);

  EXPECT_FALSE(grid_->CanDrop(*data));
}

TEST_F(SidebarSplitGripTest, AHalfDroppedOnAFolderLeavesTheSplit) {
  model_.AddTab(u"A", "https://a.example/", SidebarSection::kPinned, true);
  model_.AddTab(u"B", "https://b.example/", SidebarSection::kPinned, false);
  model_.AddTab(u"Inside", "https://inside.example/", SidebarSection::kPinned,
                false);
  MakeLists();
  const FolderId folder = model_.AddFolderWith(u"Work", {u"Inside"});
  Split(u"A", u"B");
  FolderHeaderView* header = HeaderIn(pinned_);
  TabRowView* b = RowTitled(pinned_, u"B");
  ASSERT_TRUE(header && b);
  std::unique_ptr<ui::OSExchangeData> data = DragDataFrom(b);

  DropOn(header, *data, gfx::Point(10, 10));

  EXPECT_EQ(folder, RowNamed(u"B")->folder_id);
  EXPECT_FALSE(Share(u"A", u"B"));
}

TEST_F(SidebarSplitGripTest, ATodayHalfDraggedIntoPinnedLeavesTheSplit) {
  model_.AddTab(u"P", "https://p.example/", SidebarSection::kPinned, false);
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kToday, true);
  model_.AddTab(u"Two", "https://two.example/", SidebarSection::kToday, false);
  MakeLists();
  Split(u"One", u"Two");
  TabRowView* two = RowTitled(today_, u"Two");
  TabRowView* p = RowTitled(pinned_, u"P");
  ASSERT_TRUE(two && p);
  std::unique_ptr<ui::OSExchangeData> data = DragDataFrom(two);

  DropOn(pinned_, *data, NearTopOf(p));

  EXPECT_EQ((std::vector<std::u16string>{u"Two", u"P"}),
            Titles(SidebarSection::kPinned));
  EXPECT_FALSE(RowNamed(u"One")->split.has_value());
  EXPECT_FALSE(RowNamed(u"Two")->split.has_value());
}

TEST_F(SidebarSplitGripTest, APinnedHalfDraggedIntoTodayLeavesTheSplit) {
  model_.AddTab(u"A", "https://a.example/", SidebarSection::kPinned, true);
  model_.AddTab(u"B", "https://b.example/", SidebarSection::kPinned, false);
  model_.AddTab(u"T", "https://t.example/", SidebarSection::kToday, false);
  MakeLists();
  Split(u"A", u"B");
  TabRowView* b = RowTitled(pinned_, u"B");
  TabRowView* t = RowTitled(today_, u"T");
  ASSERT_TRUE(b && t);
  std::unique_ptr<ui::OSExchangeData> data = DragDataFrom(b);

  DropOn(today_, *data, NearTopOf(t));

  // Only the half that was dragged comes down.
  EXPECT_EQ((std::vector<std::u16string>{u"A"}),
            Titles(SidebarSection::kPinned));
  EXPECT_FALSE(Share(u"A", u"B"));
}

}  // namespace
}  // namespace arcium
