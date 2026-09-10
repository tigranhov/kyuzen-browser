# Stage 3a: Spaces — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** One window holds several spaces, each with its own name, icon, gradient, favourites, pins, folders and Today, and every Chromium path that walks the tab strip is limited to the active one.

**Architecture:** The single `TabStripModel` keeps every space's tabs; a side table says which space each tab is in. A per-window `SpaceSwitcher` owns the active space, answers "is this tab in it", and performs switches. Two new patches narrow Chromium's strip-wide behaviour — the tab commands and the post-close selection — and both delegate to `arcium/`, carrying no logic. Everything persistent lives in the existing model file, at schema version 3.

**Tech Stack:** Chromium 152 C++ (Views, `TabStripModel`, `TabStripModelObserver`, `WebContentsUserData`, session `extra_data`), `base::DictValue` JSON through `ImportantFileWriter`, `sql::Database` for the archive, GN/siso, gtest with `BrowserWithTestWindowTest` and `ViewsTestBase`.

**Spec:** `docs/superpowers/specs/2026-09-10-stage-3a-spaces-design.md`. Read it before Task 1; it is the authority this plan argues from. Master requirements R3.1, R3.2, R3.3 and R3.8 live in `docs/superpowers/specs/2026-09-05-arcium-browser-design.md`.

## Global Constraints

- All Arcium code lives in `arcium/`. Upstream Chromium files change **only** through numbered patches in `patches/`, each opening with a prose header naming `Seam:`, `Why:` and `Delegates to:` before the first `diff --git`. **Logic never lives in a patch**; a patch is a few lines that call into `arcium/`.
- Always-visible UI is Chromium Views in C++. **Never WebUI. No Swift, AppKit or Cocoa.**
- One window, one `Browser`, one `TabStripModel`. Spaces are a side table over tabs, never extra browsers, never tab groups (deviation D2-1).
- Background spaces do nothing: no timer, no poll, no thumbnail per space.
- Persistence: JSON via `ImportantFileWriter`, SQLite via `sql::Database`. **Never write on the UI thread. No sync I/O ever.**
- `arcium/browser` and `arcium/browser/model` carry **no `//chrome` dependency**; the model target's deps are exactly `//base` and `//url`.
- `base::DictValue` and `base::ListValue`. **`base::Value::Dict` does not exist in this tree.**
- Files over ~500 lines are a smell; split rather than grow one.
- `scripts/format` after every code change. **`git cl format` does not work here.**
- Test-driven: the failing test comes first, and after each step delete the code just written and confirm the named test fails (mutation check).
- Commit by explicit path. **Never `git add -A` or `git add .`**
- Commit messages say why and end with `Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>`. Do not push.
- **Do not modify, revert, stash or commit** `arcium/ui/browser/browser_sidebar_controller.{h,cc}`, `arcium/ui/sidebar/sidebar_metrics.h`, `patches/0065-mac-titlebar-height.patch` or `AGENTS.md`. They hold the owner's uncommitted work. Task 12 is gated on the owner releasing the first two.
- Never commit Chromium sources or build output.
- Build and test commands, run from the repo root:
  - `scripts/build dev arcium_unittests`
  - `/Volumes/Texternal/chromium/src/out/dev/arcium_unittests --gtest_filter=<Suite.Name>`
  - `scripts/build dev` for the browser, `scripts/run` to launch it.
  - Before any build check `pgrep -f "Arcium.app/Contents/MacOS/Arcium"` and `pgrep -f siso`, and never rebuild `out/dev` while a suite is running. Do not kill the owner's processes.
  - A `BUILD.gn` edit costs a `gn gen` plus a ~17-minute graph reload, which is why Task 0 makes every `BUILD.gn` edit at once.

## Deviations from the spec this plan makes

Three rulings, all folded into the spec in Task 13's commit:

- **R1 — `IDC_SELECT_TAB_0..7` count open tabs only.** §4.2 says the nth *row* including a cold one. A cold row's position depends on which folders are collapsed, which only the view knows; counting it from the browser layer would give a number that disagrees with the screen. The command therefore selects the nth **open tab** of the active space in sidebar order and never opens a cold entry.
- **R2 — deleting a space removes it from the model at once.** §6 says the space leaves the model only after its last tab has gone, and that a cancelled `beforeunload` stops the delete. Chromium gives no cancel signal at the seam Arcium has, and the state machine that would fake one can hang forever on a dialog the user never answers. Instead the switcher moves to the landing space, asks Chromium to close the doomed space's tabs, and then removes the space and re-tags whatever is still alive — a tab held open by `beforeunload` — into the landing space. Nothing is ever tagged with a space that does not exist.
- **R4 — A3a.6 loses its tab-search half.** Nothing in the shipped UI reaches `TabSearchService` yet, so "choosing another space's tab in tab search" cannot be executed by hand. The activation rule of §4.4 is checked instead through A3a.8, where Cmd+Shift+T reopens a tab of a background space.

## File structure

New, all under `arcium/`:

| File | Responsibility |
|---|---|
| `arcium/browser/tab_space.{h,cc}` | The space tag and `TabKey` on a `WebContents`, their session `extra_data` keys, and `SpaceOfTab` — the one place that answers which space a tab is in. |
| `arcium/ui/browser/space_switcher.{h,cc}` | Per-window active space: membership, ordering, switching, new-tab tagging, foreign-activation, delete and move. |
| `arcium/ui/browser/sidebar_tab_model_spaces.cc` | `SidebarTabModel`'s spaces API, kept out of `sidebar_tab_model.cc` (430 lines already). |
| `arcium/ui/browser/tab_commands.{h,cc}` | `arcium::HandleTabCommand`, the hook target of patch 0155. |
| `arcium/ui/sidebar/space_gradients.{h,cc}` | The fixed gradient palette, preset 0 being today's colour-mixer pair. |
| `arcium/ui/playground/fake_sidebar_spaces.cc` | `FakeSidebarModel`'s spaces API, kept out of its 739-line `.cc`. |
| `patches/0155-tab-commands-space.patch` | Strip-wide commands, hooked at the `IDC_NEW_TAB` seam patch 0090 uses. |
| `patches/0160-tab-strip-selection-space.patch` | Post-close selection, through a new `TabStripModelDelegate` question. |

Modified: `space.h`, `entry_id.h`, `arcium_model.{h,cc}`, `model_serializer.{h,cc}`, `model_migration.cc`, `entry_claim.{h,cc}`, `session_tab_entry.cc`, `archive_store.{h,cc}`, `archive_service.{h,cc}`, `tab_search_service.cc`, `sidebar_tab_model.{h,cc}`, `sidebar_tab_model_entries.cc`, `sidebar_model.h`, `fake_sidebar_model.{h,cc}`, `space_bar_view.{h,cc}`, `sidebar_view.{h,cc}`, `tint_background.{h,cc}`, `row_context_menu.{h,cc}`, and in Task 12 only, `browser_sidebar_controller.{h,cc}`.

New tests: `arcium/test/tab_space_unittest.cc`, `space_switcher_unittest.cc`, `space_scoping_unittest.cc`, `tab_commands_unittest.cc`, `space_selection_unittest.cc`, `space_bar_unittest.cc`. Grown: `arcium_model_unittest.cc`, `model_serializer_unittest.cc`, `model_migration_unittest.cc`, `session_tab_entry_unittest.cc`, `archive_store_unittest.cc`. `sidebar_views_unittest.cc` (2,109 lines) does not grow.

## Performance

The four questions, answered for the stage as a whole:

1. **Processes:** none added. No WebUI surface, no helper, no service.
2. **Idle memory:** two short strings per tab (`arcium.space_id`, `arcium.tab_key`), a `Space` record and one dot view per space. Nothing per tab beyond the strings; the budget is 0 per tab, and two UUIDs at ~40 bytes each is what "spaces at all" costs.
3. **Startup:** spaces arrive in the model-file read that already happens after first paint. `SpaceSwitcher`'s construction reads the active space out of that model in memory. No new disk read.
4. **UI thread:** a switch activates one tab and rebuilds the sidebar through the existing coalesced rebuild; the slide is a compositor layer animation. Deleting a space's archive rows runs on the archive's own sequence, never here. The rebuild must stay under 8 ms for ~50 rows; if the A3.2 trace disagrees, the fallback recorded in the spec is one row list kept per space.

---

### Task 0: Scaffolding and every `BUILD.gn` edit

Every new file is created here as a compiling stub and every `BUILD.gn` list is edited once. This is the one place in the plan where setup is not folded into the task that needs it: a `BUILD.gn` edit costs a `gn gen` and a ~17-minute graph reload on this machine, and doing them one task at a time would spend most of a day reloading a graph.

**Files:**
- Create: `arcium/browser/tab_space.h`, `arcium/browser/tab_space.cc`
- Create: `arcium/ui/browser/space_switcher.h`, `arcium/ui/browser/space_switcher.cc`, `arcium/ui/browser/sidebar_tab_model_spaces.cc`, `arcium/ui/browser/tab_commands.h`, `arcium/ui/browser/tab_commands.cc`
- Create: `arcium/ui/sidebar/space_gradients.h`, `arcium/ui/sidebar/space_gradients.cc`
- Create: `arcium/ui/playground/fake_sidebar_spaces.cc`
- Create: `arcium/test/tab_space_unittest.cc`, `space_switcher_unittest.cc`, `space_scoping_unittest.cc`, `tab_commands_unittest.cc`, `space_selection_unittest.cc`, `space_bar_unittest.cc`
- Modify: `arcium/browser/BUILD.gn`, `arcium/ui/browser/BUILD.gn`, `arcium/ui/sidebar/BUILD.gn`, `arcium/ui/playground/BUILD.gn`, `arcium/test/BUILD.gn`

**Interfaces:**
- Consumes: nothing.
- Produces: file paths and targets. Every later task edits files that already build.

- [ ] **Step 1: Create each new source file as a stub**

Every `.cc` gets the licence header, its own `#include` first, and an empty `namespace arcium {}`; every `.h` gets the licence header and an include guard matching its path. For example `arcium/browser/tab_space.h`:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_TAB_SPACE_H_
#define ARCIUM_BROWSER_TAB_SPACE_H_

namespace arcium {}  // namespace arcium

#endif  // ARCIUM_BROWSER_TAB_SPACE_H_
```

and `arcium/browser/tab_space.cc`:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/tab_space.h"

namespace arcium {}  // namespace arcium
```

Each new `_unittest.cc` gets the licence header and one placeholder so the target links:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "testing/gtest/include/gtest/gtest.h"

namespace arcium {
namespace {

TEST(SpacesScaffoldingTest, TheFileBuilds) {
  SUCCEED();
}

}  // namespace
}  // namespace arcium
```

Use a distinct test-suite name per file (`TabSpaceScaffoldingTest`, `SpaceSwitcherScaffoldingTest`, and so on) — gtest refuses two `TEST`s with the same suite and name. Each placeholder is deleted by the task that fills its file.

- [ ] **Step 2: Add the sources to their targets**

In `arcium/browser/BUILD.gn`, add `"tab_space.cc",` and `"tab_space.h",` to `sources` in alphabetical order. In `arcium/ui/browser/BUILD.gn`, add `"sidebar_tab_model_spaces.cc"`, `"space_switcher.cc"`, `"space_switcher.h"`, `"tab_commands.cc"`, `"tab_commands.h"`. In `arcium/ui/sidebar/BUILD.gn`, add `"space_gradients.cc"` and `"space_gradients.h"`. In `arcium/ui/playground/BUILD.gn`, add `"fake_sidebar_spaces.cc"` to the `fake_model` target. In `arcium/test/BUILD.gn`, add the six new `_unittest.cc` files to `sources`, alphabetically.

- [ ] **Step 3: Format and build**

```bash
scripts/format && scripts/build dev arcium_unittests
```

Expected: the build reloads the GN graph and then succeeds. The six placeholder tests run green.

- [ ] **Step 4: Commit**

```bash
git add arcium/browser/tab_space.h arcium/browser/tab_space.cc arcium/browser/BUILD.gn arcium/ui/browser/space_switcher.h arcium/ui/browser/space_switcher.cc arcium/ui/browser/sidebar_tab_model_spaces.cc arcium/ui/browser/tab_commands.h arcium/ui/browser/tab_commands.cc arcium/ui/browser/BUILD.gn arcium/ui/sidebar/space_gradients.h arcium/ui/sidebar/space_gradients.cc arcium/ui/sidebar/BUILD.gn arcium/ui/playground/fake_sidebar_spaces.cc arcium/ui/playground/BUILD.gn arcium/test/tab_space_unittest.cc arcium/test/space_switcher_unittest.cc arcium/test/space_scoping_unittest.cc arcium/test/tab_commands_unittest.cc arcium/test/space_selection_unittest.cc arcium/test/space_bar_unittest.cc arcium/test/BUILD.gn
git commit -m "Stage 3a's files exist before its GN graph reload does

Every new file of the stage lands as a stub in one commit, so the
seventeen-minute graph reload a BUILD.gn edit costs is paid once instead
of once per task.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 1: The `Space` record and `ArciumModel`'s spaces API

**Files:**
- Modify: `arcium/browser/model/space.h`, `arcium/browser/model/entry_id.h`, `arcium/browser/model/arcium_model.h`, `arcium/browser/model/arcium_model.cc`
- Test: `arcium/test/arcium_model_unittest.cc`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `using TabKey = TypedId<TabKeyTag>;` in `entry_id.h`.
  - `Space` fields `std::u16string icon`, `int gradient = 0`, `TabKey last_active_tab`.
  - `SpaceId ArciumModel::AddSpace(const std::u16string& name)`
  - `void RenameSpace(SpaceId, const std::u16string&)`, `SetSpaceIcon(SpaceId, const std::u16string&)`, `SetSpaceGradient(SpaceId, int)`, `ReorderSpace(SpaceId, int position)`, `SetLastActiveTab(SpaceId, TabKey)`
  - `void RemoveSpace(SpaceId)` — refuses the last space
  - `void MoveEntryToSpace(EntryId, SpaceId)`
  - `SpaceId last_active_space() const`, `void SetLastActiveSpace(SpaceId)`
  - `EntryId AddEntry(SpaceId, EntryKind, const GURL&, const std::u16string&)` and `FolderId AddFolder(SpaceId, const std::u16string&, std::optional<FolderId>)` — the space is now explicit
  - test-only wrappers `AddEntryForTesting(kind, url, title)` and `AddFolderForTesting(name, parent)` that pass `default_space_id()`

- [ ] **Step 1: Write the failing tests**

Append to `arcium/test/arcium_model_unittest.cc`:

```cpp
TEST(ArciumModelTest, ASpaceIsAddedAfterTheExistingOnes) {
  ArciumModel model;
  const SpaceId first = model.default_space_id();
  const SpaceId second = model.AddSpace(u"Work");
  ASSERT_EQ(2u, model.spaces().size());
  EXPECT_EQ(first, model.default_space_id());
  const Space* added = model.GetSpace(second);
  ASSERT_TRUE(added);
  EXPECT_EQ(u"Work", added->name);
  EXPECT_EQ(1, added->position);
  EXPECT_EQ(0, added->gradient);
  EXPECT_TRUE(added->icon.empty());
}

TEST(ArciumModelTest, ASpacesNameIconGradientAndLastTabAreSettable) {
  ArciumModel model;
  const SpaceId id = model.AddSpace(u"Work");
  const TabKey key = TabKey::Generate();
  model.RenameSpace(id, u"Home");
  model.SetSpaceIcon(id, u"🏠");
  model.SetSpaceGradient(id, 3);
  model.SetLastActiveTab(id, key);
  const Space* space = model.GetSpace(id);
  ASSERT_TRUE(space);
  EXPECT_EQ(u"Home", space->name);
  EXPECT_EQ(u"🏠", space->icon);
  EXPECT_EQ(3, space->gradient);
  EXPECT_EQ(key, space->last_active_tab);
}

TEST(ArciumModelTest, ReorderingASpaceRenumbersItsNeighbours) {
  ArciumModel model;
  const SpaceId first = model.default_space_id();
  const SpaceId second = model.AddSpace(u"Second");
  const SpaceId third = model.AddSpace(u"Third");
  model.ReorderSpace(third, 0);
  EXPECT_EQ(0, model.GetSpace(third)->position);
  EXPECT_EQ(1, model.GetSpace(first)->position);
  EXPECT_EQ(2, model.GetSpace(second)->position);
  // spaces() is kept in position order, so default_space_id() follows.
  EXPECT_EQ(third, model.default_space_id());
}

TEST(ArciumModelTest, RemovingASpaceTakesItsEntriesAndFolders) {
  ArciumModel model;
  const SpaceId keep = model.default_space_id();
  const SpaceId doomed = model.AddSpace(u"Doomed");
  const FolderId folder = model.AddFolder(doomed, u"Reading", std::nullopt);
  model.AddEntry(doomed, EntryKind::kPinned, GURL("https://a.example/"), u"A");
  const EntryId kept =
      model.AddEntry(keep, EntryKind::kPinned, GURL("https://b.example/"), u"B");
  model.RemoveSpace(doomed);
  EXPECT_EQ(1u, model.spaces().size());
  EXPECT_FALSE(model.GetFolder(folder));
  ASSERT_EQ(1u, model.entries().size());
  EXPECT_EQ(kept, model.entries().front().id);
}

TEST(ArciumModelTest, TheLastSpaceCannotBeRemoved) {
  ArciumModel model;
  model.RemoveSpace(model.default_space_id());
  EXPECT_EQ(1u, model.spaces().size());
}

TEST(ArciumModelTest, AnEntryMovedToAnotherSpaceLandsAtItsTopLevel) {
  ArciumModel model;
  const SpaceId from = model.default_space_id();
  const SpaceId to = model.AddSpace(u"Work");
  const FolderId folder = model.AddFolder(from, u"Reading", std::nullopt);
  model.AddEntry(to, EntryKind::kPinned, GURL("https://a.example/"), u"A");
  const EntryId moved =
      model.AddEntry(from, EntryKind::kPinned, GURL("https://b.example/"), u"B");
  model.SetEntryFolder(moved, folder);
  model.MoveEntryToSpace(moved, to);
  const TabEntry* entry = model.GetEntry(moved);
  ASSERT_TRUE(entry);
  EXPECT_EQ(to, entry->space_id);
  EXPECT_FALSE(entry->folder_id.has_value());
  // Last among the target's pinned entries, which already had one.
  EXPECT_EQ(1, entry->position);
}

TEST(ArciumModelTest, TheLastActiveSpaceIsRememberedAndFallsBackToTheFirst) {
  ArciumModel model;
  EXPECT_EQ(model.default_space_id(), model.last_active_space());
  const SpaceId second = model.AddSpace(u"Work");
  model.SetLastActiveSpace(second);
  EXPECT_EQ(second, model.last_active_space());
  // A space that has gone leaves the answer pointing at one that exists.
  model.RemoveSpace(second);
  EXPECT_EQ(model.default_space_id(), model.last_active_space());
}
```

