// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_PLAYGROUND_FAKE_SIDEBAR_MODEL_H_
#define ARCIUM_UI_PLAYGROUND_FAKE_SIDEBAR_MODEL_H_

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "arcium/ui/sidebar/sidebar_model.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"

namespace arcium {

// In-memory SidebarModel for the playground and view tests. Commands mutate
// the vector the way TabStripModel would and notify observers.
//
// One divergence a reader has to know about: this notifies **synchronously**,
// from inside the command. SidebarTabModel coalesces and posts, so a test
// passing here is not proof that the posted path works — it proves the view
// half, and the model half is covered by the tests over the real model.
class FakeSidebarModel : public SidebarModel {
 public:
  FakeSidebarModel();
  ~FakeSidebarModel() override;

  void AddTab(const std::u16string& title,
              const std::string& url,
              SidebarSection section,
              bool active);
  // A persistent entry with no tab behind it, the way the browser draws one
  // that has never been opened this session.
  void AddColdEntry(const std::u16string& title,
                    const std::string& url,
                    SidebarSection section);
  void SetLoading(int tab_index, bool loading);
  void SetAudible(int tab_index, bool audible);
  void SetCanReturnToPinnedUrl(int tab_index, bool can_return);
  // Seeds a folder so the playground can show a header without a menu round
  // trip. `titles` name pinned rows already added.
  FolderId AddFolderWith(const std::u16string& name,
                         const std::vector<std::u16string>& titles);
  // Folders come back in `position` order, exactly as they do from the real
  // model. Nothing in the UI reorders folders yet, so this is how a test puts
  // them in an order that disagrees with the order they were made in.
  void SetFolderPosition(FolderId id, int position);

  // Seeds the fake archive, newest first however they are added.
  void AddArchived(const std::u16string& title,
                   const std::string& url,
                   base::Time archived_at);
  // Off the record the real model has no archive and the sidebar shows no
  // archive button. The playground and the view tests need to be able to sit
  // on both sides of that.
  void SetHasArchive(bool has_archive);
  // What the real model reports when the archive file would not open: the
  // window has an archive, the button is there, and the read comes back with
  // no rows and readable=false. Distinct from an archive that is simply
  // empty, which is what this fake does by default.
  void SetArchiveReadable(bool readable);
  // Holds every reply instead of delivering it, so a test can decide the
  // instant it lands. The real reply comes back from a SQLite read on a
  // background sequence and can arrive at any point after the request,
  // including after the view that asked for it is gone — which is the one
  // ordering a test cannot otherwise produce, because closing the bubble
  // frees the delegate in a task posted *after* the reply is already queued.
  void SetHoldArchiveReplies(bool hold);
  // Delivers everything SetHoldArchiveReplies held back, in order.
  void DeliverHeldArchiveReplies();
  size_t held_archive_reply_count() const { return held_replies_.size(); }
  // What ReopenArchived was asked to reopen, in order.
  const std::vector<ArchivedRow>& reopened() const { return reopened_; }
  // Whether a RequestArchivedRows reply is still queued. The reply is posted,
  // so a test that does not drain the run loop sees nothing.
  bool has_pending_archive_request() const {
    return pending_archive_requests_ > 0;
  }

