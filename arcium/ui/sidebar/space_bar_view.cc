// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/space_bar_view.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "arcium/ui/sidebar/profile_colors.h"
#include "arcium/ui/sidebar/profile_menu.h"
#include "arcium/ui/sidebar/rename_field.h"
#include "arcium/ui/sidebar/sidebar_colors.h"
#include "arcium/ui/sidebar/sidebar_metrics.h"
#include "arcium/ui/sidebar/space_gradients.h"
#include "arcium/ui/sidebar/vector_icons.h"
#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/third_party/icu/icu_utf.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/dialog_model.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/bubble/bubble_border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/bubble/bubble_dialog_model_host.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/layout/flex_layout_types.h"
#include "ui/views/layout/layout_types.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/widget/widget.h"

namespace arcium {

namespace {

constexpr float kChipRadius = 8;
// Radio groups, so exactly one item of each is checked.
constexpr int kTimeoutGroup = 1;
constexpr int kGradientGroup = 2;
// Wide enough for a space's name while it is being typed; the icon field
// only ever holds one emoji.
constexpr int kNameFieldChars = 10;
constexpr int kIconFieldChars = 3;

// The icon, or else the name's first letter. A letter outside the Basic
// Multilingual Plane is two UTF-16 units, and half of one draws as a box.
std::u16string ChipText(const SidebarSpace& space) {
  if (!space.icon.empty() || space.name.empty()) {
    return space.icon;
  }
  const size_t length =
      CBU16_IS_LEAD(space.name[0]) && space.name.size() > 1 ? 2 : 1;
  return space.name.substr(0, length);
}

}  // namespace

SpaceBarView::SpaceChip::SpaceChip()
    : views::LabelButton(views::Button::PressedCallback()) {
  SetMinSize(gfx::Size(metrics::kSpaceChipHeight, metrics::kSpaceChipHeight));
  SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(0, 6)));
  SetHorizontalAlignment(gfx::ALIGN_CENTER);
}

SpaceBarView::SpaceChip::~SpaceChip() = default;

void SpaceBarView::SpaceChip::SetSpace(const SidebarSpace& space) {
  space_id_ = space.id;
  is_active_ = space.is_active;
  SetText(ChipText(space));
  // The chip shows one glyph, so the name has to reach the user some other
  // way: on hover, and as what a screen reader says.
  SetTooltipText(space.name);
  // Never unnamed: a focusable view with no name cannot be announced, and a
  // space with no name draws no letter either, so the chip would be silent.
  GetViewAccessibility().SetName(
      space.name.empty() ? std::u16string(u"Unnamed space") : space.name);
  if (is_active_) {
    SetBackground(views::CreateRoundedRectBackground(
        kColorArciumSpaceChipActiveBackground, kChipRadius));
    SetEnabledTextColors(kColorArciumRowTextActive);
  } else {
    SetBackground(nullptr);
    SetEnabledTextColors(kColorArciumRowTextSecondary);
  }
}

BEGIN_METADATA(SpaceBarView, SpaceChip)
END_METADATA