- [ ] **Step 2: Run them and watch them fail**

```bash
/Volumes/Texternal/chromium/src/out/dev/arcium_unittests --gtest_filter=ArciumModelTest.*
```

Expected: compilation fails — `AddSpace`, `TabKey`, the four-argument `AddEntry` and the three-argument `AddFolder` do not exist.

- [ ] **Step 3: Add `TabKey` and the `Space` fields**

In `entry_id.h`, beside the other tags:

```cpp
struct TabKeyTag;
using TabKey = TypedId<TabKeyTag>;
```

In `space.h`, replace the struct's comment and body with:

```cpp
// A space owns its favourites, pins, folders and Today. Stage 3a made it
// several; the id travelled through the model from Stage 2 so this was a UI
// change rather than a data migration.
struct Space {
  SpaceId id;
  std::u16string name;
  // One emoji, or empty to draw the first letter of the name.
  std::u16string icon;
  // An index into the fixed palette in arcium/ui/sidebar/space_gradients.h.
  // 0 is the sidebar's original pair, so a space that never chose one looks
  // exactly as it did before spaces existed.
  int gradient = 0;
  ArchiveTimeout archive_timeout = ArchiveTimeout::kTwelveHours;
  int position = 0;
  // The tab a switch to this space lands on. A TabKey and not a SessionID:
  // Chromium's tab ids do not survive a restart (Stage 2 finding 1) and this
  // has to name a Today tab across one.
  TabKey last_active_tab;
};
```

- [ ] **Step 4: Add the model's spaces API**

In `arcium_model.h`, in the spaces block:

```cpp
  const std::vector<Space>& spaces() const { return spaces_; }
  // The first space in position order. A fallback for data that names no
  // space and for callers with no window to ask; the space a window is
  // *showing* comes from its SpaceSwitcher, never from here.
  SpaceId default_space_id() const;
  // The space last active at quit, or the first space when that one has gone.
  SpaceId last_active_space() const;
  const Space* GetSpace(SpaceId id) const;

  SpaceId AddSpace(const std::u16string& name);
  void RenameSpace(SpaceId id, const std::u16string& name);
  void SetSpaceIcon(SpaceId id, const std::u16string& icon);
  void SetSpaceGradient(SpaceId id, int gradient);
  void SetLastActiveTab(SpaceId id, TabKey key);
  void SetLastActiveSpace(SpaceId id);
  void ReorderSpace(SpaceId id, int new_position);
  // Removes the space with its entries and folders. Refuses the last space:
  // every tab has to be in one, so a model with none is unrepresentable.
  // The space's archive rows are ArchiveService's to delete, on its own
  // sequence.
  void RemoveSpace(SpaceId id);
  void SetArchiveTimeout(SpaceId space_id, ArchiveTimeout timeout);
```

and change the two creators, adding the test wrappers beside them:

```cpp
  // The space is explicit: an entry made into "whichever space is first"
  // would land in the wrong one for every window not showing that space.
  EntryId AddEntry(SpaceId space_id,
                   EntryKind kind,
                   const GURL& url,
                   const std::u16string& title);
  // Puts the entry at the end of `space_id`'s top level, with no folder:
  // folders do not cross spaces.
  void MoveEntryToSpace(EntryId id, SpaceId space_id);
  FolderId AddFolder(SpaceId space_id,
                     const std::u16string& name,
                     std::optional<FolderId> parent_id = std::nullopt);

  // For tests that predate spaces and only ever meant the first one.
  EntryId AddEntryForTesting(EntryKind kind,
                             const GURL& url,
                             const std::u16string& title) {
    return AddEntry(default_space_id(), kind, url, title);
  }
  FolderId AddFolderForTesting(
      const std::u16string& name,
      std::optional<FolderId> parent_id = std::nullopt) {
    return AddFolder(default_space_id(), name, parent_id);
  }
```

with the member `SpaceId last_active_space_;` beside `spaces_`.

- [ ] **Step 5: Implement them in `arcium_model.cc`**

```cpp
SpaceId ArciumModel::last_active_space() const {
  return GetSpace(last_active_space_) ? last_active_space_ : default_space_id();
}

SpaceId ArciumModel::AddSpace(const std::u16string& name) {
  Space space;
  space.id = SpaceId::Generate();
  space.name = name;
  space.position = static_cast<int>(spaces_.size());
  const SpaceId id = space.id;
  spaces_.push_back(std::move(space));
  Notify();
  return id;
}

void ArciumModel::RenameSpace(SpaceId id, const std::u16string& name) {
  Space* space = FindSpace(id);
  if (!space || space->name == name) {
    return;
  }
  space->name = name;
  Notify();
}
```

`SetSpaceIcon`, `SetSpaceGradient` and `SetLastActiveTab` follow the same shape on `icon`, `gradient` and `last_active_tab`. `SetLastActiveSpace` writes `last_active_space_` and notifies only when it changes.

```cpp
void ArciumModel::ReorderSpace(SpaceId id, int new_position) {
  if (!GetSpace(id)) {
    return;
  }
  new_position =
      std::clamp(new_position, 0, static_cast<int>(spaces_.size()) - 1);
  std::vector<Space> order;
  order.reserve(spaces_.size());
  Space moved;
  for (Space& space : spaces_) {
    if (space.id == id) {
      moved = std::move(space);
    } else {
      order.push_back(std::move(space));
    }
  }
  order.insert(order.begin() + new_position, std::move(moved));
  for (size_t i = 0; i < order.size(); ++i) {
    order[i].position = static_cast<int>(i);
  }
  spaces_ = std::move(order);
  Notify();
}

void ArciumModel::RemoveSpace(SpaceId id) {
  // Every tab is in a space, so the last one cannot go: a model with no
  // space at all is a state nothing downstream can draw.
  if (spaces_.size() <= 1 || !GetSpace(id)) {
    return;
  }
  std::erase_if(entries_, [id](const TabEntry& e) { return e.space_id == id; });
  std::erase_if(folders_, [id](const Folder& f) { return f.space_id == id; });
  std::erase_if(spaces_, [id](const Space& s) { return s.id == id; });
  for (size_t i = 0; i < spaces_.size(); ++i) {
    spaces_[i].position = static_cast<int>(i);
  }
  NormalisePositions();
  Notify();
}

void ArciumModel::MoveEntryToSpace(EntryId id, SpaceId space_id) {
  TabEntry* entry = FindEntry(id);
  if (!entry || !GetSpace(space_id) || entry->space_id == space_id) {
    return;
  }
  entry->space_id = space_id;
  // Folders do not cross spaces, so the entry arrives at the top level.
  entry->folder_id.reset();
  entry->position =
      static_cast<int>(EntriesForKind(space_id, entry->kind).size());
  NormalisePositions();
  Notify();
}
```

`AddEntry` and `AddFolder` take their space from the argument instead of `default_space_id()`; everything else in them is unchanged. Add the private `Space* FindSpace(SpaceId)` beside `FindEntry`. `ReplaceAll` keeps its empty-spaces fallback and additionally sorts `spaces_` by `position` so `spaces().front()` is the first space, and clears `last_active_space_` when the loaded model does not hold it.

- [ ] **Step 6: Fix the existing call sites**

`AddEntry` has 62 test call sites and one production one; `AddFolder` has 47 and one. Every old test call opens with `EntryKind` or with a `u"..."` name, and the new tests written in Step 1 open with a space id, so the rewrite can be told which is which — including the fifteen calls whose first argument is on the next line:

```bash
perl -0pi -e 's/\.AddEntry\((\s*)EntryKind/.AddEntryForTesting($1EntryKind/g; s/\.AddFolder\((\s*)u"/.AddFolderForTesting($1u"/g' arcium/test/*.cc
grep -rn "AddEntry(\|AddFolder(" arcium/test/ | grep -v ForTesting
```

Expected from the grep: only the calls added in Step 1, each naming its space. Then give the two production call sites their space explicitly — in `sidebar_tab_model_entries.cc`, `AddEntryForTab` and `CreateFolderWithEntry` pass `active_space()` once Task 5 gives them one; until then pass `arcium_model_->default_space_id()` and leave a `// Task 5: the window's space.` beside each.

- [ ] **Step 7: Run the tests**

```bash
scripts/format && scripts/build dev arcium_unittests && /Volumes/Texternal/chromium/src/out/dev/arcium_unittests
```

Expected: all green, the seven new tests included.

- [ ] **Step 8: Mutation check**

Delete the body of `MoveEntryToSpace` (leave `return;`) and rebuild: `ArciumModelTest.AnEntryMovedToAnotherSpaceLandsAtItsTopLevel` must fail. Restore it. Do the same for `RemoveSpace`'s `spaces_.size() <= 1` guard against `TheLastSpaceCannotBeRemoved`.

- [ ] **Step 9: Commit**

```bash
git add arcium/browser/model/space.h arcium/browser/model/entry_id.h arcium/browser/model/arcium_model.h arcium/browser/model/arcium_model.cc arcium/test/
git commit -m "A space is a thing you can make, name and throw away

AddEntry and AddFolder now say which space they mean. Every caller that
meant the first one says so through the testing wrappers, so the two
production call sites are the only places left that have to answer the
question, and Task 5 answers it with the window's active space.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 2: Schema version 3

**Files:**
- Modify: `arcium/browser/model/model_serializer.h`, `arcium/browser/model/model_serializer.cc`, `arcium/browser/model/model_migration.cc`
- Test: `arcium/test/model_serializer_unittest.cc`, `arcium/test/model_migration_unittest.cc`

**Interfaces:**
- Consumes: Task 1's `Space` fields, `ArciumModel::last_active_space()`, `SetLastActiveSpace`.
- Produces: `kModelSchemaVersion == 3`; the serialised keys `icon`, `gradient`, `last_active_tab` per space and the top-level `last_active_space`; `MigrateV2ToV3`.

- [ ] **Step 1: Write the failing tests**

In `model_serializer_unittest.cc`:

```cpp
TEST(ModelSerializerTest, ASpacesIconGradientAndLastTabSurviveARoundTrip) {
  ArciumModel written;
  const SpaceId id = written.AddSpace(u"Work");
  const TabKey key = TabKey::Generate();
  written.SetSpaceIcon(id, u"💼");
  written.SetSpaceGradient(id, 2);
  written.SetLastActiveTab(id, key);
  written.SetLastActiveSpace(id);

  ArciumModel read;
  ASSERT_TRUE(DeserializeModel(SerializeModel(written), &read));
  const Space* space = read.GetSpace(id);
  ASSERT_TRUE(space);
  EXPECT_EQ(u"💼", space->icon);
  EXPECT_EQ(2, space->gradient);
  EXPECT_EQ(key, space->last_active_tab);
  EXPECT_EQ(id, read.last_active_space());
}

TEST(ModelSerializerTest, ALastActiveSpaceNamingNothingFallsBackToTheFirst) {
  ArciumModel written;
  base::DictValue dict = SerializeModel(written);
  dict.Set("last_active_space", SpaceId::Generate().value());
  ArciumModel read;
  ASSERT_TRUE(DeserializeModel(dict, &read));
  EXPECT_EQ(read.default_space_id(), read.last_active_space());
}

TEST(ModelSerializerTest, AnUnreadableGradientOrIconLeavesTheDefaults) {
  ArciumModel written;
  base::DictValue dict = SerializeModel(written);
  base::ListValue* spaces = dict.FindList("spaces");
  ASSERT_TRUE(spaces);
  base::DictValue* space = (*spaces)[0].GetIfDict();
  ASSERT_TRUE(space);
  space->Set("gradient", "not a number");
  space->Set("icon", 7);
  ArciumModel read;
  ASSERT_TRUE(DeserializeModel(dict, &read));
  EXPECT_EQ(0, read.spaces().front().gradient);
  EXPECT_TRUE(read.spaces().front().icon.empty());
}
```

In `model_migration_unittest.cc`:

```cpp
TEST(ModelMigrationTest, AVersionTwoSpaceGainsTheStageThreeDefaults) {
  base::DictValue dict = DictAtVersion(2);
  base::DictValue space;
  space.Set("id", SpaceId::Generate().value());
  space.Set("name", "Space");
  base::ListValue spaces;
  spaces.Append(std::move(space));
  dict.Set("spaces", std::move(spaces));

  std::optional<base::DictValue> migrated = MigrateModelDict(std::move(dict));
  ASSERT_TRUE(migrated.has_value());
  EXPECT_EQ(kModelSchemaVersion, migrated->FindInt("version"));
  const base::ListValue* list = migrated->FindList("spaces");
  ASSERT_TRUE(list);
  const base::DictValue* first = (*list)[0].GetIfDict();
  ASSERT_TRUE(first);
  EXPECT_EQ(0, first->FindInt("gradient"));
  ASSERT_TRUE(first->FindString("icon"));
  EXPECT_EQ("", *first->FindString("icon"));
}
```

- [ ] **Step 2: Run them and watch them fail**

```bash
/Volumes/Texternal/chromium/src/out/dev/arcium_unittests --gtest_filter=ModelSerializerTest.*:ModelMigrationTest.*
```

Expected: `AVersionTwoSpaceGainsTheStageThreeDefaults` fails on the `static_assert` in `model_migration.cc` once the version is bumped, and the serializer tests fail on the missing keys.

- [ ] **Step 3: Bump the version and write the migration step**

In `model_serializer.h`, above `kModelSchemaVersion`, add the reason the file keeps for each bump:

```cpp
// 3: spaces gained `icon`, `gradient` and `last_active_tab`, and the model
//    gained `last_active_space`. Bumped for the same reason 2 was: a Stage 2
//    build opening a Stage 3 file would drop all four on its next save.
inline constexpr int kModelSchemaVersion = 3;
```

In `model_migration.cc`, beside `MigrateV1ToV2`:

```cpp
// Version 2 -> 3: spaces gained an icon, a gradient preset and a last active
// tab, and the model gained a last active space.
//
// Unlike the step before it this one writes: a version 2 space has no
// `gradient` key at all, and a reader that defaults a missing key and a
// migration that writes the default disagree the moment the default changes.
// The last active tab and space are deliberately left absent — a file written
// before spaces existed has no honest answer, and both fall back to the first
// space.
bool MigrateV2ToV3(base::DictValue& dict) {
  base::ListValue* spaces = dict.FindList("spaces");
  if (!spaces) {
    return true;
  }
  for (base::Value& item : *spaces) {
    base::DictValue* space = item.GetIfDict();
    if (!space) {
      continue;
    }
    space->Set("icon", "");
    space->Set("gradient", 0);
  }
  return true;
}
```

and extend the table: `constexpr MigrationStep kSteps[] = {&MigrateV1ToV2, &MigrateV2ToV3};`.

- [ ] **Step 4: Serialise and deserialise the new fields**

In `SerializeModel`'s space loop, after `value.Set("name", ...)`:

```cpp
    value.Set("icon", base::UTF16ToUTF8(space.icon));
    value.Set("gradient", space.gradient);
    if (space.last_active_tab.is_valid()) {
      value.Set("last_active_tab", space.last_active_tab.value());
    }
```

and after `dict.Set("spaces", std::move(spaces));`:

```cpp
  dict.Set("last_active_space", model.last_active_space().value());
```

In `DeserializeModel`'s space loop, after the name:

```cpp
      const std::string* icon = value->FindString("icon");
      space.icon = icon ? base::UTF8ToUTF16(*icon) : std::u16string();
      space.gradient = value->FindInt("gradient").value_or(0);
      const std::string* last_tab = value->FindString("last_active_tab");
      space.last_active_tab =
          last_tab ? TabKey::FromString(*last_tab) : TabKey();
```

and after `ReplaceAll`, where the model already exists:

```cpp
  // After ReplaceAll, so the id is checked against the spaces that survived
  // parsing; last_active_space() falls back to the first space on its own
  // when this names one that did not.
  if (const std::string* active = dict.FindString("last_active_space")) {
    model->SetLastActiveSpace(SpaceId::FromString(*active));
  }
