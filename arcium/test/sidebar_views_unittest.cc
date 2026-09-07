// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <vector>

#include "arcium/ui/playground/fake_sidebar_model.h"
#include "arcium/ui/sidebar/folder_header_view.h"
#include "arcium/ui/sidebar/rename_field.h"
#include "arcium/ui/sidebar/row_context_menu.h"
#include "arcium/ui/sidebar/sidebar_model.h"
#include "arcium/ui/sidebar/tab_list_view.h"
#include "arcium/ui/sidebar/tab_row_view.h"
#include "base/functional/callback_helpers.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/test/views_test_utils.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace arcium {
namespace {

// The view half of Task 7, driven through the same fake the playground runs
// on. What is checked here is the part a snapshot cannot show: the order the
// list puts its children in, and what a rename does when it ends.
class SidebarViewsTest : public views::ViewsTestBase {
 public:
  void SetUp() override {
    views::ViewsTestBase::SetUp();
    widget_ = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
    contents_ = widget_->SetContentsView(std::make_unique<views::View>());
    contents_->SetLayoutManager(std::make_unique<views::FillLayout>());
    widget_->SetBounds(gfx::Rect(0, 0, 250, 600));
    widget_->Show();
  }

  void TearDown() override {
    list_ = nullptr;
    contents_ = nullptr;
    widget_.reset();
    views::ViewsTestBase::TearDown();
  }

 protected:
  TabListView* MakeList(SidebarSection section) {
    list_ = contents_->AddChildView(
        std::make_unique<TabListView>(&model_, section));
    return list_;
  }

  void Refresh() { list_->SetRows(model_.rows()); }

  // The children in laid-out order, as class names, which is what the plan
  // decides and what BoxLayout then paints.
  std::vector<std::string> ChildClasses() const {
    std::vector<std::string> names;
    for (const views::View* child : list_->children()) {
      names.push_back(std::string(child->GetClassName()));
    }
    return names;
  }

  RenameField* FocusedField() {
    return views::AsViewClass<RenameField>(
        widget_->GetFocusManager()->GetFocusedView());
  }

  // Straight at the controller the field installs on itself, because
  // views::Textfield keeps OnKeyPressed private.
  void PressKey(RenameField* field, ui::KeyboardCode code) {
    ui::KeyEvent event(ui::EventType::kKeyPressed, code, ui::EF_NONE);
    field->HandleKeyEvent(field, event);
  }

  void Hover(views::View* view) {
    ui::MouseEvent entered(ui::EventType::kMouseEntered, gfx::Point(),
                           gfx::Point(), base::TimeTicks(), 0, 0);
    view->OnMouseEntered(entered);
  }

  // Presses on `row` and drags `dy` pixels down it, which is what the list
  // turns into a MoveTab. Straight at the view's own handlers: the drag is
  // resolved from the event's location inside the row, not from the window.
  void DragRowBy(TabRowView* row, int dy) {
    views::test::RunScheduledLayout(widget_.get());
    const gfx::Point start(10, 10);
    ui::MouseEvent press(ui::EventType::kMousePressed, start, start,
                         base::TimeTicks(), ui::EF_LEFT_MOUSE_BUTTON,
                         ui::EF_LEFT_MOUSE_BUTTON);
    row->OnMousePressed(press);
    const gfx::Point moved(10, 10 + dy);
    ui::MouseEvent drag(ui::EventType::kMouseDragged, moved, moved,
                        base::TimeTicks(), ui::EF_LEFT_MOUSE_BUTTON, 0);
    row->OnMouseDragged(drag);
    ui::MouseEvent release(ui::EventType::kMouseReleased, moved, moved,
                           base::TimeTicks(), ui::EF_LEFT_MOUSE_BUTTON, 0);
    row->OnMouseReleased(release);
  }

  // The labels of a built menu, top to bottom, with disabled ones marked.
  std::vector<std::u16string> MenuLabels(ui::SimpleMenuModel* menu) {
    std::vector<std::u16string> labels;
    for (size_t i = 0; i < menu->GetItemCount(); ++i) {
      if (menu->GetTypeAt(i) == ui::MenuModel::TYPE_SEPARATOR) {
        continue;
      }
      labels.push_back(menu->GetLabelAt(i) +
                       (menu->IsEnabledAt(i) ? u"" : u" [disabled]"));
    }
    return labels;
  }

