// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <vector>

#include "arcium/ui/playground/fake_sidebar_model.h"
#include "arcium/ui/sidebar/favorites_grid_view.h"
#include "arcium/ui/sidebar/folder_header_view.h"
#include "arcium/ui/sidebar/rename_field.h"
#include "arcium/ui/sidebar/row_context_menu.h"
#include "arcium/ui/sidebar/sidebar_model.h"
#include "arcium/ui/sidebar/sidebar_view.h"
#include "arcium/ui/sidebar/tab_list_view.h"
#include "arcium/ui/sidebar/tab_row_view.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/events/event.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/events/test/event_generator.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/test/views_test_utils.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_utils.h"

namespace arcium {
namespace {

// Keeps whatever RowContextMenu a right-click builds, instead of letting it
// spin a nested native menu loop that a unit test can never get out of. What
// the hook hands over is the real menu, built by the real path.
class ScopedMenuCapture {
 public:
  ScopedMenuCapture() {
    RowContextMenu::SetShowHookForTesting(base::BindRepeating(
        [](ScopedMenuCapture* self, RowContextMenu* menu) {
          self->menu_ = menu;
          ++self->count_;
        },
        base::Unretained(this)));
  }
  ScopedMenuCapture(const ScopedMenuCapture&) = delete;
  ScopedMenuCapture& operator=(const ScopedMenuCapture&) = delete;
  ~ScopedMenuCapture() {
    RowContextMenu::SetShowHookForTesting(RowContextMenu::ShowHookForTesting());
  }

  RowContextMenu* menu() { return menu_; }
  int count() const { return count_; }

 private:
  raw_ptr<RowContextMenu> menu_ = nullptr;
  int count_ = 0;
};

// Rebuilds the list on every model change, the way SidebarView does. Without
// one, a view test never sees the rebuild a command causes, and the rule that
// a rename must finish from a posted task is never put under load.
class RebuildOnChange : public SidebarModel::Observer {
 public:
  RebuildOnChange(SidebarModel* model, TabListView* list)
      : model_(model), list_(list) {
    model_->AddObserver(this);
  }
  ~RebuildOnChange() override { model_->RemoveObserver(this); }

  void OnSidebarModelChanged() override {
    ++changes_;
    list_->SetRows(model_->rows());
  }

  int changes() const { return changes_; }

 private:
  raw_ptr<SidebarModel> model_;
  raw_ptr<TabListView> list_;
  int changes_ = 0;
};

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
    widget_->Activate();
    generator_ = std::make_unique<ui::test::EventGenerator>(
        views::GetRootWindow(widget_.get()));
  }

