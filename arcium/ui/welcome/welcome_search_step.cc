// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Step 3, Search engine: where the command box sends a search.

#include <memory>
#include <utility>
#include <vector>

#include "arcium/ui/sidebar/sidebar_colors.h"
#include "arcium/ui/welcome/welcome_model.h"
#include "arcium/ui/welcome/welcome_steps.h"
#include "arcium/ui/welcome/welcome_style.h"
#include "base/functional/bind.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/radio_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout_view.h"

namespace arcium::welcome {
namespace {

constexpr int kEngineGroup = 2;
// What the preview searches for. Plain words, so it reads as an example.
constexpr char16_t kExampleQuery[] = u"quiet weekend walks";

std::unique_ptr<views::View> EngineRow(WelcomeModel& model,
                                       size_t index,
                                       const std::u16string& name) {
  const bool chosen = model.chosen_engine() == index;
  auto tile = MakeTile(chosen);
  auto* row = tile->SetLayoutManager(std::make_unique<views::BoxLayout>());
  row->set_inside_border_insets(gfx::Insets::VH(8, 14));
  row->set_between_child_spacing(12);
  row->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
  auto* radio = tile->AddChildView(
      std::make_unique<AccentRadioButton>(u"", kEngineGroup));
  radio->SetChecked(chosen);
  radio->GetViewAccessibility().SetName(name);
  radio->SetCallback(base::BindRepeating(
      [](WelcomeModel* model, size_t index) { model->ChooseEngine(index); },
      &model, index));
  tile->AddChildView(MakeBadge(u"", name, 26));
  tile->AddChildView(MakeLabel(name, TextStyle::kBody));
  return tile;
}

// The command box as it will look, searching with the chosen engine.
std::unique_ptr<views::View> BoxPreview(const std::u16string& engine) {
  auto frame = MakeTile();
  auto* column = frame->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));
  column->set_inside_border_insets(gfx::Insets(8));
  column->set_between_child_spacing(8);
  column->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  auto field = MakeTile();
  auto* field_row =
      field->SetLayoutManager(std::make_unique<views::BoxLayout>());
  field_row->set_inside_border_insets(gfx::Insets::VH(10, 12));
  field_row->set_between_child_spacing(10);
  field_row->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  field->AddChildView(MakeIcon(vector_icons::kSearchIcon, 18,
                               kColorArciumWelcomeTextSecondary));
  field->AddChildView(MakeLabel(kExampleQuery, TextStyle::kBody));
  frame->AddChildView(std::move(field));

  auto answer = std::make_unique<views::View>();
  answer->SetBackground(views::CreateRoundedRectBackground(
      kColorArciumWelcomeAccentSoft, kTileRadius));
  auto* answer_row =
      answer->SetLayoutManager(std::make_unique<views::BoxLayout>());
  answer_row->set_inside_border_insets(gfx::Insets::VH(10, 12));
  answer_row->set_between_child_spacing(10);
  answer_row->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  answer->AddChildView(MakeBadge(u"", engine, 24));
  auto* words = answer->AddChildView(MakeLabel(
      u"Search " + engine + u" for “" + std::u16string(kExampleQuery) + u"”",
      TextStyle::kBody));
  words->SetMultiLine(true);
  answer_row->SetFlexForView(words, 1);
  answer->AddChildView(MakeLabel(u"↵", TextStyle::kBodyStrong));
  frame->AddChildView(std::move(answer));
  return frame;
}

}  // namespace

StepContent BuildSearchStep(WelcomeModel& model) {
  StepContent step;
  step.title = u"Search engine";
  step.subtitle = u"Choose where searches from the command box go.";

  const std::vector<std::u16string> engines = model.engines();
  auto choices = std::make_unique<views::BoxLayoutView>();
  choices->SetOrientation(views::BoxLayout::Orientation::kVertical);
  choices->SetCrossAxisAlignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  choices->SetBetweenChildSpacing(8);
  for (size_t i = 0; i < engines.size(); ++i) {
    choices->AddChildView(EngineRow(model, i, engines[i]));
  }
  step.choices = std::move(choices);

  step.panel_title = u"Search from anywhere";
  step.panel_subtitle = u"A preview of your command box";
  const size_t chosen = model.chosen_engine();
  // The preview keeps its own height, its caption right under it, rather
  // than stretching to the bottom of the panel.
  auto panel = std::make_unique<views::BoxLayoutView>();
  panel->SetOrientation(views::BoxLayout::Orientation::kVertical);
  panel->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kStretch);
  panel->SetBetweenChildSpacing(14);
  panel->AddChildView(BoxPreview(
      chosen < engines.size() ? engines[chosen] : std::u16string(u"the web")));
  auto* caption = panel->AddChildView(MakeLabel(
      u"Type a question or a few words to search.", TextStyle::kCaption));
  caption->SetMultiLine(true);
  step.panel = std::move(panel);
  return step;
}

}  // namespace arcium::welcome
