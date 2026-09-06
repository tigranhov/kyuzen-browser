# Stage 2: The Arc Tab Model — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Favorites, Pinned and Today become Arcium-owned entities that outlive tabs and survive restarts, with folders, rename, auto-archive and search.

**Architecture:** A side table over `TabStripModel`. `ArciumModel` owns persistent entries and knows nothing about tabs; `TabBinding` maps an entry to the live tab currently representing it; `SidebarTabModel` merges the two into the rows the Stage 1 views already draw. Persistence is JSON via `ImportantFileWriter` for the live model and SQLite via `sql::Database` for the archive, both off the UI thread.

**Tech Stack:** Chromium 152.0.7977.83, C++20, Views, GN/siso, `base::Uuid`, `base::Value::Dict`, `base::ImportantFileWriter`, `sql::Database`, gtest.

**Spec:** `docs/superpowers/specs/2026-09-06-stage-2-arc-tab-model-design.md`

## Global Constraints

- All Arcium code lives in `arcium/`. Upstream files change only through numbered hook patches in `patches/`, each with a prose header naming the seam, the reason, and the `arcium/` function it delegates to, placed before the first `diff --git` line.
- Always-visible UI is Chromium Views in C++. Never WebUI. No Swift, AppKit or Cocoa.
- One window, one `Browser`, one `TabStripModel`. Entries are a side table over tabs, never extra browsers.
- Persistence: JSON via `ImportantFileWriter` for the live model, SQLite via `sql::Database` for the archive. Never write on the UI thread.
- Budgets: zero extra processes; +20 MB idle per window over vanilla Chromium and 0 per tab; +50 ms to first paint; 8 ms per frame on the UI thread and no sync I/O ever.
- Background work does nothing: no timers per tab, no polling, no thumbnails.
- Chromium C++ style, `git cl format` from the checkout. A file over ~500 lines is a smell; split it.
- Names are user-facing concepts: `Space`, `ArciumProfile`, `Favorite`, `PinnedTab`, `TodayTab`, `Folder`.
- Feature flags for anything user-visible and unfinished, in `arcium/common/arcium_features.h`.
- Commit messages say why. Commit small. Never commit Chromium sources or build output.
- Every commit message ends with `Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>`.

## Build and test commands

```
scripts/build dev chrome            # the browser
scripts/build dev arcium_unittests  # the unit tests
out/dev/arcium_unittests            # run them, from /Volumes/Texternal/chromium/src
scripts/run                         # launch out/dev
scripts/playground                  # the standalone sidebar host
scripts/perf --runs 3 --idle 60 --label stage2
```

Run the tests from the checkout: `/Volumes/Texternal/chromium/src/out/dev/arcium_unittests`. A single case: `--gtest_filter=ArciumModelTest.PinMovesEntryToPinnedSection`.

**Never start a build while another is running.** Two siso instances on one output directory deadlock on the lock file and sit there silently. Check with `pgrep -f siso` first.

## File structure

New, in `arcium/browser/model/` — pure data, no Chromium UI or browser dependencies, so it unit-tests without a browser:

| File | Responsibility |
|---|---|
| `entry_id.h` | `EntryId`, a strong wrapper over a `base::Uuid` string |
| `tab_entry.h` | `TabEntry` and `EntryKind` |
| `folder.h` | `Folder` |
| `space.h` | `Space` and `ArchiveTimeout` |
| `arcium_model.h/.cc` | Owns entries, folders and spaces. Mutations plus observers |
| `model_serializer.h/.cc` | `ArciumModel` to and from `base::Value::Dict`, with a schema version |

New, in `arcium/browser/`:

| File | Responsibility |
|---|---|
| `model_store.h/.cc` | Debounced atomic JSON persistence of `ArciumModel` |
| `archive_store.h/.cc` | SQLite archive: insert, list by recency, search |
| `tab_binding.h/.cc` | `EntryId` to `tabs::TabHandle` and back |
| `archive_service.h/.cc` | Idle-timeout scheduling and the never-archive rules |
| `tab_search_service.h/.cc` | Ranked query over live tabs, entries and the archive |

Modified:

| File | Change |
|---|---|
| `arcium/ui/sidebar/sidebar_model.h` | `SidebarRow` gains `entry_id`; new commands for the new interactions |
| `arcium/ui/browser/sidebar_tab_model.h/.cc` | Merges entries with live tabs instead of deriving rows from the strip |
| `arcium/ui/sidebar/tab_row_view.h/.cc` | Cold rows, inline rename, revert affordance |
| `arcium/ui/sidebar/tab_list_view.h/.cc` | Folder headers, drop targets |
| `arcium/ui/sidebar/favorites_grid_view.h/.cc` | Cold tiles, drop target |
| `arcium/ui/playground/fake_sidebar_model.h/.cc` | Cold rows and folders so the playground can host the new states |

New patches:

| Patch | Seam | Delegates to |
|---|---|---|
| `0110-sql-archive-tag.patch` | `tools/metrics/histograms/metadata/sql/histograms.xml` | Registration only, no call |
| `0120-restore-tab-entry.patch` | `chrome::AddRestoredTab` in `chrome/browser/ui/browser_tabrestore.cc` | `arcium::RestoreTabEntryFromExtraData` |
| `0130-session-tab-commands.patch` | `SessionService::BuildCommandsForTab` | `arcium::AppendTabEntryCommand` |

---

### Task 1: The model core

The persistent entities and the operations on them. No tabs, no disk, no Views — everything here is data that survives a quit, which is what makes it testable without a browser.

**Files:**
- Create: `arcium/browser/model/entry_id.h`
- Create: `arcium/browser/model/tab_entry.h`
- Create: `arcium/browser/model/folder.h`
- Create: `arcium/browser/model/space.h`
- Create: `arcium/browser/model/arcium_model.h`
- Create: `arcium/browser/model/arcium_model.cc`
- Create: `arcium/browser/model/BUILD.gn`
- Modify: `arcium/test/BUILD.gn`
- Test: `arcium/test/arcium_model_unittest.cc`

**Interfaces:**
- Consumes: nothing.
- Produces: `arcium::EntryId`, `arcium::TabEntry`, `arcium::EntryKind`, `arcium::Folder`, `arcium::Space`, `arcium::ArchiveTimeout`, `arcium::ArciumModel` with the methods listed in Step 3.

- [ ] **Step 1: Write the failing test**

Create `arcium/test/arcium_model_unittest.cc`:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/model/arcium_model.h"

#include "arcium/browser/model/tab_entry.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace arcium {
namespace {

class CountingObserver : public ArciumModel::Observer {
 public:
  void OnArciumModelChanged() override { ++count; }
  int count = 0;
};

class ArciumModelTest : public testing::Test {
 protected:
  ArciumModel model_;
};

TEST_F(ArciumModelTest, StartsWithOneDefaultSpace) {
  ASSERT_EQ(1u, model_.spaces().size());
  EXPECT_EQ(model_.default_space_id(), model_.spaces()[0].id);
  EXPECT_EQ(ArchiveTimeout::kTwelveHours, model_.spaces()[0].archive_timeout);
}

TEST_F(ArciumModelTest, AddEntryReturnsAStableUniqueId) {
  const EntryId a = model_.AddEntry(EntryKind::kFavorite,
                                    GURL("https://a.example/"), u"A");
  const EntryId b = model_.AddEntry(EntryKind::kPinned,
                                    GURL("https://b.example/"), u"B");
  EXPECT_NE(a, b);
  ASSERT_TRUE(model_.GetEntry(a));
  EXPECT_EQ(EntryKind::kFavorite, model_.GetEntry(a)->kind);
  EXPECT_EQ(GURL("https://a.example/"), model_.GetEntry(a)->url);
  EXPECT_EQ(u"A", model_.GetEntry(a)->last_title);
  EXPECT_EQ(model_.default_space_id(), model_.GetEntry(a)->space_id);
}

TEST_F(ArciumModelTest, EntriesForKindComeBackInPositionOrder) {
  const EntryId first = model_.AddEntry(EntryKind::kPinned,
                                        GURL("https://1.example/"), u"1");
  const EntryId second = model_.AddEntry(EntryKind::kPinned,
                                         GURL("https://2.example/"), u"2");
  std::vector<const TabEntry*> pinned =
      model_.EntriesForKind(model_.default_space_id(), EntryKind::kPinned);
  ASSERT_EQ(2u, pinned.size());
  EXPECT_EQ(first, pinned[0]->id);
  EXPECT_EQ(second, pinned[1]->id);
}

TEST_F(ArciumModelTest, SetEntryKindMovesBetweenSections) {
  const EntryId id = model_.AddEntry(EntryKind::kPinned,
                                     GURL("https://a.example/"), u"A");
  model_.SetEntryKind(id, EntryKind::kFavorite);
  EXPECT_EQ(EntryKind::kFavorite, model_.GetEntry(id)->kind);
  EXPECT_TRUE(
      model_.EntriesForKind(model_.default_space_id(), EntryKind::kPinned)
          .empty());
  EXPECT_EQ(1u,
            model_.EntriesForKind(model_.default_space_id(),
                                  EntryKind::kFavorite)
                .size());
}

TEST_F(ArciumModelTest, CustomTitleWinsOverLastTitle) {
  const EntryId id = model_.AddEntry(EntryKind::kPinned,
                                     GURL("https://a.example/"), u"Page title");
  EXPECT_EQ(u"Page title", model_.GetEntry(id)->DisplayTitle());
  model_.SetCustomTitle(id, u"My name");
  EXPECT_EQ(u"My name", model_.GetEntry(id)->DisplayTitle());
  // A later page title does not override a custom one.
  model_.SetLastTitle(id, u"New page title");
  EXPECT_EQ(u"My name", model_.GetEntry(id)->DisplayTitle());
  // Clearing the custom title restores the page title.
  model_.SetCustomTitle(id, u"");
  EXPECT_EQ(u"New page title", model_.GetEntry(id)->DisplayTitle());
}

TEST_F(ArciumModelTest, ReorderEntryMovesItWithinItsKind) {
  const EntryId a = model_.AddEntry(EntryKind::kPinned,
                                    GURL("https://a.example/"), u"A");
  const EntryId b = model_.AddEntry(EntryKind::kPinned,
                                    GURL("https://b.example/"), u"B");
  const EntryId c = model_.AddEntry(EntryKind::kPinned,
                                    GURL("https://c.example/"), u"C");
  model_.ReorderEntry(c, 0);
  std::vector<const TabEntry*> pinned =
      model_.EntriesForKind(model_.default_space_id(), EntryKind::kPinned);
  ASSERT_EQ(3u, pinned.size());
  EXPECT_EQ(c, pinned[0]->id);
  EXPECT_EQ(a, pinned[1]->id);
  EXPECT_EQ(b, pinned[2]->id);
}

TEST_F(ArciumModelTest, RemoveEntryDropsIt) {
  const EntryId id = model_.AddEntry(EntryKind::kPinned,
                                     GURL("https://a.example/"), u"A");
  model_.RemoveEntry(id);
  EXPECT_FALSE(model_.GetEntry(id));
}

TEST_F(ArciumModelTest, FoldersHoldPinnedEntries) {
  const FolderId folder = model_.AddFolder(u"Work");
  const EntryId id = model_.AddEntry(EntryKind::kPinned,
                                     GURL("https://a.example/"), u"A");
  model_.SetEntryFolder(id, folder);
  EXPECT_EQ(folder, model_.GetEntry(id)->folder_id);

  model_.SetFolderCollapsed(folder, true);
  ASSERT_TRUE(model_.GetFolder(folder));
  EXPECT_TRUE(model_.GetFolder(folder)->collapsed);

  model_.SetFolderName(folder, u"Personal");
  EXPECT_EQ(u"Personal", model_.GetFolder(folder)->name);
}

TEST_F(ArciumModelTest, RemovingAFolderReturnsItsEntriesToTheTopLevel) {
  const FolderId folder = model_.AddFolder(u"Work");
  const EntryId id = model_.AddEntry(EntryKind::kPinned,
                                     GURL("https://a.example/"), u"A");
  model_.SetEntryFolder(id, folder);
  model_.RemoveFolder(folder);
  ASSERT_TRUE(model_.GetEntry(id));
  EXPECT_FALSE(model_.GetEntry(id)->folder_id.has_value());
}

TEST_F(ArciumModelTest, ObserverFiresOnEveryMutation) {
  CountingObserver observer;
  model_.AddObserver(&observer);
  const EntryId id = model_.AddEntry(EntryKind::kPinned,
                                     GURL("https://a.example/"), u"A");
  EXPECT_EQ(1, observer.count);
  model_.SetCustomTitle(id, u"X");
  EXPECT_EQ(2, observer.count);
  model_.RemoveEntry(id);
  EXPECT_EQ(3, observer.count);
  model_.RemoveObserver(&observer);
  model_.AddEntry(EntryKind::kPinned, GURL("https://b.example/"), u"B");
  EXPECT_EQ(3, observer.count);
}

TEST_F(ArciumModelTest, MutatingAnUnknownIdIsANoOpNotACrash) {
  const EntryId missing = EntryId::Generate();
  model_.SetCustomTitle(missing, u"X");
  model_.SetEntryKind(missing, EntryKind::kFavorite);
  model_.ReorderEntry(missing, 0);
  model_.RemoveEntry(missing);
  EXPECT_FALSE(model_.GetEntry(missing));
}

}  // namespace
}  // namespace arcium
```

- [ ] **Step 2: Run the test to verify it fails**

Add the file to the test target first — edit `arcium/test/BUILD.gn` so `sources` reads:

```gn
  sources = [
    "arcium_model_unittest.cc",
    "sidebar_tab_model_unittest.cc",
  ]
```

and add `"//arcium/browser/model",` to its `deps`, keeping the list alphabetical.

Run: `scripts/build dev arcium_unittests`
Expected: FAIL — `arcium/browser/model/arcium_model.h` does not exist.

- [ ] **Step 3: Write the implementation**

`arcium/browser/model/entry_id.h`:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_MODEL_ENTRY_ID_H_
#define ARCIUM_BROWSER_MODEL_ENTRY_ID_H_

#include <compare>
#include <string>

#include "base/uuid.h"

namespace arcium {

// A stable identity for a persistent entity. Distinct types for entries and
// folders so one cannot be passed where the other is meant.
template <typename Tag>
class TypedId {
 public:
  TypedId() = default;

  static TypedId Generate() {
    return TypedId(base::Uuid::GenerateRandomV4().AsLowercaseString());
  }

  // Returns an invalid id when `value` is not a well-formed UUID, so a
  // corrupt file cannot inject an id that collides with a generated one.
  static TypedId FromString(const std::string& value) {
    return base::Uuid::ParseLowercase(value).is_valid() ? TypedId(value)
                                                        : TypedId();
  }

  bool is_valid() const { return !value_.empty(); }
  const std::string& value() const { return value_; }

  friend bool operator==(const TypedId&, const TypedId&) = default;
  friend auto operator<=>(const TypedId&, const TypedId&) = default;

 private:
  explicit TypedId(std::string value) : value_(std::move(value)) {}

  std::string value_;
};

struct EntryIdTag;
struct FolderIdTag;
struct SpaceIdTag;

using EntryId = TypedId<EntryIdTag>;
using FolderId = TypedId<FolderIdTag>;
using SpaceId = TypedId<SpaceIdTag>;

}  // namespace arcium

#endif  // ARCIUM_BROWSER_MODEL_ENTRY_ID_H_
```

`arcium/browser/model/tab_entry.h`:

```cpp
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
```

`arcium/browser/model/folder.h`:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_MODEL_FOLDER_H_
#define ARCIUM_BROWSER_MODEL_FOLDER_H_

#include <string>

#include "arcium/browser/model/entry_id.h"