  void TearDown() override {
    generator_.reset();
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

  ui::test::EventGenerator& generator() { return *generator_; }

  // Real events at the real view, so what is under test is the routing as
  // much as the handler.
  void ClickOn(views::View* view) {
    views::test::RunScheduledLayout(widget_.get());
    generator_->MoveMouseTo(view->GetBoundsInScreen().CenterPoint());
    generator_->ClickLeftButton();
  }

  void RightClickOn(views::View* view) {
    views::test::RunScheduledLayout(widget_.get());
    generator_->MoveMouseTo(view->GetBoundsInScreen().CenterPoint());
    generator_->ClickRightButton();
  }

  // The leading edge, where the favicon or the disclosure triangle is. Used
  // where the middle of the row is covered by an open rename field, which is
  // a views::Textfield and would answer the right-click with its own editing
  // menu — a real nested menu loop, in a unit test.
  void RightClickNearLeadingEdgeOf(views::View* view) {
    views::test::RunScheduledLayout(widget_.get());
    const gfx::Rect bounds = view->GetBoundsInScreen();
    generator_->MoveMouseTo(
        gfx::Point(bounds.x() + 4, bounds.y() + bounds.height() / 2));
    generator_->ClickRightButton();
  }

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
  std::vector<std::u16string> MenuLabels(ui::MenuModel* menu) {
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

  ui::MenuModel* Submenu(ui::MenuModel* menu, const std::u16string& label) {
    for (size_t i = 0; i < menu->GetItemCount(); ++i) {
      if (menu->GetLabelAt(i) == label) {
        return menu->GetSubmenuModelAt(i);
      }
    }
    return nullptr;
  }

  // Runs the item by the name the user reads, through the menu model, so the
  // command id the item carries is part of what is under test.
  [[nodiscard]] bool Choose(ui::MenuModel* menu, const std::u16string& label) {
    for (size_t i = 0; i < menu->GetItemCount(); ++i) {
      if (menu->GetLabelAt(i) == label) {
        if (!menu->IsEnabledAt(i)) {
          return false;
        }
        menu->ActivatedAt(i);
        return true;
      }
    }
    return false;
  }

  FakeSidebarModel model_;
  std::unique_ptr<views::Widget> widget_;
  std::unique_ptr<ui::test::EventGenerator> generator_;
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

// Enter goes in as a real key, so what is under test includes views::Textfield
// routing it to the field's controller — and the list is wired to the model
// through an observer, so the commit really does rebuild the view underneath
// the field the way it does in the browser.
TEST_F(SidebarViewsTest, EnterCommitsARenameThroughTheModel) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  MakeList(SidebarSection::kPinned);
  Refresh();
  RebuildOnChange rebuilder(&model_, list_);

  TabRowView* row = views::AsViewClass<TabRowView>(list_->children()[0]);
  ASSERT_TRUE(row);
  row->BeginRename();
  ASSERT_TRUE(row->is_renaming());

  RenameField* field = FocusedField();
  ASSERT_TRUE(field);
  // The field starts on the current title, selected, so typing replaces it.
  EXPECT_EQ(u"One", field->GetText());
  field->SetText(u"Renamed");
  generator().PressAndReleaseKey(ui::VKEY_RETURN, ui::EF_NONE);
  // The finish is posted: it deletes the field, which must not happen inside
  // the key event the field is still on the stack of.
  task_environment()->RunUntilIdle();

  EXPECT_FALSE(row->is_renaming());
  ASSERT_EQ(1u, model_.rows().size());
  EXPECT_EQ(u"Renamed", model_.rows()[0].title);
  // The commit went through the model, so the list was rebuilt by it.
  EXPECT_GE(rebuilder.changes(), 1);
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
  generator().PressAndReleaseKey(ui::VKEY_ESCAPE, ui::EF_NONE);
  task_environment()->RunUntilIdle();

  EXPECT_FALSE(row->is_renaming());
  EXPECT_EQ(u"One", model_.rows()[0].title);
}

// I1: the field belongs to the entry the edit was started on, not to the view
// slot it is sitting in. Another window on the same profile moves the shared
// model, the pool re-points this slot at a different entry, and the commit
// still has to land where the user was typing.
TEST_F(SidebarViewsTest, ARenameCommitsAgainstTheEntryItStartedOn) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  model_.AddTab(u"Two", "https://two.example/", SidebarSection::kPinned, false);
  MakeList(SidebarSection::kPinned);
  Refresh();

  TabRowView* second = views::AsViewClass<TabRowView>(list_->children()[1]);
  ASSERT_TRUE(second);
  ASSERT_EQ(u"Two", second->row().title);
  second->BeginRename();
  RenameField* field = FocusedField();
  ASSERT_TRUE(field);
  field->SetText(u"Renamed");
  generator().PressAndReleaseKey(ui::VKEY_RETURN, ui::EF_NONE);

  // Between the Enter and the posted finish, the model moves: "Two" goes into
  // a folder, so the plan draws it first and this slot now holds "One".
  model_.AddFolderWith(u"Work", {u"Two"});
  Refresh();
  ASSERT_EQ(u"One", second->row().title);
  task_environment()->RunUntilIdle();

  ASSERT_EQ(2u, model_.rows().size());
  EXPECT_EQ(u"One", model_.rows()[0].title);
  EXPECT_EQ(u"Renamed", model_.rows()[1].title);
}

// The other half of I1: the field must not hover over a row it no longer
// belongs to.
TEST_F(SidebarViewsTest, RepointingARowsSlotAbandonsItsOpenRename) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  model_.AddTab(u"Two", "https://two.example/", SidebarSection::kPinned, false);
  MakeList(SidebarSection::kPinned);
  Refresh();

