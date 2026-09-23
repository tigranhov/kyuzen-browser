// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Pinned splits: when two pinned entries are one split, and what the
// sidebar's commands do to a split drawn as one row. The link itself is
// ArciumModel's, which keeps a linked pair together through every move; this
// is where the tab strip's splits are turned into links and back.

#include <vector>

#include "arcium/browser/model/tab_entry.h"
#include "arcium/ui/browser/sidebar_tab_model.h"
#include "arcium/ui/browser/split_controller.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/tabs/public/split_tab_data.h"
#include "components/tabs/public/tab_interface.h"

namespace arcium {

tabs::TabInterface* SidebarTabModel::SplitPartnerTab(
    tabs::TabInterface* tab) const {
  if (!tab || !tab_strip_model_) {
    return nullptr;
  }
  const std::optional<split_tabs::SplitTabId> split = tab->GetSplit();
  if (!split) {
    return nullptr;
  }
  const split_tabs::SplitTabData* data = tab_strip_model_->GetSplitData(*split);
  if (!data) {
    return nullptr;
  }
  for (tabs::TabInterface* other : data->ListTabs()) {
    if (other != tab) {
      return other;
    }
  }
  return nullptr;
}

EntryId SidebarTabModel::EntryClaiming(tabs::TabInterface* tab) const {
  if (!tab) {
    return EntryId();
  }
  const std::optional<EntryId> id = binding_->EntryForTab(tab->GetHandle());
  return id && arcium_model_->GetEntry(*id) ? *id : EntryId();
}

void SidebarTabModel::PinSplitPartner(EntryId id) {
  const TabEntry* entry = arcium_model_->GetEntry(id);
  if (!entry || entry->kind != EntryKind::kPinned ||
      entry->split_partner.is_valid()) {
    return;
  }
  tabs::TabInterface* tab = LiveTabForEntry(id);
  tabs::TabInterface* other = SplitPartnerTab(tab);
  if (!other) {
    return;
  }
  // Where the pair goes: the gap `id` is in, counted without the partner,
  // which is where a pin or a drop put it.
  int gap = 0;
  EntryId partner = EntryClaiming(other);
  for (const TabEntry* sibling :
       arcium_model_->EntriesForKind(entry->space_id, EntryKind::kPinned)) {
    if (sibling->id == id) {
      break;
    }
    if (sibling->id != partner) {
      ++gap;
    }
  }
  if (!partner.is_valid()) {
    partner = AddEntryForTab(tab_strip_model_->GetIndexOfTab(other),
                             EntryKind::kPinned);
  }
  const TabEntry* partner_entry = arcium_model_->GetEntry(partner);
  // A favourite stays a favourite: a tile never merges with a row.
  if (!partner_entry || partner_entry->kind != EntryKind::kPinned) {
    return;
  }
  // The left pane first, so the row reads the way the screen does.
  const bool other_left = tab_strip_model_->GetIndexOfTab(other) <
                          tab_strip_model_->GetIndexOfTab(tab);
  if (arcium_model_->LinkSplitEntries(other_left ? partner : id,
                                      other_left ? id : partner)) {
    arcium_model_->ReorderEntry(id, gap);
  }
  NotifyChanged();
}

void SidebarTabModel::FormLinkedSplit(EntryId id) {
  const TabEntry* entry = arcium_model_->GetEntry(id);
  if (!split_ || !entry || !entry->split_partner.is_valid()) {
    return;
  }
  tabs::TabInterface* tab = LiveTabForEntry(id);
  // Only a page this window is showing, and only one sharing the screen with
  // nothing: a split the reader made with some other page is theirs.
  if (!tab || tab != tab_strip_model_->GetActiveTab() ||
      tab->GetSplit().has_value()) {
    return;
  }
  const TabEntry* partner = arcium_model_->GetEntry(entry->split_partner);
  if (!partner) {
    return;
  }
  split_->SplitWithActive(partner->id,
                          /*on_right=*/partner->position > entry->position);
}

void SidebarTabModel::MoveSplitPartnerToSpace(tabs::TabHandle tab,
                                              tabs::TabHandle partner,
                                              EntryId partner_entry,
                                              SpaceId space) {
  if (partner_entry.is_valid()) {
    const TabEntry* entry = arcium_model_->GetEntry(partner_entry);
    if (!entry || entry->kind != EntryKind::kPinned) {
      return;
    }
    // A linked partner is already there, carried with the entry it is
    // linked to, tab and all.
    if (entry->space_id != space) {
      switcher_->MoveEntryToSpace(partner_entry, space);
    }
  } else if (tabs::TabInterface* other = partner.Get()) {
    switcher_->MoveTabToSpace(tab_strip_model_->GetIndexOfTab(other), space);
  } else {
    return;
  }
  // Together again, when the move took the window with it. A pair moved from
  // the menu of a space not on screen stays as it is, one row, and the next
  // click puts it back on screen together.
  tabs::TabInterface* moved = tab.Get();
  tabs::TabInterface* other =
      partner_entry.is_valid() ? LiveTabForEntry(partner_entry) : partner.Get();
  tabs::TabInterface* active = tab_strip_model_->GetActiveTab();
  if (!split_ || !moved || !other || (active != moved && active != other) ||
      active->GetSplit().has_value()) {
    return;
  }
  tabs::TabInterface* joining = active == moved ? other : moved;
  split_->SplitWithActive(tab_strip_model_->GetIndexOfTab(joining),
                          /*on_right=*/joining == other);
}

void SidebarTabModel::EndSplit(const SidebarRow& row) {
  if (row.entry_id.is_valid()) {
    arcium_model_->UnlinkSplitEntry(row.entry_id);
  }
  tabs::TabInterface* tab = row.entry_id.is_valid()
                                ? LiveTabForEntry(row.entry_id)
                            : tab_strip_model_->ContainsIndex(row.tab_index)
                                ? tab_strip_model_->GetTabAtIndex(row.tab_index)
                                : nullptr;
  if (split_ && tab) {
    split_->EndSplitFor(tab_strip_model_->GetIndexOfTab(tab));
  }
  NotifyChanged();
}

void SidebarTabModel::CloseSplit(const SidebarRow& row) {
  tabs::TabInterface* tab = row.entry_id.is_valid()
                                ? LiveTabForEntry(row.entry_id)
                            : tab_strip_model_->ContainsIndex(row.tab_index)
                                ? tab_strip_model_->GetTabAtIndex(row.tab_index)
                                : nullptr;
  tabs::TabInterface* other = SplitPartnerTab(tab);
  if (!other && row.split_partner.is_valid()) {
    other = LiveTabForEntry(row.split_partner);
  }
  // Handles, because each close moves every index after it.
  std::vector<tabs::TabHandle> halves;
  for (tabs::TabInterface* half : {tab, other}) {
    if (half) {
      halves.push_back(half->GetHandle());
    }
  }
  for (tabs::TabHandle handle : halves) {
    tabs::TabInterface* half = handle.Get();
    if (!half) {
      continue;
    }
    // A pinned half keeps its entry, and the pair keeps its link: closing
    // both is not ending the split.
    if (const EntryId entry = EntryClaiming(half); entry.is_valid()) {
      CloseEntryTab(entry);
    } else {
      CloseTab(tab_strip_model_->GetIndexOfTab(half));
    }
  }
}

void SidebarTabModel::UpdateLinksForSplitChange(const SplitTabChange& change) {
  // Posted, both ways. The change arrives from inside the strip's own
  // notification, and a link is a model write that reaches every observer of
  // the model -- the split controller among them, which acts on the strip.
  // And a split ending because one of its tabs is closing looks, at this
  // moment, exactly like the reader ending it: only once the close has run
  // is one of the two tabs gone.
  std::vector<std::pair<tabs::TabInterface*, int>> tabs;
  bool adding = false;
  if (change.type == SplitTabChange::Type::kAdded) {
    tabs = change.GetAddedChange()->tabs();
    adding = true;
  } else if (change.type == SplitTabChange::Type::kRemoved) {
    const SplitTabChange::RemovedChange* removed = change.GetRemovedChange();
    if (moving_pair_ ||
        removed->reason() ==
            SplitTabChange::SplitTabRemoveReason::kDetachedToAnotherTabstrip) {
      return;
    }
    tabs = removed->tabs();
  }
  if (tabs.size() != 2 || !tabs[0].first || !tabs[1].first) {
    return;
  }
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(adding ? &SidebarTabModel::LinkIfBothPinned
                            : &SidebarTabModel::UnlinkIfBothOpen,
                     weak_factory_.GetWeakPtr(), tabs[0].first->GetHandle(),
                     tabs[1].first->GetHandle()));
}

