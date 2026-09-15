// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_PLAYGROUND_FAKE_SIDEBAR_MODEL_H_
#define ARCIUM_UI_PLAYGROUND_FAKE_SIDEBAR_MODEL_H_

#include <map>
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
  // A tab that exists without having loaded, the way a restart leaves every
  // tab but the one on screen. The real model never marks the active row;
  // neither does this.
  void SetUnloaded(int tab_index, bool unloaded);
  void SetAudible(int tab_index, bool audible);
  void SetCanReturnToPinnedUrl(int tab_index, bool can_return);
  // Seeds a folder so the playground can show a header without a menu round
  // trip. `titles` name pinned rows already added.
  FolderId AddFolderWith(const std::u16string& name,
                         const std::vector<std::u16string>& titles);
  // Seeds a space with the icon and gradient a real one would carry, without
  // switching to it -- AddSpace, the SidebarModel command, does switch;
  // seeding a test's spaces should never move the mark a preceding
  // EXPECT_TRUE(spaces()[0].is_active) is about to check.
  SpaceId AddSpaceForTesting(const std::u16string& name,
                             const std::u16string& icon,
                             int gradient);
  // Seeds a profile without assigning it to any space.
  ProfileId AddProfileForTesting(const std::u16string& name, int color);
  // Every profile ClearProfileData was asked to clear, in order.
  const std::vector<ProfileId>& cleared_profiles_for_testing() const {
    return cleared_profiles_;
  }
  // Seeds a Today row tagged into `space`, not active: AddTab puts every row
  // in the first space, so this is how a test fills another one without
  // switching to it.
  void AddTabInSpaceForTesting(const std::u16string& title,
                               const std::string& url,
                               SpaceId space);
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
  bool CanCreateFolderWithEntry(EntryId id) const override;
  FolderId CreateFolderWithEntry(EntryId id,
                                 const std::u16string& name) override;
  void MoveEntryToFolder(EntryId id,
                         std::optional<FolderId> folder_id) override;
  void SetFolderParent(FolderId id, std::optional<FolderId> parent_id) override;
  bool CanMoveFolderTo(FolderId id,
                       std::optional<FolderId> parent_id) const override;
  void SetFolderName(FolderId id, const std::u16string& name) override;
  void DeleteFolder(FolderId id) override;
  std::vector<SidebarSpace> spaces() const override;
  void SwitchToSpace(SpaceId id) override;
  void AddSpace(const std::u16string& name) override;
  void RenameSpace(SpaceId id, const std::u16string& name) override;
  void SetSpaceIcon(SpaceId id, const std::u16string& icon) override;
  void SetSpaceGradient(SpaceId id, int gradient) override;
  void MoveSpace(SpaceId id, int position) override;
  void DeleteSpace(SpaceId id) override;
  void MoveTabToSpace(int tab_index, SpaceId space_id) override;
  void MoveEntryToSpace(EntryId id, SpaceId space_id) override;
  bool SiteOpensInActiveSpace(const GURL& url) const override;
  void SetSiteOpensInActiveSpace(const GURL& url, bool opens_here) override;
  std::vector<SidebarProfile> profiles() const override;
  void CreateProfileForSpace(SpaceId space,
                             const std::u16string& name,
                             int color) override;
  void SetSpaceProfile(SpaceId space, ProfileId profile) override;
  void RenameProfile(ProfileId id, const std::u16string& name) override;
  void SetProfileColor(ProfileId id, int color) override;
  void ClearProfileData(ProfileId id) override;
  void DeleteProfile(ProfileId id) override;
  void SetArchiveTimeout(ArchiveTimeout timeout) override;
  ArchiveTimeout archive_timeout() const override;
  bool has_archive() const override;
  void RequestArchivedRows(int limit, ArchivedRowsCallback callback) override;
  void ReopenArchived(const GURL& url, base::Time archived_at) override;
  void AddObserver(Observer* observer) override;
  void RemoveObserver(Observer* observer) override;

 private:
  // Site to space, the one fact ArciumModel's routing rules keep that the
  // menu can see.
  std::map<std::string, SpaceId> site_rules_;

  // Name, parent, collapsed state and position; the count is derived from the
  // rows on demand, which is what folders() hands the views precomputed.
  // `position` is here because ArciumModel has it and the flattening orders
  // by it: a fake without it would let a folder-ordering regression pass.
  struct FakeFolder {
    FolderId id;
    std::optional<FolderId> parent_id;
    std::u16string name;
    bool collapsed = false;
    int position = 0;
  };

  void Notify();
  void Reindex();
  // AddTab with the space named: AddTab seeds the first space, and NewTab
  // opens its tab in the one on screen.
  void InsertTab(const std::u16string& title,
                 const std::string& url,
                 SidebarSection section,
                 bool active,
                 SpaceId space);
  // Renumbers folders_ to 0..n-1 in position order, the way
  // ArciumModel::NormalisePositions does after RemoveFolder. Without this,
  // CreateFolderWithEntry's "next free position" — folders_.size() — can
  // collide with a folder that kept the position it had before a deletion.
  void NormaliseFolderPositions();
  SidebarRow* FindByTabIndex(int tab_index);
  SidebarRow* FindByEntry(EntryId id);
  const SidebarRow* FindByEntry(EntryId id) const;
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

  // The id of whichever space is currently marked active, or the first
  // space's when somehow none is -- the same fallback the real model's
  // default_space_id() gives a caller with no window to ask.
  SpaceId ActiveSpaceId() const;
  SidebarSpace* FindSpace(SpaceId id);
  // Marks `id` as the one space on screen. What SwitchToSpace does, and what
  // a move of the active row does in place of the real switcher's adoption.
  void MarkActiveSpace(SpaceId id);
  SidebarProfile* FindProfile(ProfileId id);

  std::vector<SidebarRow> rows_;
  std::vector<SidebarSpace> spaces_;
  // Default first, as the real model keeps it.
  std::vector<SidebarProfile> profiles_ = {
      {.id = DefaultProfileId(), .name = u"Default", .color = 0}};
  std::vector<ProfileId> cleared_profiles_;
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
