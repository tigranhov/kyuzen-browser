// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/row_context_menu.h"

#include "arcium/browser/model/routing_rule.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"

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
  // The submenu's own item. It needs an id of its own so it can be disabled
  // when nothing inside it can be chosen; at id 0 it fell through to the
  // default and always read as enabled.
  kMoveToFolderParent,
  // "Move to space"'s own item. Only there when another space exists.
  kMoveToSpaceParent,
  // "Always open <site> in this space" and its undo. Only on a row whose page
  // is on the web.
  kOpenSiteHere,
  kStopOpeningSiteHere,
  // "Split with current page". Only on a row the model says could share the
  // screen with the page on it.
  kSplitWithCurrentPage,
  // Every folder in the "Move to folder" submenu, in folders() order, up to
  // RowContextMenu::kMoveToSpaceFirst, whose range is tested before this one.
  kMoveToFolderFirst = 100,
};

// The folder range ends where the space range begins, so "Move to folder"
// lists this many folders at most: one more would get a space's command id
// and move the row to a space instead.
constexpr size_t kMaxFolderTargets =
    static_cast<size_t>(RowContextMenu::kMoveToSpaceFirst - kMoveToFolderFirst);

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
  space_targets_.clear();
  space_submenu_.reset();
  menu_ = std::make_unique<ui::SimpleMenuModel>(this);

  switch (row.section) {
    case SidebarSection::kToday:
      menu_->AddItem(kPin, u"Pin");
      menu_->AddItem(kAddToFavorites, u"Add to Favorites");
      menu_->AddItem(kRename, u"Rename");
      AddSplitItem();
      AddMoveToSpaceSubmenu();
      AddRoutingItem();
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
        if (move_targets_.size() == kMaxFolderTargets) {
          break;
        }
        move_submenu_->AddItem(
            kMoveToFolderFirst + static_cast<int>(move_targets_.size()),
            folder.name);
        move_targets_.push_back(folder.id);
      }
      menu_->AddSubMenu(kMoveToFolderParent, u"Move to folder",
                        move_submenu_.get());
      AddSplitItem();
      AddMoveToSpaceSubmenu();
      AddRoutingItem();
      menu_->AddSeparator(ui::NORMAL_SEPARATOR);
      menu_->AddItem(kUnpin, u"Unpin");
      menu_->AddItem(kCloseTab, u"Close tab");
      break;
    case SidebarSection::kFavorites:
      menu_->AddItem(kRename, u"Rename");
      AddSplitItem();
      AddMoveToSpaceSubmenu();
      AddRoutingItem();
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
  space_targets_.clear();
  space_submenu_.reset();
  menu_ = std::make_unique<ui::SimpleMenuModel>(this);
  menu_->AddItem(kRename, u"Rename");
  menu_->AddSeparator(ui::NORMAL_SEPARATOR);
  // The same submenu a pinned row gets, filtered by what the model will
  // actually accept: a folder cannot go into itself or into its own
  // descendant, and cannot go somewhere that would carry its subtree past the
  // depth cap. Offering a target and then doing nothing is the mistake
  // can_accept_entry exists to prevent on the drag path.
  move_submenu_ = std::make_unique<ui::SimpleMenuModel>(this);
  move_submenu_->AddItem(kMoveToTopLevel, u"Top level");
  for (const SidebarFolder& target : model_->folders()) {
    if (move_targets_.size() == kMaxFolderTargets) {
      break;
    }
    if (!model_->CanMoveFolderTo(folder.id, target.id)) {
      continue;
    }
    move_submenu_->AddItem(
        kMoveToFolderFirst + static_cast<int>(move_targets_.size()),
        target.name);
    move_targets_.push_back(target.id);
  }
  menu_->AddSubMenu(kMoveToFolderParent, u"Move to folder",
                    move_submenu_.get());
  menu_->AddSeparator(ui::NORMAL_SEPARATOR);
  // The reassurance is in the label because ui::MenuModel has no tooltip to
  // put it in, and "Delete folder" alone reads as deleting the tabs.
  menu_->AddItem(kDeleteFolder, u"Delete folder (keeps its tabs)");
}

