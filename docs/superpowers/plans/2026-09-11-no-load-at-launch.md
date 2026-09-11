# No page loads at launch Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** After a restart only the tab on screen loads; every other restored tab waits for a click, and the sidebar dims every row whose click would have to load a page.

**Architecture:** One hook in `SessionRestoreDelegate::RestoreTabs` hands the restored tabs to `arcium::DeferRestoredTabLoads`, which asks for their favicons and schedules no loads. `SidebarRow` gains `is_unloaded`, read from Chromium's own `NeedsReload()` / `WasDiscarded()` when a row is built, and the views dim a row when `needs_load()` — cold or unloaded — using the dimming that shipped at `5dd4a7b`, renamed to say what it now covers.

**Tech Stack:** Chromium 152.0.7977.83 (C++, Views, GN, siso), gtest, Arcium's `scripts/`.

**Spec:** `docs/superpowers/specs/2026-09-11-no-load-at-launch-design.md`

## Global Constraints

- All Arcium code lives in `arcium/`. Upstream Chromium files change **only** through numbered patches in `patches/`, each opening with a prose header naming `Seam:`, `Why:` and `Delegates to:` before the first `diff --git`. **Logic never lives in a patch**; a patch is a few lines that call into `arcium/`.
- Always-visible UI is Chromium Views in C++. **Never WebUI. No Swift, AppKit or Cocoa.**
- One window, one `Browser`, one `TabStripModel`.
- Background spaces do nothing: no timer, no poll, no thumbnail per space. Reading the unloaded state costs nothing while it is not displayed: no polling and no per-tab timer, only notifications Chromium already sends.
- **Never write on the UI thread. No sync I/O ever.**
- `arcium/browser` and `arcium/browser/model` carry **no `//chrome` dependency**; the model target's deps are exactly `//base` and `//url`.
- `base::DictValue` and `base::ListValue`. **`base::Value::Dict` does not exist in this tree.**
- Files over ~500 lines are a smell; split rather than grow one.
- `scripts/format` after every code change. **`git cl format` does not work here.**
- Test-driven: the failing test comes first, and after each step delete the code just written and confirm the named test fails (mutation check); `touch` a restored file before rebuilding so the build does not reuse a stale object.
- Commit by explicit path. **Never `git add -A`, `git add .` or `git commit -a`.**
- Commit messages say why, carry no task numbers, and end with `Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>`. Do not push.
- **Do not modify or commit** the untracked `AGENTS.md`.
- Never commit Chromium sources or build output.
- Work on the branch `no-load-at-launch`, cut from `main` at or after `351b695`.
- Build and test commands, run from the repo root, always in the foreground:
  - `ARCIUM_JOBS=4 scripts/build dev arcium_unittests`
  - `/Volumes/Texternal/chromium/src/out/dev/arcium_unittests --gtest_filter=<Suite.Name>`
  - `ARCIUM_JOBS=4 scripts/build dev` for the browser, `ARCIUM_JOBS=4 scripts/build dev arcium_playground` for the playground.
  - Before any build check `pgrep -f "Arcium.app/Contents/MacOS/Arcium"` and `pgrep -f siso`; never start a second build while one runs; never kill the owner's processes.
  - A `BUILD.gn` edit costs a `gn gen` plus a long graph reload, which is why Task 0 makes every `BUILD.gn` edit at once.
  - `SidebarViewsTest` archive-list tests can time out once under load; re-run a single timeout before calling it a failure.

## Deviations from the spec this plan makes

- **R1 — `DeferRestoredTabLoads` has a second overload taking the favicon request.** The spec names one function. A test cannot see `ContentFaviconDriver::FetchFavicon` happen on a bare test `WebContents`, so the rule lives in an overload that takes the request as a callback, and the one the hook calls binds the real driver. The hook's call is unchanged.
- **R2 — "is this tab unloaded" is one function, `arcium::IsTabUnloaded`,** beside `DeferRestoredTabLoads`. The spec writes the expression inline in `SidebarTabModel`; it is needed at two row sites, and one definition means the two cannot drift. `SidebarTabModel` still owns the `!is_active` half, which is about the row, not the tab.
- **R3 — the view tests for unloaded rows go in `sidebar_views_unittest.cc`,** beside the cold-row tests they mirror, because the fixture they need (`MakeList`, `Refresh`, `FaviconCenterAlpha`) lives there. The file grows by about 50 lines past 2,176; its split is already parked for the next change that reorganises it.

## File structure

New, all under `arcium/` or `patches/`:

| File | Responsibility |
|---|---|
| `arcium/browser/restored_tab_loading.{h,cc}` | `DeferRestoredTabLoads` (the hook's target) and `IsTabUnloaded` (the one definition of the state). |
| `patches/0170-session-restore-defer-loads.patch` | The hook in `SessionRestoreDelegate::RestoreTabs`. |
| `arcium/test/restored_tab_loading_unittest.cc` | The rule, the flags and the state, over test `WebContents`. |
| `arcium/test/unloaded_rows_unittest.cc` | `SidebarTabModel` marking rows unloaded, over a real `TabStripModel`. |

Renamed: `arcium/ui/sidebar/cold_row_dimming.{h,cc}` → `unloaded_row_dimming.{h,cc}`.

Modified: `arcium/common/arcium_features.{h,cc}`, `arcium/browser/BUILD.gn`, `arcium/ui/sidebar/BUILD.gn`, `arcium/test/BUILD.gn`, `arcium/ui/sidebar/sidebar_model.h`, `arcium/ui/sidebar/sidebar_colors.{h,cc}`, `arcium/ui/sidebar/tab_row_view.cc`, `arcium/ui/sidebar/favorites_grid_view.cc`, `arcium/ui/browser/sidebar_tab_model.cc`, `arcium/ui/browser/sidebar_tab_model_entries.cc`, `arcium/ui/playground/fake_sidebar_model.{h,cc}`, `arcium/ui/playground/sidebar_example.cc`, `arcium/test/sidebar_views_unittest.cc`, `patches/README.md`; in Task 5, `docs/`, `CLAUDE.md` and the two specs.

## Performance

1. **Processes:** fewer. Up to 20 page renderers no longer start after a restart.
2. **Idle memory:** less, by the pages that no longer load. Nothing per tab; `is_unloaded` is computed when a row is built.
3. **Startup:** less work after first paint. The favicon lookups the loader made still happen; nothing is added before first paint.
4. **UI thread:** two field reads per live row when the sidebar rebuilds.

---

### Task 0: Build files, skeletons and the rename

Every `BUILD.gn` edit this plan needs, made at once so the graph reloads once, plus the rename of the closed-row dimming to the name that will cover unloaded rows too. No behaviour changes; the suite stays green.

**Files:**
- Create: `arcium/browser/restored_tab_loading.h`, `arcium/browser/restored_tab_loading.cc`, `arcium/test/restored_tab_loading_unittest.cc`, `arcium/test/unloaded_rows_unittest.cc`
- Rename: `arcium/ui/sidebar/cold_row_dimming.{h,cc}` → `arcium/ui/sidebar/unloaded_row_dimming.{h,cc}`
- Modify: `arcium/browser/BUILD.gn`, `arcium/ui/sidebar/BUILD.gn`, `arcium/test/BUILD.gn`, `arcium/ui/sidebar/sidebar_colors.{h,cc}`, `arcium/ui/sidebar/tab_row_view.cc`, `arcium/ui/sidebar/favorites_grid_view.cc`, `arcium/test/sidebar_views_unittest.cc`

**Interfaces:**
- Produces: `arcium::DimUnloadedFavicon(const ui::ImageModel&)` in `arcium/ui/sidebar/unloaded_row_dimming.h`, and the colour id `kColorArciumRowTextUnloaded`, both behaving exactly as `DimColdFavicon` and `kColorArciumRowTextCold` did.

- [ ] **Step 1: Cut the branch**

```bash
git checkout -b no-load-at-launch main
```

- [ ] **Step 2: Rename the dimming**

```bash
git mv arcium/ui/sidebar/cold_row_dimming.h arcium/ui/sidebar/unloaded_row_dimming.h
git mv arcium/ui/sidebar/cold_row_dimming.cc arcium/ui/sidebar/unloaded_row_dimming.cc
grep -rl "cold_row_dimming\|COLD_ROW_DIMMING\|DimColdFavicon\|kColorArciumRowTextCold" arcium | xargs sed -i '' \
  -e 's/cold_row_dimming/unloaded_row_dimming/g' \
  -e 's/COLD_ROW_DIMMING/UNLOADED_ROW_DIMMING/g' \
  -e 's/DimColdFavicon/DimUnloadedFavicon/g' \
  -e 's/kColorArciumRowTextCold/kColorArciumRowTextUnloaded/g'
```

Then reword the comments that the rename made wrong. In `unloaded_row_dimming.h` the function comment becomes:

```cpp
// A favicon at reduced opacity, for a row whose click has to load a page
// first -- a closed entry, or a tab whose page is not in memory -- so it reads
// as such beside a loaded one at a glance. Shared by TabRowView and
// FavoritesGridView, the two places a row's favicon is drawn.
```

and the comment above `kColorArciumRowTextUnloaded` in `sidebar_colors.h` says the same about the title. Leave the rest of each comment (the image-generator reasoning) as it is.

- [ ] **Step 3: Edit the three `BUILD.gn` files**

In `arcium/ui/sidebar/BUILD.gn`, the sed above already renamed the two source entries; check they read `"unloaded_row_dimming.cc"` and `"unloaded_row_dimming.h"` and are still in sorted order.

In `arcium/browser/BUILD.gn`, add the two sources in sorted order (after `"model_store.h"`) and two deps:

```gn
    "restored_tab_loading.cc",
    "restored_tab_loading.h",
```

```gn
  deps = [
    "//arcium/common",
    "//base",
    "//base:i18n",

    # BrowserContext and WebContentsUserData. No //chrome deps.
    "//content/public/browser",

    # ContentFaviconDriver, for the favicon a restored tab asks for without
    # loading. //components, not //chrome.
    "//components/favicon/content",
  ]
```

In `arcium/test/BUILD.gn`, add `"restored_tab_loading_unittest.cc"` after `"reorder_index_unittest.cc"`, and `"unloaded_rows_unittest.cc"` after `"tab_space_unittest.cc"`.

- [ ] **Step 4: Write the skeletons**

`arcium/browser/restored_tab_loading.h`:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_RESTORED_TAB_LOADING_H_
#define ARCIUM_BROWSER_RESTORED_TAB_LOADING_H_

namespace arcium {}  // namespace arcium

#endif  // ARCIUM_BROWSER_RESTORED_TAB_LOADING_H_
```

`arcium/browser/restored_tab_loading.cc`:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/restored_tab_loading.h"

namespace arcium {}  // namespace arcium
```

`arcium/test/restored_tab_loading_unittest.cc` and `arcium/test/unloaded_rows_unittest.cc` each get the licence header and an empty `namespace arcium {}`, so the target links.

- [ ] **Step 5: Format, build, run the suite**

```bash
scripts/format arcium/browser/restored_tab_loading.h arcium/browser/restored_tab_loading.cc arcium/ui/sidebar/unloaded_row_dimming.h arcium/ui/sidebar/unloaded_row_dimming.cc arcium/ui/sidebar/sidebar_colors.h arcium/ui/sidebar/sidebar_colors.cc arcium/ui/sidebar/tab_row_view.cc arcium/ui/sidebar/favorites_grid_view.cc arcium/test/sidebar_views_unittest.cc arcium/test/restored_tab_loading_unittest.cc arcium/test/unloaded_rows_unittest.cc arcium/browser/BUILD.gn arcium/ui/sidebar/BUILD.gn arcium/test/BUILD.gn
ARCIUM_JOBS=4 scripts/build dev arcium_unittests && /Volumes/Texternal/chromium/src/out/dev/arcium_unittests
```

Expected: builds; 538 tests pass, 1 disabled — the same as at `5dd4a7b`. `grep -rn "cold_row_dimming\|DimColdFavicon\|kColorArciumRowTextCold" arcium` prints nothing.

- [ ] **Step 6: Commit**

```bash
git add arcium/ui/sidebar/unloaded_row_dimming.h arcium/ui/sidebar/unloaded_row_dimming.cc arcium/ui/sidebar/sidebar_colors.h arcium/ui/sidebar/sidebar_colors.cc arcium/ui/sidebar/tab_row_view.cc arcium/ui/sidebar/favorites_grid_view.cc arcium/ui/sidebar/BUILD.gn arcium/test/sidebar_views_unittest.cc arcium/browser/restored_tab_loading.h arcium/browser/restored_tab_loading.cc arcium/browser/BUILD.gn arcium/test/restored_tab_loading_unittest.cc arcium/test/unloaded_rows_unittest.cc arcium/test/BUILD.gn
# git mv in Step 2 already staged the removal of the old cold_row_dimming
# paths, so they are not named again above.
git commit -m "Name the row dimming for what it is about to cover

A closed entry is about to share its look with a tab whose page is not in
memory, so the helper and the colour are renamed from cold to unloaded
before either gains a second meaning. The build files for the restored-tab
rule are edited in the same change so the graph reloads once.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 1: The rule, the flag and the state

**Files:**
- Modify: `arcium/common/arcium_features.h`, `arcium/common/arcium_features.cc`, `arcium/browser/restored_tab_loading.h`, `arcium/browser/restored_tab_loading.cc`
- Test: `arcium/test/restored_tab_loading_unittest.cc`

**Interfaces:**
- Consumes: `features::IsSidebarEnabled()` (`arcium/common/arcium_features.h`).
- Produces:

```cpp
// arcium/common/arcium_features.h
BASE_DECLARE_FEATURE(kArciumNoLoadAtLaunch);
bool IsNoLoadAtLaunchEnabled();  // in namespace arcium::features

// arcium/browser/restored_tab_loading.h
using FaviconRequest = base::RepeatingCallback<void(content::WebContents*)>;
bool DeferRestoredTabLoads(const std::vector<content::WebContents*>& tabs);
bool DeferRestoredTabLoads(const std::vector<content::WebContents*>& tabs,
                           const FaviconRequest& request_favicon);
bool IsTabUnloaded(content::WebContents* contents);
```

- [ ] **Step 1: Write the failing tests**

Replace `arcium/test/restored_tab_loading_unittest.cc`'s body with:

```cpp
#include "arcium/browser/restored_tab_loading.h"

#include <memory>
#include <vector>

#include "arcium/common/arcium_features.h"
#include "base/test/bind.h"
#include "base/test/scoped_feature_list.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/restore_type.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/test_renderer_host.h"
#include "content/public/test/web_contents_tester.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace arcium {
namespace {

class RestoredTabLoadingTest : public content::RenderViewHostTestHarness {
 protected:
  // A tab as session restore leaves it: its history is there, and nothing
  // has loaded it. The same construction session_restore_delegate_unittest.cc
  // uses.
  std::unique_ptr<content::WebContents> RestoredTab(const GURL& url) {
    std::unique_ptr<content::WebContents> contents =
        content::WebContentsTester::CreateTestWebContents(browser_context(),
                                                          nullptr);
    std::vector<std::unique_ptr<content::NavigationEntry>> entries;
    entries.push_back(content::NavigationEntry::Create());
    entries.back()->SetURL(url);
    contents->GetController().Restore(0, content::RestoreType::kRestored,
                                      &entries);
    return contents;
  }

  // A favicon request that only records who it was asked for.
  FaviconRequest Recorder(std::vector<content::WebContents*>* asked) {
    return base::BindLambdaForTesting(
        [asked](content::WebContents* contents) { asked->push_back(contents); });
  }
};

TEST_F(RestoredTabLoadingTest, EveryTabIsAskedForItsFaviconAndNoneLoads) {
  std::unique_ptr<content::WebContents> a = RestoredTab(GURL("https://a.example/"));
  std::unique_ptr<content::WebContents> b = RestoredTab(GURL("https://b.example/"));
  std::vector<content::WebContents*> asked;

  EXPECT_TRUE(DeferRestoredTabLoads({a.get(), b.get()}, Recorder(&asked)));

  EXPECT_EQ((std::vector<content::WebContents*>{a.get(), b.get()}), asked);
  EXPECT_TRUE(a->GetController().NeedsReload());
  EXPECT_TRUE(b->GetController().NeedsReload());
}

// The comparison run: Chromium's own background loader gets the tabs back.
TEST_F(RestoredTabLoadingTest, WithTheRuleSwitchedOffNothingIsTaken) {
  base::test::ScopedFeatureList off;
  off.InitAndDisableFeature(features::kArciumNoLoadAtLaunch);
  std::unique_ptr<content::WebContents> a = RestoredTab(GURL("https://a.example/"));
  std::vector<content::WebContents*> asked;

  EXPECT_FALSE(DeferRestoredTabLoads({a.get()}, Recorder(&asked)));
  EXPECT_TRUE(asked.empty());
}

// A window without the sidebar is stock Chromium, restore included.
TEST_F(RestoredTabLoadingTest, WithTheSidebarSwitchedOffNothingIsTaken) {
  base::test::ScopedFeatureList off;
  off.InitAndDisableFeature(features::kArciumSidebar);
  std::unique_ptr<content::WebContents> a = RestoredTab(GURL("https://a.example/"));
  std::vector<content::WebContents*> asked;

  EXPECT_FALSE(DeferRestoredTabLoads({a.get()}, Recorder(&asked)));
  EXPECT_TRUE(asked.empty());
}

// The overload the hook calls asks the tab's favicon driver, which a test
// WebContents does not have. It must take the tabs anyway, not trip on the
// missing driver.
TEST_F(RestoredTabLoadingTest, TheHooksCallTakesTabsWithNoFaviconDriver) {
  std::unique_ptr<content::WebContents> a = RestoredTab(GURL("https://a.example/"));
  EXPECT_TRUE(DeferRestoredTabLoads({a.get()}));
  EXPECT_TRUE(a->GetController().NeedsReload());
}

TEST_F(RestoredTabLoadingTest, ARestoredTabIsUnloadedUntilItStartsLoading) {
  std::unique_ptr<content::WebContents> a = RestoredTab(GURL("https://a.example/"));
  EXPECT_TRUE(IsTabUnloaded(a.get()));

  a->GetController().LoadIfNecessary();
  EXPECT_FALSE(IsTabUnloaded(a.get()));
}

// Chromium marks a discarded tab with WasDiscarded() and deliberately not
// with NeedsReload() (tab_lifecycle_unit.cc:246), so only reading both finds
// it.
TEST_F(RestoredTabLoadingTest, ADiscardedTabIsUnloadedAndALoadedOneIsNot) {
  std::unique_ptr<content::WebContents> loaded =
      content::WebContentsTester::CreateTestWebContents(browser_context(),
                                                        nullptr);
  content::WebContentsTester::For(loaded.get())
      ->NavigateAndCommit(GURL("https://loaded.example/"));
  EXPECT_FALSE(IsTabUnloaded(loaded.get()));

  loaded->SetWasDiscarded(true);
  EXPECT_TRUE(IsTabUnloaded(loaded.get()));
}

}  // namespace
}  // namespace arcium
```

- [ ] **Step 2: Run them and watch them fail**

```bash
ARCIUM_JOBS=4 scripts/build dev arcium_unittests
```

Expected: compile errors — `DeferRestoredTabLoads`, `IsTabUnloaded`, `FaviconRequest` and `kArciumNoLoadAtLaunch` are undeclared.

- [ ] **Step 3: Add the flag**

In `arcium/common/arcium_features.h`, after `kArciumHomeBoundary`:

```cpp
// After a restart only the tab on screen loads; every other restored tab
// waits for a click (R3.9). Enabled by default;
// --disable-features=ArciumNoLoadAtLaunch brings back Chromium's own
// background loading for a side-by-side comparison.
BASE_DECLARE_FEATURE(kArciumNoLoadAtLaunch);
```

and after `IsHomeBoundaryEnabled()`:

```cpp
// True when restored tabs should wait for a click. Off whenever the sidebar
// is, because a window without it is stock Chromium, restore included.
bool IsNoLoadAtLaunchEnabled();
```

In `arcium/common/arcium_features.cc`, beside the other two `BASE_FEATURE` lines:

```cpp
BASE_FEATURE(kArciumNoLoadAtLaunch, base::FEATURE_ENABLED_BY_DEFAULT);
```

and after `IsHomeBoundaryEnabled()`:

```cpp
bool IsNoLoadAtLaunchEnabled() {
  return IsSidebarEnabled() &&
         base::FeatureList::IsEnabled(kArciumNoLoadAtLaunch);
}
```

- [ ] **Step 4: Write the header**

`arcium/browser/restored_tab_loading.h`, replacing the skeleton's namespace:

```cpp
#include <vector>

