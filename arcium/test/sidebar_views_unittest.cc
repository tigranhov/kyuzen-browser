// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "arcium/test/test_app_activation.h"
#include "arcium/ui/playground/fake_sidebar_model.h"
#include "arcium/ui/sidebar/archive_list_view.h"
#include "arcium/ui/sidebar/favorites_grid_view.h"
#include "arcium/ui/sidebar/folder_header_view.h"
#include "arcium/ui/sidebar/rename_field.h"
#include "arcium/ui/sidebar/row_context_menu.h"
#include "arcium/ui/sidebar/section_divider_view.h"
#include "arcium/ui/sidebar/sidebar_metrics.h"
#include "arcium/ui/sidebar/sidebar_model.h"
#include "arcium/ui/sidebar/sidebar_view.h"
#include "arcium/ui/sidebar/space_bar_view.h"
#include "arcium/ui/sidebar/tab_list_view.h"
#include "arcium/ui/sidebar/tab_row_view.h"
#include "arcium/ui/sidebar/view_snapshot.h"
#include "base/auto_reset.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/logging.h"
#include "base/memory/scoped_refptr.h"
#include "base/time/time.h"
#include "cc/paint/display_item_list.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/compositor/paint_context.h"
#include "ui/events/event.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/events/test/event_generator.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/paint_info.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/test/views_test_utils.h"
#include "ui/views/view.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_utils.h"
#include "ui/views/window/client_view.h"
#include "url/gurl.h"

namespace arcium {
namespace {

// Keeps whatever RowContextMenu a right-click builds, instead of letting it
// spin a nested native menu loop that a unit test can never get out of. What
// the hook hands over is the real menu, built by the real path.
class ScopedMenuCapture {
 public:
  ScopedMenuCapture()
      : hook_(RowContextMenu::SetShowHookForTesting(base::BindRepeating(
            [](ScopedMenuCapture* self, RowContextMenu* menu) {
              self->menu_ = menu;
              ++self->count_;
            },
            base::Unretained(this)))) {}
  ScopedMenuCapture(const ScopedMenuCapture&) = delete;
  ScopedMenuCapture& operator=(const ScopedMenuCapture&) = delete;
  ~ScopedMenuCapture() = default;

  RowContextMenu* menu() { return menu_; }
  int count() const { return count_; }

 private:
  raw_ptr<RowContextMenu> menu_ = nullptr;
  int count_ = 0;
  // Restores the previous (empty) hook on destruction, so a forgotten-scoper
  // bug is impossible to write here — the type itself is the guardrail the
  // header's comment asks every caller to have.
  base::AutoReset<RowContextMenu::ShowHookForTesting> hook_;
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
    // Before ViewsTestBase::SetUp(), which constructs the helper that would
    // otherwise promote this binary to a foreground application and pull the
    // desktop onto the suite's Space.
    arcium::test::SuppressTestAppActivation();
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

  // The whole column, for the handful of things that only exist once it is
  // assembled: the divider's buttons and the bubble they open.
  SidebarView* MakeSidebar() {
    SidebarView::Delegate delegate;
    delegate.toggle_sidebar = base::DoNothing();
    delegate.back = base::DoNothing();
    delegate.forward = base::DoNothing();
    delegate.reload = base::DoNothing();
    delegate.edit_url = base::DoNothing();
    SidebarView* sidebar = contents_->AddChildView(
        std::make_unique<SidebarView>(&model_, std::move(delegate)));
    views::test::RunScheduledLayout(widget_.get());
    return sidebar;
  }

  // A real press at the real button, so the routing from the divider through
  // SidebarView::ShowArchiveList is under test as much as the handler is —
  // the button is hover-revealed, so the hover is part of the gesture.
  void OpenArchiveList(SidebarView* sidebar) {
    Hover(sidebar->divider());
    views::LabelButton* button = sidebar->divider()->archive_button();
    CHECK(button);
    ClickOn(button);
  }

  // ClickOn for a view that lives in the archive bubble's widget rather than
  // in the fixture's. The bubble is a separate top-level widget with its own
  // NSWindow, so the fixture's generator cannot reach it and the layout has to
  // be run on the bubble itself.
  void ClickOnInBubble(views::View* view) {
    views::Widget* widget = view->GetWidget();
    CHECK(widget);
    views::test::RunScheduledLayout(widget);
    // The bubble is a second top-level widget, and the fixture's generator is
    // rooted at the sidebar's — a press aimed there never arrives. Mac's
    // EventGeneratorDelegate allows exactly one live generator
    // (DCHECK(!instance_) in event_generator_delegate_mac.mm), so the fixture's
    // is put down for the duration and rebuilt afterwards.
    generator_.reset();
    {
      ui::test::EventGenerator generator(views::GetRootWindow(widget));
      generator.MoveMouseTo(view->GetBoundsInScreen().CenterPoint());
      generator.ClickLeftButton();
    }
    generator_ = std::make_unique<ui::test::EventGenerator>(
        views::GetRootWindow(widget_.get()));
  }

  // What close-on-deactivate does when the user clicks away.
  void CloseArchiveList(SidebarView* sidebar) {
    ArchiveListView* list = sidebar->archive_list_for_testing();
    CHECK(list);
    list->GetWidget()->Close();
  }

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

  // A tile the user can see is a tile the user can click. Views hit-tests
  // front to back, so a later sibling laid out over a visible child silently
  // takes its clicks; nothing in the grid may do that.
  bool EveryVisibleChildTakesItsOwnClicks(views::View* grid) {
    views::test::RunScheduledLayout(widget_.get());
    for (views::View* child : grid->children()) {
      if (!child->GetVisible()) {
        continue;
      }
      if (grid->GetEventHandlerForPoint(child->bounds().CenterPoint()) !=
          child) {
        return false;
      }
    }
    return true;
  }

  // The command id of the enabled item with this label, searching the top
  // level and then each submenu. std::nullopt when there is none, or when the
  // item is there but disabled -- a disabled item is not something the user
  // can choose, so it is not something a test should be able to choose
  // either, which is what makes MenuOffers mean what it says.
  std::optional<int> FindMenuItem(RowContextMenu& menu,
                                  const std::u16string& label) {
    ui::MenuModel* model = menu.menu();
    if (!model) {
      return std::nullopt;
    }
    for (size_t i = 0; i < model->GetItemCount(); ++i) {
      if (model->GetTypeAt(i) == ui::MenuModel::TYPE_SUBMENU) {
        ui::MenuModel* submenu = model->GetSubmenuModelAt(i);
        for (size_t j = 0; submenu && j < submenu->GetItemCount(); ++j) {
          if (submenu->GetLabelAt(j) == label && submenu->IsEnabledAt(j)) {
            return submenu->GetCommandIdAt(j);
          }
        }
        continue;
      }
      if (model->GetLabelAt(i) == label && model->IsEnabledAt(i)) {
        return model->GetCommandIdAt(i);
      }
    }
    return std::nullopt;
  }