namespace arcium {

// Holds pinned entries. Not built on Chromium tab groups: a group holds live
// tabs, and a cold pinned entry has none.
struct Folder {
  FolderId id;
  SpaceId space_id;
  std::u16string name;
  bool collapsed = false;
  int position = 0;
};

}  // namespace arcium

#endif  // ARCIUM_BROWSER_MODEL_FOLDER_H_
```

`arcium/browser/model/space.h`:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_MODEL_SPACE_H_
#define ARCIUM_BROWSER_MODEL_SPACE_H_

#include <optional>
#include <string>

#include "arcium/browser/model/entry_id.h"
#include "base/time/time.h"

namespace arcium {

// How long a Today tab may sit idle before it is archived.
enum class ArchiveTimeout { kTwelveHours, kOneDay, kSevenDays, kNever };

// Returns std::nullopt for kNever, which means no expiry is ever scheduled.
std::optional<base::TimeDelta> ArchiveTimeoutToDelta(ArchiveTimeout timeout);

// Stage 2 has exactly one space. The id travels through the model from the
// start so Stage 3 is a UI change rather than a data migration.
struct Space {
  SpaceId id;
  std::u16string name;
  ArchiveTimeout archive_timeout = ArchiveTimeout::kTwelveHours;
  int position = 0;
};

}  // namespace arcium

#endif  // ARCIUM_BROWSER_MODEL_SPACE_H_
```

`arcium/browser/model/arcium_model.h`:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_MODEL_ARCIUM_MODEL_H_
#define ARCIUM_BROWSER_MODEL_ARCIUM_MODEL_H_

#include <optional>
#include <string>
#include <vector>

#include "arcium/browser/model/folder.h"
#include "arcium/browser/model/space.h"
#include "arcium/browser/model/tab_entry.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"

namespace arcium {

// Owns every persistent sidebar entity. Deliberately ignorant of tabs, disk
// and Views: everything here is data that survives a quit, which is what lets
// it be tested without a browser.
class ArciumModel {
 public:
  class Observer : public base::CheckedObserver {
   public:
    // Fired after any mutation. ModelStore schedules a save; views rebuild.
    virtual void OnArciumModelChanged() = 0;
  };

  ArciumModel();
  ArciumModel(const ArciumModel&) = delete;
  ArciumModel& operator=(const ArciumModel&) = delete;
  ~ArciumModel();

  // Spaces. Stage 2 always has exactly one.
  const std::vector<Space>& spaces() const { return spaces_; }
  SpaceId default_space_id() const;
  void SetArchiveTimeout(SpaceId space_id, ArchiveTimeout timeout);

  // Entries. Every mutation notifies observers, and every one that names an
  // unknown id is a no-op rather than a crash, because ids arrive from disk.
  EntryId AddEntry(EntryKind kind, const GURL& url, const std::u16string& title);
  void RemoveEntry(EntryId id);
  void SetEntryKind(EntryId id, EntryKind kind);
  void SetCustomTitle(EntryId id, const std::u16string& title);
  void SetLastTitle(EntryId id, const std::u16string& title);
  void SetEntryUrl(EntryId id, const GURL& url);
  void SetEntryFolder(EntryId id, std::optional<FolderId> folder_id);
  void ReorderEntry(EntryId id, int new_position);

  const TabEntry* GetEntry(EntryId id) const;
  std::vector<const TabEntry*> EntriesForKind(SpaceId space_id,
                                              EntryKind kind) const;
  const std::vector<TabEntry>& entries() const { return entries_; }

  // Folders. Removing one returns its entries to the top level rather than
  // deleting them: a folder is a grouping, not an owner.
  FolderId AddFolder(const std::u16string& name);
  void RemoveFolder(FolderId id);
  void SetFolderName(FolderId id, const std::u16string& name);
  void SetFolderCollapsed(FolderId id, bool collapsed);
  const Folder* GetFolder(FolderId id) const;
  const std::vector<Folder>& folders() const { return folders_; }

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // For ModelSerializer, which rebuilds a model from disk without firing a
  // notification per entry.
  void ReplaceAll(std::vector<Space> spaces,
                  std::vector<Folder> folders,
                  std::vector<TabEntry> entries);

 private:
  TabEntry* FindEntry(EntryId id);
  Folder* FindFolder(FolderId id);
  // Renumbers positions 0..n-1 within each (space, kind) so a reorder never
  // leaves gaps that would make the order depend on insertion history.
  void NormalisePositions();
  void Notify();

  std::vector<Space> spaces_;
  std::vector<Folder> folders_;
  std::vector<TabEntry> entries_;
  base::ObserverList<Observer> observers_;
};

}  // namespace arcium

#endif  // ARCIUM_BROWSER_MODEL_ARCIUM_MODEL_H_
```

`arcium/browser/model/arcium_model.cc`:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/model/arcium_model.h"

#include <algorithm>

#include "base/time/time.h"

namespace arcium {

std::optional<base::TimeDelta> ArchiveTimeoutToDelta(ArchiveTimeout timeout) {
  switch (timeout) {
    case ArchiveTimeout::kTwelveHours:
      return base::Hours(12);
    case ArchiveTimeout::kOneDay:
      return base::Hours(24);
    case ArchiveTimeout::kSevenDays:
      return base::Days(7);
    case ArchiveTimeout::kNever:
      return std::nullopt;
  }
}

ArciumModel::ArciumModel() {
  Space space;
  space.id = SpaceId::Generate();
  space.name = u"Space";
  space.archive_timeout = ArchiveTimeout::kTwelveHours;
  space.position = 0;
  spaces_.push_back(std::move(space));
}

ArciumModel::~ArciumModel() = default;

SpaceId ArciumModel::default_space_id() const {
  return spaces_.empty() ? SpaceId() : spaces_.front().id;
}

void ArciumModel::SetArchiveTimeout(SpaceId space_id, ArchiveTimeout timeout) {
  for (Space& space : spaces_) {
    if (space.id == space_id) {
      space.archive_timeout = timeout;
      Notify();
      return;
    }
  }
}

EntryId ArciumModel::AddEntry(EntryKind kind,
                              const GURL& url,
                              const std::u16string& title) {
  TabEntry entry;
  entry.id = EntryId::Generate();
  entry.kind = kind;
  entry.space_id = default_space_id();
  entry.url = url;
  entry.last_title = title;
  entry.created_at = base::Time::Now();
  entry.position = static_cast<int>(
      EntriesForKind(entry.space_id, kind).size());
  const EntryId id = entry.id;
  entries_.push_back(std::move(entry));
  Notify();
  return id;
}

void ArciumModel::RemoveEntry(EntryId id) {
  const size_t before = entries_.size();
  std::erase_if(entries_,
                [id](const TabEntry& entry) { return entry.id == id; });
  if (entries_.size() != before) {
    NormalisePositions();
    Notify();
  }
}

void ArciumModel::SetEntryKind(EntryId id, EntryKind kind) {
  TabEntry* entry = FindEntry(id);
  if (!entry || entry->kind == kind) {
    return;
  }
  entry->kind = kind;
  // A favourite is never inside a folder: folders hold pinned entries only.
  if (kind == EntryKind::kFavorite) {
    entry->folder_id.reset();
  }
  entry->position = static_cast<int>(
      EntriesForKind(entry->space_id, kind).size());
  NormalisePositions();
  Notify();
}

void ArciumModel::SetCustomTitle(EntryId id, const std::u16string& title) {
  TabEntry* entry = FindEntry(id);
  if (!entry) {
    return;
  }
  entry->custom_title = title;
  Notify();
}

void ArciumModel::SetLastTitle(EntryId id, const std::u16string& title) {
  TabEntry* entry = FindEntry(id);
  if (!entry || entry->last_title == title) {
    return;
  }
  entry->last_title = title;
  Notify();
}

void ArciumModel::SetEntryUrl(EntryId id, const GURL& url) {
  TabEntry* entry = FindEntry(id);
  if (!entry || entry->url == url) {
    return;
  }
  entry->url = url;
  Notify();
}

void ArciumModel::SetEntryFolder(EntryId id,
                                 std::optional<FolderId> folder_id) {
  TabEntry* entry = FindEntry(id);
  if (!entry) {
    return;
  }
  entry->folder_id = folder_id;
  Notify();
}

void ArciumModel::ReorderEntry(EntryId id, int new_position) {
  TabEntry* entry = FindEntry(id);
  if (!entry) {
    return;
  }
  const SpaceId space_id = entry->space_id;
  const EntryKind kind = entry->kind;
  std::vector<const TabEntry*> siblings = EntriesForKind(space_id, kind);
  new_position = std::clamp(new_position, 0,
                            static_cast<int>(siblings.size()) - 1);

  // Renumber by walking the sibling order with the moved entry lifted out and
  // reinserted, so positions stay 0..n-1 with no gaps.
  std::vector<EntryId> order;
  order.reserve(siblings.size());
  for (const TabEntry* sibling : siblings) {
    if (sibling->id != id) {
      order.push_back(sibling->id);
    }
  }
  order.insert(order.begin() + new_position, id);
  for (size_t i = 0; i < order.size(); ++i) {
    FindEntry(order[i])->position = static_cast<int>(i);
  }
  Notify();
}

const TabEntry* ArciumModel::GetEntry(EntryId id) const {
  for (const TabEntry& entry : entries_) {
    if (entry.id == id) {
      return &entry;
    }
  }
  return nullptr;
}

std::vector<const TabEntry*> ArciumModel::EntriesForKind(
    SpaceId space_id,
    EntryKind kind) const {
  std::vector<const TabEntry*> result;
  for (const TabEntry& entry : entries_) {
    if (entry.space_id == space_id && entry.kind == kind) {
      result.push_back(&entry);
    }
  }
  std::sort(result.begin(), result.end(),
            [](const TabEntry* a, const TabEntry* b) {
              return a->position < b->position;
            });
  return result;
}

FolderId ArciumModel::AddFolder(const std::u16string& name) {
  Folder folder;
  folder.id = FolderId::Generate();
  folder.space_id = default_space_id();
  folder.name = name;
  folder.position = static_cast<int>(folders_.size());
  const FolderId id = folder.id;
  folders_.push_back(std::move(folder));
  Notify();
  return id;
}

void ArciumModel::RemoveFolder(FolderId id) {
  const size_t before = folders_.size();
  std::erase_if(folders_, [id](const Folder& f) { return f.id == id; });
  if (folders_.size() == before) {
    return;
  }
  // A folder groups entries, it does not own them.
  for (TabEntry& entry : entries_) {
    if (entry.folder_id == id) {
      entry.folder_id.reset();
    }
  }
  Notify();
}

void ArciumModel::SetFolderName(FolderId id, const std::u16string& name) {
  Folder* folder = FindFolder(id);
  if (!folder) {
    return;
  }
  folder->name = name;
  Notify();
}

void ArciumModel::SetFolderCollapsed(FolderId id, bool collapsed) {
  Folder* folder = FindFolder(id);
  if (!folder || folder->collapsed == collapsed) {
    return;
  }
  folder->collapsed = collapsed;
  Notify();
}

const Folder* ArciumModel::GetFolder(FolderId id) const {
  for (const Folder& folder : folders_) {
    if (folder.id == id) {
      return &folder;
    }
  }
  return nullptr;
}

void ArciumModel::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void ArciumModel::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void ArciumModel::ReplaceAll(std::vector<Space> spaces,
                             std::vector<Folder> folders,
                             std::vector<TabEntry> entries) {
  spaces_ = std::move(spaces);
  folders_ = std::move(folders);
  entries_ = std::move(entries);
  if (spaces_.empty()) {
    Space space;
    space.id = SpaceId::Generate();
    space.name = u"Space";
    spaces_.push_back(std::move(space));
  }
  NormalisePositions();
  Notify();
}

TabEntry* ArciumModel::FindEntry(EntryId id) {
  for (TabEntry& entry : entries_) {
    if (entry.id == id) {
      return &entry;
    }
  }
  return nullptr;
}

Folder* ArciumModel::FindFolder(FolderId id) {
  for (Folder& folder : folders_) {
    if (folder.id == id) {
      return &folder;
    }
  }
  return nullptr;
}

void ArciumModel::NormalisePositions() {
  for (const Space& space : spaces_) {
    for (EntryKind kind : {EntryKind::kFavorite, EntryKind::kPinned}) {
      std::vector<const TabEntry*> ordered = EntriesForKind(space.id, kind);
      for (size_t i = 0; i < ordered.size(); ++i) {
        FindEntry(ordered[i]->id)->position = static_cast<int>(i);
      }
    }
  }
}

void ArciumModel::Notify() {
  for (Observer& observer : observers_) {
    observer.OnArciumModelChanged();
  }
}

}  // namespace arcium
```

`arcium/browser/model/BUILD.gn`:

```gn
# The persistent sidebar model. No Chromium UI or browser dependencies, so it
# unit-tests without a browser and the playground could host it directly.
source_set("model") {
  sources = [
    "arcium_model.cc",
    "arcium_model.h",
    "entry_id.h",
    "folder.h",
    "space.h",
    "tab_entry.h",
  ]
  deps = [
    "//base",
    "//url",
  ]
}
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `scripts/build dev arcium_unittests && /Volumes/Texternal/chromium/src/out/dev/arcium_unittests --gtest_filter=ArciumModelTest.*`
Expected: PASS, 11 tests.

- [ ] **Step 5: Commit**

```bash
git add arcium/browser/model arcium/test
git commit -m "$(cat <<'EOF'
Stage 2: the persistent sidebar model

Favorites and pinned tabs need identity that outlives a tab, so entries
get a UUID rather than a tab index. The model knows nothing about tabs,
disk or Views, which is what lets it be tested without a browser.

Positions are renumbered after every structural change so ordering never
depends on insertion history, and every mutation naming an unknown id is
a no-op: ids arrive from disk and a corrupt file must not crash a window.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 2: JSON serialisation

Turning the model into `base::Value::Dict` and back, with a schema version and tolerance for files written by a future or a broken Arcium.

**Files:**
- Create: `arcium/browser/model/model_serializer.h`
- Create: `arcium/browser/model/model_serializer.cc`
- Modify: `arcium/browser/model/BUILD.gn`
- Modify: `arcium/test/BUILD.gn`
- Test: `arcium/test/model_serializer_unittest.cc`

**Interfaces:**
- Consumes: `arcium::ArciumModel` and its entity structs from Task 1.
- Produces: `base::Value::Dict arcium::SerializeModel(const ArciumModel&)` and `bool arcium::DeserializeModel(const base::Value::Dict&, ArciumModel*)`, plus `constexpr int arcium::kModelSchemaVersion = 1`.

- [ ] **Step 1: Write the failing test**

Create `arcium/test/model_serializer_unittest.cc`:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/model/model_serializer.h"