  FakeSidebarModel model_;
  std::unique_ptr<views::Widget> widget_;
  raw_ptr<views::View> contents_ = nullptr;
  raw_ptr<TabListView> list_ = nullptr;
};

TEST_F(SidebarViewsTest, AFolderHeaderComesBeforeItsEntries) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  model_.AddTab(u"Two", "https://two.example/", SidebarSection::kPinned, false);
  MakeList(SidebarSection::kPinned);
  model_.AddFolderWith(u"Work", {u"One"});
  Refresh();

  // Header, its one entry, then the entry that is in no folder.
  EXPECT_EQ((std::vector<std::string>{"FolderHeaderView", "TabRowView",
                                      "TabRowView"}),
            ChildClasses());
  EXPECT_EQ(1u, list_->folder_count());
  EXPECT_EQ(2u, list_->row_count());
}

TEST_F(SidebarViewsTest, ACollapsedFolderContributesOnlyItsHeader) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  model_.AddTab(u"Two", "https://two.example/", SidebarSection::kPinned, false);
  MakeList(SidebarSection::kPinned);
  const FolderId folder = model_.AddFolderWith(u"Work", {u"One"});
  model_.SetFolderCollapsed(folder, true);
  Refresh();

  EXPECT_EQ((std::vector<std::string>{"FolderHeaderView", "TabRowView"}),
            ChildClasses());
  // The row inside it is not built at all, so a big collapsed folder costs
  // one view rather than one per entry.
  EXPECT_EQ(1u, list_->row_count());
}

// The rows are laid out folder-first, so the first row view can hold a larger
// tab index than the last one. Clamping to the ends rather than the extremes
// hands std::clamp lo > hi, which libc++ hardening turns into an abort.
TEST_F(SidebarViewsTest, DraggingWorksWhenAFolderHoldsALaterEntry) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  model_.AddTab(u"Two", "https://two.example/", SidebarSection::kPinned, false);
  model_.AddTab(u"Three", "https://three.example/", SidebarSection::kPinned,
                false);
  MakeList(SidebarSection::kPinned);
  // The folder holds the *last* entry in model order, so the plan puts tab
  // index 2 first and tab index 1 last.
  model_.AddFolderWith(u"Work", {u"Three"});
  Refresh();

  TabRowView* first = views::AsViewClass<TabRowView>(list_->children()[1]);
  ASSERT_TRUE(first);
  EXPECT_EQ(2, first->tab_index());
  TabRowView* one = views::AsViewClass<TabRowView>(list_->children()[2]);
  ASSERT_TRUE(one);
  EXPECT_EQ(0, one->tab_index());

  // One row down: 0 -> 1, inside the section's real range of 0..2.
  DragRowBy(one, 40);

  ASSERT_EQ(3u, model_.rows().size());
  EXPECT_EQ(u"Two", model_.rows()[0].title);
  EXPECT_EQ(u"One", model_.rows()[1].title);
}

// The shape that could invert the bounds before folders existed: a cold row
// has no tab index, so the last row's -1 is below the first row's 0.
TEST_F(SidebarViewsTest, DraggingWorksWhenAColdRowIsLast) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  model_.AddTab(u"Two", "https://two.example/", SidebarSection::kPinned, false);
  model_.AddColdEntry(u"Cold", "https://cold.example/",
                      SidebarSection::kPinned);
  MakeList(SidebarSection::kPinned);
  Refresh();

  TabRowView* one = views::AsViewClass<TabRowView>(list_->children()[0]);
  ASSERT_TRUE(one);
  ASSERT_EQ(-1,
            views::AsViewClass<TabRowView>(list_->children()[2])->tab_index());
  DragRowBy(one, 40);

  EXPECT_EQ(u"Two", model_.rows()[0].title);
  EXPECT_EQ(u"One", model_.rows()[1].title);
}

// Nothing in the section has a tab index, so there is no range to clamp to.
// This is the bail branch rather than a bounds inversion: it guards the
// std::minmax_element call against an empty range.
TEST_F(SidebarViewsTest, DraggingAColdOnlySectionMovesNothing) {
  model_.AddColdEntry(u"Cold", "https://cold.example/",
                      SidebarSection::kPinned);
  model_.AddColdEntry(u"Colder", "https://colder.example/",
                      SidebarSection::kPinned);
  MakeList(SidebarSection::kPinned);
  Refresh();

  TabRowView* row = views::AsViewClass<TabRowView>(list_->children()[0]);
  ASSERT_TRUE(row);
  DragRowBy(row, 40);

  EXPECT_EQ(u"Cold", model_.rows()[0].title);
  EXPECT_EQ(u"Colder", model_.rows()[1].title);
}