  // Whether the menu, submenus included, has an enabled item with this label.
  bool MenuOffers(RowContextMenu& menu, const std::u16string& label) {
    return FindMenuItem(menu, label).has_value();
  }

  // Runs the enabled item with this label. CHECKs when there is none, so a
  // test cannot silently assert nothing by naming an item that is not there.
  void ExecuteMenuItem(RowContextMenu& menu, const std::u16string& label) {
    const std::optional<int> command = FindMenuItem(menu, label);
    CHECK(command.has_value()) << "no enabled menu item named " << label;
    menu.ExecuteCommand(*command, 0);
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

TEST_F(SidebarViewsTest, ANestedFolderAndItsRowsAreIndentedByDepth) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  model_.AddTab(u"Two", "https://two.example/", SidebarSection::kPinned, false);
  MakeList(SidebarSection::kPinned);
  const FolderId outer = model_.AddFolderWith(u"Outer", {u"One"});
  const FolderId inner = model_.AddFolderWith(u"Inner", {u"Two"});
  model_.SetFolderParent(inner, outer);
  Refresh();

  // Outer's header, Outer's row, then Inner's header and Inner's row inside
  // it -- pre-order, drawn.
  EXPECT_EQ((std::vector<std::string>{"FolderHeaderView", "TabRowView",
                                      "FolderHeaderView", "TabRowView"}),
            ChildClasses());

  const auto indent_of = [this](size_t index) {
    const gfx::Insets* margins =
        list_->children()[index]->GetProperty(views::kMarginsKey);
    return margins ? margins->left() : 0;
  };
  EXPECT_EQ(0, indent_of(0));
  EXPECT_EQ(metrics::kFolderIndent, indent_of(1));
  EXPECT_EQ(metrics::kFolderIndent, indent_of(2));
  EXPECT_EQ(2 * metrics::kFolderIndent, indent_of(3));
}

TEST_F(SidebarViewsTest, ACollapsedFolderHidesItsWholeSubtree) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  model_.AddTab(u"Two", "https://two.example/", SidebarSection::kPinned, false);
  model_.AddTab(u"Three", "https://three.example/", SidebarSection::kPinned,
                false);
  MakeList(SidebarSection::kPinned);
  const FolderId outer = model_.AddFolderWith(u"Outer", {u"One"});
  const FolderId inner = model_.AddFolderWith(u"Inner", {u"Two"});
  model_.SetFolderParent(inner, outer);
  model_.SetFolderCollapsed(outer, true);
  Refresh();

  // Only the collapsed header and the pinned row in no folder at all. The
  // nested header is hidden with everything under it, which is what a
  // disclosure triangle promises.
  EXPECT_EQ((std::vector<std::string>{"FolderHeaderView", "TabRowView"}),
            ChildClasses());
  // Both headers are still *built* -- `headers_` is sized to folders().size()
  // -- and the hidden one is simply not placed in the plan, so it is not a
  // child of this list.
  EXPECT_EQ(2u, list_->folder_count());
  EXPECT_EQ(1u, list_->row_count());
}

TEST_F(SidebarViewsTest, ACollapsedFolderDoesNotSwallowItsOwnSibling) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  model_.AddTab(u"Two", "https://two.example/", SidebarSection::kPinned, false);
  model_.AddTab(u"Three", "https://three.example/", SidebarSection::kPinned,
                false);
  MakeList(SidebarSection::kPinned);
  // AddFolderWith makes the folder from its first entry, so each of these
  // needs a tab of its own -- an empty title list returns an invalid id.
  const FolderId outer = model_.AddFolderWith(u"Outer", {u"One"});
  const FolderId first_inner = model_.AddFolderWith(u"First inner", {u"Two"});
  const FolderId second_inner =
      model_.AddFolderWith(u"Second inner", {u"Three"});
  model_.SetFolderParent(first_inner, outer);
  model_.SetFolderParent(second_inner, outer);
  model_.SetFolderCollapsed(first_inner, true);
  Refresh();

  // The skip must stop at the first folder that is *not* deeper than the
  // collapsed one. A skip written with >= instead of > eats the sibling that
  // follows it, and nothing else in this file would notice.
  EXPECT_EQ((std::vector<std::string>{"FolderHeaderView", "TabRowView",
                                      "FolderHeaderView", "FolderHeaderView",
                                      "TabRowView"}),
            ChildClasses());
}

TEST_F(SidebarViewsTest, ACollapsedNestedFolderStillLeavesItsParentDrawn) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  model_.AddTab(u"Two", "https://two.example/", SidebarSection::kPinned, false);
  MakeList(SidebarSection::kPinned);
  const FolderId outer = model_.AddFolderWith(u"Outer", {u"One"});
  const FolderId inner = model_.AddFolderWith(u"Inner", {u"Two"});
  model_.SetFolderParent(inner, outer);
  model_.SetFolderCollapsed(inner, true);
  Refresh();

  // Collapsing the inner folder hides only its own row.
  EXPECT_EQ((std::vector<std::string>{"FolderHeaderView", "TabRowView",
                                      "FolderHeaderView"}),
            ChildClasses());
  EXPECT_EQ(1u, list_->row_count());
}

TEST_F(SidebarViewsTest, TheTodaySectionHasNoFolders) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kToday, true);
  MakeList(SidebarSection::kToday);
  Refresh();
  EXPECT_EQ(0u, list_->folder_count());
}