#include "arcium/browser/model/arcium_model.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace arcium {
namespace {

TEST(ModelSerializerTest, RoundTripPreservesEntriesFoldersAndSpaces) {
  ArciumModel original;
  original.SetArchiveTimeout(original.default_space_id(),
                             ArchiveTimeout::kSevenDays);
  const FolderId folder = original.AddFolder(u"Work");
  const EntryId pinned = original.AddEntry(EntryKind::kPinned,
                                           GURL("https://pin.example/"), u"P");
  original.SetEntryFolder(pinned, folder);
  original.SetCustomTitle(pinned, u"Renamed");
  original.AddEntry(EntryKind::kFavorite, GURL("https://fav.example/"), u"F");

  ArciumModel restored;
  ASSERT_TRUE(DeserializeModel(SerializeModel(original), &restored));

  ASSERT_EQ(1u, restored.spaces().size());
  EXPECT_EQ(original.default_space_id(), restored.default_space_id());
  EXPECT_EQ(ArchiveTimeout::kSevenDays,
            restored.spaces()[0].archive_timeout);

  ASSERT_EQ(1u, restored.folders().size());
  EXPECT_EQ(folder, restored.folders()[0].id);
  EXPECT_EQ(u"Work", restored.folders()[0].name);

  const TabEntry* entry = restored.GetEntry(pinned);
  ASSERT_TRUE(entry);
  EXPECT_EQ(EntryKind::kPinned, entry->kind);
  EXPECT_EQ(GURL("https://pin.example/"), entry->url);
  EXPECT_EQ(u"Renamed", entry->custom_title);
  EXPECT_EQ(u"P", entry->last_title);
  ASSERT_TRUE(entry->folder_id.has_value());
  EXPECT_EQ(folder, *entry->folder_id);
  EXPECT_EQ(1u, restored.EntriesForKind(restored.default_space_id(),
                                        EntryKind::kFavorite)
                    .size());
}

TEST(ModelSerializerTest, UnknownFieldsAreIgnoredNotFatal) {
  ArciumModel original;
  original.AddEntry(EntryKind::kPinned, GURL("https://a.example/"), u"A");
  base::Value::Dict dict = SerializeModel(original);
  dict.Set("something_from_the_future", "hello");
  base::Value::List* entries = dict.FindList("entries");
  ASSERT_TRUE(entries);
  (*entries)[0].GetDict().Set("also_new", 42);

  ArciumModel restored;
  EXPECT_TRUE(DeserializeModel(dict, &restored));
  EXPECT_EQ(1u, restored.entries().size());
}

TEST(ModelSerializerTest, ANewerSchemaVersionIsRefused) {
  ArciumModel original;
  base::Value::Dict dict = SerializeModel(original);
  dict.Set("version", kModelSchemaVersion + 1);

  ArciumModel restored;
  EXPECT_FALSE(DeserializeModel(dict, &restored));
}

TEST(ModelSerializerTest, AMissingVersionIsRefused) {
  base::Value::Dict dict;
  dict.Set("entries", base::Value::List());

  ArciumModel restored;
  EXPECT_FALSE(DeserializeModel(dict, &restored));
}

TEST(ModelSerializerTest, EntriesWithBadIdsOrUrlsAreDroppedNotFatal) {
  ArciumModel original;
  original.AddEntry(EntryKind::kPinned, GURL("https://good.example/"), u"good");
  base::Value::Dict dict = SerializeModel(original);
  base::Value::List* entries = dict.FindList("entries");
  ASSERT_TRUE(entries);

  base::Value::Dict bad_id;
  bad_id.Set("id", "not-a-uuid");
  bad_id.Set("kind", "pinned");
  bad_id.Set("url", "https://bad.example/");
  entries->Append(std::move(bad_id));

  base::Value::Dict bad_url;
  bad_url.Set("id", EntryId::Generate().value());
  bad_url.Set("kind", "pinned");
  bad_url.Set("url", "not a url");
  entries->Append(std::move(bad_url));

  ArciumModel restored;
  ASSERT_TRUE(DeserializeModel(dict, &restored));
  EXPECT_EQ(1u, restored.entries().size());
  EXPECT_EQ(GURL("https://good.example/"), restored.entries()[0].url);
}

TEST(ModelSerializerTest, TruncatedJsonDoesNotParse) {
  ArciumModel original;
  original.AddEntry(EntryKind::kPinned, GURL("https://a.example/"), u"A");
  std::string json = *base::WriteJson(SerializeModel(original));
  const std::string truncated = json.substr(0, json.size() / 2);
  EXPECT_FALSE(base::JSONReader::ReadDict(truncated).has_value());
}

TEST(ModelSerializerTest, AnEntryInAnUnknownFolderLandsAtTheTopLevel) {
  ArciumModel original;
  const EntryId id = original.AddEntry(EntryKind::kPinned,
                                       GURL("https://a.example/"), u"A");
  base::Value::Dict dict = SerializeModel(original);
  base::Value::List* entries = dict.FindList("entries");
  ASSERT_TRUE(entries);
  (*entries)[0].GetDict().Set("folder_id", FolderId::Generate().value());

  ArciumModel restored;
  ASSERT_TRUE(DeserializeModel(dict, &restored));
  const TabEntry* entry = restored.GetEntry(id);
  ASSERT_TRUE(entry);
  EXPECT_FALSE(entry->folder_id.has_value());
}

}  // namespace
}  // namespace arcium
```

- [ ] **Step 2: Run the test to verify it fails**

Add `"model_serializer_unittest.cc",` to `sources` in `arcium/test/BUILD.gn`, keeping it alphabetical.

Run: `scripts/build dev arcium_unittests`
Expected: FAIL — `arcium/browser/model/model_serializer.h` does not exist.

- [ ] **Step 3: Write the implementation**

`arcium/browser/model/model_serializer.h`:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_MODEL_MODEL_SERIALIZER_H_
#define ARCIUM_BROWSER_MODEL_MODEL_SERIALIZER_H_

#include "base/values.h"

namespace arcium {

class ArciumModel;

// Bumped whenever a field changes meaning. A file claiming a newer version is
// refused rather than half-read, so a downgrade cannot silently drop data.
inline constexpr int kModelSchemaVersion = 1;

base::Value::Dict SerializeModel(const ArciumModel& model);

// Returns false only when the file is unusable as a whole: a missing or newer
// version. Individual malformed entries are dropped, because losing one row
// beats refusing to start.
bool DeserializeModel(const base::Value::Dict& dict, ArciumModel* model);

}  // namespace arcium

#endif  // ARCIUM_BROWSER_MODEL_MODEL_SERIALIZER_H_
```

`arcium/browser/model/model_serializer.cc`:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/model/model_serializer.h"

#include <set>
#include <string>
#include <vector>

#include "arcium/browser/model/arcium_model.h"
#include "base/strings/utf_string_conversions.h"

namespace arcium {

namespace {

// Strings rather than ints on the wire: a JSON file a human may read during a
// bug report should say "pinned", not "1".
std::string KindToString(EntryKind kind) {
  return kind == EntryKind::kFavorite ? "favorite" : "pinned";
}

EntryKind KindFromString(const std::string* value) {
  return (value && *value == "favorite") ? EntryKind::kFavorite
                                         : EntryKind::kPinned;
}

std::string TimeoutToString(ArchiveTimeout timeout) {
  switch (timeout) {
    case ArchiveTimeout::kTwelveHours:
      return "12h";
    case ArchiveTimeout::kOneDay:
      return "24h";
    case ArchiveTimeout::kSevenDays:
      return "7d";
    case ArchiveTimeout::kNever:
      return "never";
  }
}

ArchiveTimeout TimeoutFromString(const std::string* value) {
  if (!value) {
    return ArchiveTimeout::kTwelveHours;
  }
  if (*value == "24h") {
    return ArchiveTimeout::kOneDay;
  }
  if (*value == "7d") {
    return ArchiveTimeout::kSevenDays;
  }
  if (*value == "never") {
    return ArchiveTimeout::kNever;
  }
  return ArchiveTimeout::kTwelveHours;
}

}  // namespace

base::Value::Dict SerializeModel(const ArciumModel& model) {
  base::Value::Dict dict;
  dict.Set("version", kModelSchemaVersion);

  base::Value::List spaces;
  for (const Space& space : model.spaces()) {
    base::Value::Dict value;
    value.Set("id", space.id.value());
    value.Set("name", base::UTF16ToUTF8(space.name));
    value.Set("archive_timeout", TimeoutToString(space.archive_timeout));
    value.Set("position", space.position);
    spaces.Append(std::move(value));
  }
  dict.Set("spaces", std::move(spaces));

  base::Value::List folders;
  for (const Folder& folder : model.folders()) {
    base::Value::Dict value;
    value.Set("id", folder.id.value());
    value.Set("space_id", folder.space_id.value());
    value.Set("name", base::UTF16ToUTF8(folder.name));
    value.Set("collapsed", folder.collapsed);
    value.Set("position", folder.position);
    folders.Append(std::move(value));
  }
  dict.Set("folders", std::move(folders));

  base::Value::List entries;
  for (const TabEntry& entry : model.entries()) {
    base::Value::Dict value;
    value.Set("id", entry.id.value());
    value.Set("kind", KindToString(entry.kind));
    value.Set("space_id", entry.space_id.value());
    if (entry.folder_id) {
      value.Set("folder_id", entry.folder_id->value());
    }
    value.Set("position", entry.position);
    value.Set("url", entry.url.spec());
    value.Set("custom_title", base::UTF16ToUTF8(entry.custom_title));
    value.Set("last_title", base::UTF16ToUTF8(entry.last_title));
    value.Set("created_at",
              static_cast<double>(
                  entry.created_at.ToDeltaSinceWindowsEpoch().InMicroseconds()));
    entries.Append(std::move(value));
  }
  dict.Set("entries", std::move(entries));

  return dict;
}

bool DeserializeModel(const base::Value::Dict& dict, ArciumModel* model) {
  const std::optional<int> version = dict.FindInt("version");
  if (!version || *version > kModelSchemaVersion) {
    return false;
  }

  std::vector<Space> spaces;
  std::set<SpaceId> space_ids;
  if (const base::Value::List* list = dict.FindList("spaces")) {
    for (const base::Value& item : *list) {
      const base::Value::Dict* value = item.GetIfDict();
      if (!value) {
        continue;
      }
      const std::string* id = value->FindString("id");
      Space space;
      space.id = id ? SpaceId::FromString(*id) : SpaceId();
      if (!space.id.is_valid()) {
        continue;
      }
      const std::string* name = value->FindString("name");
      space.name = name ? base::UTF8ToUTF16(*name) : u"Space";
      space.archive_timeout =
          TimeoutFromString(value->FindString("archive_timeout"));
      space.position = value->FindInt("position").value_or(0);
      space_ids.insert(space.id);
      spaces.push_back(std::move(space));
    }
  }
  if (spaces.empty()) {
    return false;
  }
  const SpaceId default_space = spaces.front().id;

  std::vector<Folder> folders;
  std::set<FolderId> folder_ids;
  if (const base::Value::List* list = dict.FindList("folders")) {
    for (const base::Value& item : *list) {
      const base::Value::Dict* value = item.GetIfDict();
      if (!value) {
        continue;
      }
      const std::string* id = value->FindString("id");
      Folder folder;
      folder.id = id ? FolderId::FromString(*id) : FolderId();
      if (!folder.id.is_valid()) {
        continue;
      }
      const std::string* space_id = value->FindString("space_id");
      folder.space_id = space_id ? SpaceId::FromString(*space_id) : SpaceId();
      if (!space_ids.contains(folder.space_id)) {
        folder.space_id = default_space;
      }
      const std::string* name = value->FindString("name");
      folder.name = name ? base::UTF8ToUTF16(*name) : std::u16string();
      folder.collapsed = value->FindBool("collapsed").value_or(false);
      folder.position = value->FindInt("position").value_or(0);
      folder_ids.insert(folder.id);
      folders.push_back(std::move(folder));
    }
  }

  std::vector<TabEntry> entries;
  if (const base::Value::List* list = dict.FindList("entries")) {
    for (const base::Value& item : *list) {
      const base::Value::Dict* value = item.GetIfDict();
      if (!value) {
        continue;
      }
      const std::string* id = value->FindString("id");
      TabEntry entry;
      entry.id = id ? EntryId::FromString(*id) : EntryId();
      if (!entry.id.is_valid()) {
        continue;
      }
      const std::string* url = value->FindString("url");
      entry.url = url ? GURL(*url) : GURL();
      if (!entry.url.is_valid()) {
        continue;
      }
      entry.kind = KindFromString(value->FindString("kind"));
      const std::string* space_id = value->FindString("space_id");
      entry.space_id = space_id ? SpaceId::FromString(*space_id) : SpaceId();
      if (!space_ids.contains(entry.space_id)) {
        entry.space_id = default_space;
      }
      // An entry pointing at a folder that is gone belongs at the top level,
      // not nowhere.
      if (const std::string* folder_id = value->FindString("folder_id")) {
        const FolderId parsed = FolderId::FromString(*folder_id);
        if (folder_ids.contains(parsed)) {
          entry.folder_id = parsed;
        }
      }
      entry.position = value->FindInt("position").value_or(0);
      if (const std::string* title = value->FindString("custom_title")) {
        entry.custom_title = base::UTF8ToUTF16(*title);
      }
      if (const std::string* title = value->FindString("last_title")) {
        entry.last_title = base::UTF8ToUTF16(*title);
      }
      const double created =
          value->FindDouble("created_at").value_or(0.0);
      entry.created_at = base::Time::FromDeltaSinceWindowsEpoch(
          base::Microseconds(static_cast<int64_t>(created)));
      entries.push_back(std::move(entry));
    }
  }

  model->ReplaceAll(std::move(spaces), std::move(folders), std::move(entries));
  return true;
}

}  // namespace arcium
```

Add to `arcium/browser/model/BUILD.gn` `sources`, keeping it alphabetical:

```gn
    "model_serializer.cc",
    "model_serializer.h",
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `scripts/build dev arcium_unittests && /Volumes/Texternal/chromium/src/out/dev/arcium_unittests --gtest_filter=ModelSerializerTest.*`
Expected: PASS, 7 tests.

- [ ] **Step 5: Commit**

```bash
git add arcium/browser/model arcium/test
git commit -m "$(cat <<'EOF'
Stage 2: JSON serialisation for the model

A version field that refuses a newer file outright, because half-reading
a schema from a later Arcium would silently drop whatever it did not
understand and then write the loss back to disk.

Individual rows are treated as recoverable instead: a malformed entry is
dropped and an entry naming a folder that no longer exists lands at the
top level, since losing one row beats refusing to start.

Kinds and timeouts go on the wire as words rather than integers so the
file stays readable in a bug report.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 3: ModelStore — debounced atomic persistence

Writing the model to disk without ever blocking the UI thread, following `components/bookmarks/browser/bookmark_storage.h`, which is the precedent the master spec names.

**Files:**
- Create: `arcium/browser/model_store.h`
- Create: `arcium/browser/model_store.cc`
- Create: `arcium/browser/BUILD.gn`
- Modify: `arcium/test/BUILD.gn`
- Test: `arcium/test/model_store_unittest.cc`

**Interfaces:**
- Consumes: `arcium::ArciumModel`, `arcium::SerializeModel`, `arcium::DeserializeModel`, `arcium::kModelSchemaVersion`.
- Produces: `arcium::ModelStore(ArciumModel* model, const base::FilePath& path)`, `void ModelStore::Load(base::OnceClosure done)`, `void ModelStore::SaveNowForTesting()`, `base::Time ModelStore::last_save_time() const`, and `constexpr base::TimeDelta ModelStore::kSaveDelay`.

- [ ] **Step 1: Write the failing test**

Create `arcium/test/model_store_unittest.cc`:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/model_store.h"

#include "arcium/browser/model/arcium_model.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/run_loop.h"
#include "base/test/task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace arcium {
namespace {

class ModelStoreTest : public testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(dir_.CreateUniqueTempDir()); }

  base::FilePath path() const { return dir_.GetPath().AppendASCII("model.json"); }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  base::ScopedTempDir dir_;
};

TEST_F(ModelStoreTest, LoadingAMissingFileLeavesAnEmptyUsableModel) {
  ArciumModel model;
  ModelStore store(&model, path());
  base::RunLoop loop;
  store.Load(loop.QuitClosure());
  loop.Run();
  EXPECT_TRUE(model.entries().empty());
  EXPECT_EQ(1u, model.spaces().size());
}

TEST_F(ModelStoreTest, AMutationIsWrittenAfterTheSaveDelay) {
  ArciumModel model;
  ModelStore store(&model, path());
  model.AddEntry(EntryKind::kPinned, GURL("https://a.example/"), u"A");

  // Nothing on disk yet: the write is debounced, not synchronous.
  EXPECT_FALSE(base::PathExists(path()));

  task_environment_.FastForwardBy(ModelStore::kSaveDelay);
  task_environment_.RunUntilIdle();
  EXPECT_TRUE(base::PathExists(path()));
}

TEST_F(ModelStoreTest, ABurstOfMutationsWritesOnce) {
  ArciumModel model;
  ModelStore store(&model, path());
  for (int i = 0; i < 10; ++i) {
    model.AddEntry(EntryKind::kPinned,
                   GURL("https://a.example/" + base::NumberToString(i)), u"A");
  }
  EXPECT_EQ(1, store.scheduled_save_count_for_testing());

  task_environment_.FastForwardBy(ModelStore::kSaveDelay);
  task_environment_.RunUntilIdle();
  EXPECT_EQ(1, store.completed_save_count_for_testing());
}

TEST_F(ModelStoreTest, WhatWasSavedComesBack) {
  {
    ArciumModel model;
    ModelStore store(&model, path());
    const EntryId id = model.AddEntry(EntryKind::kPinned,
                                      GURL("https://a.example/"), u"A");
    model.SetCustomTitle(id, u"Renamed");
    task_environment_.FastForwardBy(ModelStore::kSaveDelay);
    task_environment_.RunUntilIdle();
  }

  ArciumModel restored;
  ModelStore store(&restored, path());
  base::RunLoop loop;
  store.Load(loop.QuitClosure());
  loop.Run();
  ASSERT_EQ(1u, restored.entries().size());
  EXPECT_EQ(u"Renamed", restored.entries()[0].custom_title);
}

TEST_F(ModelStoreTest, ACorruptFileLeavesAUsableModel) {
  ASSERT_TRUE(base::WriteFile(path(), "{ this is not json"));
  ArciumModel model;
  ModelStore store(&model, path());
  base::RunLoop loop;
  store.Load(loop.QuitClosure());
  loop.Run();
  EXPECT_TRUE(model.entries().empty());
  EXPECT_EQ(1u, model.spaces().size());
}

TEST_F(ModelStoreTest, LoadRecordsTheSaveTimeAsTheIdleFloor) {
  ArciumModel model;
  ModelStore store(&model, path());
  model.AddEntry(EntryKind::kPinned, GURL("https://a.example/"), u"A");
  task_environment_.FastForwardBy(ModelStore::kSaveDelay);
  task_environment_.RunUntilIdle();
  EXPECT_FALSE(store.last_save_time().is_null());
}

}  // namespace
}  // namespace arcium
```

