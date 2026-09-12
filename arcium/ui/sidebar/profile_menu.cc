// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/profile_menu.h"

#include <optional>
#include <utility>
#include <vector>

#include "arcium/browser/model/arcium_profile.h"
#include "arcium/ui/sidebar/profile_colors.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "ui/base/models/dialog_model.h"
#include "ui/base/models/simple_combobox_model.h"
#include "ui/views/bubble/bubble_border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/bubble/bubble_dialog_model_host.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace arcium {

namespace {

constexpr int kProfileGroup = 1;
constexpr int kColorGroup = 2;

DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kProfileNameField);
DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kProfileColorField);

std::u16string CountPhrase(int count,
                           const std::u16string& one,
                           const std::u16string& many) {
  return base::StrCat(
      {base::NumberToString16(count), u" ", count == 1 ? one : many});
}

std::optional<int> ColorForCommand(int command_id) {
  const int preset = command_id - ProfileMenu::kColorFirst;
  if (preset < 0 || static_cast<size_t>(preset) >= ProfileColors().size()) {
    return std::nullopt;
  }
  return preset;
}

// The colour combobox's items, in palette order. Its initial selection is
// always the model's own default -- index 0 -- because
// ui::DialogModelCombobox::Params in this tree has no way to override it and
// ui::SimpleComboboxModel::GetDefaultIndex() always answers 0; a "suggested"
// colour that no space has yet would need a combobox model of its own, which
// is more than this dialog earns.
std::unique_ptr<ui::SimpleComboboxModel> MakeColorComboboxModel() {
  std::vector<ui::SimpleComboboxModel::Item> items;
  for (const ProfileColor& color : ProfileColors()) {
    items.emplace_back(std::u16string(color.name));
  }
  return std::make_unique<ui::SimpleComboboxModel>(std::move(items));
}

}  // namespace

ProfileMenu::ProfileMenu(SidebarModel* model, views::View* anchor)
    : model_(model), anchor_(anchor) {
  color_menu_ = std::make_unique<ui::SimpleMenuModel>(this);
  const base::span<const ProfileColor> colors = ProfileColors();
  for (size_t i = 0; i < colors.size(); ++i) {
    color_menu_->AddRadioItem(kColorFirst + static_cast<int>(i),
                              std::u16string(colors[i].name), kColorGroup);
  }
  menu_ = std::make_unique<ui::SimpleMenuModel>(this);
}

ProfileMenu::~ProfileMenu() {
  CloseDialog();
}

void ProfileMenu::SetSpace(SpaceId space) {
  space_ = space;
  menu_->Clear();
  const std::vector<SidebarProfile> profiles = model_->profiles();
  for (size_t i = 0; i < profiles.size(); ++i) {
    menu_->AddRadioItem(kProfileFirst + static_cast<int>(i), profiles[i].name,
                        kProfileGroup);
  }
  menu_->AddSeparator(ui::NORMAL_SEPARATOR);
  menu_->AddItem(kNewProfile, u"New profile…");
  menu_->AddItem(kRenameProfile, u"Rename profile…");
  menu_->AddSubMenu(kChangeColor, u"Change colour", color_menu_.get());
  menu_->AddSeparator(ui::NORMAL_SEPARATOR);
  menu_->AddItem(kClearData, u"Clear this profile's data…");
  const std::optional<SidebarProfile> profile = MenuProfile();
  // Default is the one profile every space can always fall back to.
  if (profile && profile->id != DefaultProfileId()) {
    menu_->AddItem(kDeleteProfile, u"Delete profile…");
  }
}

bool ProfileMenu::IsCommandIdChecked(int command_id) const {
  const std::optional<SidebarProfile> profile = MenuProfile();
  if (!profile) {
    return false;
  }
  if (const std::optional<int> color = ColorForCommand(command_id)) {
    return profile->color == *color;
  }
  const int index = command_id - kProfileFirst;
  const std::vector<SidebarProfile> profiles = model_->profiles();
  return index >= 0 && static_cast<size_t>(index) < profiles.size() &&
         profiles[static_cast<size_t>(index)].id == profile->id;
}