TEST_F(SidebarViewsTest, TheTodaySectionHasNoFolders) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kToday, true);
  MakeList(SidebarSection::kToday);
  Refresh();
  EXPECT_EQ(0u, list_->folder_count());
}

// A Today tab has no entry, so there is nothing to carry a name past the
// tab's life and no rename is offered.
TEST_F(SidebarViewsTest, ARowWithNoEntryCannotBeRenamed) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kToday, true);
  MakeList(SidebarSection::kToday);
  Refresh();

  TabRowView* row = views::AsViewClass<TabRowView>(list_->children()[0]);
  ASSERT_TRUE(row);
  row->BeginRename();
  EXPECT_FALSE(row->is_renaming());
}

TEST_F(SidebarViewsTest, EnterCommitsARenameThroughTheModel) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  MakeList(SidebarSection::kPinned);
  Refresh();

  TabRowView* row = views::AsViewClass<TabRowView>(list_->children()[0]);
  ASSERT_TRUE(row);
  row->BeginRename();
  ASSERT_TRUE(row->is_renaming());

  RenameField* field = FocusedField();
  ASSERT_TRUE(field);
  // The field starts on the current title, selected, so typing replaces it.
  EXPECT_EQ(u"One", field->GetText());
  field->SetText(u"Renamed");
  PressKey(field, ui::VKEY_RETURN);
  // The finish is posted: it deletes the field, which must not happen inside
  // the key event the field is still on the stack of.
  task_environment()->RunUntilIdle();

  EXPECT_FALSE(row->is_renaming());
  ASSERT_EQ(1u, model_.rows().size());
  EXPECT_EQ(u"Renamed", model_.rows()[0].title);
}

TEST_F(SidebarViewsTest, EscapeAbandonsARename) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  MakeList(SidebarSection::kPinned);
  Refresh();

  TabRowView* row = views::AsViewClass<TabRowView>(list_->children()[0]);
  ASSERT_TRUE(row);
  row->BeginRename();
  RenameField* field = FocusedField();
  ASSERT_TRUE(field);
  field->SetText(u"Discarded");
  PressKey(field, ui::VKEY_ESCAPE);
  task_environment()->RunUntilIdle();

  EXPECT_FALSE(row->is_renaming());
  EXPECT_EQ(u"One", model_.rows()[0].title);
}

TEST_F(SidebarViewsTest, RenamingAFolderHeaderCommitsThroughTheModel) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  MakeList(SidebarSection::kPinned);
  model_.AddFolderWith(u"Work", {u"One"});
  Refresh();

  FolderHeaderView* header =
      views::AsViewClass<FolderHeaderView>(list_->children()[0]);
  ASSERT_TRUE(header);
  header->BeginRename();
  RenameField* field = FocusedField();
  ASSERT_TRUE(field);
  field->SetText(u"Renamed");
  PressKey(field, ui::VKEY_RETURN);
  task_environment()->RunUntilIdle();

  ASSERT_EQ(1u, model_.folders().size());
  EXPECT_EQ(u"Renamed", model_.folders()[0].name);
}

// An empty name is not a name; the folder keeps the one it had.
TEST_F(SidebarViewsTest, AnEmptyRenameIsNotCommitted) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  MakeList(SidebarSection::kPinned);
  Refresh();

  TabRowView* row = views::AsViewClass<TabRowView>(list_->children()[0]);
  row->BeginRename();
  RenameField* field = FocusedField();
  ASSERT_TRUE(field);
  field->SetText(std::u16string());
  PressKey(field, ui::VKEY_RETURN);
  task_environment()->RunUntilIdle();

  EXPECT_EQ(u"One", model_.rows()[0].title);
}

// Task 7's table: what each section offers, in order.
TEST_F(SidebarViewsTest, TheMenuForATodayRow) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kToday, true);
  RowContextMenu menu(&model_);
  menu.BuildForRow(model_.rows()[0], base::DoNothing());
  EXPECT_EQ((std::vector<std::u16string>{u"Pin", u"Add to Favorites",
                                         u"Rename [disabled]", u"Close"}),
            MenuLabels(menu.menu()));
}

