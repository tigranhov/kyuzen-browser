// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The half of SpaceSwitcher that edits spaces rather than showing them:
// moving tabs and entries from one space to another, and deleting a space.
// Split from space_switcher.cc for size alone; the class, its invariants and
// its registry are described there and in the header.

#include <optional>
#include <vector>

#include "arcium/browser/model/tab_entry.h"
#include "arcium/browser/tab_binding.h"
#include "arcium/browser/tab_space.h"
#include "arcium/ui/browser/archive_service.h"
#include "arcium/ui/browser/space_switcher.h"
#include "arcium/ui/browser/tab_close_types.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/tabs/public/tab_interface.h"

namespace arcium {

void SpaceSwitcher::MoveTabToSpace(int index, SpaceId space) {
  if (!tab_strip_model_ || index < 0 || index >= tab_strip_model_->count() ||
      !model_->GetSpace(space)) {
    return;
  }
  SetSpaceTag(tab_strip_model_->GetTabAtIndex(index)->GetContents(), space);
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
  model_->MoveEntryToSpace(id, space);
  const TabEntry* entry = model_->GetEntry(id);
  if (!entry || entry->space_id != space || !tab_strip_model_) {
    // Either the move did not happen -- an unknown entry or space -- or
    // there is no strip to follow it through.
    return;
  }
  std::optional<tabs::TabHandle> handle = binding_->TabForEntry(id);
  tabs::TabInterface* tab = handle ? handle->Get() : nullptr;
  if (!tab) {
    return;
  }
  // The entry's own tab is re-tagged too: an unpin drops the entry and falls
  // back to whatever the tab itself carries, and that has to agree with
  // where the entry just went.
  SetSpaceTag(tab->GetContents(), space);
  AskForSessionRebuild();
  const int index = tab_strip_model_->GetIndexOfTab(tab);
  if (index != TabStripModel::kNoTab &&
      index == tab_strip_model_->active_index()) {
    // Moving the entry behind the tab you are looking at takes you with it,
    // the same as moving the tab itself does.
    AdoptSpace(space);
  }
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
  // and a pinned tab can carry a stale tag of its own naming a different,
  // still-surviving space (moving a pin retags the entry, never the tab) --
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
