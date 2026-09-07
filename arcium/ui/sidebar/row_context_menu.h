// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_ROW_CONTEXT_MENU_H_
#define ARCIUM_UI_SIDEBAR_ROW_CONTEXT_MENU_H_

#include <memory>
#include <vector>

#include "arcium/ui/sidebar/sidebar_model.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "ui/menus/simple_menu_model.h"

namespace gfx {
class Point;
}

namespace views {
class MenuRunner;
class View;
}  // namespace views

namespace arcium {

// The right-click menu for a sidebar row or folder header. Built per row from
// its section, because a Today tab has no entry to rename and a favourite has
// no folder to be in.
//
// Everything but rename is a SidebarModel command; rename is a view action,
// so the caller hands over a closure that starts the inline edit on the view
// the menu was opened from.
class RowContextMenu : public ui::SimpleMenuModel::Delegate {
 public:
  explicit RowContextMenu(SidebarModel* model);
  RowContextMenu(const RowContextMenu&) = delete;
  RowContextMenu& operator=(const RowContextMenu&) = delete;
  ~RowContextMenu() override;

  // `point` is in screen coordinates. Both take a snapshot of what they are
  // shown for: the model can move while the menu is open, so every command
  // re-checks the entry and folder ids it names when it runs. Tab indices are
  // the exception — Pin, Add to Favorites and Close on a Today row use the
  // snapshot's index, because a tab index is a position and there is nothing
  // to re-check it against.
  void RunForRow(const SidebarRow& row,
                 views::View* source,
                 const gfx::Point& point,
                 base::RepeatingClosure begin_rename);
  void RunForFolder(const SidebarFolder& folder,
                    views::View* source,
                    const gfx::Point& point,
                    base::RepeatingClosure begin_rename);

  // The build half on its own. Separate from showing the menu so the items a
  // section offers can be asserted without spinning a nested menu loop.
  void BuildForRow(const SidebarRow& row, base::RepeatingClosure begin_rename);
  void BuildForFolder(const SidebarFolder& folder,
                      base::RepeatingClosure begin_rename);
  ui::SimpleMenuModel* menu() { return menu_.get(); }

  // Test seam. A context menu on macOS runs a nested native loop, so a test
  // that right-clicked for real would hang. With a hook installed the menu is
  // built exactly as it would be and handed over instead of being shown,
  // which is what lets the right-click path itself be covered rather than
  // only the builder underneath it. Pass a null callback to remove it.
  using ShowHookForTesting = base::RepeatingCallback<void(RowContextMenu*)>;
  static void SetShowHookForTesting(ShowHookForTesting hook);

  // ui::SimpleMenuModel::Delegate:
  bool IsCommandIdEnabled(int command_id) const override;
  void ExecuteCommand(int command_id, int event_flags) override;

 private:
  void Run(views::View* source, const gfx::Point& point);

  raw_ptr<SidebarModel> model_;
  SidebarRow row_;
  SidebarFolder folder_;
  bool is_folder_ = false;
  base::RepeatingClosure begin_rename_;
  // Parallel to the "Move to folder" submenu's items after the first.
  std::vector<FolderId> move_targets_;
  std::unique_ptr<ui::SimpleMenuModel> menu_;
  std::unique_ptr<ui::SimpleMenuModel> move_submenu_;
  std::unique_ptr<views::MenuRunner> runner_;
};

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_ROW_CONTEXT_MENU_H_