```

- [ ] **Step 5: Run the tests**

```bash
scripts/format && scripts/build dev arcium_unittests && /Volumes/Texternal/chromium/src/out/dev/arcium_unittests --gtest_filter=Model*
```

Expected: green.

- [ ] **Step 6: Mutation check**

Remove `space->Set("gradient", 0);` from `MigrateV2ToV3` and rebuild: `AVersionTwoSpaceGainsTheStageThreeDefaults` must fail. Restore it.

- [ ] **Step 7: Commit**

```bash
git add arcium/browser/model/model_serializer.h arcium/browser/model/model_serializer.cc arcium/browser/model/model_migration.cc arcium/test/model_serializer_unittest.cc arcium/test/model_migration_unittest.cc
git commit -m "The model file carries icons, gradients and where you were

Version 3. The migration writes the two defaults rather than leaving the
reader to supply them, because a defaulting reader and a silent migration
disagree the first time a default changes.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 3: Which space a tab is in, and saying so in the session file

**Files:**
- Modify: `arcium/browser/tab_space.h`, `arcium/browser/tab_space.cc`, `arcium/browser/entry_claim.h`, `arcium/browser/entry_claim.cc`, `arcium/browser/session_tab_entry.cc`
- Test: `arcium/test/tab_space_unittest.cc`, `arcium/test/session_tab_entry_unittest.cc`

**Interfaces:**
- Consumes: Task 1's `TabKey` and `Space`.
- Produces:
  - `kSpaceIdExtraDataKey = "arcium.space_id"`, `kTabKeyExtraDataKey = "arcium.tab_key"`
  - `SpaceId SpaceTagOf(content::WebContents*)`, `void SetSpaceTag(content::WebContents*, SpaceId)`
  - `TabKey KeyOf(content::WebContents*)` — generates on first ask, so every tab has one
  - `SpaceId SpaceOfTab(const ArciumModel&, const TabBinding&, tabs::TabHandle)`
  - `void RestoreTabSpaceData(content::WebContents*, const std::map<std::string, std::string>&)`
  - `void AppendTabSpaceCommands(sessions::CommandStorageManager*, SessionID, content::WebContents*, const ArciumModel&, const TabBinding&)`
  - `void PopulateTabSpaceExtraData(tabs::TabInterface*, const ArciumModel&, const TabBinding&, std::map<std::string, std::string>*)`
  - `IsClaimedByEntry` keeps its signature and loses its space clause.

No patch changes. All three session writers already call into `session_tab_entry.cc`, and that file calls these; hooking the same seams twice would be two lines upstream for nothing.

- [ ] **Step 1: Write the failing tests**

Replace the placeholder in `arcium/test/tab_space_unittest.cc` with a `BrowserWithTestWindowTest` fixture holding an `ArciumModel model_;` and a `TabBinding binding_;`, and:

```cpp
TEST_F(TabSpaceTest, ATabWithNoTagIsInTheFirstSpace) {
  AddTab(browser(), GURL("https://a.example/"));
  model_.AddSpace(u"Work");
  EXPECT_EQ(model_.default_space_id(),
            SpaceOfTab(model_, binding_, strip()->GetTabAtIndex(0)->GetHandle()));
}

TEST_F(TabSpaceTest, ATaggedTabIsInTheTaggedSpace) {
  AddTab(browser(), GURL("https://a.example/"));
  const SpaceId work = model_.AddSpace(u"Work");
  SetSpaceTag(strip()->GetTabAtIndex(0)->GetContents(), work);
  EXPECT_EQ(work,
            SpaceOfTab(model_, binding_, strip()->GetTabAtIndex(0)->GetHandle()));
}

TEST_F(TabSpaceTest, ATagNamingNoSpaceFallsBackToTheFirst) {
  AddTab(browser(), GURL("https://a.example/"));
  SetSpaceTag(strip()->GetTabAtIndex(0)->GetContents(), SpaceId::Generate());
  EXPECT_EQ(model_.default_space_id(),
            SpaceOfTab(model_, binding_, strip()->GetTabAtIndex(0)->GetHandle()));
}

// The entry wins over the tag, which is what lets "move a pin to another
// space" carry its open tab without touching the tab at all.
TEST_F(TabSpaceTest, AClaimedTabIsInItsEntrysSpaceWhateverItsTagSays) {
  AddTab(browser(), GURL("https://a.example/"));
  const SpaceId work = model_.AddSpace(u"Work");
  const EntryId id = model_.AddEntry(work, EntryKind::kPinned,
                                     GURL("https://a.example/"), u"A");
  tabs::TabInterface* tab = strip()->GetTabAtIndex(0);
  SetSpaceTag(tab->GetContents(), model_.default_space_id());
  binding_.Bind(id, tab->GetHandle());
  EXPECT_EQ(work, SpaceOfTab(model_, binding_, tab->GetHandle()));
}

TEST_F(TabSpaceTest, ATabsKeyIsGeneratedOnceAndKept) {
  AddTab(browser(), GURL("https://a.example/"));
  content::WebContents* contents = strip()->GetTabAtIndex(0)->GetContents();
  const TabKey first = KeyOf(contents);
  EXPECT_TRUE(first.is_valid());
  EXPECT_EQ(first, KeyOf(contents));
}

// A tab bound to an entry of another space is claimed. Stage 2's predicate
// said otherwise because one window only ever drew one space; a window now
// draws the space it is in, and SpaceOfTab is what decides where the tab is
// drawn.
TEST_F(TabSpaceTest, AnEntryOfAnotherSpaceStillClaimsItsTab) {
  AddTab(browser(), GURL("https://a.example/"));
  const SpaceId work = model_.AddSpace(u"Work");
  const EntryId id = model_.AddEntry(work, EntryKind::kPinned,
                                     GURL("https://a.example/"), u"A");
  tabs::TabInterface* tab = strip()->GetTabAtIndex(0);
  binding_.Bind(id, tab->GetHandle());
  EXPECT_TRUE(IsClaimedByEntry(model_, binding_, tab->GetHandle()));
}
```

In `session_tab_entry_unittest.cc`, beside the existing round-trip tests:

```cpp
TEST_F(SessionTabEntryTest, ARebuildWritesTheSpaceTagAndTheTabKey) {
  AddTab(browser(), GURL("https://a.example/"));
  const SpaceId work = state()->model()->AddSpace(u"Work");
  content::WebContents* contents = strip()->GetTabAtIndex(0)->GetContents();
  SetSpaceTag(contents, work);
  const TabKey key = KeyOf(contents);

  AppendTabEntryCommand(command_storage_manager(), SessionID::NewUnique(),
                        contents);
  const std::vector<std::string> payloads = PendingPayloads();
  EXPECT_TRUE(ContainsValue(payloads, work.value()));
  EXPECT_TRUE(ContainsValue(payloads, key.value()));
}

TEST_F(SessionTabEntryTest, AnInSessionCloseCarriesTheSpaceTagAndTheTabKey) {
  AddTab(browser(), GURL("https://a.example/"));
  const SpaceId work = state()->model()->AddSpace(u"Work");
  tabs::TabInterface* tab = strip()->GetTabAtIndex(0);
  SetSpaceTag(tab->GetContents(), work);
  const TabKey key = KeyOf(tab->GetContents());

  std::map<std::string, std::string> extra_data;
  PopulateTabEntryExtraData(tab, &extra_data);
  EXPECT_EQ(work.value(), extra_data[kSpaceIdExtraDataKey]);
  EXPECT_EQ(key.value(), extra_data[kTabKeyExtraDataKey]);
}

TEST_F(SessionTabEntryTest, ARestoredTabComesBackInItsOwnSpace) {
  AddTab(browser(), GURL("https://a.example/"));
  const SpaceId work = state()->model()->AddSpace(u"Work");
  const TabKey key = TabKey::Generate();
  content::WebContents* contents = strip()->GetTabAtIndex(0)->GetContents();

  StashRestoredEntryId(contents, {{kSpaceIdExtraDataKey, work.value()},
                                  {kTabKeyExtraDataKey, key.value()}});
  EXPECT_EQ(work, SpaceTagOf(contents));
  EXPECT_EQ(key, KeyOf(contents));
}
```

`PendingPayloads()` and `ContainsValue()` are two helpers to add to the fixture: the first maps `command_storage_manager()->pending_commands()` through the existing `PayloadOf`, the second is `std::ranges::any_of` over a substring search, because a session command's payload is a length-prefixed pickle and the id sits inside it.

- [ ] **Step 2: Run them and watch them fail**

```bash
/Volumes/Texternal/chromium/src/out/dev/arcium_unittests --gtest_filter=TabSpaceTest.*:SessionTabEntryTest.*
```

Expected: the `TabSpaceTest` file does not compile (`SpaceOfTab` undeclared), and `AnEntryOfAnotherSpaceStillClaimsItsTab` fails once it does, because `IsClaimedByEntry` still compares against `default_space_id()`.

- [ ] **Step 3: Write `tab_space.{h,cc}`**

The header declares the two keys and the seven functions listed under Interfaces. The source holds one `WebContentsUserData`:

```cpp
// The two facts a tab carries about itself. On the WebContents rather than
// on the TabInterface because the restore reads them in
// chrome::CreateRestoredTab, where the contents exists and the tab does not —
// the same gap RestoredEntryId parks in.
class TabSpaceData : public content::WebContentsUserData<TabSpaceData> {
 public:
  ~TabSpaceData() override = default;

  SpaceId space_tag;
  TabKey key;

 private:
  friend class content::WebContentsUserData<TabSpaceData>;
  explicit TabSpaceData(content::WebContents* web_contents)
      : content::WebContentsUserData<TabSpaceData>(*web_contents) {}

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

WEB_CONTENTS_USER_DATA_KEY_IMPL(TabSpaceData);
```

```cpp
SpaceId SpaceTagOf(content::WebContents* web_contents) {
  TabSpaceData* data = TabSpaceData::FromWebContents(web_contents);
  return data ? data->space_tag : SpaceId();
}

void SetSpaceTag(content::WebContents* web_contents, SpaceId id) {
  TabSpaceData::CreateForWebContents(web_contents);
  TabSpaceData::FromWebContents(web_contents)->space_tag = id;
}

TabKey KeyOf(content::WebContents* web_contents) {
  TabSpaceData::CreateForWebContents(web_contents);
  TabSpaceData* data = TabSpaceData::FromWebContents(web_contents);
  // Generated on first ask rather than at creation: there is no one seam
  // every tab passes through, and a key nothing ever reads costs a UUID.
  if (!data->key.is_valid()) {
    data->key = TabKey::Generate();
  }
  return data->key;
}

SpaceId SpaceOfTab(const ArciumModel& model,
                   const TabBinding& binding,
                   tabs::TabHandle handle) {
  // The entry's space first, and not as an optimisation: moving a pin to
  // another space must carry its open tab, and it does exactly because the
  // tab is never asked.
  if (const std::optional<EntryId> id = binding.EntryForTab(handle)) {
    if (const TabEntry* entry = model.GetEntry(*id)) {
      return entry->space_id;
    }
  }
  tabs::TabInterface* tab = handle.Get();
  content::WebContents* contents = tab ? tab->GetContents() : nullptr;
  const SpaceId tag = contents ? SpaceTagOf(contents) : SpaceId();
  // An unknown tag is a space that was deleted, or a session file from
  // another profile. Either way the tab is somewhere the user can see it.
  return model.GetSpace(tag) ? tag : model.default_space_id();
}
```

The three session helpers:

```cpp
void RestoreTabSpaceData(
    content::WebContents* web_contents,
    const std::map<std::string, std::string>& extra_data) {
  if (auto it = extra_data.find(kSpaceIdExtraDataKey); it != extra_data.end()) {
    // FromString refuses anything that is not a lowercase UUID, so a
    // hand-edited session file tags nothing rather than inventing an id.
    if (const SpaceId id = SpaceId::FromString(it->second); id.is_valid()) {
      SetSpaceTag(web_contents, id);
    }
  }
  if (auto it = extra_data.find(kTabKeyExtraDataKey); it != extra_data.end()) {
    if (const TabKey key = TabKey::FromString(it->second); key.is_valid()) {
      SetTabKey(web_contents, key);
    }
  }
}

void AppendTabSpaceCommands(
    sessions::CommandStorageManager* command_storage_manager,
    SessionID tab_id,
    content::WebContents* web_contents,
    const ArciumModel& model,
    const TabBinding& binding) {
  tabs::TabInterface* tab = tabs::TabInterface::MaybeGetFromContents(web_contents);
  if (!tab) {
    return;
  }
  // The resolved space, not the raw tag: a tab whose entry has since moved
  // spaces would otherwise come back where it used to be.
  command_storage_manager->AppendRebuildCommand(
      sessions::CreateAddTabExtraDataCommand(
          tab_id, kSpaceIdExtraDataKey,
          SpaceOfTab(model, binding, tab->GetHandle()).value()));
  command_storage_manager->AppendRebuildCommand(
      sessions::CreateAddTabExtraDataCommand(tab_id, kTabKeyExtraDataKey,
                                             KeyOf(web_contents).value()));
}
```

`PopulateTabSpaceExtraData` writes the same two values into the map it is handed.

- [ ] **Step 4: Drop the space clause from `IsClaimedByEntry`**

```cpp
bool IsClaimedByEntry(const ArciumModel& model,
                      const TabBinding& binding,
                      tabs::TabHandle handle) {
  const std::optional<EntryId> id = binding.EntryForTab(handle);
  return id.has_value() && model.GetEntry(*id) != nullptr;
}
```

Rewrite the header comment's third paragraph: an entry of another space claims its tab, and `SpaceOfTab` — not this predicate — decides which space draws it.

- [ ] **Step 5: Extend the three session writers**

In `session_tab_entry.cc`, `StashRestoredEntryId` calls `RestoreTabSpaceData(web_contents, extra_data)` before looking for the entry id, so a tab with no entry still gets its tag. `AppendTabEntryCommand` and `PopulateTabEntryExtraData` call their `tab_space` counterpart after the `ExistingStateFor` guard and before the `LiveEntryFor` early return, so the tag and key are written for every tab and the entry id only for a claimed one.

- [ ] **Step 6: Run the tests**

```bash
scripts/format && scripts/build dev arcium_unittests && /Volumes/Texternal/chromium/src/out/dev/arcium_unittests
```

Expected: green, including the existing `SidebarTabModelTest` suite — except `ATabClaimedByAnotherSpacesEntryIsStillATodayTab`, which encodes the rule just replaced. Task 5 replaces it; until then mark it `DISABLED_` with a one-line comment naming Task 5.

- [ ] **Step 7: Mutation check**

Make `SpaceOfTab` return the tag before consulting the binding: `AClaimedTabIsInItsEntrysSpaceWhateverItsTagSays` must fail. Restore. Remove the `model.GetSpace(tag)` guard: `ATagNamingNoSpaceFallsBackToTheFirst` must fail. Restore.

- [ ] **Step 8: Commit**

```bash
git add arcium/browser/tab_space.h arcium/browser/tab_space.cc arcium/browser/entry_claim.h arcium/browser/entry_claim.cc arcium/browser/session_tab_entry.cc arcium/test/tab_space_unittest.cc arcium/test/session_tab_entry_unittest.cc arcium/test/sidebar_tab_model_unittest.cc
git commit -m "A tab knows which space it is in, and says so to the session

The entry answers first, so moving a pin between spaces carries its open
tab without the tab being touched. The tag and the key ride the three
writers the entry id already rides, so no patch changes.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 4: `SpaceSwitcher`

**Files:**
- Modify: `arcium/ui/browser/space_switcher.h`, `arcium/ui/browser/space_switcher.cc`
- Test: `arcium/test/space_switcher_unittest.cc`

**Interfaces:**
- Consumes: `SpaceOfTab`, `SetSpaceTag`, `KeyOf`, `IsClaimedByEntry`, Task 1's model API.
- Produces:
  - `class SpaceSwitcher : public TabStripModelObserver, public ArciumModel::Observer`
  - `SpaceSwitcher(TabStripModel*, ArciumModel*, TabBinding*)`
  - `static SpaceSwitcher* FromTabStripModel(const TabStripModel*)`
  - `SpaceId active_space() const`, `void SwitchTo(SpaceId)`
  - `SpaceId SpaceOfTabAt(int index) const`, `bool IsInActiveSpace(int index) const`
  - `std::vector<int> OpenTabsInSidebarOrder(SpaceId) const`
  - `int OpenBlankTab()` — a blank tab in the active space, activated, index returned
  - `void MoveTabToSpace(int index, SpaceId)`
  - `class Observer { virtual void OnActiveSpaceChanged() = 0; }`, `AddObserver`, `RemoveObserver`

The registry exists because patch 0155's and 0160's hook targets are handed a `Browser*` or a `TabStripModel*` and nothing else. Reaching the switcher through `BrowserView` — the route `HandleNewTabCommand` takes — would make every command test need a real `BrowserView`, which `BrowserWithTestWindowTest` does not build.

- [ ] **Step 1: Write the failing tests**

In `space_switcher_unittest.cc`, a `BrowserWithTestWindowTest` fixture with `ArciumModel model_`, `TabBinding binding_`, a `MakeSwitcher()` and an `AddTabInSpace(url, space)` helper that adds a tab and tags it:

```cpp
TEST_F(SpaceSwitcherTest, TheWindowOpensOnTheSpaceThatWasActiveAtQuit) {
  const SpaceId work = model_.AddSpace(u"Work");
  model_.SetLastActiveSpace(work);
  auto switcher = MakeSwitcher();
  EXPECT_EQ(work, switcher->active_space());
}