  TabRowView* second = views::AsViewClass<TabRowView>(list_->children()[1]);
  ASSERT_TRUE(second);
  second->BeginRename();
  ASSERT_TRUE(second->is_renaming());
  RenameField* field = FocusedField();
  ASSERT_TRUE(field);
  field->SetText(u"Renamed");

  model_.AddFolderWith(u"Work", {u"Two"});
  Refresh();
  task_environment()->RunUntilIdle();

  EXPECT_FALSE(second->is_renaming());
  // Abandoned, so nothing was written anywhere.
  EXPECT_EQ(u"One", model_.rows()[0].title);
  EXPECT_EQ(u"Two", model_.rows()[1].title);
}

// SetFolder has the identical shape: headers are pooled by position too.
TEST_F(SidebarViewsTest, RepointingAHeadersSlotAbandonsItsOpenRename) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  model_.AddTab(u"Two", "https://two.example/", SidebarSection::kPinned, false);
  MakeList(SidebarSection::kPinned);
  const FolderId work = model_.AddFolderWith(u"Work", {u"One"});
  const FolderId reading = model_.AddFolderWith(u"Reading", {u"Two"});
  Refresh();

  FolderHeaderView* first =
      views::AsViewClass<FolderHeaderView>(list_->children()[0]);
  ASSERT_TRUE(first);
  ASSERT_EQ(work, first->folder().id);
  first->BeginRename();
  ASSERT_TRUE(first->is_renaming());
  FocusedField()->SetText(u"Renamed");

  // The folders swap places, so the first header now draws the other folder.
  model_.SetFolderPosition(work, 1);
  model_.SetFolderPosition(reading, 0);
  Refresh();
  task_environment()->RunUntilIdle();

  EXPECT_EQ(reading, first->folder().id);
  EXPECT_FALSE(first->is_renaming());
  ASSERT_EQ(2u, model_.folders().size());
  EXPECT_EQ(u"Reading", model_.folders()[0].name);
  EXPECT_EQ(u"Work", model_.folders()[1].name);
}

// Losing the focus because the whole window went away is not the user ending
// the edit; Cmd+Tab must not eat a half-typed name.
TEST_F(SidebarViewsTest, DeactivatingTheWindowLeavesTheRenameOpen) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  MakeList(SidebarSection::kPinned);
  Refresh();

  TabRowView* row = views::AsViewClass<TabRowView>(list_->children()[0]);
  ASSERT_TRUE(row);
  row->BeginRename();
  RenameField* field = FocusedField();
  ASSERT_TRUE(field);
  field->SetText(u"Half typed");

  std::unique_ptr<views::Widget> other =
      CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  other->Show();
  other->Activate();
  ASSERT_FALSE(widget_->IsActive());
  // Whichever route the platform takes to the blur, this is where it lands.
  field->OnBlur();
  task_environment()->RunUntilIdle();

  EXPECT_TRUE(row->is_renaming());
  EXPECT_EQ(u"Half typed", field->GetText());
  other.reset();
}

// The other direction, so the pair is not vacuous: inside a live window a
// blur is a click elsewhere, and that still abandons.
TEST_F(SidebarViewsTest, BlurInsideAnActiveWindowAbandonsTheRename) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  MakeList(SidebarSection::kPinned);
  Refresh();

  TabRowView* row = views::AsViewClass<TabRowView>(list_->children()[0]);
  ASSERT_TRUE(row);
  row->BeginRename();
  RenameField* field = FocusedField();
  ASSERT_TRUE(field);
  field->SetText(u"Discarded");
  ASSERT_TRUE(widget_->IsActive());
  field->OnBlur();
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

// Folders come back in position order, and the list lays their headers out in
// the order it is given. Positions that disagree with the order the folders
// were made in are what make this test say anything.
TEST_F(SidebarViewsTest, FolderHeadersAreLaidOutInPositionOrder) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  model_.AddTab(u"Two", "https://two.example/", SidebarSection::kPinned, false);
  MakeList(SidebarSection::kPinned);
  const FolderId work = model_.AddFolderWith(u"Work", {u"One"});
  const FolderId reading = model_.AddFolderWith(u"Reading", {u"Two"});
  model_.SetFolderPosition(work, 1);
  model_.SetFolderPosition(reading, 0);
  Refresh();

  ASSERT_EQ(2u, list_->folder_count());
  EXPECT_EQ(u"Reading",
            views::AsViewClass<FolderHeaderView>(list_->children()[0])
                ->folder()
                .name);
  EXPECT_EQ(u"Work", views::AsViewClass<FolderHeaderView>(list_->children()[2])
                         ->folder()
                         .name);
}

