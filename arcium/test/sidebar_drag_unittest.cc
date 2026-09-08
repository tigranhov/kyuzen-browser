// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "arcium/test/test_app_activation.h"
#include "arcium/ui/playground/fake_sidebar_model.h"
#include "arcium/ui/sidebar/favorites_grid_view.h"
#include "arcium/ui/sidebar/folder_header_view.h"
#include "arcium/ui/sidebar/row_drag_data.h"
#include "arcium/ui/sidebar/row_drag_image.h"
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
#include "ui/events/event.h"
#include "ui/events/event_constants.h"
#include "ui/events/test/event_generator.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/vector2d.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/drag_controller.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/test/views_test_utils.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_utils.h"
#include "url/gurl.h"

namespace arcium {
namespace {

// Rebuilds every surface on a model change, the way SidebarView does. A drop
// issues a command that rebuilds the lists underneath it, and a test that
// skips the rebuild never puts that under load.
class RebuildOnChange : public SidebarModel::Observer {
 public:
  explicit RebuildOnChange(SidebarModel* model) : model_(model) {
    model_->AddObserver(this);
  }
  ~RebuildOnChange() override { model_->RemoveObserver(this); }

  void Watch(TabListView* list) { lists_.push_back(list); }
  void Watch(FavoritesGridView* grid) { grid_ = grid; }

  void OnSidebarModelChanged() override { Rebuild(); }

  void Rebuild() {
    const std::vector<SidebarRow> rows = model_->rows();
    for (TabListView* list : lists_) {
      list->SetRows(rows);
    }
    if (grid_) {
      grid_->SetRows(rows);
    }
  }

 private:
  raw_ptr<SidebarModel> model_;
  std::vector<raw_ptr<TabListView>> lists_;
  raw_ptr<FavoritesGridView> grid_ = nullptr;
};

// Task 8: dragging a row from one section to another, and the double-click
// rename that contends with the same press.
class SidebarDragTest : public views::ViewsTestBase {
 public:
  void SetUp() override {
    views::ViewsTestBase::SetUp();
    // Undo the foreground promotion ViewsTestHelperMac just made: a suite that
    // activates windows as a regular application drags the desktop onto its
    // Space, and none of these tests read real activation.
    arcium::test::SuppressTestAppActivation();
    widget_ = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
    contents_ = widget_->SetContentsView(std::make_unique<views::View>());
    // Vertical rather than fill: this fixture puts the grid and both lists in
    // one window, and they must not sit on top of one another.
    contents_->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical));
    widget_->SetBounds(gfx::Rect(0, 0, metrics::kSidebarWidth, 800));
    widget_->Show();
    widget_->Activate();
    generator_ = std::make_unique<ui::test::EventGenerator>(
        views::GetRootWindow(widget_.get()));
    // ui::MouseEvent counts repeats against a process-wide "last click", so a
    // test that clicks where the test before it clicked, soon enough after,
    // inherits its count — and a count of 1 *clears* EF_IS_DOUBLE_CLICK. Every
    // gesture test here would otherwise depend on what ran before it.
    ui::MouseEvent::ResetLastClickForTest();
    rebuilder_ = std::make_unique<RebuildOnChange>(&model_);
  }

  void TearDown() override {
    rebuilder_.reset();
    generator_.reset();
    grid_ = nullptr;
    pinned_ = nullptr;
    today_ = nullptr;
    contents_ = nullptr;
    widget_.reset();
    views::ViewsTestBase::TearDown();
  }

 protected:
  FavoritesGridView* MakeGrid() {
    grid_ =
        contents_->AddChildView(std::make_unique<FavoritesGridView>(&model_));
    grid_->SetDragSession(&session_);
    rebuilder_->Watch(grid_.get());
    return grid_;
  }

  TabListView* MakePinned() {
    pinned_ = contents_->AddChildView(
        std::make_unique<TabListView>(&model_, SidebarSection::kPinned));
    pinned_->SetDragSession(&session_);
    rebuilder_->Watch(pinned_.get());
    return pinned_;
  }

  TabListView* MakeToday() {
    today_ = contents_->AddChildView(
        std::make_unique<TabListView>(&model_, SidebarSection::kToday));
    today_->SetDragSession(&session_);
    rebuilder_->Watch(today_.get());
    return today_;
  }

  void Refresh() {
    rebuilder_->Rebuild();
    views::test::RunScheduledLayout(widget_.get());
  }

  // Writes the payload through the source's own DragController, so what a
  // drop reads back is what a real drag would have carried.
  std::unique_ptr<ui::OSExchangeData> DragDataFrom(
      views::DragController* controller,
      views::View* source) {
    auto data = std::make_unique<ui::OSExchangeData>();
    controller->WriteDragDataForView(source, gfx::Point(), data.get());
    return data;
  }

  // One drop at `at`, in `target`'s own coordinates: enter, one update, then
  // the callback the target hands back.
  //
  // Straight at the handlers rather than through EventGenerator, and
  // deliberately. A real drag runs a nested platform loop that a unit test
  // cannot enter, and every answer these handlers give depends on where
  // inside the target the pointer is — which is exactly what a synthesised
  // DropTargetEvent carries and what EventGenerator could not tell them.
  void DropOn(views::View* target,
              const ui::OSExchangeData& data,
              const gfx::Point& at) {
    views::test::RunScheduledLayout(widget_.get());
    ui::DropTargetEvent event(data, gfx::PointF(at), gfx::PointF(at),
                              ui::DragDropTypes::DRAG_MOVE);
    ASSERT_TRUE(target->CanDrop(data));
    target->OnDragEntered(event);
    EXPECT_EQ(ui::DragDropTypes::DRAG_MOVE, target->OnDragUpdated(event));
    views::View::DropCallback callback = target->GetDropCallback(event);
    ASSERT_TRUE(callback);
    auto operation = ui::mojom::DragOperation::kNone;
    std::move(callback).Run(event, operation, nullptr);
    EXPECT_EQ(ui::mojom::DragOperation::kMove, operation);
  }

  // A point inside `list` that puts the insertion line just above `row`.
  gfx::Point JustAbove(views::View* row) {
    views::test::RunScheduledLayout(widget_.get());
    return gfx::Point(10, row->y() + 2);
  }

  // A point below every row of `list`, which is the end of the section.
  gfx::Point BelowEveryRow(TabListView* list) {
    views::test::RunScheduledLayout(widget_.get());
    return gfx::Point(10, list->height() - 1);
  }

  TabRowView* RowIn(TabListView* list, size_t child_index) {
    views::test::RunScheduledLayout(widget_.get());
    return views::AsViewClass<TabRowView>(list->children()[child_index]);
  }

