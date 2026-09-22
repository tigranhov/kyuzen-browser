// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/model/arcium_model.h"

#include <algorithm>
#include <limits>
#include <map>
#include <optional>

#include "base/time/time.h"
#include "url/gurl.h"

namespace arcium {

namespace {

ArciumProfile MakeDefaultProfile() {
  ArciumProfile profile;
  profile.id = DefaultProfileId();
  profile.name = u"Default";
  return profile;
}

}  // namespace

std::optional<base::TimeDelta> ArchiveTimeoutToDelta(ArchiveTimeout timeout) {
  switch (timeout) {
    case ArchiveTimeout::kTwelveHours:
      return base::Hours(12);
    case ArchiveTimeout::kOneDay:
      return base::Hours(24);
    case ArchiveTimeout::kSevenDays:
      return base::Days(7);
    case ArchiveTimeout::kNever:
      return std::nullopt;
  }
}

ArciumModel::ArciumModel() {
  profiles_.push_back(MakeDefaultProfile());
  Space space;
  space.id = SpaceId::Generate();
  space.name = u"Space";
  space.archive_timeout = ArchiveTimeout::kTwelveHours;
  space.position = 0;
  space.profile_id = DefaultProfileId();
  spaces_.push_back(std::move(space));
}

ArciumModel::~ArciumModel() = default;

SpaceId ArciumModel::default_space_id() const {
  return spaces_.empty() ? SpaceId() : spaces_.front().id;
}

SpaceId ArciumModel::last_active_space() const {
  return GetSpace(last_active_space_) ? last_active_space_ : default_space_id();
}

const Space* ArciumModel::GetSpace(SpaceId id) const {
  for (const Space& space : spaces_) {
    if (space.id == id) {
      return &space;
    }
  }
  return nullptr;
}

SpaceId ArciumModel::AddSpace(const std::u16string& name, ProfileId profile) {
  Space space;
  space.id = SpaceId::Generate();
  space.name = name;
  space.position = static_cast<int>(spaces_.size());
  space.profile_id = GetProfile(profile) ? profile : DefaultProfileId();
  const SpaceId id = space.id;
  spaces_.push_back(std::move(space));
  Notify();
  return id;
}

void ArciumModel::RenameSpace(SpaceId id, const std::u16string& name) {
  Space* space = FindSpace(id);
  if (!space || space->name == name) {
    return;
  }
  space->name = name;
  Notify();
}

void ArciumModel::SetSpaceIcon(SpaceId id, const std::u16string& icon) {
  Space* space = FindSpace(id);
  if (!space || space->icon == icon) {
    return;
  }
  space->icon = icon;
  Notify();
}

void ArciumModel::SetSpaceGradient(SpaceId id, int gradient) {
  Space* space = FindSpace(id);
  if (!space || space->gradient == gradient) {
    return;
  }
  space->gradient = gradient;
  Notify();
}

void ArciumModel::SetLastActiveTab(SpaceId id, TabKey key) {
  Space* space = FindSpace(id);
  if (!space || space->last_active_tab == key) {
    return;
  }
  space->last_active_tab = key;
  Notify();
}

void ArciumModel::SetSpaceSplit(SpaceId id, std::optional<SpaceSplit> split) {
  Space* space = FindSpace(id);
  if (!space || space->split == split) {
    return;
  }
  space->split = std::move(split);
  Notify();
}

void ArciumModel::SetLastActiveSpace(SpaceId id) {
  if (last_active_space_ == id) {
    return;
  }
  last_active_space_ = id;
  Notify();
}

void ArciumModel::ReorderSpace(SpaceId id, int new_position) {
  if (!GetSpace(id)) {
    return;
  }
  new_position =
      std::clamp(new_position, 0, static_cast<int>(spaces_.size()) - 1);
  std::vector<Space> order;
  order.reserve(spaces_.size());
  Space moved;
  for (Space& space : spaces_) {
    if (space.id == id) {
      moved = std::move(space);
    } else {
      order.push_back(std::move(space));
    }
  }
  order.insert(order.begin() + new_position, std::move(moved));
  for (size_t i = 0; i < order.size(); ++i) {
    order[i].position = static_cast<int>(i);
  }
  spaces_ = std::move(order);
  Notify();
}

void ArciumModel::RemoveSpace(SpaceId id) {
  // Every tab is in a space, so the last one cannot go: a model with no
  // space at all is a state nothing downstream can draw.
  if (spaces_.size() <= 1 || !GetSpace(id)) {
    return;
  }
  std::erase_if(entries_, [id](const TabEntry& e) { return e.space_id == id; });
  std::erase_if(folders_, [id](const Folder& f) { return f.space_id == id; });
  // A rule sending a site to a space that is gone would send it nowhere.
  std::erase_if(routing_rules_,
                [id](const RoutingRule& r) { return r.space_id == id; });
  std::erase_if(spaces_, [id](const Space& s) { return s.id == id; });
  for (size_t i = 0; i < spaces_.size(); ++i) {
    spaces_[i].position = static_cast<int>(i);
  }
  NormalisePositions();
  Notify();
}

void ArciumModel::SetArchiveTimeout(SpaceId space_id, ArchiveTimeout timeout) {
  for (Space& space : spaces_) {
    if (space.id == space_id) {
      space.archive_timeout = timeout;
      Notify();
      return;
    }
  }
}

EntryId ArciumModel::AddEntry(SpaceId space_id,
                              EntryKind kind,
                              const GURL& url,
                              const std::u16string& title) {
  TabEntry entry;
  entry.id = EntryId::Generate();
  entry.kind = kind;
  entry.space_id = space_id;
  entry.url = url;
  entry.last_title = title;
  entry.created_at = base::Time::Now();
  entry.position =
      static_cast<int>(EntriesForKind(entry.space_id, kind).size());
  const EntryId id = entry.id;
  entries_.push_back(std::move(entry));
  Notify();
  return id;
}

void ArciumModel::MoveEntryToSpace(EntryId id, SpaceId space_id) {
  TabEntry* entry = FindEntry(id);
  if (!entry || !GetSpace(space_id) || entry->space_id == space_id) {
    return;
  }
  entry->space_id = space_id;
  // Folders do not cross spaces, so the entry arrives at the top level.
  entry->folder_id.reset();
  entry->position =
      static_cast<int>(EntriesForKind(space_id, entry->kind).size());
  NormalisePositions();
  Notify();
}

void ArciumModel::RemoveEntry(EntryId id) {
  const size_t before = entries_.size();
  std::erase_if(entries_,
                [id](const TabEntry& entry) { return entry.id == id; });
  if (entries_.size() != before) {
    NormalisePositions();
    Notify();
  }
}

void ArciumModel::SetEntryKind(EntryId id, EntryKind kind) {
  TabEntry* entry = FindEntry(id);
  if (!entry || entry->kind == kind) {
    return;
  }
  entry->kind = kind;
  // A favourite is never inside a folder: folders hold pinned entries only.
  if (kind == EntryKind::kFavorite) {
    entry->folder_id.reset();
  }
  entry->position =
      static_cast<int>(EntriesForKind(entry->space_id, kind).size());
  NormalisePositions();
  Notify();
}

void ArciumModel::SetCustomTitle(EntryId id, const std::u16string& title) {
  TabEntry* entry = FindEntry(id);
  if (!entry) {
    return;
  }
  entry->custom_title = title;
  Notify();
}

void ArciumModel::SetLastTitle(EntryId id, const std::u16string& title) {
  TabEntry* entry = FindEntry(id);
  if (!entry || entry->last_title == title) {
    return;
  }
  entry->last_title = title;
  Notify();
}

void ArciumModel::SetEntryFolder(EntryId id,
                                 std::optional<FolderId> folder_id) {
  TabEntry* entry = FindEntry(id);
  if (!entry) {
    return;
  }
  entry->folder_id = folder_id;
  Notify();
}

void ArciumModel::ReorderEntry(EntryId id, int new_position) {
  TabEntry* entry = FindEntry(id);
  if (!entry) {
    return;
  }
  const SpaceId space_id = entry->space_id;
  const EntryKind kind = entry->kind;
  std::vector<const TabEntry*> siblings = EntriesForKind(space_id, kind);
  new_position =
      std::clamp(new_position, 0, static_cast<int>(siblings.size()) - 1);

  // Renumber by walking the sibling order with the moved entry lifted out and
  // reinserted, so positions stay 0..n-1 with no gaps.
  std::vector<EntryId> order;
  order.reserve(siblings.size());
  for (const TabEntry* sibling : siblings) {
    if (sibling->id != id) {
      order.push_back(sibling->id);
    }
  }
  order.insert(order.begin() + new_position, id);
  for (size_t i = 0; i < order.size(); ++i) {
    FindEntry(order[i])->position = static_cast<int>(i);
  }
  Notify();
}

const TabEntry* ArciumModel::GetEntry(EntryId id) const {
  for (const TabEntry& entry : entries_) {
    if (entry.id == id) {
      return &entry;
    }
  }
  return nullptr;
}

std::vector<const TabEntry*> ArciumModel::EntriesForKind(SpaceId space_id,
                                                         EntryKind kind) const {
  std::vector<const TabEntry*> result;
  for (const TabEntry& entry : entries_) {
    if (entry.space_id == space_id && entry.kind == kind) {
      result.push_back(&entry);
    }
  }
  std::sort(result.begin(), result.end(),
            [](const TabEntry* a, const TabEntry* b) {
              return a->position < b->position;
            });
  return result;
}

FolderId ArciumModel::AddFolder(SpaceId space_id,
                                const std::u16string& name,
                                std::optional<FolderId> parent_id) {
  Folder folder;
  folder.id = FolderId::Generate();
  folder.space_id = space_id;
  folder.name = name;
  // A parent the model does not have, one in another space, or one already at
  // the cap leaves the new folder at the top level rather than stranded under
  // an id nothing can reach.
  if (parent_id.has_value()) {
    const Folder* parent = GetFolder(*parent_id);
    if (parent && parent->space_id == folder.space_id &&
        FolderDepth(*parent_id) + 1 <= kMaxFolderDepth - 1) {
      folder.parent_id = parent_id;
    }
  }
  folder.position = static_cast<int>(std::count_if(
      folders_.begin(), folders_.end(), [&folder](const Folder& existing) {
        return existing.space_id == folder.space_id &&
               existing.parent_id == folder.parent_id;
      }));
  const FolderId id = folder.id;
  folders_.push_back(std::move(folder));
  Notify();
  return id;
}

void ArciumModel::RemoveFolder(FolderId id) {
  const Folder* doomed = GetFolder(id);
  if (!doomed) {
    return;
  }
  // Read before the erase invalidates the pointer.
  const std::optional<FolderId> parent = doomed->parent_id;
  std::erase_if(folders_, [id](const Folder& f) { return f.id == id; });
  // A folder groups, it does not own. Everything it held moves up one level:
  // to the removed folder's own parent, which for a top-level folder is the
  // top level -- exactly what this did before there was another level to move
  // to. Subfolders as well as entries, or a subfolder would be left pointing
  // at an id nothing has.
  for (Folder& folder : folders_) {
    if (folder.parent_id == id) {
      folder.parent_id = parent;
    }
  }
  for (TabEntry& entry : entries_) {
    if (entry.folder_id == id) {
      entry.folder_id = parent;
    }
  }
  NormalisePositions();
  Notify();
}

void ArciumModel::SetFolderName(FolderId id, const std::u16string& name) {
  Folder* folder = FindFolder(id);
  if (!folder) {
    return;
  }
  folder->name = name;
  Notify();
}

void ArciumModel::SetFolderCollapsed(FolderId id, bool collapsed) {
  Folder* folder = FindFolder(id);
  if (!folder || folder->collapsed == collapsed) {
    return;
  }
  folder->collapsed = collapsed;
  Notify();
}

void ArciumModel::SetFolderParent(FolderId id,
                                  std::optional<FolderId> parent_id) {
  Folder* folder = FindFolder(id);
  if (!folder || folder->parent_id == parent_id ||
      !CanMoveFolderTo(id, parent_id)) {
    return;
  }
  folder->parent_id = parent_id;
  // Last among its new siblings, which is where a drop that named a folder
  // rather than a slot should land it. NormalisePositions turns this back
  // into a contiguous number.
  folder->position = std::numeric_limits<int>::max();
  NormalisePositions();
  Notify();
}

const Folder* ArciumModel::GetFolder(FolderId id) const {
  for (const Folder& folder : folders_) {
    if (folder.id == id) {
      return &folder;
    }
  }
  return nullptr;
}

int ArciumModel::FolderDepth(FolderId id) const {
  const Folder* folder = GetFolder(id);
  if (!folder) {
    return -1;
  }
  int depth = 0;
  std::optional<FolderId> parent = folder->parent_id;
  // Bounded by the folder count rather than trusting the chain to end. Every
  // mutation refuses a cycle and the deserializer repairs one, so this cannot
  // spin today; if a later change lets one through, failing closed beats
  // hanging the UI thread.
  const int limit = static_cast<int>(folders_.size());
  while (parent.has_value() && depth <= limit) {
    const Folder* next = GetFolder(*parent);
    if (!next) {
      // A parent the model does not have: what is left is a root.
      break;
    }
    ++depth;
    parent = next->parent_id;
  }
  return depth;
}

int ArciumModel::SubtreeHeight(FolderId id) const {
  int height = 0;
  std::vector<FolderId> level = {id};
  const int limit = static_cast<int>(folders_.size());
  // A level at a time, so the walk is bounded by the folder count however
  // wide the tree is.
  while (!level.empty() && height <= limit) {
    std::vector<FolderId> next;
    for (const Folder& folder : folders_) {
      if (folder.parent_id.has_value() &&
          std::find(level.begin(), level.end(), *folder.parent_id) !=
              level.end()) {
        next.push_back(folder.id);
      }
    }
    if (next.empty()) {
      break;
    }
    ++height;
    level = std::move(next);
  }
  return height;
}

bool ArciumModel::CanMoveFolderTo(FolderId id,
                                  std::optional<FolderId> parent_id) const {
  const Folder* folder = GetFolder(id);
  if (!folder) {
    return false;
  }
  int new_depth = 0;
  if (parent_id.has_value()) {
    if (*parent_id == id) {
      return false;
    }
    const Folder* parent = GetFolder(*parent_id);
    if (!parent || parent->space_id != folder->space_id) {
      return false;
    }
    // Walking up from the proposed parent is what catches a descendant: if
    // `id` sits anywhere above it, the move would close the chain into a
    // cycle.
    std::optional<FolderId> above = parent->parent_id;
    int guard = 0;
    const int limit = static_cast<int>(folders_.size());
    while (above.has_value() && guard++ <= limit) {
      if (*above == id) {
        return false;
      }
      const Folder* next = GetFolder(*above);
      if (!next) {
        break;
      }
      above = next->parent_id;
    }
    new_depth = FolderDepth(*parent_id) + 1;
  }
  // The moved folder brings its own descendants with it, so what has to fit
  // is the whole subtree, not just its root.
  return new_depth + SubtreeHeight(id) <= kMaxFolderDepth - 1;
}

void ArciumModel::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void ArciumModel::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void ArciumModel::ReplaceAll(std::vector<ArciumProfile> profiles,
                             std::vector<Space> spaces,
                             std::vector<Folder> folders,
                             std::vector<TabEntry> entries,
                             std::vector<RoutingRule> routing_rules) {
  profiles_ = std::move(profiles);
  spaces_ = std::move(spaces);
  folders_ = std::move(folders);
  entries_ = std::move(entries);
  if (spaces_.empty()) {
    Space space;
    space.id = SpaceId::Generate();
    space.name = u"Space";
    space.profile_id = DefaultProfileId();
    spaces_.push_back(std::move(space));
  }
  // Loaded order is not trusted to already be position order, and
  // spaces().front() has to be the first space.
  std::sort(spaces_.begin(), spaces_.end(), [](const Space& a, const Space& b) {
    return a.position < b.position;
  });
  // Renumbered too: a gap a hand-edited file left would let a later
  // AddSpace, which takes position size(), sort ahead of a loaded space on
  // the next load.
  for (size_t i = 0; i < spaces_.size(); ++i) {
    spaces_[i].position = static_cast<int>(i);
  }
  if (!GetSpace(last_active_space_)) {
    last_active_space_ = SpaceId();
  }
  // Kept only for spaces that survived, and one per site: a hand-edited file
  // listing a site twice keeps the first, as SetRoutingRule would.
  routing_rules_.clear();
  for (RoutingRule& rule : routing_rules) {
    rule.site = NormaliseRuleSite(rule.site);
    const bool duplicate = std::any_of(
        routing_rules_.begin(), routing_rules_.end(),
        [&rule](const RoutingRule& kept) { return kept.site == rule.site; });
    if (rule.site.empty() || !GetSpace(rule.space_id) || duplicate) {
      continue;
    }
    routing_rules_.push_back(std::move(rule));
  }
  NormaliseProfiles();
  NormalisePositions();
  Notify();
}

void ArciumModel::SetRoutingRule(std::string_view site, SpaceId space_id) {
  const std::string normalised = NormaliseRuleSite(site);
  if (normalised.empty() || !GetSpace(space_id)) {
    return;
  }
  for (RoutingRule& rule : routing_rules_) {
    if (rule.site == normalised) {
      if (rule.space_id == space_id) {
        return;
      }
      rule.space_id = space_id;
      Notify();
      return;
    }
  }
  routing_rules_.push_back({normalised, space_id});
  Notify();
}

void ArciumModel::RemoveRoutingRule(std::string_view site) {
  const std::string normalised = NormaliseRuleSite(site);
  if (std::erase_if(routing_rules_, [&normalised](const RoutingRule& r) {
        return r.site == normalised;
      })) {
    Notify();
  }
}

SpaceId ArciumModel::SpaceForUrl(const GURL& url) const {
  const RoutingRule* rule = BestRuleForUrl(routing_rules_, url);
  return rule ? rule->space_id : SpaceId();
}

TabEntry* ArciumModel::FindEntry(EntryId id) {
  for (TabEntry& entry : entries_) {
    if (entry.id == id) {
      return &entry;
    }
  }
  return nullptr;
}

Folder* ArciumModel::FindFolder(FolderId id) {
  for (Folder& folder : folders_) {
    if (folder.id == id) {
      return &folder;
    }
  }
  return nullptr;
}

Space* ArciumModel::FindSpace(SpaceId id) {
  for (Space& space : spaces_) {
    if (space.id == id) {
      return &space;
    }
  }
  return nullptr;
}

void ArciumModel::NormalisePositions() {
  for (const Space& space : spaces_) {
    for (EntryKind kind : {EntryKind::kFavorite, EntryKind::kPinned}) {
      std::vector<const TabEntry*> ordered = EntriesForKind(space.id, kind);
      for (size_t i = 0; i < ordered.size(); ++i) {
        FindEntry(ordered[i]->id)->position = static_cast<int>(i);
      }
    }

    // Folders are numbered among their siblings -- same space, same parent --
    // so a nested folder's position is a place in its own list rather than in
    // the space's. stable_sort, not sort: SetFolderParent parks a moved
    // folder on INT_MAX and two folders can briefly share a position, and a
    // tie must resolve the same way every run.
    std::map<std::optional<FolderId>, std::vector<Folder*>> by_parent;
    for (Folder& folder : folders_) {
      if (folder.space_id == space.id) {
        by_parent[folder.parent_id].push_back(&folder);
      }
    }
    for (auto& [parent, siblings] : by_parent) {
      std::stable_sort(siblings.begin(), siblings.end(),
                       [](const Folder* a, const Folder* b) {
                         return a->position < b->position;
                       });
      for (size_t i = 0; i < siblings.size(); ++i) {
        siblings[i]->position = static_cast<int>(i);
      }
    }
  }
}

void ArciumModel::Notify() {
  for (Observer& observer : observers_) {
    observer.OnArciumModelChanged();
  }
}

}  // namespace arcium
