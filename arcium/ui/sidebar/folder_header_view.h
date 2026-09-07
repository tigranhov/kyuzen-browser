// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_FOLDER_HEADER_VIEW_H_
#define ARCIUM_UI_SIDEBAR_FOLDER_HEADER_VIEW_H_

#include <string>

#include "arcium/ui/sidebar/sidebar_model.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/geometry/point.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/controls/button/button.h"

namespace views {
class ImageView;
class Label;
}  // namespace views

namespace arcium {

class RenameField;

// The 32px row above a folder's entries: a disclosure triangle, the folder's
// name, and how many entries are inside. It draws entirely from the
// SidebarFolder it is given, including the count, so opening the sidebar
// never walks the rows once per folder.
class FolderHeaderView : public views::Button,
                         public views::ContextMenuController {
  METADATA_HEADER(FolderHeaderView, views::Button)

 public:
  struct Delegate {
    base::RepeatingCallback<void(const SidebarFolder& folder)> toggle_collapsed;
    base::RepeatingCallback<void(const SidebarFolder& folder,
                                 const std::u16string& name)>
        rename;
    // `source` is this view, so the menu's Rename item can start the edit on
    // the header it was opened from. `point` is in screen coordinates.
    base::RepeatingCallback<void(FolderHeaderView* source,
                                 const SidebarFolder& folder,
                                 const gfx::Point& point)>
        show_context_menu;
  };

  explicit FolderHeaderView(Delegate delegate);
  FolderHeaderView(const FolderHeaderView&) = delete;
  FolderHeaderView& operator=(const FolderHeaderView&) = delete;
  ~FolderHeaderView() override;

  void SetFolder(const SidebarFolder& folder);
  base::WeakPtr<FolderHeaderView> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }
  const SidebarFolder& folder() const { return folder_; }

  void BeginRename();
  bool is_renaming() const { return rename_field_ != nullptr; }

  // views::Button / View:
  bool OnMousePressed(const ui::MouseEvent& event) override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;
  void OnThemeChanged() override;
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;

  // views::ContextMenuController:
  void ShowContextMenuForViewImpl(
      views::View* source,
      const gfx::Point& point,
      ui::mojom::MenuSourceType source_type) override;

 private:
  void UpdateVisuals();
  void OnRenameFinished(bool commit, const std::u16string& name);
  void Toggle();

  Delegate delegate_;
  SidebarFolder folder_;
  bool hovered_ = false;

  raw_ptr<views::ImageView> disclosure_ = nullptr;
  raw_ptr<views::Label> name_ = nullptr;
  raw_ptr<views::Label> count_ = nullptr;
  raw_ptr<RenameField> rename_field_ = nullptr;
  base::WeakPtrFactory<FolderHeaderView> weak_factory_{this};
};

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_FOLDER_HEADER_VIEW_H_