void SidebarTabModel::LinkIfBothPinned(tabs::TabHandle a, tabs::TabHandle b) {
  tabs::TabInterface* first = a.Get();
  tabs::TabInterface* second = b.Get();
  if (!first || !second || !first->GetSplit().has_value() ||
      first->GetSplit() != second->GetSplit()) {
    return;
  }
  const TabEntry* left = arcium_model_->GetEntry(EntryClaiming(first));
  const TabEntry* right = arcium_model_->GetEntry(EntryClaiming(second));
  if (!left || !right || left->kind != EntryKind::kPinned ||
      right->kind != EntryKind::kPinned || left->split_partner == right->id) {
    return;
  }
  if (tab_strip_model_->GetIndexOfTab(second) <
      tab_strip_model_->GetIndexOfTab(first)) {
    std::swap(left, right);
  }
  arcium_model_->LinkSplitEntries(left->id, right->id);
}

void SidebarTabModel::UnlinkIfBothOpen(tabs::TabHandle a, tabs::TabHandle b) {
  tabs::TabInterface* first = a.Get();
  tabs::TabInterface* second = b.Get();
  // A tab gone means the split ended because it closed, which leaves the
  // entry half cold rather than two entries. Two tabs back on screen
  // together means nothing ended at all.
  if (!first || !second ||
      (first->GetSplit().has_value() &&
       first->GetSplit() == second->GetSplit())) {
    return;
  }
  const TabEntry* entry = arcium_model_->GetEntry(EntryClaiming(first));
  if (!entry || !entry->split_partner.is_valid() ||
      entry->split_partner != EntryClaiming(second)) {
    return;
  }
  arcium_model_->UnlinkSplitEntry(entry->id);
}

}  // namespace arcium