TEST_F(SpaceSwitcherTest, ANewTabJoinsTheActiveSpace) {
  const SpaceId work = model_.AddSpace(u"Work");
  auto switcher = MakeSwitcher();
  switcher->SwitchTo(work);
  AddTab(browser(), GURL("https://a.example/"));
  EXPECT_EQ(work, switcher->SpaceOfTabAt(0));
}

TEST_F(SpaceSwitcherTest, ANewTabJoinsItsOpenersSpaceNotTheActiveOne) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  auto switcher = MakeSwitcher();
  AddTabInSpace(GURL("https://opener.example/"), first);
  switcher->SwitchTo(work);
  // What a Cmd+click does: an inserted tab whose strip opener is the tab it
  // came from, while another space is on screen.
  AddTabWithOpener(strip()->GetTabAtIndex(0));
  EXPECT_EQ(first, switcher->SpaceOfTabAt(1));
}

TEST_F(SpaceSwitcherTest, SwitchingRecordsWhereYouWereAndLandsWhereYouLeft) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  auto switcher = MakeSwitcher();
  // Interleaved, so a path that ignores spaces lands in the wrong one
  // rather than passing by luck.
  AddTabInSpace(GURL("https://a1.example/"), first);
  AddTabInSpace(GURL("https://w1.example/"), work);
  AddTabInSpace(GURL("https://a2.example/"), first);
  AddTabInSpace(GURL("https://w2.example/"), work);
  strip()->ActivateTabAt(2);

  switcher->SwitchTo(work);
  EXPECT_EQ(work, switcher->active_space());
  EXPECT_EQ(work, switcher->SpaceOfTabAt(strip()->active_index()));
  EXPECT_EQ(KeyOf(strip()->GetTabAtIndex(2)->GetContents()),
            model_.GetSpace(first)->last_active_tab);

  strip()->ActivateTabAt(3);
  switcher->SwitchTo(first);
  switcher->SwitchTo(work);
  EXPECT_EQ(3, strip()->active_index());
}

TEST_F(SpaceSwitcherTest, ActivatingAnotherSpacesTabSwitchesToIt) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  auto switcher = MakeSwitcher();
  AddTabInSpace(GURL("https://a1.example/"), first);
  AddTabInSpace(GURL("https://w1.example/"), work);
  ASSERT_EQ(first, switcher->active_space());
  strip()->ActivateTabAt(1);
  EXPECT_EQ(work, switcher->active_space());
  // And it did not move the selection it was told about.
  EXPECT_EQ(1, strip()->active_index());
}

TEST_F(SpaceSwitcherTest, SwitchingToAnEmptySpaceOpensOneBlankTab) {
  const SpaceId work = model_.AddSpace(u"Work");
  auto switcher = MakeSwitcher();
  AddTabInSpace(GURL("https://a1.example/"), model_.default_space_id());
  switcher->SwitchTo(work);
  EXPECT_EQ(2, strip()->count());
  EXPECT_EQ(work, switcher->SpaceOfTabAt(strip()->active_index()));
  EXPECT_TRUE(strip()->GetActiveTab()->GetContents()->GetVisibleURL().is_empty());
}

TEST_F(SpaceSwitcherTest, SidebarOrderIsFavouritesThenPinsThenTodayInStripOrder) {
  const SpaceId work = model_.AddSpace(u"Work");
  auto switcher = MakeSwitcher();
  AddTabInSpace(GURL("https://today1.example/"), work);
  AddTabInSpace(GURL("https://pin.example/"), work);
  AddTabInSpace(GURL("https://fav.example/"), work);
  AddTabInSpace(GURL("https://today2.example/"), work);
  const EntryId pin = model_.AddEntry(work, EntryKind::kPinned,
                                      GURL("https://pin.example/"), u"P");
  const EntryId fav = model_.AddEntry(work, EntryKind::kFavorite,
                                      GURL("https://fav.example/"), u"F");
  binding_.Bind(pin, strip()->GetTabAtIndex(1)->GetHandle());
  binding_.Bind(fav, strip()->GetTabAtIndex(2)->GetHandle());
  EXPECT_EQ(std::vector<int>({2, 1, 0, 3}),
            switcher->OpenTabsInSidebarOrder(work));
}

TEST_F(SpaceSwitcherTest, MovingATabToAnotherSpaceRetagsItAndFollowsIt) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  auto switcher = MakeSwitcher();
  AddTabInSpace(GURL("https://a1.example/"), first);
  strip()->ActivateTabAt(0);
  switcher->MoveTabToSpace(0, work);
  EXPECT_EQ(work, switcher->SpaceOfTabAt(0));
  // Moving the tab you are looking at takes you with it, as Zen does.
  EXPECT_EQ(work, switcher->active_space());
}
```

- [ ] **Step 2: Run them and watch them fail**

```bash
/Volumes/Texternal/chromium/src/out/dev/arcium_unittests --gtest_filter=SpaceSwitcherTest.*
```

Expected: does not compile — `SpaceSwitcher` is an empty namespace.

- [ ] **Step 3: Write the header**

```cpp
// One per window. Holds which space the window is showing, answers whether a
// tab belongs to it, and performs switches.
//
// Deliberately not a member of BrowserSidebarController's model: the two
// patches of this stage are handed a Browser or a TabStripModel and nothing
// else, and a per-strip registry is reachable from both without dragging
// BrowserView into a unit test.
class SpaceSwitcher : public TabStripModelObserver,
                      public ArciumModel::Observer {
 public:
  class Observer : public base::CheckedObserver {
   public:
    virtual void OnActiveSpaceChanged() = 0;
  };

  SpaceSwitcher(TabStripModel* tab_strip_model,
                ArciumModel* model,
                TabBinding* binding);
  SpaceSwitcher(const SpaceSwitcher&) = delete;
  SpaceSwitcher& operator=(const SpaceSwitcher&) = delete;
  ~SpaceSwitcher() override;

  // The switcher of the window `tab_strip_model` belongs to, or null in a
  // window built without a sidebar (--arcium-no-sidebar, and every browser
  // test that does not want one).
  static SpaceSwitcher* FromTabStripModel(const TabStripModel* tab_strip_model);

  SpaceId active_space() const { return active_space_; }
  // Records the current space's active tab, moves to `id`, and lands on that
  // space's last active tab — or its first open tab, or a new blank one.
  void SwitchTo(SpaceId id);
  SpaceId SpaceOfTabAt(int index) const;
  bool IsInActiveSpace(int index) const;
  // Open tabs of `space`, in the order the sidebar draws them: favourites,
  // then pinned entries by position, then the tabs no entry claims in strip
  // order. Cold entries have no tab and are not here; see ruling R1.
  std::vector<int> OpenTabsInSidebarOrder(SpaceId space) const;
  // A blank foreground tab in the active space. The strip index it landed at.
  int OpenBlankTab();
  void MoveTabToSpace(int index, SpaceId space);

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // TabStripModelObserver:
  void OnTabStripModelChanged(TabStripModel* tab_strip_model,
                              const TabStripModelChange& change,
                              const TabStripSelectionChange& selection) override;
  void OnTabStripModelDestroyed(TabStripModel* tab_strip_model) override;

  // ArciumModel::Observer:
  void OnArciumModelChanged() override;

 private:
  void TagInsertedTabs(const TabStripModelChange::Insert& insert);
  void RecordActiveTab();
  void NotifyActiveSpaceChanged();

  raw_ptr<TabStripModel> tab_strip_model_;
  raw_ptr<ArciumModel> model_;
  raw_ptr<TabBinding> binding_;
  SpaceId active_space_;
  // Set while SwitchTo activates a tab, so the activation it causes is not
  // read back as the user choosing a foreign tab.
  bool switching_ = false;
  base::ObserverList<Observer> observers_;
};
```

- [ ] **Step 4: Write the source**

The registry is a function-local static map keyed by `TabStripModel*`, written in the constructor and erased in the destructor. The constructor observes the strip and the model and takes its space from the model:

```cpp
SpaceSwitcher::SpaceSwitcher(TabStripModel* tab_strip_model,
                             ArciumModel* model,
                             TabBinding* binding)
    : tab_strip_model_(tab_strip_model),
      model_(model),
      binding_(binding),
      active_space_(model->last_active_space()) {
  Registry().emplace(tab_strip_model_, this);
  tab_strip_model_->AddObserver(this);
  model_->AddObserver(this);
}
```

`OnArciumModelChanged` is where a launch actually lands on the right space: the window is built before `ModelStore::Load` has run, so the space it starts in is the fresh model's only one.

```cpp
void SpaceSwitcher::OnArciumModelChanged() {
  // The model was loaded, or a space was deleted from another window. Either
  // way the space this window is showing may not exist any more, and the one
  // the user quit in may only now have arrived.
  if (!model_->GetSpace(active_space_)) {
    SwitchTo(model_->last_active_space());
  }
}
```

Switching:

```cpp
void SpaceSwitcher::SwitchTo(SpaceId id) {
  if (!model_->GetSpace(id) || !tab_strip_model_) {
    return;
  }
  if (id == active_space_) {
    return;
  }
  RecordActiveTab();
  active_space_ = id;
  model_->SetLastActiveSpace(id);

  // The tab to land on: the one left behind, then the space's first open tab,
  // then a new blank one. A space is never left showing another space's page,
  // which is the whole of §4.3's rule.
  base::AutoReset<bool> switching(&switching_, true);
  const std::vector<int> open = OpenTabsInSidebarOrder(id);
  int landing = -1;
  const TabKey last = model_->GetSpace(id)->last_active_tab;
  for (int index : open) {
    if (last.is_valid() &&
        KeyOf(tab_strip_model_->GetTabAtIndex(index)->GetContents()) == last) {
      landing = index;
      break;
    }
  }
  if (landing == -1 && !open.empty()) {
    landing = open.front();
  }
  if (landing == -1) {
    OpenBlankTab();
  } else {
    tab_strip_model_->ActivateTabAt(landing);
  }
  NotifyActiveSpaceChanged();
}

void SpaceSwitcher::RecordActiveTab() {
  const int index = tab_strip_model_->active_index();
  if (index == TabStripModel::kNoTab || !IsInActiveSpace(index)) {
    return;
  }
  model_->SetLastActiveTab(
      active_space_,
      KeyOf(tab_strip_model_->GetTabAtIndex(index)->GetContents()));
}
```

`OpenTabsInSidebarOrder` walks the model's entries for the space, keeping those whose bound tab is in this strip, then appends the unclaimed tabs of the space in strip order — mirroring `SidebarTabModel::rows()`, which is the order on screen.

The observer does two jobs:

```cpp
void SpaceSwitcher::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  if (change.type() == TabStripModelChange::kInserted) {
    if (const TabStripModelChange::Insert* insert = change.GetInsert()) {
      TagInsertedTabs(*insert);
    }
  }
  // §4.4: the sidebar never shows one space while the page belongs to
  // another. `switching_` keeps SwitchTo's own activation from coming back
  // through here as the user choosing a foreign tab.
  if (!switching_ && selection.active_tab_changed()) {
    const int index = tab_strip_model_->active_index();
    if (index != TabStripModel::kNoTab && !IsInActiveSpace(index)) {
      SwitchTo(SpaceOfTabAt(index));
      return;
    }
    RecordActiveTab();
  }
}

void SpaceSwitcher::TagInsertedTabs(
    const TabStripModelChange::Insert& insert) {
  for (const auto& inserted : insert.contents) {
    if (!inserted.tab) {
      continue;
    }
    content::WebContents* contents = inserted.tab->GetContents();
    // A restored tab arrives already tagged, and a tag is never overwritten:
    // the session's answer is older and better than "wherever you are now".
    if (SpaceTagOf(contents).is_valid()) {
      continue;
    }
    // The tab strip's opener, which a link click and a Cmd+click both set,
    // and window.opener does not decide. A tab from another application has
    // none and joins the space on screen.
    tabs::TabInterface* opener =
        tab_strip_model_->GetOpenerOfTabAt(inserted.index);
    SetSpaceTag(contents, opener ? SpaceOfTab(*model_, *binding_,
                                              opener->GetHandle())
                                 : active_space_);
  }
}
```

`OpenBlankTab` calls `tab_strip_model_->delegate()->AddTabAt(GURL(), -1, /*foreground=*/true)` — the insert callback tags it with the active space — and returns `tab_strip_model_->active_index()`. `MoveTabToSpace` sets the tag, and when the moved tab is the active one, calls `SwitchTo(space)`.

- [ ] **Step 5: Run the tests**

```bash
scripts/format && scripts/build dev arcium_unittests && /Volumes/Texternal/chromium/src/out/dev/arcium_unittests --gtest_filter=SpaceSwitcherTest.*
```

Expected: green.

- [ ] **Step 6: Mutation check**

Remove the `SpaceTagOf(contents).is_valid()` guard from `TagInsertedTabs`: `ANewTabJoinsItsOpenersSpaceNotTheActiveOne` still passes but `ARestoredTabComesBackInItsOwnSpace`'s sibling in Task 5 will not — so instead check this one by removing the opener branch, which must fail `ANewTabJoinsItsOpenersSpaceNotTheActiveOne`. Then remove `RecordActiveTab()` from `SwitchTo`, which must fail `SwitchingRecordsWhereYouWereAndLandsWhereYouLeft`. Restore both.

- [ ] **Step 7: Commit**

```bash
git add arcium/ui/browser/space_switcher.h arcium/ui/browser/space_switcher.cc arcium/test/space_switcher_unittest.cc
git commit -m "A window knows which space it is showing

The switcher tags what arrives, lands a switch on the tab you left, and
follows you when you activate a tab from somewhere else. It is reachable
from a bare TabStripModel because the two patches of this stage are handed
nothing richer.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 5: Every surface works in the window's space

**Files:**
- Modify: `arcium/ui/browser/sidebar_tab_model.h`, `arcium/ui/browser/sidebar_tab_model.cc`, `arcium/ui/browser/sidebar_tab_model_entries.cc`, `arcium/ui/browser/archive_service.h`, `arcium/ui/browser/archive_service.cc`, `arcium/ui/browser/tab_search_service.h`, `arcium/ui/browser/tab_search_service.cc`
- Test: `arcium/test/space_scoping_unittest.cc`, `arcium/test/sidebar_tab_model_unittest.cc` (one test replaced)

**Interfaces:**
- Consumes: `SpaceSwitcher::active_space()`, `SpaceOfTab`.
- Produces:
  - `SidebarTabModel(TabStripModel*, ArciumModel*, TabBinding*, SpaceSwitcher* switcher = nullptr)`
  - `ArchiveService(..., SpaceSwitcher* switcher = nullptr)` as a trailing argument after the clock
  - `TabSearchService(..., SpaceSwitcher* switcher = nullptr)`
  - a private `SpaceId active_space() const` on each, falling back to `default_space_id()` when there is no switcher

The switcher is optional so that the playground, the existing fixtures and any window built without a sidebar keep working unchanged: with no switcher every one of them behaves exactly as it does today, in the first space.

- [ ] **Step 1: Write the failing tests**

`space_scoping_unittest.cc` gets a fixture that builds a real `SpaceSwitcher`, a `SidebarTabModel` and an `ArchiveService` over one strip, with two spaces and interleaved tabs:

```cpp
TEST_F(SpaceScopingTest, RowsHoldOnlyTheActiveSpace) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  AddTabInSpace(GURL("https://a1.example/"), first);
  AddTabInSpace(GURL("https://w1.example/"), work);
  AddTabInSpace(GURL("https://a2.example/"), first);
  AddTabInSpace(GURL("https://w2.example/"), work);

  std::vector<SidebarRow> rows = sidebar_->rows();
  ASSERT_EQ(2u, rows.size());
  EXPECT_EQ(GURL("https://a1.example/"), rows[0].url);
  EXPECT_EQ(GURL("https://a2.example/"), rows[1].url);

  switcher_->SwitchTo(work);
  rows = sidebar_->rows();
  ASSERT_EQ(2u, rows.size());
  EXPECT_EQ(GURL("https://w1.example/"), rows[0].url);
  EXPECT_EQ(GURL("https://w2.example/"), rows[1].url);
}

TEST_F(SpaceScopingTest, APinnedEntryOfAnotherSpaceIsNotDrawnHere) {
  const SpaceId work = model_.AddSpace(u"Work");
  AddTabInSpace(GURL("https://w1.example/"), work);
  const EntryId id = model_.AddEntry(work, EntryKind::kPinned,
                                     GURL("https://w1.example/"), u"W");
  binding_.Bind(id, strip()->GetTabAtIndex(0)->GetHandle());
  EXPECT_TRUE(sidebar_->rows().empty());
  switcher_->SwitchTo(work);
  ASSERT_EQ(1u, sidebar_->rows().size());
  EXPECT_EQ(SidebarSection::kPinned, sidebar_->rows().front().section);
}

TEST_F(SpaceScopingTest, TheSidebarRebuildsWhenTheSpaceChanges) {
  const SpaceId work = model_.AddSpace(u"Work");
  AddTabInSpace(GURL("https://w1.example/"), work);
  CountingObserver observer;
  sidebar_->AddObserver(&observer);
  switcher_->SwitchTo(work);
  task_environment()->RunUntilIdle();
  EXPECT_GE(observer.count, 1);
  sidebar_->RemoveObserver(&observer);
}

TEST_F(SpaceScopingTest, ClearTodayLeavesOtherSpacesAlone) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  AddTabInSpace(GURL("https://a1.example/"), first);
  AddTabInSpace(GURL("https://w1.example/"), work);
  sidebar_->ClearToday();
  task_environment()->RunUntilIdle();
  ASSERT_EQ(1, strip()->count());
  EXPECT_EQ(work, switcher_->SpaceOfTabAt(0));
}

// D2-2 extended: a background space's landing tab is not archivable, or a
// switch to that space would arrive at a page that had been swept away.
TEST_F(SpaceScopingTest, AnotherSpacesLandingTabIsNeverArchived) {
  const SpaceId work = model_.AddSpace(u"Work");
  AddTabInSpace(GURL("https://w1.example/"), work);
  model_.SetLastActiveTab(
      work, KeyOf(strip()->GetTabAtIndex(0)->GetContents()));
  EXPECT_FALSE(archive_->MayArchive(strip()->GetTabAtIndex(0)->GetHandle()));
}

TEST_F(SpaceScopingTest, EachSpaceAgesAgainstItsOwnTimeout) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  model_.SetArchiveTimeout(first, ArchiveTimeout::kTwelveHours);
  model_.SetArchiveTimeout(work, ArchiveTimeout::kNever);
  AddTabInSpace(GURL("https://a1.example/"), first);
  AddTabInSpace(GURL("https://w1.example/"), work);
  strip()->ActivateTabAt(0);
  task_environment()->FastForwardBy(base::Hours(13));
  // The first space's idle tab went; the never-archived space's stayed.
  ASSERT_EQ(1, strip()->count());
  EXPECT_EQ(work, switcher_->SpaceOfTabAt(0));
}
```