- [ ] **Step 2: Run the test to verify it fails**

Add `"model_store_unittest.cc",` to `sources` and `"//arcium/browser",` to `deps` in `arcium/test/BUILD.gn`.

Run: `scripts/build dev arcium_unittests`
Expected: FAIL — `arcium/browser/model_store.h` does not exist.

- [ ] **Step 3: Write the implementation**

`arcium/browser/model_store.h`:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_MODEL_STORE_H_
#define ARCIUM_BROWSER_MODEL_STORE_H_

#include "arcium/browser/model/arcium_model.h"
#include "base/files/file_path.h"
#include "base/files/important_file_writer.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"

namespace arcium {

// Persists ArciumModel as one JSON file, atomically and off the UI thread.
// Modelled on components/bookmarks/browser/bookmark_storage.h, which the
// master spec names as the precedent.
class ModelStore : public ArciumModel::Observer {
 public:
  // Matches BookmarkStorage. Long enough that a drag reordering ten rows
  // writes once, short enough that a crash loses almost nothing.
  static constexpr base::TimeDelta kSaveDelay = base::Milliseconds(2500);

  ModelStore(ArciumModel* model, const base::FilePath& path);
  ModelStore(const ModelStore&) = delete;
  ModelStore& operator=(const ModelStore&) = delete;
  ~ModelStore() override;

  // Reads the file on a background sequence and runs `done` on the calling
  // sequence. Never blocks: the sidebar draws live tabs meanwhile.
  void Load(base::OnceClosure done);

  // When the model was last written. Used as the idle floor for tabs restored
  // after a quit, so a browser closed overnight archives yesterday's tabs.
  base::Time last_save_time() const { return last_save_time_; }

  int scheduled_save_count_for_testing() const { return scheduled_saves_; }
  int completed_save_count_for_testing() const { return completed_saves_; }

  // ArciumModel::Observer:
  void OnArciumModelChanged() override;

 private:
  void OnLoaded(base::OnceClosure done,
                std::optional<base::Value::Dict> dict);
  void WriteNow();

  raw_ptr<ArciumModel> model_;
  scoped_refptr<base::SequencedTaskRunner> background_runner_;
  base::ImportantFileWriter writer_;
  base::Time last_save_time_;
  int scheduled_saves_ = 0;
  int completed_saves_ = 0;
  base::WeakPtrFactory<ModelStore> weak_factory_{this};
};

}  // namespace arcium

#endif  // ARCIUM_BROWSER_MODEL_STORE_H_
```

`arcium/browser/model_store.cc`:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/model_store.h"

#include <string>
#include <utility>

#include "arcium/browser/model/model_serializer.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/task/thread_pool.h"

namespace arcium {

namespace {

// Runs on the background sequence. Returns nullopt for a missing, unreadable
// or unparseable file; all three mean "start empty", never "crash".
std::optional<base::Value::Dict> ReadFileOnBackgroundSequence(
    const base::FilePath& path) {
  std::string contents;
  if (!base::ReadFileToString(path, &contents)) {
    return std::nullopt;
  }
  return base::JSONReader::ReadDict(contents);
}

}  // namespace

ModelStore::ModelStore(ArciumModel* model, const base::FilePath& path)
    : model_(model),
      background_runner_(base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
           base::TaskShutdownBehavior::BLOCK_SHUTDOWN})),
      writer_(path, background_runner_, kSaveDelay) {
  model_->AddObserver(this);
}

ModelStore::~ModelStore() {
  model_->RemoveObserver(this);
  // A pending save must not be lost at shutdown: BLOCK_SHUTDOWN on the runner
  // plus this flush is what makes a quit right after a drag durable.
  if (writer_.HasPendingWrite()) {
    writer_.DoScheduledWrite();
  }
}

void ModelStore::Load(base::OnceClosure done) {
  background_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&ReadFileOnBackgroundSequence, writer_.path()),
      base::BindOnce(&ModelStore::OnLoaded, weak_factory_.GetWeakPtr(),
                     std::move(done)));
}

void ModelStore::OnLoaded(base::OnceClosure done,
                          std::optional<base::Value::Dict> dict) {
  if (dict) {
    // A false return means the file is unusable as a whole. The model is left
    // as constructed — empty and valid — rather than partly filled.
    DeserializeModel(*dict, model_);
  }
  std::move(done).Run();
}

void ModelStore::OnArciumModelChanged() {
  if (!writer_.HasPendingWrite()) {
    ++scheduled_saves_;
  }
  writer_.ScheduleWriteWithBackgroundDataSerializer(this);
  WriteNow();
}

void ModelStore::WriteNow() {
  // Serialisation happens on the calling sequence but the write does not:
  // ImportantFileWriter hands the string to its background runner.
  std::optional<std::string> json = base::WriteJson(SerializeModel(*model_));
  if (!json) {
    return;
  }
  writer_.ScheduleWrite(this);
  last_save_time_ = base::Time::Now();
  ++completed_saves_;
}

}  // namespace arcium
```

Note for the implementer: `ImportantFileWriter` has two scheduling styles — `ScheduleWrite(DataSerializer*)` serialises on the calling sequence, and `ScheduleWriteWithBackgroundDataSerializer(BackgroundDataSerializer*)` serialises on the background sequence. Use the background one and implement `GetSerializedDataProducerForBackgroundSequence()`, matching `BookmarkStorage`. Read `base/files/important_file_writer.h` and `components/bookmarks/browser/bookmark_storage.cc` before writing this file, and shape `ModelStore` to whichever interface those two actually present in 152; the sketch above names the intent, and the tests define the contract that must hold. Do not serialise on the UI thread in the final version — take a snapshot of the model's data and serialise it on the background sequence.

`arcium/browser/BUILD.gn`:

```gn
# Browser-process services for the Arcium model: persistence, binding to live
# tabs, archiving and search. Depends on the model but not on Views.
source_set("browser") {
  sources = [
    "model_store.cc",
    "model_store.h",
  ]
  deps = [
    "//arcium/browser/model",
    "//base",
  ]
}
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `scripts/build dev arcium_unittests && /Volumes/Texternal/chromium/src/out/dev/arcium_unittests --gtest_filter=ModelStoreTest.*`
Expected: PASS, 6 tests.

- [ ] **Step 5: Commit**

```bash
git add arcium/browser arcium/test
git commit -m "$(cat <<'EOF'
Stage 2: debounced atomic persistence for the model

Follows BookmarkStorage, the precedent the master spec names, down to
its 2500 ms delay: long enough that a drag reordering ten rows writes
once, short enough that a crash loses almost nothing.

The destructor flushes a pending write and the sequence blocks shutdown,
because quitting right after a drag is exactly when the save is pending.

A missing, unreadable or unparseable file all mean start empty. None of
them may stop a window opening.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 4: ArchiveStore — the SQLite archive

Archived tabs grow without bound, so they go in SQLite rather than the JSON file.

**Files:**
- Create: `arcium/browser/archive_store.h`
- Create: `arcium/browser/archive_store.cc`
- Create: `patches/0110-sql-archive-tag.patch`
- Modify: `arcium/browser/BUILD.gn`
- Modify: `arcium/test/BUILD.gn`
- Test: `arcium/test/archive_store_unittest.cc`

**Interfaces:**
- Consumes: `arcium::SpaceId`.
- Produces: `arcium::ArchivedTab { GURL url; std::u16string title; SpaceId space_id; base::Time archived_at; }` and `arcium::ArchiveStore` with `Open(const base::FilePath&)`, `Add(const ArchivedTab&)`, `std::vector<ArchivedTab> ListRecent(SpaceId, int limit)`, `std::vector<ArchivedTab> Search(const std::u16string& query, int limit)`, `void Remove(const GURL&, base::Time)`.

- [ ] **Step 1: Write the failing test**

Create `arcium/test/archive_store_unittest.cc`:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/archive_store.h"

#include "base/files/scoped_temp_dir.h"
#include "base/test/task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace arcium {
namespace {

class ArchiveStoreTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(dir_.CreateUniqueTempDir());
    ASSERT_TRUE(store_.Open(dir_.GetPath().AppendASCII("archive.db")));
    space_ = SpaceId::Generate();
  }

  ArchivedTab MakeTab(const std::string& url,
                      const std::u16string& title,
                      base::Time when) {
    ArchivedTab tab;
    tab.url = GURL(url);
    tab.title = title;
    tab.space_id = space_;
    tab.archived_at = when;
    return tab;
  }

  base::test::TaskEnvironment task_environment_;
  base::ScopedTempDir dir_;
  ArchiveStore store_;
  SpaceId space_;
};

TEST_F(ArchiveStoreTest, AddedTabsComeBackNewestFirst) {
  const base::Time now = base::Time::Now();
  store_.Add(MakeTab("https://old.example/", u"Old", now - base::Hours(2)));
  store_.Add(MakeTab("https://new.example/", u"New", now));
  store_.Add(MakeTab("https://mid.example/", u"Mid", now - base::Hours(1)));

  std::vector<ArchivedTab> tabs = store_.ListRecent(space_, 10);
  ASSERT_EQ(3u, tabs.size());
  EXPECT_EQ(GURL("https://new.example/"), tabs[0].url);
  EXPECT_EQ(GURL("https://mid.example/"), tabs[1].url);
  EXPECT_EQ(GURL("https://old.example/"), tabs[2].url);
}

TEST_F(ArchiveStoreTest, ListRecentHonoursTheLimit) {
  const base::Time now = base::Time::Now();
  for (int i = 0; i < 10; ++i) {
    store_.Add(MakeTab("https://example.com/" + base::NumberToString(i), u"T",
                       now - base::Minutes(i)));
  }
  EXPECT_EQ(3u, store_.ListRecent(space_, 3).size());
}

TEST_F(ArchiveStoreTest, ListRecentIsScopedToOneSpace) {
  const SpaceId other = SpaceId::Generate();
  store_.Add(MakeTab("https://mine.example/", u"Mine", base::Time::Now()));
  ArchivedTab theirs =
      MakeTab("https://theirs.example/", u"Theirs", base::Time::Now());
  theirs.space_id = other;
  store_.Add(theirs);

  std::vector<ArchivedTab> tabs = store_.ListRecent(space_, 10);
  ASSERT_EQ(1u, tabs.size());
  EXPECT_EQ(GURL("https://mine.example/"), tabs[0].url);
}

TEST_F(ArchiveStoreTest, SearchMatchesTitleAndUrlCaseInsensitively) {
  const base::Time now = base::Time::Now();
  store_.Add(MakeTab("https://github.com/tigranhov/arcium", u"Arcium repo",
                     now));
  store_.Add(MakeTab("https://news.ycombinator.com/", u"Hacker News", now));

  EXPECT_EQ(1u, store_.Search(u"ARCIUM", 10).size());
  EXPECT_EQ(1u, store_.Search(u"github", 10).size());
  EXPECT_EQ(1u, store_.Search(u"hacker", 10).size());
  EXPECT_EQ(0u, store_.Search(u"nothing here", 10).size());
}

TEST_F(ArchiveStoreTest, RemoveDropsOneRow) {
  const base::Time now = base::Time::Now();
  store_.Add(MakeTab("https://a.example/", u"A", now));
  store_.Remove(GURL("https://a.example/"), now);
  EXPECT_TRUE(store_.ListRecent(space_, 10).empty());
}

TEST_F(ArchiveStoreTest, ReopeningTheDatabaseKeepsItsRows) {
  store_.Add(MakeTab("https://a.example/", u"A", base::Time::Now()));
  const base::FilePath path = dir_.GetPath().AppendASCII("archive.db");

  ArchiveStore reopened;
  ASSERT_TRUE(reopened.Open(path));
  EXPECT_EQ(1u, reopened.ListRecent(space_, 10).size());
}

TEST_F(ArchiveStoreTest, OpeningACorruptFileStartsAFreshDatabase) {
  const base::FilePath path = dir_.GetPath().AppendASCII("corrupt.db");
  ASSERT_TRUE(base::WriteFile(path, "this is not a sqlite database"));
  ArchiveStore store;
  EXPECT_TRUE(store.Open(path));
  store.Add(MakeTab("https://a.example/", u"A", base::Time::Now()));
  EXPECT_EQ(1u, store.ListRecent(space_, 10).size());
}

}  // namespace
}  // namespace arcium
```

- [ ] **Step 2: Run the test to verify it fails**

Add `"archive_store_unittest.cc",` to `sources` in `arcium/test/BUILD.gn`.

Run: `scripts/build dev arcium_unittests`
Expected: FAIL — `arcium/browser/archive_store.h` does not exist.

- [ ] **Step 3: Register the database tag**

`sql::Database` validates its tag at compile time against an allowlist generated from `tools/metrics/histograms/metadata/sql/histograms.xml` (see `sql/BUILD.gn`, target `sql_name_variants`). An unregistered tag is a compile error, so this comes before the implementation.

Add a `<variant name="ArciumArchive" summary="Arcium tab archive"/>` entry to the `DatabaseTag` variants list in that file, in alphabetical position, then capture it:

```bash
cd /Volumes/Texternal/chromium/src
git diff -- tools/metrics/histograms/metadata/sql/histograms.xml > \
  /Volumes/Texternal/repositories/arcium/patches/0110-sql-archive-tag.patch
```

Prepend this header to the patch file, above the first `diff --git` line:

```
Seam: tools/metrics/histograms/metadata/sql/histograms.xml, DatabaseTag variants.

Why: sql::Database validates its Tag at compile time against an allowlist
generated from this file (sql/BUILD.gn, target sql_name_variants). A database
with an unregistered tag does not compile. Registers Arcium's archive database.