// A Today tab now carries a name of its own, so the refusal is narrower than
// it was: what cannot be renamed is a row naming neither an entry nor a tab,
// which has nothing at either end to hold the name.
TEST_F(SidebarViewsTest, ARowNamingNeitherAnEntryNorATabCannotBeRenamed) {
  auto row = std::make_unique<TabRowView>(TabRowView::Delegate{});
  ASSERT_FALSE(row->row().entry_id.is_valid());
  ASSERT_LT(row->row().tab_index, 0);

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
  ASSERT_TRUE(row->is_renaming());
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

// FakeSidebarModel must not be more permissive than ArciumModel::RemoveFolder,
// which renormalises positions to 0..n-1 after a deletion.
//
// A single delete-then-create only produces a *tie* between the new folder
// and the last survivor, and std::sort's tie-breaking on this small a range
// turns out to preserve insertion order in practice — so a test built on one
// collision passes whether or not positions are renormalised, the "passes
// both directions" trap. Three deletions off the front instead: without
// renormalising after each one, the survivors D and E keep the *stale*
// positions (3 and 4) they had among five folders, both higher than the
// next-free position (folders_.size(), 2) a folder made afterwards receives.
// That is not a tie to break, it is F(2) sorting strictly before D(3) and
// E(4) — an unambiguous wrong answer under any conforming sort.
TEST_F(SidebarViewsTest, DeletingAFolderRenormalisesRemainingPositions) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  model_.AddTab(u"Two", "https://two.example/", SidebarSection::kPinned, false);
  model_.AddTab(u"Three", "https://three.example/", SidebarSection::kPinned,
                false);
  model_.AddTab(u"Four", "https://four.example/", SidebarSection::kPinned,
                false);
  model_.AddTab(u"Five", "https://five.example/", SidebarSection::kPinned,
                false);
  model_.AddTab(u"Six", "https://six.example/", SidebarSection::kPinned, false);
  const FolderId a = model_.AddFolderWith(u"A", {u"One"});
  const FolderId b = model_.AddFolderWith(u"B", {u"Two"});
  const FolderId c = model_.AddFolderWith(u"C", {u"Three"});
  const FolderId d = model_.AddFolderWith(u"D", {u"Four"});
  const FolderId e = model_.AddFolderWith(u"E", {u"Five"});

  model_.DeleteFolder(a);
  model_.DeleteFolder(b);
  model_.DeleteFolder(c);
  const FolderId f = model_.AddFolderWith(u"F", {u"Six"});

  std::vector<SidebarFolder> folders = model_.folders();
  ASSERT_EQ(3u, folders.size());
  EXPECT_EQ(d, folders[0].id);
  EXPECT_EQ(e, folders[1].id);
  EXPECT_EQ(f, folders[2].id);
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
  ASSERT_FALSE(model_.folders()[0].collapsed);
  views::test::RunScheduledLayout(widget_.get());
  generator().MoveMouseTo(header->GetBoundsInScreen().CenterPoint());
  generator().DoubleClickLeftButton();

  EXPECT_FALSE(header->is_renaming());
  // Click 1 collapsed it; click 2's toggle-back is swallowed by the fake's
  // unchanged-value early return, because nothing rebuilt the header between
  // the two clicks and it is still handing over the pre-click value. Without
  // this assertion the test would pass identically if the header stopped
  // responding to clicks altogether.
  EXPECT_TRUE(model_.folders()[0].collapsed);
}

// Headers are FocusBehavior::ACCESSIBLE_ONLY, which does not put them out of
// EventGenerator's reach: View::RequestFocusWithReason gates a request on
// IsAccessibilityFocusable() rather than refusing it once the focus manager
// is in keyboard-accessible mode — the state macOS Full Keyboard Access and
// VoiceOver turn on — so this is a real, focus-routed F2, not a direct call
// to the handler.
TEST_F(SidebarViewsTest, F2RenamesAFolderHeader) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  MakeList(SidebarSection::kPinned);
  model_.AddFolderWith(u"Work", {u"One"});
  Refresh();

  FolderHeaderView* header =
      views::AsViewClass<FolderHeaderView>(list_->children()[0]);
  ASSERT_TRUE(header);
  widget_->GetFocusManager()->SetKeyboardAccessible(true);
  header->RequestFocus();
  ASSERT_EQ(header, widget_->GetFocusManager()->GetFocusedView());
  generator().PressAndReleaseKey(ui::VKEY_F2, ui::EF_NONE);
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
  // Rename is live now: a Today tab's name lives in the model's side table
  // and dies with the tab, so the item has somewhere to write.
  EXPECT_EQ((std::vector<std::u16string>{u"Pin", u"Add to Favorites", u"Rename",
                                         u"Close"}),
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
  // "Move to folder" is dead here: this row is not in a folder and there are
  // no folders to move it into, so every item in the submenu is greyed out.
  EXPECT_EQ((std::vector<std::u16string>{
                u"Rename", u"Return to pinned URL", u"New folder",
                u"Move to folder [disabled]", u"Unpin", u"Close tab"}),
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
  EXPECT_EQ((std::vector<std::u16string>{u"Rename", u"New folder",
                                         u"Move to folder [disabled]", u"Unpin",
                                         u"Close tab"}),
            MenuLabels(capture.menu()->menu()));
}

// A favourite is a tile in the grid, not a row in a list, and the tiles carry
// the same menu. Rename is offered: the field goes in the tile's row, not the
// tile itself, which is a quarter of the sidebar wide.
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
  EXPECT_EQ((std::vector<std::u16string>{u"Rename", u"Remove from Favorites",
                                         u"Close tab"}),
            MenuLabels(capture.menu()->menu()));
}

// The field goes up bounded to the tile's row, not the tile: the grid is
// its own context menu controller, so the closure the menu's Rename item runs
// has to find the right tile by the index it was built for.
TEST_F(SidebarViewsTest, RenameFromAFavouriteTilesMenuOpensTheField) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kFavorites,
                false);
  auto* grid =
      contents_->AddChildView(std::make_unique<FavoritesGridView>(&model_));
  grid->SetRows(model_.rows());

  ScopedMenuCapture capture;
  RightClickOn(grid->children()[0]);
  ASSERT_TRUE(capture.menu());
  EXPECT_TRUE(Choose(capture.menu()->menu(), u"Rename"));

  RenameField* field = FocusedField();
  ASSERT_TRUE(field);
  EXPECT_EQ(u"One", field->GetText());
}

// Enter goes in as a real key, through the same field a pinned row's rename
// uses, committing through SetEntryTitle exactly as TabRowView's does.
TEST_F(SidebarViewsTest, EnterCommitsAFavouriteRenameThroughTheModel) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kFavorites,
                false);
  auto* grid =
      contents_->AddChildView(std::make_unique<FavoritesGridView>(&model_));
  grid->SetRows(model_.rows());

  ScopedMenuCapture capture;
  RightClickOn(grid->children()[0]);
  ASSERT_TRUE(capture.menu());
  EXPECT_TRUE(Choose(capture.menu()->menu(), u"Rename"));

  RenameField* field = FocusedField();
  ASSERT_TRUE(field);
  field->SetText(u"Renamed");
  generator().PressAndReleaseKey(ui::VKEY_RETURN, ui::EF_NONE);
  task_environment()->RunUntilIdle();

  ASSERT_EQ(1u, model_.rows().size());
  EXPECT_EQ(u"Renamed", model_.rows()[0].title);
}