void ProfileMenu::ExecuteCommand(int command_id, int event_flags) {
  const std::optional<SidebarProfile> profile = MenuProfile();
  if (!profile) {
    return;
  }
  if (const std::optional<int> color = ColorForCommand(command_id)) {
    model_->SetProfileColor(profile->id, *color);
    return;
  }
  if (command_id >= kProfileFirst) {
    const std::vector<SidebarProfile> profiles = model_->profiles();
    const size_t index = static_cast<size_t>(command_id - kProfileFirst);
    if (index < profiles.size()) {
      model_->SetSpaceProfile(space_, profiles[index].id);
    }
    return;
  }
  switch (command_id) {
    case kNewProfile:
      AskForNewProfile();
      return;
    case kRenameProfile:
      AskForRename(*profile);
      return;
    case kClearData:
      pending_profile_ = profile->id;
      Confirm(Pending::kClear, u"Clear this profile's data?",
              base::StrCat({u"You'll be signed out of every site in “",
                            profile->name,
                            u"”. History, passwords and bookmarks are "
                            u"shared by every profile and stay."}),
              u"Clear data");
      return;
    case kDeleteProfile: {
      int spaces = 0;
      int tabs = 0;
      for (const SidebarSpace& space : model_->spaces()) {
        if (space.profile_id == profile->id) {
          ++spaces;
          tabs += space.open_tab_count;
        }
      }
      pending_profile_ = profile->id;
      Confirm(Pending::kDelete, u"Delete profile?",
              base::StrCat({u"“", profile->name,
                            u"” and its logins will be erased for good. ",
                            CountPhrase(spaces, u"space", u"spaces"),
                            u" will switch to Default, and ",
                            CountPhrase(tabs, u"open tab", u"open tabs"),
                            u" will reopen signed out."}),
              u"Delete profile");
      return;
    }
    default:
      return;
  }
}

const SidebarSpace* ProfileMenu::MenuSpace(
    const std::vector<SidebarSpace>& spaces) const {
  for (const SidebarSpace& space : spaces) {
    if (space.id == space_) {
      return &space;
    }
  }
  return nullptr;
}

std::optional<SidebarProfile> ProfileMenu::MenuProfile() const {
  const std::vector<SidebarSpace> spaces = model_->spaces();
  const SidebarSpace* space = MenuSpace(spaces);
  if (!space) {
    return std::nullopt;
  }
  for (const SidebarProfile& profile : model_->profiles()) {
    if (profile.id == space->profile_id) {
      return profile;
    }
  }
  return std::nullopt;
}

void ProfileMenu::AskForNewProfile() {
  CloseDialog();
  pending_ = Pending::kNewProfile;
  const int serial = ++serial_;
  if (!anchor_ || !anchor_->GetWidget()) {
    return;
  }
  base::WeakPtr<ProfileMenu> weak = weak_factory_.GetWeakPtr();
  ui::DialogModel::Builder builder;
  ui::DialogModel* dialog_model = builder.model();
  auto dialog =
      builder.SetTitle(u"New profile")
          .AddParagraph(ui::DialogModelLabel(
              u"A profile has its own logins. History, passwords and "
              u"bookmarks are shared."))
          .AddTextfield(kProfileNameField, u"Name", std::u16string())
          .AddCombobox(kProfileColorField, u"Colour", MakeColorComboboxModel())
          .AddOkButton(
              base::BindOnce(
                  [](base::WeakPtr<ProfileMenu> weak, int serial,
                     ui::DialogModel* model) {
                    if (!weak) {
                      return;
                    }
                    weak->OnNewProfile(
                        serial,
                        model->GetTextfieldByUniqueId(kProfileNameField)
                            ->text(),
                        static_cast<int>(
                            model->GetComboboxByUniqueId(kProfileColorField)
                                ->selected_index()));
                  },
                  weak, serial, dialog_model),
              ui::DialogModel::Button::Params().SetLabel(u"Create"))
          .AddCancelButton(base::DoNothing())
          .Build();
  auto bubble = std::make_unique<views::BubbleDialogModelHost>(
      std::move(dialog), anchor_, views::BubbleBorder::BOTTOM_LEFT);
  views::Widget* widget = views::BubbleDialogDelegate::CreateBubbleDeprecated(
      std::move(bubble), views::Widget::InitParams::NATIVE_WIDGET_OWNS_WIDGET);
  dialog_widget_ = widget->GetWeakPtr();
  widget->Show();
}

