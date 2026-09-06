// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/tab_list_view.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "arcium/ui/sidebar/sidebar_colors.h"
#include "arcium/ui/sidebar/sidebar_metrics.h"
#include "arcium/ui/sidebar/tab_row_view.h"
#include "arcium/ui/sidebar/vector_icons.h"
#include "base/functional/bind.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/layout/box_layout.h"

namespace arcium {

TabListView::TabListView(SidebarModel* model, SidebarSection section)
    : model_(model), section_(section) {
  SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 2));
  if (section_ == SidebarSection::kToday) {
    new_tab_ = AddChildView(std::make_unique<views::LabelButton>(
        base::BindRepeating(&SidebarModel::NewTab, base::Unretained(model_)),
        u"New tab"));
    new_tab_->SetImageModel(
        views::Button::STATE_NORMAL,
        ui::ImageModel::FromVectorIcon(kAddIcon, kColorArciumRowTextSecondary,
                                       metrics::kFaviconSize));
    new_tab_->SetEnabledTextColors(kColorArciumRowTextSecondary);
    new_tab_->SetBorder(views::CreateEmptyBorder(
        gfx::Insets::VH(0, metrics::kRowHorizontalPadding)));
    new_tab_->SetMinSize(gfx::Size(0, metrics::kRowHeight));
    new_tab_->SetImageLabelSpacing(metrics::kRowIconTextGap);
    new_tab_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  }
}

TabListView::~TabListView() = default;

void TabListView::SetRows(const std::vector<SidebarRow>& all_rows) {
  std::vector<const SidebarRow*> mine;
  for (const SidebarRow& row : all_rows) {
    if (row.section == section_) {
      mine.push_back(&row);
    }
  }
  // Grow or shrink the pool of row views, then assign in order. Views are
  // reused by position so a title change or reorder does not allocate.
  while (rows_.size() < mine.size()) {
    TabRowView::Delegate delegate;
    delegate.activate = base::BindRepeating(&SidebarModel::ActivateTab,
                                            base::Unretained(model_));
    delegate.close = base::BindRepeating(&SidebarModel::CloseTab,
                                         base::Unretained(model_));
    delegate.drag_move = base::BindRepeating(&TabListView::OnDragMove,
                                             base::Unretained(this));
    auto* row = AddChildViewAt(
        std::make_unique<TabRowView>(std::move(delegate)), rows_.size());
    rows_.push_back(row);
  }
  while (rows_.size() > mine.size()) {
    TabRowView* row = rows_.back();
    rows_.pop_back();
    RemoveChildViewT(row);
  }
  for (size_t i = 0; i < mine.size(); ++i) {
    rows_[i]->SetRow(*mine[i]);
  }
  SetVisible(!rows_.empty() || new_tab_);
  InvalidateLayout();
}

void TabListView::OnDragMove(int from, int to) {
  // Clamp to this section's range in tab-index space.
  if (rows_.empty()) {
    return;
  }
  const int first = rows_.front()->tab_index();
  const int last = rows_.back()->tab_index();
  to = std::clamp(to, first, last);
  if (to != from) {
    model_->MoveTab(from, to);
  }
}

BEGIN_METADATA(TabListView)
END_METADATA

}  // namespace arcium