// The other discipline round 1 established for TabRowView and
// FolderHeaderView: a tile is pooled by position, so a rename open on one
// slot must not survive that slot being handed a different entry, and must
// not write to whatever the slot draws once it is.
TEST_F(SidebarViewsTest, RepointingAFavouriteTilesIndexAbandonsItsOpenRename) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kFavorites,
                false);
  model_.AddTab(u"Two", "https://two.example/", SidebarSection::kFavorites,
                false);
  model_.AddTab(u"Three", "https://three.example/", SidebarSection::kFavorites,
                false);
  auto* grid =
      contents_->AddChildView(std::make_unique<FavoritesGridView>(&model_));
  grid->SetRows(model_.rows());
  ASSERT_EQ(3u, grid->children().size());

  const EntryId one_id = model_.rows()[0].entry_id;
  const EntryId two_id = model_.rows()[1].entry_id;

  ScopedMenuCapture capture;
  RightClickOn(grid->children()[1]);  // "Two"
  ASSERT_TRUE(capture.menu());
  EXPECT_TRUE(Choose(capture.menu()->menu(), u"Rename"));
  RenameField* field = FocusedField();
  ASSERT_TRUE(field);
  field->SetText(u"Renamed");

  // "One" leaves the favourites grid, so tile index 1 — where the rename is
  // open — now draws "Three" instead of "Two".
  model_.UnpinEntry(one_id);
  grid->SetRows(model_.rows());

  EXPECT_FALSE(FocusedField());
  // The row the field was covering comes back with it, on this path too.
  for (const views::View* tile : grid->children()) {
    EXPECT_TRUE(tile->GetVisible());
  }
  generator().PressAndReleaseKey(ui::VKEY_RETURN, ui::EF_NONE);
  task_environment()->RunUntilIdle();

  // Abandoned: nothing was renamed anywhere in the model.
  for (const SidebarRow& row : model_.rows()) {
    if (row.entry_id == two_id) {
      EXPECT_EQ(u"Two", row.title);
    }
    EXPECT_NE(u"Renamed", row.title);
  }
}

// The field is bounded to the tile's whole row, so every other tile in that
// row would sit underneath it: still visible, still laid out at its own
// column, and — because the field is added last and Views hit-tests front to
// back — no longer able to receive the click it looks like it can. The row
// goes away for the duration of the edit instead, and comes back with it.
TEST_F(SidebarViewsTest,
       ARenameHidesTheFavouriteRowItCoversAndCommitRestoresIt) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kFavorites,
                false);
  model_.AddTab(u"Two", "https://two.example/", SidebarSection::kFavorites,
                false);
  model_.AddTab(u"Three", "https://three.example/", SidebarSection::kFavorites,
                false);
  auto* grid =
      contents_->AddChildView(std::make_unique<FavoritesGridView>(&model_));
  grid->SetRows(model_.rows());
  views::test::RunScheduledLayout(widget_.get());
  ASSERT_EQ(3u, grid->children().size());
  // kFavoritesPerRow is 4, so all three share row 0 with the renamed tile.
  views::View* one = grid->children()[0];
  views::View* three = grid->children()[2];
  const gfx::Point one_centre = one->bounds().CenterPoint();
  const gfx::Point three_centre = three->bounds().CenterPoint();
  ASSERT_EQ(one, grid->GetEventHandlerForPoint(one_centre));
  ASSERT_EQ(three, grid->GetEventHandlerForPoint(three_centre));

  ScopedMenuCapture capture;
  RightClickOn(grid->children()[1]);  // "Two"
  ASSERT_TRUE(capture.menu());
  ASSERT_TRUE(Choose(capture.menu()->menu(), u"Rename"));
  RenameField* field = FocusedField();
  ASSERT_TRUE(field);
  views::test::RunScheduledLayout(widget_.get());

  // Hidden, not merely covered: a tile that cannot be clicked must not look
  // clickable.
  EXPECT_FALSE(one->GetVisible());
  EXPECT_FALSE(three->GetVisible());
  EXPECT_EQ(field, grid->GetEventHandlerForPoint(one_centre));
  EXPECT_EQ(field, grid->GetEventHandlerForPoint(three_centre));
  // The invariant behind both: the field owns the row it covers, and no tile
  // is left visible underneath it losing the clicks it looks able to take.
  EXPECT_TRUE(EveryVisibleChildTakesItsOwnClicks(grid));

  field->SetText(u"Renamed");
  generator().PressAndReleaseKey(ui::VKEY_RETURN, ui::EF_NONE);
  task_environment()->RunUntilIdle();
  views::test::RunScheduledLayout(widget_.get());

  EXPECT_TRUE(one->GetVisible());
  EXPECT_TRUE(three->GetVisible());
  EXPECT_EQ(one, grid->GetEventHandlerForPoint(one_centre));
  EXPECT_EQ(three, grid->GetEventHandlerForPoint(three_centre));
  EXPECT_TRUE(EveryVisibleChildTakesItsOwnClicks(grid));
  ASSERT_EQ(3u, model_.rows().size());
  EXPECT_EQ(u"Renamed", model_.rows()[1].title);
}

// The other way an edit ends. Escape takes the same OnRenameFinished path as
// Enter with commit=false, and it has to put the row back just as commit does.
TEST_F(SidebarViewsTest, AbandoningAFavouriteRenameRestoresTheRowItCovered) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kFavorites,
                false);
  model_.AddTab(u"Two", "https://two.example/", SidebarSection::kFavorites,
                false);
  model_.AddTab(u"Three", "https://three.example/", SidebarSection::kFavorites,
                false);
  auto* grid =
      contents_->AddChildView(std::make_unique<FavoritesGridView>(&model_));
  grid->SetRows(model_.rows());
  views::test::RunScheduledLayout(widget_.get());
  ASSERT_EQ(3u, grid->children().size());
  views::View* one = grid->children()[0];
  views::View* three = grid->children()[2];
  const gfx::Point one_centre = one->bounds().CenterPoint();
  const gfx::Point three_centre = three->bounds().CenterPoint();

  ScopedMenuCapture capture;
  RightClickOn(grid->children()[1]);  // "Two"
  ASSERT_TRUE(capture.menu());
  ASSERT_TRUE(Choose(capture.menu()->menu(), u"Rename"));
  RenameField* field = FocusedField();
  ASSERT_TRUE(field);
  views::test::RunScheduledLayout(widget_.get());

  EXPECT_FALSE(one->GetVisible());
  EXPECT_FALSE(three->GetVisible());
  EXPECT_EQ(field, grid->GetEventHandlerForPoint(one_centre));
  EXPECT_TRUE(EveryVisibleChildTakesItsOwnClicks(grid));

  field->SetText(u"Renamed");
  generator().PressAndReleaseKey(ui::VKEY_ESCAPE, ui::EF_NONE);
  task_environment()->RunUntilIdle();
  views::test::RunScheduledLayout(widget_.get());

  EXPECT_TRUE(one->GetVisible());
  EXPECT_TRUE(three->GetVisible());
  EXPECT_EQ(one, grid->GetEventHandlerForPoint(one_centre));
  EXPECT_EQ(three, grid->GetEventHandlerForPoint(three_centre));
  EXPECT_TRUE(EveryVisibleChildTakesItsOwnClicks(grid));
  // Escape abandons, so nothing was written.
  for (const SidebarRow& row : model_.rows()) {
    EXPECT_NE(u"Renamed", row.title);
  }
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
  // "Move to folder" is disabled here, and that is the point of the submenu
  // having an id of its own: this folder is the only one and it is already at
  // the top level, so every item inside it is greyed out.
  EXPECT_EQ(
      (std::vector<std::u16string>{u"Rename", u"Move to folder [disabled]",
                                   u"Delete folder (keeps its tabs)"}),
      MenuLabels(capture.menu()->menu()));
}