#include "base/functional/callback_forward.h"

namespace content {
class WebContents;
}

namespace arcium {

// Asks for one tab's favicon without loading its page.
using FaviconRequest = base::RepeatingCallback<void(content::WebContents*)>;

// Takes the restored tabs session restore would hand to Chromium's
// background loader (patch 0170). When Arcium's no-load-at-launch rule is on
// it asks for each tab's favicon -- the one useful thing the loader did for a
// tab it had not reached yet -- loads none of them, and returns true. Returns
// false, touching nothing, when the rule is off, so the caller goes on to
// Chromium's own loader.
bool DeferRestoredTabLoads(const std::vector<content::WebContents*>& tabs);

// The same, with the favicon request supplied: the rule lives here, and the
// overload above only binds the real favicon driver, so a test can see the
// request made.
bool DeferRestoredTabLoads(const std::vector<content::WebContents*>& tabs,
                           const FaviconRequest& request_favicon);

// A tab exists but its page is not in memory: restored and not yet loaded,
// or discarded to save memory. Clicking it loads the page it was left on.
bool IsTabUnloaded(content::WebContents* contents);

}  // namespace arcium
```

- [ ] **Step 5: Write the source**

`arcium/browser/restored_tab_loading.cc`, replacing the skeleton's namespace:

```cpp
#include "arcium/common/arcium_features.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"