  // SidebarModel:
  std::vector<SidebarRow> rows() const override;
  void ActivateTab(int tab_index) override;
  void CloseTab(int tab_index) override;
  void MoveTab(int from_index, int to_index) override;
  void NewTab() override;
  void ClearToday() override;
  void AddToFavorites(int tab_index) override;
  void PinTab(int tab_index) override;
  void MoveTabToSection(int tab_index,
                        SidebarSection section,
                        int position) override;
  void UnpinEntry(EntryId id) override;
  void ActivateEntry(EntryId id) override;
  void CloseEntryTab(EntryId id) override;
  void SetEntryTitle(EntryId id, const std::u16string& title) override;
  void SetTabTitle(int tab_index,
                   const GURL& expected_url,
                   const std::u16string& title) override;
  void ReturnToPinnedUrl(EntryId id) override;
  void MoveEntryToSection(EntryId id,
                          SidebarSection section,
                          int position) override;
  std::vector<SidebarFolder> folders() const override;
  void SetFolderCollapsed(FolderId id, bool collapsed) override;
  FolderId CreateFolderWithEntry(EntryId id,
                                 const std::u16string& name) override;
  void MoveEntryToFolder(EntryId id,
                         std::optional<FolderId> folder_id) override;
  void SetFolderName(FolderId id, const std::u16string& name) override;
  void DeleteFolder(FolderId id) override;
  void SetArchiveTimeout(ArchiveTimeout timeout) override;
  ArchiveTimeout archive_timeout() const override;
  bool has_archive() const override;
  void RequestArchivedRows(int limit, ArchivedRowsCallback callback) override;
  void ReopenArchived(const GURL& url, base::Time archived_at) override;
  void AddObserver(Observer* observer) override;
  void RemoveObserver(Observer* observer) override;

 private:
  // Name, collapsed state and position; the count is derived from the rows on
  // demand, which is what folders() hands the views precomputed. `position`
  // is here because ArciumModel has it and folders() sorts by it: a fake
  // without it would let a folder-ordering regression pass.
  struct FakeFolder {
    FolderId id;
    std::u16string name;
    bool collapsed = false;
    int position = 0;
  };

  void Notify();
  void Reindex();
  // Renumbers folders_ to 0..n-1 in position order, the way
  // ArciumModel::NormalisePositions does after RemoveFolder. Without this,
  // CreateFolderWithEntry's "next free position" — folders_.size() — can
  // collide with a folder that kept the position it had before a deletion.
  void NormaliseFolderPositions();
  SidebarRow* FindByTabIndex(int tab_index);
  SidebarRow* FindByEntry(EntryId id);
  SidebarRow* FindByTitle(const std::u16string& title);
  bool HasFolder(FolderId id) const;
  // Turns the tab at `tab_index` into an entry in `section`, at `position`
  // among that section's rows. A position past the section's end appends,
  // which is what AddToFavorites and PinTab ask for.
  void MakeEntry(int tab_index, SidebarSection section, int position);
  // The iterator for the `position`-th row of `section`, or the place a new
  // one would go when the section holds fewer — the walk ArciumModel's clamp
  // and SidebarTabModel's insert both come out at.
  std::vector<SidebarRow>::iterator SlotIn(SidebarSection section,
                                           int position);

  void DeliverArchivedRows(int limit, ArchivedRowsCallback callback);
  // The rows `limit` would answer with, applying the readable flag.
  std::vector<ArchivedRow> RowsFor(int limit) const;

  std::vector<SidebarRow> rows_;
  std::vector<FakeFolder> folders_;
  ArchiveTimeout archive_timeout_ = ArchiveTimeout::kTwelveHours;
  // The real archive is SQLite behind a posted read; this is a vector behind
  // a posted read. The asynchrony is the part worth copying — a fake that
  // answered inline would let a view that only works when the rows arrive
  // before it is laid out pass here and fail in the browser.
  std::vector<ArchivedRow> archived_;
  std::vector<ArchivedRow> reopened_;
  // Replies parked by SetHoldArchiveReplies, each already stripped of its
  // limit. They count as pending until they are delivered.
  std::vector<std::pair<int, ArchivedRowsCallback>> held_replies_;
  int pending_archive_requests_ = 0;
  bool has_archive_ = true;
  bool archive_readable_ = true;
  bool hold_archive_replies_ = false;
  base::ObserverList<Observer> observers_;
  base::WeakPtrFactory<FakeSidebarModel> weak_factory_{this};
};

}  // namespace arcium

#endif  // ARCIUM_UI_PLAYGROUND_FAKE_SIDEBAR_MODEL_H_
