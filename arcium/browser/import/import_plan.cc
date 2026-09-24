// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/import/import_plan.h"

#include <algorithm>
#include <set>
#include <utility>

#include "base/strings/string_util.h"

namespace arcium {

ImportPlan::ImportPlan() = default;
ImportPlan::ImportPlan(const ImportPlan&) = default;
ImportPlan::ImportPlan(ImportPlan&&) = default;
ImportPlan& ImportPlan::operator=(const ImportPlan&) = default;
ImportPlan& ImportPlan::operator=(ImportPlan&&) = default;
ImportPlan::~ImportPlan() = default;

ImportPlan::Counts ImportPlan::counts() const {
  Counts counts;
  counts.spaces = spaces.size();
  counts.folders = folders.size();
  std::set<GURL> favorites;
  for (const ImportEntry& entry : entries) {
    if (entry.kind == ImportEntryKind::kPinned) {
      ++counts.pinned;
    } else {
      favorites.insert(entry.url);
    }
  }
  counts.favorites = favorites.size();
  return counts;
}

size_t ImportPlan::PinnedCountIn(const std::string& space_key) const {
  size_t count = 0;
  for (const ImportEntry& entry : entries) {
    if (entry.kind == ImportEntryKind::kPinned &&
        entry.space_key == space_key) {
      ++count;
    }
  }
  return count;
}

void ImportPlan::AddEntry(ImportEntry entry) {
  const bool present = std::ranges::any_of(entries, [&](const ImportEntry& e) {
    return e.kind == entry.kind && e.space_key == entry.space_key &&
           e.url == entry.url;
  });
  if (!present) {
    entries.push_back(std::move(entry));
  }
}

bool IsImportableUrl(const GURL& url) {
  return url.is_valid() && url.SchemeIsHTTPOrHTTPS();
}

bool IsEmojiIcon(std::string_view icon) {
  return !icon.empty() && icon.size() <= 64 && base::IsStringUTF8(icon) &&
         std::ranges::none_of(icon, [](char c) {
           return base::IsAsciiAlpha(c) || c == ':' || c == '/' || c == '.';
         });
}

}  // namespace arcium
