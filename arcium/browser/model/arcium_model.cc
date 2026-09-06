// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/model/arcium_model.h"

#include <algorithm>

#include "base/time/time.h"

namespace arcium {

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
  Space space;
  space.id = SpaceId::Generate();
  space.name = u"Space";
  space.archive_timeout = ArchiveTimeout::kTwelveHours;
  space.position = 0;
  spaces_.push_back(std::move(space));
}

ArciumModel::~ArciumModel() = default;

SpaceId ArciumModel::default_space_id() const {
  return spaces_.empty() ? SpaceId() : spaces_.front().id;
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

EntryId ArciumModel::AddEntry(EntryKind kind,
                              const GURL& url,
                              const std::u16string& title) {
  TabEntry entry;
  entry.id = EntryId::Generate();
  entry.kind = kind;
  entry.space_id = default_space_id();
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

void ArciumModel::SetEntryUrl(EntryId id, const GURL& url) {
  TabEntry* entry = FindEntry(id);
  if (!entry || entry->url == url) {
    return;
  }
  entry->url = url;
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

FolderId ArciumModel::AddFolder(const std::u16string& name) {
  Folder folder;
  folder.id = FolderId::Generate();
  folder.space_id = default_space_id();
  folder.name = name;
  folder.position = static_cast<int>(std::count_if(
      folders_.begin(), folders_.end(), [&folder](const Folder& existing) {
        return existing.space_id == folder.space_id;
      }));
  const FolderId id = folder.id;
  folders_.push_back(std::move(folder));
  Notify();
  return id;
}

void ArciumModel::RemoveFolder(FolderId id) {
  const size_t before = folders_.size();
  std::erase_if(folders_, [id](const Folder& f) { return f.id == id; });
  if (folders_.size() == before) {
    return;
  }
  // A folder groups entries, it does not own them.
  for (TabEntry& entry : entries_) {
    if (entry.folder_id == id) {
      entry.folder_id.reset();
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

const Folder* ArciumModel::GetFolder(FolderId id) const {
  for (const Folder& folder : folders_) {
    if (folder.id == id) {
      return &folder;
    }
  }
  return nullptr;
}

void ArciumModel::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void ArciumModel::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void ArciumModel::ReplaceAll(std::vector<Space> spaces,
                             std::vector<Folder> folders,
                             std::vector<TabEntry> entries) {
  spaces_ = std::move(spaces);
  folders_ = std::move(folders);
  entries_ = std::move(entries);
  if (spaces_.empty()) {
    Space space;
    space.id = SpaceId::Generate();
    space.name = u"Space";
    spaces_.push_back(std::move(space));
  }
  NormalisePositions();
  Notify();
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

void ArciumModel::NormalisePositions() {
  for (const Space& space : spaces_) {
    for (EntryKind kind : {EntryKind::kFavorite, EntryKind::kPinned}) {
      std::vector<const TabEntry*> ordered = EntriesForKind(space.id, kind);
      for (size_t i = 0; i < ordered.size(); ++i) {
        FindEntry(ordered[i]->id)->position = static_cast<int>(i);
      }
    }

    // Folders are not split by kind, so renumber them as a single sequence
    // per space, the same way RemoveFolder and AddFolder must agree on
    // "the next free position" within that space.
    std::vector<Folder*> folders_in_space;
    for (Folder& folder : folders_) {
      if (folder.space_id == space.id) {
        folders_in_space.push_back(&folder);
      }
    }
    std::sort(folders_in_space.begin(), folders_in_space.end(),
              [](const Folder* a, const Folder* b) {
                return a->position < b->position;
              });
    for (size_t i = 0; i < folders_in_space.size(); ++i) {
      folders_in_space[i]->position = static_cast<int>(i);
    }
  }
}

void ArciumModel::Notify() {
  for (Observer& observer : observers_) {
    observer.OnArciumModelChanged();
  }
}

}  // namespace arcium