namespace arcium {

namespace {

// The request BackgroundTabLoadingPolicy makes for a tab it has not loaded
// yet (background_tab_loading_policy.cc, ScheduleLoadForRestoredTabs): it
// reads the favicon database and does not load the page. A tab with no
// driver has nothing to ask.
void RequestFaviconFromDriver(content::WebContents* contents) {
  if (auto* driver =
          favicon::ContentFaviconDriver::FromWebContents(contents)) {
    driver->FetchFavicon(driver->GetActiveURL(), /*is_same_document=*/false);
  }
}

}  // namespace

bool DeferRestoredTabLoads(const std::vector<content::WebContents*>& tabs) {
  return DeferRestoredTabLoads(tabs,
                               base::BindRepeating(&RequestFaviconFromDriver));
}

bool DeferRestoredTabLoads(const std::vector<content::WebContents*>& tabs,
                           const FaviconRequest& request_favicon) {
  if (!features::IsNoLoadAtLaunchEnabled()) {
    return false;
  }
  for (content::WebContents* contents : tabs) {
    request_favicon.Run(contents);
  }
  return true;
}

bool IsTabUnloaded(content::WebContents* contents) {
  // Two flags because Chromium keeps two: a restored tab that has not loaded
  // reports NeedsReload(), while a discarded one reports WasDiscarded() and
  // deliberately not NeedsReload() (tab_lifecycle_unit.cc:246).
  return contents->GetController().NeedsReload() ||
         contents->WasDiscarded();
}

}  // namespace arcium
```

- [ ] **Step 6: Run the tests**

```bash
scripts/format arcium/common/arcium_features.h arcium/common/arcium_features.cc arcium/browser/restored_tab_loading.h arcium/browser/restored_tab_loading.cc arcium/test/restored_tab_loading_unittest.cc
ARCIUM_JOBS=4 scripts/build dev arcium_unittests && /Volumes/Texternal/chromium/src/out/dev/arcium_unittests --gtest_filter='RestoredTabLoadingTest.*'
```

Expected: 6 tests pass.

- [ ] **Step 7: Mutation checks**

One at a time, restoring and `touch`-ing the file after each:
- Make `DeferRestoredTabLoads` return `true` before the flag check: both `...SwitchedOffNothingIsTaken` tests must fail.
- Drop the `request_favicon.Run(contents)` line: `EveryTabIsAskedForItsFaviconAndNoneLoads` must fail.
- Drop `|| contents->WasDiscarded()`: `ADiscardedTabIsUnloadedAndALoadedOneIsNot` must fail.

- [ ] **Step 8: Full suite and commit**

```bash
ARCIUM_JOBS=4 scripts/build dev arcium_unittests && /Volumes/Texternal/chromium/src/out/dev/arcium_unittests
git add arcium/common/arcium_features.h arcium/common/arcium_features.cc arcium/browser/restored_tab_loading.h arcium/browser/restored_tab_loading.cc arcium/test/restored_tab_loading_unittest.cc
git commit -m "Restored tabs can be taken without being loaded

