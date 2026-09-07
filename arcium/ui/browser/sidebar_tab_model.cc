// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/sidebar_tab_model.h"

#include <utility>

#include "arcium/browser/model/tab_entry.h"
#include "arcium/ui/sidebar/sidebar_colors.h"
#include "arcium/ui/sidebar/sidebar_metrics.h"
#include "base/auto_reset.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/tab_list/tab_removed_reason.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_data.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model_delegate.h"
#include "components/tabs/public/tab_alert.h"
#include "components/tabs/public/tab_interface.h"
#include "components/tabs/public/tab_network_state.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/base_window.h"
#include "ui/base/page_transition_types.h"

namespace arcium {

namespace {

constexpr uint32_t kCloseTypes = TabCloseTypes::CLOSE_USER_GESTURE |
                                 TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB;

// A cold entry has no tab and so no favicon; the globe stands in until the
// entry is warmed. Favicons for cold entries are Stage 6 work.
ui::ImageModel ColdFavicon() {
  return ui::ImageModel::FromVectorIcon(
      vector_icons::kGlobeIcon, kColorArciumRowText, metrics::kFaviconSize);
}

SidebarSection SectionForKind(EntryKind kind) {
  return kind == EntryKind::kFavorite ? SidebarSection::kFavorites
                                      : SidebarSection::kPinned;
}

}  // namespace

SidebarTabModel::SidebarTabModel(TabStripModel* tab_strip_model,
                                 ArciumModel* arcium_model,
                                 TabBinding* binding)
    : tab_strip_model_(tab_strip_model),
      arcium_model_(arcium_model),
      binding_(binding) {
  tab_strip_model_->AddObserver(this);
  arcium_model_->AddObserver(this);
}

SidebarTabModel::~SidebarTabModel() {
  arcium_model_->RemoveObserver(this);
  if (tab_strip_model_) {
    tab_strip_model_->RemoveObserver(this);
  }
}

std::vector<SidebarRow> SidebarTabModel::rows() const {
  std::vector<SidebarRow> rows;
  if (!tab_strip_model_) {
    return rows;
  }
  const SpaceId space = arcium_model_->default_space_id();
  for (EntryKind kind : {EntryKind::kFavorite, EntryKind::kPinned}) {
    for (const TabEntry* entry : arcium_model_->EntriesForKind(space, kind)) {
      rows.push_back(RowForEntry(*entry));
    }
  }
  // Whatever no entry claims is Today, in strip order.
  const int count = tab_strip_model_->count();
  for (int i = 0; i < count; ++i) {
    tabs::TabInterface* tab = tab_strip_model_->GetTabAtIndex(i);
    if (binding_->IsBound(tab->GetHandle())) {
      continue;
    }
    rows.push_back(RowForTab(i, tab));
  }
  return rows;
}

tabs::TabInterface* SidebarTabModel::BoundTabAnywhere(EntryId id) const {
  const std::optional<tabs::TabHandle> handle = binding_->TabForEntry(id);
  // The handle is weak, so a closed tab reads as null and the entry is cold.
  return handle.has_value() ? handle->Get() : nullptr;
}

tabs::TabInterface* SidebarTabModel::LiveTabForEntry(EntryId id) const {
  if (!tab_strip_model_) {
    return nullptr;
  }
  // A tab living in another window's strip is not this window's to draw, so
  // it reads cold here. Activating it is still that window's job, not a
  // reason to open a second tab: see ActivateEntry().
  tabs::TabInterface* tab = BoundTabAnywhere(id);
  if (!tab || tab_strip_model_->GetIndexOfTab(tab) == TabStripModel::kNoTab) {
    return nullptr;
  }
  return tab;
}

void SidebarTabModel::ActivateTabInItsOwnWindow(tabs::TabInterface* tab) {
  const int index = tab_strip_model_->GetIndexOfTab(tab);
  if (index != TabStripModel::kNoTab) {
    tab_strip_model_->ActivateTabAt(index);
    return;
  }
  BrowserWindowInterface* window = tab->GetBrowserWindowInterface();
  if (!window) {
    return;  // Detached mid-drag; leave the binding alone rather than steal.
  }
  TabStripModel* strip = window->GetTabStripModel();
  const int other_index =
      strip ? strip->GetIndexOfTab(tab) : TabStripModel::kNoTab;
  if (other_index == TabStripModel::kNoTab) {
    return;
  }
  strip->ActivateTabAt(other_index);
  if (ui::BaseWindow* base_window = window->GetWindow()) {
    base_window->Activate();
  }
}

SidebarRow SidebarTabModel::RowForEntry(const TabEntry& entry) const {
  SidebarRow row;
  row.entry_id = entry.id;
  row.section = SectionForKind(entry.kind);
  row.folder_id = entry.folder_id;

  tabs::TabInterface* tab = LiveTabForEntry(entry.id);
  if (!tab) {
    row.is_cold = true;
    row.tab_index = -1;
    row.title = entry.DisplayTitle();
    if (row.title.empty()) {
      row.title = base::UTF8ToUTF16(entry.url.host());
    }
    row.favicon = ColdFavicon();
    row.url = entry.url;
    return row;
  }

  const tabs::TabData data = tabs::TabData::FromTabInterface(tab);
  row.tab_index = tab_strip_model_->GetIndexOfTab(tab);
  // A rename wins over the live page title for ever; otherwise the tab is
  // the truth, exactly as Stage 1 had it.
  row.title = entry.custom_title.empty() ? data.title : entry.custom_title;
  row.favicon = data.favicon;
  row.is_active = row.tab_index == tab_strip_model_->active_index();
  row.is_loading = data.network_state != tabs::TabNetworkState::kNone &&
                   !data.should_hide_throbber;
  row.is_audible = data.alert_state == tabs::TabAlert::kAudioPlaying;
  row.is_muted = data.alert_state == tabs::TabAlert::kAudioMuting;
  row.url = data.visible_url;
  row.can_return_to_pinned_url =
      entry.kind == EntryKind::kPinned && data.visible_url != entry.url;
  return row;
}

SidebarRow SidebarTabModel::RowForTab(int index,
                                      tabs::TabInterface* tab) const {
  const tabs::TabData data = tabs::TabData::FromTabInterface(tab);
  SidebarRow row;
  row.tab_index = index;
  row.section = SidebarSection::kToday;
  row.title = data.title;
  row.favicon = data.favicon;
  row.is_active = index == tab_strip_model_->active_index();
  row.is_loading = data.network_state != tabs::TabNetworkState::kNone &&
                   !data.should_hide_throbber;
  row.is_audible = data.alert_state == tabs::TabAlert::kAudioPlaying;
  row.is_muted = data.alert_state == tabs::TabAlert::kAudioMuting;
  row.url = data.visible_url;
  return row;
}

void SidebarTabModel::ActivateTab(int tab_index) {
  if (tab_index >= 0 && tab_index < tab_strip_model_->count()) {
    tab_strip_model_->ActivateTabAt(tab_index);
  }
}

void SidebarTabModel::CloseTab(int tab_index) {
  if (tab_index >= 0 && tab_index < tab_strip_model_->count()) {
    tab_strip_model_->CloseWebContentsAt(tab_index, kCloseTypes);
  }
}

void SidebarTabModel::MoveTab(int from_index, int to_index) {
  const int count = tab_strip_model_->count();
  if (from_index < 0 || from_index >= count || to_index < 0 ||
      to_index >= count || from_index == to_index) {
    return;
  }
  tab_strip_model_->MoveWebContentsAt(from_index, to_index,
                                      /*select_after_move=*/false);
}

void SidebarTabModel::NewTab() {
  tab_strip_model_->delegate()->AddTabAt(GURL(), -1, /*foreground=*/true);
}

void SidebarTabModel::ClearToday() {
  // Close from the end so indices stay valid. A tab an entry claims is not a
  // Today tab, whatever Chromium thinks of its pinned state.
  for (int i = tab_strip_model_->count() - 1; i >= 0; --i) {
    if (!binding_->IsBound(tab_strip_model_->GetTabAtIndex(i)->GetHandle())) {
      tab_strip_model_->CloseWebContentsAt(i, kCloseTypes);
    }
  }
}

void SidebarTabModel::AddEntryForTab(int tab_index, EntryKind kind) {
  if (tab_index < 0 || tab_index >= tab_strip_model_->count()) {
    return;
  }
  tabs::TabInterface* tab = tab_strip_model_->GetTabAtIndex(tab_index);
  const tabs::TabData data = tabs::TabData::FromTabInterface(tab);
  // Deliberately not TabStripModel::SetTabPinned: a Chromium pinned tab is
  // always live, which is precisely what a cold entry must not be.
  const EntryId id =
      arcium_model_->AddEntry(kind, data.visible_url, data.title);
  binding_->Bind(id, tab->GetHandle());
  NotifyChanged();
}

void SidebarTabModel::AddToFavorites(int tab_index) {
  AddEntryForTab(tab_index, EntryKind::kFavorite);
}

void SidebarTabModel::PinTab(int tab_index) {
  AddEntryForTab(tab_index, EntryKind::kPinned);
}

void SidebarTabModel::UnpinEntry(EntryId id) {
  // Releasing the binding first is what returns the tab to Today: nothing
  // claims it any more.
  binding_->UnbindEntry(id);
  arcium_model_->RemoveEntry(id);
  NotifyChanged();
}

void SidebarTabModel::ActivateEntry(EntryId id) {
  const TabEntry* entry = arcium_model_->GetEntry(id);
  if (!entry || !tab_strip_model_) {
    return;
  }
  // Consult the *global* binding, not just this window's strip. An entry
  // whose tab lives in another window is that window's to show: opening a
  // second tab here would rebind the entry, silently blank the other
  // window's row and orphan its tab in neither Pinned nor Today. Only a dead
  // handle falls through to open-and-bind.
  if (tabs::TabInterface* tab = BoundTabAnywhere(id)) {
    ActivateTabInItsOwnWindow(tab);
    return;
  }
  if (!entry->url.is_valid()) {
    return;
  }
  // AddTabAt notifies synchronously, and OnTabStripModelChanged binds the
  // tab it reports while `pending_bind_` is set. Clearing it straight after
  // the call means a failed insert cannot capture some later tab.
  pending_bind_ = id;
  tab_strip_model_->delegate()->AddTabAt(entry->url, -1, /*foreground=*/true);
  pending_bind_ = EntryId();
}

void SidebarTabModel::CloseEntryTab(EntryId id) {
  tabs::TabInterface* tab = LiveTabForEntry(id);
  if (!tab) {
    return;
  }
  // The entry stays; only the tab goes, and the row turns cold.
  tab_strip_model_->CloseWebContentsAt(tab_strip_model_->GetIndexOfTab(tab),
                                       kCloseTypes);
}

void SidebarTabModel::SetEntryTitle(EntryId id, const std::u16string& title) {
  arcium_model_->SetCustomTitle(id, title);
}

void SidebarTabModel::ReturnToPinnedUrl(EntryId id) {
  const TabEntry* entry = arcium_model_->GetEntry(id);
  tabs::TabInterface* tab = LiveTabForEntry(id);
  if (!entry || !tab || !entry->url.is_valid()) {
    return;
  }
  content::WebContents* contents = tab->GetContents();
  if (!contents) {
    return;
  }
  // Navigate the tab already bound to the entry; opening a new one would
  // leave the entry pointing at the wrong tab.
  contents->GetController().LoadURL(entry->url, content::Referrer(),
                                    ui::PAGE_TRANSITION_AUTO_BOOKMARK,
                                    std::string());
}

void SidebarTabModel::AddObserver(SidebarModel::Observer* observer) {
  observers_.AddObserver(observer);
}

void SidebarTabModel::RemoveObserver(SidebarModel::Observer* observer) {
  observers_.RemoveObserver(observer);
}

void SidebarTabModel::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  if (change.type() == TabStripModelChange::kInserted &&
      pending_bind_.is_valid()) {
    const TabStripModelChange::Insert* insert = change.GetInsert();
    if (insert && !insert->contents.empty() && insert->contents.front().tab) {
      binding_->Bind(pending_bind_, insert->contents.front().tab->GetHandle());
      pending_bind_ = EntryId();
    }
  } else if (change.type() == TabStripModelChange::kRemoved) {
    const TabStripModelChange::Remove* remove = change.GetRemove();
    if (remove) {
      for (const TabStripModelChange::RemovedTab& removed : remove->contents) {
        // A tab moving to another window keeps its entry; a tab going away
        // releases it, which is what leaves the entry cold rather than
        // deleting it.
        if (removed.tab && removed.remove_reason !=
                               TabRemovedReason::kInsertedIntoOtherTabStrip) {
          binding_->UnbindTab(removed.tab->GetHandle());
        }
      }
    }
  }
  NotifyChanged();
}