// A submenu is only as enabled as its contents. With a second folder to move
// into, the same item is live -- which is what keeps the assertion above from
// passing for the trivial reason that the item is always disabled.
TEST_F(SidebarViewsTest, MoveToFolderIsDeadWhenThereIsNowhereToGo) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  model_.AddTab(u"Two", "https://two.example/", SidebarSection::kPinned, false);
  MakeList(SidebarSection::kPinned);
  model_.AddFolderWith(u"Work", {u"One"});
  Refresh();

  {
    ScopedMenuCapture capture;
    RightClickOn(views::AsViewClass<FolderHeaderView>(list_->children()[0]));
    ASSERT_TRUE(capture.menu());
    const std::vector<std::u16string> labels =
        MenuLabels(capture.menu()->menu());
    EXPECT_NE(labels.end(), std::find(labels.begin(), labels.end(),
                                      u"Move to folder [disabled]"));
  }

  model_.AddFolderWith(u"Other", {u"Two"});
  Refresh();
  {
    ScopedMenuCapture capture;
    RightClickOn(views::AsViewClass<FolderHeaderView>(list_->children()[0]));
    ASSERT_TRUE(capture.menu());
    const std::vector<std::u16string> labels =
        MenuLabels(capture.menu()->menu());
    EXPECT_NE(labels.end(),
              std::find(labels.begin(), labels.end(), u"Move to folder"));
  }
}

TEST_F(SidebarViewsTest, ColdRowsCannotCloseATabTheyDoNotHave) {
  model_.AddColdEntry(u"Cold", "https://cold.example/",
                      SidebarSection::kPinned);
  MakeList(SidebarSection::kPinned);
  Refresh();

  ScopedMenuCapture capture;
  RightClickOn(views::AsViewClass<TabRowView>(list_->children()[0]));
  ASSERT_TRUE(capture.menu());
  EXPECT_EQ((std::vector<std::u16string>{u"Rename", u"New folder",
                                         u"Move to folder [disabled]", u"Unpin",
                                         u"Close tab [disabled]"}),
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

TEST_F(SidebarViewsTest, AFolderMenuOffersTheFoldersItCanMoveInto) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  model_.AddTab(u"Two", "https://two.example/", SidebarSection::kPinned, false);
  model_.AddTab(u"Three", "https://three.example/", SidebarSection::kPinned,
                false);
  MakeList(SidebarSection::kPinned);
  const FolderId outer = model_.AddFolderWith(u"Outer", {u"One"});
  const FolderId inner = model_.AddFolderWith(u"Inner", {u"Two"});
  const FolderId other = model_.AddFolderWith(u"Other", {u"Three"});
  model_.SetFolderParent(inner, outer);
  Refresh();

  // The menu for "Inner", which is nested inside "Outer".
  RowContextMenu inner_menu(&model_);
  inner_menu.BuildForFolder(model_.folders()[1], base::DoNothing());
  EXPECT_TRUE(MenuOffers(inner_menu, u"Other"));
  EXPECT_TRUE(MenuOffers(inner_menu, u"Top level"));
  // The folder it is already in is not somewhere to move it, and neither is
  // itself.
  EXPECT_FALSE(MenuOffers(inner_menu, u"Outer"));
  EXPECT_FALSE(MenuOffers(inner_menu, u"Inner"));

  // The menu for "Outer", which is a root and has a child.
  RowContextMenu outer_menu(&model_);
  outer_menu.BuildForFolder(model_.folders()[0], base::DoNothing());
  EXPECT_TRUE(MenuOffers(outer_menu, u"Other"));
  // Already at the top level, and "Inner" is its own descendant.
  EXPECT_FALSE(MenuOffers(outer_menu, u"Top level"));
  EXPECT_FALSE(MenuOffers(outer_menu, u"Inner"));
}

TEST_F(SidebarViewsTest, MovingAFolderFromItsMenuNestsIt) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  model_.AddTab(u"Two", "https://two.example/", SidebarSection::kPinned, false);
  MakeList(SidebarSection::kPinned);
  const FolderId outer = model_.AddFolderWith(u"Outer", {u"One"});
  const FolderId inner = model_.AddFolderWith(u"Inner", {u"Two"});
  Refresh();

  RowContextMenu menu(&model_);
  // The menu for "Inner", which is folders()[1] while both are roots.
  menu.BuildForFolder(model_.folders()[1], base::DoNothing());
  ExecuteMenuItem(menu, u"Outer");

  std::vector<SidebarFolder> folders = model_.folders();
  ASSERT_EQ(2u, folders.size());
  EXPECT_EQ(outer, folders[0].id);
  EXPECT_EQ(inner, folders[1].id);
  EXPECT_EQ(1, folders[1].depth);
}

TEST_F(SidebarViewsTest, MovingAFolderToTheTopLevelFromItsMenuUnnestsIt) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  model_.AddTab(u"Two", "https://two.example/", SidebarSection::kPinned, false);
  MakeList(SidebarSection::kPinned);
  const FolderId outer = model_.AddFolderWith(u"Outer", {u"One"});
  const FolderId inner = model_.AddFolderWith(u"Inner", {u"Two"});
  model_.SetFolderParent(inner, outer);
  Refresh();

  RowContextMenu menu(&model_);
  menu.BuildForFolder(model_.folders()[1], base::DoNothing());
  ExecuteMenuItem(menu, u"Top level");

  std::vector<SidebarFolder> folders = model_.folders();
  ASSERT_EQ(2u, folders.size());
  EXPECT_EQ(0, folders[0].depth);
  EXPECT_EQ(0, folders[1].depth);
  EXPECT_EQ(outer, folders[0].id);
  EXPECT_EQ(inner, folders[1].id);
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

