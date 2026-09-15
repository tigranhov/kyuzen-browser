// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The space bar's half of this model: reporting spaces() with their counts
// already totalled, and forwarding its commands to the persistent model and
// to the window's own switcher. The rest of this class -- Today in
// sidebar_tab_model.cc, favourites/pins/folders in
// sidebar_tab_model_entries.cc -- never needs to know a space exists beyond
// asking active_space() for the one it is drawing.

#include <utility>

#include "arcium/browser/model/space.h"
#include "arcium/browser/model/tab_entry.h"
#include "arcium/ui/browser/sidebar_tab_model.h"

#include "arcium/browser/model/routing_rule.h"
#include "url/gurl.h"

namespace arcium {

std::vector<SidebarSpace> SidebarTabModel::spaces() const {
  std::vector<SidebarSpace> result;
  const SpaceId active = active_space();
  for (const Space& space : arcium_model_->spaces()) {
    SidebarSpace out;
    out.id = space.id;
    out.name = space.name;
    out.icon = space.icon;
    out.gradient = space.gradient;
    out.profile_id = arcium_model_->ProfileOfSpace(space.id);
    out.is_active = space.id == active;
    // Counted here rather than by the bar: the strip is this object's, and a
    // dot that walked it would be a view reaching into the browser.
    out.open_tab_count = switcher_ ? switcher_->OpenTabCount(space.id) : 0;
    out.entry_count = static_cast<int>(
        arcium_model_->EntriesForKind(space.id, EntryKind::kFavorite).size() +
        arcium_model_->EntriesForKind(space.id, EntryKind::kPinned).size());
    result.push_back(std::move(out));
  }
  return result;
}

void SidebarTabModel::SwitchToSpace(SpaceId id) {
  if (switcher_) {
    switcher_->SwitchTo(id);
  }
}

void SidebarTabModel::AddSpace(const std::u16string& name) {
  // On the profile of the space you are in, as Zen creates a workspace in
  // the selected tab's container. Changing it before the space has tabs
  // costs nothing.
  const SpaceId id = arcium_model_->AddSpace(
      name, arcium_model_->ProfileOfSpace(active_space()));
  // A new space is one you are put into: it is empty, so the switch opens
  // its blank tab and the quick entry over it, which is where a new space
  // starts from.
  if (switcher_) {
    switcher_->SwitchTo(id);
  }
}

void SidebarTabModel::RenameSpace(SpaceId id, const std::u16string& name) {
  arcium_model_->RenameSpace(id, name);
}

void SidebarTabModel::SetSpaceIcon(SpaceId id, const std::u16string& icon) {
  arcium_model_->SetSpaceIcon(id, icon);
}

void SidebarTabModel::SetSpaceGradient(SpaceId id, int gradient) {
  arcium_model_->SetSpaceGradient(id, gradient);
}

void SidebarTabModel::MoveSpace(SpaceId id, int position) {
  arcium_model_->ReorderSpace(id, position);
}

void SidebarTabModel::DeleteSpace(SpaceId id) {
  // The strip and the archive rows are the switcher's to clean up; with no
  // switcher (the playground, a window with no sidebar) there is neither, so
  // there is nothing this could safely do.
  if (switcher_) {
    switcher_->DeleteSpace(id);
  }
}

void SidebarTabModel::MoveTabToSpace(int tab_index, SpaceId space_id) {
  if (switcher_) {
    switcher_->MoveTabToSpace(tab_index, space_id);
  }
}

void SidebarTabModel::MoveEntryToSpace(EntryId id, SpaceId space_id) {
  // The switcher follows the move when the entry's tab is the one on screen;
  // with no switcher there is no "on screen" to follow, so the model change
  // is all there is.
  if (switcher_) {
    switcher_->MoveEntryToSpace(id, space_id);
  } else {
    arcium_model_->MoveEntryToSpace(id, space_id);
  }
}

bool SidebarTabModel::SiteOpensInActiveSpace(const GURL& url) const {
  const std::string site = RuleSiteForUrl(url);
  if (site.empty()) {
    return false;
  }
  for (const RoutingRule& rule : arcium_model_->routing_rules()) {
    if (rule.site == site) {
      return rule.space_id == active_space();
    }
  }
  return false;
}

void SidebarTabModel::SetSiteOpensInActiveSpace(const GURL& url,
                                                bool opens_here) {
  const std::string site = RuleSiteForUrl(url);
  if (site.empty()) {
    return;
  }
  if (opens_here) {
    arcium_model_->SetRoutingRule(site, active_space());
    return;
  }
  // Only this space's rule: "stop opening it here" is not "stop sending it
  // to whichever space another window chose".
  if (SiteOpensInActiveSpace(url)) {
    arcium_model_->RemoveRoutingRule(site);
  }
}

}  // namespace arcium