Replace `SidebarTabModelTest.ATabClaimedByAnotherSpacesEntryIsStillATodayTab` — disabled in Task 3 — with the rule that took its place, keeping its explanatory comment rewritten:

```cpp
// Stage 2 drew one space, so a tab bound to another space's entry was
// nowhere: claimed, and drawn by no section. Stage 3a gave the window a
// space of its own, and the answer changed rather than the bug being fixed —
// the tab is claimed, and it is drawn when its own space is on screen.
TEST_F(SidebarTabModelTest, ATabClaimedByAnotherSpacesEntryIsNotDrawnHere) {
  // …the same setup, then:
  EXPECT_TRUE(model->rows().empty());
}
```

- [ ] **Step 2: Run them and watch them fail**

```bash
/Volumes/Texternal/chromium/src/out/dev/arcium_unittests --gtest_filter=SpaceScopingTest.*:SidebarTabModelTest.*
```

Expected: the scoping fixture does not compile — the three services take no switcher.

- [ ] **Step 3: Give the three services a switcher**

Each gains a trailing `SpaceSwitcher* switcher = nullptr` constructor argument, a `raw_ptr<SpaceSwitcher> switcher_` and:

```cpp
// The space this window is showing. Without a switcher — the playground, a
// window built with no sidebar, every fixture written before spaces — it is
// the first space, which is what those callers have always meant.
SpaceId SidebarTabModel::active_space() const {
  return switcher_ ? switcher_->active_space() : arcium_model_->default_space_id();
}
```

`SidebarTabModel` also implements `SpaceSwitcher::Observer` so a switch rebuilds the sidebar:

```cpp
void SidebarTabModel::OnActiveSpaceChanged() {
  NotifyChanged();
}
```

registering in the constructor when the switcher is non-null and unregistering in the destructor.

- [ ] **Step 4: Replace the eleven `default_space_id()` uses**

| File and line | Becomes |
|---|---|
| `sidebar_tab_model.cc:114` (`rows`) | `active_space()`, and the Today loop additionally skips a tab whose `SpaceOfTab` is not `active_space()` |
| `sidebar_tab_model.cc:240` (`SetArchiveTimeout`) | `active_space()` |
| `sidebar_tab_model.cc:247` (`archive_timeout`) | `active_space()` |
| `sidebar_tab_model.cc:283` (`RequestArchivedRows`) | `active_space()` |
| `sidebar_tab_model.cc:407` (`RequestColdFavicons`) | `active_space()` |
| `sidebar_tab_model_entries.cc:392`, `:520` | `active_space()` |
| `sidebar_tab_model_entries.cc` `AddEntryForTab`, `CreateFolderWithEntry` | `AddEntry(active_space(), …)`, `AddFolder(active_space(), …)`; delete the Task 1 markers |
| `archive_service.cc:341` (`TimeoutForDefaultSpace`) | renamed `TimeoutForTab(tabs::TabHandle)`, reading `SpaceOfTab`'s space |
| `archive_service.cc:442` (`MakeRow`) | `SpaceOfTab(*model_, *binding_, tab->GetHandle())` |
| `tab_search_service.cc:168` | `active_space()` |

The tab-walking loops that mean "this space's Today tabs" — `ClearToday` (`sidebar_tab_model.cc:197`), `TodayStripIndexForPosition` (`:210`) and `ArchiveService::ArchiveAllToday` — gain the same space test beside their existing claim test.

`MayArchive` gains one refusal, D2-2 extended across spaces:

```cpp
  // Never the tab a switch to that space would land on. The window's own
  // active tab is refused above; this is the same rule for the spaces that
  // are not on screen, and without it a quiet background space loses the
  // page it was left on.
  for (const Space& space : model_->spaces()) {
    if (space.last_active_tab.is_valid() && contents &&
        space.last_active_tab == KeyOf(contents)) {
      return false;
    }
  }
```

- [ ] **Step 5: Run the whole suite**

```bash
scripts/format && scripts/build dev arcium_unittests && /Volumes/Texternal/chromium/src/out/dev/arcium_unittests
```

Expected: green. Every pre-existing fixture builds its services without a switcher and is unaffected.

- [ ] **Step 6: Mutation check**

Drop the space test from `rows()`'s Today loop: `RowsHoldOnlyTheActiveSpace` must fail. Restore. Drop the `last_active_tab` refusal from `MayArchive`: `AnotherSpacesLandingTabIsNeverArchived` must fail. Restore.

- [ ] **Step 7: Commit**

```bash
git add arcium/ui/browser/sidebar_tab_model.h arcium/ui/browser/sidebar_tab_model.cc arcium/ui/browser/sidebar_tab_model_entries.cc arcium/ui/browser/archive_service.h arcium/ui/browser/archive_service.cc arcium/ui/browser/tab_search_service.h arcium/ui/browser/tab_search_service.cc arcium/test/space_scoping_unittest.cc arcium/test/sidebar_tab_model_unittest.cc
git commit -m "The sidebar, the archive and search work in the window's space

The switcher is optional everywhere: without one each service behaves
exactly as it did, in the first space, which is what the playground and
every fixture written before spaces meant by it.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 6: Deleting a space, and moving things between spaces

**Files:**
- Modify: `arcium/browser/archive_store.h`, `arcium/browser/archive_store.cc`, `arcium/ui/browser/archive_service.h`, `arcium/ui/browser/archive_service.cc`, `arcium/ui/browser/space_switcher.h`, `arcium/ui/browser/space_switcher.cc`
- Test: `arcium/test/archive_store_unittest.cc`, `arcium/test/space_switcher_unittest.cc`

**Interfaces:**
- Consumes: Task 4's switcher, Task 1's `RemoveSpace`, `MoveEntryToSpace`.
- Produces:
  - `void ArchiveStore::RemoveSpace(SpaceId)`
  - `void ArchiveService::RemoveSpaceRows(SpaceId)` — posts to the store sequence
  - `void SpaceSwitcher::SetArchiveService(ArchiveService*)`
  - `void SpaceSwitcher::DeleteSpace(SpaceId)`
  - `int SpaceSwitcher::OpenTabCount(SpaceId) const` — what the confirmation counts

- [ ] **Step 1: Write the failing tests**

In `archive_store_unittest.cc`:

```cpp
TEST_F(ArchiveStoreTest, RemovingASpaceTakesOnlyItsRows) {
  const SpaceId other = SpaceId::Generate();
  const base::Time now = base::Time::Now();
  store_.Add(MakeTab("https://mine.example/", u"Mine", now));
  ArchivedTab theirs = MakeTab("https://theirs.example/", u"Theirs", now);
  theirs.space_id = other;
  store_.Add(theirs);

  store_.RemoveSpace(space_);
  EXPECT_TRUE(store_.ListRecent(space_, 10).empty());
  EXPECT_EQ(1u, store_.ListRecent(other, 10).size());
}
```

In `space_switcher_unittest.cc`:

```cpp
TEST_F(SpaceSwitcherTest, DeletingASpaceClosesItsTabsAndTakesItsEntries) {
  const SpaceId first = model_.default_space_id();
  const SpaceId doomed = model_.AddSpace(u"Doomed");
  auto switcher = MakeSwitcher();
  AddTabInSpace(GURL("https://a1.example/"), first);
  AddTabInSpace(GURL("https://d1.example/"), doomed);
  model_.AddEntry(doomed, EntryKind::kPinned, GURL("https://d2.example/"), u"D");
  switcher->SwitchTo(doomed);

  switcher->DeleteSpace(doomed);
  EXPECT_FALSE(model_.GetSpace(doomed));
  EXPECT_TRUE(model_.entries().empty());
  ASSERT_EQ(1, strip()->count());
  EXPECT_EQ(first, switcher->SpaceOfTabAt(0));
  // The window moved to the neighbour before the space went, so it is never
  // showing a space that does not exist.
  EXPECT_EQ(first, switcher->active_space());
}

// R2: a tab held open by beforeunload lands in the space the window moved
// to, rather than staying tagged with one that is gone.
TEST_F(SpaceSwitcherTest, ATabThatSurvivesTheDeleteJoinsTheLandingSpace) {
  const SpaceId first = model_.default_space_id();
  const SpaceId doomed = model_.AddSpace(u"Doomed");
  auto switcher = MakeSwitcher();
  AddTabInSpace(GURL("https://a1.example/"), first);
  AddTabInSpace(GURL("https://d1.example/"), doomed);
  HoldTabOpen(strip()->GetTabAtIndex(1));  // refuses to close, as a dialog does
  switcher->DeleteSpace(doomed);
  ASSERT_EQ(2, strip()->count());
  EXPECT_EQ(first, switcher->SpaceOfTabAt(1));
}

TEST_F(SpaceSwitcherTest, TheLastSpaceCannotBeDeleted) {
  auto switcher = MakeSwitcher();
  AddTabInSpace(GURL("https://a1.example/"), model_.default_space_id());
  switcher->DeleteSpace(model_.default_space_id());
  EXPECT_EQ(1u, model_.spaces().size());
  EXPECT_EQ(1, strip()->count());
}

TEST_F(SpaceSwitcherTest, OpenTabCountIsWhatTheConfirmationPromises) {
  const SpaceId work = model_.AddSpace(u"Work");
  auto switcher = MakeSwitcher();
  AddTabInSpace(GURL("https://a1.example/"), model_.default_space_id());
  AddTabInSpace(GURL("https://w1.example/"), work);
  AddTabInSpace(GURL("https://w2.example/"), work);
  EXPECT_EQ(2, switcher->OpenTabCount(work));
}
```

`HoldTabOpen` installs a `content::WebContents` unload state the test window will not close through — use `TestWebContents::SetPageIsWaitingForBeforeUnload` if reachable; otherwise close through a `TabStripModelDelegate` stub whose `RunUnloadListenerBeforeClosing` returns true. Whichever seam the fixture can reach, the assertion is the same: a tab that did not close is re-tagged.

- [ ] **Step 2: Run them and watch them fail**

```bash
/Volumes/Texternal/chromium/src/out/dev/arcium_unittests --gtest_filter=ArchiveStoreTest.*:SpaceSwitcherTest.*
```

Expected: `RemoveSpace` and `DeleteSpace` are undeclared.

- [ ] **Step 3: Delete a space's archive rows on the archive's own sequence**

```cpp
void ArchiveStore::RemoveSpace(SpaceId space_id) {
  if (!open_) {
    return;
  }
  sql::Statement statement(
      db_.GetCachedStatement(SQL_FROM_HERE,
                             "DELETE FROM archived_tabs WHERE space_id = ?"));
  statement.BindString(0, space_id.value());
  statement.Run();
}
```

```cpp
void ArchiveService::RemoveSpaceRows(SpaceId space_id) {
  if (!store_ || !store_runner_) {
    return;
  }
  // On the store's sequence, like every other write here: this can be
  // thousands of rows and the UI thread never waits for SQLite.
  store_runner_->PostTask(
      FROM_HERE, base::BindOnce(&ArchiveStore::RemoveSpace,
                                base::Unretained(store_), space_id));
}
```

- [ ] **Step 4: Write `SpaceSwitcher::DeleteSpace`**

```cpp
void SpaceSwitcher::DeleteSpace(SpaceId id) {
  // The last space cannot go: every tab has to be in one. The model refuses
  // it too; refusing here as well keeps the tabs from being closed first.
  if (model_->spaces().size() <= 1 || !model_->GetSpace(id) ||
      !tab_strip_model_) {
    return;
  }
  // Move off it before it goes, so the window is never showing a space the
  // model no longer has.
  if (id == active_space_) {
    SwitchTo(NeighbourOf(id));
  }
  const SpaceId landing = active_space_;

  // Chromium's own close, so a page with unsaved work still gets its
  // beforeunload prompt.
  for (int index = tab_strip_model_->count() - 1; index >= 0; --index) {
    if (SpaceOfTabAt(index) == id) {
      tab_strip_model_->CloseWebContentsAt(index, kUserCloseTypes);
    }
  }
  model_->RemoveSpace(id);
  // Ruling R2. A tab still here refused to close — a beforeunload dialog the
  // user has not answered — and Chromium offers no signal for that at this
  // seam. Rather than leaving it tagged with a space that is gone, where
  // SpaceOfTab would silently move it to the first space, it joins the space
  // the window moved to and the user can see it.
  for (int index = 0; index < tab_strip_model_->count(); ++index) {
    content::WebContents* contents =
        tab_strip_model_->GetTabAtIndex(index)->GetContents();
    if (SpaceTagOf(contents) == id) {
      SetSpaceTag(contents, landing);
    }
  }
  if (archive_service_) {
    archive_service_->RemoveSpaceRows(id);
  }
  NotifyActiveSpaceChanged();
}
```

`NeighbourOf` returns the space after `id` in position order, or the one before it when `id` is last. `OpenTabCount` counts strip indices whose `SpaceOfTabAt` matches.

- [ ] **Step 5: Run the tests**

```bash
scripts/format && scripts/build dev arcium_unittests && /Volumes/Texternal/chromium/src/out/dev/arcium_unittests --gtest_filter=ArchiveStoreTest.*:SpaceSwitcherTest.*:SpaceScopingTest.*
```

Expected: green.

- [ ] **Step 6: Mutation check**

Remove the re-tagging loop: `ATabThatSurvivesTheDeleteJoinsTheLandingSpace` must fail. Remove the `SwitchTo(NeighbourOf(id))`: `DeletingASpaceClosesItsTabsAndTakesItsEntries` must fail on the active-space assertion. Restore both.

- [ ] **Step 7: Commit**

```bash
git add arcium/browser/archive_store.h arcium/browser/archive_store.cc arcium/ui/browser/archive_service.h arcium/ui/browser/archive_service.cc arcium/ui/browser/space_switcher.h arcium/ui/browser/space_switcher.cc arcium/test/archive_store_unittest.cc arcium/test/space_switcher_unittest.cc
git commit -m "Deleting a space destroys it, and says where survivors went

Arcium's one destructive delete (D3-1). A tab held open by beforeunload
joins the space the window moved to rather than keeping a tag nothing can
resolve, because Chromium gives no cancel signal at this seam.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 7: The spaces API the views need

**Files:**
- Modify: `arcium/ui/sidebar/sidebar_model.h`, `arcium/ui/browser/sidebar_tab_model.h`, `arcium/ui/browser/sidebar_tab_model_spaces.cc`, `arcium/ui/playground/fake_sidebar_model.h`, `arcium/ui/playground/fake_sidebar_spaces.cc`
- Test: `arcium/test/space_bar_unittest.cc`

**Interfaces:**
- Consumes: `SpaceSwitcher`, Task 1's model API, Task 6's `DeleteSpace` and `OpenTabCount`.
- Produces, on `SidebarModel` and both implementations:

```cpp
// One space as the bar draws it. Prepared by the model, like SidebarRow: a
// dot must not scan tabs to say how many a delete would take.
struct SidebarSpace {
  SpaceId id;
  std::u16string name;
  // Empty means draw the first letter of the name.
  std::u16string icon;
  int gradient = 0;
  bool is_active = false;
  // What the delete confirmation promises, counted where the strip is.
  int open_tab_count = 0;
  int entry_count = 0;
};

virtual std::vector<SidebarSpace> spaces() const = 0;
virtual void SwitchToSpace(SpaceId id) = 0;
virtual void AddSpace(const std::u16string& name) = 0;
virtual void RenameSpace(SpaceId id, const std::u16string& name) = 0;
virtual void SetSpaceIcon(SpaceId id, const std::u16string& icon) = 0;
virtual void SetSpaceGradient(SpaceId id, int gradient) = 0;
virtual void MoveSpace(SpaceId id, int position) = 0;
virtual void DeleteSpace(SpaceId id) = 0;
virtual void MoveTabToSpace(int tab_index, SpaceId space_id) = 0;
virtual void MoveEntryToSpace(EntryId id, SpaceId space_id) = 0;
```

