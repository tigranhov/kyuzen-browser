// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_TAB_LIST_VIEW_H_
#define ARCIUM_UI_SIDEBAR_TAB_LIST_VIEW_H_

#include <memory>
#include <string>
#include <vector>

#include "arcium/ui/sidebar/sidebar_model.h"
#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/geometry/point.h"
#include "ui/views/view.h"

namespace views {
class LabelButton;
}

namespace arcium {

class FolderHeaderView;
class RowContextMenu;
class TabRowView;

// A vertical list for one section: folder headers, each followed by its
// entries, then the entries in no folder. Only the Pinned section has
// folders. The Today list also shows the "New tab" row at its end.
class TabListView : public views::View {
  METADATA_HEADER(TabListView, views::View)

 public:
  TabListView(SidebarModel* model, SidebarSection section);
  TabListView(const TabListView&) = delete;
  TabListView& operator=(const TabListView&) = delete;
  ~TabListView() override;

  // Filters `rows` to this section and updates children, reusing views.
  void SetRows(const std::vector<SidebarRow>& rows);

  size_t row_count() const { return rows_.size(); }
  size_t folder_count() const { return headers_.size(); }

 private:
  // Where one child of this list sits in the laid-out order.
  struct PlanItem {
    bool is_header = false;
    // Index into the folder list for a header, into the section's rows for a
    // row. Both are positions in the vectors SetRows built.
    size_t index = 0;
    bool indented = false;
  };

  // A row backed by an entry commands the entry; a Today row commands the
  // tab. Only the first can be cold, and a cold row has no tab index.
  void OnActivateRow(const SidebarRow& row);
  void OnCloseRow(const SidebarRow& row);
  void OnDragMove(int from, int to);
  void OnRenameRow(const SidebarRow& row, const std::u16string& title);
  void OnRevertRow(const SidebarRow& row);
  void OnShowRowMenu(TabRowView* source,
                     const SidebarRow& row,
                     const gfx::Point& point);
  void OnToggleFolder(const SidebarFolder& folder);
  void OnRenameFolder(const SidebarFolder& folder, const std::u16string& name);
  void OnShowFolderMenu(FolderHeaderView* source,
                        const SidebarFolder& folder,
                        const gfx::Point& point);

  TabRowView* MakeRow();
  FolderHeaderView* MakeHeader();

  raw_ptr<SidebarModel> model_;
  const SidebarSection section_;
  std::vector<raw_ptr<TabRowView>> rows_;
  std::vector<raw_ptr<FolderHeaderView>> headers_;
  raw_ptr<views::LabelButton> new_tab_ = nullptr;
  // Outlives the menu it is running, so a command that arrives after the
  // click still finds its model and its snapshot.
  std::unique_ptr<RowContextMenu> context_menu_;
};

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_TAB_LIST_VIEW_H_