Delegates to: nothing. This is a registration table Chromium keys by name, so
the patch carries no call into arcium/.
```

Verify the series still applies cleanly: `scripts/sync`.

- [ ] **Step 4: Write the implementation**

`arcium/browser/archive_store.h`:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_ARCHIVE_STORE_H_
#define ARCIUM_BROWSER_ARCHIVE_STORE_H_

#include <string>
#include <vector>

#include "arcium/browser/model/entry_id.h"
#include "base/files/file_path.h"
#include "base/time/time.h"
#include "sql/database.h"
#include "url/gurl.h"

namespace arcium {

struct ArchivedTab {
  GURL url;
  std::u16string title;
  SpaceId space_id;
  base::Time archived_at;
};

// The archive grows without bound, so it is SQLite rather than part of the
// JSON model file. Every method blocks; the owner runs it on a background
// sequence and never calls it from the UI thread.
class ArchiveStore {
 public:
  ArchiveStore();
  ArchiveStore(const ArchiveStore&) = delete;
  ArchiveStore& operator=(const ArchiveStore&) = delete;
  ~ArchiveStore();

  // Creates the file and schema if absent. A file that is not a usable
  // database is razed and recreated: an unreadable archive must not stop the
  // browser, and there is nothing in it worth a recovery path.
  bool Open(const base::FilePath& path);

  void Add(const ArchivedTab& tab);
  std::vector<ArchivedTab> ListRecent(SpaceId space_id, int limit);
  std::vector<ArchivedTab> Search(const std::u16string& query, int limit);
  void Remove(const GURL& url, base::Time archived_at);

 private:
  bool InitSchema();

  sql::Database db_;
};

}  // namespace arcium

#endif  // ARCIUM_BROWSER_ARCHIVE_STORE_H_
```

`arcium/browser/archive_store.cc` — the shape, with the schema and the two queries that matter:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/archive_store.h"

#include "base/files/file_util.h"
#include "base/strings/utf_string_conversions.h"
#include "sql/statement.h"
#include "sql/transaction.h"

namespace arcium {

namespace {

// Registered in tools/metrics/histograms/metadata/sql/histograms.xml by
// patch 0110; sql::Database rejects an unregistered tag at compile time.
constexpr char kDatabaseTag[] = "ArciumArchive";

constexpr int kCurrentVersion = 1;

}  // namespace

ArchiveStore::ArchiveStore() : db_(sql::Database::Tag(kDatabaseTag)) {}

ArchiveStore::~ArchiveStore() = default;

bool ArchiveStore::Open(const base::FilePath& path) {
  if (!db_.Open(path) || !InitSchema()) {
    // Nothing in the archive is worth a recovery path, and an unreadable file
    // must not stop the browser opening.
    db_.Close();
    base::DeleteFile(path);
    return db_.Open(path) && InitSchema();
  }
  return true;
}

bool ArchiveStore::InitSchema() {
  static constexpr char kCreateTable[] =
      "CREATE TABLE IF NOT EXISTS archived_tabs("
      "  url TEXT NOT NULL,"
      "  title TEXT NOT NULL,"
      "  space_id TEXT NOT NULL,"
      "  archived_at INTEGER NOT NULL,"
      "  PRIMARY KEY (url, archived_at))";
  // Both indexes serve a query the UI actually makes: the archive list is
  // by space and recency, search is by recency across spaces.
  static constexpr char kCreateSpaceIndex[] =
      "CREATE INDEX IF NOT EXISTS idx_space_time"
      "  ON archived_tabs(space_id, archived_at DESC)";
  static constexpr char kCreateTimeIndex[] =
      "CREATE INDEX IF NOT EXISTS idx_time"
      "  ON archived_tabs(archived_at DESC)";
  static constexpr char kCreateMeta[] =
      "CREATE TABLE IF NOT EXISTS meta(version INTEGER NOT NULL)";

  sql::Transaction transaction(&db_);
  if (!transaction.Begin()) {
    return false;
  }
  if (!db_.Execute(kCreateTable) || !db_.Execute(kCreateSpaceIndex) ||
      !db_.Execute(kCreateTimeIndex) || !db_.Execute(kCreateMeta)) {
    return false;
  }
  // Version row written on creation so a future migration has a floor to
  // read. Stage 2 only ever writes version 1.
  sql::Statement version(
      db_.GetUniqueStatement("INSERT INTO meta(version) SELECT ? WHERE NOT "
                             "EXISTS (SELECT 1 FROM meta)"));
  version.BindInt(0, kCurrentVersion);
  if (!version.Run()) {
    return false;
  }
  return transaction.Commit();
}

void ArchiveStore::Add(const ArchivedTab& tab) {
  sql::Statement statement(db_.GetUniqueStatement(
      "INSERT OR REPLACE INTO archived_tabs(url, title, space_id, archived_at)"
      " VALUES (?, ?, ?, ?)"));
  statement.BindString(0, tab.url.spec());
  statement.BindString(1, base::UTF16ToUTF8(tab.title));
  statement.BindString(2, tab.space_id.value());
  statement.BindTime(3, tab.archived_at);
  statement.Run();
}

std::vector<ArchivedTab> ArchiveStore::ListRecent(SpaceId space_id, int limit) {
  sql::Statement statement(db_.GetUniqueStatement(
      "SELECT url, title, space_id, archived_at FROM archived_tabs"
      " WHERE space_id = ? ORDER BY archived_at DESC LIMIT ?"));
  statement.BindString(0, space_id.value());
  statement.BindInt(1, limit);

  std::vector<ArchivedTab> tabs;
  while (statement.Step()) {
    ArchivedTab tab;
    tab.url = GURL(statement.ColumnString(0));
    tab.title = base::UTF8ToUTF16(statement.ColumnString(1));
    tab.space_id = SpaceId::FromString(statement.ColumnString(2));
    tab.archived_at = statement.ColumnTime(3);
    tabs.push_back(std::move(tab));
  }
  return tabs;
}

std::vector<ArchivedTab> ArchiveStore::Search(const std::u16string& query,
                                              int limit) {
  // LIKE with a lowercased needle rather than FTS: the archive is small
  // enough that a scan is cheap, and FTS would be a schema to migrate later.
  const std::string needle =
      "%" + base::ToLowerASCII(base::UTF16ToUTF8(query)) + "%";
  sql::Statement statement(db_.GetUniqueStatement(
      "SELECT url, title, space_id, archived_at FROM archived_tabs"
      " WHERE lower(title) LIKE ? OR lower(url) LIKE ?"
      " ORDER BY archived_at DESC LIMIT ?"));
  statement.BindString(0, needle);
  statement.BindString(1, needle);
  statement.BindInt(2, limit);

  std::vector<ArchivedTab> tabs;
  while (statement.Step()) {
    ArchivedTab tab;
    tab.url = GURL(statement.ColumnString(0));
    tab.title = base::UTF8ToUTF16(statement.ColumnString(1));
    tab.space_id = SpaceId::FromString(statement.ColumnString(2));
    tab.archived_at = statement.ColumnTime(3);
    tabs.push_back(std::move(tab));
  }
  return tabs;
}

void ArchiveStore::Remove(const GURL& url, base::Time archived_at) {
  sql::Statement statement(db_.GetUniqueStatement(
      "DELETE FROM archived_tabs WHERE url = ? AND archived_at = ?"));
  statement.BindString(0, url.spec());
  statement.BindTime(1, archived_at);
  statement.Run();
}

}  // namespace arcium
```

Add to `arcium/browser/BUILD.gn` `sources`: `"archive_store.cc",` and `"archive_store.h",`; add `"//sql",` and `"//url",` to `deps`.

Note for the implementer: `sql::Statement::BindTime` and `ColumnTime` exist in 152 but check the exact spelling in `sql/statement.h` before writing; if they are absent, store `archived_at` as microseconds since the Windows epoch with `BindInt64`/`ColumnInt64` and convert with `base::Time::FromDeltaSinceWindowsEpoch`. The tests define the contract either way.

- [ ] **Step 5: Run the tests to verify they pass**

Run: `scripts/build dev arcium_unittests && /Volumes/Texternal/chromium/src/out/dev/arcium_unittests --gtest_filter=ArchiveStoreTest.*`
Expected: PASS, 7 tests.

- [ ] **Step 6: Commit**

```bash
git add arcium/browser arcium/test patches/0110-sql-archive-tag.patch
git commit -m "$(cat <<'EOF'
Stage 2: the SQLite tab archive

Archived tabs grow without bound, so they do not belong in the JSON
model file. Two indexes, each serving a query the UI actually makes:
list by space and recency, search by recency across spaces.

Search is LIKE over a lowercased needle rather than FTS. The archive is
small enough that a scan is cheap, and FTS would be a second schema to
migrate later for no gain at this size.

A file that will not open is razed and recreated. Nothing in an archive
justifies a recovery path, and an unreadable one must not stop a window.

Patch 0110 registers the database tag: sql::Database validates it at
compile time against a list generated from the histograms metadata.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 5: TabBinding — entries to live tabs

The join between a persistent entry and the tab currently representing it.

**Files:**
- Create: `arcium/browser/tab_binding.h`
- Create: `arcium/browser/tab_binding.cc`
- Modify: `arcium/browser/BUILD.gn`
- Modify: `arcium/test/BUILD.gn`
- Test: `arcium/test/tab_binding_unittest.cc`

**Interfaces:**
- Consumes: `arcium::EntryId`.
- Produces: `arcium::TabBinding` with `void Bind(EntryId, tabs::TabHandle)`, `void UnbindEntry(EntryId)`, `void UnbindTab(tabs::TabHandle)`, `std::optional<tabs::TabHandle> TabForEntry(EntryId) const`, `std::optional<EntryId> EntryForTab(tabs::TabHandle) const`, `bool IsBound(tabs::TabHandle) const`.

- [ ] **Step 1: Write the failing test**

Create `arcium/test/tab_binding_unittest.cc`. Use `BrowserWithTestWindowTest` as `sidebar_tab_model_unittest.cc` does, since real `tabs::TabHandle` values need real tabs:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/tab_binding.h"

#include "arcium/browser/model/entry_id.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "components/tabs/public/tab_interface.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace arcium {
namespace {

class TabBindingTest : public BrowserWithTestWindowTest {
 protected:
  tabs::TabHandle HandleAt(int index) {
    return browser()->tab_strip_model()->GetTabAtIndex(index)->GetHandle();
  }
};

TEST_F(TabBindingTest, BindingIsVisibleFromBothSides) {
  AddTab(browser(), GURL("https://a.example/"));
  const EntryId id = EntryId::Generate();
  TabBinding binding;
  binding.Bind(id, HandleAt(0));

  ASSERT_TRUE(binding.TabForEntry(id).has_value());
  EXPECT_EQ(HandleAt(0), *binding.TabForEntry(id));
  ASSERT_TRUE(binding.EntryForTab(HandleAt(0)).has_value());
  EXPECT_EQ(id, *binding.EntryForTab(HandleAt(0)));
  EXPECT_TRUE(binding.IsBound(HandleAt(0)));
}

TEST_F(TabBindingTest, AnUnboundEntryHasNoTab) {
  TabBinding binding;
  EXPECT_FALSE(binding.TabForEntry(EntryId::Generate()).has_value());
}

TEST_F(TabBindingTest, UnbindEntryClearsBothDirections) {
  AddTab(browser(), GURL("https://a.example/"));
  const EntryId id = EntryId::Generate();
  TabBinding binding;
  binding.Bind(id, HandleAt(0));
  binding.UnbindEntry(id);
  EXPECT_FALSE(binding.TabForEntry(id).has_value());
  EXPECT_FALSE(binding.EntryForTab(HandleAt(0)).has_value());
}

TEST_F(TabBindingTest, UnbindTabClearsBothDirections) {
  AddTab(browser(), GURL("https://a.example/"));
  const EntryId id = EntryId::Generate();
  TabBinding binding;
  binding.Bind(id, HandleAt(0));
  binding.UnbindTab(HandleAt(0));
  EXPECT_FALSE(binding.TabForEntry(id).has_value());
}

TEST_F(TabBindingTest, RebindingAnEntryReleasesItsOldTab) {
  AddTab(browser(), GURL("https://a.example/"));
  AddTab(browser(), GURL("https://b.example/"));
  const EntryId id = EntryId::Generate();
  TabBinding binding;
  binding.Bind(id, HandleAt(0));
  binding.Bind(id, HandleAt(1));
  EXPECT_EQ(HandleAt(1), *binding.TabForEntry(id));
  EXPECT_FALSE(binding.EntryForTab(HandleAt(0)).has_value());
}

TEST_F(TabBindingTest, BindingATabAlreadyBoundReleasesTheOldEntry) {
  AddTab(browser(), GURL("https://a.example/"));
  const EntryId first = EntryId::Generate();
  const EntryId second = EntryId::Generate();
  TabBinding binding;
  binding.Bind(first, HandleAt(0));
  binding.Bind(second, HandleAt(0));
  EXPECT_FALSE(binding.TabForEntry(first).has_value());
  EXPECT_EQ(HandleAt(0), *binding.TabForEntry(second));
}

TEST_F(TabBindingTest, AClosedTabsHandleResolvesToNothing) {
  AddTab(browser(), GURL("https://a.example/"));
  const EntryId id = EntryId::Generate();
  const tabs::TabHandle handle = HandleAt(0);
  TabBinding binding;
  binding.Bind(id, handle);
  browser()->tab_strip_model()->CloseWebContentsAt(0, 0);
  // The map may still hold the handle; what matters is that it no longer
  // resolves to a tab, so callers see the entry as cold.
  const std::optional<tabs::TabHandle> bound = binding.TabForEntry(id);
  EXPECT_TRUE(!bound.has_value() || bound->Get() == nullptr);
}

}  // namespace
}  // namespace arcium
```

- [ ] **Step 2: Run the test to verify it fails**

Add `"tab_binding_unittest.cc",` to `sources` in `arcium/test/BUILD.gn`.

Run: `scripts/build dev arcium_unittests`
Expected: FAIL — `arcium/browser/tab_binding.h` does not exist.

- [ ] **Step 3: Write the implementation**

`arcium/browser/tab_binding.h`:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_TAB_BINDING_H_
#define ARCIUM_BROWSER_TAB_BINDING_H_

#include <map>
#include <optional>

#include "arcium/browser/model/entry_id.h"
#include "components/tabs/public/tab_interface.h"

namespace arcium {

// Joins a persistent entry to the tab currently representing it. A tab is
// held as tabs::TabHandle, which is already weak, so a closed tab leaves no
// dangling pointer and the entry simply reads as cold.
//
// Both directions are kept because both are asked: the sidebar asks "does
// this entry have a tab", and a tab-strip callback asks "which entry owns
// this tab". The invariant is one-to-one, enforced in Bind().
class TabBinding {
 public:
  TabBinding();
  TabBinding(const TabBinding&) = delete;
  TabBinding& operator=(const TabBinding&) = delete;
  ~TabBinding();

  // Releases whatever either side was previously bound to, so the map cannot
  // grow a second edge for the same entry or the same tab.
  void Bind(EntryId id, tabs::TabHandle handle);
  void UnbindEntry(EntryId id);
  void UnbindTab(tabs::TabHandle handle);

  std::optional<tabs::TabHandle> TabForEntry(EntryId id) const;
  std::optional<EntryId> EntryForTab(tabs::TabHandle handle) const;
  bool IsBound(tabs::TabHandle handle) const;

 private:
  std::map<EntryId, tabs::TabHandle> entry_to_tab_;
  std::map<tabs::TabHandle, EntryId> tab_to_entry_;
};

}  // namespace arcium

#endif  // ARCIUM_BROWSER_TAB_BINDING_H_
```

