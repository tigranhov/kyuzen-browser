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

  // Spaces. Stage 2 always had exactly one; Stage 3a made it several.
  const std::vector<Space>& spaces() const { return spaces_; }
  // The first space in position order. A fallback for data that names no
  // space and for callers with no window to ask; the space a window is
  // *showing* comes from its SpaceSwitcher, never from here.
  SpaceId default_space_id() const;
  // The space last active at quit, or the first space when that one has gone.
  SpaceId last_active_space() const;
  // The space `id` names, or null. The accessor GetEntry and GetFolder always
  // had: without it every caller that wanted a space's settings wrote its own
  // scan over spaces() with its own fallback beside it, and two of them had
  // drifted into being byte-identical copies.
  const Space* GetSpace(SpaceId id) const;

  SpaceId AddSpace(const std::u16string& name);
  void RenameSpace(SpaceId id, const std::u16string& name);
  void SetSpaceIcon(SpaceId id, const std::u16string& icon);
  void SetSpaceGradient(SpaceId id, int gradient);
  void SetLastActiveTab(SpaceId id, TabKey key);
  void SetLastActiveSpace(SpaceId id);
  void ReorderSpace(SpaceId id, int new_position);
  // Removes the space with its entries and folders. Refuses the last space:
  // every tab has to be in one, so a model with none is unrepresentable.
  // The space's archive rows are ArchiveService's to delete, on its own
  // sequence.
  void RemoveSpace(SpaceId id);
  void SetArchiveTimeout(SpaceId space_id, ArchiveTimeout timeout);

  // Entries. Every mutation notifies observers, and every one that names an
  // unknown id is a no-op rather than a crash, because ids arrive from disk.
  // The space is explicit: an entry made into "whichever space is first"
  // would land in the wrong one for every window not showing that space.
  EntryId AddEntry(SpaceId space_id,
                   EntryKind kind,
                   const GURL& url,
                   const std::u16string& title);
  // Puts the entry at the end of `space_id`'s top level, with no folder:
  // folders do not cross spaces.
  void MoveEntryToSpace(EntryId id, SpaceId space_id);
  void RemoveEntry(EntryId id);
  void SetEntryKind(EntryId id, EntryKind kind);
  void SetCustomTitle(EntryId id, const std::u16string& title);
  void SetLastTitle(EntryId id, const std::u16string& title);
  void SetEntryFolder(EntryId id, std::optional<FolderId> folder_id);
  void ReorderEntry(EntryId id, int new_position);

  const TabEntry* GetEntry(EntryId id) const;
  std::vector<const TabEntry*> EntriesForKind(SpaceId space_id,
                                              EntryKind kind) const;
  const std::vector<TabEntry>& entries() const { return entries_; }

  // Folders. Removing one returns its contents to its own parent rather than
  // deleting them: a folder is a grouping, not an owner.
  FolderId AddFolder(SpaceId space_id,
                     const std::u16string& name,
                     std::optional<FolderId> parent_id = std::nullopt);
  void RemoveFolder(FolderId id);
  void SetFolderName(FolderId id, const std::u16string& name);
  void SetFolderCollapsed(FolderId id, bool collapsed);
  // How deep `id` sits: 0 at the top level, -1 for an id the model does not
  // have.
  int FolderDepth(FolderId id) const;
  // Whether `id` could become a child of `parent_id`, or of the top level for
  // std::nullopt. False for an unknown id, a parent in another space, the
  // folder itself, any of its own descendants, and any move that would carry
  // the moved subtree past kMaxFolderDepth.
  bool CanMoveFolderTo(FolderId id, std::optional<FolderId> parent_id) const;
  // Re-parents `id`, or does nothing when CanMoveFolderTo says no. One
  // command rather than a check the caller is trusted to have made first: a
  // drag asks from a snapshot of the tree that another window can invalidate
  // while the nested drag loop runs, so the answer has to be taken at the
  // moment of the move.
  void SetFolderParent(FolderId id, std::optional<FolderId> parent_id);
  const Folder* GetFolder(FolderId id) const;
  const std::vector<Folder>& folders() const { return folders_; }

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // For ModelSerializer, which rebuilds a model from disk without firing a
  // notification per entry.
  void ReplaceAll(std::vector<Space> spaces,
                  std::vector<Folder> folders,
                  std::vector<TabEntry> entries);

  // For tests that predate spaces and only ever meant the first one.
  EntryId AddEntryForTesting(EntryKind kind,
                             const GURL& url,
                             const std::u16string& title) {
    return AddEntry(default_space_id(), kind, url, title);
  }
  FolderId AddFolderForTesting(
      const std::u16string& name,
      std::optional<FolderId> parent_id = std::nullopt) {
    return AddFolder(default_space_id(), name, parent_id);
  }

 private:
  TabEntry* FindEntry(EntryId id);
  Folder* FindFolder(FolderId id);
  Space* FindSpace(SpaceId id);
  // The greatest number of levels below `id`: 0 when it holds no folders.
  int SubtreeHeight(FolderId id) const;
  // Renumbers positions 0..n-1 within each (space, kind) so a reorder never
  // leaves gaps that would make the order depend on insertion history.
  void NormalisePositions();
  void Notify();

  std::vector<Space> spaces_;
  SpaceId last_active_space_;
  std::vector<Folder> folders_;
  std::vector<TabEntry> entries_;
  base::ObserverList<Observer> observers_;
};

}  // namespace arcium

#endif  // ARCIUM_BROWSER_MODEL_ARCIUM_MODEL_H_
