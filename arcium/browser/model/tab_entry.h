// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_MODEL_TAB_ENTRY_H_
#define ARCIUM_BROWSER_MODEL_TAB_ENTRY_H_

#include <optional>
#include <string>

#include "arcium/browser/model/entry_id.h"
#include "base/time/time.h"
#include "url/gurl.h"

namespace arcium {

enum class EntryKind { kFavorite, kPinned };

// A sidebar entity that outlives the tab representing it. `url` is the home
// URL for a favourite and the pinned URL for a pinned tab.
struct TabEntry {
  EntryId id;
  EntryKind kind = EntryKind::kPinned;
  SpaceId space_id;
  std::optional<FolderId> folder_id;
  int position = 0;
  GURL url;
  // Set by rename. Wins over `last_title` for ever, until cleared.
  std::u16string custom_title;
  // The page title seen when a tab was last bound, so a cold entry still has
  // something to draw.
  std::u16string last_title;
  base::Time created_at;

  const std::u16string& DisplayTitle() const {
    return custom_title.empty() ? last_title : custom_title;
  }
};

}  // namespace arcium

#endif  // ARCIUM_BROWSER_MODEL_TAB_ENTRY_H_