Implement `tab_binding.cc` so that `Bind` first erases any existing edge from either side, then inserts into both maps. `UnbindEntry` looks up the tab, erases both. `UnbindTab` is the mirror. The lookups are plain `find` on the maps. If `tabs::TabHandle` lacks `operator<`, key `tab_to_entry_` on `handle.raw_value()` instead and check `components/tabs/public/supports_handles.h` for the accessor's real name.

Add `"tab_binding.cc",` and `"tab_binding.h",` to `arcium/browser/BUILD.gn` `sources`, and `"//components/tabs:public",` to its `deps`.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `scripts/build dev arcium_unittests && /Volumes/Texternal/chromium/src/out/dev/arcium_unittests --gtest_filter=TabBindingTest.*`
Expected: PASS, 7 tests.

- [ ] **Step 5: Commit**

```bash
git add arcium/browser arcium/test
git commit -m "$(cat <<'EOF'
Stage 2: bind entries to the tabs representing them

Both directions are stored because both are asked: the sidebar asks
whether an entry has a tab, and a tab-strip callback asks which entry
owns a tab. Bind() releases either side's old edge first, so the
one-to-one invariant cannot be broken by a caller getting it wrong.

Tabs are held as TabHandle, which is already weak. A closed tab leaves
no dangling pointer and its entry simply reads as cold.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 6: Merged rows — entries and live tabs in one list

`SidebarTabModel` stops deriving rows from the strip and starts merging. After this task the sidebar shows cold entries and clicking one opens its URL, so the feature is usable before drag and drop exists.

**Files:**
- Modify: `arcium/ui/sidebar/sidebar_model.h`
- Modify: `arcium/ui/browser/sidebar_tab_model.h`
- Modify: `arcium/ui/browser/sidebar_tab_model.cc`
- Modify: `arcium/ui/browser/browser_sidebar_controller.h/.cc`
- Modify: `arcium/ui/playground/fake_sidebar_model.h/.cc`
- Modify: `arcium/test/sidebar_tab_model_unittest.cc`

**Interfaces:**
- Consumes: `arcium::ArciumModel`, `arcium::TabBinding`, `arcium::ModelStore`.
- Produces: `SidebarRow` gains `EntryId entry_id` and `bool is_cold`; `SidebarModel` gains `void AddToFavorites(int tab_index)`, `void PinTab(int tab_index)`, `void UnpinEntry(EntryId)`, `void ActivateEntry(EntryId)`, `void CloseEntryTab(EntryId)`, `void SetEntryTitle(EntryId, const std::u16string&)`, `void ReturnToPinnedUrl(EntryId)`; `SidebarTabModel(TabStripModel*, ArciumModel*, TabBinding*)`.

- [ ] **Step 1: Extend SidebarRow and SidebarModel**

In `arcium/ui/sidebar/sidebar_model.h`, add to `SidebarRow`:

```cpp
  // Set when the row is backed by a persistent entry. Invalid for a Today
  // tab, which has no entry.
  EntryId entry_id;
  // An entry with no live tab. It draws from last_title and the entry's URL,
  // and clicking it opens that URL.
  bool is_cold = false;
  // A warm pinned entry whose tab has navigated away from the pinned URL.
  bool can_return_to_pinned_url = false;
  // Set on rows inside a folder, so TabListView can indent and hide them.
  std::optional<FolderId> folder_id;
```

and add the commands listed under Interfaces to the `SidebarModel` interface, each pure virtual. Include `arcium/browser/model/entry_id.h`.

`arcium/ui/sidebar` must not depend on `//chrome/browser`, per its BUILD.gn comment. `//arcium/browser/model` has no Chromium UI dependencies, so add it to `deps` in `arcium/ui/sidebar/BUILD.gn`; that keeps the playground small.

- [ ] **Step 2: Write the failing test**

Add to `arcium/test/sidebar_tab_model_unittest.cc`, and update the existing fixture to construct the model with its new arguments:

```cpp
class SidebarTabModelTest : public BrowserWithTestWindowTest {
 protected:
  TabStripModel* strip() { return browser()->tab_strip_model(); }

  std::unique_ptr<SidebarTabModel> MakeModel() {
    return std::make_unique<SidebarTabModel>(strip(), &arcium_model_,
                                             &binding_);
  }

  ArciumModel arcium_model_;
  TabBinding binding_;
};

TEST_F(SidebarTabModelTest, AColdEntryAppearsWithNoTab) {
  arcium_model_.AddEntry(EntryKind::kPinned, GURL("https://cold.example/"),
                         u"Cold");
  std::unique_ptr<SidebarTabModel> model = MakeModel();

  std::vector<SidebarRow> rows = model->rows();
  ASSERT_EQ(1u, rows.size());
  EXPECT_TRUE(rows[0].is_cold);
  EXPECT_EQ(-1, rows[0].tab_index);
  EXPECT_EQ(SidebarSection::kPinned, rows[0].section);
  EXPECT_EQ(u"Cold", rows[0].title);
  EXPECT_EQ(GURL("https://cold.example/"), rows[0].url);
}

TEST_F(SidebarTabModelTest, PinningATabMovesItOutOfToday) {
  AddTab(browser(), GURL("https://a.example/"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  ASSERT_EQ(SidebarSection::kToday, model->rows()[0].section);

  model->PinTab(0);
  std::vector<SidebarRow> rows = model->rows();
  ASSERT_EQ(1u, rows.size());
  EXPECT_EQ(SidebarSection::kPinned, rows[0].section);
  EXPECT_FALSE(rows[0].is_cold);
  EXPECT_TRUE(rows[0].entry_id.is_valid());
  // The tab is still the same tab, now bound to an entry.
  EXPECT_EQ(0, rows[0].tab_index);
  EXPECT_EQ(1u, arcium_model_.entries().size());
}

TEST_F(SidebarTabModelTest, AddToFavoritesMovesATabToTheGrid) {
  AddTab(browser(), GURL("https://a.example/"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  model->AddToFavorites(0);
  ASSERT_EQ(1u, model->rows().size());
  EXPECT_EQ(SidebarSection::kFavorites, model->rows()[0].section);
}

TEST_F(SidebarTabModelTest, ClosingAPinnedEntrysTabLeavesItCold) {
  AddTab(browser(), GURL("https://a.example/"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  model->PinTab(0);
  const EntryId id = model->rows()[0].entry_id;

  strip()->CloseWebContentsAt(0, 0);
  task_environment()->RunUntilIdle();

  std::vector<SidebarRow> rows = model->rows();
  ASSERT_EQ(1u, rows.size());
  EXPECT_EQ(id, rows[0].entry_id);
  EXPECT_TRUE(rows[0].is_cold);
}

TEST_F(SidebarTabModelTest, ActivatingAColdEntryOpensItsUrlAndBindsIt) {
  const EntryId id = arcium_model_.AddEntry(
      EntryKind::kPinned, GURL("https://cold.example/"), u"Cold");
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  ASSERT_TRUE(model->rows()[0].is_cold);

  model->ActivateEntry(id);
  task_environment()->RunUntilIdle();

  EXPECT_EQ(1, strip()->count());
  EXPECT_FALSE(model->rows()[0].is_cold);
  EXPECT_TRUE(binding_.TabForEntry(id).has_value());
}

TEST_F(SidebarTabModelTest, ActivatingAWarmEntryFocusesItRatherThanOpeningAgain) {
  AddTab(browser(), GURL("https://a.example/"));
  AddTab(browser(), GURL("https://b.example/"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  model->PinTab(1);
  const EntryId id = model->rows()[0].entry_id;
  const int count_before = strip()->count();

  model->ActivateEntry(id);
  EXPECT_EQ(count_before, strip()->count());
}

TEST_F(SidebarTabModelTest, UnpinningReturnsTheTabToToday) {
  AddTab(browser(), GURL("https://a.example/"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  model->PinTab(0);
  const EntryId id = model->rows()[0].entry_id;

  model->UnpinEntry(id);
  std::vector<SidebarRow> rows = model->rows();
  ASSERT_EQ(1u, rows.size());
  EXPECT_EQ(SidebarSection::kToday, rows[0].section);
  EXPECT_TRUE(arcium_model_.entries().empty());
}

TEST_F(SidebarTabModelTest, UnpinningAColdEntryJustRemovesIt) {
  const EntryId id = arcium_model_.AddEntry(
      EntryKind::kPinned, GURL("https://cold.example/"), u"Cold");
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  model->UnpinEntry(id);
  EXPECT_TRUE(model->rows().empty());
}

TEST_F(SidebarTabModelTest, NavigatingAwayOffersAReturnToThePinnedUrl) {
  AddTab(browser(), GURL("https://pinned.example/"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  model->PinTab(0);
  EXPECT_FALSE(model->rows()[0].can_return_to_pinned_url);

  NavigateAndCommitActiveTab(GURL("https://elsewhere.example/"));
  task_environment()->RunUntilIdle();
  EXPECT_TRUE(model->rows()[0].can_return_to_pinned_url);
}

TEST_F(SidebarTabModelTest, TodayTabsStillFollowStripOrder) {
  AddTab(browser(), GURL("https://a.example/"));
  AddTab(browser(), GURL("https://b.example/"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();
  std::vector<SidebarRow> rows = model->rows();
  ASSERT_EQ(2u, rows.size());
  EXPECT_EQ(GURL("https://b.example/"), rows[0].url);
  EXPECT_EQ(GURL("https://a.example/"), rows[1].url);
}
```

- [ ] **Step 3: Run the tests to verify they fail**

Run: `scripts/build dev arcium_unittests`
Expected: FAIL — `SidebarTabModel` has no three-argument constructor and no `PinTab`.

- [ ] **Step 4: Rewrite `SidebarTabModel::rows()` as a merge**

The order of a merged list is: favourites, then pinned (entries in `position` order, cold and warm alike), then Today (live tabs in strip order that no entry claims). The rules:

- Walk `ArciumModel::EntriesForKind(space, kFavorite)` and then `kPinned`. For each, ask `TabBinding::TabForEntry`. A resolvable handle gives a warm row: take title, favicon, loading and audio state from the live tab as Stage 1 does, and set `can_return_to_pinned_url` when the tab's visible URL differs from `entry->url` and the entry is pinned. An absent or unresolvable handle gives a cold row: `tab_index = -1`, `is_cold = true`, title from `entry->DisplayTitle()`, URL from `entry->url`, favicon from the default globe.
- Walk the strip. Any tab that `TabBinding::IsBound` skips; the rest are Today rows exactly as Stage 1 built them.
- A warm entry whose page title changes calls `ArciumModel::SetLastTitle`, so a cold row later has something better than a URL to draw.

`ActivateEntry` on a cold entry opens `entry->url` through `chrome::AddSelectedTabWithURL` — the same call `QuickEntryBubble` already uses — then binds the new tab in `TabStripModelObserver::OnTabStripModelChanged`'s insert notification. On a warm entry it calls `ActivateTabAt`.

`PinTab(int tab_index)` and `AddToFavorites(int tab_index)` create an entry from the live tab's URL and title, then bind it. Neither touches Chromium's own pinned state: Arcium's pinned section is its own concept, and Chromium pinning would force the tab to be live, which is exactly what a cold entry must not be.

`UnpinEntry` removes the entry; its tab, if any, falls back into Today because nothing claims it any more.

`ReturnToPinnedUrl(EntryId)` navigates the bound tab to `entry->url` with `content::NavigationController::LoadURL`, rather than opening a new tab.

Keep the burst coalescing from Stage 1 unchanged, and add `ArciumModel::Observer` to the class so a model mutation also schedules one notification.

- [ ] **Step 5: Update the playground fake and the controller**

`FakeSidebarModel` grows the same commands with in-memory behaviour and a couple of cold rows in its seed data, so the playground can host the new states without a browser.

`BrowserSidebarController` owns an `ArciumModel`, a `TabBinding` and a `ModelStore` per profile, and calls `ModelStore::Load` after the sidebar is created. Store them on a `ProfileKeyedServiceFactory` or a `base::SupportsUserData` key on the profile so two windows on one profile share one model; a second window must not get a second copy.

The JSON path is `profile->GetPath().Append(FILE_PATH_LITERAL("Arcium Model"))` and the archive is `Append(FILE_PATH_LITERAL("Arcium Archive"))`, matching Chromium's convention of extension-less data files beside `Bookmarks`.

- [ ] **Step 6: Run the tests to verify they pass**

Run: `scripts/build dev arcium_unittests && /Volumes/Texternal/chromium/src/out/dev/arcium_unittests --gtest_filter=SidebarTabModelTest.*`
Expected: PASS, 14 tests — the 4 from Stage 1 plus the 10 above.

- [ ] **Step 7: Verify it in the real browser**

```bash
scripts/build dev chrome && scripts/run
```

Open two tabs. There is no drag yet, so exercise pin through the row context menu added in Task 8; until then, verify cold rows by seeding the model file directly:

```bash
cat > "$HOME/Library/Application Support/Arcium-dev/Default/Arcium Model" <<'JSON'
{"version":1,
 "spaces":[{"id":"00000000-0000-4000-8000-000000000001","name":"Space","archive_timeout":"12h","position":0}],
 "folders":[],
 "entries":[{"id":"00000000-0000-4000-8000-000000000002","kind":"pinned",
   "space_id":"00000000-0000-4000-8000-000000000001","position":0,
   "url":"https://news.ycombinator.com/","custom_title":"","last_title":"Hacker News","created_at":0}]}
JSON
scripts/run
```

Expected: a pinned row reading "Hacker News" above the divider with no tab open, and clicking it opens the site and turns the row warm. Confirm with `scripts/run --arcium-snapshot=/tmp/cold.png` and read the view dump.

- [ ] **Step 8: Commit**

