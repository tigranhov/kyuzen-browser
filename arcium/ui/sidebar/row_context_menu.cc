// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/row_context_menu.h"

#include <memory>
#include <optional>
#include <utility>

#include "base/no_destructor.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/menu/menu_types.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace arcium {

namespace {

enum RowCommand {
  kPin = 1,
  kAddToFavorites,
  kRename,
  kReturnToPinnedUrl,
  kNewFolder,
  kUnpin,
  kRemoveFromFavorites,
  kCloseTab,
  kDeleteFolder,
  kMoveToTopLevel,
  // Every folder in the "Move to folder" submenu, in folders() order.
  kMoveToFolderFirst = 100,
};

// The name a folder gets when it is made from the menu. There is no dialog at
// this stage, so the folder appears named and the header's rename is the way
// to change it.
constexpr char16_t kNewFolderName[] = u"New folder";

RowContextMenu::ShowHookForTesting& ShowHook() {
  static base::NoDestructor<RowContextMenu::ShowHookForTesting> hook;
  return *hook;
}

}  // namespace

// static
base::AutoReset<RowContextMenu::ShowHookForTesting>
RowContextMenu::SetShowHookForTesting(ShowHookForTesting hook) {
  return base::AutoReset<ShowHookForTesting>(&ShowHook(), std::move(hook));
}

RowContextMenu::RowContextMenu(SidebarModel* model) : model_(model) {}

RowContextMenu::~RowContextMenu() = default;

void RowContextMenu::RunForRow(const SidebarRow& row,
                               views::View* source,
                               const gfx::Point& point,
                               base::RepeatingClosure begin_rename) {
  BuildForRow(row, std::move(begin_rename));
  Run(source, point);
}

void RowContextMenu::RunForFolder(const SidebarFolder& folder,
                                  views::View* source,
                                  const gfx::Point& point,
                                  base::RepeatingClosure begin_rename) {
  BuildForFolder(folder, std::move(begin_rename));
  Run(source, point);
}

void RowContextMenu::BuildForRow(const SidebarRow& row,
                                 base::RepeatingClosure begin_rename) {
  row_ = row;
  folder_ = SidebarFolder();
  is_folder_ = false;
  begin_rename_ = std::move(begin_rename);
  move_targets_.clear();
  move_submenu_.reset();
  menu_ = std::make_unique<ui::SimpleMenuModel>(this);

  switch (row.section) {
    case SidebarSection::kToday:
      menu_->AddItem(kPin, u"Pin");
      menu_->AddItem(kAddToFavorites, u"Add to Favorites");
      menu_->AddItem(kRename, u"Rename");
      menu_->AddSeparator(ui::NORMAL_SEPARATOR);
      menu_->AddItem(kCloseTab, u"Close");
      break;
    case SidebarSection::kPinned:
      menu_->AddItem(kRename, u"Rename");
      if (row.can_return_to_pinned_url) {
        menu_->AddItem(kReturnToPinnedUrl, u"Return to pinned URL");
      }
      menu_->AddSeparator(ui::NORMAL_SEPARATOR);
      menu_->AddItem(kNewFolder, u"New folder");
      move_submenu_ = std::make_unique<ui::SimpleMenuModel>(this);
      move_submenu_->AddItem(kMoveToTopLevel, u"Top level");
      for (const SidebarFolder& folder : model_->folders()) {
        move_submenu_->AddItem(
            kMoveToFolderFirst + static_cast<int>(move_targets_.size()),
            folder.name);
        move_targets_.push_back(folder.id);
      }
      menu_->AddSubMenu(0, u"Move to folder", move_submenu_.get());
      menu_->AddSeparator(ui::NORMAL_SEPARATOR);
      menu_->AddItem(kUnpin, u"Unpin");
      menu_->AddItem(kCloseTab, u"Close tab");
      break;
    case SidebarSection::kFavorites:
      menu_->AddItem(kRename, u"Rename");
      menu_->AddSeparator(ui::NORMAL_SEPARATOR);
      menu_->AddItem(kRemoveFromFavorites, u"Remove from Favorites");
      menu_->AddItem(kCloseTab, u"Close tab");
      break;
  }
}

