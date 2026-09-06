// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/playground/sidebar_example.h"

#include <memory>
#include <utility>

#include "arcium/ui/playground/snapshot.h"
#include "arcium/ui/sidebar/sidebar_metrics.h"
#include "arcium/ui/sidebar/sidebar_view.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/background.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/layout/flex_layout_types.h"
#include "ui/views/layout/layout_types.h"
#include "ui/views/view.h"
#include "ui/views/view_class_properties.h"

namespace arcium {

SidebarExample::SidebarExample()
    : ExampleBase("Arcium Sidebar"),
      model_(std::make_unique<FakeSidebarModel>()) {
  model_->AddTab(u"GitHub", "https://github.com/", SidebarSection::kFavorites,
                 false);
  model_->AddTab(u"Gmail", "https://mail.google.com/",
                 SidebarSection::kFavorites, false);
  model_->AddTab(u"Calendar", "https://calendar.google.com/",
                 SidebarSection::kFavorites, false);
  model_->AddTab(u"Linear", "https://linear.app/", SidebarSection::kFavorites,
                 false);
  model_->AddTab(u"Discord", "https://discord.com/app", SidebarSection::kPinned,
                 false);
  model_->AddTab(u"Linear · Arcium board", "https://linear.app/arcium",
                 SidebarSection::kPinned, false);
  model_->AddTab(u"tigranhov/arcium", "https://github.com/tigranhov/arcium",
                 SidebarSection::kToday, true);
  model_->AddTab(u"Chromium Views tutorial",
                 "https://www.youtube.com/watch?v=views",
                 SidebarSection::kToday, false);
  model_->AddTab(u"StoragePartitionConfig - Chromium Code Search",
                 "https://source.chromium.org/", SidebarSection::kToday, false);
  model_->AddTab(u"Hacker News", "https://news.ycombinator.com/",
                 SidebarSection::kToday, false);
  // Two entries with no tab behind them: they draw from their stored title
  // and open their URL when clicked.
  model_->AddColdEntry(u"Figma", "https://figma.com/",
                       SidebarSection::kFavorites);
  model_->AddColdEntry(u"Chromium Gerrit",
                       "https://chromium-review.googlesource.com/",
                       SidebarSection::kPinned);
  model_->SetLoading(8, true);
  model_->SetAudible(7, true);
}

SidebarExample::~SidebarExample() = default;

void SidebarExample::CreateExampleView(views::View* container) {
  auto* layout =
      container->SetLayoutManager(std::make_unique<views::FlexLayout>());
  layout->SetOrientation(views::LayoutOrientation::kHorizontal)
      .SetCrossAxisAlignment(views::LayoutAlignment::kStretch);

  // Window actions have no window here; log them so clicks are visible.
  auto log = [](const char* what) {
    return base::BindRepeating(
        [](const char* w) { LOG(ERROR) << "sidebar: " << w; }, what);
  };
  SidebarView::Delegate delegate;
  delegate.toggle_sidebar = log("toggle sidebar");
  delegate.back = log("back");
  delegate.forward = log("forward");
  delegate.reload = log("reload");
  delegate.edit_url = log("edit url");
  auto* sidebar = container->AddChildView(
      std::make_unique<SidebarView>(model_.get(), std::move(delegate)));
  sidebar->SetCaptionButtonWidth(metrics::kDefaultCaptionButtonWidth);

  auto* page = container->AddChildView(std::make_unique<views::View>());
  page->SetBackground(views::CreateRoundedRectBackground(
      SkColorSetRGB(0xFF, 0xFF, 0xFF), metrics::kContentCornerRadius));
  page->SetProperty(
      views::kMarginsKey,
      gfx::Insets::TLBR(metrics::kContentInset, 0, metrics::kContentInset,
                        metrics::kContentInset));
  page->SetProperty(
      views::kFlexBehaviorKey,
      views::FlexSpecification(views::LayoutOrientation::kHorizontal,
                               views::MinimumFlexSizeRule::kScaleToZero,
                               views::MaximumFlexSizeRule::kUnbounded));

  MaybeScheduleSnapshot(container);
}

}  // namespace arcium