```bash
git add arcium/ui arcium/test
git commit -m "$(cat <<'EOF'
Stage 2: merge persistent entries with live tabs

Rows stop being a pure function of the tab strip. Favourites and pinned
entries lead, cold or warm, then whatever tabs no entry claims are Today.

Arcium's pinned section deliberately does not use Chromium's own pinned
state: a Chromium pinned tab is always live, which is precisely what a
cold entry must not be.

Clicking a cold entry opens its URL and binds the new tab; closing a
warm entry's tab leaves the entry behind rather than deleting it. That
asymmetry is the whole point of the stage.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 7: Row interactions — rename, revert, folders, context menu

The commands from Task 6 get UI. This is also what makes pin and favourite reachable without drag and drop.

**Files:**
- Modify: `arcium/ui/sidebar/tab_row_view.h/.cc`
- Modify: `arcium/ui/sidebar/tab_list_view.h/.cc`
- Create: `arcium/ui/sidebar/folder_header_view.h/.cc`
- Create: `arcium/ui/sidebar/row_context_menu.h/.cc`
- Modify: `arcium/ui/sidebar/BUILD.gn`
- Modify: `arcium/ui/playground/sidebar_example.cc`

**Interfaces:**
- Consumes: every `SidebarModel` command from Task 6.
- Produces: `arcium::FolderHeaderView`, `arcium::RowContextMenu`; `TabRowView` gains `void BeginRename()` and a `kRevert` button.

- [ ] **Step 1: Inline rename in `TabRowView`**

`BeginRename()` swaps the row's `views::Label` for a `views::Textfield` seeded with the current display title, selects all, and focuses it. Enter commits through `SidebarModel::SetEntryTitle`; Escape and focus loss abandon. A row with no entry — a Today tab — has no rename, because there is nothing to carry the name.

Reuse the pattern in `chrome/browser/ui/views/bookmarks/` for a label that becomes a field in place; do not write a new editing widget.

- [ ] **Step 2: The revert affordance**

When `SidebarRow::can_return_to_pinned_url` is set, `TabRowView` shows a revert button in the same slot the close button uses on hover, with tooltip "Return to pinned URL". Clicking calls `SidebarModel::ReturnToPinnedUrl`. Bind Cmd+Shift+Backspace to the same command on the focused row.

Add `revert.icon` to `arcium/ui/sidebar/icons/` and to the `aggregate_vector_icons` source list, following the existing `close.icon`.

- [ ] **Step 3: Folder headers**

`FolderHeaderView` is a 32 px row: a disclosure triangle, the folder name, and the count of entries inside. Clicking toggles `SidebarModel::SetFolderCollapsed`. Double-click renames in place, by the same mechanism as Step 1.

`TabListView` for the Pinned section lays out folder headers in `Folder::position` order, each followed by its entries when expanded, then the entries with no folder. A collapsed folder contributes only its header.

- [ ] **Step 4: The context menu**

`RowContextMenu` is a `ui::SimpleMenuModel` shown by `views::MenuRunner`, built per row from its section. Include `ui/menus/simple_menu_model.h`, not `ui/base/models/`, which is where Stage 1 found it.

| Row | Items |
|---|---|
| Today | Pin, Add to Favorites, Rename (disabled), Close |
| Pinned | Rename, Return to pinned URL (when applicable), New folder, Move to folder, Unpin, Close tab |
| Favorite | Rename, Remove from Favorites, Close tab |
| Folder header | Rename, Delete folder |

"Delete folder" removes the folder and returns its entries to the top level, which is what `ArciumModel::RemoveFolder` already does; say so in the menu item's tooltip so it does not read as deleting the tabs.

- [ ] **Step 5: Verify in the playground first**

```bash
scripts/playground
```

Iterate on all four states there — cold row, warm row, renaming row, collapsed folder — before touching the browser. This is the workflow Stage 1 established because a Views rebuild is seconds and a Chrome rebuild is minutes.

Capture: `out/dev/arcium_playground --snapshot=docs/screens/stage2/01-folders-and-cold-rows.png`

- [ ] **Step 6: Verify in the browser**

```bash
scripts/build dev chrome && scripts/run
```

By hand: open three tabs; pin one from its context menu; rename it; navigate it away and use the revert affordance; make a folder and move the pinned entry into it; collapse the folder; quit; relaunch. Everything should be where it was. This is acceptance A2.1 in miniature and it is worth doing now rather than at the end.

- [ ] **Step 7: Commit**

```bash
git add arcium/ui docs/screens/stage2
git commit -m "$(cat <<'EOF'
Stage 2: rename, revert and folders in the sidebar

Also the row context menu, which is what makes pin and favourite
reachable before drag and drop lands. Building the commands first and
the drag second means the stage is usable at every point rather than
only at the end.

Renaming is offered only on rows with an entry: a Today tab has nothing
to carry the name. Deleting a folder returns its entries to the top
level and the menu says so, because "delete folder" otherwise reads as
deleting the tabs inside it.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 8: Drag and drop

**Files:**
- Create: `arcium/ui/sidebar/row_drag_data.h/.cc`
- Modify: `arcium/ui/sidebar/tab_row_view.h/.cc`
- Modify: `arcium/ui/sidebar/tab_list_view.h/.cc`
- Modify: `arcium/ui/sidebar/favorites_grid_view.h/.cc`
- Modify: `arcium/ui/sidebar/BUILD.gn`

**Interfaces:**
- Consumes: `SidebarModel` commands from Task 6, `FolderId`.
- Produces: `arcium::RowDragData` with `EntryId entry_id`, `int tab_index`, and the custom `ui::ClipboardFormatType` that carries it.

- [ ] **Step 1: The drag payload**

`RowDragData` carries either an `EntryId` or a tab index, written into `ui::OSExchangeData` under a custom format registered once. Never put a raw pointer in drag data: a drag can outlive the row that started it.

- [ ] **Step 2: Sources**

`TabRowView` implements `views::DragController`: `WriteDragDataForView`, `GetDragOperationsForView` returning `ui::DragDropTypes::DRAG_MOVE`, and `CanStartDragForView` with the usual threshold. `FavoritesGridView`'s tiles do the same.

- [ ] **Step 3: Targets**

Each of `FavoritesGridView`, the Pinned `TabListView`, `FolderHeaderView` and the Today `TabListView` overrides `GetDropFormats`, `AreDropTypesRequired`, `CanDrop`, `OnDragUpdated`, `OnPerformDrop`.

The drop semantics, which are the part worth being exact about:

| From | To | Effect |
|---|---|---|
| Today tab | Favourites | `AddToFavorites(tab_index)` |
| Today tab | Pinned | `PinTab(tab_index)` |
| Today tab | Today | `MoveTab` — a tab-strip move, as in Stage 1 |
| Entry | Favourites | `SetEntryKind(id, kFavorite)` then `ReorderEntry` |
| Entry | Pinned | `SetEntryKind(id, kPinned)` then `ReorderEntry` |
| Entry | Folder header | `SetEntryFolder(id, folder)` |
| Entry | Today | `UnpinEntry(id)`; a cold entry dropped into Today is simply deleted, so guard it behind a confirmation-free undo rather than a dialog |
| Favourite | Favourites | `ReorderEntry` |

`OnDragUpdated` returns the operation and draws an insertion indicator: a 2 px line at the drop index in a list, a highlighted tile gap in the grid, a highlighted header for a folder.

- [ ] **Step 4: Verify in the playground**

```bash
scripts/playground
```

Drag every combination in the table. The playground's fake model implements the same commands, so all of it works without a browser.

- [ ] **Step 5: Verify in the browser and commit**

```bash
scripts/build dev chrome && scripts/run
```

```bash
git add arcium/ui
git commit -m "$(cat <<'EOF'
Stage 2: drag and drop between sections

Drag data carries an entry id or a tab index, never a pointer: a drag
can outlive the row that started it.

Dropping a Today tab into Pinned creates an entry and binds it; dropping
an entry into Today removes the entry and leaves the tab. The asymmetry
follows from entries owning identity and tabs owning liveness.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 9: Auto-archive

**Files:**
- Create: `arcium/browser/archive_service.h/.cc`
- Modify: `arcium/browser/BUILD.gn`
- Modify: `arcium/ui/sidebar/section_divider_view.cc`
- Modify: `arcium/ui/sidebar/space_bar_view.cc`
- Modify: `arcium/ui/sidebar/sidebar_model.h`
- Modify: `arcium/ui/playground/fake_sidebar_model.h/.cc`
- Test: `arcium/test/archive_service_unittest.cc`

**Interfaces:**
- Consumes: `ArciumModel`, `ArchiveStore`, `TabBinding`, `ModelStore::last_save_time()`.
- Produces: `arcium::ArchiveService` with `void OnTabActivated(tabs::TabHandle)`, `void ArchiveAllToday()`, `bool MayArchive(tabs::TabHandle) const`, `std::optional<base::Time> next_expiry_for_testing() const`, `int scheduled_timer_count_for_testing() const`; `SidebarModel` gains `void SetArchiveTimeout(ArchiveTimeout)` and `ArchiveTimeout archive_timeout() const`.

- [ ] **Step 1: Write the failing test**

```cpp
TEST_F(ArchiveServiceTest, ATabIdleBeyondTheTimeoutIsArchivedAndClosed) {
  AddTab(browser(), GURL("https://a.example/"));
  AddTab(browser(), GURL("https://keep.example/"));  // active, never archived
  service_->OnTabActivated(HandleAt(0));

  task_environment()->FastForwardBy(base::Hours(13));
  EXPECT_EQ(1, strip()->count());
  EXPECT_EQ(1u, archive_.ListRecent(model_.default_space_id(), 10).size());
}

TEST_F(ArchiveServiceTest, TheActiveTabIsNeverArchived) {
  AddTab(browser(), GURL("https://a.example/"));
  task_environment()->FastForwardBy(base::Days(30));
  EXPECT_EQ(1, strip()->count());
}

TEST_F(ArchiveServiceTest, ATabPlayingAudioIsNeverArchived) {
  AddTab(browser(), GURL("https://music.example/"));
  AddTab(browser(), GURL("https://active.example/"));
  content::WebContentsTester::For(strip()->GetWebContentsAt(1))
      ->SetIsCurrentlyAudible(true);
  service_->OnTabActivated(HandleAt(1));

  task_environment()->FastForwardBy(base::Days(30));
  EXPECT_EQ(2, strip()->count());
  EXPECT_FALSE(service_->MayArchive(HandleAt(1)));
}

TEST_F(ArchiveServiceTest, ATabWithAnUnloadHandlerIsNeverArchived) {
  AddTab(browser(), GURL("https://form.example/"));
  AddTab(browser(), GURL("https://active.example/"));
  content::WebContentsTester::For(strip()->GetWebContentsAt(1))
      ->SetShouldSuppressDialogs(false);
  // RenderFrameHostTester lets a test claim the frame has a beforeunload
  // handler without running one.
  content::RenderFrameHostTester::For(
      strip()->GetWebContentsAt(1)->GetPrimaryMainFrame())
      ->SimulateBeforeUnloadHandlerPresent();
  service_->OnTabActivated(HandleAt(1));

  task_environment()->FastForwardBy(base::Days(30));
  EXPECT_EQ(2, strip()->count());
  EXPECT_FALSE(service_->MayArchive(HandleAt(1)));
}

TEST_F(ArchiveServiceTest, PinnedAndFavouriteTabsAreNeverArchived) {
  AddTab(browser(), GURL("https://pinned.example/"));
  AddTab(browser(), GURL("https://active.example/"));
  sidebar_model_->PinTab(1);
  task_environment()->FastForwardBy(base::Days(30));
  EXPECT_EQ(2, strip()->count());
}

TEST_F(ArchiveServiceTest, NeverMeansNoTimerIsEverScheduled) {
  model_.SetArchiveTimeout(model_.default_space_id(), ArchiveTimeout::kNever);
  AddTab(browser(), GURL("https://a.example/"));
  AddTab(browser(), GURL("https://b.example/"));
  EXPECT_FALSE(service_->next_expiry_for_testing().has_value());
  task_environment()->FastForwardBy(base::Days(365));
  EXPECT_EQ(2, strip()->count());
}

TEST_F(ArchiveServiceTest, OneTimerServesEveryTab) {
  for (int i = 0; i < 20; ++i) {
    AddTab(browser(), GURL("https://example.com/" + base::NumberToString(i)));
  }
  // The contract is one scheduled expiry for the whole browser, not twenty.
  EXPECT_EQ(1, service_->scheduled_timer_count_for_testing());
}

TEST_F(ArchiveServiceTest, ActivityPushesTheExpiryOut) {
  AddTab(browser(), GURL("https://a.example/"));
  AddTab(browser(), GURL("https://b.example/"));
  service_->OnTabActivated(HandleAt(0));
  const base::Time first = *service_->next_expiry_for_testing();
  task_environment()->FastForwardBy(base::Hours(1));
  service_->OnTabActivated(HandleAt(0));
  EXPECT_GT(*service_->next_expiry_for_testing(), first);
}

TEST_F(ArchiveServiceTest, ArchiveAllTodayLeavesPinnedAndFavouritesAlone) {
  AddTab(browser(), GURL("https://today.example/"));
  AddTab(browser(), GURL("https://pinned.example/"));
  sidebar_model_->PinTab(0);
  service_->ArchiveAllToday();
  EXPECT_EQ(1, strip()->count());
}
```

The two test helpers above are the parts most likely to have drifted. Before writing them, check the real spelling in `content/public/test/web_contents_tester.h` and `content/public/test/test_renderer_host.h`; if `SimulateBeforeUnloadHandlerPresent` is absent in 152, look at how `chrome/browser/ui/browser_unittest.cc` fakes `NeedToFireBeforeUnloadOrUnloadEvents` and use that instead. The assertion each test makes is the contract; the helper is just how it gets there.

- [ ] **Step 2: Implement**

`ArchiveService` keeps `std::map<tabs::TabHandle, base::Time> last_active_`. On construction it seeds every existing tab with `ModelStore::last_save_time()`, which is the restart floor: a browser closed overnight archives yesterday's Today tabs on launch, and no per-tab timestamp has to survive the quit.

One `base::OneShotTimer` for the whole service. `RescheduleTimer()` computes the earliest expiry over tabs that `MayArchive` allows and starts the timer for that instant; a `kNever` timeout means no expiry and the timer is stopped. It runs on `OnTabActivated`, on tab insert and close, and after each firing.

`MayArchive` returns false for: the active tab in any window, a tab whose alert state is audio playing, a tab where `WebContents::NeedToFireBeforeUnloadOrUnloadEvents()` is true, and any tab that `TabBinding::IsBound` — pinned and favourite tabs are not Today tabs.

Archiving one tab: write `ArchivedTab` through `ArchiveStore` on its background sequence, then `CloseWebContentsAt`.

`SectionDividerView`'s Clear action calls `ArchiveAllToday`.

- [ ] **Step 3: The timeout is settable, because R2.3 says which values**

R2.3 names four choices, so they need somewhere to be chosen. Stage 1 built `SpaceBarView` with a context-menu scaffold whose items are disabled until Stages 3 and 6; add a live "Archive Today tabs after" submenu to it now, with the four options as radio items:

| Item | Value |
|---|---|
| 12 hours | `ArchiveTimeout::kTwelveHours` (default) |
| 1 day | `ArchiveTimeout::kOneDay` |
| 7 days | `ArchiveTimeout::kSevenDays` |
| Never | `ArchiveTimeout::kNever` |

Selecting one calls `SidebarModel::SetArchiveTimeout(ArchiveTimeout)`, which forwards to `ArciumModel::SetArchiveTimeout` for the active space and reschedules the timer. Add that command to `SidebarModel` and to the playground fake alongside the others.

Choosing Never must stop the timer rather than schedule a far-future one, which is what `NeverMeansNoTimerIsEverScheduled` above asserts.

- [ ] **Step 4: Run the tests**

Run: `scripts/build dev arcium_unittests && /Volumes/Texternal/chromium/src/out/dev/arcium_unittests --gtest_filter=ArchiveServiceTest.*`
Expected: PASS, 8 tests.

- [ ] **Step 5: Commit**

```bash
git add arcium/browser arcium/ui arcium/test
git commit -m "$(cat <<'EOF'
Stage 2: archive Today tabs after an idle timeout

One one-shot timer for the whole browser, rescheduled to the earliest
upcoming expiry, rather than a timer per tab or any polling. Twenty
tabs cost one timer.

Tabs restored after a quit take the model's last-save time as their idle
floor, so a browser closed overnight archives yesterday's tabs without
any per-tab timestamp having to survive the quit.

Never archived: the active tab, a tab playing audio, a tab with an
unload handler, and anything bound to an entry. Closing those is data
loss rather than tidying, and a feature that occasionally eats work is
one people switch off.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 10: The archive list

Deliberately plain, and scoped as throwaway: Stage 6's library replaces it. It exists so auto-archive is trustworthy rather than feeling like tab loss.

**Files:**
- Create: `arcium/ui/sidebar/archive_list_view.h/.cc`
- Modify: `arcium/ui/sidebar/section_divider_view.cc`
- Modify: `arcium/ui/sidebar/sidebar_model.h`
- Modify: `arcium/ui/browser/sidebar_tab_model.cc`

**Interfaces:**
- Consumes: `ArchiveStore::ListRecent`, `ArchiveStore::Remove`.
- Produces: `arcium::ArchiveListView`; `SidebarModel` gains `std::vector<ArchivedRow> archived_rows() const` and `void ReopenArchived(const GURL&, base::Time)`.

- [ ] **Step 1: Build the view**

A `views::ScrollView` of 32 px rows — favicon, title, and a relative timestamp such as "2 h ago" via `ui::TimeFormat::Simple`. Clicking reopens the URL and removes the row. Shown in a `views::BubbleDialogDelegate` anchored to the divider, opened from an archive button that appears beside Clear on hover.

