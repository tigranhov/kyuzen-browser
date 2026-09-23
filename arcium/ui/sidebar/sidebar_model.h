// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_SIDEBAR_MODEL_H_
#define ARCIUM_UI_SIDEBAR_SIDEBAR_MODEL_H_

#include <optional>
#include <string>
#include <vector>

#include "arcium/browser/model/arcium_profile.h"
#include "arcium/browser/model/entry_id.h"
#include "arcium/browser/model/space.h"
#include "base/functional/callback.h"
#include "base/observer_list_types.h"
#include "base/time/time.h"
#include "components/split_tabs/split_tab_id.h"
#include "ui/base/models/image_model.h"
#include "url/gurl.h"

namespace arcium {

enum class SidebarSection { kFavorites, kPinned, kToday };

// Everything a row needs to paint itself. Derived by the model, never by views.
struct SidebarRow {
  int tab_index = -1;
  SidebarSection section = SidebarSection::kToday;
  std::u16string title;
  ui::ImageModel favicon;
  bool is_active = false;
  bool is_loading = false;
  bool is_audible = false;
  bool is_muted = false;
  GURL url;
  // Set when the row is backed by a persistent entry. Invalid for a Today
  // tab, which has no entry.
  EntryId entry_id;
  // An entry with no live tab. It draws from last_title and the entry's URL,
  // and clicking it opens that URL.
  bool is_cold = false;
  // A tab exists but its page is not in memory: restored and not yet loaded,
  // or discarded to save memory. Clicking it loads the page it was left on.
  // Never set on a cold row, which has no tab, or on the active row.
  bool is_unloaded = false;

  // The split this row's tab shares the screen in, absent when it shares
  // with nothing. Two rows carrying the same value are the two halves of one
  // split, and both are active: being current means being in the foreground,
  // which is one tab normally and two in a split.
  std::optional<split_tabs::SplitTabId> split;
  // Whether this row is one half of a pair drawn as one row, and which half:
  // the row immediately before or after it in rows() is the other. Set by
  // GroupSplitRows, which puts the two next to each other in pane order. A
  // row in a split that is not joined -- one with a favourite, which is a
  // tile in a grid and never merges -- carries a small two-pane mark
  // instead.
  bool split_joins_previous = false;
  bool split_joins_next = false;
  // For a pinned entry linked to another as one split, that entry. Such a
  // pair is joined whether or not its tabs are open, which is what lets a
  // pinned split come back as one row after a relaunch.
  EntryId split_partner;
  // Where the row is drawn when that is not its own section: a Today tab
  // split with a pinned entry is drawn beside it in Pinned. Its folder is
  // copied the same way, so a collapsed folder hides both halves.
  std::optional<SidebarSection> drawn_section;
  SidebarSection DrawnSection() const {
    return drawn_section.value_or(section);
  }

  // Whether clicking this row has to load a page first: a cold entry opens
  // its URL, an unloaded tab reloads its page. The one question the views
  // ask before dimming.
  bool needs_load() const { return is_cold || is_unloaded; }

