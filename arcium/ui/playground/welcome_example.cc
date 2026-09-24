// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/playground/welcome_example.h"

#include <algorithm>
#include <iterator>
#include <memory>
#include <optional>
#include <utility>

#include "arcium/ui/playground/snapshot.h"
#include "arcium/ui/sidebar/sidebar_metrics.h"
#include "arcium/ui/sidebar/sidebar_view.h"
#include "arcium/ui/welcome/welcome_view.h"
#include "base/command_line.h"
#include "base/strings/string_number_conversions.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/layout/flex_layout_types.h"
#include "ui/views/layout/layout_types.h"
#include "ui/views/view_class_properties.h"

namespace arcium {
namespace {

constexpr WelcomeStep kSteps[] = {
    WelcomeStep::kSetup,          WelcomeStep::kLogins, WelcomeStep::kSearch,
    WelcomeStep::kDefaultBrowser, WelcomeStep::kDone,
};

}  // namespace

WelcomeExample::WelcomeExample()
    : ExampleBase("Arcium Welcome"),
      sidebar_model_(std::make_unique<FakeSidebarModel>()),
      welcome_model_(std::make_unique<FakeWelcomeModel>()) {
  // A sidebar as a first launch leaves it: one tab, the one on screen.
  sidebar_model_->AddTab(u"Welcome to Kyuzen", "https://example.com/",
                         SidebarSection::kToday, true);
}

WelcomeExample::~WelcomeExample() = default;

void WelcomeExample::CreateExampleView(views::View* container) {
  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();
  if (command_line.HasSwitch("welcome-fresh")) {
    welcome_model_->ChooseSource(std::nullopt);
  }
  if (command_line.HasSwitch("welcome-imported")) {
    welcome_model_->ApplyImport();
  }
  if (command_line.HasSwitch("welcome-default")) {
    welcome_model_->SetDefaultBrowser(true);
  }
  size_t step = 1;
  base::StringToSizeT(command_line.GetSwitchValueASCII("welcome-step"), &step);
  const WelcomeStep first =
      kSteps[std::clamp<size_t>(step, 1, std::size(kSteps)) - 1];

  container->SetLayoutManager(std::make_unique<views::FlexLayout>())
      ->SetOrientation(views::LayoutOrientation::kHorizontal)
      .SetCrossAxisAlignment(views::LayoutAlignment::kStretch);
  auto* sidebar = container->AddChildView(std::make_unique<SidebarView>(
      sidebar_model_.get(), SidebarView::Delegate()));
  sidebar->SetCaptionButtonWidth(metrics::kDefaultCaptionButtonWidth);
  auto* welcome = container->AddChildView(std::make_unique<WelcomeView>(
      welcome_model_.get(),
      command_line.HasSwitch("welcome-import-only")
          ? WelcomeView::Mode::kImportOnly
          : WelcomeView::Mode::kWelcome,
      first));
  welcome->SetProperty(
      views::kFlexBehaviorKey,
      views::FlexSpecification(views::LayoutOrientation::kHorizontal,
                               views::MinimumFlexSizeRule::kScaleToZero,
                               views::MaximumFlexSizeRule::kUnbounded));
  MaybeScheduleSnapshot(container);
}

}  // namespace arcium