  std::vector<std::u16string> TitlesInSection(SidebarSection section) {
    std::vector<std::u16string> titles;
    for (const SidebarRow& row : model_.rows()) {
      if (row.section == section) {
        titles.push_back(row.title);
      }
    }
    return titles;
  }

  // The row the model now has under `title`, or nothing. Returned by value:
  // rows() hands back a fresh vector every call.
  std::optional<SidebarRow> RowNamed(const std::u16string& title) {
    for (const SidebarRow& row : model_.rows()) {
      if (row.title == title) {
        return row;
      }
    }
    return std::nullopt;
  }

  ui::test::EventGenerator& generator() { return *generator_; }

  FakeSidebarModel model_;
  // What SidebarView owns and hands to every section. A unit test has no
  // nested platform loop to end the drag, so End() is called by hand where a
  // real drag would have the widget report it.
  RowDragSession session_;
  std::unique_ptr<views::Widget> widget_;
  std::unique_ptr<ui::test::EventGenerator> generator_;
  std::unique_ptr<RebuildOnChange> rebuilder_;
  raw_ptr<views::View> contents_ = nullptr;
  raw_ptr<FavoritesGridView> grid_ = nullptr;
  raw_ptr<TabListView> pinned_ = nullptr;
  raw_ptr<TabListView> today_ = nullptr;
};

// ---------------------------------------------------------------------------
// The payload
// ---------------------------------------------------------------------------

TEST_F(SidebarDragTest, ARowWritesItsIdAndIndexNotItsAddress) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  MakePinned();
  Refresh();
  TabRowView* row = RowIn(pinned_, 0);
  ASSERT_TRUE(row);

  std::unique_ptr<ui::OSExchangeData> data = DragDataFrom(row, row);
  std::optional<RowDragData> payload = RowDragData::Read(*data);
  ASSERT_TRUE(payload);
  EXPECT_EQ(row->row().entry_id, payload->entry_id);
  EXPECT_EQ(row->tab_index(), payload->tab_index);
  EXPECT_TRUE(payload->is_entry());
  EXPECT_FALSE(payload->is_tab());
}

TEST_F(SidebarDragTest, ATodayRowWritesATabIndexAndNoEntry) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kToday, true);
  MakeToday();
  Refresh();
  TabRowView* row = RowIn(today_, 0);
  ASSERT_TRUE(row);

  std::optional<RowDragData> payload =
      RowDragData::Read(*DragDataFrom(row, row));
  ASSERT_TRUE(payload);
  EXPECT_FALSE(payload->entry_id.is_valid());
  EXPECT_TRUE(payload->is_tab());
}

// A drag with no image is fatal, not ugly: DragDropClientMac turns whatever
// the provider holds into an NSImage and DCHECKs that it is not zero-sized,
// so this is the difference between dragging a row and aborting the browser.
// None of the drop tests above reach it -- they call the drop handlers
// directly, because View::DoDrag enters a nested platform loop a unit test
// cannot drive, and the image is read inside that loop.
TEST_F(SidebarDragTest, ARowDragCarriesADragImage) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  MakePinned();
  Refresh();
  TabRowView* row = RowIn(pinned_, 0);
  ASSERT_TRUE(row);

  const gfx::ImageSkia image =
      DragDataFrom(row, row)->provider().GetDragImage();
  EXPECT_FALSE(image.isNull());
  EXPECT_FALSE(image.size().IsEmpty());
}

// The case that would put the crash back, and the reason the image is not
// built from the dragged view's own pixels. A row is drawn as soon as its tab
// exists -- before the favicon has loaded and before the page has given up a
// title -- so a user dragging a tab they just opened is dragging a row with
// nothing drawn in it but its URL. Straight at the helper, because the fake
// model gives every row a swatch favicon and cannot produce this row.
TEST_F(SidebarDragTest, ARowWithNoFaviconAndNoTitleStillCarriesADragImage) {
  SidebarRow bare;
  bare.url = GURL("https://bare.example/");
  bare.tab_index = 0;
  ASSERT_TRUE(bare.favicon.IsEmpty());
  ASSERT_TRUE(bare.title.empty());

  ui::OSExchangeData data;
  // Null source too: rasterizing a favicon needs a colour provider, which a
  // view has only once it is in a widget, and this is the path that has none.
  SetRowDragImage(bare, nullptr, gfx::Point(), &data);

  const gfx::ImageSkia image = data.provider().GetDragImage();
  EXPECT_FALSE(image.isNull());
  EXPECT_FALSE(image.size().IsEmpty());
}

// The guard that keeps a row with no URL at all away from the code above.
// It matters more than it looks: the drag image is a button, and painting a
// button with neither text nor a URL to fall back on fails Views' own
// accessibility paint check -- an unnamed focusable view -- which is fatal in
// exactly the same builds the zero-size image was. So "a blank row is not
// draggable" is what stops one abort being traded for another.
TEST_F(SidebarDragTest, ARowNamingNeitherAnEntryNorATabIsNotDraggable) {
  auto row = std::make_unique<TabRowView>(TabRowView::Delegate{});
  ASSERT_FALSE(row->row().entry_id.is_valid());
  ASSERT_LT(row->row().tab_index, 0);

  EXPECT_EQ(ui::DragDropTypes::DRAG_NONE,
            row->GetDragOperationsForView(row.get(), gfx::Point()));
}

// The grid is a second drag source with its own WriteDragDataForView, so it
// can lose the image on its own.
TEST_F(SidebarDragTest, AFavouriteTileDragCarriesADragImage) {
  model_.AddTab(u"Fav", "https://fav.example/", SidebarSection::kFavorites,
                false);
  MakeGrid();
  Refresh();
  views::test::RunScheduledLayout(widget_.get());
  ASSERT_FALSE(grid_->children().empty());
  views::View* tile = grid_->children()[0];

  const gfx::ImageSkia image =
      DragDataFrom(grid_, tile)->provider().GetDragImage();
  EXPECT_FALSE(image.isNull());
  EXPECT_FALSE(image.size().IsEmpty());
}

TEST_F(SidebarDragTest, DataFromSomewhereElseIsRefused) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  MakePinned();
  MakeToday();
  MakeGrid();
  Refresh();

  ui::OSExchangeData stranger;
  stranger.SetString(u"https://elsewhere.example/");
  EXPECT_FALSE(RowDragData::Read(stranger).has_value());
  EXPECT_FALSE(pinned_->CanDrop(stranger));
  EXPECT_FALSE(today_->CanDrop(stranger));
  EXPECT_FALSE(grid_->CanDrop(stranger));
}

// ---------------------------------------------------------------------------
// The drop table
// ---------------------------------------------------------------------------

