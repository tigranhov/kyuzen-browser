// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_FOLDER_HEADER_VIEW_H_
#define ARCIUM_UI_SIDEBAR_FOLDER_HEADER_VIEW_H_

#include <set>
#include <string>

#include "arcium/ui/sidebar/row_drag_data.h"
#include "arcium/ui/sidebar/sidebar_model.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/geometry/point.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/drag_controller.h"

namespace ui {
class ClipboardFormatType;
class OSExchangeData;
}  // namespace ui

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
                         public views::ContextMenuController,
                         public views::DragController {
  METADATA_HEADER(FolderHeaderView, views::Button)

 public:
  struct Delegate {
    base::RepeatingCallback<void(const SidebarFolder& folder)> toggle_collapsed;
    // Against the folder the edit was started on rather than whatever this
    // header draws when it ends: headers are pooled by position, so the two
    // are not the same thing.
    base::RepeatingCallback<void(FolderId id, const std::u16string& name)>
        rename;
    // `source` is this view, so the menu's Rename item can start the edit on
    // the header it was opened from. `point` is in screen coordinates.
    base::RepeatingCallback<void(FolderHeaderView* source,
                                 const SidebarFolder& folder,
                                 const gfx::Point& point)>
        show_context_menu;
    // A row dropped on this header: the entry it names goes into the folder.
    base::RepeatingCallback<void(EntryId id, const SidebarFolder& folder)>
        drop_entry;
    // Whether a folder may hold `id` at all. A header cannot tell a favourite
    // from a pinned entry — the payload carries an id and nothing else — and
    // MoveEntryToFolder no-ops for a favourite, so without this the drop is
    // accepted, the header highlights, and the gesture is silently discarded.
    // A predicate rather than a wider payload: the delegate is the owning
    // list, which holds the model, and it also refuses an id the model has
    // dropped since the drag began, which nothing written into the payload at
    // drag-start could.
    base::RepeatingCallback<bool(EntryId id)> can_accept_entry;
    // Whether `id` may be moved inside `parent`. The header cannot answer
    // this: a cycle and a depth cap are properties of the whole tree, and
    // only the owning list holds the model that knows it.
    base::RepeatingCallback<bool(FolderId id, FolderId parent)>
        can_accept_folder;
    // A folder header dropped on this one: it becomes this folder's child.
    base::RepeatingCallback<void(FolderId id, const SidebarFolder& folder)>
        drop_folder;
    // Once per drag, at the one moment a source knows one is starting -- the
    // signal an empty section needs to reserve a band to be dropped on.
    base::RepeatingCallback<void(const RowDragData& payload)> drag_started;
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
  // The folder the open edit belongs to; invalid when nothing is being renamed.
  FolderId renaming_folder_id() const { return renaming_folder_id_; }

  // views::Button / View:
  bool OnKeyPressed(const ui::KeyEvent& event) override;
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

  // views::View, drop target half. A header takes entries only: a Today tab
  // has no entry to put in a folder, so refusing it here lets the drop fall
  // through to the Pinned list, which knows how to make one.
  bool GetDropFormats(int* formats,
                      std::set<ui::ClipboardFormatType>* format_types) override;
  bool AreDropTypesRequired() override;
  bool CanDrop(const ui::OSExchangeData& data) override;
  void OnDragEntered(const ui::DropTargetEvent& event) override;
  int OnDragUpdated(const ui::DropTargetEvent& event) override;
  void OnDragExited() override;
  views::View::DropCallback GetDropCallback(
      const ui::DropTargetEvent& event) override;

  // views::DragController, drag source half. A folder is dragged by its
  // header, which is the only thing on screen that represents it.
  void WriteDragDataForView(views::View* sender,
                            const gfx::Point& press_pt,
                            ui::OSExchangeData* data) override;
  int GetDragOperationsForView(views::View* sender,
                               const gfx::Point& p) override;
  bool CanStartDragForView(views::View* sender,
                           const gfx::Point& press_pt,
                           const gfx::Point& p) override;

  bool is_drop_target_for_testing() const { return drop_target_; }

 private:
  // Whether this header would take `data`: an entry, and one the owning list
  // says a folder may hold. CanDrop and GetDropCallback must agree, or a
  // header refuses the highlight and then takes the drop anyway.
  bool CanAccept(const ui::OSExchangeData& data) const;
  void UpdateVisuals();
  // Takes the field away without an outcome, for when this header stops being
  // the header for the folder the edit was started on.
  void AbandonRename();
  void OnRenameFinished(FolderId id, bool commit, const std::u16string& name);
  void Toggle();
  void SetDropTarget(bool drop_target);
  void PerformDrop(RowDragData payload,
                   const ui::DropTargetEvent& event,
                   ui::mojom::DragOperation& output_drag_op,
                   std::unique_ptr<ui::LayerTreeOwner> drag_image_layer_owner);

  Delegate delegate_;
  SidebarFolder folder_;
  bool hovered_ = false;
  // Painted like a hover, so the header a drop would land in is the one that
  // looks like it. Kept apart from `hovered_`, which a drag does not set.
  bool drop_target_ = false;

  raw_ptr<views::ImageView> disclosure_ = nullptr;
  raw_ptr<views::Label> name_ = nullptr;
  raw_ptr<views::Label> count_ = nullptr;
  raw_ptr<RenameField> rename_field_ = nullptr;
  // Captured when the edit starts; see TabRowView for why the slot's current
  // contents are not good enough.
  FolderId renaming_folder_id_;
  base::WeakPtrFactory<FolderHeaderView> weak_factory_{this};
};

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_FOLDER_HEADER_VIEW_H_