void RowContextMenu::BuildForFolder(const SidebarFolder& folder,
                                    base::RepeatingClosure begin_rename) {
  row_ = SidebarRow();
  folder_ = folder;
  is_folder_ = true;
  begin_rename_ = std::move(begin_rename);
  move_targets_.clear();
  move_submenu_.reset();
  menu_ = std::make_unique<ui::SimpleMenuModel>(this);
  menu_->AddItem(kRename, u"Rename");
  menu_->AddSeparator(ui::NORMAL_SEPARATOR);
  // The reassurance is in the label because ui::MenuModel has no tooltip to
  // put it in, and "Delete folder" alone reads as deleting the tabs.
  menu_->AddItem(kDeleteFolder, u"Delete folder (keeps its tabs)");
}

void RowContextMenu::Run(views::View* source, const gfx::Point& point) {
  if (ShowHook()) {
    ShowHook().Run(this);
    return;
  }
  runner_ = std::make_unique<views::MenuRunner>(
      menu_.get(), views::MenuRunner::CONTEXT_MENU);
  runner_->RunMenuAt(source->GetWidget(), /*button_controller=*/nullptr,
                     gfx::Rect(point, gfx::Size()),
                     views::MenuAnchorPosition::kTopLeft,
                     ui::mojom::MenuSourceType::kMouse);
}

bool RowContextMenu::IsCommandIdEnabled(int command_id) const {
  if (command_id >= kMoveToFolderFirst) {
    // The folder the row is already in is not somewhere to move it, the same
    // way "Top level" is not when it is already there.
    const size_t index = static_cast<size_t>(command_id - kMoveToFolderFirst);
    return index < move_targets_.size() &&
           row_.folder_id != move_targets_[index];
  }
  switch (command_id) {
    case kRename:
      // A cold entry has no tab and a Today tab has no entry; both can still
      // be named. A row that is neither -- and a row whose view has nowhere
      // to put a field, such as a favourite tile, which offers no rename
      // closure -- has nothing for the item to do.
      return (is_folder_ || row_.entry_id.is_valid() || row_.tab_index >= 0) &&
             !begin_rename_.is_null();
    case kCloseTab:
      // A cold entry has no tab to close.
      return !row_.is_cold;
    case kMoveToTopLevel:
      return row_.folder_id.has_value();
    default:
      return true;
  }
}

void RowContextMenu::ExecuteCommand(int command_id, int event_flags) {
  if (command_id >= kMoveToFolderFirst) {
    const size_t index = static_cast<size_t>(command_id - kMoveToFolderFirst);
    if (index < move_targets_.size()) {
      model_->MoveEntryToFolder(row_.entry_id, move_targets_[index]);
    }
    return;
  }
  switch (command_id) {
    case kPin:
      model_->PinTab(row_.tab_index);
      return;
    case kAddToFavorites:
      model_->AddToFavorites(row_.tab_index);
      return;
    case kRename:
      if (begin_rename_) {
        begin_rename_.Run();
      }
      return;
    case kReturnToPinnedUrl:
      model_->ReturnToPinnedUrl(row_.entry_id);
      return;
    case kNewFolder:
      model_->CreateFolderWithEntry(row_.entry_id, kNewFolderName);
      return;
    case kMoveToTopLevel:
      model_->MoveEntryToFolder(row_.entry_id, std::nullopt);
      return;
    case kUnpin:
    case kRemoveFromFavorites:
      model_->UnpinEntry(row_.entry_id);
      return;
    case kCloseTab:
      // The same split TabListView makes: an entry's tab closes through the
      // entry so the entry survives, cold.
      if (row_.entry_id.is_valid()) {
        model_->CloseEntryTab(row_.entry_id);
      } else {
        model_->CloseTab(row_.tab_index);
      }
      return;
    case kDeleteFolder:
      model_->DeleteFolder(folder_.id);
      return;
    default:
      return;
  }
}

}  // namespace arcium