void SidebarTabModel::OnTabChangedAt(tabs::TabInterface* tab,
                                     int index,
                                     TabChangeType change_type) {
  NotifyChanged();
}

void SidebarTabModel::OnTabPinnedStateChanged(tabs::TabInterface* tab,
                                              int index) {
  NotifyChanged();
}

void SidebarTabModel::OnTabStripModelDestroyed(TabStripModel* tab_strip_model) {
  tab_strip_model_->RemoveObserver(this);
  tab_strip_model_ = nullptr;
}

void SidebarTabModel::OnArciumModelChanged() {
  if (suppress_model_notifications_) {
    return;
  }
  NotifyChanged();
}

void SidebarTabModel::SyncEntryTitles() {
  if (!tab_strip_model_) {
    return;
  }
  const SpaceId space = arcium_model_->default_space_id();
  std::vector<EntryId> ids;
  for (EntryKind kind : {EntryKind::kFavorite, EntryKind::kPinned}) {
    for (const TabEntry* entry : arcium_model_->EntriesForKind(space, kind)) {
      ids.push_back(entry->id);
    }
  }
  for (const EntryId& id : ids) {
    tabs::TabInterface* tab = LiveTabForEntry(id);
    if (!tab) {
      continue;
    }
    const std::u16string title = tabs::TabData::FromTabInterface(tab).title;
    if (!title.empty()) {
      arcium_model_->SetLastTitle(id, title);
    }
  }
}

void SidebarTabModel::NotifyChanged() {
  if (notification_pending_) {
    return;
  }
  notification_pending_ = true;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&SidebarTabModel::FlushNotification,
                                weak_factory_.GetWeakPtr()));
}

void SidebarTabModel::FlushNotification() {
  notification_pending_ = false;
  {
    // The write-back is part of this burst, not a new one.
    base::AutoReset<bool> suppress(&suppress_model_notifications_, true);
    SyncEntryTitles();
  }
  for (SidebarModel::Observer& observer : observers_) {
    observer.OnSidebarModelChanged();
  }
}

}  // namespace arcium