// Today tab -> Favourites: AddToFavorites(tab_index).
TEST_F(SidebarDragTest, ATodayTabDroppedOnFavouritesBecomesOne) {
  model_.AddTab(u"Fav", "https://fav.example/", SidebarSection::kFavorites,
                false);
  model_.AddTab(u"Loose", "https://loose.example/", SidebarSection::kToday,
                true);
  MakeGrid();
  MakeToday();
  Refresh();
  TabRowView* row = RowIn(today_, 0);
  ASSERT_TRUE(row);

  // The gap before the first tile, which is where the gap indicator was
  // drawn: the new favourite lands there rather than at the end. Appending
  // would make the indicator a promise the command does not keep.
  DropOn(grid_, *DragDataFrom(row, row), gfx::Point(0, 0));

  EXPECT_EQ((std::vector<std::u16string>{u"Loose", u"Fav"}),
            TitlesInSection(SidebarSection::kFavorites));
  EXPECT_TRUE(TitlesInSection(SidebarSection::kToday).empty());
}

// The same promise on the Pinned side, which is the one pinning depends on.
TEST_F(SidebarDragTest, ATodayTabDroppedOnPinnedLandsWhereTheLineWas) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  model_.AddTab(u"Two", "https://two.example/", SidebarSection::kPinned, false);
  model_.AddTab(u"Loose", "https://loose.example/", SidebarSection::kToday,
                true);
  MakePinned();
  MakeToday();
  Refresh();
  TabRowView* loose = RowIn(today_, 0);
  ASSERT_TRUE(loose);

  DropOn(pinned_, *DragDataFrom(loose, loose), JustAbove(RowIn(pinned_, 1)));

  EXPECT_EQ((std::vector<std::u16string>{u"One", u"Loose", u"Two"}),
            TitlesInSection(SidebarSection::kPinned));
  EXPECT_TRUE(TitlesInSection(SidebarSection::kToday).empty());
}

// Today tab -> Pinned: PinTab(tab_index).
TEST_F(SidebarDragTest, ATodayTabDroppedOnPinnedBecomesAnEntry) {
  model_.AddTab(u"Pin", "https://pin.example/", SidebarSection::kPinned, false);
  model_.AddTab(u"Loose", "https://loose.example/", SidebarSection::kToday,
                true);
  MakePinned();
  MakeToday();
  Refresh();
  TabRowView* row = RowIn(today_, 0);
  ASSERT_TRUE(row);

  DropOn(pinned_, *DragDataFrom(row, row), BelowEveryRow(pinned_));

  EXPECT_EQ((std::vector<std::u16string>{u"Pin", u"Loose"}),
            TitlesInSection(SidebarSection::kPinned));
  EXPECT_TRUE(TitlesInSection(SidebarSection::kToday).empty());
  std::optional<SidebarRow> moved = RowNamed(u"Loose");
  ASSERT_TRUE(moved);
  EXPECT_TRUE(moved->entry_id.is_valid());
}

// Today tab -> Today: a tab-strip move, as in Stage 1.
TEST_F(SidebarDragTest, ATodayTabDroppedInTodayMovesTheTab) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kToday, true);
  model_.AddTab(u"Two", "https://two.example/", SidebarSection::kToday, false);
  model_.AddTab(u"Three", "https://three.example/", SidebarSection::kToday,
                false);
  MakeToday();
  Refresh();
  TabRowView* three = RowIn(today_, 2);
  ASSERT_TRUE(three);
  ASSERT_EQ(2, three->tab_index());

  // Just above the first row: the last tab goes to the front.
  DropOn(today_, *DragDataFrom(three, three), JustAbove(RowIn(today_, 0)));

  EXPECT_EQ((std::vector<std::u16string>{u"Three", u"One", u"Two"}),
            TitlesInSection(SidebarSection::kToday));
}

// The downward half of the same gesture, and the one nothing pinned: every
// other Today-reorder test here drags upward or off the end, and neither
// touches LiftThenInsertIndex's correction. Deleting the correction from this
// call site used to fail no test at all, while the identical rule three lines
// away in the entry reorder was covered — which is exactly how a shared rule
// with one test hides two live branches.
TEST_F(SidebarDragTest, ATodayTabDraggedDownLandsInTheGapItWasDroppedIn) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kToday, true);
  model_.AddTab(u"Two", "https://two.example/", SidebarSection::kToday, false);
  model_.AddTab(u"Three", "https://three.example/", SidebarSection::kToday,
                false);
  MakeToday();
  Refresh();
  TabRowView* one = RowIn(today_, 0);
  ASSERT_TRUE(one);
  ASSERT_EQ(0, one->tab_index());

  // The gap just above "Three", which is the gap between "Two" and "Three".
  DropOn(today_, *DragDataFrom(one, one), JustAbove(RowIn(today_, 2)));

  EXPECT_EQ((std::vector<std::u16string>{u"Two", u"One", u"Three"}),
            TitlesInSection(SidebarSection::kToday));
}

TEST_F(SidebarDragTest, ATodayTabDroppedBelowEveryRowGoesToTheEnd) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kToday, true);
  model_.AddTab(u"Two", "https://two.example/", SidebarSection::kToday, false);
  model_.AddTab(u"Three", "https://three.example/", SidebarSection::kToday,
                false);
  MakeToday();
  Refresh();
  TabRowView* one = RowIn(today_, 0);
  ASSERT_TRUE(one);

  DropOn(today_, *DragDataFrom(one, one), BelowEveryRow(today_));

  EXPECT_EQ((std::vector<std::u16string>{u"Two", u"Three", u"One"}),
            TitlesInSection(SidebarSection::kToday));
}

// Entry -> Favourites: the kind change and the reorder, as one command.
TEST_F(SidebarDragTest, APinnedEntryDroppedOnFavouritesChangesSection) {
  model_.AddTab(u"Fav", "https://fav.example/", SidebarSection::kFavorites,
                false);
  model_.AddTab(u"Pin", "https://pin.example/", SidebarSection::kPinned, false);
  MakeGrid();
  MakePinned();
  Refresh();
  TabRowView* row = RowIn(pinned_, 0);
  ASSERT_TRUE(row);

  // At the very left of the grid, which is the gap before the first tile.
  DropOn(grid_, *DragDataFrom(row, row), gfx::Point(0, 4));

  EXPECT_EQ((std::vector<std::u16string>{u"Pin", u"Fav"}),
            TitlesInSection(SidebarSection::kFavorites));
  EXPECT_TRUE(TitlesInSection(SidebarSection::kPinned).empty());
}

// Entry -> Pinned.
TEST_F(SidebarDragTest, AFavouriteDroppedOnPinnedChangesSection) {
  model_.AddTab(u"Fav", "https://fav.example/", SidebarSection::kFavorites,
                false);
  model_.AddTab(u"Pin", "https://pin.example/", SidebarSection::kPinned, false);
  MakeGrid();
  MakePinned();
  Refresh();
  views::View* tile = grid_->children()[0];

  DropOn(pinned_, *DragDataFrom(grid_.get(), tile),
         JustAbove(RowIn(pinned_, 0)));

  EXPECT_TRUE(TitlesInSection(SidebarSection::kFavorites).empty());
  EXPECT_EQ((std::vector<std::u16string>{u"Fav", u"Pin"}),
            TitlesInSection(SidebarSection::kPinned));
}