// A single click collapses, straight away. The gesture is not delayed to wait
// for a possible second click, so the common action never feels laggy.
TEST_F(SidebarViewsTest, ClickingAFolderHeaderCollapsesIt) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  MakeList(SidebarSection::kPinned);
  model_.AddFolderWith(u"Work", {u"One"});
  Refresh();
  RebuildOnChange rebuilder(&model_, list_);

  FolderHeaderView* header =
      views::AsViewClass<FolderHeaderView>(list_->children()[0]);
  ASSERT_TRUE(header);
  ASSERT_FALSE(model_.folders()[0].collapsed);
  ClickOn(header);

  EXPECT_TRUE(model_.folders()[0].collapsed);
  // Collapsed, so the entry inside it is not built at all.
  EXPECT_EQ(0u, list_->row_count());
}

// The double click no longer renames: click 1 has already collapsed, and a
// toggle back would be swallowed by the model's unchanged-value early return.
// Rename is the context menu's item and F2.
TEST_F(SidebarViewsTest, DoubleClickingAFolderHeaderDoesNotRenameIt) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  MakeList(SidebarSection::kPinned);
  model_.AddFolderWith(u"Work", {u"One"});
  Refresh();

  FolderHeaderView* header =
      views::AsViewClass<FolderHeaderView>(list_->children()[0]);
  ASSERT_TRUE(header);
  views::test::RunScheduledLayout(widget_.get());
  generator().MoveMouseTo(header->GetBoundsInScreen().CenterPoint());
  generator().DoubleClickLeftButton();

  EXPECT_FALSE(header->is_renaming());
}

TEST_F(SidebarViewsTest, F2RenamesAFolderHeader) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  MakeList(SidebarSection::kPinned);
  model_.AddFolderWith(u"Work", {u"One"});
  Refresh();

  FolderHeaderView* header =
      views::AsViewClass<FolderHeaderView>(list_->children()[0]);
  ASSERT_TRUE(header);
  // Sidebar rows are accessibility-focusable only, so the key arrives at the
  // view rather than through the focus manager.
  ui::KeyEvent f2(ui::EventType::kKeyPressed, ui::VKEY_F2, ui::EF_NONE);
  EXPECT_TRUE(header->OnKeyPressed(f2));
  EXPECT_TRUE(header->is_renaming());

  FocusedField()->SetText(u"Renamed");
  generator().PressAndReleaseKey(ui::VKEY_RETURN, ui::EF_NONE);
  task_environment()->RunUntilIdle();
  ASSERT_EQ(1u, model_.folders().size());
  EXPECT_EQ(u"Renamed", model_.folders()[0].name);
}

// Task 7's table: what each section offers, in order — through a right-click
// on the view, not through the builder underneath it.
TEST_F(SidebarViewsTest, TheMenuForATodayRow) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kToday, true);
  MakeList(SidebarSection::kToday);
  Refresh();

  ScopedMenuCapture capture;
  RightClickOn(views::AsViewClass<TabRowView>(list_->children()[0]));
  ASSERT_TRUE(capture.menu());
  EXPECT_EQ((std::vector<std::u16string>{u"Pin", u"Add to Favorites",
                                         u"Rename [disabled]", u"Close"}),
            MenuLabels(capture.menu()->menu()));
}

TEST_F(SidebarViewsTest, TheMenuForAPinnedRowThatHasNavigatedAway) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  MakeList(SidebarSection::kPinned);
  model_.SetCanReturnToPinnedUrl(0, true);
  Refresh();

  ScopedMenuCapture capture;
  RightClickOn(views::AsViewClass<TabRowView>(list_->children()[0]));
  ASSERT_TRUE(capture.menu());
  EXPECT_EQ((std::vector<std::u16string>{u"Rename", u"Return to pinned URL",
                                         u"New folder", u"Move to folder",
                                         u"Unpin", u"Close tab"}),
            MenuLabels(capture.menu()->menu()));
}

