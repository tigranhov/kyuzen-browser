// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_TAB_ROW_VIEW_H_
#define ARCIUM_UI_SIDEBAR_TAB_ROW_VIEW_H_

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
class ImageButton;
class ImageView;
class Label;
class Throbber;
}  // namespace views

namespace arcium {

class RenameField;

// One 32px row: favicon or throbber, title, audio indicator, and on hover
// either a close button or — for a pinned entry that has navigated away — the
// button that returns it to its pinned URL.
class TabRowView : public views::Button, public views::ContextMenuController {
  METADATA_HEADER(TabRowView, views::Button)

 public:
  struct Delegate {
    // The whole row, not an index: a cold row has no tab index, so the
    // owner has to dispatch on entry_id instead.
    base::RepeatingCallback<void(const SidebarRow& row)> activate;
    base::RepeatingCallback<void(const SidebarRow& row)> close;
    // Called while dragging: the row at `from` wants to move to `to`.
    base::RepeatingCallback<void(int from, int to)> drag_move;
    // A finished inline rename, against the entry the edit was started on
    // rather than whatever this row draws when it ends: the view is pooled by
    // position, so the two are not the same thing. Never called for a row
    // with no entry.
    base::RepeatingCallback<void(EntryId id, const std::u16string& title)>
        rename;
    base::RepeatingCallback<void(const SidebarRow& row)> return_to_pinned_url;
    // `source` is this view, so the menu's Rename item can start the edit on
    // the row it was opened from. `point` is in screen coordinates.
    base::RepeatingCallback<void(TabRowView* source,
                                 const SidebarRow& row,
                                 const gfx::Point& point)>
        show_context_menu;
  };

  // Ids for the two hover buttons, so a test can ask which one has the slot.
  enum ViewId { kRevertButtonId = 1, kCloseButtonId };

  explicit TabRowView(Delegate delegate);
  TabRowView(const TabRowView&) = delete;
  TabRowView& operator=(const TabRowView&) = delete;
  ~TabRowView() override;

  void SetRow(const SidebarRow& row);
  base::WeakPtr<TabRowView> GetWeakPtr() { return weak_factory_.GetWeakPtr(); }
  int tab_index() const { return row_.tab_index; }
  const SidebarRow& row() const { return row_; }

  // Swaps the title label for a field seeded with the current title. A row
  // with no entry has nothing to carry the name, so this does nothing.
  void BeginRename();
  bool is_renaming() const { return rename_field_ != nullptr; }
  // The entry the open edit belongs to; invalid when nothing is being renamed.
  EntryId renaming_entry_id() const { return renaming_entry_id_; }

  // views::Button / View:
  bool OnMousePressed(const ui::MouseEvent& event) override;
  bool OnMouseDragged(const ui::MouseEvent& event) override;
  void OnMouseReleased(const ui::MouseEvent& event) override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;
  bool OnKeyPressed(const ui::KeyEvent& event) override;
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
  void UpdateTrailingButtons();
  // Takes the field away without an outcome, for when this view stops being
  // the view for the entry the edit was started on.
  void AbandonRename();
  void OnRenameFinished(EntryId id, bool commit, const std::u16string& title);
  // Runs the "return to pinned URL" command off copies, because it rebuilds
  // the list and can destroy this view before the call returns.
  void Revert();

  Delegate delegate_;
  SidebarRow row_;
  bool hovered_ = false;
  bool dragging_ = false;
  gfx::Point drag_start_;

  raw_ptr<views::ImageView> favicon_ = nullptr;
  raw_ptr<views::Throbber> throbber_ = nullptr;
  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<views::ImageView> audio_ = nullptr;
  raw_ptr<views::ImageButton> revert_ = nullptr;
  raw_ptr<views::ImageButton> close_ = nullptr;
  raw_ptr<RenameField> rename_field_ = nullptr;
  // Captured when the edit starts. The pool re-points a row view at a
  // different entry whenever the model moves — including from another window
  // on the same profile — and an open field must not follow it.
  EntryId renaming_entry_id_;
  base::WeakPtrFactory<TabRowView> weak_factory_{this};
};

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_TAB_ROW_VIEW_H_