// Favourite -> Favourites: the same command, reordering inside the grid.
TEST_F(SidebarDragTest, AFavouriteDroppedInTheGridReorders) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kFavorites,
                false);
  model_.AddTab(u"Two", "https://two.example/", SidebarSection::kFavorites,
                false);
  model_.AddTab(u"Three", "https://three.example/", SidebarSection::kFavorites,
                false);
  MakeGrid();
  Refresh();
  views::View* three = grid_->children()[2];

  DropOn(grid_, *DragDataFrom(grid_.get(), three), gfx::Point(0, 4));

  EXPECT_EQ((std::vector<std::u16string>{u"Three", u"One", u"Two"}),
            TitlesInSection(SidebarSection::kFavorites));
}

// The other direction, which is the common one and which every reorder test
// in Task 8 missed. The drop index counts the section as it looks now, with
// the dragged entry still in it; ReorderEntry is lift-then-insert, so an
// entry already above the gap it is dropped into shifts everything below it
// up one when it is lifted out. Without the correction this lands "One" after
// "Three" instead of between "Two" and "Three".
TEST_F(SidebarDragTest, AFavouriteDraggedDownLandsInTheGapItWasDroppedIn) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kFavorites,
                false);
  model_.AddTab(u"Two", "https://two.example/", SidebarSection::kFavorites,
                false);
  model_.AddTab(u"Three", "https://three.example/", SidebarSection::kFavorites,
                false);
  MakeGrid();
  Refresh();
  views::View* one = grid_->children()[0];
  // The gap before the third tile, which is the gap between "Two" and
  // "Three".
  const int gap_x = grid_->children()[2]->x();

  DropOn(grid_, *DragDataFrom(grid_.get(), one), gfx::Point(gap_x, 4));

  EXPECT_EQ((std::vector<std::u16string>{u"Two", u"One", u"Three"}),
            TitlesInSection(SidebarSection::kFavorites));
}

// The same overshoot in a list rather than the grid.
TEST_F(SidebarDragTest, APinnedEntryDraggedDownLandsInTheGapItWasDroppedIn) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  model_.AddTab(u"Two", "https://two.example/", SidebarSection::kPinned, false);
  model_.AddTab(u"Three", "https://three.example/", SidebarSection::kPinned,
                false);
  MakePinned();
  Refresh();
  TabRowView* one = RowIn(pinned_, 0);
  ASSERT_TRUE(one);
  ASSERT_EQ(u"One", one->row().title);

  // The gap just above "Three", which is the gap between "Two" and "Three".
  DropOn(pinned_, *DragDataFrom(one, one), JustAbove(RowIn(pinned_, 2)));

  EXPECT_EQ((std::vector<std::u16string>{u"Two", u"One", u"Three"}),
            TitlesInSection(SidebarSection::kPinned));
}

// And upward in the same shape, so the pair reads as one fact rather than two
// tests that happen to disagree about direction.
TEST_F(SidebarDragTest, APinnedEntryDraggedUpLandsInTheGapItWasDroppedIn) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  model_.AddTab(u"Two", "https://two.example/", SidebarSection::kPinned, false);
  model_.AddTab(u"Three", "https://three.example/", SidebarSection::kPinned,
                false);
  MakePinned();
  Refresh();
  TabRowView* three = RowIn(pinned_, 2);
  ASSERT_TRUE(three);
  ASSERT_EQ(u"Three", three->row().title);

  DropOn(pinned_, *DragDataFrom(three, three), JustAbove(RowIn(pinned_, 1)));

  EXPECT_EQ((std::vector<std::u16string>{u"One", u"Three", u"Two"}),
            TitlesInSection(SidebarSection::kPinned));
}

// Entry -> Folder header: SetEntryFolder, through MoveEntryToFolder.
TEST_F(SidebarDragTest, AnEntryDroppedOnAFolderHeaderJoinsTheFolder) {
  model_.AddTab(u"Inside", "https://inside.example/", SidebarSection::kPinned,
                false);
  model_.AddTab(u"Outside", "https://outside.example/", SidebarSection::kPinned,
                false);
  MakePinned();
  const FolderId folder = model_.AddFolderWith(u"Work", {u"Inside"});
  Refresh();
  ASSERT_EQ(1, model_.folders()[0].entry_count);

  FolderHeaderView* header =
      views::AsViewClass<FolderHeaderView>(pinned_->children()[0]);
  ASSERT_TRUE(header);
  // The top-level row is last in the plan: header, its member, then the rest.
  TabRowView* outside = RowIn(pinned_, 2);
  ASSERT_TRUE(outside);
  ASSERT_EQ(u"Outside", outside->row().title);

  DropOn(header, *DragDataFrom(outside, outside), gfx::Point(10, 10));

  EXPECT_EQ(2, model_.folders()[0].entry_count);
  std::optional<SidebarRow> moved = RowNamed(u"Outside");
  ASSERT_TRUE(moved);
  EXPECT_EQ(folder, moved->folder_id);
}

// A Today tab has no entry to put in a folder. Refusing it at the header is
// what lets the drop fall through to the Pinned list, which makes one.
TEST_F(SidebarDragTest, AFolderHeaderRefusesATodayTab) {
  model_.AddTab(u"Inside", "https://inside.example/", SidebarSection::kPinned,
                false);
  model_.AddTab(u"Loose", "https://loose.example/", SidebarSection::kToday,
                true);
  MakePinned();
  MakeToday();
  model_.AddFolderWith(u"Work", {u"Inside"});
  Refresh();

  FolderHeaderView* header =
      views::AsViewClass<FolderHeaderView>(pinned_->children()[0]);
  ASSERT_TRUE(header);
  TabRowView* loose = RowIn(today_, 0);
  ASSERT_TRUE(loose);

  std::unique_ptr<ui::OSExchangeData> data = DragDataFrom(loose, loose);
  EXPECT_FALSE(header->CanDrop(*data));
  // The list behind it takes what the header refused.
  EXPECT_TRUE(pinned_->CanDrop(*data));
}

