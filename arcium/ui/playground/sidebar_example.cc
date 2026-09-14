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
#include "base/time/time.h"
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
  model_->AddTab(u"Notion", "https://notion.so/", SidebarSection::kPinned,
                 false);
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
  // Two tabs that exist but have not loaded, the state a restart leaves
  // every tab but the one on screen in: they draw dimmed like the entries
  // above.
  for (const SidebarRow& row : model_->rows()) {
    if (row.title == u"Notion" || row.title == u"Hacker News") {
      model_->SetUnloaded(row.tab_index, true);
    }
  }
  model_->SetLoading(9, true);
  model_->SetAudible(8, true);
  // The four states Task 7 has to look right in: a warm row inside an
  // expanded folder next to a cold one, a collapsed folder that contributes
  // only its header, a folder nested inside another so the indent can be
  // judged against a real 250px sidebar rather than against a margin
  // assertion, and a top-level pinned row offering the way back to its pinned
  // URL on hover.
  const FolderId work =
      model_->AddFolderWith(u"Work", {u"Discord", u"Chromium Gerrit"});
  const FolderId reading = model_->AddFolderWith(u"Reading", {u"Notion"});
  model_->SetFolderParent(reading, work);
  model_->SetFolderCollapsed(reading, true);
  model_->SetCanReturnToPinnedUrl(5, true);
  // Enough archived rows to see what the list has to draw: several ages, a
  // title long enough to elide against the timestamp column, and — once they
  // are all clicked away — the empty message.
  //
  // Not the empty-title case, which cannot be reached from here: an archived
  // row with no title falls back to its URL in
  // SidebarTabModel::DeliverArchivedRows, and the playground has no
  // SidebarTabModel. That fallback is covered in archive_service_unittest.cc.
  const base::Time now = base::Time::Now();
  model_->AddArchived(u"Arc Browser", "https://arc.net/", now - base::Hours(2));
  model_->AddArchived(u"WebKit Blog", "https://webkit.org/blog/",
                      now - base::Hours(9));
  model_->AddArchived(
      u"A very long title that has to elide before it reaches "
      u"the timestamp column",
      "https://example.com/long", now - base::Days(1));
  model_->AddArchived(u"Rust Book", "https://doc.rust-lang.org/book/",
                      now - base::Days(4));

  // A second profile, so the badge and the space menu's Profile submenu have
  // something besides Default to show.
  model_->AddSpaceForTesting(u"Work", u"", 4);
  const ProfileId work_profile = model_->AddProfileForTesting(u"Work", 2);
  model_->SetSpaceProfile(model_->spaces()[1].id, work_profile);
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
  delegate.open_extensions = log("open extensions");
  delegate.copy_link = log("copy link");
  delegate.open_site_info = log("open site info");
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