// The return is offered only when there is somewhere to return from.
TEST_F(SidebarViewsTest, APinnedRowOnItsPinnedUrlIsNotOfferedTheReturn) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  MakeList(SidebarSection::kPinned);
  Refresh();

  ScopedMenuCapture capture;
  RightClickOn(views::AsViewClass<TabRowView>(list_->children()[0]));
  ASSERT_TRUE(capture.menu());
  EXPECT_EQ(
      (std::vector<std::u16string>{u"Rename", u"New folder", u"Move to folder",
                                   u"Unpin", u"Close tab"}),
      MenuLabels(capture.menu()->menu()));
}

// A favourite is a tile in the grid, not a row in a list, and the tiles carry
// the same menu. Rename is disabled: a 40px tile has nowhere to put a field.
TEST_F(SidebarViewsTest, TheMenuForAFavouriteTile) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kFavorites,
                false);
  auto* grid =
      contents_->AddChildView(std::make_unique<FavoritesGridView>(&model_));
  grid->SetRows(model_.rows());
  ASSERT_EQ(1u, grid->children().size());

  ScopedMenuCapture capture;
  RightClickOn(grid->children()[0]);
  ASSERT_TRUE(capture.menu());
  EXPECT_EQ((std::vector<std::u16string>{
                u"Rename [disabled]", u"Remove from Favorites", u"Close tab"}),
            MenuLabels(capture.menu()->menu()));
}

TEST_F(SidebarViewsTest, RemoveFromFavoritesFromTheTilesMenu) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kFavorites,
                false);
  auto* grid =
      contents_->AddChildView(std::make_unique<FavoritesGridView>(&model_));
  grid->SetRows(model_.rows());

  ScopedMenuCapture capture;
  RightClickOn(grid->children()[0]);
  ASSERT_TRUE(capture.menu());
  EXPECT_TRUE(Choose(capture.menu()->menu(), u"Remove from Favorites"));

  ASSERT_EQ(1u, model_.rows().size());
  // The entry is gone; the tab falls back into Today.
  EXPECT_EQ(SidebarSection::kToday, model_.rows()[0].section);
}

// "Delete folder" says what it does, because ui::MenuModel has no tooltip to
// say it in and the bare wording reads as deleting the tabs.
TEST_F(SidebarViewsTest, TheMenuForAFolderHeaderSaysTheTabsStay) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  MakeList(SidebarSection::kPinned);
  model_.AddFolderWith(u"Work", {u"One"});
  Refresh();

  ScopedMenuCapture capture;
  RightClickOn(views::AsViewClass<FolderHeaderView>(list_->children()[0]));
  ASSERT_TRUE(capture.menu());
  EXPECT_EQ((std::vector<std::u16string>{u"Rename",
                                         u"Delete folder (keeps its tabs)"}),
            MenuLabels(capture.menu()->menu()));
}

TEST_F(SidebarViewsTest, ColdRowsCannotCloseATabTheyDoNotHave) {
  model_.AddColdEntry(u"Cold", "https://cold.example/",
                      SidebarSection::kPinned);
  MakeList(SidebarSection::kPinned);
  Refresh();

  ScopedMenuCapture capture;
  RightClickOn(views::AsViewClass<TabRowView>(list_->children()[0]));
  ASSERT_TRUE(capture.menu());
  EXPECT_EQ(
      (std::vector<std::u16string>{u"Rename", u"New folder", u"Move to folder",
                                   u"Unpin", u"Close tab [disabled]"}),
      MenuLabels(capture.menu()->menu()));
}

// A field open on the row owns it: a right-click that put a menu over the
// half-typed name would drop the edit on the floor.
TEST_F(SidebarViewsTest, ARenamingRowOffersNoContextMenu) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  MakeList(SidebarSection::kPinned);
  Refresh();

  TabRowView* row = views::AsViewClass<TabRowView>(list_->children()[0]);
  ASSERT_TRUE(row);
  ScopedMenuCapture capture;
  RightClickOn(row);
  ASSERT_EQ(1, capture.count());

  row->BeginRename();
  ASSERT_TRUE(row->is_renaming());
  RightClickNearLeadingEdgeOf(row);
  EXPECT_EQ(1, capture.count());
  EXPECT_TRUE(row->is_renaming());
}

