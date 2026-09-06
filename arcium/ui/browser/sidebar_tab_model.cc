// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/sidebar_tab_model.h"

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/ui/tabs/tab_data.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model_delegate.h"
#include "components/tabs/public/tab_alert.h"
#include "components/tabs/public/tab_interface.h"
#include "components/tabs/public/tab_network_state.h"
#include "content/public/browser/web_contents.h"

namespace arcium {

namespace {

constexpr uint32_t kCloseTypes =
    TabCloseTypes::CLOSE_USER_GESTURE | TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB;

}  // namespace

SidebarTabModel::SidebarTabModel(TabStripModel* tab_strip_model)
    : tab_strip_model_(tab_strip_model) {
  tab_strip_model_->AddObserver(this);
}

SidebarTabModel::~SidebarTabModel() {
  if (tab_strip_model_) {
    tab_strip_model_->RemoveObserver(this);
  }
}

std::vector<SidebarRow> SidebarTabModel::rows() const {
  std::vector<SidebarRow> rows;
  if (!tab_strip_model_) {
    return rows;
  }
  const int count = tab_strip_model_->count();
  rows.reserve(count);
  for (int i = 0; i < count; ++i) {
    tabs::TabInterface* tab = tab_strip_model_->GetTabAtIndex(i);
    const tabs::TabData data = tabs::TabData::FromTabInterface(tab);
    SidebarRow row;
    row.tab_index = i;
    row.section = tab_strip_model_->IsTabPinned(i) ? SidebarSection::kPinned
                                                   : SidebarSection::kToday;
    row.title = data.title;
    row.favicon = data.favicon;
    row.is_active = i == tab_strip_model_->active_index();
    row.is_loading = data.network_state != tabs::TabNetworkState::kNone &&
                     !data.should_hide_throbber;
    row.is_audible = data.alert_state == tabs::TabAlert::kAudioPlaying;
    row.is_muted = data.alert_state == tabs::TabAlert::kAudioMuting;
    row.url = data.visible_url;
    rows.push_back(std::move(row));
  }
  return rows;
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
  // Close from the end so indices stay valid; pinned tabs are always first.
  for (int i = tab_strip_model_->count() - 1;
       i >= tab_strip_model_->IndexOfFirstNonPinnedTab(); --i) {
    tab_strip_model_->CloseWebContentsAt(i, kCloseTypes);
  }
}

void SidebarTabModel::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void SidebarTabModel::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void SidebarTabModel::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
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
  for (Observer& observer : observers_) {
    observer.OnSidebarModelChanged();
  }
}

}  // namespace arcium