`SetArchiveTimeout` and `archive_timeout()` keep their signatures and their meaning — the active space — which Task 5 already made true.

- [ ] **Step 1: Write the failing tests**

Replace the placeholder in `space_bar_unittest.cc` with a `ViewsTestBase` fixture over `FakeSidebarModel`, and start with the model half, which is what this task delivers:

```cpp
TEST_F(SpaceBarTest, TheFakeReportsSpacesWithTheActiveOneMarked) {
  FakeSidebarModel model;
  model.AddSpaceForTesting(u"Work", u"💼", 2);
  ASSERT_EQ(2u, model.spaces().size());
  EXPECT_TRUE(model.spaces()[0].is_active);
  EXPECT_EQ(u"Work", model.spaces()[1].name);
  EXPECT_EQ(u"💼", model.spaces()[1].icon);
  EXPECT_EQ(2, model.spaces()[1].gradient);
}

TEST_F(SpaceBarTest, SwitchingTheFakeMovesTheActiveMarkAndNotifies) {
  FakeSidebarModel model;
  CountingObserver observer;
  model.AddObserver(&observer);
  model.AddSpaceForTesting(u"Work", u"", 0);
  model.SwitchToSpace(model.spaces()[1].id);
  EXPECT_TRUE(model.spaces()[1].is_active);
  EXPECT_FALSE(model.spaces()[0].is_active);
  EXPECT_GE(observer.count, 1);
  model.RemoveObserver(&observer);
}

TEST_F(SpaceBarTest, TheFakeRefusesToDeleteItsLastSpace) {
  FakeSidebarModel model;
  model.DeleteSpace(model.spaces().front().id);
  EXPECT_EQ(1u, model.spaces().size());
}
```

and in `space_scoping_unittest.cc`, the browser half over the real model:

```cpp
TEST_F(SpaceScopingTest, TheSidebarModelReportsAndSwitchesSpaces) {
  const SpaceId work = model_.AddSpace(u"Work");
  AddTabInSpace(GURL("https://w1.example/"), work);
  AddTabInSpace(GURL("https://w2.example/"), work);
  model_.AddEntry(work, EntryKind::kPinned, GURL("https://w3.example/"), u"W3");

  const std::vector<SidebarSpace> spaces = sidebar_->spaces();
  ASSERT_EQ(2u, spaces.size());
  EXPECT_TRUE(spaces[0].is_active);
  EXPECT_EQ(work, spaces[1].id);
  EXPECT_EQ(2, spaces[1].open_tab_count);
  EXPECT_EQ(1, spaces[1].entry_count);

  sidebar_->SwitchToSpace(work);
  EXPECT_EQ(work, switcher_->active_space());
}
```

- [ ] **Step 2: Run them and watch them fail**

```bash
/Volumes/Texternal/chromium/src/out/dev/arcium_unittests --gtest_filter=SpaceBarTest.*:SpaceScopingTest.TheSidebarModelReportsAndSwitchesSpaces
```

Expected: `spaces()` is undeclared on both models.

- [ ] **Step 3: Declare the API on `SidebarModel`**

Add `struct SidebarSpace` above the class and the ten pure virtuals in a block of their own, each with the one-line reason it exists, following the file's existing voice.

- [ ] **Step 4: Implement it in `sidebar_tab_model_spaces.cc`**

```cpp
std::vector<SidebarSpace> SidebarTabModel::spaces() const {
  std::vector<SidebarSpace> result;
  const SpaceId active = active_space();
  for (const Space& space : arcium_model_->spaces()) {
    SidebarSpace out;
    out.id = space.id;
    out.name = space.name;
    out.icon = space.icon;
    out.gradient = space.gradient;
    out.is_active = space.id == active;
    // Counted here rather than by the bar: the strip is this object's, and a
    // dot that walked it would be a view reaching for the browser.
    out.open_tab_count = switcher_ ? switcher_->OpenTabCount(space.id) : 0;
    out.entry_count =
        static_cast<int>(arcium_model_->EntriesForKind(space.id, EntryKind::kFavorite).size() +
                         arcium_model_->EntriesForKind(space.id, EntryKind::kPinned).size());
    result.push_back(std::move(out));
  }
  return result;
}

void SidebarTabModel::SwitchToSpace(SpaceId id) {
  if (switcher_) {
    switcher_->SwitchToSpace(id);
  }
}

void SidebarTabModel::AddSpace(const std::u16string& name) {
  const SpaceId id = arcium_model_->AddSpace(name);
  // A new space is one you are put into: it is empty, so the switch opens
  // its blank tab and the quick entry over it, which is where a new space
  // starts from.
  if (switcher_) {
    switcher_->SwitchTo(id);
  }
}
```

`RenameSpace`, `SetSpaceIcon`, `SetSpaceGradient` and `MoveSpace` forward straight to the model. `DeleteSpace` and `MoveTabToSpace` forward to the switcher when there is one. `MoveEntryToSpace` calls `arcium_model_->MoveEntryToSpace` and, when the entry's tab is the active one, `switcher_->SwitchTo(space_id)` — moving what you are looking at takes you with it, as Zen does.

- [ ] **Step 5: Implement it in `fake_sidebar_spaces.cc`**

The fake holds `std::vector<SidebarSpace> spaces_` with one space marked active in its constructor, plus `AddSpaceForTesting(name, icon, gradient)` for seeding. Every command mutates that vector and calls `Notify()`; `DeleteSpace` refuses the last one; `MoveTabToSpace` and `MoveEntryToSpace` drop the row from `rows_` the way the real one drops it from the space being drawn. `open_tab_count` counts the fake's own rows tagged with the space, and the tag is a new `SpaceId space` field on the fake's row record, defaulted to the first space so every existing playground and view test is unchanged.

- [ ] **Step 6: Run the tests**

```bash
scripts/format && scripts/build dev arcium_unittests && /Volumes/Texternal/chromium/src/out/dev/arcium_unittests
```

Expected: green, including all 2,109 lines of `sidebar_views_unittest.cc`, which never mentions spaces.

- [ ] **Step 7: Mutation check**

Make `spaces()` mark every space active: `TheSidebarModelReportsAndSwitchesSpaces` must fail. Restore.

- [ ] **Step 8: Commit**

```bash
git add arcium/ui/sidebar/sidebar_model.h arcium/ui/browser/sidebar_tab_model.h arcium/ui/browser/sidebar_tab_model_spaces.cc arcium/ui/playground/fake_sidebar_model.h arcium/ui/playground/fake_sidebar_model.cc arcium/ui/playground/fake_sidebar_spaces.cc arcium/test/space_bar_unittest.cc arcium/test/space_scoping_unittest.cc
git commit -m "The views can ask for the spaces and switch between them

Counts come prepared, like every other thing a row draws: a dot that
walked the tab strip to say what a delete would take would be a view
reaching into the browser.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 8: Strip-wide commands stay in the space — `patches/0155-tab-commands-space.patch`

**Files:**
- Modify: `arcium/ui/browser/tab_commands.h`, `arcium/ui/browser/tab_commands.cc`, `arcium/ui/browser/space_switcher.h`, `arcium/ui/browser/space_switcher.cc`
- Create: `patches/0155-tab-commands-space.patch`
- Test: `arcium/test/tab_commands_unittest.cc`

**Interfaces:**
- Consumes: `SpaceSwitcher::FromTabStripModel`, `OpenTabsInSidebarOrder`, `OpenBlankTab`, `IsInActiveSpace`.
- Produces:
  - `bool arcium::HandleTabCommand(Browser* browser, int command_id)` — false for anything it does not own
  - `void SpaceSwitcher::SetBlankTabCallback(base::RepeatingClosure)` — run after `OpenBlankTab`; Task 12 points it at the quick entry

- [ ] **Step 1: Write the failing tests**

`tab_commands_unittest.cc` uses the same interleaved fixture as Task 4 and calls `HandleTabCommand` directly, which is what the patch does:

```cpp
TEST_F(TabCommandsTest, NextAndPreviousStayInTheSpaceAndWrap) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  auto switcher = MakeSwitcher();
  AddTabInSpace(GURL("https://a1.example/"), first);   // 0
  AddTabInSpace(GURL("https://w1.example/"), work);    // 1
  AddTabInSpace(GURL("https://a2.example/"), first);   // 2
  AddTabInSpace(GURL("https://w2.example/"), work);    // 3
  strip()->ActivateTabAt(0);

  EXPECT_TRUE(HandleTabCommand(browser(), IDC_SELECT_NEXT_TAB));
  EXPECT_EQ(2, strip()->active_index());
  EXPECT_TRUE(HandleTabCommand(browser(), IDC_SELECT_NEXT_TAB));
  EXPECT_EQ(0, strip()->active_index());
  EXPECT_TRUE(HandleTabCommand(browser(), IDC_SELECT_PREVIOUS_TAB));
  EXPECT_EQ(2, strip()->active_index());
}

TEST_F(TabCommandsTest, CommandDigitsCountTheSpacesOwnTabs) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  auto switcher = MakeSwitcher();
  AddTabInSpace(GURL("https://w1.example/"), work);   // 0
  AddTabInSpace(GURL("https://a1.example/"), first);  // 1
  AddTabInSpace(GURL("https://w2.example/"), work);   // 2
  switcher->SwitchTo(work);

  EXPECT_TRUE(HandleTabCommand(browser(), IDC_SELECT_TAB_1));
  EXPECT_EQ(2, strip()->active_index());
  EXPECT_TRUE(HandleTabCommand(browser(), IDC_SELECT_LAST_TAB));
  EXPECT_EQ(2, strip()->active_index());
  // A digit past the space's last tab does nothing rather than reaching
  // into another space.
  EXPECT_TRUE(HandleTabCommand(browser(), IDC_SELECT_TAB_7));
  EXPECT_EQ(2, strip()->active_index());
}

TEST_F(TabCommandsTest, MovingATodayTabSkipsOtherSpacesTabs) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  auto switcher = MakeSwitcher();
  AddTabInSpace(GURL("https://a1.example/"), first);  // 0
  AddTabInSpace(GURL("https://w1.example/"), work);   // 1
  AddTabInSpace(GURL("https://a2.example/"), first);  // 2
  strip()->ActivateTabAt(0);
  EXPECT_TRUE(HandleTabCommand(browser(), IDC_MOVE_TAB_NEXT));
  // Past the foreign tab, not into its place.
  EXPECT_EQ(2, strip()->active_index());
  EXPECT_EQ(GURL("https://a1.example/"),
            strip()->GetTabAtIndex(2)->GetContents()->GetVisibleURL());
}

TEST_F(TabCommandsTest, ClosingTheSpacesLastTabLeavesABlankOneBehind) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  auto switcher = MakeSwitcher();
  AddTabInSpace(GURL("https://a1.example/"), first);
  AddTabInSpace(GURL("https://w1.example/"), work);
  switcher->SwitchTo(work);

  EXPECT_TRUE(HandleTabCommand(browser(), IDC_CLOSE_TAB));
  EXPECT_EQ(2, strip()->count());
  EXPECT_EQ(work, switcher->SpaceOfTabAt(strip()->active_index()));
  EXPECT_TRUE(strip()->GetActiveTab()->GetContents()->GetVisibleURL().is_empty());
}

TEST_F(TabCommandsTest, ClosingWhenTheSpaceHasOthersIsChromiumsOwnClose) {
  const SpaceId work = model_.AddSpace(u"Work");
  auto switcher = MakeSwitcher();
  AddTabInSpace(GURL("https://w1.example/"), work);
  AddTabInSpace(GURL("https://w2.example/"), work);
  switcher->SwitchTo(work);
  EXPECT_FALSE(HandleTabCommand(browser(), IDC_CLOSE_TAB));
  EXPECT_EQ(2, strip()->count());
}

TEST_F(TabCommandsTest, CloseOthersAndCloseToTheRightSpareOtherSpaces) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  auto switcher = MakeSwitcher();
  AddTabInSpace(GURL("https://w1.example/"), work);   // 0
  AddTabInSpace(GURL("https://a1.example/"), first);  // 1
  AddTabInSpace(GURL("https://w2.example/"), work);   // 2
  AddTabInSpace(GURL("https://w3.example/"), work);   // 3
  switcher->SwitchTo(work);
  strip()->ActivateTabAt(0);

  EXPECT_TRUE(HandleTabCommand(browser(), IDC_WINDOW_CLOSE_TABS_TO_RIGHT));
  ASSERT_EQ(2, strip()->count());
  EXPECT_EQ(first, switcher->SpaceOfTabAt(1));
}

TEST_F(TabCommandsTest, AnUnownedCommandIsRefused) {
  auto switcher = MakeSwitcher();
  AddTabInSpace(GURL("https://a1.example/"), model_.default_space_id());
  EXPECT_FALSE(HandleTabCommand(browser(), IDC_RELOAD));
}

// A window with no sidebar has no switcher, and every command is Chromium's.
TEST_F(TabCommandsTest, WithoutASwitcherEveryCommandIsRefused) {
  AddTab(browser(), GURL("https://a1.example/"));
  EXPECT_FALSE(HandleTabCommand(browser(), IDC_SELECT_NEXT_TAB));
}
```

- [ ] **Step 2: Run them and watch them fail**

```bash
/Volumes/Texternal/chromium/src/out/dev/arcium_unittests --gtest_filter=TabCommandsTest.*
```

Expected: `HandleTabCommand` is undeclared.

- [ ] **Step 3: Write `tab_commands.cc`**

```cpp
bool HandleTabCommand(Browser* browser, int command_id) {
  TabStripModel* strip = browser ? browser->tab_strip_model() : nullptr;
  SpaceSwitcher* switcher =
      strip ? SpaceSwitcher::FromTabStripModel(strip) : nullptr;
  // No sidebar, no spaces, and every one of these is Chromium's own. The
  // same shape HandleNewTabCommand has, for the same reason.
  if (!switcher) {
    return false;
  }
  const std::vector<int> order =
      switcher->OpenTabsInSidebarOrder(switcher->active_space());
  switch (command_id) {
    case IDC_SELECT_NEXT_TAB:
      return SelectRelative(strip, order, 1);
    case IDC_SELECT_PREVIOUS_TAB:
      return SelectRelative(strip, order, -1);
    case IDC_SELECT_TAB_0:
    case IDC_SELECT_TAB_1:
    case IDC_SELECT_TAB_2:
    case IDC_SELECT_TAB_3:
    case IDC_SELECT_TAB_4:
    case IDC_SELECT_TAB_5:
    case IDC_SELECT_TAB_6:
    case IDC_SELECT_TAB_7:
      return SelectNth(strip, order, command_id - IDC_SELECT_TAB_0);
    case IDC_SELECT_LAST_TAB:
      return SelectNth(strip, order, static_cast<int>(order.size()) - 1);
    case IDC_MOVE_TAB_NEXT:
      return MoveTodayTab(strip, *switcher, 1);
    case IDC_MOVE_TAB_PREVIOUS:
      return MoveTodayTab(strip, *switcher, -1);
    case IDC_CLOSE_TAB:
      return CloseActiveTab(strip, *switcher, order);
    case IDC_WINDOW_CLOSE_OTHER_TABS:
      return CloseTodayTabs(strip, *switcher, /*only_to_the_right=*/false);
    case IDC_WINDOW_CLOSE_TABS_TO_RIGHT:
      return CloseTodayTabs(strip, *switcher, /*only_to_the_right=*/true);
    default:
      return false;
  }
}
```

The helpers in the anonymous namespace:

- `SelectRelative` finds the active index in `order`, steps by `delta` with wrapping, activates, and returns true. An active tab that is not in `order` — a foreign tab about to trigger §4.4 — activates `order.front()`.
- `SelectNth` activates `order[n]` when `n` is in range, and returns true either way: the command is owned, and reaching past a space's last tab does nothing rather than falling through to Chromium's own count of the whole strip. Ruling R1 lives here: `order` holds open tabs only.
- `MoveTodayTab` returns true and does nothing when the active tab is claimed by an entry — an entry's place is the model's, not the strip's — and otherwise finds the next unclaimed tab of the space in `delta`'s direction and calls `MoveWebContentsAt`, so the moved tab steps over foreign tabs rather than into them.
- `CloseActiveTab` returns false unless the active tab is the space's only open tab; in that case it calls `switcher.OpenBlankTab()`, then closes the old tab with `kUserCloseTypes`, and returns true.
- `CloseTodayTabs` walks the strip backwards, closing tabs of the active space that no entry claims, skipping the active one, and for `only_to_the_right` those before it as well.

- [ ] **Step 4: Add the blank-tab callback**

In `space_switcher.h`, `void SetBlankTabCallback(base::RepeatingClosure callback);` with a `base::RepeatingClosure blank_tab_callback_;` run at the end of `OpenBlankTab`. Task 12 points it at `BrowserSidebarController::ShowQuickEntry`, which is what §4.3 means by the quick entry over the blank tab; here it is simply unset.

- [ ] **Step 5: Write patch 0155**

```
Seam: BrowserCommandController::HandleCommandWithDisposition, the tab
      commands, in chrome/browser/ui/browser_command_controller.cc.
Why: every one of these commands walks the whole tab strip, and a window
     shows one space. Ctrl+Tab would step into another space's tab, Cmd+1
     would count tabs the sidebar is not drawing, and Cmd+W on a space's
     last tab would leave the window showing a page from somewhere else.
     This is the seam patch 0090 already uses for IDC_NEW_TAB, and the
     shape is the same: Arcium answers, or says it did not.
