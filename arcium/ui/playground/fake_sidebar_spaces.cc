// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The fake's half of the space bar: an in-memory stand-in for the model's
// spaces and SpaceSwitcher's commands, kept simple enough for a view test to
// build on without a browser. See the class comment in fake_sidebar_model.h for
// why this notifies synchronously where the real model posts.

#include <algorithm>
#include <limits>
#include <utility>

#include "arcium/ui/playground/fake_sidebar_model.h"

#include "arcium/browser/model/routing_rule.h"
#include "url/gurl.h"

namespace arcium {

SpaceId FakeSidebarModel::ActiveSpaceId() const {
  for (const SidebarSpace& space : spaces_) {
    if (space.is_active) {
      return space.id;
    }
  }
  // Never actually reached -- one space is always marked active -- but the
  // first space is the same fallback the real model's default_space_id()
  // gives a caller with no window to ask.
  return spaces_.empty() ? SpaceId() : spaces_.front().id;
}

SidebarSpace* FakeSidebarModel::FindSpace(SpaceId id) {
  for (SidebarSpace& space : spaces_) {
    if (space.id == id) {
      return &space;
    }
  }
  return nullptr;
}

void FakeSidebarModel::MarkActiveSpace(SpaceId id) {
  for (SidebarSpace& space : spaces_) {
    space.is_active = space.id == id;
  }
}

SpaceId FakeSidebarModel::AddSpaceForTesting(const std::u16string& name,
                                             const std::u16string& icon,
                                             int gradient) {
  SidebarSpace space;
  space.id = SpaceId::Generate();
  space.name = name;
  space.icon = icon;
  space.gradient = gradient;
  spaces_.push_back(space);
  Notify();
  return space.id;
}

void FakeSidebarModel::AddTabInSpaceForTesting(const std::u16string& title,
                                               const std::string& url,
                                               SpaceId space) {
  // No favicon: SwatchFor is private to fake_sidebar_model.cc, and a test
  // seeding another space checks which space a row is in, not what it looks
  // like.
  SidebarRow row;
  row.title = title;
  row.url = GURL(url);
  row.section = SidebarSection::kToday;
  row.space = space;
  rows_.push_back(std::move(row));
  Reindex();
  Notify();
}

std::vector<SidebarSpace> FakeSidebarModel::spaces() const {
  std::vector<SidebarSpace> result = spaces_;
  for (SidebarSpace& space : result) {
    space.open_tab_count = 0;
    space.entry_count = 0;
    for (const SidebarRow& row : rows_) {
      if (row.space != space.id) {
        continue;
      }
      // open_tab_count: the fake's own rows tagged with the space that carry
      // a live tab, mirroring what SpaceSwitcher::OpenTabCount counts in the
      // strip. entry_count: the ones that are also persistent entries,
      // mirroring EntriesForKind(favourite) + EntriesForKind(pinned).
      if (!row.is_cold) {
        ++space.open_tab_count;
      }
      if (row.entry_id.is_valid()) {
        ++space.entry_count;
      }
    }
  }
  return result;
}

void FakeSidebarModel::SwitchToSpace(SpaceId id) {
  if (!FindSpace(id)) {
    return;
  }
  MarkActiveSpace(id);
  Notify();
}

void FakeSidebarModel::AddSpace(const std::u16string& name) {
  SidebarSpace space;
  space.id = SpaceId::Generate();
  space.name = name;
  // A new space starts on the profile of the space you are in, as Zen
  // creates a workspace in the selected tab's container.
  if (const SidebarSpace* current = FindSpace(ActiveSpaceId())) {
    space.profile_id = current->profile_id;
  }
  const SpaceId id = space.id;
  spaces_.push_back(std::move(space));
  // A new space is one you are put into, not just a dot that appears.
  MarkActiveSpace(id);
  Notify();
}

void FakeSidebarModel::RenameSpace(SpaceId id, const std::u16string& name) {
  if (SidebarSpace* space = FindSpace(id)) {
    space->name = name;
    Notify();
  }
}

void FakeSidebarModel::SetSpaceIcon(SpaceId id, const std::u16string& icon) {
  if (SidebarSpace* space = FindSpace(id)) {
    space->icon = icon;
    Notify();
  }
}

void FakeSidebarModel::SetSpaceGradient(SpaceId id, int gradient) {
  if (SidebarSpace* space = FindSpace(id)) {
    space->gradient = gradient;
    Notify();
  }
}

void FakeSidebarModel::MoveSpace(SpaceId id, int position) {
  auto it =
      std::find_if(spaces_.begin(), spaces_.end(),
                   [id](const SidebarSpace& space) { return space.id == id; });
  if (it == spaces_.end()) {
    return;
  }
  SidebarSpace moved = std::move(*it);
  spaces_.erase(it);
  const size_t clamped =
      std::min(static_cast<size_t>(std::max(position, 0)), spaces_.size());
  spaces_.insert(spaces_.begin() + clamped, std::move(moved));
  Notify();
}

void FakeSidebarModel::DeleteSpace(SpaceId id) {
  // Refuses the last space, same as the real model: every row has to be in
  // one.
  if (spaces_.size() <= 1) {
    return;
  }
  auto it =
      std::find_if(spaces_.begin(), spaces_.end(),
                   [id](const SidebarSpace& space) { return space.id == id; });
  if (it == spaces_.end()) {
    return;
  }
  // The real switcher moves off the active space to the one after it, or
  // the one before when it is last, and never to the space being removed.
  const bool was_active = it->is_active;
  const auto neighbour = it + 1 != spaces_.end() ? it + 1 : it - 1;
  const SpaceId landing = neighbour->id;
  spaces_.erase(it);
  // Takes its rows with it, the way the real DeleteSpace closes the space's
  // tabs and drops its entries.
  std::erase_if(rows_, [id](const SidebarRow& row) { return row.space == id; });
  if (was_active) {
    MarkActiveSpace(landing);
  }
  Reindex();
  Notify();
}

void FakeSidebarModel::MoveTabToSpace(int tab_index, SpaceId space_id) {
  if (!FindSpace(space_id)) {
    return;
  }
  SidebarRow* row = FindByTabIndex(tab_index);
  if (!row || row->space == space_id) {
    return;
  }
  // Re-tagged, not removed: the real tab stays in the strip and is drawn by
  // whichever window shows its new space.
  row->space = space_id;
  if (row->is_active) {
    // Moving the tab you are looking at takes you with it, as the real
    // switcher's adoption does.
    MarkActiveSpace(space_id);
  }
  Notify();
}

void FakeSidebarModel::MoveEntryToSpace(EntryId id, SpaceId space_id) {
  if (!FindSpace(space_id)) {
    return;
  }
  SidebarRow* found = FindByEntry(id);
  if (!found || found->space == space_id) {
    return;
  }
  // Copied out before the erase below invalidates it.
  SidebarRow moved = *found;
  std::erase_if(rows_, [id](const SidebarRow& r) { return r.entry_id == id; });
  moved.space = space_id;
  // ArciumModel::MoveEntryToSpace's rule: folders do not cross spaces, and
  // the entry arrives last among its kind in the target.
  moved.folder_id.reset();
  const bool follows = moved.is_active;
  const SidebarSection section = moved.section;
  rows_.insert(SlotIn(section, std::numeric_limits<int>::max()),
               std::move(moved));
  Reindex();
  if (follows) {
    MarkActiveSpace(space_id);
  }
  Notify();
}

bool FakeSidebarModel::SiteOpensInActiveSpace(const GURL& url) const {
  const auto it = site_rules_.find(RuleSiteForUrl(url));
  return it != site_rules_.end() && it->second == ActiveSpaceId();
}

void FakeSidebarModel::SetSiteOpensInActiveSpace(const GURL& url,
                                                 bool opens_here) {
  const std::string site = RuleSiteForUrl(url);
  if (site.empty()) {
    return;
  }
  if (opens_here) {
    site_rules_[site] = ActiveSpaceId();
  } else if (SiteOpensInActiveSpace(url)) {
    site_rules_.erase(site);
  }
  Notify();
}

}  // namespace arcium