// A folder holds pinned entries only — Task 7's rule, which
// MoveEntryToFolder enforces by doing nothing. Accepting the drop and then
// discarding it highlights the header and eats the gesture, so the header
// refuses instead.
TEST_F(SidebarDragTest, AFolderHeaderRefusesAFavourite) {
  model_.AddTab(u"Inside", "https://inside.example/", SidebarSection::kPinned,
                false);
  model_.AddTab(u"Fav", "https://fav.example/", SidebarSection::kFavorites,
                false);
  MakeGrid();
  MakePinned();
  model_.AddFolderWith(u"Work", {u"Inside"});
  Refresh();

  FolderHeaderView* header =
      views::AsViewClass<FolderHeaderView>(pinned_->children()[0]);
  ASSERT_TRUE(header);
  views::View* tile = grid_->children()[0];
  std::unique_ptr<ui::OSExchangeData> data = DragDataFrom(grid_.get(), tile);

  EXPECT_FALSE(header->CanDrop(*data));
  // No highlight either: a header the drop cannot land in must not look like
  // one it can.
  ui::DropTargetEvent event(*data, gfx::PointF(10, 10), gfx::PointF(10, 10),
                            ui::DragDropTypes::DRAG_MOVE);
  EXPECT_FALSE(header->GetDropCallback(event));
  EXPECT_FALSE(header->is_drop_target_for_testing());
  EXPECT_EQ(1, model_.folders()[0].entry_count);
}

// The header refuses a favourite and gives no highlight, but DropHelper walks
// a refused drop up to the view behind it — the Pinned list, which used to
// still accept and land the favourite as a top-level pinned entry. That
// silently changes the row's kind as a side effect of a gesture aimed at a
// folder, which is the exact outcome the header's own refusal exists to
// prevent. The list must refuse too, while the pointer sits over the header
// that refused it.
TEST_F(SidebarDragTest,
       PinnedRefusesAFavouriteWhenTheHeaderUnderThePointerRefusedIt) {
  model_.AddTab(u"Inside", "https://inside.example/", SidebarSection::kPinned,
                false);
  model_.AddTab(u"Fav", "https://fav.example/", SidebarSection::kFavorites,
                false);
  MakeGrid();
  MakePinned();
  model_.AddFolderWith(u"Work", {u"Inside"});
  Refresh();

  FolderHeaderView* header =
      views::AsViewClass<FolderHeaderView>(pinned_->children()[0]);
  ASSERT_TRUE(header);
  views::View* tile = grid_->children()[0];
  std::unique_ptr<ui::OSExchangeData> data = DragDataFrom(grid_.get(), tile);

  // Squarely inside the header, in the Pinned list's own coordinate space —
  // exactly what event.location() carries once DropHelper walks the header's
  // refusal up to its owning list.
  const gfx::Point at = header->bounds().CenterPoint();
  ui::DropTargetEvent event(*data, gfx::PointF(at), gfx::PointF(at),
                            ui::DragDropTypes::DRAG_MOVE);

  // CanDrop is format-level and unchanged: the list still takes an entry in
  // general, so a drag elsewhere in it still highlights.
  ASSERT_TRUE(pinned_->CanDrop(*data));
  pinned_->OnDragEntered(event);
  EXPECT_EQ(ui::DragDropTypes::DRAG_NONE, pinned_->OnDragUpdated(event));
  EXPECT_FALSE(pinned_->drop_index_for_testing().has_value());
  EXPECT_FALSE(pinned_->GetDropCallback(event));

  // Nothing happened: the favourite is still a favourite, the folder still
  // holds only "Inside".
  EXPECT_EQ((std::vector<std::u16string>{u"Fav"}),
            TitlesInSection(SidebarSection::kFavorites));
  EXPECT_EQ((std::vector<std::u16string>{u"Inside"}),
            TitlesInSection(SidebarSection::kPinned));
  EXPECT_EQ(1, model_.folders()[0].entry_count);
}

// The asymmetry the ruling keeps: a Today tab dropped on the same header
// falls all the way through and pins at the top level, because that changes
// no row's kind — there is no kind to protect for a tab that was never an
// entry.
TEST_F(SidebarDragTest, PinnedStillAcceptsATodayTabOverAFolderHeader) {
  model_.AddTab(u"Inside", "https://inside.example/", SidebarSection::kPinned,
                false);
  model_.AddTab(u"Loose", "https://loose.example/", SidebarSection::kToday,
                true);
  MakePinned();
  MakeToday();
  model_.AddFolderWith(u"Work", {u"Inside"});
  Refresh();

  FolderHeaderView* header =
      views::AsViewClass<FolderHeaderView>(pinned_->children()[0]);
  ASSERT_TRUE(header);
  TabRowView* loose = RowIn(today_, 0);
  ASSERT_TRUE(loose);
  std::unique_ptr<ui::OSExchangeData> data = DragDataFrom(loose, loose);

  DropOn(pinned_, *data, header->bounds().CenterPoint());

  EXPECT_TRUE(TitlesInSection(SidebarSection::kToday).empty());
  std::optional<SidebarRow> moved = RowNamed(u"Loose");
  ASSERT_TRUE(moved);
  EXPECT_EQ(SidebarSection::kPinned, moved->section);
  EXPECT_FALSE(moved->folder_id.has_value());
}

// A fix that refuses too much is worse than the bug: the same favourite,
// dropped on the same list, away from any header, still lands.
TEST_F(SidebarDragTest, PinnedStillAcceptsAFavouriteAwayFromAFolderHeader) {
  model_.AddTab(u"Inside", "https://inside.example/", SidebarSection::kPinned,
                false);
  model_.AddTab(u"Fav", "https://fav.example/", SidebarSection::kFavorites,
                false);
  MakeGrid();
  MakePinned();
  model_.AddFolderWith(u"Work", {u"Inside"});
  Refresh();

  views::View* tile = grid_->children()[0];
  std::unique_ptr<ui::OSExchangeData> data = DragDataFrom(grid_.get(), tile);

  DropOn(pinned_, *data, BelowEveryRow(pinned_));

  EXPECT_TRUE(TitlesInSection(SidebarSection::kFavorites).empty());
  EXPECT_EQ((std::vector<std::u16string>{u"Inside", u"Fav"}),
            TitlesInSection(SidebarSection::kPinned));
}

// The predicate asks the list, not the payload, so it also refuses an id the
// model has dropped since the drag began — which is what no field written
// into the payload at drag-start could do.
TEST_F(SidebarDragTest, AFolderHeaderRefusesAnEntryTheModelNoLongerHas) {
  model_.AddTab(u"Inside", "https://inside.example/", SidebarSection::kPinned,
                false);
  model_.AddTab(u"Outside", "https://outside.example/", SidebarSection::kPinned,
                false);
  MakePinned();
  model_.AddFolderWith(u"Work", {u"Inside"});
  Refresh();
  FolderHeaderView* header =
      views::AsViewClass<FolderHeaderView>(pinned_->children()[0]);
  TabRowView* outside = RowIn(pinned_, 2);
  ASSERT_TRUE(header);
  ASSERT_TRUE(outside);
  std::unique_ptr<ui::OSExchangeData> data = DragDataFrom(outside, outside);
  ASSERT_TRUE(header->CanDrop(*data));

  // Another window unpins it while the drag is still running.
  const EntryId id = outside->row().entry_id;
  model_.UnpinEntry(id);
  Refresh();

  EXPECT_FALSE(header->CanDrop(*data));
}