TEST_F(SidebarViewsTest, ARenamingFolderHeaderOffersNoContextMenu) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  MakeList(SidebarSection::kPinned);
  model_.AddFolderWith(u"Work", {u"One"});
  Refresh();

  FolderHeaderView* header =
      views::AsViewClass<FolderHeaderView>(list_->children()[0]);
  ASSERT_TRUE(header);
  ScopedMenuCapture capture;
  RightClickOn(header);
  ASSERT_EQ(1, capture.count());

  header->BeginRename();
  RightClickNearLeadingEdgeOf(header);
  EXPECT_EQ(1, capture.count());
}

// Every command in the table, run the way a click on the item runs it.
TEST_F(SidebarViewsTest, PinFromATodayRowsMenu) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kToday, true);
  MakeList(SidebarSection::kToday);
  Refresh();

  ScopedMenuCapture capture;
  RightClickOn(views::AsViewClass<TabRowView>(list_->children()[0]));
  ASSERT_TRUE(capture.menu());
  EXPECT_TRUE(Choose(capture.menu()->menu(), u"Pin"));
  ASSERT_EQ(1u, model_.rows().size());
  EXPECT_EQ(SidebarSection::kPinned, model_.rows()[0].section);
}

TEST_F(SidebarViewsTest, AddToFavoritesFromATodayRowsMenu) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kToday, true);
  MakeList(SidebarSection::kToday);
  Refresh();

  ScopedMenuCapture capture;
  RightClickOn(views::AsViewClass<TabRowView>(list_->children()[0]));
  ASSERT_TRUE(capture.menu());
  EXPECT_TRUE(Choose(capture.menu()->menu(), u"Add to Favorites"));
  ASSERT_EQ(1u, model_.rows().size());
  EXPECT_EQ(SidebarSection::kFavorites, model_.rows()[0].section);
}

TEST_F(SidebarViewsTest, CloseFromATodayRowsMenu) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kToday, true);
  model_.AddTab(u"Two", "https://two.example/", SidebarSection::kToday, false);
  MakeList(SidebarSection::kToday);
  Refresh();

  ScopedMenuCapture capture;
  RightClickOn(views::AsViewClass<TabRowView>(list_->children()[0]));
  ASSERT_TRUE(capture.menu());
  EXPECT_TRUE(Choose(capture.menu()->menu(), u"Close"));
  ASSERT_EQ(1u, model_.rows().size());
  EXPECT_EQ(u"Two", model_.rows()[0].title);
}

// A pinned entry's tab closes through the entry, so the entry survives cold.
TEST_F(SidebarViewsTest, CloseTabFromAPinnedRowsMenuLeavesTheEntry) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  MakeList(SidebarSection::kPinned);
  Refresh();

  ScopedMenuCapture capture;
  RightClickOn(views::AsViewClass<TabRowView>(list_->children()[0]));
  ASSERT_TRUE(capture.menu());
  EXPECT_TRUE(Choose(capture.menu()->menu(), u"Close tab"));
  ASSERT_EQ(1u, model_.rows().size());
  EXPECT_TRUE(model_.rows()[0].is_cold);
}

TEST_F(SidebarViewsTest, UnpinFromAPinnedRowsMenu) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  MakeList(SidebarSection::kPinned);
  Refresh();

  ScopedMenuCapture capture;
  RightClickOn(views::AsViewClass<TabRowView>(list_->children()[0]));
  ASSERT_TRUE(capture.menu());
  EXPECT_TRUE(Choose(capture.menu()->menu(), u"Unpin"));
  ASSERT_EQ(1u, model_.rows().size());
  EXPECT_EQ(SidebarSection::kToday, model_.rows()[0].section);
}

TEST_F(SidebarViewsTest, ReturnToPinnedUrlFromAPinnedRowsMenu) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  MakeList(SidebarSection::kPinned);
  model_.SetCanReturnToPinnedUrl(0, true);
  Refresh();

  ScopedMenuCapture capture;
  RightClickOn(views::AsViewClass<TabRowView>(list_->children()[0]));
  ASSERT_TRUE(capture.menu());
  EXPECT_TRUE(Choose(capture.menu()->menu(), u"Return to pinned URL"));
  EXPECT_FALSE(model_.rows()[0].can_return_to_pinned_url);
}