void ProfileMenu::AskForRename(const SidebarProfile& profile) {
  CloseDialog();
  pending_ = Pending::kRename;
  pending_profile_ = profile.id;
  const int serial = ++serial_;
  if (!anchor_ || !anchor_->GetWidget()) {
    return;
  }
  base::WeakPtr<ProfileMenu> weak = weak_factory_.GetWeakPtr();
  ui::DialogModel::Builder builder;
  ui::DialogModel* dialog_model = builder.model();
  auto dialog =
      builder.SetTitle(u"Rename profile")
          .AddTextfield(kProfileNameField, u"Name", profile.name)
          .AddOkButton(
              base::BindOnce(
                  [](base::WeakPtr<ProfileMenu> weak, int serial,
                     ui::DialogModel* model) {
                    if (weak) {
                      weak->OnRename(
                          serial,
                          model->GetTextfieldByUniqueId(kProfileNameField)
                              ->text());
                    }
                  },
                  weak, serial, dialog_model),
              ui::DialogModel::Button::Params().SetLabel(u"Rename"))
          .AddCancelButton(base::DoNothing())
          .Build();
  auto bubble = std::make_unique<views::BubbleDialogModelHost>(
      std::move(dialog), anchor_, views::BubbleBorder::BOTTOM_LEFT);
  views::Widget* widget = views::BubbleDialogDelegate::CreateBubbleDeprecated(
      std::move(bubble), views::Widget::InitParams::NATIVE_WIDGET_OWNS_WIDGET);
  dialog_widget_ = widget->GetWeakPtr();
  widget->Show();
}

void ProfileMenu::Confirm(Pending kind,
                          const std::u16string& title,
                          const std::u16string& text,
                          const std::u16string& ok_label) {
  CloseDialog();
  pending_ = kind;
  pending_text_ = text;
  const int serial = ++serial_;
  if (!anchor_ || !anchor_->GetWidget()) {
    return;
  }
  base::WeakPtr<ProfileMenu> weak = weak_factory_.GetWeakPtr();
  auto dialog =
      ui::DialogModel::Builder()
          .SetTitle(title)
          .AddParagraph(ui::DialogModelLabel(text))
          .AddOkButton(base::BindOnce(&ProfileMenu::OnAnswer, weak, serial,
                                      /*accept=*/true),
                       ui::DialogModel::Button::Params().SetLabel(ok_label))
          .AddCancelButton(base::BindOnce(&ProfileMenu::OnAnswer, weak, serial,
                                          /*accept=*/false))
          .SetCloseActionCallback(base::BindOnce(&ProfileMenu::OnAnswer, weak,
                                                 serial, /*accept=*/false))
          // Enter keeps the data. The default button of a dialog that
          // destroys something must be the one that does not.
          .OverrideDefaultButton(ui::mojom::DialogButton::kCancel)
          .Build();
  auto bubble = std::make_unique<views::BubbleDialogModelHost>(
      std::move(dialog), anchor_, views::BubbleBorder::BOTTOM_LEFT);
  views::Widget* widget = views::BubbleDialogDelegate::CreateBubbleDeprecated(
      std::move(bubble), views::Widget::InitParams::NATIVE_WIDGET_OWNS_WIDGET);
  dialog_widget_ = widget->GetWeakPtr();
  widget->Show();
}

void ProfileMenu::OnAnswer(int serial, bool accept) {
  if (serial != serial_ ||
      (pending_ != Pending::kClear && pending_ != Pending::kDelete)) {
    return;
  }
  const Pending kind = pending_;
  const ProfileId profile = pending_profile_;
  pending_ = Pending::kNone;
  pending_text_.clear();
  if (!accept) {
    return;
  }
  if (kind == Pending::kClear) {
    model_->ClearProfileData(profile);
  } else {
    model_->DeleteProfile(profile);
  }
}

void ProfileMenu::OnNewProfile(int serial,
                               const std::u16string& name,
                               int color) {
  if (serial != serial_ || pending_ != Pending::kNewProfile) {
    return;
  }
  pending_ = Pending::kNone;
  // A profile with no name has nothing to list it by.
  if (!name.empty()) {
    model_->CreateProfileForSpace(space_, name, color);
  }
}

void ProfileMenu::OnRename(int serial, const std::u16string& name) {
  if (serial != serial_ || pending_ != Pending::kRename) {
    return;
  }
  pending_ = Pending::kNone;
  if (!name.empty()) {
    model_->RenameProfile(pending_profile_, name);
  }
}

void ProfileMenu::CloseDialog() {
  if (dialog_widget_) {
    dialog_widget_->Close();
  }
}

void ProfileMenu::AnswerForTesting(bool accept) {
  OnAnswer(serial_, accept);
}

void ProfileMenu::SubmitNewProfileForTesting(const std::u16string& name,
                                             int color) {
  OnNewProfile(serial_, name, color);
}

void ProfileMenu::SubmitRenameForTesting(const std::u16string& name) {
  OnRename(serial_, name);
}

}  // namespace arcium
