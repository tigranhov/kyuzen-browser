// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_PLAYGROUND_FAKE_SIDEBAR_MODEL_H_
#define ARCIUM_UI_PLAYGROUND_FAKE_SIDEBAR_MODEL_H_

#include <string>
#include <vector>

#include "arcium/ui/sidebar/sidebar_model.h"
#include "base/observer_list.h"

namespace arcium {

// In-memory SidebarModel for the playground and view tests. Commands mutate
// the vector the way TabStripModel would and notify observers.
class FakeSidebarModel : public SidebarModel {
 public:
  FakeSidebarModel();
  ~FakeSidebarModel() override;

  void AddTab(const std::u16string& title,
              const std::string& url,
              SidebarSection section,
              bool active);
  void SetLoading(int tab_index, bool loading);
  void SetAudible(int tab_index, bool audible);

  // SidebarModel:
  std::vector<SidebarRow> rows() const override;
  void ActivateTab(int tab_index) override;
  void CloseTab(int tab_index) override;
  void MoveTab(int from_index, int to_index) override;
  void NewTab() override;
  void ClearToday() override;
  void AddObserver(Observer* observer) override;
  void RemoveObserver(Observer* observer) override;

 private:
  void Notify();
  void Reindex();

  std::vector<SidebarRow> rows_;
  base::ObserverList<Observer> observers_;
};

}  // namespace arcium

#endif  // ARCIUM_UI_PLAYGROUND_FAKE_SIDEBAR_MODEL_H_
