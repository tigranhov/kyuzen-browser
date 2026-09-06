// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_TAB_LIST_VIEW_H_
#define ARCIUM_UI_SIDEBAR_TAB_LIST_VIEW_H_

#include <vector>

#include "arcium/ui/sidebar/sidebar_model.h"
#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

namespace views {
class LabelButton;
}

namespace arcium {

class TabRowView;

// A vertical list of TabRowViews for one section. The Today list also shows
// the "New tab" row at its end.
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

 private:
  void OnDragMove(int from, int to);

  raw_ptr<SidebarModel> model_;
  const SidebarSection section_;
  std::vector<raw_ptr<TabRowView>> rows_;
  raw_ptr<views::LabelButton> new_tab_ = nullptr;
};

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_TAB_LIST_VIEW_H_
