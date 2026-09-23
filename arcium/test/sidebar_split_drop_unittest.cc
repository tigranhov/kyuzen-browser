// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A sidebar row dragged onto the middle of another row splits the two: the
// row under the pointer tints its right half while the drop would split, and
// the top and bottom of a row stay the boundaries a reorder drops on.

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "arcium/test/test_app_activation.h"
#include "arcium/ui/playground/fake_sidebar_model.h"
#include "arcium/ui/sidebar/row_drag_session.h"
#include "arcium/ui/sidebar/sidebar_metrics.h"
#include "arcium/ui/sidebar/sidebar_model.h"
#include "arcium/ui/sidebar/tab_list_view.h"
#include "arcium/ui/sidebar/tab_row_view.h"
#include "base/memory/raw_ptr.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/dragdrop/drag_drop_types.h"
#include "ui/base/dragdrop/drop_target_event.h"
#include "ui/base/dragdrop/mojom/drag_drop_types.mojom.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/compositor/layer_tree_owner.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/test/views_test_utils.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace arcium {
namespace {

class SidebarSplitDropTest : public views::ViewsTestBase,
                             public SidebarModel::Observer {
 public:
  void SetUp() override {
    arcium::test::SuppressTestAppActivation();
    views::ViewsTestBase::SetUp();
    widget_ = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
    contents_ = widget_->SetContentsView(std::make_unique<views::View>());
    contents_->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical));
    widget_->SetBounds(gfx::Rect(0, 0, metrics::kSidebarWidth, 600));
    widget_->Show();
    model_.AddObserver(this);
  }

  void TearDown() override {
    model_.RemoveObserver(this);
    pinned_ = nullptr;
    today_ = nullptr;
    contents_ = nullptr;
    widget_.reset();
    views::ViewsTestBase::TearDown();
  }

  // A drop rebuilds the lists underneath it, the way SidebarView does.
  void OnSidebarModelChanged() override { Rebuild(); }

 protected:
  void MakeLists() {
    pinned_ = contents_->AddChildView(
        std::make_unique<TabListView>(&model_, SidebarSection::kPinned));
    today_ = contents_->AddChildView(
        std::make_unique<TabListView>(&model_, SidebarSection::kToday));
    pinned_->SetDragSession(&session_);
    today_->SetDragSession(&session_);
    Rebuild();
  }

  void Rebuild() {
    const std::vector<SidebarRow> rows = model_.rows();
    if (pinned_) {
      pinned_->SetRows(rows);
    }
    if (today_) {
      today_->SetRows(rows);
    }
    views::test::RunScheduledLayout(widget_.get());
  }

  TabRowView* RowIn(TabListView* list, size_t child_index) {
    views::test::RunScheduledLayout(widget_.get());
    return views::AsViewClass<TabRowView>(list->children()[child_index]);
  }

  std::unique_ptr<ui::OSExchangeData> DragDataFrom(TabRowView* row) {
    auto data = std::make_unique<ui::OSExchangeData>();
    row->WriteDragDataForView(row, gfx::Point(), data.get());
    return data;
  }

  // Points in `row`'s list's coordinates: its middle, and just inside its
  // top edge.
  gfx::Point MiddleOf(views::View* row) {
    return gfx::Point(10, row->y() + row->height() / 2);
  }
  gfx::Point NearTopOf(views::View* row) {
    return gfx::Point(10, row->y() + 2);
  }

  void MoveOver(TabListView* list,
                const ui::OSExchangeData& data,
                const gfx::Point& at) {
    ui::DropTargetEvent event(data, gfx::PointF(at), gfx::PointF(at),
                              ui::DragDropTypes::DRAG_MOVE);
    list->OnDragEntered(event);
    EXPECT_EQ(ui::DragDropTypes::DRAG_MOVE, list->OnDragUpdated(event));
  }

  void DropOn(TabListView* list,
              const ui::OSExchangeData& data,
              const gfx::Point& at) {
    MoveOver(list, data, at);
    ui::DropTargetEvent event(data, gfx::PointF(at), gfx::PointF(at),
                              ui::DragDropTypes::DRAG_MOVE);
    views::View::DropCallback callback = list->GetDropCallback(event);
    ASSERT_TRUE(callback);
    auto operation = ui::mojom::DragOperation::kNone;
    std::move(callback).Run(event, operation, nullptr);
    EXPECT_EQ(ui::mojom::DragOperation::kMove, operation);
  }

  std::optional<SidebarRow> RowNamed(const std::u16string& title) {
    for (const SidebarRow& row : model_.rows()) {
      if (row.title == title) {
        return row;
      }
    }
    return std::nullopt;
  }

  bool Share(const std::u16string& a, const std::u16string& b) {
    const std::optional<SidebarRow> first = RowNamed(a);
    const std::optional<SidebarRow> second = RowNamed(b);
    return first && second && first->split.has_value() &&
           first->split == second->split;
  }

  FakeSidebarModel model_;
  RowDragSession session_;
  std::unique_ptr<views::Widget> widget_;
  raw_ptr<views::View> contents_ = nullptr;
  raw_ptr<TabListView> pinned_ = nullptr;
  raw_ptr<TabListView> today_ = nullptr;
};

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
