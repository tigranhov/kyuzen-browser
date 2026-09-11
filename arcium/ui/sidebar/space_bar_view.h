// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_SPACE_BAR_VIEW_H_
#define ARCIUM_UI_SIDEBAR_SPACE_BAR_VIEW_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "arcium/ui/sidebar/sidebar_model.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/view.h"

namespace views {
class ImageButton;
class MenuRunner;
class Widget;
}  // namespace views

namespace arcium {

class RenameField;

// Bottom bar: one chip per space, the button that makes a new one, and the
// profile badge. Drawn from SidebarModel::spaces() and redrawn on every
// change, so nothing a chip shows is decided here.
//
// The context menu acts on the chip it was opened on, not on the active
// space; that is what lets "Move left" reorder a space you are not in. The
// archive timeout is the exception, because the model holds only the active
// space's.
class SpaceBarView : public views::View,
                     public views::ContextMenuController,
                     public ui::SimpleMenuModel::Delegate,
                     public SidebarModel::Observer {
  METADATA_HEADER(SpaceBarView, views::View)

 public:
  enum MenuCommand {
    kRename = 1,
    // The gradient submenu. A custom theme editor replaces it later.
    kEditTheme,
    kChangeIcon,
    kArchiveTimeout,
    kDelete,
    // The four values of R2.3, as one radio group.
    kTimeoutTwelveHours,
    kTimeoutOneDay,
    kTimeoutSevenDays,
    kTimeoutNever,
    // After the timeouts, so no value above moves.
    kMoveLeft,
    kMoveRight,
    // kGradientFirst + i chooses preset i of space_gradients.h.
    kGradientFirst = 200,
  };

  // One space's chip: its icon, or the first letter of its name. It remembers
  // which space it draws and whether that space is on screen, because a chip
  // is reused for whichever space ends up at its position.
  class SpaceChip : public views::LabelButton {
    METADATA_HEADER(SpaceChip, views::LabelButton)

   public:
    SpaceChip();
    SpaceChip(const SpaceChip&) = delete;
    SpaceChip& operator=(const SpaceChip&) = delete;
    ~SpaceChip() override;

    void SetSpace(const SidebarSpace& space);
    SpaceId space_id() const { return space_id_; }
    bool is_active() const { return is_active_; }

   private:
    SpaceId space_id_;
    bool is_active_ = false;
  };

  // `model` must outlive this view; the sidebar owns both.
  explicit SpaceBarView(SidebarModel* model);
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
  bool IsCommandIdChecked(int command_id) const override;
  void ExecuteCommand(int command_id, int event_flags) override;

  // SidebarModel::Observer:
  void OnSidebarModelChanged() override;

  // views::View:
  void OnThemeChanged() override;

  const std::vector<raw_ptr<SpaceChip>>& chips_for_testing() const {
    return chips_;
  }
  views::ImageButton* add_button_for_testing() { return add_button_; }
  // Points the menu at `id`, which is everything a right-click on its chip
  // does before the menu runs. Running it is left out: a context menu on
  // macOS spins a nested loop that a unit test cannot get out of.
  void BuildMenuForTesting(SpaceId id) { menu_space_ = id; }
  // The sentence the pending delete confirmation shows; empty when none is.
  const std::u16string& confirm_text_for_testing() const {
    return confirm_text_;
  }
  // Answers the pending confirmation the way its buttons do, without the
  // bubble, which a bar that is in no widget has nowhere to show.
  void ConfirmDeleteForTesting(bool accept);

 private:
  enum class EditKind { kName, kIcon };

  void Rebuild();
  void OnChipPressed(SpaceChip* chip);
  SpaceChip* ChipFor(SpaceId id) const;
  // Where the menu's space is in `spaces`, if it still exists.
  std::optional<size_t> MenuSpaceIndex(
      const std::vector<SidebarSpace>& spaces) const;

  void BeginEdit(const SidebarSpace& space, EditKind kind);
  void AbandonEdit();
  void OnEditFinished(SpaceId id,
                      EditKind kind,
                      bool commit,
                      const std::u16string& text);
  // Keeps the open edit where its space's chip is, after a rebuild that may
  // have given that chip to another space.
  void PlaceEditField();

  void ShowConfirmation(SpaceId id);
  void OnConfirmation(int serial, bool accept);

  raw_ptr<SidebarModel> model_;
  base::ScopedObservation<SidebarModel, SidebarModel::Observer> observation_{
      this};
  std::vector<raw_ptr<SpaceChip>> chips_;
  raw_ptr<views::ImageButton> add_button_ = nullptr;
  raw_ptr<views::View> profile_badge_ = nullptr;

  // The space the context menu was opened on.
  SpaceId menu_space_;

  raw_ptr<RenameField> edit_field_ = nullptr;
  SpaceId editing_space_;

  // The space a delete is waiting on, and what the confirmation says about
  // it. `confirm_serial_` tells an answer from a confirmation that has since
  // been replaced apart from the one on screen.
  SpaceId pending_delete_;
  std::u16string confirm_text_;
  int confirm_serial_ = 0;
  base::WeakPtr<views::Widget> confirm_widget_;

  // Declared before the menu, so they are destroyed after it:
  // SimpleMenuModel::AddSubMenu keeps a bare pointer to each and owns neither.
  std::unique_ptr<ui::SimpleMenuModel> timeout_menu_;
  std::unique_ptr<ui::SimpleMenuModel> gradient_menu_;
  std::unique_ptr<ui::SimpleMenuModel> menu_model_;
  std::unique_ptr<views::MenuRunner> menu_runner_;

  base::WeakPtrFactory<SpaceBarView> weak_factory_{this};
};

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_SPACE_BAR_VIEW_H_