TEST_F(SidebarViewsTest, TheMenuForAPinnedRowThatHasNavigatedAway) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  model_.SetCanReturnToPinnedUrl(0, true);
  RowContextMenu menu(&model_);
  menu.BuildForRow(model_.rows()[0], base::DoNothing());
  EXPECT_EQ((std::vector<std::u16string>{u"Rename", u"Return to pinned URL",
                                         u"New folder", u"Move to folder",
                                         u"Unpin", u"Close tab"}),
            MenuLabels(menu.menu()));
}

// The return is offered only when there is somewhere to return from.
TEST_F(SidebarViewsTest, APinnedRowOnItsPinnedUrlIsNotOfferedTheReturn) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  RowContextMenu menu(&model_);
  menu.BuildForRow(model_.rows()[0], base::DoNothing());
  EXPECT_EQ(
      (std::vector<std::u16string>{u"Rename", u"New folder", u"Move to folder",
                                   u"Unpin", u"Close tab"}),
      MenuLabels(menu.menu()));
}

TEST_F(SidebarViewsTest, TheMenuForAFavourite) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kFavorites,
                false);
  RowContextMenu menu(&model_);
  menu.BuildForRow(model_.rows()[0], base::DoNothing());
  EXPECT_EQ((std::vector<std::u16string>{u"Rename", u"Remove from Favorites",
                                         u"Close tab"}),
            MenuLabels(menu.menu()));
}

// "Delete folder" says what it does, because ui::MenuModel has no tooltip to
// say it in and the bare wording reads as deleting the tabs.
TEST_F(SidebarViewsTest, TheMenuForAFolderHeaderSaysTheTabsStay) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  model_.AddFolderWith(u"Work", {u"One"});
  RowContextMenu menu(&model_);
  menu.BuildForFolder(model_.folders()[0], base::DoNothing());
  EXPECT_EQ((std::vector<std::u16string>{u"Rename",
                                         u"Delete folder (keeps its tabs)"}),
            MenuLabels(menu.menu()));
}

TEST_F(SidebarViewsTest, ColdRowsCannotCloseATabTheyDoNotHave) {
  model_.AddColdEntry(u"Cold", "https://cold.example/",
                      SidebarSection::kPinned);
  RowContextMenu menu(&model_);
  menu.BuildForRow(model_.rows()[0], base::DoNothing());
  EXPECT_EQ(
      (std::vector<std::u16string>{u"Rename", u"New folder", u"Move to folder",
                                   u"Unpin", u"Close tab [disabled]"}),
      MenuLabels(menu.menu()));
}

// The hover slot: a pinned row that has navigated away offers the way back
// where the close button would otherwise be.
TEST_F(SidebarViewsTest, TheRevertButtonTakesTheCloseButtonsSlot) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  MakeList(SidebarSection::kPinned);
  model_.SetCanReturnToPinnedUrl(0, true);
  Refresh();

  TabRowView* row = views::AsViewClass<TabRowView>(list_->children()[0]);
  ASSERT_TRUE(row);
  Hover(row);
  EXPECT_TRUE(row->GetViewByID(TabRowView::kRevertButtonId)->GetVisible());
  EXPECT_FALSE(row->GetViewByID(TabRowView::kCloseButtonId)->GetVisible());
}

TEST_F(SidebarViewsTest, AnUnmovedPinnedRowKeepsItsCloseButton) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  MakeList(SidebarSection::kPinned);
  Refresh();

  TabRowView* row = views::AsViewClass<TabRowView>(list_->children()[0]);
  Hover(row);
  EXPECT_FALSE(row->GetViewByID(TabRowView::kRevertButtonId)->GetVisible());
  EXPECT_TRUE(row->GetViewByID(TabRowView::kCloseButtonId)->GetVisible());
}

TEST_F(SidebarViewsTest, ClickingRevertIssuesTheCommand) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  MakeList(SidebarSection::kPinned);
  model_.SetCanReturnToPinnedUrl(0, true);
  Refresh();

  TabRowView* row = views::AsViewClass<TabRowView>(list_->children()[0]);
  Hover(row);
  auto* button = views::AsViewClass<views::ImageButton>(
      row->GetViewByID(TabRowView::kRevertButtonId));
  ASSERT_TRUE(button);
  ui::MouseEvent click(ui::EventType::kMousePressed, gfx::Point(), gfx::Point(),
                       base::TimeTicks(), ui::EF_LEFT_MOUSE_BUTTON, 0);
  views::test::ButtonTestApi(button).NotifyClick(click);

  ASSERT_EQ(1u, model_.rows().size());
  EXPECT_FALSE(model_.rows()[0].can_return_to_pinned_url);
}

}  // namespace
}  // namespace arcium