The rule session restore will ask: with it on, each tab gets its favicon
and nothing loads; with it off, or with the sidebar off, the tabs go back
to Chromium's own loader for a comparison run. The state the sidebar will
draw is defined once beside it, reading both flags Chromium keeps, since a
discarded tab is not marked as needing a reload.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 2: The hook

**Files:**
- Create: `patches/0170-session-restore-defer-loads.patch`
- Modify (in the Chromium checkout, recorded by the patch): `chrome/browser/sessions/session_restore_delegate.cc`
- Modify: `patches/README.md`

**Interfaces:**
- Consumes: `arcium::DeferRestoredTabLoads(const std::vector<content::WebContents*>&)` from Task 1.

- [ ] **Step 1: Confirm the statistics do not wait on background loads**

Read `chrome/browser/sessions/session_restore_stats_collector.cc`. `ReportStatsAndSelfDestroy` is called on the foreground tab's first paint, on a visibility change, and when the tracked render widgets go away — not on background loads. If that is no longer true in this checkout, stop and report it: the spec's §3.2 rests on it.

- [ ] **Step 2: Edit the Chromium file**

In `/Volumes/Texternal/chromium/src/chrome/browser/sessions/session_restore_delegate.cc`, add the include as the first of the quoted includes, where patch 0150 put its own:

```cpp
#include "arcium/browser/restored_tab_loading.h"
#include "base/compiler_specific.h"
```

and replace the last statement of `SessionRestoreDelegate::RestoreTabs`:

```cpp
  // Arcium: restored tabs load when the user asks for them, not four to
  // twenty at a time in the background (arcium/browser/restored_tab_loading.h).
  if (arcium::DeferRestoredTabLoads(web_contents_vector)) {
    return;
  }
  performance_manager::policies::ScheduleLoadForRestoredTabs(
      std::move(web_contents_vector));
```

`session_restore_delegate.cc` builds in `//chrome/browser/sessions:impl`, which reaches `//arcium/browser` through patch 0125; no GN change.

- [ ] **Step 3: Generate the patch from the checkout, never by hand**

```bash
git -C /Volumes/Texternal/chromium/src diff -- chrome/browser/sessions/session_restore_delegate.cc > /private/tmp/0170.diff
```

Write `patches/0170-session-restore-defer-loads.patch` as this header, a blank line, then the contents of `/private/tmp/0170.diff` unchanged:

```
Seam: SessionRestoreDelegate::RestoreTabs in
      chrome/browser/sessions/session_restore_delegate.cc, at its one call to
      performance_manager::policies::ScheduleLoadForRestoredTabs.
Why: after a restart Chromium loads the tab on screen itself and hands every
     other restored tab to BackgroundTabLoadingPolicy, which loads at least 4
     and up to 20 of them before anyone asks. R3.9 says nothing loads until it
     is clicked. This is the one handoff; Cmd+Shift+T reaches the same policy
     from browser_live_tab_context.cc and is deliberately left alone.
Delegates to: arcium::DeferRestoredTabLoads
```

- [ ] **Step 4: Check the patch set applies and stays idempotent**

```bash
scripts/sync
scripts/sync
```

Expected: both runs succeed; the second reports every patch, 0170 included, as already applied, with no CONFLICT. No other patch touches `session_restore_delegate.cc`.

- [ ] **Step 5: Record the patch in the inventory**

In `patches/README.md`, before `## Monthly rebase routine`, add:

```markdown
Stage 3, R3.9. `0170` stops Chromium loading restored tabs in the background after a restart: the
tab on screen still loads, and every other one waits for a click. It is the only patch in
`session_restore_delegate.cc`, and it needs no GN wiring because `0125` already gives that file's
target `//arcium/browser`.

| Patch | Seam | Delegates to |
|---|---|---|
| `0170-session-restore-defer-loads.patch` | `SessionRestoreDelegate::RestoreTabs` in `chrome/browser/sessions/session_restore_delegate.cc`, at its call to `ScheduleLoadForRestoredTabs` | `arcium::DeferRestoredTabLoads` |
```

- [ ] **Step 6: Build the browser**

```bash
ARCIUM_JOBS=4 scripts/build dev
```

Expected: builds. This is long on a loaded machine; wait in the foreground.

- [ ] **Step 7: Commit**

```bash
git add patches/0170-session-restore-defer-loads.patch patches/README.md
git commit -m "Restored tabs wait for a click instead of loading in the background

Chromium hands every restored tab but the one on screen to a loader that
starts four to twenty pages nobody asked for. One call at that handoff now
asks Arcium first, which keeps the favicon lookups and loads nothing.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 3: Rows know when their tab is unloaded

**Files:**
- Modify: `arcium/ui/sidebar/sidebar_model.h`, `arcium/ui/browser/sidebar_tab_model.cc`, `arcium/ui/browser/sidebar_tab_model_entries.cc`
- Test: `arcium/test/unloaded_rows_unittest.cc`

**Interfaces:**
- Consumes: `arcium::IsTabUnloaded(content::WebContents*)` from Task 1.
- Produces, on `SidebarRow`:

```cpp
  bool is_unloaded = false;
  bool needs_load() const { return is_cold || is_unloaded; }
```

- [ ] **Step 1: Write the failing tests**

Replace `arcium/test/unloaded_rows_unittest.cc`'s body with:

```cpp
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/entry_id.h"
#include "arcium/browser/model/tab_entry.h"
#include "arcium/browser/tab_binding.h"
#include "arcium/ui/browser/sidebar_tab_model.h"
#include "arcium/ui/sidebar/sidebar_model.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/restore_type.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/web_contents_tester.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace arcium {
namespace {

class UnloadedRowsTest : public BrowserWithTestWindowTest {
 protected:
  TabStripModel* strip() { return browser()->tab_strip_model(); }

  // A background tab as session restore leaves it: history, not loaded.
  // Appended, so the tab AddTab made stays at index 0 and stays active.
  tabs::TabInterface* AppendRestoredTab(const GURL& url) {
    std::unique_ptr<content::WebContents> contents =
        content::WebContentsTester::CreateTestWebContents(profile(), nullptr);
    std::vector<std::unique_ptr<content::NavigationEntry>> entries;
    entries.push_back(content::NavigationEntry::Create());
    entries.back()->SetURL(url);
    contents->GetController().Restore(0, content::RestoreType::kRestored,
                                      &entries);
    strip()->AppendWebContents(std::move(contents), /*foreground=*/false);
    return strip()->GetTabAtIndex(strip()->count() - 1);
  }

  std::unique_ptr<SidebarTabModel> MakeModel() {
    return std::make_unique<SidebarTabModel>(strip(), &arcium_model_,
                                             &binding_);
  }

  static std::optional<SidebarRow> RowAt(const SidebarTabModel& model,
                                         int tab_index) {
    for (const SidebarRow& row : model.rows()) {
      if (row.tab_index == tab_index) {
        return row;
      }
    }
    return std::nullopt;
  }

  static std::optional<SidebarRow> RowFor(const SidebarTabModel& model,
                                          EntryId id) {
    for (const SidebarRow& row : model.rows()) {
      if (row.entry_id == id) {
        return row;
      }
    }
    return std::nullopt;
  }

  ArciumModel arcium_model_;
  TabBinding binding_;
};

TEST(SidebarRowTest, NeedsLoadIsColdOrUnloaded) {
  SidebarRow row;
  EXPECT_FALSE(row.needs_load());
  row.is_cold = true;
  EXPECT_TRUE(row.needs_load());
  row.is_cold = false;
  row.is_unloaded = true;
  EXPECT_TRUE(row.needs_load());
}

TEST_F(UnloadedRowsTest, ARestoredTodayTabThatHasNotLoadedIsUnloaded) {
  AddTab(browser(), GURL("https://on-screen.example/"));
  AppendRestoredTab(GURL("https://later.example/"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();

  ASSERT_TRUE(RowAt(*model, 0));
  ASSERT_TRUE(RowAt(*model, 1));
  EXPECT_FALSE(RowAt(*model, 0)->is_unloaded);
  EXPECT_TRUE(RowAt(*model, 1)->is_unloaded);
  EXPECT_FALSE(RowAt(*model, 1)->is_cold);
}

TEST_F(UnloadedRowsTest, ATabThatStartsLoadingIsNoLongerUnloaded) {
  AddTab(browser(), GURL("https://on-screen.example/"));
  AppendRestoredTab(GURL("https://later.example/"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();

  strip()->GetWebContentsAt(1)->GetController().LoadIfNecessary();
  EXPECT_FALSE(RowAt(*model, 1)->is_unloaded);
}

TEST_F(UnloadedRowsTest, ADiscardedTabIsUnloaded) {
  AddTab(browser(), GURL("https://two.example/"));
  AddTab(browser(), GURL("https://one.example/"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();

  strip()->GetWebContentsAt(1)->SetWasDiscarded(true);
  EXPECT_TRUE(RowAt(*model, 1)->is_unloaded);
}

// The row on screen is the one the user is looking at; whatever Chromium's
// flags say for an instant, it is never drawn as waiting for a click.
TEST_F(UnloadedRowsTest, TheActiveRowIsNeverUnloaded) {
  AddTab(browser(), GURL("https://on-screen.example/"));
  std::unique_ptr<SidebarTabModel> model = MakeModel();

  strip()->GetWebContentsAt(0)->SetWasDiscarded(true);
  EXPECT_TRUE(RowAt(*model, 0)->is_active);
  EXPECT_FALSE(RowAt(*model, 0)->is_unloaded);
}

// A pin whose tab came back from the session but has not loaded: it has a
// tab, so it is not cold, and the tab has no page, so it is unloaded.
TEST_F(UnloadedRowsTest, APinWhoseTabHasNotLoadedIsUnloadedNotCold) {
  AddTab(browser(), GURL("https://on-screen.example/"));
  tabs::TabInterface* tab = AppendRestoredTab(GURL("https://pinned.example/"));
  const EntryId id = arcium_model_.AddEntry(
      arcium_model_.spaces().front().id, EntryKind::kPinned,
      GURL("https://pinned.example/"), u"Pinned");
  binding_.Bind(id, tab->GetHandle());
  std::unique_ptr<SidebarTabModel> model = MakeModel();

  ASSERT_TRUE(RowFor(*model, id));
  EXPECT_FALSE(RowFor(*model, id)->is_cold);
  EXPECT_TRUE(RowFor(*model, id)->is_unloaded);
}

// A pin with no tab at all is cold, and cold only.
TEST_F(UnloadedRowsTest, AColdPinIsNotAlsoUnloaded) {
  AddTab(browser(), GURL("https://on-screen.example/"));
  const EntryId id = arcium_model_.AddEntry(
      arcium_model_.spaces().front().id, EntryKind::kPinned,
      GURL("https://cold.example/"), u"Cold");
  std::unique_ptr<SidebarTabModel> model = MakeModel();

  ASSERT_TRUE(RowFor(*model, id));
  EXPECT_TRUE(RowFor(*model, id)->is_cold);
  EXPECT_FALSE(RowFor(*model, id)->is_unloaded);
}

}  // namespace
}  // namespace arcium
```
- [ ] **Step 2: Run them and watch them fail**

```bash
ARCIUM_JOBS=4 scripts/build dev arcium_unittests
```

Expected: compile errors — `SidebarRow` has no `is_unloaded` and no `needs_load()`.

- [ ] **Step 3: Add the field**

In `arcium/ui/sidebar/sidebar_model.h`, after `bool is_cold = false;`:

```cpp
  // A tab exists but its page is not in memory: restored and not yet loaded,
  // or discarded to save memory. Clicking it loads the page it was left on.
  // Never set on a cold row, which has no tab, or on the active row.
  bool is_unloaded = false;

  // Whether clicking this row has to load a page first: a cold entry opens
  // its URL, an unloaded tab reloads its page. The one question the views
  // ask before dimming.
  bool needs_load() const { return is_cold || is_unloaded; }
```

- [ ] **Step 4: Set it where rows for live tabs are built**

In `arcium/ui/browser/sidebar_tab_model.cc`, `SidebarTabModel::RowForTab`, after `row.is_active = ...;`:

```cpp
  row.is_unloaded = !row.is_active && IsTabUnloaded(tab->GetContents());
```

In `arcium/ui/browser/sidebar_tab_model_entries.cc`, `SidebarTabModel::RowForEntry`, in the live-tab half after `row.is_active = ...;`:

```cpp
  row.is_unloaded = !row.is_active && IsTabUnloaded(tab->GetContents());
```

Add `#include "arcium/browser/restored_tab_loading.h"` to both files. No new observer is needed: `SidebarTabModel::OnTabChangedAt` already notifies on every change type, and both a load starting and a discard reach it.

- [ ] **Step 5: Run the tests**

```bash
scripts/format arcium/ui/sidebar/sidebar_model.h arcium/ui/browser/sidebar_tab_model.cc arcium/ui/browser/sidebar_tab_model_entries.cc arcium/test/unloaded_rows_unittest.cc
ARCIUM_JOBS=4 scripts/build dev arcium_unittests && /Volumes/Texternal/chromium/src/out/dev/arcium_unittests --gtest_filter='SidebarRowTest.*:UnloadedRowsTest.*'
```

Expected: 7 tests pass.

- [ ] **Step 6: Mutation checks**

One at a time, restoring and `touch`-ing after each:
- Drop the `RowForTab` line: `ARestoredTodayTabThatHasNotLoadedIsUnloaded` and `ADiscardedTabIsUnloaded` must fail.
- Drop the `RowForEntry` line: `APinWhoseTabHasNotLoadedIsUnloadedNotCold` must fail.
- Drop `!row.is_active &&` from `RowForTab`: `TheActiveRowIsNeverUnloaded` must fail.
- Make `needs_load()` return `is_cold`: `NeedsLoadIsColdOrUnloaded` must fail.

- [ ] **Step 7: Full suite and commit**

```bash
ARCIUM_JOBS=4 scripts/build dev arcium_unittests && /Volumes/Texternal/chromium/src/out/dev/arcium_unittests
git add arcium/ui/sidebar/sidebar_model.h arcium/ui/browser/sidebar_tab_model.cc arcium/ui/browser/sidebar_tab_model_entries.cc arcium/test/unloaded_rows_unittest.cc
git commit -m "A row knows when its tab has no page in memory

After a restart most tabs exist without having loaded, and a tab Chromium
discards to save memory is in the same state. The row reads it from the
tab when it is built, and the sidebar's existing redraw on every tab change
keeps it current, so nothing polls. The active row is never marked.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 4: The views dim every row that needs a load

**Files:**
- Modify: `arcium/ui/sidebar/tab_row_view.cc`, `arcium/ui/sidebar/favorites_grid_view.cc`, `arcium/ui/playground/fake_sidebar_model.h`, `arcium/ui/playground/fake_sidebar_model.cc`, `arcium/ui/playground/sidebar_example.cc`
- Test: `arcium/test/sidebar_views_unittest.cc`

**Interfaces:**
- Consumes: `SidebarRow::needs_load()` and `is_unloaded` from Task 3; `DimUnloadedFavicon` and `kColorArciumRowTextUnloaded` from Task 0.
- Produces: `void FakeSidebarModel::SetUnloaded(int tab_index, bool unloaded);`

- [ ] **Step 1: Give the fake the state**

In `arcium/ui/playground/fake_sidebar_model.h`, after `SetLoading`:

```cpp
  // A tab that exists without having loaded, the way a restart leaves every
  // tab but the one on screen. The real model never marks the active row;
  // neither does this.
  void SetUnloaded(int tab_index, bool unloaded);
```

In `fake_sidebar_model.cc`, after `SetLoading`:

```cpp
void FakeSidebarModel::SetUnloaded(int tab_index, bool unloaded) {
  if (SidebarRow* row = FindByTabIndex(tab_index)) {
    row->is_unloaded = unloaded && !row->is_active;
    Notify();
  }
}
```

- [ ] **Step 2: Write the failing view tests**

In `arcium/test/sidebar_views_unittest.cc`, after `AColdRowsFaviconIsNotTheSameImageAsALoadedRows`:

```cpp
// A tab that exists but has not loaded -- what a restart leaves every tab but
// the one on screen as -- is dimmed exactly as a cold row is: the question
// both answer is whether a click has to load a page. Same URL on both rows so
// is_unloaded is the only thing that can make them differ.
TEST_F(SidebarViewsTest, AnUnloadedRowIsDimmedLikeAColdOne) {
  model_.AddTab(u"Loaded", "https://shared.example/", SidebarSection::kPinned,
                false);
  model_.AddTab(u"Unloaded", "https://shared.example/",
                SidebarSection::kPinned, false);
  model_.SetUnloaded(1, true);
  MakeList(SidebarSection::kPinned);
  Refresh();

  TabRowView* loaded = views::AsViewClass<TabRowView>(list_->children()[0]);
  TabRowView* unloaded = views::AsViewClass<TabRowView>(list_->children()[1]);
  ASSERT_TRUE(loaded);
  ASSERT_TRUE(unloaded);
  ASSERT_FALSE(loaded->row().is_unloaded);
  ASSERT_TRUE(unloaded->row().is_unloaded);

  ASSERT_TRUE(loaded->title_for_testing()->GetRequestedEnabledColor());
  ASSERT_TRUE(unloaded->title_for_testing()->GetRequestedEnabledColor());
  EXPECT_EQ(*loaded->title_for_testing()->GetRequestedEnabledColor(),
            kColorArciumRowText);
  EXPECT_EQ(*unloaded->title_for_testing()->GetRequestedEnabledColor(),
            kColorArciumRowTextUnloaded);

  EXPECT_EQ(SK_AlphaOPAQUE,
            FaviconCenterAlpha(loaded->favicon_for_testing()->GetImage()));
  EXPECT_LT(FaviconCenterAlpha(unloaded->favicon_for_testing()->GetImage()),
            SK_AlphaOPAQUE);
}

