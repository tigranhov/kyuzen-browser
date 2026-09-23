// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_TAB_ROW_VIEW_H_
#define ARCIUM_UI_SIDEBAR_TAB_ROW_VIEW_H_

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
class OSExchangeData;
}

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
class TabRowView : public views::Button,
                   public views::ContextMenuController,
                   public views::DragController {
  METADATA_HEADER(TabRowView, views::Button)

 public:
  struct Delegate {
    // The whole row, not an index: a cold row has no tab index, so the
    // owner has to dispatch on entry_id instead.
    base::RepeatingCallback<void(const SidebarRow& row)> activate;
    base::RepeatingCallback<void(const SidebarRow& row)> close;
    // A finished inline rename, against the row the edit was started on
    // rather than whatever this view draws when it ends: the view is pooled
    // by position, so the two are not the same thing. The whole row travels
    // because a Today tab has no entry to name it by, and its strip index
    // alone would be a lie by the time this runs -- the owner dispatches on
    // entry_id, and falls back to the index checked against the row's URL.
    base::RepeatingCallback<void(const SidebarRow& row,
                                 const std::u16string& title)>
        rename;
    base::RepeatingCallback<void(const SidebarRow& row)> return_to_pinned_url;
    // `source` is this view, so the menu's Rename item can start the edit on
    // the row it was opened from. `point` is in screen coordinates.
    base::RepeatingCallback<void(TabRowView* source,
                                 const SidebarRow& row,
                                 const gfx::Point& point)>
        show_context_menu;
    // This row is about to be dragged. Views has no ambient "a drag is
    // running" signal and the sidebar's empty sections need one, so the
    // source says so; see RowDragSession.
    base::RepeatingCallback<void(const RowDragData& payload)> drag_started;
    // The pointer came onto the row or left it. A split row shows its grip
    // while either half is under the pointer.
    base::RepeatingClosure hover_changed;
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

  // What UpdateVisuals last set the title's colour and the favicon's image
  // to, so a test can check the cold-row dimming without a mock standing in
  // for either.
  views::Label* title_for_testing() { return title_; }
  views::ImageView* favicon_for_testing() { return favicon_; }

  // Marks this row as the one a drag would split with: its right half is
  // tinted, the pane the dragged page would take. The list sets it while
  // the pointer is over the middle of the row, and clears it when the
  // pointer moves on.
  void SetSplitTarget(bool target);
  bool is_split_target() const { return split_target_; }
  bool hovered() const { return hovered_; }

  // views::Button / View:
  bool OnMousePressed(const ui::MouseEvent& event) override;
  void OnMouseReleased(const ui::MouseEvent& event) override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;
  bool OnKeyPressed(const ui::KeyEvent& event) override;
  void OnThemeChanged() override;
  void PaintButtonContents(gfx::Canvas* canvas) override;
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;

  // views::ContextMenuController:
  void ShowContextMenuForViewImpl(
      views::View* source,
      const gfx::Point& point,
      ui::mojom::MenuSourceType source_type) override;

  // views::DragController:
  void WriteDragDataForView(views::View* sender,
                            const gfx::Point& press_pt,
                            ui::OSExchangeData* data) override;
  int GetDragOperationsForView(views::View* sender,
                               const gfx::Point& p) override;
  bool CanStartDragForView(views::View* sender,
                           const gfx::Point& press_pt,
                           const gfx::Point& p) override;

 private:
  void UpdateVisuals();
  void UpdateTrailingButtons();
  // Takes the field away without an outcome, for when this view stops being
  // the view for the entry the edit was started on.
  void AbandonRename();
  void OnRenameFinished(const SidebarRow& row,
                        bool commit,
                        const std::u16string& title);
  // Runs the "return to pinned URL" command off copies, because it rebuilds
  // the list and can destroy this view before the call returns.
  void Revert();

  // Opens the rename a double-click asks for. Returns whether it did, which
  // is what tells the press whether it has already been spent.
  bool BeginRenameFromDoubleClick();

  Delegate delegate_;
  SidebarRow row_;
  bool hovered_ = false;
  bool split_target_ = false;
  // Set for the press that opened a rename, and read by the two things that
  // press must not also do: start a drag, and fire the button a second time.
  // A double-click's second press and a drag's first press are the same
  // press, and the drag threshold is the only thing between them — so the
  // decision is taken here, at press time, before the threshold can be
  // crossed, rather than left to whichever handler runs first.
  bool rename_began_on_press_ = false;

  raw_ptr<views::ImageView> favicon_ = nullptr;
  raw_ptr<views::Throbber> throbber_ = nullptr;
  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<views::ImageView> audio_ = nullptr;
  // Two panes, shown on a row whose split partner is not its neighbour in
  // the list. Where the two are neighbours the list draws a bracket joining
  // them instead, which says the same thing in one mark rather than two.
  raw_ptr<views::ImageView> split_ = nullptr;
  raw_ptr<views::ImageButton> revert_ = nullptr;
  raw_ptr<views::ImageButton> close_ = nullptr;
  raw_ptr<RenameField> rename_field_ = nullptr;
  // Captured when the edit starts. The pool re-points a row view at a
  // different entry whenever the model moves — including from another window
  // on the same profile — and an open field must not follow it.
  EntryId renaming_entry_id_;
  // The row as it was when the edit opened, which is what the commit names.
  SidebarRow renaming_row_;
  base::WeakPtrFactory<TabRowView> weak_factory_{this};
};

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_TAB_ROW_VIEW_H_