// The close button appears under the cursor that summoned it, so the row must
// still count as hovered once the pointer is over that button. Views' default
// is the opposite -- a view is "entered" only while the mouse is over it and
// NOT over a descendant -- which makes the button hide itself the instant it
// appears, then reappear, forever. SectionDividerView already works around
// this by hand with IsMouseHovered(); rows use the flag upstream provides.
TEST_F(SidebarViewsTest, MovingOntoTheCloseButtonKeepsTheRowHovered) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kPinned, false);
  MakeList(SidebarSection::kPinned);
  Refresh();

  TabRowView* row = views::AsViewClass<TabRowView>(list_->children()[0]);
  ASSERT_TRUE(row);

  // Real routing, not the Hover() helper: that helper calls OnMouseEntered
  // directly, so the event processor never holds the row as its target and
  // never sends it the exit this test is about.
  views::test::RunScheduledLayout(widget_.get());
  generator_->MoveMouseTo(row->GetBoundsInScreen().CenterPoint());
  views::test::RunScheduledLayout(widget_.get());

  views::View* close = row->GetViewByID(TabRowView::kCloseButtonId);
  ASSERT_TRUE(close);
  ASSERT_TRUE(close->GetVisible()) << "hovering the row must reveal it";
  const gfx::Point on_button = close->GetBoundsInScreen().CenterPoint();
  ASSERT_NE(on_button, row->GetBoundsInScreen().CenterPoint())
      << "the button must sit somewhere the row's centre is not";

  generator_->MoveMouseTo(on_button);
  views::test::RunScheduledLayout(widget_.get());

  EXPECT_TRUE(close->GetVisible())
      << "the button hid itself when the cursor reached it";
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

// R2.3 names four archive timeouts, and the space bar's menu is where they are
// chosen. The rest of that menu stays disabled until Stages 3 and 6.
TEST_F(SidebarViewsTest, TheSpaceMenuChoosesTheArchiveTimeout) {
  SpaceBarView bar(&model_);
  ASSERT_EQ(ArchiveTimeout::kTwelveHours, model_.archive_timeout());
  EXPECT_TRUE(bar.IsCommandIdChecked(SpaceBarView::kTimeoutTwelveHours));

  bar.ExecuteCommand(SpaceBarView::kTimeoutSevenDays, 0);
  EXPECT_EQ(ArchiveTimeout::kSevenDays, model_.archive_timeout());
  EXPECT_TRUE(bar.IsCommandIdChecked(SpaceBarView::kTimeoutSevenDays));
  EXPECT_FALSE(bar.IsCommandIdChecked(SpaceBarView::kTimeoutTwelveHours));

  bar.ExecuteCommand(SpaceBarView::kTimeoutNever, 0);
  EXPECT_EQ(ArchiveTimeout::kNever, model_.archive_timeout());

  // Choosing one must be possible; the Stage 3 and 6 items must not be.
  EXPECT_TRUE(bar.IsCommandIdEnabled(SpaceBarView::kTimeoutOneDay));
  EXPECT_FALSE(bar.IsCommandIdEnabled(SpaceBarView::kRename));
  EXPECT_FALSE(bar.IsCommandIdEnabled(SpaceBarView::kDelete));
}

// Off the record there is no archive and never will be, so the button is
// absent rather than present and disabled: a control that can never be used
// reads as a bug, and this stage has already removed one for that reason.
TEST_F(SidebarViewsTest, NoArchiveMeansNoArchiveButton) {
  model_.SetHasArchive(false);
  SidebarView* sidebar = MakeSidebar();
  EXPECT_EQ(nullptr, sidebar->divider()->archive_button());
}

TEST_F(SidebarViewsTest, TheArchiveButtonAppearsBesideClearOnHover) {
  SidebarView* sidebar = MakeSidebar();
  views::LabelButton* button = sidebar->divider()->archive_button();
  ASSERT_TRUE(button);
  EXPECT_FALSE(button->GetVisible());

  Hover(sidebar->divider());
  EXPECT_TRUE(button->GetVisible());
}

// The rows are asked for when the list opens and arrive on a later turn of
// the run loop, which is what the model's asynchrony buys and what the view
// has to be correct about: it is laid out once with nothing in it.
//
// And while it is empty it says nothing. The bubble used to show "Nothing
// archived yet" for that turn, so the first thing a user saw after archiving
// a tab was the message telling them nothing had been archived.
TEST_F(SidebarViewsTest, TheArchiveListFillsWhenTheReadComesBack) {
  const base::Time now = base::Time::Now();
  model_.AddArchived(u"One", "https://one.example/", now - base::Hours(2));
  model_.AddArchived(u"Two", "https://two.example/", now - base::Hours(5));
  SidebarView* sidebar = MakeSidebar();

  OpenArchiveList(sidebar);
  ArchiveListView* list = sidebar->archive_list_for_testing();
  ASSERT_TRUE(list);
  EXPECT_EQ(0u, list->row_count_for_testing());
  EXPECT_EQ(u"", list->status_message_for_testing());

  task_environment()->RunUntilIdle();
  EXPECT_EQ(2u, list->row_count_for_testing());
  EXPECT_EQ(u"", list->status_message_for_testing());
}

// Clicking reopens the page and takes the row out of the list, without asking
// the archive again: the delete is posted to a background sequence, so a
// re-read would race it and could hand back the row just clicked.
TEST_F(SidebarViewsTest, ClickingAnArchivedRowReopensItAndDropsIt) {
  const base::Time now = base::Time::Now();
  model_.AddArchived(u"One", "https://one.example/", now - base::Hours(2));
  model_.AddArchived(u"Two", "https://two.example/", now - base::Hours(5));
  SidebarView* sidebar = MakeSidebar();
  OpenArchiveList(sidebar);
  task_environment()->RunUntilIdle();
  ArchiveListView* list = sidebar->archive_list_for_testing();
  ASSERT_EQ(2u, list->row_count_for_testing());

  // A real press at the real row, in the bubble's own widget. The handler
  // destroys the very Button whose callback is running, so the routing is as
  // much of the claim as the handler is.
  ClickOnInBubble(list->row_at_for_testing(0));

  ASSERT_EQ(1u, model_.reopened().size());
  EXPECT_EQ(GURL("https://one.example/"), model_.reopened()[0].url);
  EXPECT_EQ(now - base::Hours(2), model_.reopened()[0].archived_at);
  EXPECT_EQ(1u, list->row_count_for_testing());
  EXPECT_FALSE(model_.has_pending_archive_request());
}

// An archive with nothing in it says so, rather than showing an empty box the
// user cannot tell from a broken one.
TEST_F(SidebarViewsTest, AnEmptyArchiveSaysSo) {
  SidebarView* sidebar = MakeSidebar();
  OpenArchiveList(sidebar);
  task_environment()->RunUntilIdle();

  ArchiveListView* list = sidebar->archive_list_for_testing();
  ASSERT_TRUE(list);
  EXPECT_EQ(0u, list->row_count_for_testing());
  EXPECT_EQ(u"Nothing archived yet", list->status_message_for_testing());
}