  // A warm pinned entry whose tab has navigated away from the pinned URL.
  bool can_return_to_pinned_url = false;
  // Set on rows inside a folder, so TabListView can indent and hide them.
  std::optional<FolderId> folder_id;
  // Which space this row belongs to. Every implementation fills it: the real
  // model with the space it is drawing, the only one it builds rows for, and
  // the fake with the space each of its rows was seeded, opened or moved
  // into.
  SpaceId space;
};

// Everything a folder header needs to paint itself. Like SidebarRow, it is
// prepared by the model: a header must never scan the rows to draw its count,
// and it has no way to reach the folder's name, depth or collapsed state
// otherwise.
struct SidebarFolder {
  FolderId id;
  // Absent at the top level. Never disagrees with `depth`: a folder reported
  // at depth 0 is reported with no parent, including one whose parent the
  // model could not find.
  std::optional<FolderId> parent_id;
  // 0 at the top level. What the list indents by, and -- because folders()
  // comes back in pre-order -- what lets a collapsed folder's subtree be
  // found as the run of folders after it with a greater depth.
  int depth = 0;
  std::u16string name;
  bool collapsed = false;
  // Entries in this folder's whole subtree, not just directly inside it. A
  // collapsed folder holding only subfolders would otherwise say "0" while
  // hiding everything under it.
  int entry_count = 0;
};

// One profile as the space menu lists it. Prepared by the model, like
// SidebarSpace.
struct SidebarProfile {
  ProfileId id;
  std::u16string name;
  // An index into profile_colors.h's palette.
  int color = 0;
};

// One space as the bar draws it. Prepared by the model, like SidebarRow: a
// dot must not scan tabs to say how many a delete would take.
struct SidebarSpace {
  SpaceId id;
  std::u16string name;
  // Empty means draw the first letter of the name.
  std::u16string icon;
  int gradient = 0;
  bool is_active = false;
  // What the delete confirmation promises, counted where the strip is.
  int open_tab_count = 0;
  int entry_count = 0;
  // Whose logins this space's tabs use. The badge draws the active space's.
  ProfileId profile_id = DefaultProfileId();
};

// One row of the archive list. Prepared by the model, like SidebarRow: the
// list derives nothing and knows nothing about SQLite.
struct ArchivedRow {
  GURL url;
  std::u16string title;
  // Half of the archive's primary key — `url` is the other half — which is
  // why both travel back with a reopen.
  base::Time archived_at;
};

// The sidebar's view of a window's tabs plus the commands it can issue. The
// browser implements it on top of TabStripModel; the playground uses a fake.
class SidebarModel {
 public:
  class Observer : public base::CheckedObserver {
   public:
    // Fired after any change. Views re-read rows() and rebuild; the model
    // coalesces bursts so one tab-strip event yields one notification.
    virtual void OnSidebarModelChanged() = 0;
  };

  virtual ~SidebarModel() = default;

  virtual std::vector<SidebarRow> rows() const = 0;

  virtual void ActivateTab(int tab_index) = 0;
  virtual void CloseTab(int tab_index) = 0;
  virtual void MoveTab(int from_index, int to_index) = 0;
  virtual void NewTab() = 0;
  virtual void ClearToday() = 0;

  // Entry commands. A row backed by an entry routes through these instead of
  // the tab-index commands above: the entry outlives the tab, so identity has
  // to be the entry's, not a position in the strip.
  // Both append, which is what a menu item means by them.
  virtual void AddToFavorites(int tab_index) = 0;
  virtual void PinTab(int tab_index) = 0;
  // Makes an entry for the tab at `tab_index` and puts it at `position` among
  // `section`'s entries. What a drop does, and the reason it is not the two
  // commands above: a drop drew an insertion indicator at a place before it
  // was taken, and appending would make that indicator a lie. A position past
  // the end of the section appends, so AddToFavorites and PinTab are this
  // command with no position asked for. kToday is a no-op — the tab is
  // already there, and the drop that would mean it is MoveTab.
  virtual void MoveTabToSection(int tab_index,
                                SidebarSection section,
                                int position) = 0;
  // Drops the entry. Its tab, if any, falls back into Today.
  virtual void UnpinEntry(EntryId id) = 0;
  // Focuses the entry's tab, or opens the entry's URL when it is cold.
  virtual void ActivateEntry(EntryId id) = 0;
  // Closes the entry's tab but keeps the entry, which turns cold.
  virtual void CloseEntryTab(EntryId id) = 0;
  virtual void SetEntryTitle(EntryId id, const std::u16string& title) = 0;
  // Renames a row with no entry -- a Today tab. The name is not persisted:
  // a Today tab is transient and nothing carries a name past its life, so
  // this dies with the tab rather than outliving what it named. An empty
  // `title` clears it and the row follows the page again.
  //
  // Takes the strip index because that is what a row holds, and resolves it
  // to the tab's handle before returning: an index is only true at the
  // instant it is read. `expected_url` is the URL the row showed when the
  // edit opened; a tab that no longer matches it is a different page in the
  // same slot, and the rename is dropped rather than landing on it.
  virtual void SetTabTitle(int tab_index,
                           const GURL& expected_url,
                           const std::u16string& title) = 0;
  // Navigates the entry's bound tab back to the entry's URL.
  virtual void ReturnToPinnedUrl(EntryId id) = 0;

