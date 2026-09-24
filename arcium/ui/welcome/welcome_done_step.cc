// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Step 6, You're set: four things worth knowing, and the first imported
// space as it now stands.

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "arcium/ui/sidebar/sidebar_colors.h"
#include "arcium/ui/welcome/welcome_model.h"
#include "arcium/ui/welcome/welcome_steps.h"
#include "arcium/ui/welcome/welcome_style.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/view_class_properties.h"

namespace arcium::welcome {
namespace {

constexpr size_t kPreviewRows = 4;
// Room for three keys, the most any tip needs, so every tip's words start at
// the same place.
constexpr int kKeysWidth = 3 * 40 + 2 * 8;

// One thing worth knowing: the keys that do it, or a gesture, and what it
// does. Every one of these is true of this build; a tip that is not would be
// the first thing a new user learns not to trust.
std::unique_ptr<views::View> Tip(std::vector<std::u16string> keys,
                                 const std::u16string& what,
                                 const std::u16string& detail = u"") {
  auto tile = MakeTile();
  auto* row = tile->SetLayoutManager(std::make_unique<views::BoxLayout>());
  row->set_inside_border_insets(gfx::Insets::VH(8, 14));
  row->set_between_child_spacing(8);
  row->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
  auto* caps = tile->AddChildView(std::make_unique<views::BoxLayoutView>());
  caps->SetBetweenChildSpacing(8);
  caps->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kCenter);
  caps->SetPreferredSize(gfx::Size(kKeysWidth, 40));
  for (const std::u16string& key : keys) {
    caps->AddChildView(MakeKeycap(key));
  }
  auto* words = tile->AddChildView(std::make_unique<views::BoxLayoutView>());
  words->SetOrientation(views::BoxLayout::Orientation::kVertical);
  words->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kStart);
  words->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(0, 10, 0, 0));
  auto* main = words->AddChildView(MakeLabel(what, TextStyle::kBody));
  main->SetMultiLine(true);
  if (!detail.empty()) {
    words->AddChildView(MakeLabel(detail, TextStyle::kCaption));
  }
  row->SetFlexForView(words, 1);
  return tile;
}

// A gesture rather than keys: the grip a row shows under the pointer.
std::unique_ptr<views::View> GestureTip(const std::u16string& what,
                                        const std::u16string& detail) {
  std::unique_ptr<views::View> tip = Tip({}, what, detail);
  views::View* caps = tip->children().front();
  auto grip = MakeLabel(u"⠿", TextStyle::kPanelTitle);
  grip->SetEnabledColor(kColorArciumWelcomeTextSecondary);
  grip->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  grip->SetPreferredSize(gfx::Size(40, 40));
  caps->AddChildView(std::move(grip));
  return tip;
}

// A small picture of the space in the sidebar: its name, then its pinned
// rows.
std::unique_ptr<views::View> SpacePicture(const WelcomeSpace& space) {
  auto tile = MakeTile();
  auto* column = tile->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));
  column->set_inside_border_insets(gfx::Insets::VH(12, 12));
  column->set_between_child_spacing(8);
  column->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  tile->AddChildView(MakeSpaceRow(space.icon, space.name, space.origin));
  tile->AddChildView(MakeRule());
  if (!space.rows.empty()) {
    tile->AddChildView(MakeLabel(u"Pinned", TextStyle::kCaption));
  }
  for (size_t i = 0; i < space.rows.size() && i < kPreviewRows; ++i) {
    const WelcomeSpace::Row& entry = space.rows[i];
    auto* row = tile->AddChildView(std::make_unique<views::BoxLayoutView>());
    row->SetBetweenChildSpacing(10);
    row->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kCenter);
    if (entry.folder) {
      row->AddChildView(MakeIcon(vector_icons::kFolderFlippableIcon, 20,
                                 kColorArciumWelcomeText));
    } else {
      row->AddChildView(MakeBadge(u"", entry.title, 22));
    }
    row->SetFlexForView(
        row->AddChildView(MakeLabel(entry.title, TextStyle::kBody)), 1);
  }
  if (space.rows.size() > kPreviewRows) {
    tile->AddChildView(
        MakeLabel(CountOf(space.rows.size() - kPreviewRows, u"more", u"more"),
                  TextStyle::kCaption));
  }
  return tile;
}

}  // namespace

StepContent BuildDoneStep(WelcomeModel& model) {
  StepContent step;
  step.title = u"You’re set";
  step.subtitle = u"Welcome to Kyuzen. Make yourself at home.";

  auto choices = std::make_unique<views::BoxLayoutView>();
  choices->SetOrientation(views::BoxLayout::Orientation::kVertical);
  choices->SetCrossAxisAlignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  choices->SetBetweenChildSpacing(8);
  choices->AddChildView(
      Tip({u"⌘", u"T"}, u"Command box: go anywhere, search, run commands"));
  choices->AddChildView(
      Tip({u"⌃", u"1"}, u"Jump to a space by its place in the bar"));
  choices->AddChildView(
      Tip({u"⌘", u"⌥", u"S"}, u"Split the screen with the page before"));
  choices->AddChildView(
      GestureTip(u"Drag a row onto another", u"Split two tabs"));
  step.choices = std::move(choices);

  step.panel_title = u"Everything is in place";
  const std::optional<WelcomeSpace> arrived = model.arrived();
  if (arrived) {
    step.panel_subtitle = u"Your " + arrived->name + u" space";
    // The picture keeps its own height rather than filling the panel.
    auto panel = std::make_unique<views::BoxLayoutView>();
    panel->SetOrientation(views::BoxLayout::Orientation::kVertical);
    panel->SetCrossAxisAlignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);
    panel->AddChildView(SpacePicture(*arrived));
    step.panel = std::move(panel);
  } else {
    step.panel_subtitle = u"Your sidebar";
    auto hint = MakeLabel(
        u"Pin a tab to keep it here. It comes back after every quit, with "
        u"nothing loaded until you click it.",
        TextStyle::kBody);
    hint->SetMultiLine(true);
    step.panel = std::move(hint);
  }
  return step;
}

}  // namespace arcium::welcome
