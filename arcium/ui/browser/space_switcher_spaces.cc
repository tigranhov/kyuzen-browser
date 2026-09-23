// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The half of SpaceSwitcher that edits spaces rather than showing them:
// moving tabs and entries from one space to another, and deleting a space.
// Split from space_switcher.cc for size alone; the class, its invariants and
// its registry are described there and in the header.

#include <optional>
#include <vector>

#include "arcium/browser/model/arcium_profile.h"
#include "arcium/browser/model/tab_entry.h"
#include "arcium/browser/tab_binding.h"
#include "arcium/browser/tab_space.h"
#include "arcium/ui/browser/archive_service.h"
#include "arcium/ui/browser/profile_reopen.h"
#include "arcium/ui/browser/space_switcher.h"

#include "arcium/ui/browser/split_controller.h"
#include "arcium/ui/browser/tab_close_types.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/tabs/public/tab_interface.h"

namespace arcium {

void SpaceSwitcher::SetSplitController(SplitController* split) {
  split_ = split;
}

void SpaceSwitcher::MoveTabToSpace(int index, SpaceId space) {
  if (!tab_strip_model_ || index < 0 || index >= tab_strip_model_->count() ||
      !model_->GetSpace(space)) {
    return;
  }
  const ProfileId from = model_->ProfileOfSpace(SpaceOfTabAt(index));
  const ProfileId to = model_->ProfileOfSpace(space);
  // Before the tag and before any reopen. A split whose halves are in two
  // spaces must not exist even for one turn of the loop: the pane would hold
  // a tab that has moved out from under it, and between profiles the reopen
  // replaces the very tab the split is made of.
  if (split_) {
    split_->EndSplitFor(index);
  }
  // The tag goes on before the reopen, never after. The reopen starts a load,
  // and the guard that keeps a page in its space's storage reads the tag to
  // decide where the tab belongs: a tab still wearing the space it is leaving
  // looks misplaced the moment it arrives, so the guard cancels that load and
  // puts the page in a second tab -- one move, two tabs. Moving an entry a
  // few lines down has never had this fault for exactly this reason: it moves
  // the entry first, and the entry is what decides a tab's space before its
  // tag is consulted at all.
  SetSpaceTag(tab_strip_model_->GetTabAtIndex(index)->GetContents(), space);
  // A tab cannot change its storage, so a move between profiles is a
  // reopen: same address, same history, same place, other logins.
  if (from != to) {
    ReopenTabInProfile(tab_strip_model_, index, to);
    // The new contents inherits the tag, since carrying a tab's identity
    // never overwrites one that is already set. Setting it again here costs
    // nothing and keeps this function from resting on that.
    SetSpaceTag(tab_strip_model_->GetTabAtIndex(index)->GetContents(), space);
  }
  AskForSessionRebuild();
  // Moving the tab you are looking at takes you with it, as Zen does --
  // adopted, not switched to: SwitchTo would land on whatever `space`
  // already remembers as its last active tab, which can be a different tab
  // than the one that just moved, so the moved page would disappear behind
  // it.
  if (index == tab_strip_model_->active_index()) {
    AdoptSpace(space);
  }
}

void SpaceSwitcher::MoveEntryToSpace(EntryId id, SpaceId space) {
  const TabEntry* before = model_->GetEntry(id);
  const ProfileId from =
      before ? model_->ProfileOfSpace(before->space_id) : DefaultProfileId();
  // Read before the move: the model carries a linked partner along, and its
  // tab has to follow as this entry's does.
  const EntryId partner = before ? before->split_partner : EntryId();
  model_->MoveEntryToSpace(id, space);
  const TabEntry* entry = model_->GetEntry(id);
  if (!entry || entry->space_id != space || !tab_strip_model_) {
    // Either the move did not happen -- an unknown entry or space -- or
    // there is no strip to follow it through.
    return;
  }
  bool follow = MoveEntryTabToSpace(id, from, space);
  if (partner.is_valid()) {
    follow = MoveEntryTabToSpace(partner, from, space) || follow;
  }
  AskForSessionRebuild();
  if (follow) {
    // Moving the entry behind the tab you are looking at takes you with it,
    // the same as moving the tab itself does.
    AdoptSpace(space);
  }
}

bool SpaceSwitcher::MoveEntryTabToSpace(EntryId id,
                                        ProfileId from,
                                        SpaceId space) {
  std::optional<tabs::TabHandle> handle = binding_->TabForEntry(id);
  tabs::TabInterface* tab = handle ? handle->Get() : nullptr;
  if (!tab) {
    return false;
  }
  const int index = tab_strip_model_->GetIndexOfTab(tab);
  // The same rule as for a tab: the split ends before the tab goes.
  if (split_ && index != TabStripModel::kNoTab) {
    split_->EndSplitFor(index);
  }
  // Only this window's strip: an entry's tab living in another window is
  // re-tagged here and put right by the guard when it next navigates.
  if (from != model_->ProfileOfSpace(space) && index != TabStripModel::kNoTab) {
    ReopenTabInProfile(tab_strip_model_, index, model_->ProfileOfSpace(space));
    tab = tab_strip_model_->GetTabAtIndex(index);
  }
  // The entry's own tab is re-tagged too: an unpin drops the entry and falls
  // back to whatever the tab itself carries, and that has to agree with
  // where the entry just went.
  SetSpaceTag(tab->GetContents(), space);
  return index != TabStripModel::kNoTab &&
         index == tab_strip_model_->active_index();
}

void SpaceSwitcher::SetArchiveService(ArchiveService* archive_service) {
  archive_service_ = archive_service;
}

SpaceId SpaceSwitcher::NeighbourOf(SpaceId id) const {
  const std::vector<Space>& spaces = model_->spaces();
  for (size_t i = 0; i < spaces.size(); ++i) {
    if (spaces[i].id != id) {
      continue;
    }
    if (i + 1 < spaces.size()) {
      return spaces[i + 1].id;
    }
    if (i > 0) {
      return spaces[i - 1].id;
    }
    return SpaceId();
  }
  return SpaceId();
}

int SpaceSwitcher::OpenTabCount(SpaceId space) const {
  if (!tab_strip_model_) {
    return 0;
  }
  int count = 0;
  for (int index = 0; index < tab_strip_model_->count(); ++index) {
    if (SpaceOfTabAt(index) == space) {
      ++count;
    }
  }
  return count;
}

void SpaceSwitcher::DeleteSpace(SpaceId id) {
  // The last space cannot go: every tab has to be in one. The model refuses
  // it too; refusing here as well keeps the tabs from being closed first.
  if (model_->spaces().size() <= 1 || !model_->GetSpace(id) ||
      !tab_strip_model_) {
    return;
  }
  // Move off it before it goes, so the window is never showing a space the
  // model no longer has.
  if (id == active_space_) {
    SwitchTo(NeighbourOf(id));
  }
  const SpaceId landing = active_space_;

  // Chromium's own close, so a page with unsaved work still gets its
  // beforeunload prompt. The handles asked to close are remembered by
  // identity, not by tag: a tab bound to one of this space's entries is
  // chosen here through SpaceOfTabAt, which reads the entry's space first,
  // and a pinned tab's own tag need not name that space -- the entry decides
  // where the tab is drawn, and the tag is only what an unpin falls back on --
  // the re-tag loop below has to catch exactly the tabs this loop tried to
  // close, not whichever ones still wear the deleted space's raw tag.
  std::vector<tabs::TabHandle> asked_to_close;
  for (int index = tab_strip_model_->count() - 1; index >= 0; --index) {
    if (SpaceOfTabAt(index) == id) {
      asked_to_close.push_back(
          tab_strip_model_->GetTabAtIndex(index)->GetHandle());
      tab_strip_model_->CloseWebContentsAt(index, kUserCloseTypes);
    }
  }
  model_->RemoveSpace(id);
  // A tab still here refused to close -- a beforeunload dialog the user has
  // not answered -- and Chromium offers no signal for that at this seam.
  // Rather than leaving it tagged with a space that is gone, or with a tag
  // that was already stale, it joins the space the window moved to, where
  // the user can see it.
  bool retagged = false;
  for (const tabs::TabHandle& handle : asked_to_close) {
    tabs::TabInterface* tab = handle.Get();
    if (tab) {
      SetSpaceTag(tab->GetContents(), landing);
      retagged = true;
    }
  }
  if (retagged) {
    AskForSessionRebuild();
  }
  if (archive_service_) {
    archive_service_->RemoveSpaceRows(id);
  }
  NotifyActiveSpaceChanged();
}

}  // namespace arcium