Delegates to: arcium::HandleTabCommand
```

The diff wraps each of the nine cases in the same two lines, for example:

```
     case IDC_SELECT_NEXT_TAB:
       base::RecordAction(base::UserMetricsAction("Accel_SelectNextTab"));
+      if (arcium::HandleTabCommand(browser_, IDC_SELECT_NEXT_TAB)) {
+        break;
+      }
       SelectNextTab(
```

Generate the patch against the checkout, never by hand:

```bash
cd /Volumes/Texternal/chromium/src && git diff chrome/browser/ui/browser_command_controller.cc > /tmp/0155.diff
```

then put the header above the diff in `patches/0155-tab-commands-space.patch` and check it applies from clean with `scripts/sync`.

- [ ] **Step 6: Run the tests and the browser**

```bash
scripts/format && scripts/build dev arcium_unittests && /Volumes/Texternal/chromium/src/out/dev/arcium_unittests --gtest_filter=TabCommandsTest.*
scripts/build dev
```

Expected: unit tests green, browser builds with the patch applied.

- [ ] **Step 7: Mutation check**

Make `SelectNth` fall through to `return false` when `n` is out of range: `CommandDigitsCountTheSpacesOwnTabs` must fail on the third assertion. Restore. Make `CloseActiveTab` always return false: `ClosingTheSpacesLastTabLeavesABlankOneBehind` must fail. Restore.

- [ ] **Step 8: Commit**

```bash
git add arcium/ui/browser/tab_commands.h arcium/ui/browser/tab_commands.cc arcium/ui/browser/space_switcher.h arcium/ui/browser/space_switcher.cc patches/0155-tab-commands-space.patch arcium/test/tab_commands_unittest.cc
git commit -m "Ctrl+Tab, Cmd+1 and Cmd+W mean this space

Nine commands, one hook, no logic upstream: the patch asks and breaks, or
lets Chromium do what it always did. Cmd+1..8 count the space's open tabs
rather than its rows, because a cold row's place depends on which folders
are collapsed and only the view knows that (ruling R1).

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 9: The tab that comes next — `patches/0160-tab-strip-selection-space.patch`

**Files:**
- Modify: `arcium/ui/browser/space_switcher.h`, `arcium/ui/browser/space_switcher.cc`
- Create: `patches/0160-tab-strip-selection-space.patch`
- Test: `arcium/test/space_selection_unittest.cc`

**Interfaces:**
- Consumes: `SpaceSwitcher::FromTabStripModel`, `IsInActiveSpace`.
- Produces: `std::optional<int> arcium::NextSelectedIndexInSpace(TabStripModel*, std::optional<int> chromium_choice, int removed_index)`, and on `TabStripModelDelegate` a new non-pure `virtual std::optional<int> AdjustNewSelectedIndex(std::optional<int> chromium_choice, int removed_index)` returning `chromium_choice`.

The hook is at `DetermineNewSelectedIndex`'s two call sites rather than inside it: the function returns from six places, and reworking it to have one exit would be logic in a patch. `chromium_choice` is a post-removal index, which is what the call sites use and what Arcium must give back.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_F(SpaceSelectionTest, ClosingTheActiveTabNeverLandsInAnotherSpace) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  auto switcher = MakeSwitcher();
  AddTabInSpace(GURL("https://a1.example/"), first);  // 0
  AddTabInSpace(GURL("https://w1.example/"), work);   // 1
  AddTabInSpace(GURL("https://a2.example/"), first);  // 2
  strip()->ActivateTabAt(0);
  // Chromium's own pick after closing 0 is index 0, which is the foreign
  // tab once the strip has shifted.
  EXPECT_EQ(1, NextSelectedIndexInSpace(strip(), std::optional<int>(0), 0));
}

TEST_F(SpaceSelectionTest, ChromiumsPickIsKeptWhenItIsInTheSpace) {
  const SpaceId first = model_.default_space_id();
  auto switcher = MakeSwitcher();
  AddTabInSpace(GURL("https://a1.example/"), first);
  AddTabInSpace(GURL("https://a2.example/"), first);
  EXPECT_EQ(1, NextSelectedIndexInSpace(strip(), std::optional<int>(1), 0));
}

TEST_F(SpaceSelectionTest, ASpaceWithNoOtherTabKeepsChromiumsAnswer) {
  const SpaceId first = model_.default_space_id();
  const SpaceId work = model_.AddSpace(u"Work");
  auto switcher = MakeSwitcher();
  AddTabInSpace(GURL("https://w1.example/"), work);
  AddTabInSpace(GURL("https://a1.example/"), first);
  switcher->SwitchTo(work);
  // A close Arcium did not see — a script closing its own popup. §4.3
  // accepts landing on a foreign tab here; §4.4 then switches the space.
  EXPECT_EQ(0, NextSelectedIndexInSpace(strip(), std::optional<int>(0), 0));
}

TEST_F(SpaceSelectionTest, AnEmptyAnswerStaysEmpty) {
  auto switcher = MakeSwitcher();
  AddTabInSpace(GURL("https://a1.example/"), model_.default_space_id());
  EXPECT_FALSE(
      NextSelectedIndexInSpace(strip(), std::nullopt, 0).has_value());
}

TEST_F(SpaceSelectionTest, WithoutASwitcherChromiumsAnswerIsUntouched) {
  AddTab(browser(), GURL("https://a1.example/"));
  AddTab(browser(), GURL("https://a2.example/"));
  EXPECT_EQ(1, NextSelectedIndexInSpace(strip(), std::optional<int>(1), 0));
}
```

- [ ] **Step 2: Run them and watch them fail**

```bash
/Volumes/Texternal/chromium/src/out/dev/arcium_unittests --gtest_filter=SpaceSelectionTest.*
```

Expected: `NextSelectedIndexInSpace` is undeclared.

- [ ] **Step 3: Write the answer**

```cpp
std::optional<int> NextSelectedIndexInSpace(TabStripModel* tab_strip_model,
                                            std::optional<int> chromium_choice,
                                            int removed_index) {
  SpaceSwitcher* switcher =
      tab_strip_model ? SpaceSwitcher::FromTabStripModel(tab_strip_model)
                      : nullptr;
  // No sidebar, or the strip is about to be empty and Chromium has already
  // said there is nothing to select.
  if (!switcher || !chromium_choice.has_value()) {
    return chromium_choice;
  }
  // Chromium answers in post-removal indices and the strip has not moved
  // yet, so shift back to ask which tab it actually means.
  const auto to_current = [removed_index](int after) {
    return after >= removed_index ? after + 1 : after;
  };
  const auto to_after = [removed_index](int current) {
    return current > removed_index ? current - 1 : current;
  };
  if (switcher->IsInActiveSpace(to_current(*chromium_choice))) {
    return chromium_choice;
  }
  // The nearest open tab of the space, looking right first — the direction
  // the strip closes towards, and the one a list reads in.
  const int count = tab_strip_model->count();
  for (int index = removed_index + 1; index < count; ++index) {
    if (switcher->IsInActiveSpace(index)) {
      return to_after(index);
    }
  }
  for (int index = removed_index - 1; index >= 0; --index) {
    if (switcher->IsInActiveSpace(index)) {
      return to_after(index);
    }
  }
  // The space has no other open tab. Every close Arcium sees opens a blank
  // tab first (§4.2, §4.3), so this is a close it did not see; keep
  // Chromium's answer and let §4.4 switch to wherever it lands.
  return chromium_choice;
}
```

- [ ] **Step 4: Write patch 0160**

```
Seam: TabStripModel::DetermineNewSelectedIndex's two call sites in
      chrome/browser/ui/tabs/tab_strip_model.cc, through a new
      TabStripModelDelegate method.
Why: closing the active tab must not activate a tab from another space.
     DetermineNewSelectedIndex is the single choke point behind both close
     paths, but it returns from six places, so the hook sits at the two
     call sites instead: one line each, no control flow moved.

     The delegate indirection is forced by the build graph, not chosen.
     tab_strip_model.cc builds in //chrome/browser/ui/tabs:tab_strip_impl,
     which is below //chrome/browser/ui, and only //chrome/browser/ui
     reaches arcium/ (patch 0010). The tab-strip half can only ask the
     question; BrowserTabStripModelDelegate, which is in //chrome/browser/ui,
     answers it. The new method is not pure and returns what it was handed,
     so every other delegate — and every unit test with a stub one — behaves
     exactly as before.
Delegates to: arcium::NextSelectedIndexInSpace
```

Three hunks: the defaulted method on `TabStripModelDelegate`, the override on `BrowserTabStripModelDelegate` that calls `arcium::NextSelectedIndexInSpace(browser_->tab_strip_model(), chromium_choice, removed_index)`, and the two call sites:

```
   std::optional<int> next_selected_index =
       DetermineNewSelectedIndex(tab_to_remove);
+  next_selected_index = delegate_->AdjustNewSelectedIndex(next_selected_index,
+                                                         index);
```

with the collection call site passing `collection_start_index`.

- [ ] **Step 5: Build and run**

```bash
scripts/format && scripts/build dev arcium_unittests && /Volumes/Texternal/chromium/src/out/dev/arcium_unittests --gtest_filter=SpaceSelectionTest.*
scripts/build dev
```

Expected: green, and the browser builds with both new patches.

- [ ] **Step 6: Mutation check**

Make the function return `chromium_choice` unconditionally: `ClosingTheActiveTabNeverLandsInAnotherSpace` must fail. Restore. Remove the `to_current` shift and compare the raw index: the same test must fail (with a three-tab strip the unshifted read looks at the wrong tab). Restore.

- [ ] **Step 7: Commit**

```bash
git add arcium/ui/browser/space_switcher.h arcium/ui/browser/space_switcher.cc patches/0160-tab-strip-selection-space.patch arcium/test/space_selection_unittest.cc
git commit -m "Closing a tab lands on a tab of the same space

Two call sites, one defaulted delegate method, and the answer itself in
arcium/. The indirection is the build graph's doing: tab_strip_impl sits
below the target that can see arcium/, so the tab strip can only ask.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 10: The space bar draws every space

**Files:**
- Modify: `arcium/ui/sidebar/space_bar_view.h`, `arcium/ui/sidebar/space_bar_view.cc`
- Test: `arcium/test/space_bar_unittest.cc`

**Interfaces:**
- Consumes: Task 7's `SidebarModel` spaces API.
- Produces: `SpaceBarView` with one chip per space plus an add button; the menu commands `kRename`, `kChangeIcon`, `kEditTheme`, `kDelete`, `kMoveLeft`, `kMoveRight` live, and `kGradientFirst = 200` as the base of the gradient submenu. A test seam `SpaceBarView::confirm_delete_for_testing()` returning the pending confirmation's text, and `ConfirmDeleteForTesting(bool accept)`.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_F(SpaceBarTest, OneChipPerSpaceWithTheActiveOneMarked) {
  FakeSidebarModel model;
  model.AddSpaceForTesting(u"Work", u"💼", 0);
  SpaceBarView bar(&model);
  EXPECT_EQ(2u, bar.chips_for_testing().size());
  EXPECT_TRUE(bar.chips_for_testing()[0]->GetHighlighted());
  EXPECT_EQ(u"💼", bar.chips_for_testing()[1]->GetText());
}

TEST_F(SpaceBarTest, ASpaceWithNoIconDrawsItsFirstLetter) {
  FakeSidebarModel model;
  model.AddSpaceForTesting(u"Work", u"", 0);
  SpaceBarView bar(&model);
  EXPECT_EQ(u"W", bar.chips_for_testing()[1]->GetText());
}

TEST_F(SpaceBarTest, PressingAChipSwitchesToThatSpace) {
  FakeSidebarModel model;
  model.AddSpaceForTesting(u"Work", u"", 0);
  SpaceBarView bar(&model);
  views::test::ButtonTestApi(bar.chips_for_testing()[1]).NotifyClick(
      ui::test::TestEvent());
  EXPECT_TRUE(model.spaces()[1].is_active);
}

TEST_F(SpaceBarTest, TheAddButtonMakesASpace) {
  FakeSidebarModel model;
  SpaceBarView bar(&model);
  views::test::ButtonTestApi(bar.add_button_for_testing()).NotifyClick(
      ui::test::TestEvent());
  EXPECT_EQ(2u, model.spaces().size());
}

TEST_F(SpaceBarTest, RenameIconGradientAndReorderAreLiveNow) {
  FakeSidebarModel model;
  model.AddSpaceForTesting(u"Work", u"", 0);
  SpaceBarView bar(&model);
  bar.BuildMenuForTesting(model.spaces()[1].id);
  EXPECT_TRUE(bar.IsCommandIdEnabled(SpaceBarView::kRename));
  EXPECT_TRUE(bar.IsCommandIdEnabled(SpaceBarView::kChangeIcon));
  EXPECT_TRUE(bar.IsCommandIdEnabled(SpaceBarView::kDelete));
  EXPECT_TRUE(bar.IsCommandIdEnabled(SpaceBarView::kMoveLeft));
  // Last space in the row: there is nothing to its right.
  EXPECT_FALSE(bar.IsCommandIdEnabled(SpaceBarView::kMoveRight));

  bar.ExecuteCommand(SpaceBarView::kMoveLeft, 0);
  EXPECT_EQ(u"Work", model.spaces()[0].name);
  bar.ExecuteCommand(SpaceBarView::kGradientFirst + 2, 0);
  EXPECT_EQ(2, model.spaces()[0].gradient);
}

TEST_F(SpaceBarTest, DeleteAsksFirstAndSaysWhatGoes) {
  FakeSidebarModel model;
  model.AddSpaceForTesting(u"Work", u"", 0);
  model.AddTabInSpaceForTesting(u"One", "https://w1.example/",
                                model.spaces()[1].id);
  SpaceBarView bar(&model);
  bar.BuildMenuForTesting(model.spaces()[1].id);
  bar.ExecuteCommand(SpaceBarView::kDelete, 0);
  EXPECT_EQ(2u, model.spaces().size());  // nothing yet
  EXPECT_NE(std::u16string::npos,
            bar.confirm_text_for_testing().find(u"Work"));
  EXPECT_NE(std::u16string::npos,
            bar.confirm_text_for_testing().find(u"1 tab"));
  bar.ConfirmDeleteForTesting(/*accept=*/true);
  EXPECT_EQ(1u, model.spaces().size());
}

TEST_F(SpaceBarTest, DecliningTheConfirmationKeepsTheSpace) {
  FakeSidebarModel model;
  model.AddSpaceForTesting(u"Work", u"", 0);
  SpaceBarView bar(&model);
  bar.BuildMenuForTesting(model.spaces()[1].id);
  bar.ExecuteCommand(SpaceBarView::kDelete, 0);
  bar.ConfirmDeleteForTesting(/*accept=*/false);
  EXPECT_EQ(2u, model.spaces().size());
}

TEST_F(SpaceBarTest, TheArchiveTimeoutStillReadsTheActiveSpace) {
  FakeSidebarModel model;
  SpaceBarView bar(&model);
  bar.BuildMenuForTesting(model.spaces()[0].id);
  EXPECT_TRUE(bar.IsCommandIdChecked(SpaceBarView::kTimeoutTwelveHours));
  bar.ExecuteCommand(SpaceBarView::kTimeoutSevenDays, 0);
  EXPECT_EQ(ArchiveTimeout::kSevenDays, model.archive_timeout());
}
```

The four timeout tests already in `sidebar_views_unittest.cc:1724` move here unchanged, so that file shrinks rather than grows.

- [ ] **Step 2: Run them and watch them fail**

```bash
/Volumes/Texternal/chromium/src/out/dev/arcium_unittests --gtest_filter=SpaceBarTest.*
```

Expected: `chips_for_testing`, `kMoveLeft` and the rest are undeclared.

- [ ] **Step 3: Rebuild the bar from the model**

`SpaceBarView` becomes a `SidebarModel::Observer` and rebuilds its chips on every change, the way `SidebarView::Rebuild` does. Each chip is a `views::LabelButton` whose text is the space's icon or the first letter of its name, whose callback is `model_->SwitchToSpace(id)`, and whose `set_context_menu_controller(this)` opens the menu for *that* space — the menu's subject is the chip it was opened on, not the active space, which is what makes "Move left" on a background space work. The add button is a `views::ImageButton` with the existing `add.icon`, calling `model_->AddSpace(u"New space")`.

The menu grows three items and one submenu:

```cpp
  menu_model_->AddItem(kRename, u"Rename space");
  menu_model_->AddSubMenu(kEditTheme, u"Edit theme", gradient_menu_.get());
  menu_model_->AddItem(kChangeIcon, u"Change icon");
  menu_model_->AddSubMenu(kArchiveTimeout, u"Archive Today tabs after",
                          timeout_menu_.get());
  menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  menu_model_->AddItem(kMoveLeft, u"Move left");
  menu_model_->AddItem(kMoveRight, u"Move right");
  menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  menu_model_->AddItem(kDelete, u"Delete space");