// Entry -> Today, warm: the entry goes, its tab stays.
TEST_F(SidebarDragTest, AWarmEntryDroppedOnTodayLeavesItsTab) {
  model_.AddTab(u"Pin", "https://pin.example/", SidebarSection::kPinned, false);
  model_.AddTab(u"Loose", "https://loose.example/", SidebarSection::kToday,
                true);
  MakePinned();
  MakeToday();
  Refresh();
  TabRowView* pin = RowIn(pinned_, 0);
  ASSERT_TRUE(pin);
  ASSERT_FALSE(pin->row().is_cold);

  DropOn(today_, *DragDataFrom(pin, pin), BelowEveryRow(today_));

  EXPECT_TRUE(TitlesInSection(SidebarSection::kPinned).empty());
  EXPECT_EQ((std::vector<std::u16string>{u"Loose", u"Pin"}),
            TitlesInSection(SidebarSection::kToday));
  std::optional<SidebarRow> moved = RowNamed(u"Pin");
  ASSERT_TRUE(moved);
  EXPECT_FALSE(moved->entry_id.is_valid());
  EXPECT_FALSE(moved->is_cold);
}

// Today's order *is* the tab-strip order, so the line drawn while an entry is
// dragged into Today is a promise about where its tab goes. The warm and cold
// tests above drop below every row, which is the one place appending and
// placing agree.
TEST_F(SidebarDragTest, AnEntryDroppedIntoTodayLandsWhereTheLineWas) {
  model_.AddTab(u"Pin", "https://pin.example/", SidebarSection::kPinned, false);
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kToday, true);
  model_.AddTab(u"Two", "https://two.example/", SidebarSection::kToday, false);
  MakePinned();
  MakeToday();
  Refresh();
  TabRowView* pin = RowIn(pinned_, 0);
  ASSERT_TRUE(pin);

  DropOn(today_, *DragDataFrom(pin, pin), JustAbove(RowIn(today_, 0)));

  EXPECT_EQ((std::vector<std::u16string>{u"Pin", u"One", u"Two"}),
            TitlesInSection(SidebarSection::kToday));
  EXPECT_TRUE(TitlesInSection(SidebarSection::kPinned).empty());
}

// Entry -> Today, cold: nothing lives behind it, so its URL is opened as a
// tab first. Same end state as the warm case, which is why the drop needs no
// undo to apologise for.
TEST_F(SidebarDragTest, AColdEntryDroppedOnTodayIsOpenedNotDeleted) {
  model_.AddColdEntry(u"Cold", "https://cold.example/",
                      SidebarSection::kPinned);
  model_.AddTab(u"Loose", "https://loose.example/", SidebarSection::kToday,
                true);
  MakePinned();
  MakeToday();
  Refresh();
  TabRowView* cold = RowIn(pinned_, 0);
  ASSERT_TRUE(cold);
  ASSERT_TRUE(cold->row().is_cold);

  DropOn(today_, *DragDataFrom(cold, cold), BelowEveryRow(today_));

  EXPECT_TRUE(TitlesInSection(SidebarSection::kPinned).empty());
  std::optional<SidebarRow> moved = RowNamed(u"Cold");
  ASSERT_TRUE(moved);
  EXPECT_EQ(SidebarSection::kToday, moved->section);
  // Warm, with a tab index: the page is there, only the entry went.
  EXPECT_FALSE(moved->is_cold);
  EXPECT_GE(moved->tab_index, 0);
  EXPECT_EQ(GURL("https://cold.example/"), moved->url);
}

// ---------------------------------------------------------------------------
// Where the drop lands
// ---------------------------------------------------------------------------

// `rows_` is in laid-out order — a folder's members come before the top level
// — and that is not the order the model keeps positions in. Reading the drop
// index off the row's own slot would move the entry to the wrong place, and
// the same confusion in the tab-index clamp was once a hard abort.
TEST_F(SidebarDragTest, TheDropIndexIsAPositionNotAPlanSlot) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  model_.AddTab(u"Two", "https://two.example/", SidebarSection::kPinned, false);
  model_.AddTab(u"Three", "https://three.example/", SidebarSection::kPinned,
                false);
  MakePinned();
  // The folder holds the last entry, so the plan is: header, Three, One, Two.
  model_.AddFolderWith(u"Work", {u"Three"});
  Refresh();
  ASSERT_EQ(u"Three", RowIn(pinned_, 1)->row().title);
  ASSERT_EQ(u"One", RowIn(pinned_, 2)->row().title);

  TabRowView* two = RowIn(pinned_, 3);
  ASSERT_TRUE(two);
  ASSERT_EQ(u"Two", two->row().title);
  // Just above the row for "One", which is plan slot 1 among the rows but
  // model position 0. Taking the slot would ask for position 1, where "Two"
  // already is, and nothing would move.
  DropOn(pinned_, *DragDataFrom(two, two), JustAbove(RowIn(pinned_, 2)));

  EXPECT_EQ((std::vector<std::u16string>{u"Two", u"One", u"Three"}),
            TitlesInSection(SidebarSection::kPinned));
}

// The rows are pooled by index and the model is shared by every window on the
// profile, so between the pointer coming to rest and the drop running a slot
// can be handed a different entry. The drop is aimed at an entry, not at a
// slot: "One" going away must not move where "Four" lands.
TEST_F(SidebarDragTest, ADropLandsOnTheEntryItWasAimedAtEvenAfterARebuild) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  model_.AddTab(u"Two", "https://two.example/", SidebarSection::kPinned, false);
  model_.AddTab(u"Three", "https://three.example/", SidebarSection::kPinned,
                false);
  model_.AddTab(u"Four", "https://four.example/", SidebarSection::kPinned,
                false);
  MakePinned();
  Refresh();
  TabRowView* four = RowIn(pinned_, 3);
  TabRowView* three = RowIn(pinned_, 2);
  ASSERT_TRUE(four);
  ASSERT_TRUE(three);
  ASSERT_EQ(u"Four", four->row().title);
  ASSERT_EQ(u"Three", three->row().title);
  std::unique_ptr<ui::OSExchangeData> data = DragDataFrom(four, four);

  // Aimed just above "Three".
  const gfx::Point at = JustAbove(three);
  ui::DropTargetEvent event(*data, gfx::PointF(at), gfx::PointF(at),
                            ui::DragDropTypes::DRAG_MOVE);
  ASSERT_TRUE(pinned_->CanDrop(*data));
  pinned_->OnDragEntered(event);
  pinned_->OnDragUpdated(event);
  views::View::DropCallback callback = pinned_->GetDropCallback(event);
  ASSERT_TRUE(callback);

  // Another window unpins "One" before the drop runs. Every row below it
  // slides up a slot; slot 2 now holds "Four" itself.
  const EntryId one = RowIn(pinned_, 0)->row().entry_id;
  model_.UnpinEntry(one);
  Refresh();

  auto operation = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, operation, nullptr);

  // Before "Three", which is what the line was drawn above. Reading the slot
  // back instead would ask for the place "Four" already sits and move
  // nothing.
  EXPECT_EQ((std::vector<std::u16string>{u"Two", u"Four", u"Three"}),
            TitlesInSection(SidebarSection::kPinned));
}

