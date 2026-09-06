// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_SPACE_BAR_VIEW_H_
#define ARCIUM_UI_SIDEBAR_SPACE_BAR_VIEW_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/view.h"

namespace views {
class LabelButton;
class MenuRunner;
}  // namespace views

namespace arcium {

// Bottom bar: space chips (one, "Default", in Stage 1) and the profile badge.
// The context menu exists with every item disabled; Stages 3 and 6 enable them.
class SpaceBarView : public views::View,
                     public views::ContextMenuController,
                     public ui::SimpleMenuModel::Delegate {
  METADATA_HEADER(SpaceBarView, views::View)

 public:
  enum MenuCommand { kRename = 1, kEditTheme, kChangeIcon, kDelete };

  SpaceBarView();
  SpaceBarView(const SpaceBarView&) = delete;
  SpaceBarView& operator=(const SpaceBarView&) = delete;
  ~SpaceBarView() override;

  // views::ContextMenuController:
  void ShowContextMenuForViewImpl(
      views::View* source,
      const gfx::Point& point,
      ui::mojom::MenuSourceType source_type) override;

  // ui::SimpleMenuModel::Delegate:
  bool IsCommandIdEnabled(int command_id) const override;
  void ExecuteCommand(int command_id, int event_flags) override;

  // views::View:
  void OnThemeChanged() override;

 private:
  raw_ptr<views::LabelButton> active_chip_ = nullptr;
  raw_ptr<views::View> profile_badge_ = nullptr;
  std::unique_ptr<ui::SimpleMenuModel> menu_model_;
  std::unique_ptr<views::MenuRunner> menu_runner_;
};

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_SPACE_BAR_VIEW_H_