SpaceBarView::SpaceBarView(SidebarModel* model) : model_(model) {
  auto* layout = SetLayoutManager(std::make_unique<views::FlexLayout>());
  layout->SetOrientation(views::LayoutOrientation::kHorizontal)
      .SetCrossAxisAlignment(views::LayoutAlignment::kCenter)
      .SetInteriorMargin(gfx::Insets::TLBR(8, 0, 0, 0))
      .SetDefault(views::kMarginsKey, gfx::Insets::VH(0, 3));

  // The chips go in front of this button, so it always ends the row.
  add_button_ = AddChildView(std::make_unique<views::ImageButton>(
      base::BindRepeating(&SidebarModel::AddSpace, base::Unretained(model_),
                          std::u16string(u"New space"))));
  add_button_->SetImageModel(
      views::Button::STATE_NORMAL,
      ui::ImageModel::FromVectorIcon(kAddIcon, kColorArciumRowTextSecondary,
                                     metrics::kFaviconSize));
  add_button_->SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
  add_button_->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
  add_button_->SetPreferredSize(
      gfx::Size(metrics::kSpaceChipHeight, metrics::kSpaceChipHeight));
  add_button_->SetTooltipText(u"New space");
  add_button_->GetViewAccessibility().SetName(u"New space");

  auto* spacer = AddChildView(std::make_unique<views::View>());
  spacer->SetProperty(
      views::kFlexBehaviorKey,
      views::FlexSpecification(views::LayoutOrientation::kHorizontal,
                               views::MinimumFlexSizeRule::kScaleToZero,
                               views::MaximumFlexSizeRule::kUnbounded));

  profile_badge_ = AddChildView(std::make_unique<views::View>());
  profile_badge_->SetPreferredSize(
      gfx::Size(metrics::kProfileBadgeSize, metrics::kProfileBadgeSize));
  // A plain View has no role, and the accessibility tree refuses a name with
  // none: it would have nothing to tell a screen reader the name is a name
  // of. The badge is a small solid disc standing in for the profile, which
  // is what an image role means here.
  profile_badge_->GetViewAccessibility().SetRole(ax::mojom::Role::kImage);

  profile_menu_ = std::make_unique<ProfileMenu>(model_, profile_badge_);

  timeout_menu_ = std::make_unique<ui::SimpleMenuModel>(this);
  timeout_menu_->AddRadioItem(kTimeoutTwelveHours, u"12 hours", kTimeoutGroup);
  timeout_menu_->AddRadioItem(kTimeoutOneDay, u"1 day", kTimeoutGroup);
  timeout_menu_->AddRadioItem(kTimeoutSevenDays, u"7 days", kTimeoutGroup);
  timeout_menu_->AddRadioItem(kTimeoutNever, u"Never", kTimeoutGroup);

  gradient_menu_ = std::make_unique<ui::SimpleMenuModel>(this);
  const base::span<const SpaceGradient> gradients = SpaceGradients();
  for (size_t preset = 0; preset < gradients.size(); ++preset) {
    gradient_menu_->AddRadioItem(kGradientFirst + static_cast<int>(preset),
                                 std::u16string(gradients[preset].name),
                                 kGradientGroup);
  }

  menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  menu_model_->AddItem(kRename, u"Rename space");
  menu_model_->AddSubMenu(kEditTheme, u"Edit theme", gradient_menu_.get());
  menu_model_->AddItem(kChangeIcon, u"Change icon");
  menu_model_->AddSubMenu(kArchiveTimeout, u"Archive Today tabs after",
                          timeout_menu_.get());
  menu_model_->AddSubMenu(kProfile, u"Profile", profile_menu_->model());
  menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  menu_model_->AddItem(kMoveLeft, u"Move left");
  menu_model_->AddItem(kMoveRight, u"Move right");
  menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  menu_model_->AddItem(kDelete, u"Delete space");

  observation_.Observe(model_);
  Rebuild();
}

SpaceBarView::~SpaceBarView() {
  // The bubble is anchored to one of the chips about to go. Its callbacks are
  // bound to a weak pointer, so closing it cannot reach back into this view.
  if (confirm_widget_) {
    confirm_widget_->Close();
  }
}

void SpaceBarView::OnSidebarModelChanged() {
  Rebuild();
}

void SpaceBarView::OnThemeChanged() {
  views::View::OnThemeChanged();
  UpdateProfileBadge();
}

void SpaceBarView::ConfirmDeleteForTesting(bool accept) {
  OnConfirmation(confirm_serial_, accept);
}