// ---------------------------------------------------------------------------
// An empty section, which is a section with nothing to hit
// ---------------------------------------------------------------------------

// On a fresh profile the Pinned list holds nothing, hides itself, and is
// skipped by GetEventHandlerForPoint — so pinning by drag, the central Arc
// gesture, is unreachable. It reserves a band while a row drag is running,
// and only then: idle layout is Stage 1's and its snapshots depend on it.
TEST_F(SidebarDragTest, AnEmptyPinnedListReservesADropBandOnlyDuringADrag) {
  model_.AddTab(u"Loose", "https://loose.example/", SidebarSection::kToday,
                true);
  MakePinned();
  MakeToday();
  Refresh();
  ASSERT_EQ(0u, pinned_->row_count());
  EXPECT_FALSE(pinned_->GetVisible());

  TabRowView* loose = RowIn(today_, 0);
  ASSERT_TRUE(loose);
  // Written through the row's own DragController, which is what announces the
  // drag; nothing else in Views does.
  std::unique_ptr<ui::OSExchangeData> data = DragDataFrom(loose, loose);
  views::test::RunScheduledLayout(widget_.get());

  EXPECT_TRUE(session_.in_flight());
  EXPECT_TRUE(pinned_->GetVisible());
  EXPECT_GT(pinned_->height(), 0);
  EXPECT_TRUE(pinned_->CanDrop(*data));

  // The drag ends without a drop: the band goes and the sidebar looks exactly
  // as it did.
  session_.End();
  views::test::RunScheduledLayout(widget_.get());
  EXPECT_FALSE(pinned_->GetVisible());

  // And with the drag running again, the drop actually lands.
  data = DragDataFrom(loose, loose);
  DropOn(pinned_, *data, gfx::Point(10, 4));
  EXPECT_EQ((std::vector<std::u16string>{u"Loose"}),
            TitlesInSection(SidebarSection::kPinned));
}

// The grid has the same guard, and it is the one the Task 8 report flagged.
TEST_F(SidebarDragTest, AnEmptyFavouritesGridReservesADropBandOnlyDuringADrag) {
  model_.AddTab(u"Loose", "https://loose.example/", SidebarSection::kToday,
                true);
  MakeGrid();
  MakeToday();
  Refresh();
  EXPECT_FALSE(grid_->GetVisible());

  TabRowView* loose = RowIn(today_, 0);
  ASSERT_TRUE(loose);
  std::unique_ptr<ui::OSExchangeData> data = DragDataFrom(loose, loose);
  views::test::RunScheduledLayout(widget_.get());

  EXPECT_TRUE(grid_->GetVisible());
  EXPECT_GT(grid_->height(), 0);
  EXPECT_TRUE(grid_->CanDrop(*data));

  session_.End();
  views::test::RunScheduledLayout(widget_.get());
  EXPECT_FALSE(grid_->GetVisible());

  data = DragDataFrom(loose, loose);
  DropOn(grid_, *data, gfx::Point(0, 4));
  EXPECT_EQ((std::vector<std::u16string>{u"Loose"}),
            TitlesInSection(SidebarSection::kFavorites));
}

// A guard, not a regression: no gesture can drop a tab into a Today list with
// no rows in it, because the tab being dragged is one of them. The clamp
// still has to survive being asked, since an empty range would hand
// std::clamp lo > hi and libc++ hardening turns that into an abort.
TEST_F(SidebarDragTest, DroppingATabIntoAnEmptyTodayListMovesNothing) {
  model_.AddTab(u"Pin", "https://pin.example/", SidebarSection::kPinned, false);
  MakeToday();
  Refresh();
  ASSERT_EQ(0u, today_->row_count());

  RowDragData payload;
  payload.tab_index = 5;
  ui::OSExchangeData data;
  payload.Write(&data);
  DropOn(today_, data, gfx::Point(10, 4));

  EXPECT_EQ((std::vector<std::u16string>{u"Pin"}),
            TitlesInSection(SidebarSection::kPinned));
  EXPECT_TRUE(TitlesInSection(SidebarSection::kToday).empty());
}

TEST_F(SidebarDragTest, TheInsertionLineFollowsThePointerAndClearsOnExit) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kToday, true);
  model_.AddTab(u"Two", "https://two.example/", SidebarSection::kToday, false);
  MakeToday();
  Refresh();
  TabRowView* one = RowIn(today_, 0);
  TabRowView* two = RowIn(today_, 1);
  ASSERT_TRUE(one);
  ASSERT_TRUE(two);
  std::unique_ptr<ui::OSExchangeData> data = DragDataFrom(one, one);

  EXPECT_FALSE(today_->drop_index_for_testing().has_value());

  auto update_at = [&](const gfx::Point& at) {
    ui::DropTargetEvent event(*data, gfx::PointF(at), gfx::PointF(at),
                              ui::DragDropTypes::DRAG_MOVE);
    today_->OnDragEntered(event);
    today_->OnDragUpdated(event);
  };
  update_at(JustAbove(one));
  EXPECT_EQ(0u, today_->drop_index_for_testing());
  update_at(JustAbove(two));
  EXPECT_EQ(1u, today_->drop_index_for_testing());
  update_at(BelowEveryRow(today_));
  EXPECT_EQ(2u, today_->drop_index_for_testing());

  today_->OnDragExited();
  EXPECT_FALSE(today_->drop_index_for_testing().has_value());
}

TEST_F(SidebarDragTest, TheGridsGapIndicatorFollowsThePointerAndClears) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kFavorites,
                false);
  model_.AddTab(u"Two", "https://two.example/", SidebarSection::kFavorites,
                false);
  MakeGrid();
  Refresh();
  views::View* one = grid_->children()[0];
  std::unique_ptr<ui::OSExchangeData> data = DragDataFrom(grid_.get(), one);

  auto update_at = [&](const gfx::Point& at) {
    ui::DropTargetEvent event(*data, gfx::PointF(at), gfx::PointF(at),
                              ui::DragDropTypes::DRAG_MOVE);
    grid_->OnDragEntered(event);
    grid_->OnDragUpdated(event);
  };
  update_at(gfx::Point(0, 4));
  EXPECT_EQ(0u, grid_->drop_index_for_testing());
  // Past the middle of the first tile is the gap after it.
  update_at(gfx::Point(grid_->children()[1]->x(), 4));
  EXPECT_EQ(1u, grid_->drop_index_for_testing());

  grid_->OnDragExited();
  EXPECT_FALSE(grid_->drop_index_for_testing().has_value());
}