TEST_F(SidebarViewsTest, RenameFromAPinnedRowsMenuOpensTheField) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  MakeList(SidebarSection::kPinned);
  Refresh();

  TabRowView* row = views::AsViewClass<TabRowView>(list_->children()[0]);
  ScopedMenuCapture capture;
  RightClickOn(row);
  ASSERT_TRUE(capture.menu());
  EXPECT_TRUE(Choose(capture.menu()->menu(), u"Rename"));
  EXPECT_TRUE(row->is_renaming());
}

TEST_F(SidebarViewsTest, NewFolderFromAPinnedRowsMenu) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  MakeList(SidebarSection::kPinned);
  Refresh();

  ScopedMenuCapture capture;
  RightClickOn(views::AsViewClass<TabRowView>(list_->children()[0]));
  ASSERT_TRUE(capture.menu());
  EXPECT_TRUE(Choose(capture.menu()->menu(), u"New folder"));

  ASSERT_EQ(1u, model_.folders().size());
  EXPECT_EQ(u"New folder", model_.folders()[0].name);
  EXPECT_EQ(1, model_.folders()[0].entry_count);
}

// The submenu is where the parallel-array arithmetic lives: item N after the
// first has to name folder N. Three folders, so it is not just the zeroth.
TEST_F(SidebarViewsTest, TheMoveToFolderSubmenuNamesEveryFolder) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  model_.AddTab(u"Two", "https://two.example/", SidebarSection::kPinned, false);
  model_.AddTab(u"Three", "https://three.example/", SidebarSection::kPinned,
                false);
  model_.AddTab(u"Four", "https://four.example/", SidebarSection::kPinned,
                false);
  MakeList(SidebarSection::kPinned);
  model_.AddFolderWith(u"Alpha", {u"Two"});
  model_.AddFolderWith(u"Beta", {u"Three"});
  model_.AddFolderWith(u"Gamma", {u"Four"});
  Refresh();

  // "One" is at the top level, so every folder is somewhere it could go.
  TabRowView* top_level = nullptr;
  for (views::View* child : list_->children()) {
    TabRowView* row = views::AsViewClass<TabRowView>(child);
    if (row && row->row().title == u"One") {
      top_level = row;
    }
  }
  ASSERT_TRUE(top_level);

  ScopedMenuCapture capture;
  RightClickOn(top_level);
  ASSERT_TRUE(capture.menu());
  ui::MenuModel* submenu = Submenu(capture.menu()->menu(), u"Move to folder");
  ASSERT_TRUE(submenu);
  EXPECT_EQ((std::vector<std::u16string>{u"Top level [disabled]", u"Alpha",
                                         u"Beta", u"Gamma"}),
            MenuLabels(submenu));

  // The third folder, not the first: the id the item carries has to be the
  // one the label names.
  EXPECT_TRUE(Choose(submenu, u"Gamma"));
  ASSERT_EQ(3u, model_.folders().size());
  EXPECT_EQ(u"Gamma", model_.folders()[2].name);
  EXPECT_EQ(2, model_.folders()[2].entry_count);
  EXPECT_EQ(1, model_.folders()[0].entry_count);
  EXPECT_EQ(1, model_.folders()[1].entry_count);
}

// Symmetry with "Top level": the folder a row is already in is not somewhere
// to move it.
TEST_F(SidebarViewsTest, TheMoveToFolderSubmenuDisablesTheCurrentFolder) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  model_.AddTab(u"Two", "https://two.example/", SidebarSection::kPinned, false);
  MakeList(SidebarSection::kPinned);
  model_.AddFolderWith(u"Alpha", {u"One"});
  model_.AddFolderWith(u"Beta", {u"Two"});
  Refresh();

  // children: Alpha header, One, Beta header, Two.
  TabRowView* in_alpha = views::AsViewClass<TabRowView>(list_->children()[1]);
  ASSERT_TRUE(in_alpha);
  ASSERT_EQ(u"One", in_alpha->row().title);

  ScopedMenuCapture capture;
  RightClickOn(in_alpha);
  ASSERT_TRUE(capture.menu());
  ui::MenuModel* submenu = Submenu(capture.menu()->menu(), u"Move to folder");
  ASSERT_TRUE(submenu);
  EXPECT_EQ(
      (std::vector<std::u16string>{u"Top level", u"Alpha [disabled]", u"Beta"}),
      MenuLabels(submenu));
}

