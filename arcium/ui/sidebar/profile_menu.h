// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_PROFILE_MENU_H_
#define ARCIUM_UI_SIDEBAR_PROFILE_MENU_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "arcium/ui/sidebar/sidebar_model.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "ui/menus/simple_menu_model.h"

namespace views {
class View;
class Widget;
}  // namespace views

namespace arcium {

// The space menu's "Profile" submenu, and the dialogs it opens: a new
// profile's name and colour, a rename, and the confirmations for clearing
// and deleting. Its own file because the space bar is already at its size
// limit; the bar owns one and adds its model as a submenu.
//
// Acts on the space the space menu was opened on, and on that space's
// profile. Rebuilt each time the menu opens, because the list of profiles
// changes and a SimpleMenuModel's items are fixed once built.
class ProfileMenu : public ui::SimpleMenuModel::Delegate {
 public:
  enum Command {
    kNewProfile = 1,
    kRenameProfile,
    kChangeColor,
    kClearData,
    kDeleteProfile,
    // kColorFirst + i chooses preset i of profile_colors.h.
    kColorFirst = 100,
    // kProfileFirst + i puts the space on the i-th profile.
    kProfileFirst = 200,
  };

  // `model` must outlive this. `anchor` is where dialogs point; null means
  // there is nowhere to show one, so they stay pending for a test to answer.
  ProfileMenu(SidebarModel* model, views::View* anchor);
  ProfileMenu(const ProfileMenu&) = delete;
  ProfileMenu& operator=(const ProfileMenu&) = delete;
  ~ProfileMenu() override;

  ui::SimpleMenuModel* model() { return menu_.get(); }
  // Points the menu at `space` and rebuilds its items.
  void SetSpace(SpaceId space);

  // ui::SimpleMenuModel::Delegate:
  bool IsCommandIdChecked(int command_id) const override;
  void ExecuteCommand(int command_id, int event_flags) override;

  // The sentence the pending confirmation shows; empty when none is.
  const std::u16string& pending_text_for_testing() const {
    return pending_text_;
  }
  // Answers the pending confirmation as its buttons would.
  void AnswerForTesting(bool accept);
  // Submits the pending "New profile…" or rename dialog as its OK would.
  void SubmitNewProfileForTesting(const std::u16string& name, int color);
  void SubmitRenameForTesting(const std::u16string& name);

 private:
  enum class Pending { kNone, kNewProfile, kRename, kClear, kDelete };

  // The space the menu acts on, and its profile, if both still exist.
  const SidebarSpace* MenuSpace(const std::vector<SidebarSpace>& spaces) const;
  std::optional<SidebarProfile> MenuProfile() const;

  void AskForNewProfile();
  void AskForRename(const SidebarProfile& profile);
  void Confirm(Pending kind,
               const std::u16string& title,
               const std::u16string& text,
               const std::u16string& ok_label);
  void OnAnswer(int serial, bool accept);
  void OnNewProfile(int serial, const std::u16string& name, int color);
  void OnRename(int serial, const std::u16string& name);
  void CloseDialog();

  raw_ptr<SidebarModel> model_;
  raw_ptr<views::View> anchor_;
  SpaceId space_;

  Pending pending_ = Pending::kNone;
  ProfileId pending_profile_;
  std::u16string pending_text_;
  int serial_ = 0;
  base::WeakPtr<views::Widget> dialog_widget_;

  // Declared before `menu_`, which keeps a bare pointer to it.
  std::unique_ptr<ui::SimpleMenuModel> color_menu_;
  std::unique_ptr<ui::SimpleMenuModel> menu_;

  base::WeakPtrFactory<ProfileMenu> weak_factory_{this};
};

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_PROFILE_MENU_H_
