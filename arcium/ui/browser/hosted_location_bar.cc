// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/hosted_location_bar.h"

#include "arcium/ui/sidebar/url_pill_view.h"
#include "chrome/browser/ui/views/permissions/chip/permission_chip_view.h"
#include "chrome/browser/ui/views/permissions/chip/permission_dashboard_view.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"

namespace arcium {

namespace {

// The pill the bar was moved into, or null when it was left where Chromium
// put it -- a window with no sidebar, or one built before the move. The
// template is what keeps the const caller and the mutating one from being
// the same walk written twice.
template <typename ViewType>
auto* PillFor(ViewType* bar) {
  for (auto* parent = bar ? bar->parent() : nullptr; parent;
       parent = parent->parent()) {
    if (auto* pill = views::AsViewClass<UrlPillView>(parent)) {
      return pill;
    }
  }
  return static_cast<decltype(views::AsViewClass<UrlPillView>(bar))>(nullptr);
}

}  // namespace

bool IsLocationBarHosted(const views::View* bar) {
  return PillFor(bar) != nullptr;
}

void OnHostedLocationBarLaidOut(views::View* bar) {
  UrlPillView* pill = PillFor(bar);
  if (!pill) {
    return;
  }
  bool speaking = false;
  for (views::View* child : bar->children()) {
    const bool is_chip = views::IsViewClass<PermissionDashboardView>(child) ||
                         views::IsViewClass<PermissionChipView>(child);
    if (!is_chip) {
      // Hidden rather than removed: this runs on every layout, and the bar
      // rebuilds and re-shows its own children whenever the page changes.
      child->SetVisible(false);
      continue;
    }
    speaking = speaking || child->GetVisible();
  }
  pill->SetHostedBarSpeaking(speaking);
}

}  // namespace arcium
