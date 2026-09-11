// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_BROWSER_SPACE_SWITCHER_H_
#define ARCIUM_UI_BROWSER_SPACE_SWITCHER_H_

#include <vector>

#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/entry_id.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"

class TabStripModel;

namespace arcium {

class ArchiveService;
class TabBinding;

// One per window. Holds which space the window is showing, answers whether a
// tab belongs to it, and performs switches.
//
// Deliberately not a member of BrowserSidebarController's model: the two
// patches of this stage are handed a Browser or a TabStripModel and nothing
// else, and a per-strip registry is reachable from both without dragging
// BrowserView into a unit test.
class SpaceSwitcher : public TabStripModelObserver,
                      public ArciumModel::Observer {
 public:
  class Observer : public base::CheckedObserver {
   public:
    virtual void OnActiveSpaceChanged() = 0;
  };

  SpaceSwitcher(TabStripModel* tab_strip_model,
                ArciumModel* model,
                TabBinding* binding);
  SpaceSwitcher(const SpaceSwitcher&) = delete;
  SpaceSwitcher& operator=(const SpaceSwitcher&) = delete;
  ~SpaceSwitcher() override;

  // The switcher of the window `tab_strip_model` belongs to, or null in a
  // window built without a sidebar (--arcium-no-sidebar, and every browser
  // test that does not want one).
  static SpaceSwitcher* FromTabStripModel(const TabStripModel* tab_strip_model);

  SpaceId active_space() const { return active_space_; }
  // Records the current space's active tab, moves to `id`, and lands on that
  // space's last active tab — or its first open tab, or a new blank one.
  void SwitchTo(SpaceId id);
  SpaceId SpaceOfTabAt(int index) const;
  bool IsInActiveSpace(int index) const;
  // Whether a pinned or favourite entry claims the tab at `index`. Such a
  // tab's place is its entry's, not the strip's, so strip-wide commands that
  // move or sweep Today tabs leave it alone.
  bool IsClaimedByEntryAt(int index) const;
  // Open tabs of `space`, in the order the sidebar draws them: favourites,
  // then pinned entries by position, then the tabs no entry claims in strip
  // order. Cold entries have no tab and are not here.
  std::vector<int> OpenTabsInSidebarOrder(SpaceId space) const;
  // A blank foreground tab in the active space. The strip index it landed at.
  // Runs the blank-tab callback, if one is set, once the tab is on screen.
  int OpenBlankTab();
  // Every close Arcium makes itself asks this first. When `closing` -- strip
  // indices about to close -- holds the active tab and every other open tab
  // of the active space, opens the space's blank tab, as OpenBlankTab does,
  // and answers true; otherwise does nothing and answers false. The blank tab
  // is appended, so each index in `closing` still names the same tab after.
  // Never from a strip callback: it inserts.
  bool OpenBlankTabBeforeClosing(const std::vector<int>& closing);
  // What to offer over a blank tab once OpenBlankTab has put it on screen --
  // a switch to an empty space, or a close of a space's last tab, both land
  // there. A switch runs it only after its observers have heard of the
  // switch. May be left unset, and then a blank tab is simply blank.
  void SetBlankTabCallback(base::RepeatingClosure callback);
  void MoveTabToSpace(int index, SpaceId space);
  // Moves the persistent entry to `space` and re-tags its own tab, if it has
  // one, so the tab's tag agrees with its entry after a later unpin falls
  // back on it. When that tab is the one on screen, follows it there --
  // adopting the target rather than switching, which would land on whatever
  // `space` already remembers as its own place, not on the tab that moved.
  void MoveEntryToSpace(EntryId id, SpaceId space);

  // Optional, and may be reset to null: the playground and fixtures that
  // build no ArchiveService never call this, and DeleteSpace simply skips
  // the archive cleanup then.
  void SetArchiveService(ArchiveService* archive_service);

  // Refuses the last space and an unknown id, same as the model does --
  // refusing here too keeps the tabs from being closed first. Moves the
  // window off `id` before it goes, closes `id`'s own tabs through the
  // strip's normal close (so a page with unsaved work still gets its
  // beforeunload prompt), removes the space from the model, and re-tags any
  // tab a declined close left behind into the space the window landed on --
  // it cannot stay tagged with a space that no longer exists.
  void DeleteSpace(SpaceId id);

  // How many strip tabs are currently in `space`. What a delete confirmation
  // counts.
  int OpenTabCount(SpaceId space) const;

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // Replaces how this switcher asks for a session rebuild -- by default the
  // profile's coalesced nudge, see AskForSessionRebuild -- so a test can count
  // the requests instead of reaching a SessionService.
  void SetSessionRebuildRequestForTesting(base::RepeatingClosure request) {
    session_rebuild_request_ = std::move(request);
  }

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void OnTabStripModelDestroyed(TabStripModel* tab_strip_model) override;

  // ArciumModel::Observer:
  void OnArciumModelChanged() override;

 private:
  void TagInsertedTabs(const TabStripModelChange::Insert& insert);
  // OpenBlankTab without the blank-tab callback. SwitchTo inserts through
  // this and runs the callback itself, once it has notified its observers.
  int InsertBlankTab();
  void RecordActiveTab();
  // A tab's space tag and key reach the session file only on a command
  // rebuild, so every change to either on a live tab asks for one.
  void AskForSessionRebuild();
  // Puts the window in `id` without touching the strip: sets the active
  // space, records whatever tab is already on screen as `id`'s place, and
  // notifies. Shared by the foreign-activation branch of
  // OnTabStripModelChanged and by MoveTabToSpace -- the two places that put
  // the window in a space it did not SwitchTo.
  void AdoptSpace(SpaceId id);
  // The space after `id` in position order, or the one before it when `id`
  // is last. Where DeleteSpace lands the window: never the space it is
  // about to remove.
  SpaceId NeighbourOf(SpaceId id) const;
  void NotifyActiveSpaceChanged();
  // Posted from OnArciumModelChanged rather than run inline: see the comment
  // there. Re-checks that the active space is still gone before switching,
  // because the model can change again before this task runs.
  void ApplyFallbackSwitch();

  raw_ptr<TabStripModel> tab_strip_model_;
  raw_ptr<ArciumModel> model_;
  raw_ptr<TabBinding> binding_;
  // Null in the playground, in a window built without a sidebar, and in
  // every fixture that does not set one. See SetArchiveService.
  raw_ptr<ArchiveService> archive_service_ = nullptr;
  SpaceId active_space_;
  base::RepeatingClosure blank_tab_callback_;
  // Null off the record, where there is no session file to keep in step.
  base::RepeatingClosure session_rebuild_request_;
  base::ObserverList<Observer> observers_;
  // Last: anything posted through this must run after every other member is
  // already constructed.
  base::WeakPtrFactory<SpaceSwitcher> weak_factory_{this};
};

}  // namespace arcium

#endif  // ARCIUM_UI_BROWSER_SPACE_SWITCHER_H_