  // Whether `row` could share the screen with the page on it: the row menu
  // asks before offering the item, so a row it would refuse does not offer
  // it. False for the row already on screen, for a row in another space, and
  // for anything already sharing.
  virtual bool CanSplitRow(const SidebarRow& row) const = 0;
  // Puts `row`'s page beside the page on screen, opening it first when the
  // row is cold. The row menu's "Split with current page".
  virtual void SplitRowWithCurrentPage(const SidebarRow& row) = 0;
  // Splits the page on screen with the one the reader was on before it, or
  // ends the split when there is one. What Cmd+Option+S does; the one way in
  // that needs no pointer, and the fastest way out.
  virtual void ToggleSplit() = 0;
  // Ends the split `row` is half of, leaving both pages open as two rows. A
  // pinned pair stops being one entry. The row menu's "End split".
  virtual void EndSplit(const SidebarRow& row) = 0;
  // Closes both halves of the split `row` is in. A pinned half keeps its
  // entry, cold, the way closing one pinned tab does. "Close both".
  virtual void CloseSplit(const SidebarRow& row) = 0;
  // Whether a row dragged onto the middle of `target` may split with it. The
  // dragged row is named the way a drag names it: an entry by id, a Today
  // tab by strip index. False for the same row, for either one already
  // sharing or joined to a partner, and for what the split rules refuse.
  // Asked on every drag move, so it must not allocate.
  virtual bool CanSplitByDrop(const SidebarRow& target,
                              EntryId dragged_entry,
                              int dragged_tab) const = 0;
  // Puts `target` on screen, opening it when it is cold, and the dragged row
  // beside it on the right. The drop on the middle of a row.
  virtual void SplitByDrop(const SidebarRow& target,
                           EntryId dragged_entry,
                           int dragged_tab) = 0;

  // Puts `id` in `section` at `position` among that section's entries. What a
  // drop does, and one command rather than a kind change followed by a
  // reorder: a drop changes both at once, so two calls would let an observer
  // see the entry in its new section still holding its old position, and a
  // failure between them would strand it mid-move.
  //
  // `position` is where the entry ends up once it has been lifted out of
  // wherever it was, not a gap in the section as it stands. The two readings
  // differ by one whenever an entry moves *down* inside its own section, and
  // it is the caller — the view that drew the insertion indicator — that
  // knows which of its rows was being dragged, so the conversion belongs
  // there.
  //
  // kToday drops the entry rather than moving it — Today is tabs, and a tab
  // is not an entry — but never the page: a warm entry's tab stays behind,
  // and a cold entry's URL is opened as a tab first, so nothing is lost and
  // the drop needs no undo. `position` still counts, because Today's order is
  // the tab strip's: the tab left behind moves to the `position`-th place
  // among Today's rows, which is what the insertion line promised.
  //
  // Naming an id the model does not have, or a section it cannot reach, is a
  // no-op rather than a crash, because the drag that issued this began from a
  // snapshot of rows().
  virtual void MoveEntryToSection(EntryId id,
                                  SidebarSection section,
                                  int position) = 0;

