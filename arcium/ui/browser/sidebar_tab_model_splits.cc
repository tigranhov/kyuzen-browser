// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Pinned splits: when two pinned entries are one split, and what the
// sidebar's commands do to a split drawn as one row. The link itself is
// ArciumModel's, which keeps a linked pair together through every move; this
// is where the tab strip's splits are turned into links and back.

#include <algorithm>
#include <optional>
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

bool SidebarTabModel::CanSplitByDrop(const SidebarRow& target,
                                     EntryId dragged_entry,
                                     int dragged_tab) const {
  if (!split_ || !tab_strip_model_) {
    return false;
  }
  // A pair is taken apart before either half joins anything else, the same
  // rule the split controller keeps for a live split.
  if (target.split.has_value() || target.split_partner.is_valid() ||
      target.split_joins_previous || target.split_joins_next) {
    return false;
  }
  int dragged_index = dragged_tab;
  if (dragged_entry.is_valid()) {
    if (dragged_entry == target.entry_id) {
      return false;
    }
    const TabEntry* entry = arcium_model_->GetEntry(dragged_entry);
    if (!entry || entry->split_partner.is_valid()) {
      return false;
    }
    tabs::TabInterface* tab = LiveTabForEntry(dragged_entry);
    dragged_index = tab ? tab_strip_model_->GetIndexOfTab(tab) : -1;
  } else if (!tab_strip_model_->ContainsIndex(dragged_tab)) {
    return false;
  }
  const int target_index = target.is_cold ? -1 : target.tab_index;
  if (target_index >= 0 && dragged_index >= 0) {
    // Both open: every rule the other ways in apply, including the same
    // row, which is one index twice.
    return split_->CanSplit(target_index, dragged_index);
  }
  // At least one is cold. Both rows are drawn in the space on screen, and a
  // cold entry is neither loose nor sharing, so the only refusal left is an
  // open half that already shares the screen.
  for (int index : {target_index, dragged_index}) {
    if (tab_strip_model_->ContainsIndex(index) &&
        tab_strip_model_->GetSplitForTab(index)) {
      return false;
    }
  }
  return true;
}

bool SidebarTabModel::CanPutBesideActive(const RowDragData& payload) const {
  // A folder holds no page, and a whole split is already sharing.
  if (payload.is_folder() || payload.split_pair) {
    return false;
  }
  // A warm row's payload carries its tab's index and a cold one's carries
  // none, which is all CanSplitRow reads of a row.
  SidebarRow row;
  row.entry_id = payload.entry_id;
  row.tab_index = payload.tab_index;
  row.is_cold = payload.tab_index < 0;
  return CanSplitRow(row);
}

void SidebarTabModel::LeaveSplit(EntryId entry, int tab_index) {
  const bool linked = entry.is_valid() && arcium_model_->GetEntry(entry) &&
                      arcium_model_->GetEntry(entry)->split_partner.is_valid();
  tabs::TabInterface* tab = entry.is_valid() ? LiveTabForEntry(entry)
                            : tab_strip_model_->ContainsIndex(tab_index)
                                ? tab_strip_model_->GetTabAtIndex(tab_index)
                                : nullptr;
  const bool sharing = tab && tab->GetSplit().has_value();
  if (!linked && !sharing) {
    return;
  }
  // The row menu's End split, which is what leaving is when there are only
  // two halves.
  SidebarRow row;
  row.entry_id = entry;
  row.tab_index = tab ? tab_strip_model_->GetIndexOfTab(tab) : tab_index;
  EndSplit(row);
}

void SidebarTabModel::MoveSplit(int tab_index, int before_tab) {
  if (!tab_strip_model_->ContainsIndex(tab_index)) {
    return;
  }
  const std::optional<split_tabs::SplitTabId> split =
      tab_strip_model_->GetTabAtIndex(tab_index)->GetSplit();
  if (!split) {
    return;
  }
  const split_tabs::SplitTabData* const data =
      tab_strip_model_->GetSplitData(*split);
  if (!data) {
    return;
  }
  const std::vector<tabs::TabInterface*> halves = data->ListTabs();
  const int first = tab_strip_model_->GetIndexOfTab(halves.front());
  const int size = static_cast<int>(halves.size());
  const int count = tab_strip_model_->count();
  // Landing inside another split would pull it apart, and the strip treats
  // that as a broken invariant rather than a request, so such a place is no
  // place at all. Only the second half of a split has its first half before
  // it; landing before a first half is between two splits, which is fine.
  if (tab_strip_model_->ContainsIndex(before_tab) && before_tab > 0) {
    const std::optional<split_tabs::SplitTabId> there =
        tab_strip_model_->GetTabAtIndex(before_tab)->GetSplit();
    if (there && there != split &&
        tab_strip_model_->GetTabAtIndex(before_tab - 1)->GetSplit() == there) {
      return;
    }
  }
  // `before_tab` counts the strip with the pair still in it; the strip wants
  // the index the pair's first tab ends up at once it has been lifted out.
  int to = before_tab < 0       ? count - size
           : before_tab > first ? before_tab - size
                                : before_tab;
  to = std::clamp(to, 0, count - size);
  if (to == first) {
    return;
  }
  tab_strip_model_->MoveSplitTo(*split, to, /*pinned=*/false,
                                /*group_id=*/std::nullopt);
}

void SidebarTabModel::SplitByDrop(const SidebarRow& target,
                                  EntryId dragged_entry,
                                  int dragged_tab) {
  if (!CanSplitByDrop(target, dragged_entry, dragged_tab)) {
    return;
  }
  // A handle, because opening a cold target adds a tab and can move the
  // dragged one's index.
  const tabs::TabHandle dragged =
      dragged_entry.is_valid()
          ? tabs::TabHandle()
          : tab_strip_model_->GetTabAtIndex(dragged_tab)->GetHandle();
  tabs::TabInterface* shown = nullptr;
  if (target.entry_id.is_valid()) {
    ShowEntry(target.entry_id);
    shown = LiveTabForEntry(target.entry_id);
  } else {
    ActivateTab(target.tab_index);
    shown = tab_strip_model_->GetTabAtIndex(target.tab_index);
  }
  // The target's tab lives in another window, which was raised instead:
  // there is nothing on this screen to put the dragged page beside.
  if (!shown || shown != tab_strip_model_->GetActiveTab()) {
    return;
  }
  // The target is the page on screen now, so the dragged page joins it, on
  // the right: the half the drop showed it would take.
  if (dragged_entry.is_valid()) {
    split_->SplitWithActive(dragged_entry, /*on_right=*/true);
  } else if (tabs::TabInterface* tab = dragged.Get()) {
    split_->SplitWithActive(tab_strip_model_->GetIndexOfTab(tab),
                            /*on_right=*/true);
  }
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