void SpaceBarView::Rebuild() {
  const std::vector<SidebarSpace> spaces = model_->spaces();
  // Chips are reused by position rather than rebuilt. Pressing one switches
  // spaces, which notifies before the press returns, and a fresh set of chips
  // would destroy the very button whose callback is still running.
  while (chips_.size() > spaces.size()) {
    SpaceChip* chip = chips_.back().get();
    chips_.pop_back();
    RemoveChildViewT(chip);
  }
  while (chips_.size() < spaces.size()) {
    SpaceChip* chip = AddChildViewAt(std::make_unique<SpaceChip>(),
                                     GetIndexOf(add_button_).value());
    chip->SetCallback(base::BindRepeating(&SpaceBarView::OnChipPressed,
                                          base::Unretained(this),
                                          base::Unretained(chip)));
    chip->set_context_menu_controller(this);
    chips_.push_back(chip);
  }
  for (size_t i = 0; i < spaces.size(); ++i) {
    chips_[i]->SetSpace(spaces[i]);
  }
  UpdateProfileBadge();
  PlaceEditField();
  InvalidateLayout();
}

void SpaceBarView::OnChipPressed(SpaceChip* chip) {
  // Read at press time: the chip draws whichever space is at its position
  // now, which need not be the one it was made for.
  model_->SwitchToSpace(chip->space_id());
}

SpaceBarView::SpaceChip* SpaceBarView::ChipFor(SpaceId id) const {
  for (const raw_ptr<SpaceChip>& chip : chips_) {
    if (chip->space_id() == id) {
      return chip.get();
    }
  }
  return nullptr;
}

void SpaceBarView::SetMenuSpace(SpaceId id) {
  menu_space_ = id;
  profile_menu_->SetSpace(id);
}

std::optional<size_t> SpaceBarView::MenuSpaceIndex(
    const std::vector<SidebarSpace>& spaces) const {
  for (size_t i = 0; i < spaces.size(); ++i) {
    if (spaces[i].id == menu_space_) {
      return i;
    }
  }
  return std::nullopt;
}

void SpaceBarView::BeginEdit(const SidebarSpace& space, EditKind kind) {
  AbandonEdit();
  SpaceChip* chip = ChipFor(space.id);
  if (!chip) {
    return;
  }
  auto field = std::make_unique<RenameField>(
      kind == EditKind::kName ? space.name : space.icon,
      base::BindOnce(&SpaceBarView::OnEditFinished, weak_factory_.GetWeakPtr(),
                     space.id, kind));
  field->SetDefaultWidthInChars(kind == EditKind::kName ? kNameFieldChars
                                                        : kIconFieldChars);
  // A focusable view without a name cannot be announced.
  field->GetViewAccessibility().SetName(
      kind == EditKind::kName ? u"Space name" : u"Space icon");
  editing_space_ = space.id;
  edit_field_ = AddChildViewAt(std::move(field), GetIndexOf(chip).value());
  chip->SetVisible(false);
  edit_field_->RequestFocus();
  InvalidateLayout();
}

void SpaceBarView::AbandonEdit() {
  if (!edit_field_) {
    return;
  }
  RenameField* field = edit_field_.ExtractAsDangling();
  field->Abandon();
  RemoveChildViewT(field);
  editing_space_ = SpaceId();
  for (const raw_ptr<SpaceChip>& chip : chips_) {
    chip->SetVisible(true);
  }
  InvalidateLayout();
}

void SpaceBarView::OnEditFinished(SpaceId id,
                                  EditKind kind,
                                  bool commit,
                                  const std::u16string& text) {
  if (edit_field_) {
    RemoveChildViewT(edit_field_.ExtractAsDangling());
  }
  editing_space_ = SpaceId();
  for (const raw_ptr<SpaceChip>& chip : chips_) {
    chip->SetVisible(true);
  }
  InvalidateLayout();
  if (!commit) {
    return;
  }
  if (kind == EditKind::kName) {
    // A space with no name would have no letter to draw.
    if (!text.empty()) {
      model_->RenameSpace(id, text);
    }
    return;
  }
  // An empty icon is a real answer: it goes back to the name's letter.
  model_->SetSpaceIcon(id, text);
}