```

`kEditTheme` is now the gradient submenu — the palette of Stage 3a — and Stage 6's custom editor replaces it. The submenu holds `kGradientFirst + i` for each preset in `space_gradients.h`, as a radio group so the current one is checked. `IsCommandIdEnabled` returns true for everything except `kMoveLeft` on the first space and `kMoveRight` on the last. Rename and Change icon start the inline edit on the chip through `RenameField`, the same control a row rename uses; the icon edit takes whatever the field holds, which is how one emoji gets in without an emoji picker in this stage.

- [ ] **Step 4: The confirmation**

```cpp
void SpaceBarView::ExecuteCommand(int command_id, int event_flags) {
  // …timeouts and the gradient first, then:
  if (command_id == kDelete) {
    const SidebarSpace* space = SpaceForMenu();
    if (!space) {
      return;
    }
    // Arcium's one destructive delete (D3-1), so the sentence says what
    // goes and that it does not come back — Zen's own wording, over more
    // than Zen loses, because an Arcium space owns its favourites.
    confirm_text_ = base::StrCat(
        {u"Delete “", space->name, u"”? Its ", CountPhrase(space->open_tab_count, u"tab"),
         u", ", CountPhrase(space->entry_count, u"pin and favourite"),
         u" will be deleted. This action cannot be undone."});
    ShowConfirmation(space->id);
  }
}
```

`ShowConfirmation` builds a `ui::DialogModel` with a destructive OK button labelled "Delete space" and shows it through `views::BubbleDialogModelHost` anchored to the chip; accepting calls `model_->DeleteSpace(id)`. `ConfirmDeleteForTesting(bool)` runs the same accept or cancel callback without a widget, which is how the tests above reach it — a modal dialog in a unit test is the same nested-loop hang `RowContextMenu::SetShowHookForTesting` exists to avoid.

- [ ] **Step 5: Run the tests**

```bash
scripts/format && scripts/build dev arcium_unittests && /Volumes/Texternal/chromium/src/out/dev/arcium_unittests --gtest_filter=SpaceBarTest.*:SidebarViewsTest.*
```

Expected: green.

- [ ] **Step 6: Mutation check**

Make `IsCommandIdEnabled` return true for `kMoveRight` unconditionally: `RenameIconGradientAndReorderAreLiveNow` must fail. Restore. Make `kDelete` call `DeleteSpace` directly: `DeleteAsksFirstAndSaysWhatGoes` must fail on its first assertion. Restore.

- [ ] **Step 7: Commit**

```bash
git add arcium/ui/sidebar/space_bar_view.h arcium/ui/sidebar/space_bar_view.cc arcium/test/space_bar_unittest.cc arcium/test/sidebar_views_unittest.cc
git commit -m "The space bar is a row of spaces you can act on

Its menu items stop being disabled placeholders. Delete asks first and
names what it takes, because it is the one delete in Arcium that does not
come back.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 11: Gradients, the switch animation, the shortcuts and "Move to space"

**Files:**
- Modify: `arcium/ui/sidebar/space_gradients.h`, `arcium/ui/sidebar/space_gradients.cc`, `arcium/ui/sidebar/tint_background.h`, `arcium/ui/sidebar/tint_background.cc`, `arcium/ui/sidebar/sidebar_view.h`, `arcium/ui/sidebar/sidebar_view.cc`, `arcium/ui/sidebar/row_context_menu.h`, `arcium/ui/sidebar/row_context_menu.cc`
- Test: `arcium/test/space_bar_unittest.cc`

**Interfaces:**
- Consumes: Task 7's spaces API.
- Produces:
  - `struct SpaceGradient { SkColor light_top, light_bottom, dark_top, dark_bottom; };`
  - `base::span<const SpaceGradient> SpaceGradients();` — preset 0 is a sentinel meaning "the colour-mixer pair"
  - `void TintBackground::SetPreset(int preset)`
  - `RowContextMenu::kMoveToSpaceFirst = 300`, and a `spaces()`-driven submenu on all three sections

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_F(SpaceBarTest, PresetZeroIsTheSidebarsOriginalPair) {
  FakeSidebarModel model;
  SidebarView view(&model, SidebarView::Delegate());
  // The colours the mixer gives, which is what every existing snapshot and
  // every space that never chose a gradient draws.
  EXPECT_EQ(TintColorsForTesting(view, /*preset=*/0),
            MixerColorsForTesting(view));
  EXPECT_NE(TintColorsForTesting(view, /*preset=*/1),
            MixerColorsForTesting(view));
}

TEST_F(SpaceBarTest, TheSidebarPaintsTheActiveSpacesGradient) {
  FakeSidebarModel model;
  model.AddSpaceForTesting(u"Work", u"", 3);
  SidebarView view(&model, SidebarView::Delegate());
  model.SwitchToSpace(model.spaces()[1].id);
  EXPECT_EQ(3, view.tint_preset_for_testing());
}

TEST_F(SpaceBarTest, CtrlDigitSwitchesToTheNthSpace) {
  FakeSidebarModel model;
  model.AddSpaceForTesting(u"Work", u"", 0);
  model.AddSpaceForTesting(u"Play", u"", 0);
  SidebarView view(&model, SidebarView::Delegate());
  EXPECT_TRUE(view.AcceleratorPressed(
      ui::Accelerator(ui::VKEY_3, ui::EF_CONTROL_DOWN)));
  EXPECT_TRUE(model.spaces()[2].is_active);
  // A digit past the last space is not this view's to swallow.
  EXPECT_FALSE(view.AcceleratorPressed(
      ui::Accelerator(ui::VKEY_9, ui::EF_CONTROL_DOWN)));
}

TEST_F(SpaceBarTest, MoveToSpaceIsOfferedOnEveryRowSection) {
  FakeSidebarModel model;
  model.AddSpaceForTesting(u"Work", u"", 0);
  model.AddTab(u"Today", "https://t.example/", SidebarSection::kToday, true);
  RowContextMenu menu(&model);
  for (const SidebarRow& row : model.rows()) {
    menu.BuildForRow(row, base::DoNothing());
    EXPECT_TRUE(HasItem(menu.menu(), u"Move to space"));
  }
}

TEST_F(SpaceBarTest, MoveToSpaceOffersEverySpaceButThisOne) {
  FakeSidebarModel model;
  model.AddSpaceForTesting(u"Work", u"", 0);
  model.AddTab(u"Today", "https://t.example/", SidebarSection::kToday, true);
  RowContextMenu menu(&model);
  menu.BuildForRow(model.rows().front(), base::DoNothing());
  menu.ExecuteCommand(RowContextMenu::kMoveToSpaceFirst, 0);
  // The one target offered is the space this row is not in.
  EXPECT_TRUE(model.rows().empty());
  EXPECT_TRUE(model.spaces()[1].is_active);
}
```

- [ ] **Step 2: Run them and watch them fail**

```bash
/Volumes/Texternal/chromium/src/out/dev/arcium_unittests --gtest_filter=SpaceBarTest.*
```

Expected: `SetPreset`, `tint_preset_for_testing` and `kMoveToSpaceFirst` are undeclared.

- [ ] **Step 3: The palette**

`space_gradients.cc` holds eight presets as a `constexpr SpaceGradient kGradients[]`, index 0 being all-transparent and documented as the sentinel:

```cpp
// Preset 0 is not a colour: it means "whatever the colour mixer says", which
// is the pair the sidebar has always drawn. A space that never chose a
// gradient therefore looks exactly as it did before spaces existed, and the
// snapshot tests that predate this file keep passing.
```

`TintBackground::SetPreset(int)` stores the preset and drops the cached shader; `Paint` reads the two stops from the preset, or from `kColorArciumSidebarBackgroundTop`/`Bottom` for preset 0, choosing the light or dark pair from `view->GetNativeTheme()->ShouldUseDarkColors()`. The existing cache keys on the colours, so nothing else changes.

- [ ] **Step 4: The sidebar's half**

`SidebarView::Rebuild` sets the tint preset from the active space and rebuilds. `AcceleratorPressed` grows the digit branch before the existing pinned-URL branch:

```cpp
  if (accelerator.IsCtrlDown() && accelerator.key_code() >= ui::VKEY_1 &&
      accelerator.key_code() <= ui::VKEY_9) {
    const size_t index = accelerator.key_code() - ui::VKEY_1;
    const std::vector<SidebarSpace> spaces = model_->spaces();
    // Past the last space this is not ours: swallowing it would make a
    // shortcut that does nothing, which reads as a broken key.
    if (index >= spaces.size()) {
      return false;
    }
    model_->SwitchToSpace(spaces[index].id);
    return true;
  }
```

with nine `AddAccelerator(ui::Accelerator(ui::VKEY_1 + i, ui::EF_CONTROL_DOWN))` beside the existing Cmd+Shift+Backspace registration, and the same caveat: they arrive only when the page has not consumed the key.

The switch animation is a `ui::LayerAnimator` on the scrolling column's layer — the column is already layer-backed, because `ScrollWithLayers` is on — sliding it from ±`metrics::kSidebarWidth` to zero over 200 ms with `gfx::Tween::EASE_OUT`, started from `OnSidebarModelChanged` when the active space changed since the last rebuild. The page swaps when the model activates the tab, at the start of the slide, and is not animated.

Two-finger horizontal swipe: `SidebarView::OnScrollEvent` reads `event->x_offset()` and, past a threshold of `metrics::kSidebarWidth / 4` with a horizontal-dominant offset, switches to the neighbouring space and marks the event handled. Over the page nothing changes, because this handler is the sidebar's.

- [ ] **Step 5: "Move to space"**

`RowContextMenu` gains `kMoveToSpaceFirst = 300` and `std::vector<SpaceId> space_targets_`. `BuildForRow` appends, in all three section arms, a submenu built from `model_->spaces()` minus the active one, and `ExecuteCommand` dispatches it above the existing folder range:

```cpp
  if (command_id >= kMoveToSpaceFirst) {
    const size_t index = static_cast<size_t>(command_id - kMoveToSpaceFirst);
    if (index >= space_targets_.size()) {
      return;
    }
    // A row with an entry moves the entry, which carries its tab; a Today
    // row has no entry, so the tab itself is re-tagged.
    if (row_.entry_id.is_valid()) {
      model_->MoveEntryToSpace(row_.entry_id, space_targets_[index]);
    } else {
      model_->MoveTabToSpace(row_.tab_index, space_targets_[index]);
    }
    return;
  }
```

The folder branch keeps its `>= kMoveToFolderFirst` test below this one, which is why the space range starts at 300 and the comment on each constant says so.

- [ ] **Step 6: Run the tests and look at it**

```bash
scripts/format && scripts/build dev arcium_unittests && /Volumes/Texternal/chromium/src/out/dev/arcium_unittests
scripts/playground
```

Expected: green, and the playground shows several chips, a gradient that changes with the chosen preset, and a slide on switching.

- [ ] **Step 7: Mutation check**

Make preset 0 read `kGradients[0]` instead of the mixer: `PresetZeroIsTheSidebarsOriginalPair` must fail. Restore. Make the accelerator return true past the last space: `CtrlDigitSwitchesToTheNthSpace` must fail. Restore.

- [ ] **Step 8: Commit**

```bash
git add arcium/ui/sidebar/space_gradients.h arcium/ui/sidebar/space_gradients.cc arcium/ui/sidebar/tint_background.h arcium/ui/sidebar/tint_background.cc arcium/ui/sidebar/sidebar_view.h arcium/ui/sidebar/sidebar_view.cc arcium/ui/sidebar/row_context_menu.h arcium/ui/sidebar/row_context_menu.cc arcium/test/space_bar_unittest.cc
git commit -m "Each space has its own colours, and three ways to reach it

Preset 0 is the colour mixer's own pair, so a space that never chose a
gradient draws exactly what the sidebar always drew and the snapshot
tests that predate spaces keep passing.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 12: Wiring the switcher into the window

**GATE: do not start this task until the owner has committed or released `arcium/ui/browser/browser_sidebar_controller.{h,cc}` and `arcium/ui/sidebar/sidebar_metrics.h`.** They hold 25 lines of uncommitted work that is not this stage's to touch. If the gate is still closed when every other task is done, stop and say so rather than editing around it.

**Files:**
- Modify: `arcium/ui/browser/browser_sidebar_controller.h`, `arcium/ui/browser/browser_sidebar_controller.cc`
- Test: exercised by hand in Task 13; no new unit test, because the controller needs a real `BrowserView`.

**Interfaces:**
- Consumes: `SpaceSwitcher`, the four-argument `SidebarTabModel`, the switcher-aware `ArchiveService`, `SetBlankTabCallback`.
- Produces: a `std::unique_ptr<SpaceSwitcher> space_switcher_` on the controller, declared **before** `model_` and `archive_service_` so it outlives both.

- [ ] **Step 1: Build the switcher first**

In the controller's constructor, before the sidebar model:

```cpp
  // Before the model and the service, both of which hold a bare pointer to
  // it, and before the sidebar view, which draws whatever space it names.
  space_switcher_ = std::make_unique<SpaceSwitcher>(
      browser_view_->browser()->tab_strip_model(), state->model(),
      state->binding());
  space_switcher_->SetBlankTabCallback(base::BindRepeating(
      &BrowserSidebarController::ShowQuickEntry, weak_factory_.GetWeakPtr()));
```

and pass `space_switcher_.get()` as the trailing argument of the `SidebarTabModel` and `ArchiveService` constructions. Give the switcher the archive service with `space_switcher_->SetArchiveService(archive_service_.get())` immediately after the service is built, beside the existing `model_->SetArchiveService(...)` call — the same shape, for the same reason: the service needs a built strip and the switcher does not.

- [ ] **Step 2: Build and run the browser**

```bash
scripts/build dev && scripts/run
```

Expected: the window opens on the space that was active at quit, the space bar shows one chip, and Cmd+T still opens the quick entry.

- [ ] **Step 3: Commit**

```bash
git add arcium/ui/browser/browser_sidebar_controller.h arcium/ui/browser/browser_sidebar_controller.cc
git commit -m "The window owns its switcher

Declared before the model and the archive service, which both hold a bare
pointer to it, and pointed at the quick entry so a space that runs out of
tabs opens one and asks where to go.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 13: Acceptance, findings and the record

**Files:**
- Create: `docs/stage3a-findings.md`
- Modify: `CLAUDE.md`, `docs/superpowers/specs/2026-09-05-arcium-browser-design.md`

- [ ] **Step 1: Run the suite and the browser**

```bash
scripts/build dev arcium_unittests && /Volumes/Texternal/chromium/src/out/dev/arcium_unittests
scripts/build dev && scripts/run
```

- [ ] **Step 2: Execute the acceptance list by hand**

A3a.1 through A3a.8 from the spec's §9, plus A3.2. A3a.6 runs its Cmd+click half only; its tab-search half has no UI to run in (ruling R4), and §4.4's rule is covered by A3a.8 instead. Note the result of each, including what was checked and what was seen — a pass with no observation recorded is not a pass.

- [ ] **Step 3: Write `docs/stage3a-findings.md`**

Following `docs/stage2.5-findings.md`'s shape: what the acceptance pass found, every defect and its fix commit, anything deferred, and the perf note. Answer the four perf questions with what the build actually showed, and record `scripts/perf` output in `docs/perf/` if the machine is quiet enough to run it — a perf result is discussed, not a gate.

- [ ] **Step 4: Record the deviation and the stage**

In the master spec's deviations section, add D3-1 with the text from the 3a spec's §7. In `CLAUDE.md`'s stage table, change the Stage 3 row to name what is done — 3a — and what is not: 3b profiles and R3.9.

- [ ] **Step 5: Commit**

```bash
git add docs/stage3a-findings.md CLAUDE.md docs/superpowers/specs/2026-09-05-arcium-browser-design.md
git commit -m "Record what the Stage 3a acceptance pass found

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

- [ ] **Step 6: Finish the branch**

Use superpowers:finishing-a-development-branch: verify the suite on the tree about to be integrated, then present the three options and wait.

---

## Self-review

**Spec coverage.** §3.1 and §3.2 are Tasks 1 and 2; §3.3 and §3.4 are Tasks 3 and 4; §4.1 is Task 4; §4.2 is Task 8; §4.3 is Tasks 8 and 9; §4.4 is Task 4. §5's dots, swipe, Ctrl+1..9, gradient and "Move to space" are Tasks 10 and 11; its `SidebarModel` API is Task 7. §6's delete flow and archive exemptions are Tasks 5 and 6; its Cmd+Shift+T case is Task 3's session writers. §7's deviation is recorded in Task 13. §8's tests live in the task that writes each behaviour. §9 is Task 13. §10's four answers are in this plan's Performance section. §11 is out of scope and stays out.

**Three requirements the spec states that no single task owns**, and where they are met: "background spaces do nothing" is met by never adding a timer or observer per space — the one observer is per window; "the same site pinned in two spaces is two tabs" falls out of an entry belonging to a space, checked in A3a.3; "a switch holds 60 fps" is A3.2, a trace, not a unit test.

**Type consistency.** `SpaceId`, `TabKey`, `EntryId` throughout; `active_space()` is the private accessor on the three services and `active_space()` the public one on `SpaceSwitcher`; `SwitchTo` on the switcher and `SwitchToSpace` on `SidebarModel`, which is deliberate — one takes the window's own space, the other is a view command — and both appear in Task 7's implementation. `OpenTabsInSidebarOrder` is the only ordering function and is used by Tasks 8 and 4. `kMoveToFolderFirst = 100` and `kMoveToSpaceFirst = 300` do not overlap, and Task 11 orders their dispatch explicitly.

**Known gap, deliberately left.** Task 12 has no unit test because `BrowserSidebarController` needs a real `BrowserView`; it is covered by the hand pass in Task 13, which is how every previous stage covered the same file.