void RowContextMenu::AddMoveToSpaceSubmenu() {
  // With one space there is nowhere to go, and an item greyed out on every
  // row's menu would say something is missing. Unlike "Move to folder",
  // which is still worth showing empty because a folder is made from this
  // same menu, a space is made from the bar.
  if (model_->spaces().size() < 2) {
    return;
  }
  space_submenu_ = std::make_unique<ui::SimpleMenuModel>(this);
  for (const SidebarSpace& space : model_->spaces()) {
    // The row is drawn in the active space, so that is where it already is.
    if (space.is_active) {
      continue;
    }
    space_submenu_->AddItem(
        kMoveToSpaceFirst + static_cast<int>(space_targets_.size()),
        space.icon.empty() ? space.name : space.icon + u" " + space.name);
    space_targets_.push_back(space.id);
  }
  menu_->AddSubMenu(kMoveToSpaceParent, u"Move to space", space_submenu_.get());
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
  if (command_id >= kMoveToSpaceFirst) {
    return static_cast<size_t>(command_id - kMoveToSpaceFirst) <
           space_targets_.size();
  }
  if (command_id >= kMoveToFolderFirst) {
    const size_t index = static_cast<size_t>(command_id - kMoveToFolderFirst);
    if (index >= move_targets_.size()) {
      return false;
    }
    // The place it already is, is not a place to move it -- the same rule for
    // a folder as for a row. A folder's list was additionally filtered when
    // it was built, to what the model will actually accept.
    return is_folder_ ? folder_.parent_id != move_targets_[index]
                      : row_.folder_id != move_targets_[index];
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
    case kNewFolder:
      // A folder made from this row goes inside the folder the row is already
      // in, so at the deepest level there is nowhere to put it. Offering the
      // item and then making nothing is the affordance-that-lies mistake this
      // stage has already fixed twice.
      return model_->CanCreateFolderWithEntry(row_.entry_id);
    case kMoveToTopLevel:
      return is_folder_ ? folder_.parent_id.has_value()
                        : row_.folder_id.has_value();
    case kMoveToFolderParent: {
      // A submenu is only as enabled as its contents. Opening one whose every
      // item is greyed out tells the user there is somewhere to go and then
      // offers nowhere.
      if (IsCommandIdEnabled(kMoveToTopLevel)) {
        return true;
      }
      for (size_t i = 0; i < move_targets_.size(); ++i) {
        if (IsCommandIdEnabled(kMoveToFolderFirst + static_cast<int>(i))) {
          return true;
        }
      }
      return false;
    }
    default:
      return true;
  }
}

void RowContextMenu::AddSplitItem() {
  // Asked rather than assumed: the row on screen cannot share with itself,
  // a row in another space is refused, and a row already sharing has to be
  // taken apart first. An item that would do nothing is worse than no item.
  if (model_->CanSplitRow(row_)) {
    menu_->AddItem(kSplitWithCurrentPage, u"Split with current page");
  }
}

void RowContextMenu::AddRoutingItem() {
  const std::string site = RuleSiteForUrl(row_.url);
  if (site.empty()) {
    return;
  }
  const std::u16string name = base::UTF8ToUTF16(site);
  if (model_->SiteOpensInActiveSpace(row_.url)) {
    menu_->AddItem(kStopOpeningSiteHere,
                   base::StrCat({u"Stop opening ", name, u" in this space"}));
  } else {
    menu_->AddItem(kOpenSiteHere,
                   base::StrCat({u"Always open ", name, u" in this space"}));
  }
}

void RowContextMenu::ExecuteCommand(int command_id, int event_flags) {
  if (command_id == kSplitWithCurrentPage) {
    // Re-asked at the moment it runs: the menu is a snapshot and the page on
    // screen can change while it is open, which is exactly what this item is
    // about.
    if (model_->CanSplitRow(row_)) {
      model_->SplitRowWithCurrentPage(row_);
    }
    return;
  }
  if (command_id >= kMoveToSpaceFirst) {
    const size_t index = static_cast<size_t>(command_id - kMoveToSpaceFirst);
    if (index >= space_targets_.size()) {
      return;
    }
    // A row with an entry moves the entry, which carries its tab; a Today
    // row has no entry, so the tab itself is re-tagged.
    if (row_.entry_id.is_valid()) {
      model_->MoveEntryToSpace(row_.entry_id, space_targets_[index]);
    } else {
      model_->MoveTabToSpace(row_.tab_index, space_targets_[index]);
    }
    return;
  }
  if (command_id >= kMoveToFolderFirst) {
    const size_t index = static_cast<size_t>(command_id - kMoveToFolderFirst);
    if (index >= move_targets_.size()) {
      return;
    }
    if (is_folder_) {
      model_->SetFolderParent(folder_.id, move_targets_[index]);
    } else {
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
    case kOpenSiteHere:
      model_->SetSiteOpensInActiveSpace(row_.url, true);
      return;
    case kStopOpeningSiteHere:
      model_->SetSiteOpensInActiveSpace(row_.url, false);
      return;
    case kNewFolder:
      model_->CreateFolderWithEntry(row_.entry_id, kNewFolderName);
      return;
    case kMoveToTopLevel:
      if (is_folder_) {
        model_->SetFolderParent(folder_.id, std::nullopt);
      } else {
        model_->MoveEntryToFolder(row_.entry_id, std::nullopt);
      }
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
