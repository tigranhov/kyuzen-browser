// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_IMPORT_IMPORT_PLAN_H_
#define ARCIUM_BROWSER_IMPORT_IMPORT_PLAN_H_

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "url/gurl.h"

namespace arcium {

// Where a plan was read from.
enum class ImportSourceKind { kZen, kArc };

// A set of separate logins in the other browser: a Zen container or an Arc
// profile. Becomes one Kyuzen profile.
struct ImportProfile {
  std::string key;
  std::string name;
};

struct ImportSpace {
  std::string key;
  std::string name;
  // One emoji, or empty to let Kyuzen draw the first letter.
  std::string icon;
  // The profile's key, or empty for the shared logins.
  std::string profile_key;
};

struct ImportFolder {
  std::string key;
  std::string space_key;
  // The enclosing folder's key, or empty at the top of the space.
  std::string parent_key;
  std::string name;
  bool collapsed = false;
};

enum class ImportEntryKind { kFavorite, kPinned };

struct ImportEntry {
  ImportEntryKind kind = ImportEntryKind::kPinned;
  std::string space_key;
  // Empty outside any folder. Favourites are never in one.
  std::string folder_key;
  GURL url;
  std::string title;
  // The title is one its owner gave it rather than the page's, so it stays
  // when the page later calls itself something else.
  bool renamed = false;
};

// Everything one source holds that Kyuzen can take, in Kyuzen's terms and in
// the source's order: parents before children, and entries in the order they
// were drawn. Keys are the plan's own and only name things inside it.
//
// Favourites are already fanned out: Kyuzen keeps them per space, so one the
// source shared between spaces appears once for each.
struct ImportPlan {
  ImportPlan();
  ImportPlan(const ImportPlan&);
  ImportPlan(ImportPlan&&);
  ImportPlan& operator=(const ImportPlan&);
  ImportPlan& operator=(ImportPlan&&);
  ~ImportPlan();

  struct Counts {
    size_t spaces = 0;
    size_t pinned = 0;
    // Distinct addresses, as the source showed them, not one per space.
    size_t favorites = 0;
    size_t folders = 0;
  };
  Counts counts() const;
  size_t PinnedCountIn(const std::string& space_key) const;

  // Adds `entry` unless its space already holds one of the same kind at the
  // same address, which is the rule a second import follows too.
  void AddEntry(ImportEntry entry);

  ImportSourceKind source = ImportSourceKind::kZen;
  std::vector<ImportProfile> profiles;
  std::vector<ImportSpace> spaces;
  std::vector<ImportFolder> folders;
  std::vector<ImportEntry> entries;
};

// Only web pages come across: a settings page, a note or an extension's page
// would mean nothing in Kyuzen.
bool IsImportableUrl(const GURL& url);

// Whether a source's space icon is an emoji Kyuzen can draw. Both browsers
// also offer icons of their own, saved as names or image addresses.
bool IsEmojiIcon(std::string_view icon);

}  // namespace arcium

#endif  // ARCIUM_BROWSER_IMPORT_IMPORT_PLAN_H_