  // Folder commands. Folders hold pinned entries only, so every one of these
  // that names a favourite, a Today row or an id the model no longer has is a
  // no-op rather than a crash: menus are built from a snapshot of rows() and
  // the model can move underneath them while the menu is open.
  //
  // The default space's folders in `position` order, each with the number of
  // entries inside it already counted.
  virtual std::vector<SidebarFolder> folders() const = 0;
  virtual void SetFolderCollapsed(FolderId id, bool collapsed) = 0;
  // Makes a folder holding just `id`, inside whatever folder `id` is already
  // in -- so a folder made from a nested row appears where that row was drawn,
  // not at the top level. Returns an invalid id if it could not, which
  // includes the case where the entry's folder is already at kMaxFolderDepth.
  virtual FolderId CreateFolderWithEntry(EntryId id,
                                         const std::u16string& name) = 0;
  // The same question asked before the fact, so the menu item can be greyed
  // out instead of offering a folder that would silently not appear. Zen greys
  // its "New Subfolder" item on the same rule.
  virtual bool CanCreateFolderWithEntry(EntryId id) const = 0;
  // std::nullopt returns the entry to the top level of the Pinned section.
  virtual void MoveEntryToFolder(EntryId id,
                                 std::optional<FolderId> folder_id) = 0;
  // Re-parents a folder, or does nothing when the move is one the model will
  // not make: into itself, into its own descendant, or deeper than
  // kMaxFolderDepth once the moved folder's own subtree is counted. A no-op
  // rather than a crash for the same reason every other folder command is
  // one -- a drag decided what to do from a snapshot of a tree that another
  // window can have changed since.
  virtual void SetFolderParent(FolderId id,
                               std::optional<FolderId> parent_id) = 0;
  // The same question asked before the move, so a drop target can refuse
  // rather than accept a gesture and silently discard it -- the mistake
  // can_accept_entry exists to prevent for entries.
  virtual bool CanMoveFolderTo(FolderId id,
                               std::optional<FolderId> parent_id) const = 0;
  virtual void SetFolderName(FolderId id, const std::u16string& name) = 0;
  // Removes the folder. Its entries return to the top level; a folder groups
  // entries, it does not own them.
  virtual void DeleteFolder(FolderId id) = 0;

  // Space commands. The bar is drawn from spaces() the same way the list is
  // drawn from rows(): nothing about a dot -- its counts included -- is
  // derived by the view.
  //
  // Every space in position order, prepared here rather than walked by the
  // bar itself: see SidebarSpace for why a dot's counts cannot be a scan over
  // the strip.
  virtual std::vector<SidebarSpace> spaces() const = 0;
  // Shows `id` in this window, which is what clicking its dot asks for.
  // Named apart from SpaceSwitcher::SwitchTo, which carries it out, so a call
  // site says which of the two it is calling.
  virtual void SwitchToSpace(SpaceId id) = 0;
  // Makes a new space and switches to it -- a space is somewhere you are put,
  // not just a dot that appears while you stay where you were.
  virtual void AddSpace(const std::u16string& name) = 0;
  // What the dot's rename field commits. The name belongs to the space, not
  // to the dot, so it goes to the model and every window showing it follows.
  virtual void RenameSpace(SpaceId id, const std::u16string& name) = 0;
  // One emoji for the dot, or empty to go back to the name's first letter --
  // the same empty-means-letter rule SidebarSpace::icon is drawn by.
  virtual void SetSpaceIcon(SpaceId id, const std::u16string& icon) = 0;
  // An index into the fixed palette, not a colour: the space stores the
  // choice and the sidebar owns what each choice looks like.
  virtual void SetSpaceGradient(SpaceId id, int gradient) = 0;
  // Reorders the space bar itself -- the same shape MoveTab gives the tab
  // list.
  virtual void MoveSpace(SpaceId id, int position) = 0;
  // Destroys the space. What happens to its open tabs and entries is
  // SpaceSwitcher's rule, not this interface's.
  virtual void DeleteSpace(SpaceId id) = 0;
  // Moves a tab to another space: the row menu's "Move to space".
  virtual void MoveTabToSpace(int tab_index, SpaceId space_id) = 0;
  // Moves a favourite or pinned entry to another space, from the same menu.
  // Moving the one you are looking at takes you with it, as Zen does.
  virtual void MoveEntryToSpace(EntryId id, SpaceId space_id) = 0;

  // Routing rules, from a row's menu: whether pages on `url`'s site always
  // open in the space on screen, and making or removing that rule. A URL
  // that is not a web page has no site, answers false and changes nothing.
  virtual bool SiteOpensInActiveSpace(const GURL& url) const = 0;
  virtual void SetSiteOpensInActiveSpace(const GURL& url, bool opens_here) = 0;

