// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/space_bar_view.h"

#include <memory>
#include <optional>

#include "arcium/ui/sidebar/sidebar_colors.h"
#include "arcium/ui/sidebar/sidebar_metrics.h"
#include "arcium/ui/sidebar/sidebar_model.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/layout/flex_layout_types.h"
#include "ui/views/layout/layout_types.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/widget/widget.h"

namespace arcium {

namespace {

constexpr float kChipRadius = 8;
// One radio group, so exactly one of the four is checked.
constexpr int kTimeoutGroup = 1;

std::optional<ArchiveTimeout> TimeoutForCommand(int command_id) {
  switch (command_id) {
    case SpaceBarView::kTimeoutTwelveHours:
      return ArchiveTimeout::kTwelveHours;
    case SpaceBarView::kTimeoutOneDay:
      return ArchiveTimeout::kOneDay;
    case SpaceBarView::kTimeoutSevenDays:
      return ArchiveTimeout::kSevenDays;
    case SpaceBarView::kTimeoutNever:
      return ArchiveTimeout::kNever;
    default:
      return std::nullopt;
  }
}

}  // namespace

SpaceBarView::SpaceBarView(SidebarModel* model) : model_(model) {
  auto* layout = SetLayoutManager(std::make_unique<views::FlexLayout>());
  layout->SetOrientation(views::LayoutOrientation::kHorizontal)
      .SetCrossAxisAlignment(views::LayoutAlignment::kCenter)
      .SetInteriorMargin(gfx::Insets::TLBR(8, 0, 0, 0))
      .SetDefault(views::kMarginsKey, gfx::Insets::VH(0, 3));

  active_chip_ = AddChildView(std::make_unique<views::LabelButton>(
      views::Button::PressedCallback(), u"💼 Default"));
  active_chip_->SetMinSize(gfx::Size(0, metrics::kSpaceChipHeight));
  active_chip_->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(0, 9)));
  active_chip_->set_context_menu_controller(this);

  auto* spacer = AddChildView(std::make_unique<views::View>());
  spacer->SetProperty(
      views::kFlexBehaviorKey,
      views::FlexSpecification(views::LayoutOrientation::kHorizontal,
                               views::MinimumFlexSizeRule::kScaleToZero,
                               views::MaximumFlexSizeRule::kUnbounded));

  profile_badge_ = AddChildView(std::make_unique<views::View>());
  profile_badge_->SetPreferredSize(
      gfx::Size(metrics::kProfileBadgeSize, metrics::kProfileBadgeSize));
  profile_badge_->SetTooltipText(u"Profile: Default");

  timeout_menu_ = std::make_unique<ui::SimpleMenuModel>(this);
  timeout_menu_->AddRadioItem(kTimeoutTwelveHours, u"12 hours", kTimeoutGroup);
  timeout_menu_->AddRadioItem(kTimeoutOneDay, u"1 day", kTimeoutGroup);
  timeout_menu_->AddRadioItem(kTimeoutSevenDays, u"7 days", kTimeoutGroup);
  timeout_menu_->AddRadioItem(kTimeoutNever, u"Never", kTimeoutGroup);

  menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  menu_model_->AddItem(kRename, u"Rename space");
  menu_model_->AddItem(kEditTheme, u"Edit theme…");
  menu_model_->AddItem(kChangeIcon, u"Change icon");
  menu_model_->AddSubMenu(kArchiveTimeout, u"Archive Today tabs after",
                          timeout_menu_.get());
  menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  menu_model_->AddItem(kDelete, u"Delete space");
}

SpaceBarView::~SpaceBarView() = default;

void SpaceBarView::ShowContextMenuForViewImpl(
    views::View* source,
    const gfx::Point& point,
    ui::mojom::MenuSourceType source_type) {
  menu_runner_ = std::make_unique<views::MenuRunner>(
      menu_model_.get(), views::MenuRunner::CONTEXT_MENU);
  menu_runner_->RunMenuAt(source->GetWidget(), nullptr,
                          gfx::Rect(point, gfx::Size()),
                          views::MenuAnchorPosition::kTopLeft, source_type);
}

bool SpaceBarView::IsCommandIdEnabled(int command_id) const {
  // Everything else is Stage 3 (rename, icon, delete) or Stage 6 (theme).
  return command_id == kArchiveTimeout ||
         TimeoutForCommand(command_id).has_value();
}

bool SpaceBarView::IsCommandIdChecked(int command_id) const {
  const std::optional<ArchiveTimeout> timeout = TimeoutForCommand(command_id);
  return timeout.has_value() && *timeout == model_->archive_timeout();
}

void SpaceBarView::ExecuteCommand(int command_id, int event_flags) {
  const std::optional<ArchiveTimeout> timeout = TimeoutForCommand(command_id);
  if (timeout) {
    model_->SetArchiveTimeout(*timeout);
  }
}

void SpaceBarView::OnThemeChanged() {
  views::View::OnThemeChanged();
  active_chip_->SetBackground(views::CreateRoundedRectBackground(
      kColorArciumSpaceChipActiveBackground, kChipRadius));
  active_chip_->SetEnabledTextColors(kColorArciumRowTextActive);
  profile_badge_->SetBackground(views::CreateRoundedRectBackground(
      kColorArciumSpaceAccent,
      static_cast<float>(metrics::kProfileBadgeSize) / 2));
}

BEGIN_METADATA(SpaceBarView)
END_METADATA

}  // namespace arcium