TEST_F(SidebarViewsTest, MoveToTopLevelFromTheSubmenu) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  MakeList(SidebarSection::kPinned);
  model_.AddFolderWith(u"Alpha", {u"One"});
  Refresh();

  TabRowView* row = views::AsViewClass<TabRowView>(list_->children()[1]);
  ASSERT_TRUE(row);
  ScopedMenuCapture capture;
  RightClickOn(row);
  ASSERT_TRUE(capture.menu());
  ui::MenuModel* submenu = Submenu(capture.menu()->menu(), u"Move to folder");
  ASSERT_TRUE(submenu);
  EXPECT_TRUE(Choose(submenu, u"Top level"));

  EXPECT_FALSE(model_.rows()[0].folder_id.has_value());
  ASSERT_EQ(1u, model_.folders().size());
  EXPECT_EQ(0, model_.folders()[0].entry_count);
}

// Deleting a folder returns its entries to the top level rather than closing
// them, which is what the item's label promises.
TEST_F(SidebarViewsTest, DeleteFolderFromAHeadersMenuKeepsTheTabs) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  MakeList(SidebarSection::kPinned);
  model_.AddFolderWith(u"Work", {u"One"});
  Refresh();

  ScopedMenuCapture capture;
  RightClickOn(views::AsViewClass<FolderHeaderView>(list_->children()[0]));
  ASSERT_TRUE(capture.menu());
  EXPECT_TRUE(
      Choose(capture.menu()->menu(), u"Delete folder (keeps its tabs)"));

  EXPECT_TRUE(model_.folders().empty());
  ASSERT_EQ(1u, model_.rows().size());
  EXPECT_EQ(u"One", model_.rows()[0].title);
  EXPECT_FALSE(model_.rows()[0].folder_id.has_value());
}

TEST_F(SidebarViewsTest, RenameFromAHeadersMenuOpensTheField) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  MakeList(SidebarSection::kPinned);
  model_.AddFolderWith(u"Work", {u"One"});
  Refresh();

  FolderHeaderView* header =
      views::AsViewClass<FolderHeaderView>(list_->children()[0]);
  ScopedMenuCapture capture;
  RightClickOn(header);
  ASSERT_TRUE(capture.menu());
  EXPECT_TRUE(Choose(capture.menu()->menu(), u"Rename"));
  EXPECT_TRUE(header->is_renaming());
}

// The accelerator is a plain virtual and needs no window: it applies the
// command to the active row, which is the reachable reading of "the focused
// row" while sidebar rows are accessibility-focusable only.
TEST_F(SidebarViewsTest, TheAcceleratorReturnsTheActiveRowToItsPinnedUrl) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, true);
  model_.AddTab(u"Two", "https://two.example/", SidebarSection::kPinned, false);
  model_.SetCanReturnToPinnedUrl(0, true);
  model_.SetCanReturnToPinnedUrl(1, true);
  auto* sidebar = contents_->AddChildView(
      std::make_unique<SidebarView>(&model_, SidebarView::Delegate()));

  EXPECT_TRUE(sidebar->AcceleratorPressed(
      ui::Accelerator(ui::VKEY_BACK, ui::EF_COMMAND_DOWN | ui::EF_SHIFT_DOWN)));
  // Only the active row, and it is the active one that changed.
  EXPECT_FALSE(model_.rows()[0].can_return_to_pinned_url);
  EXPECT_TRUE(model_.rows()[1].can_return_to_pinned_url);
}

TEST_F(SidebarViewsTest, TheAcceleratorDoesNothingWithNowhereToReturnTo) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, true);
  auto* sidebar = contents_->AddChildView(
      std::make_unique<SidebarView>(&model_, SidebarView::Delegate()));

  EXPECT_FALSE(sidebar->AcceleratorPressed(
      ui::Accelerator(ui::VKEY_BACK, ui::EF_COMMAND_DOWN | ui::EF_SHIFT_DOWN)));
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