  // Profile commands. A profile belongs to the whole browser, so every one
  // of these reaches every window, not only this one.
  //
  // Every profile, Default first.
  virtual std::vector<SidebarProfile> profiles() const = 0;
  // "New profile…": makes a profile and puts `space` on it.
  virtual void CreateProfileForSpace(SpaceId space,
                                     const std::u16string& name,
                                     int color) = 0;
  // Puts `space` on another profile. Its open tabs reopen there, logged in
  // as that profile; that is the model's rule, not the menu's.
  virtual void SetSpaceProfile(SpaceId space, ProfileId profile) = 0;
  virtual void RenameProfile(ProfileId id, const std::u16string& name) = 0;
  // An index into the palette, not a colour, as for a space's gradient.
  virtual void SetProfileColor(ProfileId id, int color) = 0;
  // Logs every site in the profile out: its cookies, site data and cache go.
  // Open tabs are not reloaded, as Chrome's own clear does not reload them.
  virtual void ClearProfileData(ProfileId id) = 0;
  // Erases the profile. Its spaces move to Default and their tabs reopen
  // there. Default itself is refused.
  virtual void DeleteProfile(ProfileId id) = 0;

  // How long a Today tab in the active space may sit idle before it is
  // archived. On the model rather than on the service because it is a
  // persistent property of the space, and the space bar's menu is the only
  // place it is chosen.
  virtual void SetArchiveTimeout(ArchiveTimeout timeout) = 0;
  virtual ArchiveTimeout archive_timeout() const = 0;

  // The archive, which unlike everything above is not in memory: it is a
  // SQLite file owned by a background sequence. So there is no
  // `archived_rows()` to match rows() — a synchronous getter over it could
  // only be a blocking read on the UI thread or a lie about freshness, and
  // Stage 6's library inherits whatever shape is set here.
  // `archive_readable` is false when the archive could not be read at all —
  // a file that would not open. It travels beside the rows because an empty
  // vector on its own is ambiguous, and the two states have to read
  // differently: "nothing archived yet" told to a user whose archive is
  // broken says their tabs were never archived, which is the opposite of the
  // truth and exactly the reassurance this feature exists to give.
  using ArchivedRowsCallback =
      base::OnceCallback<void(std::vector<ArchivedRow> rows,
                              bool archive_readable)>;

  // Whether this window has an archive at all. False off the record, where
  // there is no archive file and cannot be one — an off-the-record context's
  // GetPath() is the parent profile's, so an incognito archive would write
  // incognito browsing into the regular profile's file. See
  // ArciumProfileState::archive(). The affordance is then absent rather than
  // disabled: a control that can never be used reads as a bug.
  //
  // Deliberately *not* "the archive can be read": the file is opened on a
  // background sequence and this is asked while the sidebar is being built,
  // possibly before the open has run. A profile whose archive will not open
  // still answers true here and says so in the list instead — a button that
  // vanishes a few seconds after launch is worse than one that explains
  // itself.
  virtual bool has_archive() const = 0;

  // Asks for the `limit` most recently archived rows of the active space,
  // newest first. Always answers on a later turn of the run loop, never
  // inline, so a caller has one order of events to handle rather than two.
  //
  // `callback` may be dropped without ever running: the window can close
  // while the read is in flight. Bind it through a WeakPtr and put nothing in
  // it that has to happen.
  virtual void RequestArchivedRows(int limit,
                                   ArchivedRowsCallback callback) = 0;

  // Opens `url` in a new foreground tab and drops its archive row. An
  // archived tab was a Today tab and comes back as one, claimed by no entry.
  // Naming a row the archive no longer has still opens the tab and deletes
  // nothing, which is what a stale list clicked twice should do.
  virtual void ReopenArchived(const GURL& url, base::Time archived_at) = 0;

  virtual void AddObserver(Observer* observer) = 0;
  virtual void RemoveObserver(Observer* observer) = 0;
};

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_SIDEBAR_MODEL_H_