// The other empty list, and the whole reason the reply carries a readable
// flag: an archive whose file would not open has no rows either, and telling
// that user "Nothing archived yet" says their tabs were never written down
// when in fact they cannot be read back. The button stays — it is offered
// before the open has even run — so the list is the thing that has to tell
// the truth.
TEST_F(SidebarViewsTest, AnUnreadableArchiveSaysSoRatherThanNothingArchived) {
  model_.AddArchived(u"One", "https://one.example/",
                     base::Time::Now() - base::Hours(2));
  model_.SetArchiveReadable(false);
  SidebarView* sidebar = MakeSidebar();
  ASSERT_TRUE(sidebar->divider()->archive_button());

  OpenArchiveList(sidebar);
  task_environment()->RunUntilIdle();
  ArchiveListView* list = sidebar->archive_list_for_testing();
  ASSERT_TRUE(list);
  EXPECT_EQ(0u, list->row_count_for_testing());
  EXPECT_EQ(u"The archive could not be opened",
            list->status_message_for_testing());
}

// A bubble outlives the click that opened it, and the read behind it outlives
// the bubble. The reply must land on nothing rather than on a freed delegate.
//
// The ordering this needs cannot be produced by closing and draining: the
// close callback is synchronous (CreateBubble arms it with
// MakeCloseSynchronous, and Widget::CloseWithReason runs override_close_ on
// the spot), so DestroyArchiveList is posted *after* the model's reply is
// already queued and the reply therefore lands on a live view every time. An
// earlier version of this test did exactly that and could not fail for the
// reason it was named for. Holding the reply in the model is what puts it
// behind the destruction, which is where a real SQLite read can land.
TEST_F(SidebarViewsTest, ClosingTheListWhileTheReadIsInFlightIsSafe) {
  model_.AddArchived(u"One", "https://one.example/",
                     base::Time::Now() - base::Hours(2));
  model_.SetHoldArchiveReplies(true);
  SidebarView* sidebar = MakeSidebar();
  OpenArchiveList(sidebar);
  ASSERT_TRUE(sidebar->archive_list_for_testing());

  // The request has left the view and the answer is parked, not delivered.
  task_environment()->RunUntilIdle();
  ASSERT_EQ(1u, model_.held_archive_reply_count());
  ASSERT_TRUE(model_.has_pending_archive_request());
  ASSERT_EQ(0u, sidebar->archive_list_for_testing()->row_count_for_testing());

  // What close-on-deactivate does, and then the turn that frees the delegate.
  CloseArchiveList(sidebar);
  task_environment()->RunUntilIdle();
  ASSERT_EQ(nullptr, sidebar->archive_list_for_testing());

  // Now the read comes back, into a delegate that no longer exists. Without
  // the WeakPtr on ArchiveListView this writes `archived_` and walks
  // `contents_` on freed memory.
  model_.DeliverHeldArchiveReplies();
  EXPECT_FALSE(model_.has_pending_archive_request());
  EXPECT_EQ(nullptr, sidebar->archive_list_for_testing());
}

// The reopen guard. A press that is not a mouse press — an accessibility
// action, a test — can reach the button with the bubble already up, and the
// guard must reuse that bubble rather than close it: CreateBubble's
// override_close_ is a OnceCallback the first close consumes, so a second
// Close() on a CLIENT_OWNS_WIDGET widget falls through Widget's deprecated
// asynchronous path underneath the reset already queued. Reusing is also what
// BrowserSidebarController::ShowQuickEntry does.
TEST_F(SidebarViewsTest, OpeningTheListTwiceReusesTheOpenBubble) {
  model_.AddArchived(u"One", "https://one.example/",
                     base::Time::Now() - base::Hours(2));
  SidebarView* sidebar = MakeSidebar();
  OpenArchiveList(sidebar);
  task_environment()->RunUntilIdle();
  ArchiveListView* first = sidebar->archive_list_for_testing();
  ASSERT_TRUE(first);
  ASSERT_EQ(1u, first->row_count_for_testing());
  views::Widget* widget = first->GetWidget();
  ASSERT_TRUE(widget);

  OpenArchiveList(sidebar);
  // The same delegate and the same widget, still open, and no second read.
  EXPECT_EQ(first, sidebar->archive_list_for_testing());
  EXPECT_EQ(widget, sidebar->archive_list_for_testing()->GetWidget());
  EXPECT_FALSE(model_.has_pending_archive_request());
  EXPECT_EQ(1u, first->row_count_for_testing());

  // And it survives the turn: nothing was closed, so nothing frees it.
  task_environment()->RunUntilIdle();
  EXPECT_EQ(first, sidebar->archive_list_for_testing());
}

// The other half of the guard: between the close and the posted
// DestroyArchiveList the widget is still there but must not be shown again,
// and a press in that window must not build a second delegate over the one
// the closing widget still points at.
TEST_F(SidebarViewsTest, PressingTheButtonWhileTheListIsClosingOpensNothing) {
  SidebarView* sidebar = MakeSidebar();
  OpenArchiveList(sidebar);
  task_environment()->RunUntilIdle();
  ArchiveListView* first = sidebar->archive_list_for_testing();
  ASSERT_TRUE(first);

  CloseArchiveList(sidebar);
  // DestroyArchiveList is posted and has not run.
  ASSERT_EQ(first, sidebar->archive_list_for_testing());
  OpenArchiveList(sidebar);
  EXPECT_EQ(first, sidebar->archive_list_for_testing());

  task_environment()->RunUntilIdle();
  EXPECT_EQ(nullptr, sidebar->archive_list_for_testing());

  // And the next press opens a fresh one, which issues its own read.
  OpenArchiveList(sidebar);
  EXPECT_TRUE(sidebar->archive_list_for_testing());
  EXPECT_TRUE(model_.has_pending_archive_request());
}

