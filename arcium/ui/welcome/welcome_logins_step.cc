// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Step 2, Spaces and logins: which spaces keep their logins apart.

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "arcium/ui/sidebar/sidebar_colors.h"
#include "arcium/ui/welcome/welcome_model.h"
#include "arcium/ui/welcome/welcome_steps.h"
#include "arcium/ui/welcome/welcome_style.h"
#include "base/functional/bind.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/toggle_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/view_class_properties.h"

namespace arcium::welcome {
namespace {

std::unique_ptr<views::View> SpaceToggle(WelcomeModel& model,
                                         size_t index,
                                         const WelcomeSpace& space) {
  auto tile = MakeTile();
  auto* row = tile->SetLayoutManager(std::make_unique<views::BoxLayout>());
  row->set_inside_border_insets(gfx::Insets::VH(10, 14));
  row->set_between_child_spacing(12);
  row->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
  auto* name =
      tile->AddChildView(MakeSpaceRow(space.icon, space.name, space.origin));
  row->SetFlexForView(name, 1);
  tile->AddChildView(MakeLabel(u"Separate logins", TextStyle::kCaption));
  auto* toggle = tile->AddChildView(std::make_unique<views::ToggleButton>());
  toggle->SetIsOn(space.separate_logins);
  toggle->SetTrackOnColor(kColorArciumWelcomeAccent);
  toggle->SetThumbOnColor(kColorArciumWelcomeOnAccent);
  toggle->GetViewAccessibility().SetName(u"Separate logins for " + space.name);
  toggle->SetCallback(base::BindRepeating(
      [](WelcomeModel* model, size_t index, views::ToggleButton* toggle) {
        model->SetSeparateLogins(index, toggle->GetIsOn());
      },
      &model, index, base::Unretained(toggle)));
  return tile;
}

// Two accounts on one site, one in each space: what keeping logins apart is.
std::unique_ptr<views::View> Account(const gfx::VectorIcon& icon,
                                     const std::u16string& space,
                                     const std::u16string& account) {
  auto tile = MakeTile();
  auto* row = tile->SetLayoutManager(std::make_unique<views::BoxLayout>());
  row->set_inside_border_insets(gfx::Insets::VH(16, 16));
  row->set_between_child_spacing(14);
  row->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kStart);
  tile->AddChildView(MakeIcon(icon, 22, kColorArciumWelcomeText));
  auto* words = tile->AddChildView(std::make_unique<views::BoxLayoutView>());
  words->SetOrientation(views::BoxLayout::Orientation::kVertical);
  words->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kStart);
  words->SetBetweenChildSpacing(2);
  words->AddChildView(MakeLabel(space, TextStyle::kBodyStrong));
  words->AddChildView(MakeLabel(u"Signed in as", TextStyle::kCaption));
  words->AddChildView(MakeLabel(account, TextStyle::kBody));
  return tile;
}

// What the source did with logins, when it kept any apart.
std::u16string SourceNote(const WelcomeModel& model,
                          const std::vector<WelcomeSpace>& spaces) {
  const std::optional<size_t> chosen = model.chosen_source();
  if (!chosen) {
    return u"Example spaces. Rename or remove them any time.";
  }
  size_t apart = 0;
  for (const WelcomeSpace& space : spaces) {
    apart += space.had_separate_logins ? 1 : 0;
  }
  if (apart == 0) {
    return std::u16string();
  }
  const WelcomeSource& source = model.sources()[*chosen];
  const bool zen = source.kind == WelcomeSource::Kind::kZen;
  return CountOf(apart, u"space", u"spaces") + u" used " +
         (zen ? (apart == 1 ? u"a container" : u"separate containers")
              : (apart == 1 ? u"a profile of its own"
                            : u"profiles of their own")) +
         u" in " + source.name + u".";
}

}  // namespace

StepContent BuildLoginsStep(WelcomeModel& model) {
  StepContent step;
  step.title = u"Spaces and logins";
  step.subtitle =
      u"Each space can keep its own logins, so Work and Personal can be "
      u"signed into different accounts.";

  const std::vector<WelcomeSpace> spaces = model.spaces();
  auto choices = std::make_unique<views::BoxLayoutView>();
  choices->SetOrientation(views::BoxLayout::Orientation::kVertical);
  choices->SetCrossAxisAlignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  choices->SetBetweenChildSpacing(8);
  for (size_t i = 0; i < spaces.size(); ++i) {
    choices->AddChildView(SpaceToggle(model, i, spaces[i]));
  }
  const std::u16string note = SourceNote(model, spaces);
  if (!note.empty()) {
    auto* line =
        choices->AddChildView(std::make_unique<views::BoxLayoutView>());
    line->SetBetweenChildSpacing(8);
    line->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kCenter);
    line->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(6, 2, 0, 0));
    line->AddChildView(MakeIcon(vector_icons::kInfoIcon, 16,
                                kColorArciumWelcomeTextSecondary));
    line->AddChildView(MakeLabel(note, TextStyle::kCaption));
  }
  step.choices = std::move(choices);

  step.panel_title = u"Separate logins";
  step.panel_subtitle = u"Keep accounts separate, even on the same website.";
  auto panel = std::make_unique<views::BoxLayoutView>();
  panel->SetOrientation(views::BoxLayout::Orientation::kVertical);
  panel->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kStretch);
  panel->SetBetweenChildSpacing(12);
  panel->AddChildView(
      Account(vector_icons::kWorkIcon, u"Work", u"you@work.example"));
  panel->AddChildView(
      Account(vector_icons::kHomeIcon, u"Personal", u"you@home.example"));
  step.panel = std::move(panel);
  step.panel_note = u"Signing in or out in one space won’t affect the other.";
  return step;
}

}  // namespace arcium::welcome
