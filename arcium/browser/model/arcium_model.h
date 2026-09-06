// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_MODEL_ARCIUM_MODEL_H_
#define ARCIUM_BROWSER_MODEL_ARCIUM_MODEL_H_

#include <optional>
#include <string>
#include <vector>

#include "arcium/browser/model/folder.h"
#include "arcium/browser/model/space.h"
#include "arcium/browser/model/tab_entry.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"

namespace arcium {

// Owns every persistent sidebar entity. Deliberately ignorant of tabs, disk
// and Views: everything here is data that survives a quit, which is what lets
// it be tested without a browser.
class ArciumModel {
 public:
  class Observer : public base::CheckedObserver {
   public:
    // Fired after any mutation. ModelStore schedules a save; views rebuild.
    virtual void OnArciumModelChanged() = 0;
  };

  ArciumModel();
  ArciumModel(const ArciumModel&) = delete;
  ArciumModel& operator=(const ArciumModel&) = delete;
  ~ArciumModel();

  // Spaces. Stage 2 always has exactly one.
  const std::vector<Space>& spaces() const { return spaces_; }
  SpaceId default_space_id() const;
  void SetArchiveTimeout(SpaceId space_id, ArchiveTimeout timeout);

  // Entries. Every mutation notifies observers, and every one that names an
  // unknown id is a no-op rather than a crash, because ids arrive from disk.
  EntryId AddEntry(EntryKind kind,
                   const GURL& url,
                   const std::u16string& title);
  void RemoveEntry(EntryId id);
  void SetEntryKind(EntryId id, EntryKind kind);
  void SetCustomTitle(EntryId id, const std::u16string& title);
  void SetLastTitle(EntryId id, const std::u16string& title);
  void SetEntryUrl(EntryId id, const GURL& url);
  void SetEntryFolder(EntryId id, std::optional<FolderId> folder_id);
  void ReorderEntry(EntryId id, int new_position);

  const TabEntry* GetEntry(EntryId id) const;
  std::vector<const TabEntry*> EntriesForKind(SpaceId space_id,
                                              EntryKind kind) const;
  const std::vector<TabEntry>& entries() const { return entries_; }

  // Folders. Removing one returns its entries to the top level rather than
  // deleting them: a folder is a grouping, not an owner.
  FolderId AddFolder(const std::u16string& name);
  void RemoveFolder(FolderId id);
  void SetFolderName(FolderId id, const std::u16string& name);
  void SetFolderCollapsed(FolderId id, bool collapsed);
  const Folder* GetFolder(FolderId id) const;
  const std::vector<Folder>& folders() const { return folders_; }

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // For ModelSerializer, which rebuilds a model from disk without firing a
  // notification per entry.
  void ReplaceAll(std::vector<Space> spaces,
                  std::vector<Folder> folders,
                  std::vector<TabEntry> entries);

 private:
  TabEntry* FindEntry(EntryId id);
  Folder* FindFolder(FolderId id);
  // Renumbers positions 0..n-1 within each (space, kind) so a reorder never
  // leaves gaps that would make the order depend on insertion history.
  void NormalisePositions();
  void Notify();

  std::vector<Space> spaces_;
  std::vector<Folder> folders_;
  std::vector<TabEntry> entries_;
  base::ObserverList<Observer> observers_;
};

}  // namespace arcium

#endif  // ARCIUM_BROWSER_MODEL_ARCIUM_MODEL_H_