void SpaceBarView::PlaceEditField() {
  if (!edit_field_) {
    return;
  }
  SpaceChip* host = ChipFor(editing_space_);
  if (!host) {
    // The space went while its name was being typed.
    AbandonEdit();
    return;
  }
  for (const raw_ptr<SpaceChip>& chip : chips_) {
    chip->SetVisible(chip != host);
  }
  ReorderChildView(edit_field_, GetIndexOf(host).value());
}

void SpaceBarView::ShowConfirmation(SpaceId id) {
  ++confirm_serial_;
  if (confirm_widget_) {
    confirm_widget_->Close();
  }
  SpaceChip* anchor = ChipFor(id);
  // No widget means nowhere to show a bubble: a bar under test. The delete
  // stays pending and ConfirmDeleteForTesting answers it.
  if (!GetWidget() || !anchor) {
    return;
  }
  base::WeakPtr<SpaceBarView> weak = weak_factory_.GetWeakPtr();
  auto dialog =
      ui::DialogModel::Builder()
          .SetTitle(u"Delete space?")
          .AddParagraph(ui::DialogModelLabel(confirm_text_))
          .AddOkButton(
              base::BindOnce(&SpaceBarView::OnConfirmation, weak,
                             confirm_serial_, /*accept=*/true),
              ui::DialogModel::Button::Params().SetLabel(u"Delete space"))
          .AddCancelButton(base::BindOnce(&SpaceBarView::OnConfirmation, weak,
                                          confirm_serial_, /*accept=*/false))
          .SetCloseActionCallback(base::BindOnce(&SpaceBarView::OnConfirmation,
                                                 weak, confirm_serial_,
                                                 /*accept=*/false))
          // Enter keeps the space. The default button of a dialog that
          // destroys something must be the one that does not.
          .OverrideDefaultButton(ui::mojom::DialogButton::kCancel)
          .Build();
  auto bubble = std::make_unique<views::BubbleDialogModelHost>(
      std::move(dialog), anchor, views::BubbleBorder::BOTTOM_LEFT);
  // The widget owns the host: BubbleDialogModelHost marks itself owned by
  // its widget, which the client-owned CreateBubble cannot take.
  views::Widget* widget = views::BubbleDialogDelegate::CreateBubbleDeprecated(
      std::move(bubble), views::Widget::InitParams::NATIVE_WIDGET_OWNS_WIDGET);
  confirm_widget_ = widget->GetWeakPtr();
  widget->Show();
}

void SpaceBarView::OnConfirmation(int serial, bool accept) {
  // An answer from a confirmation that has been replaced since.
  if (serial != confirm_serial_ || !pending_delete_.is_valid()) {
    return;
  }
  const SpaceId id = pending_delete_;
  pending_delete_ = SpaceId();
  confirm_text_.clear();
  if (accept) {
    model_->DeleteSpace(id);
  }
}

void SpaceBarView::UpdateProfileBadge() {
  // The active space's profile: the one whose logins the tab on screen uses.
  ProfileId profile_id = DefaultProfileId();
  for (const SidebarSpace& space : model_->spaces()) {
    if (space.is_active) {
      profile_id = space.profile_id;
    }
  }
  SidebarProfile profile{.id = DefaultProfileId(), .name = u"Default"};
  for (const SidebarProfile& candidate : model_->profiles()) {
    if (candidate.id == profile_id) {
      profile = candidate;
    }
  }
  profile_badge_->SetBackground(views::CreateRoundedRectBackground(
      ProfileColorAt(profile.color).color,
      static_cast<float>(metrics::kProfileBadgeSize) / 2));
  const std::u16string label = base::StrCat({u"Profile: ", profile.name});
  profile_badge_->SetTooltipText(label);
  profile_badge_->GetViewAccessibility().SetName(label);
}

BEGIN_METADATA(SpaceBarView)
END_METADATA

}  // namespace arcium