TEST_F(SidebarDragTest, AFolderHeaderHighlightsWhileADragIsOverIt) {
  model_.AddTab(u"Inside", "https://inside.example/", SidebarSection::kPinned,
                false);
  model_.AddTab(u"Outside", "https://outside.example/", SidebarSection::kPinned,
                false);
  MakePinned();
  model_.AddFolderWith(u"Work", {u"Inside"});
  Refresh();
  FolderHeaderView* header =
      views::AsViewClass<FolderHeaderView>(pinned_->children()[0]);
  TabRowView* outside = RowIn(pinned_, 2);
  ASSERT_TRUE(header);
  ASSERT_TRUE(outside);
  std::unique_ptr<ui::OSExchangeData> data = DragDataFrom(outside, outside);

  EXPECT_FALSE(header->is_drop_target_for_testing());
  ui::DropTargetEvent event(*data, gfx::PointF(10, 10), gfx::PointF(10, 10),
                            ui::DragDropTypes::DRAG_MOVE);
  header->OnDragEntered(event);
  EXPECT_TRUE(header->is_drop_target_for_testing());
  header->OnDragExited();
  EXPECT_FALSE(header->is_drop_target_for_testing());
}

// ---------------------------------------------------------------------------
// Double-click renaming, and the press it shares with the drag
// ---------------------------------------------------------------------------

// R2.4. Click 1 activates the row, which is the row being renamed, so the two
// do not fight; that is what made this safe here and not on a folder header,
// where click 1 had already collapsed the folder.
TEST_F(SidebarDragTest, DoubleClickingARowRenamesIt) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  MakePinned();
  Refresh();
  TabRowView* row = RowIn(pinned_, 0);
  ASSERT_TRUE(row);
  const EntryId id = row->row().entry_id;

  generator().MoveMouseTo(row->GetBoundsInScreen().CenterPoint());
  generator().DoubleClickLeftButton();

  EXPECT_TRUE(row->is_renaming());
  EXPECT_EQ(id, row->renaming_entry_id());
}

// A Today tab has nothing to carry a name past its tab's life.
TEST_F(SidebarDragTest, DoubleClickingARowWithNoEntryDoesNotRenameIt) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kToday, true);
  model_.AddTab(u"Two", "https://two.example/", SidebarSection::kToday, false);
  MakeToday();
  Refresh();
  TabRowView* two = RowIn(today_, 1);
  ASSERT_TRUE(two);
  ASSERT_FALSE(two->row().is_active);

  generator().MoveMouseTo(two->GetBoundsInScreen().CenterPoint());
  generator().DoubleClickLeftButton();

  EXPECT_FALSE(two->is_renaming());
  // Both clicks still reached the row: without this the test would pass
  // identically if the gesture had swallowed them and left a dead row.
  std::optional<SidebarRow> row = RowNamed(u"Two");
  ASSERT_TRUE(row);
  EXPECT_TRUE(row->is_active);
}

// The press that opens the rename is also the press a drag would start from,
// and Views' threshold is the only thing between them. The rename wins for
// that press, so a hand that moves while double-clicking does not tear the
// row out of its section.
TEST_F(SidebarDragTest, TheDoubleClicksPressDoesNotAlsoStartADrag) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  MakePinned();
  Refresh();
  TabRowView* row = RowIn(pinned_, 0);
  ASSERT_TRUE(row);
  // Well past the threshold, which without the gesture is a drag.
  const gfx::Point press(10, 10);
  const gfx::Point moved(10, 60);
  EXPECT_TRUE(row->CanStartDragForView(row, press, moved));

  generator().MoveMouseTo(row->GetBoundsInScreen().CenterPoint());
  generator().DoubleClickLeftButton();
  ASSERT_TRUE(row->is_renaming());

  EXPECT_FALSE(row->CanStartDragForView(row, press, moved));
  // And the field owns the row for as long as it is up.
  EXPECT_EQ(ui::DragDropTypes::DRAG_NONE,
            row->GetDragOperationsForView(row, press));
}

// View::ProcessMousePressed records `possible_drag` from GetDragOperations,
// which it computes *before* this row's OnMousePressed opens the rename. So a
// hand that moves during the double-click still reaches ProcessMouseDragged,
// CanStartDragForView refuses it, and the else branch hands the move to
// Button::OnMouseDragged, which paints the row pressed. The release that
// follows is the one OnMouseReleased returns early from, so nothing else will
// ever put the row back — and it sits under an open rename field, which is
// where it is most visible.
TEST_F(SidebarDragTest, ADoubleClickThatDragsDoesNotLeaveTheRowPressed) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  MakePinned();
  Refresh();
  TabRowView* row = RowIn(pinned_, 0);
  ASSERT_TRUE(row);
  const gfx::Rect bounds = row->GetBoundsInScreen();

  generator().MoveMouseTo(bounds.CenterPoint());
  generator().ClickLeftButton();
  generator().set_flags(ui::EF_IS_DOUBLE_CLICK);
  generator().PressLeftButton();
  // Only the double-click bit goes: the left button is still down, and
  // EventGenerator reads that flag to decide a move is a drag.
  generator().set_flags(ui::EF_LEFT_MOUSE_BUTTON);
  ASSERT_TRUE(row->is_renaming());

  // Well past the drag threshold but still inside the row, which is what puts
  // Button::OnMouseDragged into STATE_PRESSED rather than STATE_NORMAL.
  generator().MoveMouseTo(bounds.CenterPoint() + gfx::Vector2d(60, 0));
  ASSERT_EQ(views::Button::STATE_PRESSED, row->GetState());
  generator().ReleaseLeftButton();

  // The pointer is still over the row, so this is where a release that had
  // gone to the button would have left it.
  EXPECT_EQ(views::Button::STATE_HOVERED, row->GetState());
  // And the rename the double-click opened is still open: clearing the paint
  // must not clear the edit.
  EXPECT_TRUE(row->is_renaming());
}

TEST_F(SidebarDragTest, ADragDoesNotStartInsideTheThreshold) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  MakePinned();
  Refresh();
  TabRowView* row = RowIn(pinned_, 0);
  ASSERT_TRUE(row);

  EXPECT_FALSE(
      row->CanStartDragForView(row, gfx::Point(10, 10), gfx::Point(10, 11)));
  EXPECT_TRUE(
      row->CanStartDragForView(row, gfx::Point(10, 10), gfx::Point(10, 40)));
}

}  // namespace
}  // namespace arcium
