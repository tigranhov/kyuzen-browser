// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/sidebar_tab_model.h"

#include <algorithm>
#include <map>
#include <string_view>
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
#include "chrome/browser/ui/browser_tabstrip.h"
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
#include "url/url_constants.h"

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

// A path with its trailing slash dropped, so "/docs" and "/docs/" — and the
// degenerate "" and "/" — compare equal.
std::string_view PathWithoutTrailingSlash(const GURL& url) {
  std::string_view path = url.path();
  if (path.size() >= 1u && path.back() == '/') {
    path.remove_suffix(1);
  }
  return path;
}

// Whether `url` is effectively the pinned target `pinned`, rather than a
// navigation away from it. Exact GURL equality lights the "return to the
// pinned URL" affordance up after an http -> https upgrade or a
// trailing-slash redirect, offering to return to a URL the tab is already
// on. Host, port, query and path must agree; the ref never matters, and an
// http/https difference is an upgrade, not a destination.
bool IsSamePinnedTarget(const GURL& url, const GURL& pinned) {
  if (!url.is_valid() || !pinned.is_valid()) {
    return url == pinned;
  }
  if (url.host() != pinned.host() || url.port() != pinned.port() ||
      url.query() != pinned.query() ||
      PathWithoutTrailingSlash(url) != PathWithoutTrailingSlash(pinned)) {
    return false;
  }
  if (url.scheme() == pinned.scheme()) {
    return true;
  }
  auto is_web = [](std::string_view scheme) {
    return scheme == url::kHttpScheme || scheme == url::kHttpsScheme;
  };
  return is_web(url.scheme()) && is_web(pinned.scheme());
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
    if (IsClaimedByEntry(tab)) {
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

bool SidebarTabModel::IsClaimedByEntry(tabs::TabInterface* tab) const {
  const std::optional<EntryId> id = binding_->EntryForTab(tab->GetHandle());
  return id.has_value() && arcium_model_->GetEntry(*id) != nullptr;
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
      entry.kind == EntryKind::kPinned &&
      !IsSamePinnedTarget(data.visible_url, entry.url);
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
    if (!IsClaimedByEntry(tab_strip_model_->GetTabAtIndex(i))) {
      tab_strip_model_->CloseWebContentsAt(i, kCloseTypes);
    }
  }
}

EntryId SidebarTabModel::AddEntryForTab(int tab_index, EntryKind kind) {
  if (tab_index < 0 || tab_index >= tab_strip_model_->count()) {
    return EntryId();
  }
  tabs::TabInterface* tab = tab_strip_model_->GetTabAtIndex(tab_index);
  const tabs::TabData data = tabs::TabData::FromTabInterface(tab);
  // Deliberately not TabStripModel::SetTabPinned: a Chromium pinned tab is
  // always live, which is precisely what a cold entry must not be.
  const EntryId id =
      arcium_model_->AddEntry(kind, data.visible_url, data.title);
  binding_->Bind(id, tab->GetHandle());
  NotifyChanged();
  return id;
}

void SidebarTabModel::AddToFavorites(int tab_index) {
  AddEntryForTab(tab_index, EntryKind::kFavorite);
}

void SidebarTabModel::PinTab(int tab_index) {
  AddEntryForTab(tab_index, EntryKind::kPinned);
}

void SidebarTabModel::MoveTabToSection(int tab_index,
                                       SidebarSection section,
                                       int position) {
  if (section == SidebarSection::kToday || !tab_strip_model_) {
    return;
  }
  const EntryKind kind = section == SidebarSection::kFavorites
                             ? EntryKind::kFavorite
                             : EntryKind::kPinned;
  const EntryId id = AddEntryForTab(tab_index, kind);
  if (!id.is_valid()) {
    return;
  }
  // AddEntry appends, so the new entry is already lifted out of the order
  // `position` counted: `position` is the gap the insertion indicator was
  // drawn in, measured over the entries that were there before this one, and
  // that is exactly what ReorderEntry inserts at. A position past the end
  // clamps, which is the append AddToFavorites and PinTab mean.
  //
  // Two ArciumModel writes, one sidebar notification: NotifyChanged coalesces
  // the burst, so no observer sees the entry appended before it is placed.
  arcium_model_->ReorderEntry(id, position);
  NotifyChanged();
}

int SidebarTabModel::TodayStripIndexForPosition(int position) const {
  if (position < 0 || !tab_strip_model_) {
    return -1;
  }
  int seen = 0;
  const int count = tab_strip_model_->count();
  for (int i = 0; i < count; ++i) {
    if (IsClaimedByEntry(tab_strip_model_->GetTabAtIndex(i))) {
      continue;
    }
    if (seen == position) {
      return i;
    }
    ++seen;
  }
  return -1;
}

void SidebarTabModel::MoveTabBeforeStripIndex(int from, int before) {
  const int count = tab_strip_model_->count();
  if (from < 0 || from >= count || count == 0) {
    return;
  }
  int to = before < 0 ? count - 1 : before;
  // Lifting the tab out first shifts everything below it up one, so landing
  // before a tab that is already below it means one index less. The same
  // correction MoveTabToDropIndex applies in the view, and the one the entry
  // reorder paths need.
  if (before >= 0 && from < to) {
    --to;
  }
  to = std::clamp(to, 0, count - 1);
  if (to != from) {
    tab_strip_model_->MoveWebContentsAt(from, to, /*select_after_move=*/false);
  }
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
  BrowserWindowInterface* window =
      tab_strip_model_->delegate()->GetBrowserWindowInterface();
  if (!window) {
    return;
  }
  // The insert notifies synchronously, and OnTabStripModelChanged binds the
  // tab it reports while `pending_bind_` is set. Clearing it straight after
  // the call means a failed insert cannot capture some later tab.
  //
  // AUTO_BOOKMARK, matching ReturnToPinnedUrl(): both are the browser
  // opening a URL it stored on the user's behalf, not something typed.
  pending_bind_ = id;
  chrome::AddSelectedTabWithURL(window, entry->url,
                                ui::PAGE_TRANSITION_AUTO_BOOKMARK);
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

void SidebarTabModel::MoveEntryToSection(EntryId id,
                                         SidebarSection section,
                                         int position) {
  const TabEntry* entry = arcium_model_->GetEntry(id);
  if (!entry || !tab_strip_model_) {
    return;
  }
  if (section == SidebarSection::kToday) {
    // Today holds tabs, not entries, so this drops the entry. It must not
    // drop the page with it. A warm entry already has a tab that stays
    // behind once nothing claims it; a cold entry has none, so one is opened
    // first and the end state is the same either way — Today holding a live
    // tab for that URL. Nothing is lost, which is why this needs no undo.
    //
    // The URL is copied out before the mutation: RemoveEntry invalidates
    // every TabEntry pointer.
    const GURL url = entry->url;
    // Anywhere, not just this window: an entry whose tab sits in another
    // window still has a page behind it, and opening a second copy here
    // would duplicate it.
    const bool cold = BoundTabAnywhere(id) == nullptr;
    // Read before the entry goes, while `position` still counts the Today
    // rows it was dropped between — this entry is not one of them, and
    // removing it is what puts its tab among them.
    const int before = TodayStripIndexForPosition(position);
    int from = -1;
    if (cold) {
      if (url.is_valid()) {
        // Background: a drop rearranges the sidebar, it does not ask to read
        // the page. Deliberately unbound — the entry is about to go, and a
        // binding to a removed entry is what ATabWhoseEntryVanishes covers.
        tab_strip_model_->delegate()->AddTabAt(url, -1, /*foreground=*/false);
        // Appended, so it is the last tab; nothing above it moved.
        from = tab_strip_model_->count() - 1;
      }
    } else if (tabs::TabInterface* tab = LiveTabForEntry(id)) {
      from = tab_strip_model_->GetIndexOfTab(tab);
    }
    binding_->UnbindEntry(id);
    arcium_model_->RemoveEntry(id);
    // Today's order is the tab strip's, so putting the row where the
    // insertion line was drawn is a strip move. A tab in another window's
    // strip has `from` -1 and stays where it is: that window owns it.
    MoveTabBeforeStripIndex(from, before);
    NotifyChanged();
    return;
  }
  const EntryKind kind = section == SidebarSection::kFavorites
                             ? EntryKind::kFavorite
                             : EntryKind::kPinned;
  // Two ArciumModel writes, one sidebar notification: NotifyChanged coalesces
  // the burst into a single posted flush, so no SidebarModel observer can see
  // the entry in its new section still holding its old position.
  arcium_model_->SetEntryKind(id, kind);
  arcium_model_->ReorderEntry(id, position);
  NotifyChanged();
}

std::vector<SidebarFolder> SidebarTabModel::folders() const {
  const SpaceId space = arcium_model_->default_space_id();
  // One pass over the entries counts every folder, so a header never scans
  // and rows() is not walked once per folder.
  std::map<FolderId, int> counts;
  for (const TabEntry& entry : arcium_model_->entries()) {
    if (entry.space_id == space && entry.folder_id.has_value()) {
      ++counts[*entry.folder_id];
    }
  }
  std::vector<SidebarFolder> result;
  for (const Folder& folder : arcium_model_->folders()) {
    if (folder.space_id != space) {
      continue;
    }
    SidebarFolder out;
    out.id = folder.id;
    out.name = folder.name;
    out.collapsed = folder.collapsed;
    auto it = counts.find(folder.id);
    out.entry_count = it == counts.end() ? 0 : it->second;
    result.push_back(std::move(out));
  }
  // ArciumModel stores folders in insertion order and keeps `position`
  // normalised; the sidebar wants that order, not the vector's.
  std::sort(result.begin(), result.end(),
            [this](const SidebarFolder& a, const SidebarFolder& b) {
              const Folder* fa = arcium_model_->GetFolder(a.id);
              const Folder* fb = arcium_model_->GetFolder(b.id);
              return fa->position < fb->position;
            });
  return result;
}

const TabEntry* SidebarTabModel::FolderableEntry(EntryId id) const {
  const TabEntry* entry = arcium_model_->GetEntry(id);
  // A favourite is drawn as a tile in the grid and has nowhere to be indented
  // to, so ArciumModel keeps folder_id empty for one; refuse here rather than
  // write a field the model would clear behind our back.
  return entry && entry->kind == EntryKind::kPinned ? entry : nullptr;
}

void SidebarTabModel::SetFolderCollapsed(FolderId id, bool collapsed) {
  arcium_model_->SetFolderCollapsed(id, collapsed);
}

FolderId SidebarTabModel::CreateFolderWithEntry(EntryId id,
                                                const std::u16string& name) {
  if (!FolderableEntry(id)) {
    return FolderId();
  }
  const FolderId folder = arcium_model_->AddFolder(name);
  arcium_model_->SetEntryFolder(id, folder);
  return folder;
}

void SidebarTabModel::MoveEntryToFolder(EntryId id,
                                        std::optional<FolderId> folder_id) {
  // The menu that issued this was built from a snapshot; the folder may be
  // gone by the time the item is chosen.
  if (!FolderableEntry(id) ||
      (folder_id.has_value() && !arcium_model_->GetFolder(*folder_id))) {
    return;
  }
  arcium_model_->SetEntryFolder(id, folder_id);
}

void SidebarTabModel::SetFolderName(FolderId id, const std::u16string& name) {
  arcium_model_->SetFolderName(id, name);
}

void SidebarTabModel::DeleteFolder(FolderId id) {
  arcium_model_->RemoveFolder(id);
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
  // Opening one tab reaches here twice: once for the insert, and again about
  // 200ms later when Browser::ProcessPendingUIUpdates flushes the deferred
  // title as TabChangeType::kAll. A test that counts notifications will see
  // one or two depending on how fast the tests before it ran, so it must call
  // MakeBrowserUiUpdatesImmediate() (arcium/test/sidebar_tab_model_unittest.cc)
  // to collapse Browser's 200ms coalescing window first.
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
  // One pass over the strip builds the entry-to-tab lookup, then one pass
  // over the entries reads it. Asking TabBinding per entry per tab made this
  // O(entries x tabs) on every flush, and a flush happens on every burst.
  std::map<EntryId, tabs::TabInterface*> tab_for_entry;
  const int count = tab_strip_model_->count();
  for (int i = 0; i < count; ++i) {
    tabs::TabInterface* tab = tab_strip_model_->GetTabAtIndex(i);
    const std::optional<EntryId> id = binding_->EntryForTab(tab->GetHandle());
    if (id.has_value()) {
      tab_for_entry[*id] = tab;
    }
  }
  if (tab_for_entry.empty()) {
    return;
  }
  // Collected first: SetLastTitle notifies, and an observer must not be able
  // to invalidate the entry pointers this loop is walking.
  std::vector<std::pair<EntryId, std::u16string>> updates;
  const SpaceId space = arcium_model_->default_space_id();
  for (EntryKind kind : {EntryKind::kFavorite, EntryKind::kPinned}) {
    for (const TabEntry* entry : arcium_model_->EntriesForKind(space, kind)) {
      auto it = tab_for_entry.find(entry->id);
      if (it == tab_for_entry.end()) {
        continue;
      }
      std::u16string title = tabs::TabData::FromTabInterface(it->second).title;
      if (!title.empty() && title != entry->last_title) {
        updates.emplace_back(entry->id, std::move(title));
      }
    }
  }
  for (auto& [id, title] : updates) {
    arcium_model_->SetLastTitle(id, title);
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