// Disabled by default: it writes a PNG and asserts nothing a normal run
// needs. Run it with
//
//   out/dev/arcium_unittests --single-process-tests \
//     --gtest_also_run_disabled_tests \
//     --gtest_filter=*ArchiveListBubbleRendersItsRows*
//
// and look at the file it names.
//
// It exists because --snapshot cannot show this bubble, and the reason is not
// obvious: WriteViewSnapshot paints the widget's root view, and
// BubbleDialogDelegate::CreateClientView
// (ui/views/bubble/bubble_dialog_delegate_view.cc, SetPaintToLayer(
// layer_type()) — on BubbleDialogDelegate, not on BubbleDialogDelegateView,
// so a plain-delegate bubble like this one gets it too) puts the client view
// on its own ui::Layer so its rounded-corner clip applies. A root paint skips
// layer-backed children, so the snapshot comes out as a bubble frame with the
// title and an empty body — a limitation, not a bug. Painting the client view
// as its own paint root is what shows the rows. Without this test the next
// person rediscovers that the slow way.
//
// The colours come out as unresolved placeholders: arcium_unittests registers
// no AddArciumColorMixer. The geometry is the part worth looking at.
TEST_F(SidebarViewsTest, DISABLED_ArchiveListBubbleRendersItsRows) {
  const base::Time now = base::Time::Now();
  model_.AddArchived(u"Arc Browser", "https://arc.net/", now - base::Hours(2));
  model_.AddArchived(u"WebKit Blog", "https://webkit.org/blog/",
                     now - base::Hours(9));
  model_.AddArchived(
      u"A very long title that has to elide before it reaches "
      u"the timestamp column",
      "https://example.com/long", now - base::Days(1));
  model_.AddArchived(u"Rust Book", "https://doc.rust-lang.org/book/",
                     now - base::Days(4));
  SidebarView* sidebar = MakeSidebar();
  OpenArchiveList(sidebar);
  task_environment()->RunUntilIdle();

  ArchiveListView* list = sidebar->archive_list_for_testing();
  ASSERT_TRUE(list);
  ASSERT_EQ(4u, list->row_count_for_testing());
  views::Widget* bubble = list->GetWidget();
  ASSERT_TRUE(bubble);
  bubble->LayoutRootViewIfNecessary();
  views::View* client = bubble->client_view();
  ASSERT_TRUE(client->layer()) << "the client view is expected to be "
                                  "layer-backed; that is the whole finding";

  // Paint the client view as its own paint root, which is the step
  // WriteViewSnapshot cannot take.
  const gfx::Size size = client->size();
  ASSERT_FALSE(size.IsEmpty());
  constexpr float kScale = 2.f;
  auto display_list = base::MakeRefCounted<cc::DisplayItemList>();
  ui::PaintContext context(display_list.get(), kScale, gfx::Rect(size),
                           /*is_pixel_canvas=*/false);
  client->Paint(views::PaintInfo::CreateRootPaintInfo(context, size));
  display_list->Finalize();
  SkBitmap bitmap;
  ASSERT_TRUE(
      bitmap.tryAllocN32Pixels(size.width() * kScale, size.height() * kScale));
  bitmap.eraseColor(SK_ColorBLACK);
  SkCanvas canvas(bitmap);
  canvas.scale(kScale, kScale);
  display_list->Raster(&canvas, cc::PlaybackParams(nullptr));

  std::optional<std::vector<uint8_t>> png =
      gfx::PNGCodec::EncodeBGRASkBitmap(bitmap, /*discard_transparency=*/true);
  ASSERT_TRUE(png);
  base::FilePath dir;
  ASSERT_TRUE(base::GetTempDir(&dir));
  const base::FilePath path = dir.AppendASCII("arcium_archive_list_bubble.png");
  ASSERT_TRUE(base::WriteFile(path, *png));
  LOG(ERROR) << "archive bubble snapshot: " << path << " (" << bitmap.width()
             << "x" << bitmap.height() << " px)";
  LogViewHierarchy(client);
}

// Pinned and Today scroll as one. With Pinned outside the scroll viewport,
// enough pins push Today off the bottom of the panel and it cannot be reached
// at all; scrolling down must take the pins away and reveal Today, and
// scrolling back up must bring them back.
TEST_F(SidebarViewsTest, PinnedAndTodayShareOneScrollViewport) {
  SidebarView* sidebar = MakeSidebar();

  views::ScrollView* scroll = nullptr;
  for (views::View* view : sidebar->children()) {
    if (views::IsViewClass<views::ScrollView>(view)) {
      scroll = static_cast<views::ScrollView*>(view);
      break;
    }
  }
  ASSERT_TRUE(scroll);
  ASSERT_TRUE(scroll->contents());

  std::vector<SidebarSection> scrolling;
  std::function<void(views::View*)> collect = [&](views::View* view) {
    if (auto* list = views::AsViewClass<TabListView>(view)) {
      scrolling.push_back(list->section());
    }
    for (views::View* child : view->children()) {
      collect(child);
    }
  };
  collect(scroll->contents());

  std::sort(scrolling.begin(), scrolling.end());
  EXPECT_EQ((std::vector<SidebarSection>{SidebarSection::kPinned,
                                         SidebarSection::kToday}),
            scrolling)
      << "both lists must live inside the one scroll viewport";
}

// A Today tab can be named now that SetTabTitle gives the name somewhere to
// live. It used to be refused for having no entry.
TEST_F(SidebarViewsTest, ATodayRowCanBeRenamed) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kToday, false);
  MakeList(SidebarSection::kToday);
  Refresh();

  TabRowView* row = views::AsViewClass<TabRowView>(list_->children()[0]);
  ASSERT_TRUE(row);
  ASSERT_FALSE(row->row().entry_id.is_valid()) << "must be a Today row";

  row->BeginRename();

  EXPECT_TRUE(row->is_renaming());
}

// The double-click gesture reaches Today rows for the same reason, which is
// what makes it the way to rename without going through the menu.
TEST_F(SidebarViewsTest, DoubleClickingATodayRowOpensTheRenameField) {
  model_.AddTab(u"One", "https://one.example/", SidebarSection::kToday, false);
  MakeList(SidebarSection::kToday);
  Refresh();

  TabRowView* row = views::AsViewClass<TabRowView>(list_->children()[0]);
  ASSERT_TRUE(row);
  views::test::RunScheduledLayout(widget_.get());
  generator_->MoveMouseTo(row->GetBoundsInScreen().CenterPoint());
  generator_->DoubleClickLeftButton();

  EXPECT_TRUE(row->is_renaming());
}

// A two-finger scroll arrives as a ui::ScrollEvent, and ScrollView::
// OnScrollEvent DCHECKs scroll_with_layers_enabled_ whenever the compositor
// has a scroll input handler -- which on macOS it always does, because
// kUiCompositorScrollWithLayers is enabled by default there. A scroll view
// built with ScrollWithLayers::kDisabled therefore aborts the browser the
// first time anyone scrolls the sidebar. The layer on the contents is the
// observable half of that state.
TEST_F(SidebarViewsTest, TheTodayListScrollsWithLayers) {
  SidebarView* sidebar = MakeSidebar();

  views::ScrollView* scroll = nullptr;
  for (views::View* view : sidebar->children()) {
    if (views::IsViewClass<views::ScrollView>(view)) {
      scroll = static_cast<views::ScrollView*>(view);
      break;
    }
  }
  ASSERT_TRUE(scroll) << "the Today list is no longer in a ScrollView";
  ASSERT_TRUE(scroll->contents());
  EXPECT_TRUE(scroll->contents()->layer())
      << "contents must be layer-backed, or scrolling the sidebar aborts";
}

}  // namespace
}  // namespace arcium