Use `ScrollView::ScrollWithLayers::kDisabled` and `ClipHeightTo`, exactly as the Today list does. Stage 1 found that a layer-backed viewport is not opaque over the sidebar gradient, which trips a `views::Label` DCHECK and hides rows from the offscreen paint that `--snapshot` uses.

Load rows on open, not on every model change: the archive can be large and nothing in the sidebar needs it until it is looked at.

- [ ] **Step 2: Verify and commit**

```bash
scripts/build dev chrome && scripts/run
```

Archive a few tabs with Clear, open the list, reopen one.

```bash
git add arcium/ui
git commit -m "$(cat <<'EOF'
Stage 2: a plain archive list

Scoped as throwaway; Stage 6's library replaces it. It exists because
auto-archive with nowhere to look reads as tab loss, and a feature that
looks like it eats tabs gets switched off.

Rows load when the list opens rather than on every model change: the
archive grows without bound and nothing needs it until it is looked at.

The ScrollView is layer-free for the reason Stage 1 documented: a
layer-backed viewport is not opaque over the gradient.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 11: Tab search

The query engine only. Stage 4's command bar becomes its front end, so no UI is built twice.

**Files:**
- Create: `arcium/browser/tab_search_service.h/.cc`
- Modify: `arcium/browser/BUILD.gn`
- Test: `arcium/test/tab_search_service_unittest.cc`

**Interfaces:**
- Consumes: `TabStripModel`, `ArciumModel`, `TabBinding`, `ArchiveStore`.
- Produces: `arcium::SearchResult { enum class Source { kLiveTab, kEntry, kArchive }; Source source; std::u16string title; GURL url; EntryId entry_id; int tab_index; base::Time archived_at; int score; }` and `std::vector<SearchResult> TabSearchService::Search(const std::u16string& query, int limit)`.

- [ ] **Step 1: Write the failing test**

```cpp
TEST_F(TabSearchServiceTest, MatchesTitleAndUrlAcrossAllThreeSources) {
  AddTab(browser(), GURL("https://live.example/"), u"Live page");
  model_.AddEntry(EntryKind::kPinned, GURL("https://entry.example/"), u"Entry");
  archive_.Add(MakeArchived("https://archived.example/", u"Archived"));

  EXPECT_EQ(1u, service_->Search(u"live", 10).size());
  EXPECT_EQ(1u, service_->Search(u"entry", 10).size());
  EXPECT_EQ(1u, service_->Search(u"archived", 10).size());
  EXPECT_EQ(3u, service_->Search(u"example", 10).size());
}

TEST_F(TabSearchServiceTest, LiveTabsOutrankEntriesWhichOutrankTheArchive) {
  AddTab(browser(), GURL("https://match.example/live"), u"match");
  model_.AddEntry(EntryKind::kPinned, GURL("https://match.example/entry"),
                  u"match");
  archive_.Add(MakeArchived("https://match.example/archive", u"match"));

  std::vector<SearchResult> results = service_->Search(u"match", 10);
  ASSERT_EQ(3u, results.size());
  EXPECT_EQ(SearchResult::Source::kLiveTab, results[0].source);
  EXPECT_EQ(SearchResult::Source::kEntry, results[1].source);
  EXPECT_EQ(SearchResult::Source::kArchive, results[2].source);
}

TEST_F(TabSearchServiceTest, ATitlePrefixOutranksAMidWordMatch) {
  AddTab(browser(), GURL("https://a.example/"), u"Chromium docs");
  AddTab(browser(), GURL("https://b.example/"), u"The Chromium project");
  std::vector<SearchResult> results = service_->Search(u"chromium", 10);
  ASSERT_EQ(2u, results.size());
  EXPECT_EQ(u"Chromium docs", results[0].title);
}

TEST_F(TabSearchServiceTest, AWarmEntryIsReportedOnceNotTwice) {
  AddTab(browser(), GURL("https://both.example/"), u"Both");
  sidebar_model_->PinTab(0);
  EXPECT_EQ(1u, service_->Search(u"both", 10).size());
}

TEST_F(TabSearchServiceTest, MatchingIsCaseAndDiacriticInsensitive) {
  AddTab(browser(), GURL("https://a.example/"), u"Café");
  EXPECT_EQ(1u, service_->Search(u"CAFE", 10).size());
}

TEST_F(TabSearchServiceTest, AnEmptyQueryReturnsNothing) {
  AddTab(browser(), GURL("https://a.example/"), u"A");
  EXPECT_TRUE(service_->Search(u"", 10).empty());
}

TEST_F(TabSearchServiceTest, TheLimitIsHonouredAcrossSources) {
  for (int i = 0; i < 5; ++i) {
    AddTab(browser(), GURL("https://x.example/" + base::NumberToString(i)),
           u"x");
  }
  archive_.Add(MakeArchived("https://x.example/archived", u"x"));
  EXPECT_EQ(3u, service_->Search(u"x", 3).size());
}
```

- [ ] **Step 2: Implement**

Score is a small integer, highest first: a title prefix match scores above a title substring, which scores above a URL substring. Source breaks ties — live tab, then entry, then archive — because a thing you can switch to beats a thing that must be opened.

A warm entry is reported once, as an entry: `TabBinding::IsBound` filters its tab out of the live-tab pass.

Normalise both sides with `base::i18n::FoldCase` before comparing, so "CAFE" matches "Café".

Query the archive with `ArchiveStore::Search` and merge; the archive call blocks, so `Search` must run on the store's sequence and hand results back, or the caller must be a background context. Stage 4 will decide which when it builds the command bar; for now expose both a synchronous `Search` for tests and an async `SearchAsync(query, limit, base::OnceCallback<void(std::vector<SearchResult>)>)`, and document that UI callers must use the async one.

- [ ] **Step 3: Run the tests and commit**

Run: `scripts/build dev arcium_unittests && /Volumes/Texternal/chromium/src/out/dev/arcium_unittests --gtest_filter=TabSearchServiceTest.*`
Expected: PASS, 7 tests.

```bash
git add arcium/browser arcium/test
git commit -m "$(cat <<'EOF'
Stage 2: search across live tabs, entries and the archive

Engine only. Stage 4's command bar becomes the front end, so no search
surface is built twice.

Ranking puts a title prefix above a substring above a URL match, and
breaks ties by source: a tab you can switch to beats one that must be
opened, which beats one that must be un-archived. A warm entry is
reported once, as an entry, not twice.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 12: Session restore rebinding

Making a pinned entry come back warm when Chromium restored its tab. The spike behind this is section 8 of the spec; read it before starting.

**Files:**
- Create: `arcium/browser/session_tab_entry.h/.cc`
- Create: `patches/0120-restore-tab-entry.patch`
- Create: `patches/0130-session-tab-commands.patch`
- Modify: `arcium/browser/BUILD.gn`
- Modify: `patches/README.md`

**Interfaces:**
- Consumes: `TabBinding`, `ArciumModel`.
- Produces: `void arcium::RestoreTabEntryFromExtraData(content::WebContents*, const std::map<std::string, std::string>& extra_data)`, `void arcium::AppendTabEntryCommand(SessionService*, SessionID window_id, SessionID tab_id, content::WebContents*)`, and `inline constexpr char arcium::kEntryIdExtraDataKey[] = "arcium.entry_id"`.

- [ ] **Step 1: Understand why two patches are needed**

`SessionID` is not stable across a restart: `SessionTabHelper` assigns `SessionID::NewUnique()` in its constructor unconditionally, and `SessionIdGenerator::SetHighestRestoredID` — the API that would make restored ids survive — has no production caller. So the entry id must ride in Chromium's per-tab session `extra_data`, which does survive.

The second patch exists because `SessionService::ScheduleResetCommands()` rebuilds the command list from live browser state and re-emits no extra data. Upstream's two users of `extra_data` tolerate losing their bit; Arcium would silently unbind a pinned tab. `BuildCommandsForTab` is already where per-tab commands are appended on a rebuild, so that is the seam.

- [ ] **Step 2: Write the Arcium side**

`RestoreTabEntryFromExtraData` reads `kEntryIdExtraDataKey`, parses it with `EntryId::FromString`, and — if the id names a real entry — binds the WebContents' tab to it. An id that names nothing is ignored: the tab becomes a Today tab, which is a correct outcome, not an error.

`AppendTabEntryCommand` asks `TabBinding::EntryForTab`; if the tab is bound it calls `SessionService::AddTabExtraData(window_id, tab_id, kEntryIdExtraDataKey, id.value())`.

Both need the per-profile `ArciumModel` and `TabBinding`, so route through the same profile-keyed accessor Task 6 introduced.

- [ ] **Step 3: Patch 0120 — the restore seam**

In `chrome/browser/ui/browser_tabrestore.cc`, beside the two calls that already do this:

```cpp
  glic::RestoreGlicStateFromExtraData(web_contents.get(), extra_data);
  send_tab_to_self::SendTabToSelfActivationTracker::RestoreFromExtraData(
      web_contents.get(), extra_data);
  arcium::RestoreTabEntryFromExtraData(web_contents.get(), extra_data);
```

Header:

```
Seam: chrome::AddRestoredTab in chrome/browser/ui/browser_tabrestore.cc.

Why: session restore gives a tab a fresh SessionID, so Arcium cannot key a
pinned entry on it. Chromium's per-tab session extra_data does survive a
restart and reaches this function; two upstream features already consume it
here in one line each. Without this hook every pinned entry comes back cold.

Delegates to: arcium::RestoreTabEntryFromExtraData.
```

- [ ] **Step 4: Patch 0130 — the rebuild seam**

In `SessionService::BuildCommandsForTab`, after the existing per-tab commands:

```cpp
  arcium::AppendTabEntryCommand(this, window_id, session_id, tab);
```

Header:

```
Seam: SessionService::BuildCommandsForTab in chrome/browser/sessions/session_service.cc.

Why: ScheduleResetCommands() rebuilds the session command list from live
browser state and re-emits no tab extra_data, so a key written earlier is
silently dropped. Upstream's two users tolerate that because losing their bit
is benign; for Arcium it would unbind a pinned tab. This is already the place
per-tab commands are appended on a rebuild.

Delegates to: arcium::AppendTabEntryCommand.
```

- [ ] **Step 5: Verify the round trip by hand**

There is no unit test for this: it needs a real session file written and read across two browser lifetimes.

```bash
scripts/build dev chrome && scripts/run
```

Pin two tabs and leave them open. Quit with Cmd+Q. Relaunch with `scripts/run --restore-last-session`. Both pinned rows must come back **warm** — bound to the restored tabs, not opening a second copy when clicked.

Then the rebuild path, which is the one the second patch exists for: pin a tab, open and close twenty tabs to provoke a command reset, quit, relaunch. The pinned row must still be warm.

Finally the degradation path: pin a tab, quit, delete `"$HOME/Library/Application Support/Arcium-dev/Default/Sessions"`, relaunch. The entry must come back **cold**, not missing.

Record all three in `docs/stage2-findings.md`.

- [ ] **Step 6: Update the patch inventory and commit**

Add 0110, 0120 and 0130 to the table in `patches/README.md`.

```bash
scripts/sync   # confirm the whole series still applies
git add arcium/browser patches docs/stage2-findings.md
git commit -m "$(cat <<'EOF'
Stage 2: rebind restored tabs to their entries

A restored tab gets a fresh SessionID, so the entry id travels in
Chromium's per-tab session extra_data instead, which survives a restart
and which two upstream features already read at the same seam.

The second patch is not redundant. A session command rebuild re-emits no
extra data, so a key written earlier is dropped; upstream tolerates that
because losing their bit is harmless, and Arcium would silently unbind a
pinned tab.

A lost key degrades to a cold entry rather than to a missing one, which
is why this is safe to depend on.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 13: Acceptance, perf and close-out

**Files:**
- Create: `docs/stage2-findings.md`
- Create: `docs/perf/2026-XX-XX-stage2.md`
- Modify: `CLAUDE.md`
- Modify: `docs/superpowers/specs/2026-09-05-arcium-browser-design.md`
- Modify: `docs/netaudit-findings.md`

- [ ] **Step 1: Run the full unit suite**

```bash
scripts/build dev arcium_unittests && /Volumes/Texternal/chromium/src/out/dev/arcium_unittests
```

Expected: PASS. Report failures with their output, not a summary.

- [ ] **Step 2: Execute acceptance A2.1**

Pin, favourite, rename, fold. Quit and relaunch. Everything where it was. Record each line in `docs/stage2-findings.md` with the same table shape Stage 1 used, including anything that had to be fixed.

- [ ] **Step 3: Execute acceptance A2.2**

Set the timeout to 12 h in the space's context menu. Rather than waiting, use `--arcium-fake-clock-offset=13h`, a debugging switch added to `arcium/common/arcium_features.h` alongside the Stage 1 ones, which offsets `base::Time::Now()` for the archive service only. Today tabs must archive and appear in the archive list.

- [ ] **Step 4: Execute acceptance A2.3 — perf**

```bash
pgrep -f siso   # must print nothing before starting a build
scripts/perf --runs 3 --idle 60 --label stage2
```

Compare against `docs/perf/2026-09-06-stage1.md`. Process count must be equal. Idle RSS within a few tens of MB. Startup is not a gate on this machine and the record should say so plainly rather than reporting noise as a regression — Stage 1's perf record has the wording to reuse.

Then answer the four questions from CLAUDE.md in the record, with evidence:

1. No new process.
2. Idle memory: entries are small structs shared by every window on a profile.
3. Before first paint: the JSON read and the SQLite open are both on background sequences; the sidebar draws live tabs first.
4. UI thread: writes debounced onto a background sequence, SQLite off-thread, one archive timer per browser.

For point 4, confirm with tracing rather than assertion, as A2.3 asks: run with `--trace-startup=disabled-by-default-file` and check no `base::ScopedBlockingCall` appears on the UI thread's track.

- [ ] **Step 5: Network audit**

```bash
scripts/netaudit 120
```

Nothing in Stage 2 talks to the network, so the allowlist should be unchanged. Append a Stage 2 section to `docs/netaudit-findings.md` saying so, with the run's output.

- [ ] **Step 6: Update the docs**

- `CLAUDE.md`: Stage 2 to done, with a pointer to `docs/stage2-findings.md`; add any new command or switch to the Commands section.
- Master spec: add a Stage 2 entry to the deviations list — folders are Arcium entities rather than Chromium tab groups, and why; the never-archive rules; permanent custom titles.
- `patches/README.md`: confirm 0110, 0120 and 0130 are described.

- [ ] **Step 7: Commit and push**

```bash
git add -A docs CLAUDE.md patches
git commit -m "$(cat <<'EOF'
Stage 2 close-out: acceptance, perf and findings

Records what the acceptance pass found, the perf run against Stage 1,
and the deviations from the master spec that building the stage forced.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
git push
```

---

## Deferred to later stages, deliberately

- **Tab search UI** — Stage 4's command bar. The engine ships here.
- **The library** — Stage 6 replaces the plain archive list from Task 10.
- **Downloads and extension actions in the sidebar** — Stage 6, carried over from Stage 1.
- **Many spaces** — Stage 3. `space_id` travels through the model, the stores and the archive from the first commit, so Stage 3 is a UI change rather than a data migration.
- **Browser tests** — as in Stage 1, building Chromium's browser-test target costs hours on this machine. Verification is `arcium_unittests` plus the checklist in `docs/stage2-findings.md`, executed over the DevTools protocol.

## Ordering, and why

Tasks 1 to 4 build the model and its two stores with no UI at all, so every rule about identity, corruption and persistence is settled by a unit test before anything draws. Task 5 joins entries to tabs. Task 6 makes it visible. Task 7 gives every command a menu, which is what makes the stage usable **before** drag and drop rather than only after it — so if Task 8 runs long, the stage still delivers. Tasks 9 to 11 are independent features over the same model. Task 12 is the one with real upstream risk and comes late, when the thing it rebinds already works. Task 13 closes.