TEST_F(SidebarViewsTest, AFavouriteTileForAnUnloadedTabIsDimmed) {
  model_.AddTab(u"Loaded", "https://shared.example/",
                SidebarSection::kFavorites, false);
  model_.AddTab(u"Unloaded", "https://shared.example/",
                SidebarSection::kFavorites, false);
  model_.SetUnloaded(1, true);
  auto* grid =
      contents_->AddChildView(std::make_unique<FavoritesGridView>(&model_));
  grid->SetRows(model_.rows());
  ASSERT_EQ(2u, grid->children().size());

  EXPECT_EQ(SK_AlphaOPAQUE,
            FaviconCenterAlpha(grid->tile_at_for_testing(0)->GetImage(
                views::Button::STATE_NORMAL)));
  EXPECT_LT(FaviconCenterAlpha(grid->tile_at_for_testing(1)->GetImage(
                views::Button::STATE_NORMAL)),
            SK_AlphaOPAQUE);
}
```

If `AddTab` in the fake does not give the second tab `tab_index` 1, look the index up in `model_.rows()` by title instead, and say so in the report.

- [ ] **Step 3: Run them and watch them fail**

```bash
ARCIUM_JOBS=4 scripts/build dev arcium_unittests && /Volumes/Texternal/chromium/src/out/dev/arcium_unittests --gtest_filter='SidebarViewsTest.AnUnloadedRow*:SidebarViewsTest.AFavouriteTileForAnUnloaded*'
```

Expected: both fail — the unloaded row's title is `kColorArciumRowText` and its favicon is opaque.

- [ ] **Step 4: Dim on `needs_load()`**

In `arcium/ui/sidebar/tab_row_view.cc`, the favicon (around line 141) and the title colour (around line 155) test `row_.is_cold`; change both to `row_.needs_load()`, keeping the active check first in the title's ternary. Update the comment above the title colour so it says an active row is never cold and never unloaded, rather than only never cold.

In `arcium/ui/sidebar/favorites_grid_view.cc` (around line 99), change `row.is_cold ? DimUnloadedFavicon(row.favicon) : row.favicon` to test `row.needs_load()`.

- [ ] **Step 5: Show it in the playground**

In `arcium/ui/playground/sidebar_example.cc`, after the `AddColdEntry` calls and before `SetLoading`:

```cpp
  // Two tabs that exist but have not loaded, the state a restart leaves
  // every tab but the one on screen in: they draw dimmed like the entries
  // above.
  for (const SidebarRow& row : model_->rows()) {
    if (row.title == u"Notion" || row.title == u"Hacker News") {
      model_->SetUnloaded(row.tab_index, true);
    }
  }
```

- [ ] **Step 6: Run the tests, build the playground, look at it**

```bash
scripts/format arcium/ui/sidebar/tab_row_view.cc arcium/ui/sidebar/favorites_grid_view.cc arcium/ui/playground/fake_sidebar_model.h arcium/ui/playground/fake_sidebar_model.cc arcium/ui/playground/sidebar_example.cc arcium/test/sidebar_views_unittest.cc
ARCIUM_JOBS=4 scripts/build dev arcium_unittests && /Volumes/Texternal/chromium/src/out/dev/arcium_unittests
ARCIUM_JOBS=4 scripts/build dev arcium_playground
/Volumes/Texternal/chromium/src/out/dev/arcium_playground --snapshot=/private/tmp/unloaded-playground.png
```

Expected: the whole suite passes; the playground builds; the snapshot log lists the Notion and Hacker News rows. The scrolling list's rows do not paint in an offscreen snapshot (known since 2026-09-08); the favourite tiles do.

- [ ] **Step 7: Mutation check**

Put `row_.is_cold` back in `tab_row_view.cc`'s title ternary: `AnUnloadedRowIsDimmedLikeAColdOne` must fail. Restore, `touch`, rebuild.

- [ ] **Step 8: Commit**

```bash
git add arcium/ui/sidebar/tab_row_view.cc arcium/ui/sidebar/favorites_grid_view.cc arcium/ui/playground/fake_sidebar_model.h arcium/ui/playground/fake_sidebar_model.cc arcium/ui/playground/sidebar_example.cc arcium/test/sidebar_views_unittest.cc
git commit -m "Dim every row whose click has to load a page

A closed entry and a tab with no page in memory mean the same thing to the
person looking at the sidebar: clicking will wait for a load. They now look
the same, so after a restart only the tab on screen is bright and each tab
brightens as it starts loading.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 5: Acceptance and the record

Needs the owner at the keyboard for Step 2.

**Files:**
- Create: `docs/stage3-no-load-at-launch-findings.md`
- Modify: `CLAUDE.md`, `docs/superpowers/specs/2026-09-05-arcium-browser-design.md`, `docs/superpowers/specs/2026-09-11-no-load-at-launch-design.md`

- [ ] **Step 1: Suite and browser**

```bash
ARCIUM_JOBS=4 scripts/build dev arcium_unittests && /Volumes/Texternal/chromium/src/out/dev/arcium_unittests
ARCIUM_JOBS=4 scripts/build dev
```

- [ ] **Step 2: The owner runs the acceptance list**

The spec's §8, by hand, with `scripts/run`: A3.4, A3.9.1, A3.9.2 and A3.9.3. Chromium's task manager is under Window → Task Manager; the "Spare Renderer" row is not a tab and does not count. Record what was checked and what was seen for each; a pass with no observation is not a pass.

- [ ] **Step 3: Write the findings**

`docs/stage3-no-load-at-launch-findings.md`, in the shape of `docs/stage3a-findings.md`: what shipped, what the acceptance pass found (every defect and its fix commit), anything carried forward, and the four perf answers. Run `scripts/perf --label no-load-at-launch` and record it in `docs/perf/` only if the machine is quiet; otherwise say it was not run and why.

- [ ] **Step 4: Record the deviation and the status**

In the master spec's §7, after D3-1, add D3-2 with the text of the design spec's §6. In `CLAUDE.md`'s stage table, the Stage 3 row says R3.9 is done, with the date and the findings file. In the design spec's status line, name this plan and the findings file.

- [ ] **Step 5: Commit**

```bash
git add docs/stage3-no-load-at-launch-findings.md CLAUDE.md docs/superpowers/specs/2026-09-05-arcium-browser-design.md docs/superpowers/specs/2026-09-11-no-load-at-launch-design.md
git commit -m "Record what the no-load-at-launch acceptance pass found

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

- [ ] **Step 6: Finish the branch**

Use superpowers:finishing-a-development-branch: verify the suite on the tree about to be integrated, then present the options and wait.
