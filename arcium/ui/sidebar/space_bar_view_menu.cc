// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The space chip's context menu: what each command offers, whether it is
// available, and what it does. Apart from space_bar_view.cc because the view
// there is already a full file of layout, inline editing and confirmation,
// and the menu answers a different question. The two meet only at the enum
// of command ids the menu is built from, which lives in the header.

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "arcium/ui/sidebar/space_bar_view.h"
#include "arcium/ui/sidebar/space_gradients.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/view_utils.h"

namespace arcium {

namespace {

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

std::optional<int> GradientForCommand(int command_id) {
  const int preset = command_id - SpaceBarView::kGradientFirst;
  if (preset < 0 || static_cast<size_t>(preset) >= SpaceGradients().size()) {
    return std::nullopt;
  }
  return preset;
}

std::u16string CountPhrase(int count,
                           const std::u16string& one,
                           const std::u16string& many) {
  return base::StrCat(
      {base::NumberToString16(count), u" ", count == 1 ? one : many});
}

}  // namespace

void SpaceBarView::ShowContextMenuForViewImpl(
    views::View* source,
    const gfx::Point& point,
    ui::mojom::MenuSourceType source_type) {
  auto* chip = views::AsViewClass<SpaceChip>(source);
  if (!chip) {
    return;
  }
  SetMenuSpace(chip->space_id());
  menu_runner_ = std::make_unique<views::MenuRunner>(
      menu_model_.get(), views::MenuRunner::CONTEXT_MENU);
  menu_runner_->RunMenuAt(source->GetWidget(), nullptr,
                          gfx::Rect(point, gfx::Size()),
                          views::MenuAnchorPosition::kTopLeft, source_type);
}

bool SpaceBarView::IsCommandIdEnabled(int command_id) const {
  const std::vector<SidebarSpace> spaces = model_->spaces();
  const std::optional<size_t> index = MenuSpaceIndex(spaces);
  if (command_id == kArchiveTimeout ||
      TimeoutForCommand(command_id).has_value()) {
    // The model keeps the active space's timeout and no other, so choosing
    // one on a background space's chip would set a different space's.
    return !index || spaces[*index].is_active;
  }
  if (!index) {
    return false;
  }
  switch (command_id) {
    case kMoveLeft:
      return *index > 0;
    case kMoveRight:
      return *index + 1 < spaces.size();
    case kDelete:
      // The model refuses to delete the last space; offering it would ask
      // for a confirmation that then does nothing.
      return spaces.size() > 1;
    default:
      return true;
  }
}

bool SpaceBarView::IsCommandIdChecked(int command_id) const {
  if (const std::optional<int> preset = GradientForCommand(command_id)) {
    const std::vector<SidebarSpace> spaces = model_->spaces();
    const std::optional<size_t> index = MenuSpaceIndex(spaces);
    return index && spaces[*index].gradient == *preset;
  }
  const std::optional<ArchiveTimeout> timeout = TimeoutForCommand(command_id);
  return timeout.has_value() && *timeout == model_->archive_timeout();
}

void SpaceBarView::ExecuteCommand(int command_id, int event_flags) {
  if (const std::optional<ArchiveTimeout> timeout =
          TimeoutForCommand(command_id)) {
    model_->SetArchiveTimeout(*timeout);
    return;
  }
  // A copy: every command below changes the model, which rebuilds the bar.
  const std::vector<SidebarSpace> spaces = model_->spaces();
  const std::optional<size_t> index = MenuSpaceIndex(spaces);
  if (!index) {
    return;
  }
  const SidebarSpace& space = spaces[*index];
  if (const std::optional<int> preset = GradientForCommand(command_id)) {
    model_->SetSpaceGradient(space.id, *preset);
    return;
  }
  switch (command_id) {
    case kRename:
      BeginEdit(space, EditKind::kName);
      return;
    case kChangeIcon:
      // Takes whatever the field holds, which is how an emoji gets in
      // without a picker of its own.
      BeginEdit(space, EditKind::kIcon);
      return;
    case kMoveLeft:
      if (*index > 0) {
        model_->MoveSpace(space.id, static_cast<int>(*index) - 1);
      }
      return;
    case kMoveRight:
      if (*index + 1 < spaces.size()) {
        model_->MoveSpace(space.id, static_cast<int>(*index) + 1);
      }
      return;
    case kDelete:
      // The tabs and the entries do not leave the same way, so the sentence
      // makes two separate promises instead of one blanket one. Closing a
      // space's tabs is an ordinary tab close underneath, so Cmd+Shift+T
      // brings them back; the pins and favourites are removed from the model
      // itself, with nothing to reopen, so only they are promised gone for
      // good. One entry is one thing whichever kind it is, so it reads "1
      // pin or favourite"; "1 pin and favourite" would read as two.
      pending_delete_ = space.id;
      confirm_text_ =
          base::StrCat({u"Delete “", space.name, u"”? Its ",
                        CountPhrase(space.open_tab_count, u"tab", u"tabs"),
                        u" will close and its ",
                        CountPhrase(space.entry_count, u"pin or favourite",
                                    u"pins and favourites"),
                        u" will be deleted for good."});
      ShowConfirmation(space.id);
      return;
    default:
      return;
  }
}

}  // namespace arcium
