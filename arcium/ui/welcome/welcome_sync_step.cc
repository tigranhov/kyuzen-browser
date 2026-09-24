// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Step 5, Sync: shown only once folder sync exists (its own design, next
// after the welcome). Until then the card never reaches it, and this page
// says what sync will and will not carry, so the step reads right the day it
// appears.

#include <memory>
#include <utility>

#include "arcium/ui/welcome/welcome_model.h"
#include "arcium/ui/welcome/welcome_steps.h"
#include "arcium/ui/welcome/welcome_style.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout_view.h"

namespace arcium::welcome {
namespace {

std::unique_ptr<views::View> List(const std::u16string& heading,
                                  const std::u16string& items) {
  auto list = std::make_unique<views::BoxLayoutView>();
  list->SetOrientation(views::BoxLayout::Orientation::kVertical);
  list->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kStretch);
  list->SetBetweenChildSpacing(4);
  list->AddChildView(MakeLabel(heading, TextStyle::kBodyStrong));
  auto* body = list->AddChildView(MakeLabel(items, TextStyle::kCaption));
  body->SetMultiLine(true);
  return list;
}

}  // namespace

StepContent BuildSyncStep(WelcomeModel& model) {
  StepContent step;
  step.title = u"Sync";
  step.subtitle =
      u"Keep your spaces the same on every Mac, through a folder you "
      u"already sync, such as iCloud Drive.";
  auto choices = std::make_unique<views::BoxLayoutView>();
  choices->SetOrientation(views::BoxLayout::Orientation::kVertical);
  choices->AddChildView(
      MakeLabel(u"Optional. Nothing leaves this Mac until you choose a folder.",
                TextStyle::kCaption));
  step.choices = std::move(choices);

  step.panel_title = u"What syncs";
  auto panel = std::make_unique<views::BoxLayoutView>();
  panel->SetOrientation(views::BoxLayout::Orientation::kVertical);
  panel->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kStretch);
  panel->SetBetweenChildSpacing(16);
  panel->AddChildView(
      List(u"Across your Macs",
           u"Spaces, pinned tabs, folders, favourites and site rules."));
  panel->AddChildView(List(u"Only on this Mac",
                           u"Logins, history and the tabs you have open."));
  step.panel = std::move(panel);
  return step;
}

}  // namespace arcium::welcome
