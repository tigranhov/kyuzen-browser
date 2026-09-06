// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_SIDEBAR_MODEL_H_
#define ARCIUM_UI_SIDEBAR_SIDEBAR_MODEL_H_

#include <string>
#include <vector>

#include "base/observer_list_types.h"
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

  virtual void AddObserver(Observer* observer) = 0;
  virtual void RemoveObserver(Observer* observer) = 0;
};

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_SIDEBAR_MODEL_H_
