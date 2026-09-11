# Profiles Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A space can use its own profile — its own cookies, site storage and cache inside the one Chromium profile — so the same site is logged in as two accounts in two spaces at once and both survive a relaunch.

**Architecture:** Every non-default Arcium profile is a storage partition `arcium-<id>` of the one Chromium profile. Four hooks create each new, restored or recreated tab on a `SiteInstance` fixed to its space's profile; a navigation throttle reopens any page that would land in the wrong storage; moving a tab between profiles swaps its contents in place with history kept. Two more hooks keep the partitions' data alive (Chrome's partition cleanup, session cookies), and one puts a warning in front of Chrome's own Clear browsing data.

**Tech Stack:** Chromium 152.0.7977.83 (C++, Views, GN, siso), gtest, in-process browser tests, Arcium's `scripts/`.

**Spec:** `docs/superpowers/specs/2026-09-11-stage-3b-profiles-design.md` (research: `docs/research/stage3b-partition-mechanics.md`, `docs/research/stage3-partition-spike.md`)

## Global Constraints

- All Arcium code lives in `arcium/`. Upstream Chromium files change **only** through numbered patches in `patches/`, each opening with a prose header naming `Seam:`, `Why:` and `Delegates to:` before the first `diff --git`. **Logic never lives in a patch**; a patch is a few lines that call into `arcium/`. A hook passes Chromium's own choice into Arcium and uses what comes back, so the patch carries no condition. GN wiring patches say `Delegates to: nothing. GN wiring carries no call.`
- Generate every patch from the checkout with `git -C /Volumes/Texternal/chromium/src diff -- <file>`, never by hand; then run `scripts/sync` twice and expect the second run to report every patch already applied.
- Always-visible UI is Chromium Views in C++. **Never WebUI. No Swift, AppKit or Cocoa.**
- One window, one `Browser`, one `TabStripModel`. **Profiles are storage partitions inside the single Chromium profile. Never spawn Chromium profiles for spaces.**
- Background spaces do nothing: no timer, no poll, no thumbnail per space.
- **Never write on the UI thread. No sync I/O ever.** Disk work (deleting a cache directory) goes to `base::ThreadPool` with `base::MayBlock()`.
- `arcium/browser` and `arcium/browser/model` carry **no `//chrome` dependency**; the model target's deps are exactly `//base` and `//url`. `arcium/browser` may use `//content/public/browser`.
- `base::DictValue` and `base::ListValue`. **`base::Value::Dict` does not exist in this tree.**
- Files over ~500 lines are a smell; split rather than grow one. `space_bar_view.cc` is at 513 already: new menu code goes in its own file.
- `scripts/format` after every code change. **`git cl format` does not work here.**
- Test-driven: the failing test comes first; after each step delete the code just written and confirm the named test fails (mutation check); `touch` a restored file before rebuilding so the build does not reuse a stale object.
- Commit by explicit path. **Never `git add -A`, `git add .` or `git commit -a`.**
- Commit messages say why, carry no task numbers, and end with `Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>`. Do not push.
- **Do not modify or commit** the untracked `AGENTS.md`. Never commit Chromium sources or build output.
- Work on the branch `stage-3b-profiles` (already cut from `main` at `ba65eb7`).
- User-facing words: "profile", "space", "log out", "Clear every profile". Never "partition" or "storage partition" in UI text.
- Build and test commands, run from the repo root, always in the foreground with a 600000 ms timeout:
  - `ARCIUM_JOBS=4 scripts/build dev arcium_unittests`, then `/Volumes/Texternal/chromium/src/out/dev/arcium_unittests --gtest_filter=<Suite.Name>`
  - `ARCIUM_JOBS=4 scripts/build dev arcium_browsertests`, then `/Volumes/Texternal/chromium/src/out/dev/arcium_browsertests --gtest_filter=<Suite.Name>`
  - `ARCIUM_JOBS=4 scripts/build dev` for the browser.
  - Before any build check `pgrep -f siso`; never start a second build while one runs. If a build is already running, wait with `for i in $(seq 1 55); do pgrep -f siso >/dev/null || break; sleep 10; done`.
  - **Before building the browser or `arcium_browsertests`** (both relink the component libraries the owner's running browser uses) check `pgrep -f "Arcium.app/Contents/MacOS/Arcium"`. If it prints anything, do not build them; wait with `for i in $(seq 1 55); do pgrep -f "Arcium.app/Contents/MacOS/Arcium" >/dev/null || break; sleep 10; done`, and if it is still running after two such waits, report BLOCKED. `arcium_unittests` may always be built. Never kill the owner's processes.
  - A `BUILD.gn` edit costs a `gn gen` plus a long graph reload, which is why Task 0 makes every `BUILD.gn` edit at once, the Chromium ones included.
  - `SidebarViewsTest` archive-list tests can time out once under load; re-run a single timeout before calling it a failure.

## Rulings: where this plan departs from the spec's first draft

The spec was corrected alongside this plan; these are the corrections and why.

- **R1 — A reopened tab keeps its back history.** The spec first said history is lost. `TabStripModel::DiscardWebContentsAt` swaps a tab's contents in place, so the tab keeps its handle, its place, its pinned or favourite entry and its selection; the history is carried by serialising the old entries and `Restore`-ing them into the new contents, never `CopyStateFrom`, which would carry the old partition's `SiteInstance`s. The tab on screen reloads at once; a background tab comes back unloaded and loads when clicked, as after a restart. The page gets no beforeunload.
- **R2 — Chrome's Clear browsing data asks "Clear every profile" or "Only Default", with "Only Default" the default button.** A Cancel that clears nothing cannot be built without WebUI changes: the settings page shows its own "Deleted" message whenever its request finishes (`clear_browsing_data_dialog.ts:431-433`), so a Cancel would be reported as a deletion. Chrome's own removal therefore always runs once its dialog is confirmed, and the warning decides whether every other profile is cleared too. The removal on the other profiles runs from the warning's answer, not from a hook in `ChromeBrowsingDataRemoverDelegate`; other callers of the remover (the extensions browsing-data API) reach only Default.
- **R3 — "The model has loaded" is two facts on `ModelStore`.** `load_finished()` is true once the file has been read, whatever it held; the guard needs only that. `load_succeeded()` is true when the file was read and understood, or there was no file; the partition cleanup needs that, because an unreadable file leaves an empty model whose keep list would erase every profile.
- **R4 — The guard reopens through `WebContents::OpenURL`,** exactly as the home-boundary divert does, so the new tab is created by the same hook as every other new tab. The reopened tab is marked with the URL it was opened for, and the guard never reopens that one navigation again, so a defect in the hooks cannot become an endless chain of tabs.
- **R5 — sessionStorage of a non-default profile does not survive a restart or a reopen.** Chromium saves and recreates only the default partition's session storage (`session_service_base.cc:280-290`, `session_restore.cc:1092-1099`, both with upstream TODOs). Keeping it would mean two more patches that carry logic; accepted instead. localStorage, cookies and IndexedDB are unaffected.
- **R6 — Deleting a profile erases in one of two ways.** A partition no tab has loaded this session is deleted at once, together with its HTTP cache, which Chromium keeps under `~/Library/Caches` rather than in the partition. A loaded partition is emptied in place (cookies, storage and cache through the remover, then `AsyncObliterateStoragePartition`), Chrome's cleanup pref is set, and its directory goes at a later launch when the cleanup runs and the model no longer lists it.
- **R7 — Clearing or erasing a profile loads its partition for the rest of the session** if nothing had. A partition-filtered removal cannot run on a partition that does not exist (`browsing_data_remover_impl.cc:406-407`). Clearing is a rare, asked-for action; the cost is recorded in the performance notes.
- **R8 — No feature flag.** The stage merges whole; a half-finished state is only ever on this branch.
- **R9 — Arcium's blank tab for an empty space is fixed to its profile with `about:blank`** as the SiteInstance URL, since it navigates nowhere until the quick entry sends it somewhere.

## File structure

New, all under `arcium/` or `patches/`:

| File | Responsibility |
|---|---|
| `arcium/browser/model/arcium_profile.h` | `ArciumProfile`, the default profile's fixed id. |
| `arcium/browser/profile_partition.{h,cc}` | The one place a profile becomes storage: its partition and directory, which storage a page belongs in, the guard's rule, SiteInstances for new, recreated and restored tabs, and the prerender answer. Content API only. |
| `arcium/browser/profile_data.{h,cc}` | Clearing one profile's data, and the paths Chrome's cleanup must keep. Content API only. |
| `arcium/ui/browser/new_tab_storage.{h,cc}` | Which space, and so which storage, a tab Chromium's `Navigate` creates belongs to. |
| `arcium/ui/browser/partition_guard_throttle.{h,cc}` | The guard: a navigation throttle that reopens a page landing in the wrong storage. |
| `arcium/ui/browser/profile_reopen.{h,cc}` | Swapping one tab's contents onto another profile's storage, in place. |
| `arcium/ui/browser/profile_actions.{h,cc}` | Changing a space's profile, and deleting a profile, across every window. |
| `arcium/ui/browser/clear_data_warning.{h,cc}` | The warning in front of Chrome's Clear browsing data. |
| `arcium/ui/browser/sidebar_tab_model_profiles.cc` | The sidebar model's profile commands. |
| `arcium/ui/sidebar/profile_colors.{h,cc}` | The eight profile colours. |
| `arcium/ui/sidebar/profile_menu.{h,cc}` | The space menu's Profile submenu and its dialogs. |
| `arcium/ui/playground/fake_sidebar_profiles.cc` | The fake model's profiles. |
| `arcium/test/profile_partition_unittest.cc`, `new_tab_storage_unittest.cc`, `profile_reopen_unittest.cc`, `profile_data_unittest.cc`, `clear_data_warning_unittest.cc`, `sidebar_profiles_unittest.cc`, `profile_menu_unittest.cc` | Unit tests. |
| `arcium/test/browser/profile_browsertest_base.{h,cc}`, `profile_isolation_browsertest.cc`, `profile_restore_browsertest.cc`, `profile_lifecycle_browsertest.cc` | The `arcium_browsertests` target: the automated isolation check (A3b.1) and everything that needs real partitions. |
| `patches/0180-gn-navigator-arcium.patch` | GN: `//chrome/browser/ui/navigator:impl` gets `//arcium/ui/browser`. |
| `patches/0181-new-tab-profile-storage.patch` | Hook in `CreateTargetContents`. |
| `patches/0182-restored-tab-profile-storage.patch` | Hook in `CreateRestoredTab`. |
| `patches/0183-gn-resource-coordinator-arcium.patch` | GN: `//chrome/browser/resource_coordinator:impl` gets `//arcium/browser`. |
| `patches/0184-discard-keeps-storage.patch` | Hook in `TabLifecycleUnit::FinishDiscard`. |
| `patches/0185-prerender-profile-storage.patch` | Hook in `Browser::IsPrerender2Supported`. |
| `patches/0186-navigation-throttle-partition-guard.patch` | Registers the guard beside the home-boundary throttle. |
| `patches/0187-gn-web-applications-arcium.patch` | GN: `//chrome/browser/web_applications` gets `//arcium/browser`. |
| `patches/0188-storage-cleanup-keeps-profiles.patch` | Hook in `GarbageCollectStoragePartitionsCommand::DoGarbageCollection`. |
| `patches/0189-gn-net-arcium.patch` | GN: `//chrome/browser/net:impl` gets `//arcium/browser`. |
| `patches/0190-session-cookies-profiles.patch` | Hook in `ProfileNetworkContextService::ConfigureNetworkContextParamsInternal`. |
| `patches/0191-gn-settings-arcium.patch` | GN: `//chrome/browser/ui/webui/settings:impl` gets `//arcium/ui/browser`. |
| `patches/0192-clear-data-warning.patch` | Hook in `ClearBrowsingDataHandler::HandleClearBrowsingData`. |

Modified: `arcium/browser/model/*` (Task 1), `arcium/browser/model_store.{h,cc}`, `arcium/browser/arcium_profile_state.{h,cc}`, `arcium/browser/tab_space.{h,cc}`, `arcium/ui/browser/space_switcher.cc`, `arcium/ui/browser/space_switcher_spaces.cc`, `arcium/ui/browser/sidebar_tab_model.h`, `arcium/ui/browser/sidebar_tab_model_spaces.cc`, `arcium/ui/sidebar/sidebar_model.h`, `arcium/ui/sidebar/space_bar_view.{h,cc}`, `arcium/ui/playground/fake_sidebar_model.h`, `arcium/ui/playground/fake_sidebar_spaces.cc`, `arcium/ui/playground/sidebar_example.cc`, every `BUILD.gn` named in Task 0, `arcium/BUILD.gn`, `patches/README.md`; in Task 13, `docs/`, `CLAUDE.md` and the two specs.

## Performance

1. **Processes:** none while only Default is used. A profile's first loaded page starts a renderer of its own, because renderers and the spare renderer are never shared across partitions. None at idle.
2. **Idle memory:** per profile that has loaded a page this session, one network context and a partition's storage contexts; A3.3 measures it. Nothing per tab. Clearing or erasing a profile, and "Clear every profile", load the partitions they touch for the rest of the session (R7).
3. **Startup:** nothing before first paint; the model file gains a short list and each session tab one short string. Task 4 checks that a restored, unloaded tab does not create its partition.
4. **UI thread:** creating a partition builds about seventeen storage contexts once per profile per session. The guard is a few map lookups and one string comparison per main-frame navigation of a tab in the sidebar. A reopen serialises one tab's history, which is what session restore already does per tab.

---

### Task 0: Build files, GN wiring and skeletons

Every `BUILD.gn` edit this plan needs, Arcium's and Chromium's, made at once so the graph reloads once. Every new file starts as a skeleton that compiles and does nothing; the task that fills it replaces its whole content. No behaviour changes; the suite stays green.

**Files:**
- Create (skeletons): `arcium/browser/model/arcium_profile.h`, `arcium/browser/profile_partition.{h,cc}`, `arcium/browser/profile_data.{h,cc}`, `arcium/ui/browser/new_tab_storage.{h,cc}`, `arcium/ui/browser/partition_guard_throttle.{h,cc}`, `arcium/ui/browser/profile_reopen.{h,cc}`, `arcium/ui/browser/profile_actions.{h,cc}`, `arcium/ui/browser/clear_data_warning.{h,cc}`, `arcium/ui/browser/sidebar_tab_model_profiles.cc`, `arcium/ui/sidebar/profile_colors.{h,cc}`, `arcium/ui/sidebar/profile_menu.{h,cc}`, `arcium/ui/playground/fake_sidebar_profiles.cc`, `arcium/test/profile_partition_unittest.cc`, `arcium/test/new_tab_storage_unittest.cc`, `arcium/test/profile_reopen_unittest.cc`, `arcium/test/profile_data_unittest.cc`, `arcium/test/clear_data_warning_unittest.cc`, `arcium/test/sidebar_profiles_unittest.cc`, `arcium/test/profile_menu_unittest.cc`, `arcium/test/browser/profile_browsertest_base.{h,cc}`, `arcium/test/browser/profile_isolation_browsertest.cc`, `arcium/test/browser/profile_restore_browsertest.cc`, `arcium/test/browser/profile_lifecycle_browsertest.cc`
- Create: `patches/0180-gn-navigator-arcium.patch`, `patches/0183-gn-resource-coordinator-arcium.patch`, `patches/0187-gn-web-applications-arcium.patch`, `patches/0189-gn-net-arcium.patch`, `patches/0191-gn-settings-arcium.patch`
- Modify: `arcium/browser/BUILD.gn`, `arcium/browser/model/BUILD.gn`, `arcium/ui/browser/BUILD.gn`, `arcium/ui/sidebar/BUILD.gn`, `arcium/ui/playground/BUILD.gn`, `arcium/test/BUILD.gn`, `arcium/BUILD.gn`, `patches/README.md`
- Modify (in the Chromium checkout, recorded by the patches): `chrome/browser/ui/navigator/BUILD.gn`, `chrome/browser/resource_coordinator/BUILD.gn`, `chrome/browser/web_applications/BUILD.gn`, `chrome/browser/net/BUILD.gn`, `chrome/browser/ui/webui/settings/BUILD.gn`

**Interfaces:**
- Produces: the `arcium_browsertests` target, reachable from `//arcium:all`; every file above, compiling.

- [ ] **Step 1: Write the skeletons**

Every header is its guard and nothing else, for example `arcium/browser/profile_partition.h`:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_PROFILE_PARTITION_H_
#define ARCIUM_BROWSER_PROFILE_PARTITION_H_

#endif  // ARCIUM_BROWSER_PROFILE_PARTITION_H_
```

The guard is the path in capitals with `/` and `.` as `_`. Every `.cc` that has a header is the licence block and `#include` of its own header. `arcium/ui/browser/sidebar_tab_model_profiles.cc` and `arcium/ui/playground/fake_sidebar_profiles.cc` include `arcium/ui/browser/sidebar_tab_model.h` and `arcium/ui/playground/fake_sidebar_model.h` respectively. Every test file, unit and browser, is the licence block and `#include "testing/gtest/include/gtest/gtest.h"`; `profile_browsertest_base.cc` includes its header.

- [ ] **Step 2: Arcium's build files**

`arcium/browser/model/BUILD.gn`: add `"arcium_profile.h"` to `sources` in sorted order.

`arcium/browser/BUILD.gn`: add to `sources`, in sorted order after `"model_store.h"`:

```gn
    "profile_data.cc",
    "profile_data.h",
    "profile_partition.cc",
    "profile_partition.h",
```

and to `deps`, after `"//content/public/browser"`:

```gn
    # url_constants.h, for the browser-page schemes profile_partition.cc
    # sorts into shared storage.
    "//content/public/common",
```

`arcium/ui/browser/BUILD.gn`: add to `sources`, each in sorted order: `"clear_data_warning.cc"`, `"clear_data_warning.h"`, `"new_tab_storage.cc"`, `"new_tab_storage.h"`, `"partition_guard_throttle.cc"`, `"partition_guard_throttle.h"`, `"profile_actions.cc"`, `"profile_actions.h"`, `"profile_reopen.cc"`, `"profile_reopen.h"`, `"sidebar_tab_model_profiles.cc"`. Add to `deps`, each in sorted order:

```gn
    # prefs::kShouldGarbageCollectStoragePartitions, which a profile's
    # deletion sets when its partition is still in use.
    "//chrome/common:constants",

    # TimePeriod and its begin and end times, for the clear-data warning.
    "//components/browsing_data/core",

    # ShowWebModal, for the clear-data warning over the settings page.
    "//components/constrained_window",
```

`arcium/ui/sidebar/BUILD.gn`: add `"profile_colors.cc"`, `"profile_colors.h"`, `"profile_menu.cc"`, `"profile_menu.h"` to `sources` in sorted order.

`arcium/ui/playground/BUILD.gn`: in `source_set("fake_model")`, add `"fake_sidebar_profiles.cc"` after `"fake_sidebar_model.h"`.

`arcium/test/BUILD.gn`: add to `arcium_unittests`' `sources`, each in sorted order: `"clear_data_warning_unittest.cc"`, `"new_tab_storage_unittest.cc"`, `"profile_data_unittest.cc"`, `"profile_menu_unittest.cc"`, `"profile_partition_unittest.cc"`, `"profile_reopen_unittest.cc"`, `"sidebar_profiles_unittest.cc"`; add `"//components/browsing_data/core"` to its `deps` in sorted order. Then append a second target:

```gn
# In-process browser tests: a real browser with the sidebar, real storage
# partitions and real navigations. The automated isolation check of the
# profiles spec (A3b.1) lives here, because a partition that leaks shows
# nothing on screen.
test("arcium_browsertests") {
  sources = [
    "browser/profile_browsertest_base.cc",
    "browser/profile_browsertest_base.h",
    "browser/profile_isolation_browsertest.cc",
    "browser/profile_lifecycle_browsertest.cc",
    "browser/profile_restore_browsertest.cc",
  ]

  # As browser_tests and sync_performance_tests set it.
  defines = [ "HAS_OUT_OF_PROC_TEST_RUNNER" ]

  deps = [
    "//arcium/browser",
    "//arcium/browser/model",
    "//arcium/ui/browser",
    "//base/test:test_support",
    "//chrome/browser/ui",
    "//chrome/test:browser_tests_runner",
    "//chrome/test:test_support",
    "//chrome/test:test_support_ui",
    "//components/browsing_data/core",
    "//content/test:test_support",
    "//net:test_support",
    "//testing/gtest",
  ]

  data_deps = [ "//chrome:packed_resources" ]

  if (is_mac) {
    # Browser tests do not run on macOS without it; see
    # sync_performance_tests in chrome/test/BUILD.gn.
    ldflags = [ "-Wl,-ObjC" ]
  }
}
```

`arcium/BUILD.gn`: add `"//arcium/test:arcium_browsertests",` to `group("all")`'s `deps`, first in sorted order.

- [ ] **Step 3: Chromium's build files**

In each file below, add the one dep to the named target's `deps`, with its comment, where `gn format` would put it. None of these files is touched by another patch.

| Patch | File | Target | Dep | Comment |
|---|---|---|---|---|
| 0180 | `chrome/browser/ui/navigator/BUILD.gn` | `source_set("impl")` | `"//arcium/ui/browser"` | `# Arcium: browser_navigator.cc calls arcium/ui/browser/new_tab_storage.h.` |
| 0183 | `chrome/browser/resource_coordinator/BUILD.gn` | the `impl` target at line 77 | `"//arcium/browser"` | `# Arcium: tab_lifecycle_unit.cc calls arcium/browser/profile_partition.h.` |
| 0187 | `chrome/browser/web_applications/BUILD.gn` | `source_set("web_applications")` | `"//arcium/browser"` | `# Arcium: garbage_collect_storage_partitions_command.cc calls arcium/browser/profile_data.h.` |
| 0189 | `chrome/browser/net/BUILD.gn` | `source_set("impl")` | `"//arcium/browser"` | `# Arcium: profile_network_context_service.cc calls arcium/browser/profile_partition.h.` |
| 0191 | `chrome/browser/ui/webui/settings/BUILD.gn` | `source_set("impl")` | `"//arcium/ui/browser"` | `# Arcium: settings_clear_browsing_data_handler.cc calls arcium/ui/browser/clear_data_warning.h.` |

Generate each patch from the checkout, for example:

```bash
git -C /Volumes/Texternal/chromium/src diff -- chrome/browser/ui/navigator/BUILD.gn > /private/tmp/0180.diff
```

and write `patches/<number>-<name>.patch` as a header, a blank line, then the diff unchanged. The header for 0180; the other four follow it with their own file, target and hook patch (0184, 0188, 0190, 0192):

```
Seam: chrome/browser/ui/navigator/BUILD.gn, source_set("impl") -- the target
      browser_navigator.cc builds in.
Why: GN wiring for patch 0181. The hook includes an //arcium/ui/browser
     header, and this target reaches Arcium only through //chrome/browser/ui,
     whose dep on //arcium/ui/browser is private (patch 0010), so `gn check`
     rejects the include even though ninja accepts it.

     No cycle: //arcium/ui/browser depends on nothing that reaches back into
     this target (`gn path` finds no path from it), which is also why
     `gn gen` accepts the graph.
Delegates to: nothing. GN wiring carries no call.
```

Use `//arcium/browser` in the headers of 0183, 0187 and 0189, whose hooks call only content-level Arcium code.

- [ ] **Step 4: Apply, generate and build**

```bash
scripts/sync
scripts/sync
ARCIUM_JOBS=4 scripts/build dev arcium_unittests
/Volumes/Texternal/chromium/src/out/dev/arcium_unittests
```

Expected: the second sync reports every patch already applied; `gn gen` accepts the graph, `arcium_browsertests` included (a visibility error on `//chrome/test:browser_tests_runner` would show here: if it does, replace that dep with `//chrome/test/base:chrome_test_launcher` and add `"//chrome/test/base/browser_tests_main.cc"` is not allowed across directories, so instead copy that file's twelve lines into `arcium/test/browser/browser_tests_main.cc` and list it in `sources`); the suite passes as before (553 tests).

- [ ] **Step 5: Record the wiring in the inventory**

In `patches/README.md`, before `## Monthly rebase routine`, add:

```markdown
Stage 3b, profiles. An Arcium profile is a storage partition inside the one Chromium profile. The
hooks create every new, restored and recreated tab in its space's profile, turn prerendering off
for those tabs, register the guard that reopens a page landing in the wrong storage, keep the
partitions safe from Chrome's own cleanup, keep their session cookies, and put a warning in front
of Chrome's Clear browsing data. Five of the hooked files build in targets that reach Arcium only
privately, so each has a GN wiring patch numbered just before its hook.

| Patch | Seam | Delegates to |
|---|---|---|
| `0180-gn-navigator-arcium.patch` | `chrome/browser/ui/navigator/BUILD.gn`, `source_set("impl")` | nothing: GN wiring for 0181 |
| `0183-gn-resource-coordinator-arcium.patch` | `chrome/browser/resource_coordinator/BUILD.gn`, `impl` | nothing: GN wiring for 0184 |
| `0187-gn-web-applications-arcium.patch` | `chrome/browser/web_applications/BUILD.gn`, `source_set("web_applications")` | nothing: GN wiring for 0188 |
| `0189-gn-net-arcium.patch` | `chrome/browser/net/BUILD.gn`, `source_set("impl")` | nothing: GN wiring for 0190 |
| `0191-gn-settings-arcium.patch` | `chrome/browser/ui/webui/settings/BUILD.gn`, `source_set("impl")` | nothing: GN wiring for 0192 |
```

Each hook task adds its own row to this table, in number order.

- [ ] **Step 6: Commit**

```bash
git add arcium/BUILD.gn arcium/browser/BUILD.gn arcium/browser/model/BUILD.gn arcium/ui/browser/BUILD.gn arcium/ui/sidebar/BUILD.gn arcium/ui/playground/BUILD.gn arcium/test/BUILD.gn \
  arcium/browser/model/arcium_profile.h arcium/browser/profile_partition.h arcium/browser/profile_partition.cc arcium/browser/profile_data.h arcium/browser/profile_data.cc \
  arcium/ui/browser/new_tab_storage.h arcium/ui/browser/new_tab_storage.cc arcium/ui/browser/partition_guard_throttle.h arcium/ui/browser/partition_guard_throttle.cc \
  arcium/ui/browser/profile_reopen.h arcium/ui/browser/profile_reopen.cc arcium/ui/browser/profile_actions.h arcium/ui/browser/profile_actions.cc \
  arcium/ui/browser/clear_data_warning.h arcium/ui/browser/clear_data_warning.cc arcium/ui/browser/sidebar_tab_model_profiles.cc \
  arcium/ui/sidebar/profile_colors.h arcium/ui/sidebar/profile_colors.cc arcium/ui/sidebar/profile_menu.h arcium/ui/sidebar/profile_menu.cc \
  arcium/ui/playground/fake_sidebar_profiles.cc \
  arcium/test/profile_partition_unittest.cc arcium/test/new_tab_storage_unittest.cc arcium/test/profile_reopen_unittest.cc arcium/test/profile_data_unittest.cc \
  arcium/test/clear_data_warning_unittest.cc arcium/test/sidebar_profiles_unittest.cc arcium/test/profile_menu_unittest.cc \
  arcium/test/browser/profile_browsertest_base.h arcium/test/browser/profile_browsertest_base.cc arcium/test/browser/profile_isolation_browsertest.cc \
  arcium/test/browser/profile_restore_browsertest.cc arcium/test/browser/profile_lifecycle_browsertest.cc \
  patches/0180-gn-navigator-arcium.patch patches/0183-gn-resource-coordinator-arcium.patch patches/0187-gn-web-applications-arcium.patch \
  patches/0189-gn-net-arcium.patch patches/0191-gn-settings-arcium.patch patches/README.md
git commit -F - <<'MSG'
Lay out the files and build wiring profiles need

Every build file changes at once so the graph reloads once. Five Chromium
targets that will call into Arcium reach it only through a private dep, so
each gets its own wiring patch; the in-process browser test target is where
a profile that leaks its logins will show, since on screen it shows nothing.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
MSG
```

---

### Task 1: Profiles in the model

The saved model learns profiles: a list of them, Default always first, and a
profile on every space. Schema version 4, with a migration from 3. Nothing
outside the model uses them yet.

**Files:**
- Create: `arcium/browser/model/arcium_profile.h`
- Modify: `arcium/browser/model/entry_id.h`, `arcium/browser/model/space.h`,
  `arcium/browser/model/arcium_model.h`, `arcium/browser/model/arcium_model.cc`,
  `arcium/browser/model/model_serializer.h`, `arcium/browser/model/model_serializer.cc`,
  `arcium/browser/model/model_migration.cc`
- Test: `arcium/test/arcium_model_unittest.cc`, `arcium/test/model_serializer_unittest.cc`,
  `arcium/test/model_migration_unittest.cc`

**Interfaces:**
- Produces, in `arcium/browser/model/entry_id.h`: `using ProfileId = TypedId<ProfileIdTag>;`
- Produces, in `arcium/browser/model/arcium_profile.h`:
  `inline constexpr char kDefaultProfileIdValue[]`, `ProfileId DefaultProfileId()`,
  `struct ArciumProfile { ProfileId id; std::u16string name; int color; int position; }`
- Produces, on `Space`: `ProfileId profile_id` (defaults to `DefaultProfileId()`).
- Produces, on `ArciumModel`:
  `const std::vector<ArciumProfile>& profiles() const`,
  `const ArciumProfile* GetProfile(ProfileId id) const`,
  `ProfileId ProfileOfSpace(SpaceId space) const`,
  `ProfileId AddProfile(const std::u16string& name, int color)`,
  `void RenameProfile(ProfileId id, const std::u16string& name)`,
  `void SetProfileColor(ProfileId id, int color)`,
  `void RemoveProfile(ProfileId id)`,
  `void SetSpaceProfile(SpaceId space, ProfileId profile)`,
  `SpaceId AddSpace(const std::u16string& name, ProfileId profile = DefaultProfileId())`,
  and `ReplaceAll(std::vector<ArciumProfile>, std::vector<Space>, std::vector<Folder>, std::vector<TabEntry>)`.

- [ ] **Step 1: Write the failing model tests**

Append to `arcium/test/arcium_model_unittest.cc`, inside `namespace arcium { namespace {`, and add `#include "arcium/browser/model/arcium_profile.h"`:

```cpp
TEST_F(ArciumModelTest, StartsWithOnlyTheDefaultProfile) {
  ASSERT_EQ(1u, model_.profiles().size());
  EXPECT_EQ(DefaultProfileId(), model_.profiles()[0].id);
  EXPECT_EQ(u"Default", model_.profiles()[0].name);
  EXPECT_EQ(DefaultProfileId(), model_.spaces()[0].profile_id);
}

TEST_F(ArciumModelTest, ANewSpaceUsesTheProfileItIsGiven) {
  const ProfileId work = model_.AddProfile(u"Work", 2);
  const SpaceId space = model_.AddSpace(u"Office", work);
  EXPECT_EQ(work, model_.ProfileOfSpace(space));
  EXPECT_EQ(DefaultProfileId(), model_.ProfileOfSpace(model_.AddSpace(u"Home")));
}

TEST_F(ArciumModelTest, ANewSpaceGivenAnUnknownProfileUsesDefault) {
  const SpaceId space =
      model_.AddSpace(u"Office", ProfileId::FromString(
                                     "11111111-1111-4111-8111-111111111111"));
  EXPECT_EQ(DefaultProfileId(), model_.ProfileOfSpace(space));
}

TEST_F(ArciumModelTest, AddProfileAppendsANamedColouredProfileAndNotifies) {
  CountingObserver observer;
  model_.AddObserver(&observer);
  const ProfileId work = model_.AddProfile(u"Work", 3);
  model_.RemoveObserver(&observer);

  EXPECT_EQ(1, observer.count);
  ASSERT_EQ(2u, model_.profiles().size());
  const ArciumProfile* profile = model_.GetProfile(work);
  ASSERT_TRUE(profile);
  EXPECT_EQ(u"Work", profile->name);
  EXPECT_EQ(3, profile->color);
  EXPECT_EQ(1, profile->position);
}

TEST_F(ArciumModelTest, AProfileCanBeRenamedAndRecoloured) {
  const ProfileId work = model_.AddProfile(u"Work", 1);
  model_.RenameProfile(work, u"Job");
  model_.SetProfileColor(work, 5);
  EXPECT_EQ(u"Job", model_.GetProfile(work)->name);
  EXPECT_EQ(5, model_.GetProfile(work)->color);
}

TEST_F(ArciumModelTest, TheDefaultProfileCanBeRenamedButNotRemoved) {
  model_.RenameProfile(DefaultProfileId(), u"Personal");
  model_.RemoveProfile(DefaultProfileId());
  ASSERT_EQ(1u, model_.profiles().size());
  EXPECT_EQ(u"Personal", model_.profiles()[0].name);
}

TEST_F(ArciumModelTest, RemovingAProfileMovesItsSpacesToDefault) {
  const ProfileId work = model_.AddProfile(u"Work", 1);
  const SpaceId office = model_.AddSpace(u"Office", work);
  const SpaceId lab = model_.AddSpace(u"Lab", work);

  model_.RemoveProfile(work);

  EXPECT_FALSE(model_.GetProfile(work));
  EXPECT_EQ(DefaultProfileId(), model_.ProfileOfSpace(office));
  EXPECT_EQ(DefaultProfileId(), model_.ProfileOfSpace(lab));
  ASSERT_EQ(1u, model_.profiles().size());
  EXPECT_EQ(0, model_.profiles()[0].position);
}

TEST_F(ArciumModelTest, SetSpaceProfileIgnoresAnUnknownProfile) {
  const ProfileId work = model_.AddProfile(u"Work", 1);
  const SpaceId space = model_.default_space_id();
  model_.SetSpaceProfile(space, work);
  EXPECT_EQ(work, model_.ProfileOfSpace(space));

  model_.SetSpaceProfile(
      space, ProfileId::FromString("22222222-2222-4222-8222-222222222222"));
  EXPECT_EQ(work, model_.ProfileOfSpace(space));
}

TEST_F(ArciumModelTest, AnUnknownSpaceHasTheDefaultProfile) {
  EXPECT_EQ(DefaultProfileId(),
            model_.ProfileOfSpace(SpaceId::FromString(
                "33333333-3333-4333-8333-333333333333")));
}
```

- [ ] **Step 2: Write the failing serializer and migration tests**

Append to `arcium/test/model_serializer_unittest.cc` (add `#include "arcium/browser/model/arcium_profile.h"`):

```cpp
TEST(ModelSerializerTest, RoundTripPreservesProfilesAndEachSpacesProfile) {
  ArciumModel original;
  const ProfileId work = original.AddProfile(u"Work", 4);
  const SpaceId office = original.AddSpace(u"Office", work);

  ArciumModel restored;
  ASSERT_TRUE(DeserializeModel(SerializeModel(original), &restored));

  ASSERT_EQ(2u, restored.profiles().size());
  EXPECT_EQ(DefaultProfileId(), restored.profiles()[0].id);
  const ArciumProfile* profile = restored.GetProfile(work);
  ASSERT_TRUE(profile);
  EXPECT_EQ(u"Work", profile->name);
  EXPECT_EQ(4, profile->color);
  EXPECT_EQ(work, restored.ProfileOfSpace(office));
  EXPECT_EQ(DefaultProfileId(),
            restored.ProfileOfSpace(restored.default_space_id()));
}

TEST(ModelSerializerTest, ASpaceNamingAMissingProfileFallsBackToDefault) {
  ArciumModel original;
  const ProfileId work = original.AddProfile(u"Work", 1);
  const SpaceId office = original.AddSpace(u"Office", work);
  base::DictValue dict = SerializeModel(original);
  // Drop Work from the list but leave the space pointing at it.
  base::ListValue* profiles = dict.FindList("profiles");
  ASSERT_TRUE(profiles);
  profiles->EraseIf([&](const base::Value& value) {
    const std::string* id = value.GetDict().FindString("id");
    return id && *id == work.value();
  });

  ArciumModel restored;
  ASSERT_TRUE(DeserializeModel(dict, &restored));
  EXPECT_EQ(DefaultProfileId(), restored.ProfileOfSpace(office));
}

TEST(ModelSerializerTest, AFileWithoutTheDefaultProfileStillHasIt) {
  ArciumModel original;
  base::DictValue dict = SerializeModel(original);
  dict.Set("profiles", base::ListValue());

  ArciumModel restored;
  ASSERT_TRUE(DeserializeModel(dict, &restored));
  ASSERT_EQ(1u, restored.profiles().size());
  EXPECT_EQ(DefaultProfileId(), restored.profiles()[0].id);
}
```

Append to `arcium/test/model_migration_unittest.cc` (add `#include "arcium/browser/model/arcium_profile.h"`):

```cpp
TEST(ModelMigrationTest, AVersionThreeFileGainsTheDefaultProfileOnEverySpace) {
  base::DictValue dict = DictAtVersion(3);
  base::DictValue space;
  space.Set("id", "44444444-4444-4444-8444-444444444444");
  space.Set("name", "Space");
  dict.FindList("spaces")->Append(std::move(space));

  std::optional<base::DictValue> migrated = MigrateModelDict(std::move(dict));

  ASSERT_TRUE(migrated.has_value());
  EXPECT_EQ(kModelSchemaVersion, migrated->FindInt("version"));
  const base::ListValue* profiles = migrated->FindList("profiles");
  ASSERT_TRUE(profiles);
  ASSERT_EQ(1u, profiles->size());
  EXPECT_EQ(kDefaultProfileIdValue,
            *(*profiles)[0].GetDict().FindString("id"));
  EXPECT_EQ("Default", *(*profiles)[0].GetDict().FindString("name"));
  EXPECT_EQ(kDefaultProfileIdValue,
            *(*migrated->FindList("spaces"))[0].GetDict().FindString(
                "profile_id"));
}
```

- [ ] **Step 3: Run the tests to see them fail**

Run: `ARCIUM_JOBS=4 scripts/build dev arcium_unittests`
Expected: compile errors — `ProfileId`, `DefaultProfileId`, `profiles()`,
`AddProfile` and `kDefaultProfileIdValue` are not declared.

- [ ] **Step 4: Add the id and the profile struct**

In `arcium/browser/model/entry_id.h`, beside the other tags:

```cpp
struct ProfileIdTag;
```

and beside the other aliases:

```cpp
using ProfileId = TypedId<ProfileIdTag>;
```

Create `arcium/browser/model/arcium_profile.h`:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_MODEL_ARCIUM_PROFILE_H_
#define ARCIUM_BROWSER_MODEL_ARCIUM_PROFILE_H_

#include <string>

#include "arcium/browser/model/entry_id.h"

namespace arcium {

// The Default profile's id. Fixed rather than generated, so every build and
// every file names it the same way and no generated id can collide with it.
// Well formed, because TypedId::FromString refuses anything that is not.
inline constexpr char kDefaultProfileIdValue[] =
    "00000000-0000-4000-8000-000000000000";

inline ProfileId DefaultProfileId() {
  return ProfileId::FromString(kDefaultProfileIdValue);
}

// An Arcium profile: a set of logins inside the one Chromium profile. The
// Default profile is Chromium's default storage partition; every other
// profile is a partition of its own, named from its id
// (arcium/browser/profile_partition.h). Not to be confused with
// ArciumProfileState, which is Arcium's per-Chromium-profile state holder.
struct ArciumProfile {
  ProfileId id;
  std::u16string name;
  // An index into the fixed palette in arcium/ui/sidebar/profile_colors.h.
  int color = 0;
  int position = 0;
};

}  // namespace arcium

#endif  // ARCIUM_BROWSER_MODEL_ARCIUM_PROFILE_H_
```

In `arcium/browser/model/space.h`, add `#include "arcium/browser/model/arcium_profile.h"` and, after `int position = 0;`:

```cpp
  // The profile whose logins this space's tabs use. Defaulted in the struct,
  // not by callers: Space() is default-constructed to read defaults from
  // (ArchiveService::TimeoutForTab), and an invalid id there would be a space
  // with no storage at all.
  ProfileId profile_id = DefaultProfileId();
```

- [ ] **Step 5: Give the model its profiles**

In `arcium/browser/model/arcium_model.h`, add `#include "arcium/browser/model/arcium_profile.h"`; replace

```cpp
  SpaceId AddSpace(const std::u16string& name);
```

with

```cpp
  // A new space on `profile`, or on Default when `profile` is unknown.
  SpaceId AddSpace(const std::u16string& name,
                   ProfileId profile = DefaultProfileId());
  // Moves a space to another profile. An unknown space or profile changes
  // nothing. The model only records it: reopening the space's open tabs in
  // their new storage is the window's job (SpaceSwitcher).
  void SetSpaceProfile(SpaceId space, ProfileId profile);
```

and after `void SetArchiveTimeout(SpaceId space_id, ArchiveTimeout timeout);` add:

```cpp
  // Profiles, in position order. Default is always present and always first.
  const std::vector<ArciumProfile>& profiles() const { return profiles_; }
  const ArciumProfile* GetProfile(ProfileId id) const;
  // The profile `space` uses. Default for a space the model does not have,
  // so a caller always gets storage it can use.
  ProfileId ProfileOfSpace(SpaceId space) const;
  ProfileId AddProfile(const std::u16string& name, int color);
  void RenameProfile(ProfileId id, const std::u16string& name);
  void SetProfileColor(ProfileId id, int color);
  // Removes a profile. Every space on it moves to Default, the way a removed
  // folder's contents move up rather than vanish. Refuses Default: every
  // space needs a profile, and Default is the one that always exists.
  void RemoveProfile(ProfileId id);
```

Replace `ReplaceAll`'s declaration with:

```cpp
  void ReplaceAll(std::vector<ArciumProfile> profiles,
                  std::vector<Space> spaces,
                  std::vector<Folder> folders,
                  std::vector<TabEntry> entries);
```

In the private section, beside `Space* FindSpace(SpaceId id);`:

```cpp
  ArciumProfile* FindProfile(ProfileId id);
  // Puts Default first and renumbers positions; points any space whose
  // profile is gone at Default. The one place those two invariants are kept.
  void NormaliseProfiles();
```

and beside `std::vector<Space> spaces_;`:

```cpp
  std::vector<ArciumProfile> profiles_;
```

In `arcium/browser/model/arcium_model.cc`, add to the anonymous namespace (or create one after the includes):

```cpp
ArciumProfile MakeDefaultProfile() {
  ArciumProfile profile;
  profile.id = DefaultProfileId();
  profile.name = u"Default";
  return profile;
}
```

Make the constructor seed Default:

```cpp
ArciumModel::ArciumModel() {
  profiles_.push_back(MakeDefaultProfile());
  Space space;
  space.id = SpaceId::Generate();
  space.name = u"Space";
  space.archive_timeout = ArchiveTimeout::kTwelveHours;
  space.position = 0;
  space.profile_id = DefaultProfileId();
  spaces_.push_back(std::move(space));
}
```

Replace `AddSpace`:

```cpp
SpaceId ArciumModel::AddSpace(const std::u16string& name, ProfileId profile) {
  Space space;
  space.id = SpaceId::Generate();
  space.name = name;
  space.position = static_cast<int>(spaces_.size());
  space.profile_id = GetProfile(profile) ? profile : DefaultProfileId();
  const SpaceId id = space.id;
  spaces_.push_back(std::move(space));
  Notify();
  return id;
}

void ArciumModel::SetSpaceProfile(SpaceId space_id, ProfileId profile) {
  Space* space = FindSpace(space_id);
  if (!space || !GetProfile(profile) || space->profile_id == profile) {
    return;
  }
  space->profile_id = profile;
  Notify();
}
```

After `SetArchiveTimeout`, add:

```cpp
const ArciumProfile* ArciumModel::GetProfile(ProfileId id) const {
  for (const ArciumProfile& profile : profiles_) {
    if (profile.id == id) {
      return &profile;
    }
  }
  return nullptr;
}

ProfileId ArciumModel::ProfileOfSpace(SpaceId space) const {
  const Space* found = GetSpace(space);
  return found && GetProfile(found->profile_id) ? found->profile_id
                                                 : DefaultProfileId();
}

ProfileId ArciumModel::AddProfile(const std::u16string& name, int color) {
  ArciumProfile profile;
  profile.id = ProfileId::Generate();
  profile.name = name;
  profile.color = color;
  profile.position = static_cast<int>(profiles_.size());
  const ProfileId id = profile.id;
  profiles_.push_back(std::move(profile));
  Notify();
  return id;
}

void ArciumModel::RenameProfile(ProfileId id, const std::u16string& name) {
  ArciumProfile* profile = FindProfile(id);
  if (!profile || profile->name == name) {
    return;
  }
  profile->name = name;
  Notify();
}

void ArciumModel::SetProfileColor(ProfileId id, int color) {
  ArciumProfile* profile = FindProfile(id);
  if (!profile || profile->color == color) {
    return;
  }
  profile->color = color;
  Notify();
}

void ArciumModel::RemoveProfile(ProfileId id) {
  if (id == DefaultProfileId() || !GetProfile(id)) {
    return;
  }
  std::erase_if(profiles_,
                [id](const ArciumProfile& p) { return p.id == id; });
  NormaliseProfiles();
  Notify();
}
```

Replace `ReplaceAll` so it takes and keeps profiles:

```cpp
void ArciumModel::ReplaceAll(std::vector<ArciumProfile> profiles,
                             std::vector<Space> spaces,
                             std::vector<Folder> folders,
                             std::vector<TabEntry> entries) {
  profiles_ = std::move(profiles);
  spaces_ = std::move(spaces);
  folders_ = std::move(folders);
  entries_ = std::move(entries);
  if (spaces_.empty()) {
    Space space;
    space.id = SpaceId::Generate();
    space.name = u"Space";
    space.profile_id = DefaultProfileId();
    spaces_.push_back(std::move(space));
  }
  // Loaded order is not trusted to already be position order, and
  // spaces().front() has to be the first space.
  std::sort(spaces_.begin(), spaces_.end(), [](const Space& a, const Space& b) {
    return a.position < b.position;
  });
  // Renumbered too: a gap a hand-edited file left would let a later
  // AddSpace, which takes position size(), sort ahead of a loaded space on
  // the next load.
  for (size_t i = 0; i < spaces_.size(); ++i) {
    spaces_[i].position = static_cast<int>(i);
  }
  if (!GetSpace(last_active_space_)) {
    last_active_space_ = SpaceId();
  }
  NormaliseProfiles();
  NormalisePositions();
  Notify();
}
```

Beside `FindSpace`, add:

```cpp
ArciumProfile* ArciumModel::FindProfile(ProfileId id) {
  for (ArciumProfile& profile : profiles_) {
    if (profile.id == id) {
      return &profile;
    }
  }
  return nullptr;
}

void ArciumModel::NormaliseProfiles() {
  // Keep the first Default the list holds, whole -- its name and colour are
  // the owner's to change and must survive a save -- and drop any second
  // copy and any row with an unusable id.
  std::optional<ArciumProfile> saved_default;
  std::erase_if(profiles_, [&](const ArciumProfile& p) {
    if (p.id == DefaultProfileId()) {
      if (!saved_default) {
        saved_default = p;
      }
      return true;
    }
    return !p.id.is_valid();
  });
  std::sort(profiles_.begin(), profiles_.end(),
            [](const ArciumProfile& a, const ArciumProfile& b) {
              return a.position < b.position;
            });
  profiles_.insert(profiles_.begin(),
                   saved_default ? *saved_default : MakeDefaultProfile());
  for (size_t i = 0; i < profiles_.size(); ++i) {
    profiles_[i].position = static_cast<int>(i);
  }
  for (Space& space : spaces_) {
    if (!GetProfile(space.profile_id)) {
      space.profile_id = DefaultProfileId();
    }
  }
}
```

Add `#include <optional>` to `arcium_model.cc` if it is not already there.

Update every other caller of `ReplaceAll` in `arcium/` (there is one, in
`model_serializer.cc`, handled in Step 6; `grep -rn "ReplaceAll(" arcium/`
to confirm no test calls it directly, and pass an empty profile list where one
does — `NormaliseProfiles` supplies Default).

- [ ] **Step 6: Save and load profiles**

In `arcium/browser/model/model_serializer.h`, replace the version comment's
last paragraph and the constant with:

```cpp
// 3: spaces gained `icon`, `gradient` and `last_active_tab`, and the model
//    gained `last_active_space`. Bumped for the same reason 2 was: a Stage 2
//    build opening a Stage 3 file would drop all four on its next save.
// 4: the model gained `profiles`, and spaces gained `profile_id`. Bumped so a
//    build without profiles refuses the file rather than saving every space
//    back onto Default and losing which logins it used.
inline constexpr int kModelSchemaVersion = 4;
```

In `arcium/browser/model/model_serializer.cc`, add
`#include "arcium/browser/model/arcium_profile.h"`. In `SerializeModel`, before
the spaces loop:

```cpp
  base::ListValue profiles;
  for (const ArciumProfile& profile : model.profiles()) {
    base::DictValue value;
    value.Set("id", profile.id.value());
    value.Set("name", base::UTF16ToUTF8(profile.name));
    value.Set("color", profile.color);
    value.Set("position", profile.position);
    profiles.Append(std::move(value));
  }
  dict.Set("profiles", std::move(profiles));
```

and inside the spaces loop, after `value.Set("gradient", space.gradient);`:

```cpp
    value.Set("profile_id", space.profile_id.value());
```

In `DeserializeModel`, before the spaces are read:

```cpp
  // Rows with an unusable id are skipped; ArciumModel::ReplaceAll puts
  // Default first whether or not the file had it, and points any space whose
  // profile is missing at Default. The file is not trusted to be whole.
  std::vector<ArciumProfile> profiles;
  if (const base::ListValue* list = dict.FindList("profiles")) {
    for (const base::Value& item : *list) {
      const base::DictValue* value = item.GetIfDict();
      if (!value) {
        continue;
      }
      const std::string* id = value->FindString("id");
      ArciumProfile profile;
      profile.id = id ? ProfileId::FromString(*id) : ProfileId();
      if (!profile.id.is_valid()) {
        continue;
      }
      const std::string* name = value->FindString("name");
      profile.name = name ? base::UTF8ToUTF16(*name) : u"Profile";
      profile.color = value->FindInt("color").value_or(0);
      profile.position = value->FindInt("position").value_or(0);
      profiles.push_back(std::move(profile));
    }
  }
```

inside the spaces loop, after `space.gradient = value->FindInt("gradient").value_or(0);`:

```cpp
      const std::string* profile_id = value->FindString("profile_id");
      space.profile_id =
          profile_id ? ProfileId::FromString(*profile_id) : DefaultProfileId();
```

and change the final call to:

```cpp
  model->ReplaceAll(std::move(profiles), std::move(spaces), std::move(folders),
                    std::move(entries));
```

- [ ] **Step 7: Migrate version 3 files**

In `arcium/browser/model/model_migration.cc`, add
`#include "arcium/browser/model/arcium_profile.h"` and after `MigrateV2ToV3`:

```cpp
// Version 3 -> 4: the model gained profiles and every space a profile_id.
//
// Writes both, for MigrateV2ToV3's reason: every space in a version 3 file
// used the one storage there was, which is Default's, so Default is the
// honest answer and the migration says so rather than leaving it to a
// reader's fallback.
bool MigrateV3ToV4(base::DictValue& dict) {
  base::DictValue default_profile;
  default_profile.Set("id", kDefaultProfileIdValue);
  default_profile.Set("name", "Default");
  default_profile.Set("color", 0);
  default_profile.Set("position", 0);
  base::ListValue profiles;
  profiles.Append(std::move(default_profile));
  dict.Set("profiles", std::move(profiles));
  if (base::ListValue* spaces = dict.FindList("spaces")) {
    for (base::Value& item : *spaces) {
      if (base::DictValue* space = item.GetIfDict()) {
        space->Set("profile_id", kDefaultProfileIdValue);
      }
    }
  }
  return true;
}
```

and change the steps table to:

```cpp
constexpr MigrationStep kSteps[] = {&MigrateV1ToV2, &MigrateV2ToV3,
                                    &MigrateV3ToV4};
```

- [ ] **Step 8: Run the tests to see them pass**

```bash
scripts/format arcium/browser/model/*.h arcium/browser/model/*.cc arcium/test/arcium_model_unittest.cc arcium/test/model_serializer_unittest.cc arcium/test/model_migration_unittest.cc
ARCIUM_JOBS=4 scripts/build dev arcium_unittests
/Volumes/Texternal/chromium/src/out/dev/arcium_unittests --gtest_filter='ArciumModelTest.*:ModelSerializerTest.*:ModelMigrationTest.*'
```

Expected: all pass, including every test that existed before.

- [ ] **Step 9: Mutation checks**

Make each change, rebuild, confirm the named test fails, then restore the code
and `touch` the file:

| Mutation | Test that must fail |
|---|---|
| `RemoveProfile` skips `NormaliseProfiles()` | `RemovingAProfileMovesItsSpacesToDefault` |
| `AddSpace` stores `profile` without the `GetProfile` check | `ANewSpaceGivenAnUnknownProfileUsesDefault` |
| `NormaliseProfiles` always inserts `MakeDefaultProfile()` | a new round trip of a renamed Default: add `TEST(ModelSerializerTest, ARenamedDefaultProfileKeepsItsName)` that calls `RenameProfile(DefaultProfileId(), u"Personal")`, round-trips, and expects `u"Personal"` — write it now, before this mutation |
| `SerializeModel` omits `profile_id` | `RoundTripPreservesProfilesAndEachSpacesProfile` |
| `MigrateV3ToV4` skips the spaces loop | `AVersionThreeFileGainsTheDefaultProfileOnEverySpace` |

- [ ] **Step 10: Full suite, then commit**

```bash
/Volumes/Texternal/chromium/src/out/dev/arcium_unittests
git add arcium/browser/model/entry_id.h arcium/browser/model/arcium_profile.h arcium/browser/model/space.h arcium/browser/model/arcium_model.h arcium/browser/model/arcium_model.cc arcium/browser/model/model_serializer.h arcium/browser/model/model_serializer.cc arcium/browser/model/model_migration.cc arcium/test/arcium_model_unittest.cc arcium/test/model_serializer_unittest.cc arcium/test/model_migration_unittest.cc
git commit -F - <<'MSG'
Every space now names the profile whose logins it uses

A profile is a name and a colour; Default always exists, first, and every
space from an older file is on it. The model only records the choice, so a
build without profiles refuses the file instead of flattening it.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
MSG
```

---

### Task 2: When the model has loaded, a profile's storage, and the profile in the session

Three foundations the hooks stand on: whether the model file has been read (and understood), the one mapping from a profile to its storage, and the profile id written beside each tab's space in the session file, because restore creates tabs before the model file has been read.

**Files:**
- Modify: `arcium/browser/model_store.h`, `arcium/browser/model_store.cc`, `arcium/browser/arcium_profile_state.h`, `arcium/browser/tab_space.h`, `arcium/browser/tab_space.cc`
- Replace (skeletons from Task 0): `arcium/browser/profile_partition.h`, `arcium/browser/profile_partition.cc`, `arcium/test/profile_partition_unittest.cc`
- Test: `arcium/test/model_store_unittest.cc`, `arcium/test/arcium_profile_state_unittest.cc`, `arcium/test/tab_space_unittest.cc`, `arcium/test/profile_partition_unittest.cc`

**Interfaces:**
- Consumes (Task 1): `ProfileId`, `DefaultProfileId()`, `ArciumModel::AddProfile`, `ArciumModel::ProfileOfSpace`, `ArciumModel::AddSpace(name, profile)`.
- Produces, on `ModelStore`: `bool load_finished() const`, `bool load_succeeded() const`.
- Produces, on `ArciumProfileState`: `bool model_load_finished() const`, `bool model_load_succeeded() const`.
- Produces, in `arcium/browser/tab_space.h`: `inline constexpr char kProfileIdExtraDataKey[] = "arcium.profile_id";` and `ProfileId ProfileIdFromExtraData(const std::map<std::string, std::string>& extra_data)`.
- Produces, in `arcium/browser/profile_partition.h`: `kPartitionDomainPrefix`, `std::string PartitionDomainForProfile(const ProfileId&)`, `bool IsArciumPartitionDomain(std::string_view)`, `bool IsArciumPartitionPath(const base::FilePath&)`, `base::FilePath PartitionDirectory(const base::FilePath& profile_path, const ProfileId&)`, `std::optional<content::StoragePartitionConfig> PartitionForProfile(content::BrowserContext*, const ProfileId&)`, `bool IsPartitionLoaded(content::BrowserContext*, const ProfileId&)`, `enum class PageStorage { kAny, kShared, kProfile }`, `PageStorage StorageForUrl(const GURL&)`, `scoped_refptr<content::SiteInstance> SiteInstanceForProfile(content::BrowserContext*, const ProfileId&, const GURL&)`, `std::string PartitionDomainOfTab(content::WebContents*)`, `bool IsInRightStorage(const GURL&, std::string_view tab_partition_domain, const ProfileId& space_profile)`.

- [ ] **Step 1: Write the failing load-state tests**

Append to `arcium/test/model_store_unittest.cc`, inside the anonymous namespace:

```cpp
TEST_F(ModelStoreTest, ALoadWithNoFileFinishesAndSucceeds) {
  ArciumModel model;
  ModelStore store(&model, path());
  EXPECT_FALSE(store.load_finished());
  EXPECT_FALSE(store.load_succeeded());
  base::RunLoop loop;
  store.Load(loop.QuitClosure());
  loop.Run();
  EXPECT_TRUE(store.load_finished());
  EXPECT_TRUE(store.load_succeeded());
}

TEST_F(ModelStoreTest, AFileThatWasUnderstoodSucceeds) {
  {
    ArciumModel model;
    ModelStore store(&model, path());
    base::RunLoop loop;
    store.Load(loop.QuitClosure());
    loop.Run();
    model.AddSpace(u"Work");
    store.SaveNowForTesting();
    task_environment_.RunUntilIdle();
  }
  ArciumModel model;
  ModelStore store(&model, path());
  base::RunLoop loop;
  store.Load(loop.QuitClosure());
  loop.Run();
  ASSERT_EQ(2u, model.spaces().size());
  EXPECT_TRUE(store.load_succeeded());
}

// An unreadable file leaves an empty model. Its list of profiles is empty
// too, and Chrome's partition cleanup keeps only what that list names, so
// this load must never count as a success.
TEST_F(ModelStoreTest, ACorruptFileFinishesButDoesNotSucceed) {
  ASSERT_TRUE(base::WriteFile(path(), "{ this is not json"));
  ArciumModel model;
  ModelStore store(&model, path());
  base::RunLoop loop;
  store.Load(loop.QuitClosure());
  loop.Run();
  EXPECT_TRUE(store.load_finished());
  EXPECT_FALSE(store.load_succeeded());
}
```

If `AFileThatWasUnderstoodSucceeds`'s save does not reach disk this way, write it exactly as the existing `WhatWasSavedComesBack` test does.

Append to `arcium/test/arcium_profile_state_unittest.cc`:

```cpp
// Incognito has no file to read, so its model is complete from the start;
// and nothing on disk may be judged by it.
TEST_F(ArciumProfileStateTest, IncognitoIsLoadedButNeverASuccessfulRead) {
  Profile* otr = profile()->GetPrimaryOTRProfile(/*create_if_needed=*/true);
  ArciumProfileState* state = ArciumProfileState::GetForBrowserContext(otr);
  EXPECT_TRUE(state->model_load_finished());
  EXPECT_FALSE(state->model_load_succeeded());
}
```

- [ ] **Step 2: Run them to see them fail**

```bash
ARCIUM_JOBS=4 scripts/build dev arcium_unittests
```

Expected: the build fails on the missing `load_finished`, `load_succeeded`, `model_load_finished` and `model_load_succeeded`.

- [ ] **Step 3: Implement the load state**

In `arcium/browser/model_store.h`, after `Load`'s declaration:

```cpp
  // Whether Load() has applied the file, whatever it held. Until then the
  // model is a placeholder with one space, and nothing may judge a tab by it.
  bool load_finished() const { return load_finished_; }
  // Whether Load() found no file, or a file it understood. False for a file
  // it had to move aside: the model is then empty, not the user's, and
  // anything that deletes what the model does not name must not run.
  bool load_succeeded() const { return load_succeeded_; }
```

add to `LoadResult`:

```cpp
    // True when there was no file at all, which a first launch looks like.
    // Nothing is lost by treating that model as the user's.
    bool file_absent = false;
```

and beside `saves_suppressed_`:

```cpp
  bool load_finished_ = false;
  bool load_succeeded_ = false;
```

In `arcium/browser/model_store.cc`, `ReadFileOnBackgroundSequence`'s early return becomes:

```cpp
  if (!base::ReadFileToString(path, &contents)) {
    result.file_absent = !base::PathExists(path);
    return result;
  }
```

and in `OnLoaded` replace the `if (result.dict) { ... }` block and what follows it with:

```cpp
  bool understood = false;
  if (result.dict) {
    // A false return means the file is unusable as a whole. The model is
    // left as constructed — empty and valid — rather than partly filled.
    understood = DeserializeModel(*result.dict, model_);
  }
  loading_ = false;
  load_finished_ = true;
  load_succeeded_ = understood || result.file_absent;
  std::move(done).Run();
```

In `arcium/browser/arcium_profile_state.h`, after `store()`:

```cpp
  // Whether the model is the user's yet rather than the one-space
  // placeholder. Off the record there is no file, so the model is complete
  // from the start.
  bool model_load_finished() const {
    return !store_ || store_->load_finished();
  }
  // Whether the model was read from disk and understood, or there was no
  // file. Always false off the record, where nothing on disk belongs to
  // this model.
  bool model_load_succeeded() const {
    return store_ && store_->load_succeeded();
  }
```

- [ ] **Step 4: Run the tests, then the mutation checks**

```bash
scripts/format arcium/browser/model_store.h arcium/browser/model_store.cc arcium/browser/arcium_profile_state.h arcium/test/model_store_unittest.cc arcium/test/arcium_profile_state_unittest.cc
ARCIUM_JOBS=4 scripts/build dev arcium_unittests
/Volumes/Texternal/chromium/src/out/dev/arcium_unittests --gtest_filter='ModelStoreTest.*:ArciumProfileStateTest.*'
```

| Mutation | Test that must fail |
|---|---|
| `load_succeeded_ = true;` unconditionally | `ACorruptFileFinishesButDoesNotSucceed` |
| drop `|| result.file_absent` | `ALoadWithNoFileFinishesAndSucceeds` |
| `load_finished_ = true;` removed | `ALoadWithNoFileFinishesAndSucceeds` |
| `model_load_finished()` returns `store_ && store_->load_finished()` | `IncognitoIsLoadedButNeverASuccessfulRead` |

- [ ] **Step 5: Write the failing partition tests**

Replace `arcium/test/profile_partition_unittest.cc`:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/profile_partition.h"

#include "arcium/browser/model/arcium_profile.h"
#include "arcium/browser/model/entry_id.h"
#include "base/files/file_path.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/test/browser_task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace arcium {
namespace {

const ProfileId kWork =
    ProfileId::FromString("22222222-2222-4222-8222-222222222222");

class ProfilePartitionTest : public testing::Test {
 protected:
  content::BrowserTaskEnvironment task_environment_;
  TestingProfile profile_;
};

TEST_F(ProfilePartitionTest, TheDefaultProfileHasNoPartitionOfItsOwn) {
  EXPECT_EQ("", PartitionDomainForProfile(DefaultProfileId()));
  EXPECT_FALSE(PartitionForProfile(&profile_, DefaultProfileId()));
  EXPECT_TRUE(PartitionDirectory(profile_.GetPath(), DefaultProfileId())
                  .empty());
}

TEST_F(ProfilePartitionTest, AnotherProfileIsNamedFromItsId) {
  EXPECT_EQ("arcium-22222222-2222-4222-8222-222222222222",
            PartitionDomainForProfile(kWork));
  const std::optional<content::StoragePartitionConfig> config =
      PartitionForProfile(&profile_, kWork);
  ASSERT_TRUE(config);
  EXPECT_EQ(PartitionDomainForProfile(kWork), config->partition_domain());
  EXPECT_EQ("", config->partition_name());
  EXPECT_FALSE(config->in_memory());
}

TEST_F(ProfilePartitionTest, OnlyArciumDomainsAreArciums) {
  EXPECT_TRUE(IsArciumPartitionDomain(PartitionDomainForProfile(kWork)));
  EXPECT_FALSE(IsArciumPartitionDomain(""));
  EXPECT_FALSE(IsArciumPartitionDomain("arcium-"));
  // An extension's partition domain is its 32-letter id.
  EXPECT_FALSE(IsArciumPartitionDomain("abcdefghijklmnopabcdefghijklmnop"));
}

// Content hands the network context only this relative path, so it is the
// one fact the session-cookie hook has to recognise an Arcium profile by.
TEST_F(ProfilePartitionTest, APartitionPathIsRecognisedOnlyInItsExactShape) {
  const std::string domain = PartitionDomainForProfile(kWork);
  EXPECT_TRUE(IsArciumPartitionPath(base::FilePath(FILE_PATH_LITERAL("Storage"))
                                        .Append(FILE_PATH_LITERAL("ext"))
                                        .AppendASCII(domain)
                                        .Append(FILE_PATH_LITERAL("def"))));
  EXPECT_FALSE(IsArciumPartitionPath(
      base::FilePath(FILE_PATH_LITERAL("Storage"))
          .Append(FILE_PATH_LITERAL("ext"))
          .AppendASCII("abcdefghijklmnopabcdefghijklmnop")
          .Append(FILE_PATH_LITERAL("def"))));
  EXPECT_FALSE(IsArciumPartitionPath(base::FilePath(FILE_PATH_LITERAL("Storage"))
                                         .Append(FILE_PATH_LITERAL("ext"))
                                         .AppendASCII(domain)));
  EXPECT_FALSE(IsArciumPartitionPath(base::FilePath()));
}

// Built by hand because content's own helper is internal, so this pins it
// to what content really uses.
TEST_F(ProfilePartitionTest, ThePartitionDirectoryIsWhereContentPutsIt) {
  content::StoragePartition* partition =
      profile_.GetStoragePartition(*PartitionForProfile(&profile_, kWork));
  EXPECT_EQ(PartitionDirectory(profile_.GetPath(), kWork)
                .Append(FILE_PATH_LITERAL("def")),
            partition->GetPath());
}

TEST_F(ProfilePartitionTest, AskingWhetherAPartitionIsLoadedNeverLoadsIt) {
  EXPECT_FALSE(IsPartitionLoaded(&profile_, kWork));
  EXPECT_FALSE(IsPartitionLoaded(&profile_, kWork));
  profile_.GetStoragePartition(*PartitionForProfile(&profile_, kWork));
  EXPECT_TRUE(IsPartitionLoaded(&profile_, kWork));
  EXPECT_FALSE(IsPartitionLoaded(&profile_, DefaultProfileId()));
}

TEST_F(ProfilePartitionTest, PagesAreSortedIntoTheirStorage) {
  EXPECT_EQ(PageStorage::kProfile, StorageForUrl(GURL("https://a.test/")));
  EXPECT_EQ(PageStorage::kProfile, StorageForUrl(GURL("data:text/html,hi")));
  EXPECT_EQ(PageStorage::kShared, StorageForUrl(GURL("chrome://settings/")));
  EXPECT_EQ(PageStorage::kShared,
            StorageForUrl(GURL("chrome-extension://abcdefghijklmnop/o.html")));
  EXPECT_EQ(PageStorage::kShared,
            StorageForUrl(GURL("devtools://devtools/bundled/inspector.html")));
  EXPECT_EQ(PageStorage::kShared,
            StorageForUrl(GURL("chrome-untrusted://print/")));
  EXPECT_EQ(PageStorage::kAny, StorageForUrl(GURL("about:blank")));
  EXPECT_EQ(PageStorage::kAny, StorageForUrl(GURL()));
}

TEST_F(ProfilePartitionTest, ANewTabOnAnotherProfileIsFixedToItsPartition) {
  scoped_refptr<content::SiteInstance> site_instance =
      SiteInstanceForProfile(&profile_, kWork, GURL("https://a.test/"));
  ASSERT_TRUE(site_instance);
  EXPECT_EQ(PartitionDomainForProfile(kWork),
            profile_.GetStoragePartition(site_instance.get())
                ->GetConfig()
                .partition_domain());
}

TEST_F(ProfilePartitionTest, ChromiumsChoiceStandsForDefaultAndBrowserPages) {
  EXPECT_FALSE(SiteInstanceForProfile(&profile_, DefaultProfileId(),
                                      GURL("https://a.test/")));
  EXPECT_FALSE(
      SiteInstanceForProfile(&profile_, kWork, GURL("chrome://settings/")));
  EXPECT_FALSE(SiteInstanceForProfile(
      profile_.GetPrimaryOTRProfile(/*create_if_needed=*/true), kWork,
      GURL("https://a.test/")));
  // A blank tab navigates nowhere yet, but will: it takes the profile.
  EXPECT_TRUE(SiteInstanceForProfile(&profile_, kWork, GURL("about:blank")));
}

TEST_F(ProfilePartitionTest, TheGuardsRule) {
  const std::string work = PartitionDomainForProfile(kWork);
  const GURL web("https://a.test/");
  const GURL settings("chrome://settings/");
  // A web page belongs in its space's profile.
  EXPECT_TRUE(IsInRightStorage(web, work, kWork));
  EXPECT_FALSE(IsInRightStorage(web, "", kWork));
  EXPECT_TRUE(IsInRightStorage(web, "", DefaultProfileId()));
  EXPECT_FALSE(IsInRightStorage(web, work, DefaultProfileId()));
  // A browser page belongs in shared storage, whatever the space.
  EXPECT_TRUE(IsInRightStorage(settings, "", kWork));
  EXPECT_FALSE(IsInRightStorage(settings, work, kWork));
  // A blank page holds nothing to leak.
  EXPECT_TRUE(IsInRightStorage(GURL("about:blank"), "", kWork));
  // A guest's or an app's partition is not Arcium's to judge.
  EXPECT_TRUE(
      IsInRightStorage(web, "abcdefghijklmnopabcdefghijklmnop", kWork));
}

}  // namespace
}  // namespace arcium
```

- [ ] **Step 6: Implement the partition mapping**

Replace `arcium/browser/profile_partition.h`:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_PROFILE_PARTITION_H_
#define ARCIUM_BROWSER_PROFILE_PARTITION_H_

#include <optional>
#include <string>
#include <string_view>

#include "arcium/browser/model/entry_id.h"
#include "base/files/file_path.h"
#include "base/memory/scoped_refptr.h"
#include "content/public/browser/storage_partition_config.h"

class GURL;

namespace content {
class BrowserContext;
class SiteInstance;
class WebContents;
}  // namespace content

namespace arcium {

// The one place an Arcium profile becomes storage. The default profile is
// Chromium's default partition and keeps Chromium's own code paths; every
// other profile is the partition "arcium-<id>" of the one Chromium profile,
// one partition domain per profile so erasing a profile erases one domain.
//
// Content documents a partition domain as "[a-z]*" but checks only that it
// is not empty (storage_partition_config.cc). A profile id is a lowercase
// UUID, so the domain carries digits and hyphens; the disk path and download
// resumption take it as it is, and stripping them would let ids collide.

inline constexpr char kPartitionDomainPrefix[] = "arcium-";

// "" for the default profile, "arcium-<id>" for any other.
std::string PartitionDomainForProfile(const ProfileId& profile);

// Whether `partition_domain` is an Arcium profile's. The prefix cannot
// collide with an extension's partition, whose domain is its 32-letter id.
bool IsArciumPartitionDomain(std::string_view partition_domain);

// Whether `relative_partition_path`, a partition's path relative to the
// Chromium profile's directory, is an Arcium profile's:
// Storage/ext/arcium-<id>/def. The only fact about a partition that reaches
// the network context's configuration.
bool IsArciumPartitionPath(const base::FilePath& relative_partition_path);

// <profile_path>/Storage/ext/arcium-<id>, the directory holding `profile`'s
// partition; empty for the default profile. Built by hand: content keeps its
// helper internal, and asking a partition for its path would create it.
base::FilePath PartitionDirectory(const base::FilePath& profile_path,
                                  const ProfileId& profile);

// The partition `profile`'s tabs live in, or nullopt for the default
// profile, which uses Chromium's default partition.
std::optional<content::StoragePartitionConfig> PartitionForProfile(
    content::BrowserContext* context,
    const ProfileId& profile);

// Whether `profile`'s partition has been created this session. Never
// creates it. False for the default profile, which is not Arcium's to erase.
bool IsPartitionLoaded(content::BrowserContext* context,
                       const ProfileId& profile);

// Which storage a page belongs in.
enum class PageStorage {
  // A page with nothing of its own to keep: no URL, about:blank,
  // about:srcdoc.
  kAny,
  // Browser and extension pages: always the default partition, so an
  // extension's pages read the same storage as its background worker
  // whatever space they open in.
  kShared,
  // Every other page: the storage of the tab's profile.
  kProfile,
};
PageStorage StorageForUrl(const GURL& url);

// A SiteInstance fixed to `profile`'s partition for a new tab about to show
// `url`, or nullptr when Chromium's own choice is right: the default
// profile, a browser or extension page, or an off-the-record context, where
// profiles do not apply.
scoped_refptr<content::SiteInstance> SiteInstanceForProfile(
    content::BrowserContext* context,
    const ProfileId& profile,
    const GURL& url);

// The partition domain of the storage `contents` uses: "" for the default
// partition. Creates the partition if the tab's SiteInstance has not yet.
std::string PartitionDomainOfTab(content::WebContents* contents);

// Whether a main-frame navigation to `url`, in a tab using
// `tab_partition_domain` whose space is on `space_profile`, lands in the
// storage it belongs in. A tab on a partition that is neither the default
// nor Arcium's -- a guest, an app -- is not Arcium's to judge, and passes.
bool IsInRightStorage(const GURL& url,
                      std::string_view tab_partition_domain,
                      const ProfileId& space_profile);

}  // namespace arcium

#endif  // ARCIUM_BROWSER_PROFILE_PARTITION_H_
```

Replace `arcium/browser/profile_partition.cc`:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/profile_partition.h"

#include <vector>

#include "arcium/browser/model/arcium_profile.h"
#include "base/notreached.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/url_constants.h"
#include "url/gurl.h"

namespace arcium {

namespace {

// Content's own layout (storage_partition_impl_map.cc): an unnamed
// partition of domain D lives in <profile>/Storage/ext/D/def.
constexpr base::FilePath::CharType kStorageDirname[] =
    FILE_PATH_LITERAL("Storage");
constexpr base::FilePath::CharType kExtensionsDirname[] =
    FILE_PATH_LITERAL("ext");
constexpr base::FilePath::CharType kDefaultPartitionDirname[] =
    FILE_PATH_LITERAL("def");

// extensions::kExtensionScheme, which //arcium/browser cannot depend on.
constexpr char kExtensionScheme[] = "chrome-extension";

}  // namespace

std::string PartitionDomainForProfile(const ProfileId& profile) {
  if (!profile.is_valid() || profile == DefaultProfileId()) {
    return std::string();
  }
  return base::StrCat({kPartitionDomainPrefix, profile.value()});
}

bool IsArciumPartitionDomain(std::string_view partition_domain) {
  return partition_domain.size() > std::string_view(kPartitionDomainPrefix).size() &&
         base::StartsWith(partition_domain, kPartitionDomainPrefix);
}

bool IsArciumPartitionPath(const base::FilePath& relative_partition_path) {
  const std::vector<base::FilePath::StringType> parts =
      relative_partition_path.GetComponents();
  return parts.size() == 4 && parts[0] == kStorageDirname &&
         parts[1] == kExtensionsDirname &&
         IsArciumPartitionDomain(base::FilePath(parts[2]).AsUTF8Unsafe()) &&
         parts[3] == kDefaultPartitionDirname;
}

base::FilePath PartitionDirectory(const base::FilePath& profile_path,
                                  const ProfileId& profile) {
  const std::string domain = PartitionDomainForProfile(profile);
  if (domain.empty()) {
    return base::FilePath();
  }
  return profile_path.Append(kStorageDirname)
      .Append(kExtensionsDirname)
      .AppendASCII(domain);
}

std::optional<content::StoragePartitionConfig> PartitionForProfile(
    content::BrowserContext* context,
    const ProfileId& profile) {
  const std::string domain = PartitionDomainForProfile(profile);
  if (domain.empty()) {
    return std::nullopt;
  }
  return content::StoragePartitionConfig::Create(
      context, domain, /*partition_name=*/"", /*in_memory=*/false);
}

bool IsPartitionLoaded(content::BrowserContext* context,
                       const ProfileId& profile) {
  const std::optional<content::StoragePartitionConfig> config =
      PartitionForProfile(context, profile);
  return config &&
         context->GetStoragePartition(*config, /*can_create=*/false);
}

PageStorage StorageForUrl(const GURL& url) {
  if (url.is_empty() || url.IsAboutBlank() || url.IsAboutSrcdoc()) {
    return PageStorage::kAny;
  }
  if (url.SchemeIs(content::kChromeUIScheme) ||
      url.SchemeIs(content::kChromeUIUntrustedScheme) ||
      url.SchemeIs(content::kChromeDevToolsScheme) ||
      url.SchemeIs(kExtensionScheme)) {
    return PageStorage::kShared;
  }
  return PageStorage::kProfile;
}

scoped_refptr<content::SiteInstance> SiteInstanceForProfile(
    content::BrowserContext* context,
    const ProfileId& profile,
    const GURL& url) {
  if (context->IsOffTheRecord() ||
      StorageForUrl(url) == PageStorage::kShared) {
    return nullptr;
  }
  const std::optional<content::StoragePartitionConfig> config =
      PartitionForProfile(context, profile);
  if (!config) {
    return nullptr;
  }
  return content::SiteInstance::CreateForFixedStoragePartition(context, url,
                                                               *config);
}

std::string PartitionDomainOfTab(content::WebContents* contents) {
  return contents->GetBrowserContext()
      ->GetStoragePartition(contents->GetSiteInstance())
      ->GetConfig()
      .partition_domain();
}

bool IsInRightStorage(const GURL& url,
                      std::string_view tab_partition_domain,
                      const ProfileId& space_profile) {
  if (!tab_partition_domain.empty() &&
      !IsArciumPartitionDomain(tab_partition_domain)) {
    return true;
  }
  switch (StorageForUrl(url)) {
    case PageStorage::kAny:
      return true;
    case PageStorage::kShared:
      return tab_partition_domain.empty();
    case PageStorage::kProfile:
      return tab_partition_domain == PartitionDomainForProfile(space_profile);
  }
  NOTREACHED();
}

}  // namespace arcium
```

Check the three `content::kChrome*Scheme` names against `content/public/common/url_constants.h` and use what is there.

- [ ] **Step 7: Run the tests, then the mutation checks**

```bash
scripts/format arcium/browser/profile_partition.h arcium/browser/profile_partition.cc arcium/test/profile_partition_unittest.cc
ARCIUM_JOBS=4 scripts/build dev arcium_unittests
/Volumes/Texternal/chromium/src/out/dev/arcium_unittests --gtest_filter='ProfilePartitionTest.*'
```

| Mutation | Test that must fail |
|---|---|
| `PartitionDomainForProfile` drops the default-profile check | `TheDefaultProfileHasNoPartitionOfItsOwn` |
| `IsArciumPartitionDomain` uses `>=` for the length | `OnlyArciumDomainsAreArciums` |
| `IsArciumPartitionPath` drops `parts.size() == 4` (keep the index reads safe by also checking `>= 3`) | `APartitionPathIsRecognisedOnlyInItsExactShape` |
| `PartitionDirectory` appends `"def"` too | `ThePartitionDirectoryIsWhereContentPutsIt` |
| `IsPartitionLoaded` passes `can_create=true` | `AskingWhetherAPartitionIsLoadedNeverLoadsIt` |
| `StorageForUrl` drops the extension scheme | `PagesAreSortedIntoTheirStorage` |
| `SiteInstanceForProfile` drops the `kShared` check | `ChromiumsChoiceStandsForDefaultAndBrowserPages` |
| `IsInRightStorage` returns `true` for `kShared` | `TheGuardsRule` |
| `IsInRightStorage` drops the foreign-partition early return | `TheGuardsRule` |

- [ ] **Step 8: Write the failing session tests**

Append to `arcium/test/tab_space_unittest.cc`, adding `#include <map>`, `#include <string>` and `#include "arcium/browser/model/arcium_profile.h"`:

```cpp
// Restore creates a tab before the model file has been read, so the space
// id alone could not say which storage the tab belongs in.
TEST_F(TabSpaceTest, TheSessionRecordsTheProfileOfTheTabsSpace) {
  AddTab(browser(), GURL("https://a.example/"));
  const ProfileId work_profile = model_.AddProfile(u"Work", 1);
  const SpaceId work = model_.AddSpace(u"Work", work_profile);
  SetSpaceTag(strip()->GetTabAtIndex(0)->GetContents(), work);
  std::map<std::string, std::string> extra_data;
  PopulateTabSpaceExtraData(strip()->GetTabAtIndex(0), model_, binding_,
                            &extra_data);
  EXPECT_EQ(work_profile.value(), extra_data[kProfileIdExtraDataKey]);
  EXPECT_EQ(work_profile, ProfileIdFromExtraData(extra_data));
}

// A session written before profiles existed, or a hand-edited one.
TEST_F(TabSpaceTest, ASessionWithoutAReadableProfileMeansDefault) {
  EXPECT_EQ(DefaultProfileId(), ProfileIdFromExtraData({}));
  EXPECT_EQ(DefaultProfileId(),
            ProfileIdFromExtraData({{kProfileIdExtraDataKey, "Work"}}));
}
```

- [ ] **Step 9: Implement the session key**

In `arcium/browser/tab_space.h`, beside the two keys:

```cpp
// The profile of the tab's space when the session was written. Read by
// restore, which creates the tab -- and so fixes its storage -- before the
// model file has been read.
inline constexpr char kProfileIdExtraDataKey[] = "arcium.profile_id";
```

and after `RestoreTabSpaceData`:

```cpp
// The profile a restored tab's storage belongs to: the default profile when
// `extra_data` names none or names something that is not an id, which is
// also what a session written before profiles existed means.
ProfileId ProfileIdFromExtraData(
    const std::map<std::string, std::string>& extra_data);
```

In `arcium/browser/tab_space.cc`, add `#include "arcium/browser/model/arcium_profile.h"`. In `AppendTabSpaceCommands`, compute the space once and write the profile after the space:

```cpp
  // The resolved space, not the raw tag: a tab whose entry has since moved
  // spaces would otherwise come back where it used to be.
  const SpaceId space = SpaceOfTab(model, binding, tab->GetHandle());
  command_storage_manager->AppendRebuildCommand(
      sessions::CreateAddTabExtraDataCommand(tab_id, kSpaceIdExtraDataKey,
                                             space.value()));
  command_storage_manager->AppendRebuildCommand(
      sessions::CreateAddTabExtraDataCommand(tab_id, kTabKeyExtraDataKey,
                                             KeyOf(web_contents).value()));
  command_storage_manager->AppendRebuildCommand(
      sessions::CreateAddTabExtraDataCommand(
          tab_id, kProfileIdExtraDataKey,
          model.ProfileOfSpace(space).value()));
```

In `PopulateTabSpaceExtraData`, the same:

```cpp
  const SpaceId space = SpaceOfTab(model, binding, tab->GetHandle());
  (*extra_data)[kSpaceIdExtraDataKey] = space.value();
  (*extra_data)[kTabKeyExtraDataKey] = KeyOf(tab->GetContents()).value();
  (*extra_data)[kProfileIdExtraDataKey] = model.ProfileOfSpace(space).value();
```

And the reader:

```cpp
ProfileId ProfileIdFromExtraData(
    const std::map<std::string, std::string>& extra_data) {
  auto it = extra_data.find(kProfileIdExtraDataKey);
  if (it == extra_data.end()) {
    return DefaultProfileId();
  }
  // FromString refuses anything that is not a lowercase UUID.
  const ProfileId id = ProfileId::FromString(it->second);
  return id.is_valid() ? id : DefaultProfileId();
}
```

- [ ] **Step 10: Run the tests, the mutation checks, the whole suite, and commit**

```bash
scripts/format arcium/browser/tab_space.h arcium/browser/tab_space.cc arcium/test/tab_space_unittest.cc
ARCIUM_JOBS=4 scripts/build dev arcium_unittests
/Volumes/Texternal/chromium/src/out/dev/arcium_unittests --gtest_filter='TabSpaceTest.*:SpaceSessionTest.*'
```

| Mutation | Test that must fail |
|---|---|
| `PopulateTabSpaceExtraData` skips the profile line | `TheSessionRecordsTheProfileOfTheTabsSpace` |
| `ProfileIdFromExtraData` returns `id` without the validity check | `ASessionWithoutAReadableProfileMeansDefault` |

(`AppendTabSpaceCommands`'s new line is proven by Task 4's restart test.)

```bash
/Volumes/Texternal/chromium/src/out/dev/arcium_unittests
git add arcium/browser/model_store.h arcium/browser/model_store.cc arcium/browser/arcium_profile_state.h arcium/browser/tab_space.h arcium/browser/tab_space.cc arcium/browser/profile_partition.h arcium/browser/profile_partition.cc arcium/test/model_store_unittest.cc arcium/test/arcium_profile_state_unittest.cc arcium/test/tab_space_unittest.cc arcium/test/profile_partition_unittest.cc
git commit -F - <<'MSG'
Say which storage a profile is, and remember it per tab

A profile other than Default is a storage partition named from its id,
browser and extension pages stay in shared storage, and a restored tab
learns its profile from the session file because restore runs before the
model is read. The model now says whether it has been read, and whether
what it read can be trusted to name every profile.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
MSG
```

---

### Task 3: The browser-test harness, and a new tab in its space's profile

The first task that proves a partition really holds: an `arcium_browsertests` target with a real browser, a real test server and real cookies. With it, the hook that creates every new tab — Cmd+click, "Open link in new tab", the quick entry, bookmarks, extensions, the home-boundary divert — in its space's profile, and Arcium's own blank tab with it.

**Files:**
- Replace (skeletons from Task 0): `arcium/ui/browser/new_tab_storage.h`, `arcium/ui/browser/new_tab_storage.cc`, `arcium/test/new_tab_storage_unittest.cc`, `arcium/test/browser/profile_browsertest_base.h`, `arcium/test/browser/profile_browsertest_base.cc`, `arcium/test/browser/profile_isolation_browsertest.cc`
- Create: `patches/0181-new-tab-profile-storage.patch`
- Modify: `arcium/ui/browser/space_switcher.cc`, `arcium/test/space_switcher_unittest.cc`, `patches/README.md`
- Modify (in the Chromium checkout, recorded by the patch): `chrome/browser/ui/navigator/browser_navigator.cc`

**Interfaces:**
- Consumes (Task 2): `SiteInstanceForProfile`, `PartitionDomainForProfile`, `PartitionDomainOfTab`, `ArciumProfileState::model_load_finished()`.
- Produces, in `arcium/ui/browser/new_tab_storage.h`: `SpaceId SpaceForNewTab(const ArciumModel&, const TabBinding&, const SpaceSwitcher*, content::WebContents* source)`, `scoped_refptr<content::SiteInstance> SiteInstanceForNewTab(BrowserWindowInterface*, content::WebContents* source, const GURL&, scoped_refptr<content::SiteInstance> chromium_choice)`, `void TagNewTab(BrowserWindowInterface*, content::WebContents* source, content::WebContents* contents)`.
- Produces, in `arcium/test/browser/profile_browsertest_base.h`: `class arcium::test::ProfileBrowserTest`, used by every later browser test.

- [ ] **Step 1: Write the browser-test harness**

Replace `arcium/test/browser/profile_browsertest_base.h`:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_TEST_BROWSER_PROFILE_BROWSERTEST_BASE_H_
#define ARCIUM_TEST_BROWSER_PROFILE_BROWSERTEST_BASE_H_

#include <string>
#include <string_view>

#include "arcium/browser/model/entry_id.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "url/gurl.h"

class TabStripModel;

namespace content {
class WebContents;
}

namespace arcium {

class ArciumModel;
class ArciumProfileState;
class SpaceSwitcher;

namespace test {

// A real browser with the sidebar, a test server answering for every host,
// and a model that has finished loading. Everything about profiles that
// cannot be seen without real storage is tested through this: a partition
// that leaks shows nothing on screen, only a cookie in the wrong place.
class ProfileBrowserTest : public InProcessBrowserTest {
 public:
  ProfileBrowserTest();
  ~ProfileBrowserTest() override;

  void SetUpOnMainThread() override;

 protected:
  ArciumProfileState* state();
  ArciumModel* model();
  SpaceSwitcher* switcher();
  TabStripModel* strip();
  content::WebContents* active();

  // Adds a profile and a space on it, and switches the window there.
  SpaceId AddSpaceOnNewProfile(const std::u16string& name,
                               ProfileId* profile_out);

  // The one test page, served for every host under /arcium/: a link that
  // opens in this tab and one that opens in a new tab with no opener,
  // which is what target=_blank means.
  GURL PageUrl(std::string_view host, std::string_view query);

  // The storage `contents` is using: "" for the default partition.
  std::string PartitionOf(content::WebContents* contents);

  // Sets and reads document.cookie in the page `contents` shows. A session
  // cookie is one with no lifetime, which Chromium keeps only in memory
  // unless the profile restores its session.
  void SetCookie(content::WebContents* contents,
                 std::string_view value,
                 bool session_only = false);
  std::string ReadCookie(content::WebContents* contents);

  // The first tab of this window showing `url`, or null.
  content::WebContents* FindTab(const GURL& url);

  // For a PRE_ test: makes the next launch restore this session, and gives
  // the space tags, the profile ids and the model their chance to reach
  // disk before the browser goes.
  void RestoreSessionAtNextLaunch();
  void FlushSessionAndModel();
};

}  // namespace test
}  // namespace arcium

#endif  // ARCIUM_TEST_BROWSER_PROFILE_BROWSERTEST_BASE_H_
```

Replace `arcium/test/browser/profile_browsertest_base.cc`:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/test/browser/profile_browsertest_base.h"

#include <memory>

#include "arcium/browser/arcium_profile_state.h"
#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/profile_partition.h"
#include "arcium/ui/browser/space_switcher.h"
#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/strings/strcat.h"
#include "base/test/run_until.h"
#include "chrome/browser/prefs/session_startup_pref.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"

namespace arcium::test {

namespace {

std::unique_ptr<net::test_server::HttpResponse> ServeProfilePage(
    const net::test_server::HttpRequest& request) {
  if (!request.relative_url.starts_with("/arcium/")) {
    return nullptr;
  }
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  response->set_content_type("text/html");
  response->set_content(
      "<!doctype html><title>profile</title>"
      "<a id=\"same\" href=\"/arcium/page?same\">same</a> "
      "<a id=\"blank\" target=\"_blank\" href=\"/arcium/page?blank\">blank</a>");
  return response;
}

}  // namespace

ProfileBrowserTest::ProfileBrowserTest() = default;
ProfileBrowserTest::~ProfileBrowserTest() = default;

void ProfileBrowserTest::SetUpOnMainThread() {
  InProcessBrowserTest::SetUpOnMainThread();
  host_resolver()->AddRule("*", "127.0.0.1");
  embedded_test_server()->RegisterRequestHandler(
      base::BindRepeating(&ServeProfilePage));
  ASSERT_TRUE(embedded_test_server()->Start());
  // Every test mutates the model; a load landing afterwards would replace
  // what the test had put there.
  ASSERT_TRUE(
      base::test::RunUntil([&] { return state()->model_load_finished(); }));
}

ArciumProfileState* ProfileBrowserTest::state() {
  return ArciumProfileState::GetForBrowserContext(browser()->profile());
}

ArciumModel* ProfileBrowserTest::model() {
  return state()->model();
}

SpaceSwitcher* ProfileBrowserTest::switcher() {
  return SpaceSwitcher::FromTabStripModel(browser()->tab_strip_model());
}

TabStripModel* ProfileBrowserTest::strip() {
  return browser()->tab_strip_model();
}

content::WebContents* ProfileBrowserTest::active() {
  return strip()->GetActiveWebContents();
}

SpaceId ProfileBrowserTest::AddSpaceOnNewProfile(const std::u16string& name,
                                                 ProfileId* profile_out) {
  *profile_out = model()->AddProfile(name, /*color=*/1);
  const SpaceId space = model()->AddSpace(name, *profile_out);
  switcher()->SwitchTo(space);
  return space;
}

GURL ProfileBrowserTest::PageUrl(std::string_view host,
                                 std::string_view query) {
  return embedded_test_server()->GetURL(
      std::string(host), base::StrCat({"/arcium/page?", query}));
}

std::string ProfileBrowserTest::PartitionOf(content::WebContents* contents) {
  return PartitionDomainOfTab(contents);
}

void ProfileBrowserTest::SetCookie(content::WebContents* contents,
                                   std::string_view value,
                                   bool session_only) {
  ASSERT_TRUE(content::ExecJs(
      contents, base::StrCat({"document.cookie = 'who=", value,
                              session_only ? "'" : "; max-age=86400'"})));
}

std::string ProfileBrowserTest::ReadCookie(content::WebContents* contents) {
  return content::EvalJs(contents, "document.cookie").ExtractString();
}

content::WebContents* ProfileBrowserTest::FindTab(const GURL& url) {
  for (int i = 0; i < strip()->count(); ++i) {
    if (strip()->GetWebContentsAt(i)->GetVisibleURL() == url) {
      return strip()->GetWebContentsAt(i);
    }
  }
  return nullptr;
}

void ProfileBrowserTest::RestoreSessionAtNextLaunch() {
  SessionStartupPref::SetStartupPref(
      browser()->profile(), SessionStartupPref(SessionStartupPref::LAST));
}

void ProfileBrowserTest::FlushSessionAndModel() {
  // A tab's space and profile reach the session file only on a rebuild,
  // which the sidebar asks for through a posted, coalesced nudge; the model
  // file is written by its own store, which flushes when the profile goes.
  base::RunLoop().RunUntilIdle();
}
```

That one line is enough because the nudge posts its rebuild immediately and coalesces it (`session_rebuild_nudge.cc`: `PostTask` of `Run`, which calls `SessionService::ResetFromCurrentBrowsers`); the session service then writes its commands, and the model store flushes whatever it still owes when the profile goes at the end of the test. If a `PRE_` test later proves flaky about what reached disk, wait on the effect with `base::test::RunUntil` rather than sleeping.

- [ ] **Step 2: Prove the target builds and runs**

Replace `arcium/test/browser/profile_isolation_browsertest.cc` with the harness check alone for now:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/arcium_profile_state.h"
#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/arcium_profile.h"
#include "arcium/browser/profile_partition.h"
#include "arcium/test/browser/profile_browsertest_base.h"
#include "arcium/ui/browser/space_switcher.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/window_open_disposition.h"

namespace arcium::test {
namespace {

using ProfileIsolationTest = ProfileBrowserTest;

IN_PROC_BROWSER_TEST_F(ProfileIsolationTest, TheWindowHasASidebarAndAModel) {
  ASSERT_TRUE(switcher());
  EXPECT_TRUE(state()->model_load_finished());
  EXPECT_EQ("", PartitionOf(active()));
}

}  // namespace
}  // namespace arcium::test
```

```bash
ARCIUM_JOBS=4 scripts/build dev arcium_browsertests
/Volumes/Texternal/chromium/src/out/dev/arcium_browsertests --gtest_filter='ProfileIsolationTest.TheWindowHasASidebarAndAModel'
```

Expected: it builds and the test passes. This is the step the spec's §10 calls for; if the target cannot be made to build or run **after a real attempt** — read the mac-specific `data_deps` and `ldflags` of `browser_tests` in `chrome/test/BUILD.gn:1975` and copy what is missing — stop and report BLOCKED with the output, because the fallback (a script driving the dev build over the DevTools protocol) is a different plan.

- [ ] **Step 3: Write the failing tests for which space a new tab joins**

Replace `arcium/test/new_tab_storage_unittest.cc`:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/new_tab_storage.h"

#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/entry_id.h"
#include "arcium/browser/tab_binding.h"
#include "arcium/test/space_test_util.h"
#include "arcium/ui/browser/space_switcher.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace arcium {
namespace {

class NewTabStorageTest : public BrowserWithTestWindowTest {
 protected:
  TabStripModel* strip() { return browser()->tab_strip_model(); }

  ArciumModel model_;
  TabBinding binding_;
};

// The rule TagInsertedTabs applies at insertion, applied at creation:
// a tab's storage is fixed when it is created and can never change, so the
// space has to be known by then.
TEST_F(NewTabStorageTest, ANewTabJoinsItsSourceTabsSpaceNotTheOneOnScreen) {
  const SpaceId work = model_.AddSpace(u"Work");
  SpaceSwitcher switcher(strip(), &model_, &binding_);
  tabs::TabInterface* source = arcium::test::AddTabInSpace(
      strip(), profile(), GURL("https://a.example/"), work);
  switcher.SwitchTo(model_.default_space_id());
  EXPECT_EQ(work,
            SpaceForNewTab(model_, binding_, &switcher, source->GetContents()));
}

TEST_F(NewTabStorageTest, ANewTabFromNowhereJoinsTheSpaceOnScreen) {
  const SpaceId work = model_.AddSpace(u"Work");
  SpaceSwitcher switcher(strip(), &model_, &binding_);
  switcher.SwitchTo(work);
  EXPECT_EQ(work, SpaceForNewTab(model_, binding_, &switcher, nullptr));
}

// --arcium-no-sidebar, and every browser test that does not want one.
TEST_F(NewTabStorageTest, AWindowWithoutASidebarDecidesNothing) {
  EXPECT_FALSE(SpaceForNewTab(model_, binding_, nullptr, nullptr).is_valid());
}

}  // namespace
}  // namespace arcium
```

- [ ] **Step 4: Run them to see them fail, then implement**

```bash
ARCIUM_JOBS=4 scripts/build dev arcium_unittests
```

Expected: the build fails on the missing `SpaceForNewTab`.

Replace `arcium/ui/browser/new_tab_storage.h`:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_BROWSER_NEW_TAB_STORAGE_H_
#define ARCIUM_UI_BROWSER_NEW_TAB_STORAGE_H_

#include "arcium/browser/model/entry_id.h"
#include "base/memory/scoped_refptr.h"

class BrowserWindowInterface;
class GURL;

namespace content {
class SiteInstance;
class WebContents;
}  // namespace content

namespace arcium {

class ArciumModel;
class SpaceSwitcher;
class TabBinding;

// Which space, and so which storage, a tab Chromium is about to create
// belongs to. The same rule SpaceSwitcher::TagInsertedTabs applies when a
// tab is inserted -- the tab it was opened from, else the space on screen --
// applied at creation instead, because a tab's storage is fixed then and can
// never change afterwards. Invalid in a window without a sidebar.
SpaceId SpaceForNewTab(const ArciumModel& model,
                       const TabBinding& binding,
                       const SpaceSwitcher* switcher,
                       content::WebContents* source);

// The hook in CreateTargetContents (patch 0181): `chromium_choice` unless
// the new tab's space is on a profile with storage of its own.
scoped_refptr<content::SiteInstance> SiteInstanceForNewTab(
    BrowserWindowInterface* browser,
    content::WebContents* source,
    const GURL& url,
    scoped_refptr<content::SiteInstance> chromium_choice);

// The rest of the same hook, once the contents exists: tags it with the
// space its storage was chosen for, so the space Arcium records and the
// storage the tab uses cannot disagree. TagInsertedTabs never overwrites a
// tag, so this one stands.
void TagNewTab(BrowserWindowInterface* browser,
               content::WebContents* source,
               content::WebContents* contents);

}  // namespace arcium

#endif  // ARCIUM_UI_BROWSER_NEW_TAB_STORAGE_H_
```

Replace `arcium/ui/browser/new_tab_storage.cc`:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/new_tab_storage.h"

#include "arcium/browser/arcium_profile_state.h"
#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/profile_partition.h"
#include "arcium/browser/tab_binding.h"
#include "arcium/browser/tab_space.h"
#include "arcium/ui/browser/space_switcher.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/web_contents.h"
#include "url/gurl.h"

namespace arcium {

namespace {

// The window's sidebar state and the space the new tab joins, or an invalid
// space when this window has no sidebar or the profile no state.
SpaceId SpaceAndState(BrowserWindowInterface* browser,
                      content::WebContents* source,
                      ArciumProfileState** state_out) {
  if (!browser) {
    return SpaceId();
  }
  // IfExists, never GetForBrowserContext: the constructing variant posts an
  // archive open and a model load, and this runs for every window, sidebar
  // or not.
  ArciumProfileState* state =
      ArciumProfileState::GetForBrowserContextIfExists(browser->GetProfile());
  const SpaceSwitcher* switcher =
      SpaceSwitcher::FromTabStripModel(browser->GetTabStripModel());
  if (!state || !switcher) {
    return SpaceId();
  }
  *state_out = state;
  return SpaceForNewTab(*state->model(), *state->binding(), switcher, source);
}

}  // namespace

SpaceId SpaceForNewTab(const ArciumModel& model,
                       const TabBinding& binding,
                       const SpaceSwitcher* switcher,
                       content::WebContents* source) {
  if (!switcher) {
    return SpaceId();
  }
  tabs::TabInterface* source_tab =
      source ? tabs::TabInterface::MaybeGetFromContents(source) : nullptr;
  // A navigation from something that is not a tab -- devtools, an extension
  // page -- joins the space on screen, as a tab from another application
  // does.
  return source_tab ? SpaceOfTab(model, binding, source_tab->GetHandle())
                    : switcher->active_space();
}

scoped_refptr<content::SiteInstance> SiteInstanceForNewTab(
    BrowserWindowInterface* browser,
    content::WebContents* source,
    const GURL& url,
    scoped_refptr<content::SiteInstance> chromium_choice) {
  ArciumProfileState* state = nullptr;
  const SpaceId space = SpaceAndState(browser, source, &state);
  if (!space.is_valid()) {
    return chromium_choice;
  }
  scoped_refptr<content::SiteInstance> fixed = SiteInstanceForProfile(
      browser->GetProfile(), state->model()->ProfileOfSpace(space), url);
  return fixed ? fixed : chromium_choice;
}

void TagNewTab(BrowserWindowInterface* browser,
               content::WebContents* source,
               content::WebContents* contents) {
  ArciumProfileState* state = nullptr;
  const SpaceId space = SpaceAndState(browser, source, &state);
  if (space.is_valid()) {
    SetSpaceTag(contents, space);
  }
}

}  // namespace arcium
```

```bash
scripts/format arcium/ui/browser/new_tab_storage.h arcium/ui/browser/new_tab_storage.cc arcium/test/new_tab_storage_unittest.cc
ARCIUM_JOBS=4 scripts/build dev arcium_unittests
/Volumes/Texternal/chromium/src/out/dev/arcium_unittests --gtest_filter='NewTabStorageTest.*'
```

| Mutation | Test that must fail |
|---|---|
| `SpaceForNewTab` returns `switcher->active_space()` always | `ANewTabJoinsItsSourceTabsSpaceNotTheOneOnScreen` |
| `SpaceForNewTab` drops the null-switcher check | `AWindowWithoutASidebarDecidesNothing` |

- [ ] **Step 5: The hook**

In `/Volumes/Texternal/chromium/src/chrome/browser/ui/navigator/browser_navigator.cc`, add `#include "arcium/ui/browser/new_tab_storage.h"` as the first quoted include, and in `CreateTargetContents`:

```cpp
  scoped_refptr<content::SiteInstance> initial_site_instance_for_new_contents =
      params.opener ? params.opener->GetSiteInstance()
                    // Arcium: a new tab is created in its space's profile
                    // (arcium/ui/browser/new_tab_storage.h). With an opener
                    // the partition already comes from the opener's tab.
                    : arcium::SiteInstanceForNewTab(
                          params.browser, params.source_contents, url,
                          tab_util::GetSiteInstanceForNewTab(
                              params.browser->GetProfile(), url));
```

and replace the final `return WebContents::Create(create_params);` with:

```cpp
  std::unique_ptr<content::WebContents> contents =
      WebContents::Create(create_params);
  // Arcium: tagged with the space its storage was chosen for, before the
  // tab strip can tag it with anything else.
  arcium::TagNewTab(params.browser, params.source_contents, contents.get());
  return contents;
```

Then:

```bash
git -C /Volumes/Texternal/chromium/src diff -- chrome/browser/ui/navigator/browser_navigator.cc > /private/tmp/0181.diff
```

Write `patches/0181-new-tab-profile-storage.patch` as this header, a blank line, then the diff unchanged:

```
Seam: CreateTargetContents in chrome/browser/ui/navigator/browser_navigator.cc,
      where every new tab that is not a content-created popup is built.
Why: a tab's storage partition is fixed when its WebContents is created and
     can never change, so the space it belongs to has to be known here
     rather than at insertion. Cmd+click, "Open link in new tab", the quick
     entry, bookmarks, extension-created tabs and the home-boundary divert
     all pass through this one function. The tag is set in the same call so
     the space Arcium records and the storage the tab uses cannot disagree.
Delegates to: arcium::SiteInstanceForNewTab, arcium::TagNewTab
```

```bash
scripts/sync
scripts/sync
```

Expected: both succeed, the second reporting 0181 already applied.

- [ ] **Step 6: Arcium's own blank tab**

In `arcium/ui/browser/space_switcher.cc`, `InsertBlankTab` creates its contents with no SiteInstance, which means the default partition. Add `#include "arcium/browser/profile_partition.h"` and `#include "url/url_constants.h"`, and replace the `WebContents::Create` call:

```cpp
  content::BrowserContext* context = tab_strip_model_->profile();
  // On the space's own profile, like every other tab of the space:
  // about:blank because it navigates nowhere until the quick entry sends it
  // somewhere, and that navigation then stays in this storage.
  std::unique_ptr<content::WebContents> contents =
      content::WebContents::Create(content::WebContents::CreateParams(
          context,
          SiteInstanceForProfile(context,
                                 model_->ProfileOfSpace(active_space_),
                                 GURL(url::kAboutBlankURL))));
```

and add to `arcium/test/space_switcher_unittest.cc`, with `#include "arcium/browser/model/arcium_profile.h"` and `#include "arcium/browser/profile_partition.h"`:

```cpp
TEST_F(SpaceSwitcherTest, TheBlankTabOfASpaceOnAProfileUsesThatProfile) {
  const ProfileId work_profile = model_.AddProfile(u"Work", 1);
  const SpaceId work = model_.AddSpace(u"Work", work_profile);
  auto switcher = MakeSwitcher();
  switcher->SwitchTo(work);
  EXPECT_EQ(PartitionDomainForProfile(work_profile),
            PartitionDomainOfTab(strip()->GetActiveWebContents()));
}
```

If this fixture cannot create a storage partition at all (the failure would be inside `GetStoragePartition`, not in Arcium's code), move this assertion into Step 7's browser test as `TheBlankTabOfAnEmptySpaceIsInItsProfile` and say so in the report.

- [ ] **Step 7: The isolation tests**

Append to `arcium/test/browser/profile_isolation_browsertest.cc`:

```cpp
// The whole point of the stage: the same site, two spaces, two logins.
IN_PROC_BROWSER_TEST_F(ProfileIsolationTest, ASpaceOnItsOwnProfileLogsInSeparately) {
  ProfileId work_profile;
  const SpaceId work = AddSpaceOnNewProfile(u"Work", &work_profile);
  const GURL url = PageUrl("a.test", "one");

  ui_test_utils::NavigateToURLWithDisposition(
      browser(), url, WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  content::WebContents* work_tab = active();
  EXPECT_EQ(PartitionDomainForProfile(work_profile), PartitionOf(work_tab));
  EXPECT_EQ(work,
            switcher()->SpaceOfTabAt(strip()->GetIndexOfWebContents(work_tab)));
  SetCookie(work_tab, "work");

  switcher()->SwitchTo(model()->default_space_id());
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), url, WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  content::WebContents* default_tab = active();
  EXPECT_EQ("", PartitionOf(default_tab));
  EXPECT_EQ("", ReadCookie(default_tab));
  SetCookie(default_tab, "default");

  EXPECT_EQ("who=work", ReadCookie(work_tab));
  EXPECT_EQ("who=default", ReadCookie(default_tab));
}

// A Cmd+click: a real click with a real gesture, through the same seam.
IN_PROC_BROWSER_TEST_F(ProfileIsolationTest, ALinkOpenedInANewTabKeepsTheProfile) {
  ProfileId work_profile;
  AddSpaceOnNewProfile(u"Work", &work_profile);
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), PageUrl("a.test", "one"),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  content::WebContents* work_tab = active();
  SetCookie(work_tab, "work");

  ui_test_utils::TabAddedWaiter waiter(browser());
  const int x = content::EvalJs(work_tab,
                                "Math.round(document.getElementById('same')."
                                "getBoundingClientRect().left + 4)")
                    .ExtractInt();
  const int y = content::EvalJs(work_tab,
                                "Math.round(document.getElementById('same')."
                                "getBoundingClientRect().top + 4)")
                    .ExtractInt();
  content::SimulateMouseClickAt(work_tab, blink::WebInputEvent::kMetaKey,
                                blink::WebMouseEvent::Button::kLeft,
                                gfx::Point(x, y));
  waiter.Wait();

  content::WebContents* opened = FindTab(PageUrl("a.test", "same"));
  ASSERT_TRUE(opened);
  ASSERT_TRUE(content::WaitForLoadStop(opened));
  EXPECT_EQ(PartitionDomainForProfile(work_profile), PartitionOf(opened));
  EXPECT_EQ("who=work", ReadCookie(opened));
}

// What an extension's tabs.create, or a menu command, does: a new tab from
// a tab of a space that is not the one on screen, and with no gesture, so
// nothing but the hook's own rule can place it.
IN_PROC_BROWSER_TEST_F(ProfileIsolationTest, ANewTabFollowsItsSourceNotTheScreen) {
  ProfileId work_profile;
  const SpaceId work = AddSpaceOnNewProfile(u"Work", &work_profile);
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), PageUrl("a.test", "one"),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  content::WebContents* work_tab = active();
  switcher()->SwitchTo(model()->default_space_id());

  NavigateParams params(browser(), PageUrl("a.test", "two"),
                        ui::PAGE_TRANSITION_LINK);
  params.disposition = WindowOpenDisposition::NEW_BACKGROUND_TAB;
  params.source_contents = work_tab;
  params.user_gesture = false;
  Navigate(&params);

  content::WebContents* opened = FindTab(PageUrl("a.test", "two"));
  ASSERT_TRUE(opened);
  ASSERT_TRUE(content::WaitForLoadStop(opened));
  EXPECT_EQ(PartitionDomainForProfile(work_profile), PartitionOf(opened));
  EXPECT_EQ(work,
            switcher()->SpaceOfTabAt(strip()->GetIndexOfWebContents(opened)));
}

// Decision 7: an extension's pages must read the same storage wherever
// they are opened, or its options page would forget its settings.
IN_PROC_BROWSER_TEST_F(ProfileIsolationTest, ABrowserPageStaysInSharedStorage) {
  ProfileId work_profile;
  AddSpaceOnNewProfile(u"Work", &work_profile);
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL("chrome://version/"),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  EXPECT_EQ("", PartitionOf(active()));
}
```

Add the includes these need: `chrome/browser/ui/navigator/browser_navigator.h`, `chrome/browser/ui/navigator/browser_navigator_params.h`, `third_party/blink/public/common/input/web_input_event.h`, `third_party/blink/public/common/input/web_mouse_event.h`, `ui/gfx/geometry/point.h`, `ui/base/page_transition_types.h`. Match `Navigate`'s declaration in `browser_navigator.h`, which may take a second, defaulted argument.

- [ ] **Step 8: Run, mutate, record and commit**

```bash
ARCIUM_JOBS=4 scripts/build dev arcium_browsertests
/Volumes/Texternal/chromium/src/out/dev/arcium_browsertests --gtest_filter='ProfileIsolationTest.*'
ARCIUM_JOBS=4 scripts/build dev arcium_unittests
/Volumes/Texternal/chromium/src/out/dev/arcium_unittests
```

| Mutation | Test that must fail |
|---|---|
| `SiteInstanceForNewTab` returns `chromium_choice` always | `ASpaceOnItsOwnProfileLogsInSeparately` |
| `TagNewTab` does nothing | `ANewTabFollowsItsSourceNotTheScreen` |
| `InsertBlankTab` passes no SiteInstance | `TheBlankTabOfASpaceOnAProfileUsesThatProfile` |

In `patches/README.md`, add to the Stage 3b table, after the 0180 row:

```markdown
| `0181-new-tab-profile-storage.patch` | `CreateTargetContents` in `chrome/browser/ui/navigator/browser_navigator.cc` | `arcium::SiteInstanceForNewTab`, `arcium::TagNewTab` |
```

```bash
git add arcium/ui/browser/new_tab_storage.h arcium/ui/browser/new_tab_storage.cc arcium/ui/browser/space_switcher.cc arcium/test/new_tab_storage_unittest.cc arcium/test/space_switcher_unittest.cc arcium/test/browser/profile_browsertest_base.h arcium/test/browser/profile_browsertest_base.cc arcium/test/browser/profile_isolation_browsertest.cc patches/0181-new-tab-profile-storage.patch patches/README.md
git commit -F - <<'MSG'
A new tab opens in the profile of the space it belongs to

A tab's storage is fixed when it is created, so the space it joins has to
be decided there too, from the tab it was opened from or the space on
screen; the tag is set in the same breath so the two cannot disagree. The
browser tests are how a partition that leaks is seen at all: on screen it
looks like nothing.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
MSG
```

---

### Task 4: A restored tab comes back in its profile

Session restore and Cmd+Shift+T create their tabs before the model file has been read, so the profile has to come from the session file itself, where Task 2 put it. This is what A3.1 rests on: quit, relaunch, still logged in as both accounts.

**Files:**
- Modify: `arcium/browser/tab_space.h`, `arcium/browser/tab_space.cc`
- Replace (skeleton from Task 0): `arcium/test/browser/profile_restore_browsertest.cc`
- Create: `patches/0182-restored-tab-profile-storage.patch`
- Modify: `patches/README.md`
- Modify (in the Chromium checkout, recorded by the patch): `chrome/browser/ui/browser_tabrestore.cc`

**Interfaces:**
- Consumes (Task 2): `ProfileIdFromExtraData`, `SiteInstanceForProfile`, `IsPartitionLoaded`, `PartitionDomainForProfile`; (Task 3) `arcium::test::ProfileBrowserTest`.
- Produces, in `arcium/browser/tab_space.h`: `scoped_refptr<content::SiteInstance> SiteInstanceForRestoredTab(content::BrowserContext*, const GURL& url, const std::map<std::string, std::string>& extra_data, scoped_refptr<content::SiteInstance> chromium_choice)`.

- [ ] **Step 1: Write the failing restart tests**

Replace `arcium/test/browser/profile_restore_browsertest.cc`:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/arcium_profile.h"
#include "arcium/browser/profile_partition.h"
#include "arcium/test/browser/profile_browsertest_base.h"
#include "arcium/ui/browser/space_switcher.h"
#include "base/test/run_until.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/window_open_disposition.h"

namespace arcium::test {
namespace {

using ProfileRestoreTest = ProfileBrowserTest;

// Two spaces, two profiles, one site, two logins -- then a restart.
IN_PROC_BROWSER_TEST_F(ProfileRestoreTest, PRE_ARestoredTabKeepsItsProfile) {
  RestoreSessionAtNextLaunch();
  ProfileId work_profile;
  AddSpaceOnNewProfile(u"Work", &work_profile);
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), PageUrl("a.test", "one"),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  SetCookie(active(), "work");

  switcher()->SwitchTo(model()->default_space_id());
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), PageUrl("a.test", "one"),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  SetCookie(active(), "default");
  FlushSessionAndModel();
}

IN_PROC_BROWSER_TEST_F(ProfileRestoreTest, ARestoredTabKeepsItsProfile) {
  ASSERT_EQ(2u, model()->profiles().size());
  ASSERT_EQ(2u, model()->spaces().size());
  const SpaceId work = model()->spaces()[1].id;
  const ProfileId work_profile = model()->ProfileOfSpace(work);
  ASSERT_NE(DefaultProfileId(), work_profile);

  // Found by its space, not by its storage: asking a tab for its partition
  // would create it, which the next test is about.
  content::WebContents* work_tab = nullptr;
  for (int i = 0; i < strip()->count(); ++i) {
    if (switcher()->SpaceOfTabAt(i) == work) {
      work_tab = strip()->GetWebContentsAt(i);
    }
  }
  ASSERT_TRUE(work_tab);
  EXPECT_EQ(PartitionDomainForProfile(work_profile), PartitionOf(work_tab));

  // R3.9 leaves a restored tab unloaded; clicking it is what loads it.
  content::TestNavigationObserver observer(work_tab);
  strip()->ActivateTabAt(strip()->GetIndexOfWebContents(work_tab));
  observer.Wait();
  EXPECT_EQ("who=work", ReadCookie(work_tab));
}

IN_PROC_BROWSER_TEST_F(ProfileRestoreTest,
                       PRE_ARestoredTabBuildsNoStorageUntilItLoads) {
  RestoreSessionAtNextLaunch();
  ProfileId work_profile;
  AddSpaceOnNewProfile(u"Work", &work_profile);
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), PageUrl("a.test", "one"),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  SetCookie(active(), "work");
  // Leave the window on Default, so the restored Work tab is not the one
  // the restart loads.
  switcher()->SwitchTo(model()->default_space_id());
  FlushSessionAndModel();
}

// What the performance claim rests on: a profile whose tabs are all
// unloaded costs nothing at startup.
IN_PROC_BROWSER_TEST_F(ProfileRestoreTest,
                       ARestoredTabBuildsNoStorageUntilItLoads) {
  ASSERT_EQ(2u, model()->spaces().size());
  const SpaceId work = model()->spaces()[1].id;
  const ProfileId work_profile = model()->ProfileOfSpace(work);
  EXPECT_FALSE(IsPartitionLoaded(browser()->profile(), work_profile));

  content::WebContents* work_tab = nullptr;
  for (int i = 0; i < strip()->count(); ++i) {
    if (switcher()->SpaceOfTabAt(i) == work) {
      work_tab = strip()->GetWebContentsAt(i);
    }
  }
  ASSERT_TRUE(work_tab);
  content::TestNavigationObserver observer(work_tab);
  strip()->ActivateTabAt(strip()->GetIndexOfWebContents(work_tab));
  observer.Wait();
  EXPECT_TRUE(IsPartitionLoaded(browser()->profile(), work_profile));
}

// Cmd+Shift+T reaches the same tab-building function by another road.
IN_PROC_BROWSER_TEST_F(ProfileRestoreTest, AReopenedClosedTabComesBackInItsProfile) {
  ProfileId work_profile;
  AddSpaceOnNewProfile(u"Work", &work_profile);
  const GURL url = PageUrl("a.test", "one");
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), url, WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  SetCookie(active(), "work");
  strip()->CloseWebContentsAt(strip()->active_index(),
                              TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);

  chrome::RestoreTab(browser());
  content::WebContents* reopened = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&] {
    reopened = FindTab(url);
    return reopened != nullptr;
  }));
  ASSERT_TRUE(content::WaitForLoadStop(reopened));
  EXPECT_EQ(PartitionDomainForProfile(work_profile), PartitionOf(reopened));
  EXPECT_EQ("who=work", ReadCookie(reopened));
}

}  // namespace
}  // namespace arcium::test
```

If `ARestoredTabBuildsNoStorageUntilItLoads`'s first expectation fails, **do not change any code to make it pass**: it means Chromium builds a partition when the WebContents is created rather than when it loads. Report it; the spec's performance claim is what changes.

- [ ] **Step 2: Run them to see them fail**

```bash
ARCIUM_JOBS=4 scripts/build dev arcium_browsertests
/Volumes/Texternal/chromium/src/out/dev/arcium_browsertests --gtest_filter='ProfileRestoreTest.*'
```

Expected: the build fails on the missing `SiteInstanceForRestoredTab`; once Step 3's declaration exists but the hook does not, `ARestoredTabKeepsItsProfile` fails on the partition, reading `""`.

- [ ] **Step 3: Implement**

In `arcium/browser/tab_space.h`, add `#include "base/memory/scoped_refptr.h"`, forward-declare `content::BrowserContext` and `content::SiteInstance` and `class GURL;`, and declare after `RestoreTabSpaceData`:

```cpp
// The storage a restored tab belongs in, from the profile its session
// recorded: `chromium_choice` unless that profile has storage of its own.
// Restore runs before the model file has been read, so the session is the
// only thing that can answer this.
scoped_refptr<content::SiteInstance> SiteInstanceForRestoredTab(
    content::BrowserContext* context,
    const GURL& url,
    const std::map<std::string, std::string>& extra_data,
    scoped_refptr<content::SiteInstance> chromium_choice);
```

In `arcium/browser/tab_space.cc`, add `#include "arcium/browser/profile_partition.h"`, `#include "content/public/browser/site_instance.h"` and `#include "url/gurl.h"`, and:

```cpp
scoped_refptr<content::SiteInstance> SiteInstanceForRestoredTab(
    content::BrowserContext* context,
    const GURL& url,
    const std::map<std::string, std::string>& extra_data,
    scoped_refptr<content::SiteInstance> chromium_choice) {
  scoped_refptr<content::SiteInstance> fixed = SiteInstanceForProfile(
      context, ProfileIdFromExtraData(extra_data), url);
  return fixed ? fixed : chromium_choice;
}
```

In `/Volumes/Texternal/chromium/src/chrome/browser/ui/browser_tabrestore.cc`, add `#include "arcium/browser/tab_space.h"` beside the existing `arcium/browser/session_tab_entry.h` include, and replace `CreateRestoredTab`'s `create_params` construction:

```cpp
  WebContents::CreateParams create_params(
      browser->GetProfile(),
      // Arcium: the profile its session recorded, if that profile has
      // storage of its own (arcium/browser/tab_space.h). The session's
      // answer, not the model's: the model file has not been read yet.
      arcium::SiteInstanceForRestoredTab(
          browser->GetProfile(), restore_url, extra_data,
          tab_util::GetSiteInstanceForNewTab(browser->GetProfile(),
                                             restore_url)));
```

The session-storage map above it is left alone: Chromium saves and recreates only the default partition's session storage, so a non-default tab starts with an empty sessionStorage. That is the accepted cost in the spec (§5.2); putting the default namespace under an Arcium key would trip a DCHECK.

```bash
git -C /Volumes/Texternal/chromium/src diff -- chrome/browser/ui/browser_tabrestore.cc > /private/tmp/0182.diff
```

Write `patches/0182-restored-tab-profile-storage.patch` as this header, a blank line, then the diff unchanged. Note the file already carries patch 0120, so the diff will contain both hunks — keep only the new hunk's context by generating the diff after 0120 is applied, which `scripts/sync` guarantees, and check the resulting patch applies on a clean tree with `scripts/sync` twice.

```
Seam: CreateRestoredTab in chrome/browser/ui/browser_tabrestore.cc, where
      session restore and Cmd+Shift+T build a restored tab's WebContents.
Why: a tab's storage is fixed when it is created, and restore creates its
     tabs before the Arcium model file has been read, so the profile has to
     travel in the session file beside the space id. Without this every
     restored tab of a profile comes back logged out, which is A3.1.
Delegates to: arcium::SiteInstanceForRestoredTab
```

```bash
scripts/sync
scripts/sync
ARCIUM_JOBS=4 scripts/build dev arcium_browsertests
/Volumes/Texternal/chromium/src/out/dev/arcium_browsertests --gtest_filter='ProfileRestoreTest.*'
```

- [ ] **Step 4: Mutation checks, the suites, the inventory and the commit**

| Mutation | Test that must fail |
|---|---|
| `SiteInstanceForRestoredTab` returns `chromium_choice` always | `ARestoredTabKeepsItsProfile` |
| `AppendTabSpaceCommands` (Task 2) drops its profile line | `ARestoredTabKeepsItsProfile` |
| `PopulateTabSpaceExtraData` (Task 2) drops its profile line | `AReopenedClosedTabComesBackInItsProfile` |

```bash
/Volumes/Texternal/chromium/src/out/dev/arcium_unittests
```

In `patches/README.md`, add after the 0181 row:

```markdown
| `0182-restored-tab-profile-storage.patch` | `CreateRestoredTab` in `chrome/browser/ui/browser_tabrestore.cc` | `arcium::SiteInstanceForRestoredTab` |
```

```bash
git add arcium/browser/tab_space.h arcium/browser/tab_space.cc arcium/test/browser/profile_restore_browsertest.cc patches/0182-restored-tab-profile-storage.patch patches/README.md
git commit -F - <<'MSG'
A restored tab comes back logged in as the profile it left as

Restore builds its tabs before the model file has been read, so the
profile travels in the session file beside the space and decides the tab's
storage there. Reopening a closed tab takes the same road. A restored tab
that has not loaded still builds no storage at all.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
MSG
```

---

### Task 5: A tab Chromium throws away, and prerendering

Two paths that would take a tab out of its profile without anyone asking. Chromium recreates a discarded tab's contents with no SiteInstance, which means the default partition; and a prerender builds its page in the default partition and then swaps the tab into it.

**Files:**
- Modify: `arcium/browser/profile_partition.h`, `arcium/browser/profile_partition.cc`, `arcium/test/profile_partition_unittest.cc`
- Replace (skeleton from Task 0): `arcium/test/browser/profile_lifecycle_browsertest.cc`
- Create: `patches/0184-discard-keeps-storage.patch`, `patches/0185-prerender-profile-storage.patch`
- Modify: `patches/README.md`
- Modify (in the Chromium checkout, recorded by the patches): `chrome/browser/resource_coordinator/tab_lifecycle_unit.cc`, `chrome/browser/ui/browser.cc`

**Interfaces:**
- Produces, in `arcium/browser/profile_partition.h`: `scoped_refptr<content::SiteInstance> SiteInstanceForReplacement(content::WebContents* old_contents)`, `content::PreloadingEligibility PrerenderEligibilityForTab(content::WebContents& contents, content::PreloadingEligibility chromium_answer)`.

- [ ] **Step 1: Write the failing tests**

Append to `arcium/test/profile_partition_unittest.cc`, adding `#include "content/public/browser/preloading.h"`, `#include "content/public/test/test_renderer_host.h"` and `#include "content/public/test/web_contents_tester.h"`, and a `content::RenderViewHostTestEnabler rvh_enabler_;` member to the fixture:

```cpp
TEST_F(ProfilePartitionTest, AReplacementKeepsTheTabsOwnStorage) {
  std::unique_ptr<content::WebContents> contents =
      content::WebContentsTester::CreateTestWebContents(
          &profile_,
          SiteInstanceForProfile(&profile_, kWork, GURL("https://a.test/")));
  scoped_refptr<content::SiteInstance> replacement =
      SiteInstanceForReplacement(contents.get());
  ASSERT_TRUE(replacement);
  EXPECT_EQ(PartitionDomainForProfile(kWork),
            profile_.GetStoragePartition(replacement.get())
                ->GetConfig()
                .partition_domain());
}

TEST_F(ProfilePartitionTest, AReplacementOfADefaultTabKeepsChromiumsChoice) {
  std::unique_ptr<content::WebContents> contents =
      content::WebContentsTester::CreateTestWebContents(&profile_, nullptr);
  EXPECT_FALSE(SiteInstanceForReplacement(contents.get()));
}

// A prerender builds its page in the default partition and activation swaps
// the tab into it, which would take the tab out of its profile.
TEST_F(ProfilePartitionTest, ATabInAProfileIsNotPrerenderedInto) {
  std::unique_ptr<content::WebContents> in_profile =
      content::WebContentsTester::CreateTestWebContents(
          &profile_,
          SiteInstanceForProfile(&profile_, kWork, GURL("https://a.test/")));
  EXPECT_EQ(content::PreloadingEligibility::kNonDefaultStoragePartition,
            PrerenderEligibilityForTab(
                *in_profile, content::PreloadingEligibility::kEligible));

  std::unique_ptr<content::WebContents> in_default =
      content::WebContentsTester::CreateTestWebContents(&profile_, nullptr);
  EXPECT_EQ(content::PreloadingEligibility::kEligible,
            PrerenderEligibilityForTab(
                *in_default, content::PreloadingEligibility::kEligible));
  // Chromium's own answer still stands where it says no.
  EXPECT_EQ(content::PreloadingEligibility::kPreloadingDisabled,
            PrerenderEligibilityForTab(
                *in_default, content::PreloadingEligibility::kPreloadingDisabled));
}
```

Replace `arcium/test/browser/profile_lifecycle_browsertest.cc`:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/profile_partition.h"
#include "arcium/test/browser/profile_browsertest_base.h"
#include "arcium/ui/browser/space_switcher.h"
#include "chrome/browser/resource_coordinator/tab_lifecycle_unit_external.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/performance_manager/public/mojom/lifecycle.mojom.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/window_open_disposition.h"

namespace arcium::test {
namespace {

using ProfileLifecycleTest = ProfileBrowserTest;

// Chromium throws a background tab away under memory pressure and builds a
// new contents for it; without the hook that contents is in the default
// partition, and the tab comes back logged out.
IN_PROC_BROWSER_TEST_F(ProfileLifecycleTest, ADiscardedTabComesBackInItsProfile) {
  ProfileId work_profile;
  AddSpaceOnNewProfile(u"Work", &work_profile);
  const GURL url = PageUrl("a.test", "one");
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), url, WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  SetCookie(active(), "work");
  const int index = strip()->active_index();

  // A tab on screen is never discarded; leave it for another one.
  switcher()->SwitchTo(model()->default_space_id());

  resource_coordinator::TabLifecycleUnitExternal* unit =
      resource_coordinator::TabLifecycleUnitExternal::FromWebContents(
          strip()->GetWebContentsAt(index));
  ASSERT_TRUE(unit);
  ASSERT_TRUE(unit->DiscardTab(
      mojom::LifecycleUnitDiscardReason::PROACTIVE));

  content::WebContents* replacement = strip()->GetWebContentsAt(index);
  EXPECT_EQ(PartitionDomainForProfile(work_profile), PartitionOf(replacement));

  content::TestNavigationObserver observer(replacement);
  strip()->ActivateTabAt(index);
  observer.Wait();
  EXPECT_EQ("who=work", ReadCookie(replacement));
}

}  // namespace
}  // namespace arcium::test
```

Check `DiscardTab`'s enum: it is `mojom::LifecycleUnitDiscardReason` from `chrome/browser/resource_coordinator/lifecycle_unit_state.mojom.h` or the performance-manager mojom the header includes — use whatever `tab_lifecycle_unit_external.h` names, and a reason that is allowed for a background tab. If the discard is refused (`DiscardTab` returns false) because of a policy in this fixture, call `unit->SetAutoDiscardable(true)` first, and if it is still refused, drive `TabLifecycleUnitSource` the way `chrome/browser/resource_coordinator/tab_lifecycle_unit_browsertest.cc` does and follow that file's setup.

- [ ] **Step 2: Run them to see them fail, then implement**

In `arcium/browser/profile_partition.h`, add `#include "content/public/browser/preloading.h"` and:

```cpp
// The storage a tab Chromium is recreating must keep: a SiteInstance fixed
// to the partition `old_contents` uses, or nullptr when that is the default
// partition and Chromium's own choice -- none at all -- is right.
scoped_refptr<content::SiteInstance> SiteInstanceForReplacement(
    content::WebContents* old_contents);

// Whether a page may be prerendered for `contents`. A prerender builds its
// own frame tree in the default partition and activation swaps the tab into
// it, so a tab in a profile refuses: R3.9 -- nothing loads unless it is
// asked for -- argues the same way.
content::PreloadingEligibility PrerenderEligibilityForTab(
    content::WebContents& contents,
    content::PreloadingEligibility chromium_answer);
```

In `arcium/browser/profile_partition.cc`:

```cpp
scoped_refptr<content::SiteInstance> SiteInstanceForReplacement(
    content::WebContents* old_contents) {
  content::BrowserContext* context = old_contents->GetBrowserContext();
  const content::StoragePartitionConfig config =
      context->GetStoragePartition(old_contents->GetSiteInstance())
          ->GetConfig();
  if (!IsArciumPartitionDomain(config.partition_domain())) {
    return nullptr;
  }
  return content::SiteInstance::CreateForFixedStoragePartition(
      context, old_contents->GetLastCommittedURL(), config);
}

content::PreloadingEligibility PrerenderEligibilityForTab(
    content::WebContents& contents,
    content::PreloadingEligibility chromium_answer) {
  return IsArciumPartitionDomain(PartitionDomainOfTab(&contents))
             ? content::PreloadingEligibility::kNonDefaultStoragePartition
             : chromium_answer;
}
```

```bash
scripts/format arcium/browser/profile_partition.h arcium/browser/profile_partition.cc arcium/test/profile_partition_unittest.cc
ARCIUM_JOBS=4 scripts/build dev arcium_unittests
/Volumes/Texternal/chromium/src/out/dev/arcium_unittests --gtest_filter='ProfilePartitionTest.*'
```

| Mutation | Test that must fail |
|---|---|
| `SiteInstanceForReplacement` always returns a fixed SiteInstance | `AReplacementOfADefaultTabKeepsChromiumsChoice` (it CHECKs on a default config) |
| `SiteInstanceForReplacement` uses `GURL()` instead of the last committed URL | nothing — that is fine, and the comment should not claim otherwise |
| `PrerenderEligibilityForTab` returns `chromium_answer` always | `ATabInAProfileIsNotPrerenderedInto` |

- [ ] **Step 3: The two hooks**

In `/Volumes/Texternal/chromium/src/chrome/browser/resource_coordinator/tab_lifecycle_unit.cc`, add `#include "arcium/browser/profile_partition.h"` as the first quoted include, and in `FinishDiscard`, after the last `create_params` assignment and before `WebContents::Create`:

```cpp
  // Arcium: the replacement keeps the tab's own storage; without it a tab
  // in a profile comes back logged out, and its history entries would still
  // point at the storage it left (arcium/browser/profile_partition.h).
  create_params.site_instance = arcium::SiteInstanceForReplacement(old_contents);
```

In `/Volumes/Texternal/chromium/src/chrome/browser/ui/browser.cc`, add `#include "arcium/browser/profile_partition.h"` as the first quoted include, and make `IsPrerender2Supported` end:

```cpp
  // Arcium: a tab in a profile of its own cannot be prerendered into,
  // because the prerender is built in the default partition
  // (arcium/browser/profile_partition.h).
  return arcium::PrerenderEligibilityForTab(
      web_contents, prefetch::IsSomePreloadingEnabled(*profile->GetPrefs()));
```

Generate both patches and write their headers:

```bash
git -C /Volumes/Texternal/chromium/src diff -- chrome/browser/resource_coordinator/tab_lifecycle_unit.cc > /private/tmp/0184.diff
git -C /Volumes/Texternal/chromium/src diff -- chrome/browser/ui/browser.cc > /private/tmp/0185.diff
```

```
Seam: TabLifecycleUnitSource::TabLifecycleUnit::FinishDiscard in
      chrome/browser/resource_coordinator/tab_lifecycle_unit.cc, where the
      discarded tab's replacement WebContents is created.
Why: the replacement is created with no SiteInstance, which means the
     default partition, and its copied history entries still name the old
     partition's SiteInstances. A tab in a profile would come back logged
     out after Chromium had thrown it away to save memory.
Delegates to: arcium::SiteInstanceForReplacement
```

```
Seam: Browser::IsPrerender2Supported in chrome/browser/ui/browser.cc.
Why: a prerender builds its page in the default partition and activation
     swaps the tab into it, which would take a tab out of its profile's
     storage. Prefetch already refuses a non-default partition; this is the
     same answer for prerender.
Delegates to: arcium::PrerenderEligibilityForTab
```

`browser.cc` builds in `//chrome/browser/ui:ui`, which reaches `//arcium/browser` through patch 0010's dep and `//arcium/ui/browser`'s public deps; `tab_lifecycle_unit.cc` needs patch 0183 from Task 0.

```bash
scripts/sync
scripts/sync
ARCIUM_JOBS=4 scripts/build dev arcium_browsertests
/Volumes/Texternal/chromium/src/out/dev/arcium_browsertests --gtest_filter='ProfileLifecycleTest.*'
```

| Mutation | Test that must fail |
|---|---|
| the `create_params.site_instance` line removed | `ADiscardedTabComesBackInItsProfile` |

- [ ] **Step 4: Record and commit**

In `patches/README.md`, add in number order:

```markdown
| `0184-discard-keeps-storage.patch` | `TabLifecycleUnit::FinishDiscard` in `chrome/browser/resource_coordinator/tab_lifecycle_unit.cc` | `arcium::SiteInstanceForReplacement` |
| `0185-prerender-profile-storage.patch` | `Browser::IsPrerender2Supported` in `chrome/browser/ui/browser.cc` | `arcium::PrerenderEligibilityForTab` |
```

```bash
/Volumes/Texternal/chromium/src/out/dev/arcium_unittests
git add arcium/browser/profile_partition.h arcium/browser/profile_partition.cc arcium/test/profile_partition_unittest.cc arcium/test/browser/profile_lifecycle_browsertest.cc patches/0184-discard-keeps-storage.patch patches/0185-prerender-profile-storage.patch patches/README.md
git commit -F - <<'MSG'
Keep a tab's logins when Chromium throws it away, and never prerender one

A discarded tab is rebuilt with a fresh contents that would land in shared
storage, so it now keeps the storage it had. Prerendering builds its page
in shared storage and then swaps the tab into it, so a tab with a profile
of its own refuses it, as prefetch already does.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
MSG
```

---

### Task 6: The guard

The hooks decide a tab's storage when the tab is made. The guard checks the result: on every main-frame navigation of a tab in the sidebar, a web page must be in its space's profile and a browser or extension page in shared storage. When one would not be, the guard cancels it and opens the same address in a tab of the right kind. It is what makes popups without an opener — which content creates, and content cannot call into Arcium — right, and what turns any path the hooks miss into a visible reopen rather than a silent shared login.

**Files:**
- Replace (skeletons from Task 0): `arcium/ui/browser/partition_guard_throttle.h`, `arcium/ui/browser/partition_guard_throttle.cc`
- Modify: `arcium/test/browser/profile_isolation_browsertest.cc`, `patches/README.md`
- Create: `patches/0186-navigation-throttle-partition-guard.patch`
- Modify (in the Chromium checkout, recorded by the patch): `chrome/browser/chrome_content_browser_client_navigation_throttles.cc`

**Interfaces:**
- Consumes (Task 2): `IsInRightStorage`, `PartitionDomainOfTab`, `ArciumProfileState::model_load_finished()`; the rule itself is already unit-tested as `ProfilePartitionTest.TheGuardsRule`.
- Produces: `arcium::PartitionGuardThrottle::MaybeCreateAndAdd(content::NavigationThrottleRegistry&)`.

- [ ] **Step 1: Write the failing tests**

Append to `arcium/test/browser/profile_isolation_browsertest.cc`, adding `#include "base/test/run_until.h"` and `#include "content/public/browser/navigation_controller.h"`:

```cpp
// content creates a popup with no opener in the default partition and
// cannot call into Arcium; the guard is what puts it right.
IN_PROC_BROWSER_TEST_F(ProfileIsolationTest, APopupWithNoOpenerIsReopenedInTheProfile) {
  ProfileId work_profile;
  AddSpaceOnNewProfile(u"Work", &work_profile);
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), PageUrl("a.test", "one"),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  content::WebContents* work_tab = active();
  SetCookie(work_tab, "work");
  const int tabs_before = strip()->count();

  // target=_blank, which is noopener by default.
  ASSERT_TRUE(
      content::ExecJs(work_tab, "document.getElementById('blank').click()"));

  const GURL opened_url = PageUrl("a.test", "blank");
  content::WebContents* opened = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&] {
    opened = FindTab(opened_url);
    return opened && opened->GetLastCommittedURL() == opened_url;
  }));
  ASSERT_TRUE(content::WaitForLoadStop(opened));
  EXPECT_EQ(PartitionDomainForProfile(work_profile), PartitionOf(opened));
  EXPECT_EQ("who=work", ReadCookie(opened));
  // The empty tab the popup arrived in is gone again: one tab added, not two.
  EXPECT_EQ(tabs_before + 1, strip()->count());
}

// Decision 7 from the other side: a browser page typed into a profile's tab
// opens in shared storage, in a tab of its own, and the page behind it stays.
IN_PROC_BROWSER_TEST_F(ProfileIsolationTest, ABrowserPageInAProfileTabMovesToSharedStorage) {
  ProfileId work_profile;
  AddSpaceOnNewProfile(u"Work", &work_profile);
  const GURL page = PageUrl("a.test", "one");
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), page, WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  content::WebContents* work_tab = active();

  const GURL version("chrome://version/");
  work_tab->GetController().LoadURL(version, content::Referrer(),
                                    ui::PAGE_TRANSITION_TYPED, std::string());
  content::WebContents* opened = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&] {
    opened = FindTab(version);
    return opened && opened->GetLastCommittedURL() == version;
  }));
  EXPECT_EQ("", PartitionOf(opened));
  // The tab the user was on still shows its page.
  EXPECT_EQ(page, work_tab->GetLastCommittedURL());
  EXPECT_EQ(PartitionDomainForProfile(work_profile), PartitionOf(work_tab));
}
```

- [ ] **Step 2: Run them to see them fail**

```bash
ARCIUM_JOBS=4 scripts/build dev arcium_browsertests
/Volumes/Texternal/chromium/src/out/dev/arcium_browsertests --gtest_filter='ProfileIsolationTest.APopupWithNoOpenerIsReopenedInTheProfile:ProfileIsolationTest.ABrowserPageInAProfileTabMovesToSharedStorage'
```

Expected: the popup lands in the default partition and reads no cookie; the browser page stays in the tab it was typed into.

- [ ] **Step 3: Write the throttle**

Replace `arcium/ui/browser/partition_guard_throttle.h`:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_BROWSER_PARTITION_GUARD_THROTTLE_H_
#define ARCIUM_UI_BROWSER_PARTITION_GUARD_THROTTLE_H_

#include "arcium/browser/model/entry_id.h"
#include "content/public/browser/navigation_throttle.h"

namespace arcium {

// The last word on which storage a page loads in. Every creation hook picks
// a tab's storage when the tab is made; this checks the result on every
// main-frame navigation of a tab in the sidebar, and when a page would land
// in the wrong storage -- a popup content created without an opener, a
// browser page in a profile's tab, or any path a future Chromium adds --
// cancels it and opens the same address in a tab of the right kind.
//
// The reopen goes through WebContents::OpenURL, so the new tab is created by
// the same hook as every other new tab, and carries a POST body and the rest
// of the navigation with it.
class PartitionGuardThrottle : public content::NavigationThrottle {
 public:
  static void MaybeCreateAndAdd(
      content::NavigationThrottleRegistry& registry);

  PartitionGuardThrottle(content::NavigationThrottleRegistry& registry,
                         ProfileId space_profile);
  PartitionGuardThrottle(const PartitionGuardThrottle&) = delete;
  PartitionGuardThrottle& operator=(const PartitionGuardThrottle&) = delete;
  ~PartitionGuardThrottle() override;

  // content::NavigationThrottle:
  ThrottleCheckResult WillStartRequest() override;
  ThrottleCheckResult WillRedirectRequest() override;
  const char* GetNameForLogging() override;

 private:
  ThrottleCheckResult CheckStorage();

  // The profile of the space the tab belonged to when the navigation
  // started. Read once: a profile change reopens every tab of the space, so
  // a tab whose space changes mid-navigation is already gone.
  const ProfileId space_profile_;
};

}  // namespace arcium

#endif  // ARCIUM_UI_BROWSER_PARTITION_GUARD_THROTTLE_H_
```

Replace `arcium/ui/browser/partition_guard_throttle.cc`:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/partition_guard_throttle.h"

#include <memory>
#include <utility>

#include "arcium/browser/arcium_profile_state.h"
#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/profile_partition.h"
#include "arcium/browser/tab_binding.h"
#include "arcium/browser/tab_space.h"
#include "arcium/ui/browser/space_switcher.h"
#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/navigation_throttle_registry.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_user_data.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"

namespace arcium {

namespace {

// The mark a reopened tab carries until its own navigation is seen. A
// defect in a creation hook can then cost one extra tab, never an endless
// chain of them.
class ReopenedByGuard : public content::WebContentsUserData<ReopenedByGuard> {
 public:
  ~ReopenedByGuard() override = default;

  GURL url;

 private:
  friend class content::WebContentsUserData<ReopenedByGuard>;
  explicit ReopenedByGuard(content::WebContents* web_contents)
      : content::WebContentsUserData<ReopenedByGuard>(*web_contents) {}

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

WEB_CONTENTS_USER_DATA_KEY_IMPL(ReopenedByGuard);

// Whether this navigation is the one a reopen asked for. Clears the mark
// either way: it answers for one navigation only.
bool WasJustReopened(content::WebContents* web_contents, const GURL& url) {
  ReopenedByGuard* mark = ReopenedByGuard::FromWebContents(web_contents);
  if (!mark) {
    return false;
  }
  const bool matches = mark->url == url;
  web_contents->RemoveUserData(ReopenedByGuard::UserDataKey());
  return matches;
}

void MarkReopened(const GURL& url, content::NavigationHandle& handle) {
  ReopenedByGuard::CreateForWebContents(handle.GetWebContents());
  ReopenedByGuard::FromWebContents(handle.GetWebContents())->url = url;
}

// A tab that never showed anything -- the popup content created for the
// navigation the guard just cancelled -- has nothing left to show. Posted,
// because a throttle may not close its own tab while it is answering, and
// checked again because anything may have happened in between.
void CloseTabIfStillEmpty(base::WeakPtr<content::WebContents> web_contents) {
  if (!web_contents) {
    return;
  }
  content::NavigationEntry* entry =
      web_contents->GetController().GetLastCommittedEntry();
  if (entry && !entry->IsInitialEntry()) {
    return;
  }
  if (tabs::TabInterface* tab =
          tabs::TabInterface::MaybeGetFromContents(web_contents.get())) {
    tab->Close();
  }
}

}  // namespace

// static
void PartitionGuardThrottle::MaybeCreateAndAdd(
    content::NavigationThrottleRegistry& registry) {
  content::NavigationHandle& handle = registry.GetNavigationHandle();
  if (!handle.IsInPrimaryMainFrame()) {
    return;
  }
  content::WebContents* web_contents = handle.GetWebContents();
  // Incognito ignores profiles: its tabs use incognito's own storage.
  if (!web_contents || web_contents->GetBrowserContext()->IsOffTheRecord()) {
    return;
  }
  tabs::TabInterface* tab =
      tabs::TabInterface::MaybeGetFromContents(web_contents);
  if (!tab) {
    return;
  }
  BrowserWindowInterface* window = tab->GetBrowserWindowInterface();
  // A window without a sidebar has no spaces, and the creation hooks leave
  // its tabs to Chromium; the guard has to leave them alone too, or the two
  // would argue over every tab in it.
  if (!window ||
      !SpaceSwitcher::FromTabStripModel(window->GetTabStripModel())) {
    return;
  }
  // IfExists, never GetForBrowserContext: the constructing variant posts an
  // archive open and a model load, and this runs for every navigation.
  ArciumProfileState* state =
      ArciumProfileState::GetForBrowserContextIfExists(
          web_contents->GetBrowserContext());
  // Until the model file has been read every space is unknown and every
  // profile reads as Default, which would reopen every restored tab of a
  // profile in shared storage. The session's answer created these tabs and
  // is the better one.
  if (!state || !state->model_load_finished()) {
    return;
  }
  if (WasJustReopened(web_contents, handle.GetURL())) {
    return;
  }
  const SpaceId space =
      SpaceOfTab(*state->model(), *state->binding(), tab->GetHandle());
  registry.AddThrottle(std::make_unique<PartitionGuardThrottle>(
      registry, state->model()->ProfileOfSpace(space)));
}

PartitionGuardThrottle::PartitionGuardThrottle(
    content::NavigationThrottleRegistry& registry,
    ProfileId space_profile)
    : content::NavigationThrottle(registry), space_profile_(space_profile) {}

PartitionGuardThrottle::~PartitionGuardThrottle() = default;

content::NavigationThrottle::ThrottleCheckResult
PartitionGuardThrottle::WillStartRequest() {
  return CheckStorage();
}

content::NavigationThrottle::ThrottleCheckResult
PartitionGuardThrottle::WillRedirectRequest() {
  // A redirect can change what kind of page this is -- an extension can
  // redirect a web page to one of its own -- and the kind is what decides
  // the storage.
  return CheckStorage();
}

const char* PartitionGuardThrottle::GetNameForLogging() {
  return "PartitionGuardThrottle";
}

content::NavigationThrottle::ThrottleCheckResult
PartitionGuardThrottle::CheckStorage() {
  content::NavigationHandle* handle = navigation_handle();
  content::WebContents* web_contents = handle->GetWebContents();
  const GURL url = handle->GetURL();
  if (IsInRightStorage(url, PartitionDomainOfTab(web_contents),
                       space_profile_)) {
    return content::NavigationThrottle::PROCEED;
  }
  content::OpenURLParams params =
      content::OpenURLParams::FromNavigationHandle(handle);
  // In a new tab rather than in this frame, so it is built by the same hook
  // as every other new tab and lands in the storage it belongs in.
  params.frame_tree_node_id = content::FrameTreeNodeId();
  params.disposition =
      web_contents->GetVisibility() == content::Visibility::VISIBLE
          ? WindowOpenDisposition::NEW_FOREGROUND_TAB
          : WindowOpenDisposition::NEW_BACKGROUND_TAB;
  web_contents->OpenURL(std::move(params), base::BindOnce(&MarkReopened, url));

  content::NavigationEntry* entry =
      web_contents->GetController().GetLastCommittedEntry();
  if (!entry || entry->IsInitialEntry()) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&CloseTabIfStillEmpty, web_contents->GetWeakPtr()));
  }
  return content::NavigationThrottle::CANCEL_AND_IGNORE;
}

}  // namespace arcium
```

- [ ] **Step 4: Register it**

In `/Volumes/Texternal/chromium/src/chrome/browser/chrome_content_browser_client_navigation_throttles.cc`, add the include beside the home-boundary one and the call after it:

```cpp
#include "arcium/ui/browser/home_boundary_throttle.h"
#include "arcium/ui/browser/partition_guard_throttle.h"
```

```cpp
  arcium::HomeBoundaryThrottle::MaybeCreateAndAdd(registry);
  arcium::PartitionGuardThrottle::MaybeCreateAndAdd(registry);
```

The home boundary goes first deliberately: a link click it diverts into a new tab never reaches the guard, and the tab it opens is built by the new-tab hook, so it is in the right storage already.

```bash
git -C /Volumes/Texternal/chromium/src diff -- chrome/browser/chrome_content_browser_client_navigation_throttles.cc > /private/tmp/0186.diff
```

The file already carries patch 0150, so generate the patch with 0150 applied — `scripts/sync` guarantees that — and check the result with two syncs. Header:

```
Seam: ChromeContentBrowserClient's navigation throttle registration, beside
      arcium::HomeBoundaryThrottle.
Why: content creates a popup without an opener itself, in the default
     partition, and cannot call into arcium/. A throttle is the one seam
     that sees such a navigation before it commits, and it also catches any
     other path that would put a page in the wrong profile's storage --
     silently, since a shared login looks like nothing on screen.
Delegates to: arcium::PartitionGuardThrottle::MaybeCreateAndAdd
```

```bash
scripts/sync
scripts/sync
scripts/format arcium/ui/browser/partition_guard_throttle.h arcium/ui/browser/partition_guard_throttle.cc arcium/test/browser/profile_isolation_browsertest.cc
ARCIUM_JOBS=4 scripts/build dev arcium_browsertests
/Volumes/Texternal/chromium/src/out/dev/arcium_browsertests --gtest_filter='ProfileIsolationTest.*'
```

- [ ] **Step 5: Mutation checks, the suites, the inventory and the commit**

| Mutation | Test that must fail |
|---|---|
| `CheckStorage` returns `PROCEED` always | `APopupWithNoOpenerIsReopenedInTheProfile` |
| the `kShared` case of `IsInRightStorage` returns true (Task 2's code) | `ABrowserPageInAProfileTabMovesToSharedStorage` |
| the `CloseTabIfStillEmpty` post is removed | `APopupWithNoOpenerIsReopenedInTheProfile`'s tab count |
| `MaybeCreateAndAdd` drops the `IsInPrimaryMainFrame` check | nothing today — leave the check; it is what keeps iframes and prerenders out |

Also run the whole browser-test binary and the unit suite:

```bash
/Volumes/Texternal/chromium/src/out/dev/arcium_browsertests
/Volumes/Texternal/chromium/src/out/dev/arcium_unittests
```

In `patches/README.md`, add in number order:

```markdown
| `0186-navigation-throttle-partition-guard.patch` | `CreateAndAddChromeThrottlesForNavigation` in `chrome/browser/chrome_content_browser_client_navigation_throttles.cc` | `arcium::PartitionGuardThrottle::MaybeCreateAndAdd` |
```

```bash
git add arcium/ui/browser/partition_guard_throttle.h arcium/ui/browser/partition_guard_throttle.cc arcium/test/browser/profile_isolation_browsertest.cc patches/0186-navigation-throttle-partition-guard.patch patches/README.md
git commit -F - <<'MSG'
Reopen any page that would load in the wrong profile's storage

A popup opened without an opener is built by content itself, which cannot
ask Arcium anything, so it starts in shared storage; the same check also
catches a browser page opened inside a profile's tab, and anything a later
Chromium adds. Getting this wrong is invisible -- the page simply knows
the wrong account -- so the check runs on every main-frame navigation of a
tab the sidebar owns.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
MSG
```

---

### Task 7: Reopening a tab in another profile

A tab's storage cannot change, so moving a tab between profiles means a new tab at the same address. Chromium's own discard machinery swaps a tab's contents in place, which keeps the tab's handle, its place in the strip, its pinned or favourite entry and its selection; the history is carried across by serialising it, the way session restore does. The tab on screen loads again at once; a background one waits for a click, as after a restart.

**Files:**
- Replace (skeletons from Task 0): `arcium/ui/browser/profile_reopen.h`, `arcium/ui/browser/profile_reopen.cc`, `arcium/test/profile_reopen_unittest.cc`
- Modify: `arcium/ui/browser/space_switcher_spaces.cc`, `arcium/test/space_switcher_unittest.cc`

**Interfaces:**
- Consumes (Task 2): `SiteInstanceForProfile`, `PartitionDomainOfTab`; (Task 1) `ArciumModel::ProfileOfSpace`.
- Produces, in `arcium/ui/browser/profile_reopen.h`: `void ReopenTabInProfile(TabStripModel* strip, int index, const ProfileId& profile)`.

- [ ] **Step 1: Write the failing tests**

Replace `arcium/test/profile_reopen_unittest.cc`:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/profile_reopen.h"

#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/arcium_profile.h"
#include "arcium/browser/model/entry_id.h"
#include "arcium/browser/profile_partition.h"
#include "arcium/browser/restored_tab_loading.h"
#include "arcium/browser/tab_binding.h"
#include "arcium/browser/tab_space.h"
#include "arcium/test/space_test_util.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/web_contents_tester.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace arcium {
namespace {

const ProfileId kWork =
    ProfileId::FromString("22222222-2222-4222-8222-222222222222");

class ProfileReopenTest : public BrowserWithTestWindowTest {
 protected:
  TabStripModel* strip() { return browser()->tab_strip_model(); }

  // A tab with two pages behind it, tagged with `space`.
  tabs::TabInterface* AddVisitedTab(SpaceId space) {
    tabs::TabInterface* tab = arcium::test::AddTabInSpace(
        strip(), profile(), GURL("https://a.example/one"), space);
    content::WebContentsTester::For(tab->GetContents())
        ->NavigateAndCommit(GURL("https://a.example/two"));
    return tab;
  }

  ArciumModel model_;
  TabBinding binding_;
};

TEST_F(ProfileReopenTest, ThePageAndTheWayBackToItComeWith) {
  tabs::TabInterface* tab = AddVisitedTab(model_.default_space_id());
  ReopenTabInProfile(strip(), strip()->GetIndexOfTab(tab), kWork);

  content::WebContents* reopened = strip()->GetWebContentsAt(0);
  content::NavigationController& controller = reopened->GetController();
  EXPECT_EQ(2, controller.GetEntryCount());
  EXPECT_EQ(1, controller.GetLastCommittedEntryIndex());
  EXPECT_EQ(GURL("https://a.example/two"),
            controller.GetLastCommittedEntry()->GetVirtualURL());
  EXPECT_EQ(GURL("https://a.example/one"),
            controller.GetEntryAtIndex(0)->GetVirtualURL());
}

TEST_F(ProfileReopenTest, TheTabLandsInTheNewProfilesStorage) {
  tabs::TabInterface* tab = AddVisitedTab(model_.default_space_id());
  ReopenTabInProfile(strip(), strip()->GetIndexOfTab(tab), kWork);
  EXPECT_EQ(PartitionDomainForProfile(kWork),
            PartitionDomainOfTab(strip()->GetWebContentsAt(0)));
}

// The swap happens inside the tab, so nothing that hangs off the tab --
// its place, its handle, the entry bound to it -- has to be rebuilt.
TEST_F(ProfileReopenTest, TheTabKeepsItsPlaceItsHandleItsEntryAndItsTag) {
  const SpaceId work_space = model_.AddSpace(u"Work");
  tabs::TabInterface* tab = AddVisitedTab(work_space);
  arcium::test::AddTabInSpace(strip(), profile(), GURL("https://b.example/"),
                              model_.default_space_id());
  const int index = strip()->GetIndexOfTab(tab);
  const tabs::TabHandle handle = tab->GetHandle();
  const TabKey key = KeyOf(tab->GetContents());
  const EntryId entry = model_.AddEntry(work_space, EntryKind::kPinned,
                                        GURL("https://a.example/one"), u"A");
  binding_.Bind(entry, handle);

  ReopenTabInProfile(strip(), index, kWork);

  EXPECT_EQ(index, strip()->GetIndexOfTab(handle.Get()));
  EXPECT_EQ(handle, strip()->GetTabAtIndex(index)->GetHandle());
  content::WebContents* reopened = strip()->GetWebContentsAt(index);
  EXPECT_EQ(work_space, SpaceTagOf(reopened));
  EXPECT_EQ(key, ExistingKeyOf(reopened));
  EXPECT_EQ(handle, binding_.TabForEntry(entry));
}

// R3.9's rule holds through a reopen: only the tab you are looking at
// loads, and it loads at once.
TEST_F(ProfileReopenTest, TheTabOnScreenLoadsAgainAndABackgroundOneWaits) {
  tabs::TabInterface* on_screen = AddVisitedTab(model_.default_space_id());
  tabs::TabInterface* behind = AddVisitedTab(model_.default_space_id());
  strip()->ActivateTabAt(strip()->GetIndexOfTab(on_screen));

  ReopenTabInProfile(strip(), strip()->GetIndexOfTab(behind), kWork);
  EXPECT_TRUE(IsTabUnloaded(strip()->GetWebContentsAt(1)));

  ReopenTabInProfile(strip(), strip()->GetIndexOfTab(on_screen), kWork);
  EXPECT_FALSE(strip()->GetWebContentsAt(0)->GetController().NeedsReload());
}

}  // namespace
}  // namespace arcium
```

Add to `arcium/test/space_switcher_unittest.cc`:

```cpp
TEST_F(SpaceSwitcherTest, MovingATabToASpaceOnAnotherProfileReopensIt) {
  const ProfileId work_profile = model_.AddProfile(u"Work", 1);
  const SpaceId work = model_.AddSpace(u"Work", work_profile);
  auto switcher = MakeSwitcher();
  tabs::TabInterface* tab =
      AddTabInSpace(GURL("https://a.example/"), model_.default_space_id());
  const int index = strip()->GetIndexOfTab(tab);
  content::WebContents* before = strip()->GetWebContentsAt(index);

  switcher->MoveTabToSpace(index, work);

  content::WebContents* after = strip()->GetWebContentsAt(index);
  EXPECT_NE(before, after);
  EXPECT_EQ(PartitionDomainForProfile(work_profile),
            PartitionDomainOfTab(after));
  EXPECT_EQ(work, SpaceTagOf(after));
  EXPECT_EQ(work, switcher->SpaceOfTabAt(index));
}

// Same profile, so nothing is thrown away: the tab is only re-tagged.
TEST_F(SpaceSwitcherTest, MovingATabWithinAProfileKeepsTheSameTab) {
  const SpaceId other = model_.AddSpace(u"Other");
  auto switcher = MakeSwitcher();
  tabs::TabInterface* tab =
      AddTabInSpace(GURL("https://a.example/"), model_.default_space_id());
  const int index = strip()->GetIndexOfTab(tab);
  content::WebContents* before = strip()->GetWebContentsAt(index);

  switcher->MoveTabToSpace(index, other);

  EXPECT_EQ(before, strip()->GetWebContentsAt(index));
  EXPECT_EQ(other, SpaceTagOf(before));
}

// A pin dragged to a space on another profile takes its open tab with it,
// and the tab comes back logged in as that profile.
TEST_F(SpaceSwitcherTest, MovingAnEntryToAnotherProfileReopensItsTab) {
  const ProfileId work_profile = model_.AddProfile(u"Work", 1);
  const SpaceId work = model_.AddSpace(u"Work", work_profile);
  auto switcher = MakeSwitcher();
  tabs::TabInterface* tab =
      AddTabInSpace(GURL("https://a.example/"), model_.default_space_id());
  const EntryId entry =
      model_.AddEntry(model_.default_space_id(), EntryKind::kPinned,
                      GURL("https://a.example/"), u"A");
  binding_.Bind(entry, tab->GetHandle());
  const int index = strip()->GetIndexOfTab(tab);

  switcher->MoveEntryToSpace(entry, work);

  content::WebContents* after = strip()->GetWebContentsAt(index);
  EXPECT_EQ(PartitionDomainForProfile(work_profile),
            PartitionDomainOfTab(after));
  EXPECT_EQ(work, SpaceTagOf(after));
  EXPECT_EQ(tab->GetHandle(), binding_.TabForEntry(entry));
}
```

- [ ] **Step 2: Run them to see them fail, then implement the reopen**

Replace `arcium/ui/browser/profile_reopen.h`:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_BROWSER_PROFILE_REOPEN_H_
#define ARCIUM_UI_BROWSER_PROFILE_REOPEN_H_

#include "arcium/browser/model/entry_id.h"

class TabStripModel;

namespace arcium {

// Puts the tab at `index` on `profile`'s storage. A tab's storage is fixed
// when its contents is created, so this makes a new contents at the same
// address, with the same history, and swaps it into the same tab -- the way
// Chromium replaces a tab it has discarded. The tab keeps its place, its
// handle, its pinned or favourite entry, its space tag and its key; the page
// is logged in as `profile` from now on, and anything it had unsaved is
// lost, since no beforeunload runs.
//
// The tab on screen loads again at once. A background tab comes back
// unloaded and loads when it is clicked, as a restored tab does.
//
// Does nothing off the record, where profiles do not apply.
void ReopenTabInProfile(TabStripModel* strip,
                        int index,
                        const ProfileId& profile);

}  // namespace arcium

#endif  // ARCIUM_UI_BROWSER_PROFILE_REOPEN_H_
```

Replace `arcium/ui/browser/profile_reopen.cc`:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/profile_reopen.h"

#include <memory>
#include <utility>
#include <vector>

#include "arcium/browser/profile_partition.h"
#include "arcium/browser/tab_space.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/sessions/content/content_serialized_navigation_builder.h"
#include "components/sessions/core/serialized_navigation_entry.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/restore_type.h"
#include "content/public/browser/web_contents.h"
#include "url/gurl.h"

namespace arcium {

void ReopenTabInProfile(TabStripModel* strip,
                        int index,
                        const ProfileId& profile) {
  if (!strip || index < 0 || index >= strip->count()) {
    return;
  }
  content::WebContents* old_contents = strip->GetWebContentsAt(index);
  content::BrowserContext* context = old_contents->GetBrowserContext();
  if (context->IsOffTheRecord()) {
    return;
  }

  // Serialised and rebuilt, never CopyStateFrom: that clones each entry's
  // SiteInstance, which belongs to the storage this tab is leaving, and the
  // old session-storage map with it.
  std::vector<sessions::SerializedNavigationEntry> navigations;
  int selected = 0;
  content::NavigationController& old_controller =
      old_contents->GetController();
  for (int i = 0; i < old_controller.GetEntryCount(); ++i) {
    content::NavigationEntry* entry = old_controller.GetEntryAtIndex(i);
    if (entry->IsInitialEntry()) {
      continue;
    }
    if (i == old_controller.GetLastCommittedEntryIndex()) {
      selected = static_cast<int>(navigations.size());
    }
    navigations.push_back(
        sessions::ContentSerializedNavigationBuilder::FromNavigationEntry(
            static_cast<int>(navigations.size()), entry));
  }

  const GURL url = navigations.empty()
                       ? GURL(url::kAboutBlankURL)
                       : navigations[selected].virtual_url();
  content::WebContents::CreateParams params(
      context, SiteInstanceForProfile(context, profile, url));
  params.initially_hidden =
      old_contents->GetVisibility() == content::Visibility::HIDDEN;
  // Nothing loads until the tab is on screen, exactly as a restored tab.
  params.desired_renderer_state =
      content::WebContents::CreateParams::kNoRendererProcess;
  params.last_active_time = old_contents->GetLastActiveTime();
  params.last_active_time_ticks = old_contents->GetLastActiveTimeTicks();
  std::unique_ptr<content::WebContents> new_contents =
      content::WebContents::Create(params);

  if (!navigations.empty()) {
    std::vector<std::unique_ptr<content::NavigationEntry>> entries =
        sessions::ContentSerializedNavigationBuilder::ToNavigationEntries(
            navigations, context);
    new_contents->GetController().Restore(
        selected, content::RestoreType::kRestored, &entries);
  }

  // The tag and the key live on the WebContents and do not follow it.
  SetSpaceTag(new_contents.get(), SpaceTagOf(old_contents));
  if (const TabKey key = ExistingKeyOf(old_contents); key.is_valid()) {
    SetTabKey(new_contents.get(), key);
  }

  const bool was_on_screen = strip->active_index() == index;
  // Inside the tab: the handle, the entry binding and the selection all
  // stay, and the tab strip reports a replacement rather than a close.
  std::unique_ptr<content::WebContents> discarded =
      strip->DiscardWebContentsAt(index, std::move(new_contents));
  discarded.reset();
  if (was_on_screen) {
    strip->GetWebContentsAt(index)->GetController().LoadIfNecessary();
  }
}

}  // namespace arcium
```

Add `#include "url/url_constants.h"` for `url::kAboutBlankURL`.

- [ ] **Step 3: Make the two moves reopen**

In `arcium/ui/browser/space_switcher_spaces.cc`, add `#include "arcium/browser/model/arcium_profile.h"` and `#include "arcium/ui/browser/profile_reopen.h"`, and make `MoveTabToSpace`:

```cpp
void SpaceSwitcher::MoveTabToSpace(int index, SpaceId space) {
  if (!tab_strip_model_ || index < 0 || index >= tab_strip_model_->count() ||
      !model_->GetSpace(space)) {
    return;
  }
  // A tab cannot change its storage, so a move between profiles is a
  // reopen: same address, same history, same place, other logins.
  if (model_->ProfileOfSpace(SpaceOfTabAt(index)) !=
      model_->ProfileOfSpace(space)) {
    ReopenTabInProfile(tab_strip_model_, index, model_->ProfileOfSpace(space));
  }
  SetSpaceTag(tab_strip_model_->GetTabAtIndex(index)->GetContents(), space);
  AskForSessionRebuild();
  // Moving the tab you are looking at takes you with it, as Zen does --
  // adopted, not switched to: SwitchTo would land on whatever `space`
  // already remembers as its last active tab, which can be a different tab
  // than the one that just moved, so the moved page would disappear behind
  // it.
  if (index == tab_strip_model_->active_index()) {
    AdoptSpace(space);
  }
}
```

and in `MoveEntryToSpace`, read the entry's profile **before** the model moves it, and reopen its tab when that profile changes:

```cpp
void SpaceSwitcher::MoveEntryToSpace(EntryId id, SpaceId space) {
  const TabEntry* before = model_->GetEntry(id);
  const ProfileId from = before ? model_->ProfileOfSpace(before->space_id)
                                : DefaultProfileId();
  model_->MoveEntryToSpace(id, space);
  const TabEntry* entry = model_->GetEntry(id);
  if (!entry || entry->space_id != space || !tab_strip_model_) {
    return;
  }
  std::optional<tabs::TabHandle> handle = binding_->TabForEntry(id);
  tabs::TabInterface* tab = handle ? handle->Get() : nullptr;
  if (!tab) {
    return;
  }
  const int index = tab_strip_model_->GetIndexOfTab(tab);
  // Only this window's strip: an entry's tab living in another window is
  // re-tagged here and put right by the guard when it next navigates.
  if (from != model_->ProfileOfSpace(space) &&
      index != TabStripModel::kNoTab) {
    ReopenTabInProfile(tab_strip_model_, index, model_->ProfileOfSpace(space));
    tab = tab_strip_model_->GetTabAtIndex(index);
  }
  // The entry's own tab is re-tagged too: an unpin drops the entry and
  // falls back to whatever the tab itself carries, and that has to agree
  // with where the entry just went.
  SetSpaceTag(tab->GetContents(), space);
  AskForSessionRebuild();
  if (index != TabStripModel::kNoTab &&
      index == tab_strip_model_->active_index()) {
    AdoptSpace(space);
  }
}
```

- [ ] **Step 4: Run, mutate and commit**

```bash
scripts/format arcium/ui/browser/profile_reopen.h arcium/ui/browser/profile_reopen.cc arcium/ui/browser/space_switcher_spaces.cc arcium/test/profile_reopen_unittest.cc arcium/test/space_switcher_unittest.cc
ARCIUM_JOBS=4 scripts/build dev arcium_unittests
/Volumes/Texternal/chromium/src/out/dev/arcium_unittests --gtest_filter='ProfileReopenTest.*:SpaceSwitcherTest.*'
```

| Mutation | Test that must fail |
|---|---|
| `ReopenTabInProfile` skips the `Restore` call | `ThePageAndTheWayBackToItComeWith` |
| `ReopenTabInProfile` passes no SiteInstance | `TheTabLandsInTheNewProfilesStorage` |
| `ReopenTabInProfile` skips `SetSpaceTag`/`SetTabKey` | `TheTabKeepsItsPlaceItsHandleItsEntryAndItsTag` |
| `ReopenTabInProfile` skips the `LoadIfNecessary` | `TheTabOnScreenLoadsAgainAndABackgroundOneWaits` |
| `MoveTabToSpace` reopens unconditionally | `MovingATabWithinAProfileKeepsTheSameTab` |
| `MoveEntryToSpace` reads `from` after the model move | `MovingAnEntryToAnotherProfileReopensItsTab` |

```bash
/Volumes/Texternal/chromium/src/out/dev/arcium_unittests
git add arcium/ui/browser/profile_reopen.h arcium/ui/browser/profile_reopen.cc arcium/ui/browser/space_switcher_spaces.cc arcium/test/profile_reopen_unittest.cc arcium/test/space_switcher_unittest.cc
git commit -F - <<'MSG'
Move a tab to another profile by reopening it where it lands

A page cannot change which logins it uses, so a tab that moves to a space
with another profile is built again at the same address and swapped into
the same tab: its place, its history, its pin and its position in the
sidebar all survive, and only the account changes. The tab you are looking
at loads again straight away; the rest wait to be clicked.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
MSG
```

---

### Task 8: Changing, clearing and deleting a profile, in every window

The three operations the menu will call. All of them reach every window, because profiles belong to the browser rather than to a window: changing a space's profile reopens its tabs wherever they are, clearing empties one profile's storage and leaves the others alone, and deleting moves the profile's spaces to Default and erases what it held.

**Files:**
- Replace (skeletons from Task 0): `arcium/browser/profile_data.h`, `arcium/browser/profile_data.cc`, `arcium/ui/browser/profile_actions.h`, `arcium/ui/browser/profile_actions.cc`
- Modify: `arcium/test/browser/profile_lifecycle_browsertest.cc`

**Interfaces:**
- Consumes (Task 2): `PartitionForProfile`, `PartitionDirectory`, `IsPartitionLoaded`, `IsArciumPartitionDomain`; (Task 7) `ReopenTabInProfile`; (Task 1) `ArciumModel::RemoveProfile`, `SetSpaceProfile`, `GetProfile`, `spaces()`.
- Produces, in `arcium/browser/profile_data.h`: `void ClearArciumProfileData(content::BrowserContext*, const ProfileId&, base::OnceClosure done)`.
- Produces, in `arcium/ui/browser/profile_actions.h` (which includes `profile_data.h`, so one include gives a caller all three): `void MoveSpaceToProfile(content::BrowserContext*, SpaceId, ProfileId)`, `void DeleteArciumProfile(content::BrowserContext*, ProfileId)`.

- [ ] **Step 1: Write the failing tests**

Append to `arcium/test/browser/profile_lifecycle_browsertest.cc`, adding `#include "arcium/browser/model/arcium_profile.h"`, `#include "arcium/browser/profile_data.h"`, `#include "arcium/ui/browser/profile_actions.h"`, `#include "base/run_loop.h"` and `#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"`:

```cpp
// Profiles belong to the browser, not to a window, so a change reaches
// every window's tabs.
IN_PROC_BROWSER_TEST_F(ProfileLifecycleTest, ChangingASpacesProfileReopensItsTabsEverywhere) {
  ProfileId work_profile;
  const SpaceId work = AddSpaceOnNewProfile(u"Work", &work_profile);
  const GURL url = PageUrl("a.test", "one");
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), url, WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  SetCookie(active(), "work");
  const int index = strip()->active_index();
  content::WebContents* before = active();

  Browser* second = CreateBrowser(browser()->profile());
  SpaceSwitcher* second_switcher =
      SpaceSwitcher::FromTabStripModel(second->tab_strip_model());
  ASSERT_TRUE(second_switcher);
  second_switcher->SwitchTo(work);
  ui_test_utils::NavigateToURLWithDisposition(
      second, url, WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  const int second_index = second->tab_strip_model()->active_index();
  ASSERT_EQ(PartitionDomainForProfile(work_profile),
            PartitionOf(second->tab_strip_model()->GetWebContentsAt(second_index)));

  MoveSpaceToProfile(browser()->profile(), work, DefaultProfileId());

  content::WebContents* after = strip()->GetWebContentsAt(index);
  EXPECT_NE(before, after);
  EXPECT_EQ("", PartitionOf(after));
  EXPECT_EQ(work, switcher()->SpaceOfTabAt(index));
  ASSERT_TRUE(content::WaitForLoadStop(after));
  EXPECT_EQ(url, after->GetLastCommittedURL());
  // Logged out, because Default has never seen this site.
  EXPECT_EQ("", ReadCookie(after));
  EXPECT_EQ("",
            PartitionOf(second->tab_strip_model()->GetWebContentsAt(second_index)));
}

IN_PROC_BROWSER_TEST_F(ProfileLifecycleTest, ClearingOneProfileLeavesTheOthersAlone) {
  const GURL url = PageUrl("a.test", "one");
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), url, WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  content::WebContents* default_tab = active();
  SetCookie(default_tab, "default");

  ProfileId work_profile;
  AddSpaceOnNewProfile(u"Work", &work_profile);
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), url, WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  content::WebContents* work_tab = active();
  SetCookie(work_tab, "work");

  ProfileId home_profile;
  AddSpaceOnNewProfile(u"Home", &home_profile);
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), url, WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  content::WebContents* home_tab = active();
  SetCookie(home_tab, "home");

  base::RunLoop loop;
  ClearArciumProfileData(browser()->profile(), work_profile,
                         loop.QuitClosure());
  loop.Run();

  EXPECT_EQ("", ReadCookie(work_tab));
  EXPECT_EQ("who=home", ReadCookie(home_tab));
  EXPECT_EQ("who=default", ReadCookie(default_tab));
  // Decision 8: clearing does not reload anything, as Chrome's own clear
  // does not.
  EXPECT_EQ(url, work_tab->GetLastCommittedURL());
}

IN_PROC_BROWSER_TEST_F(ProfileLifecycleTest, DeletingAProfileMovesItsSpacesAndLogsThemOut) {
  ProfileId work_profile;
  const SpaceId work = AddSpaceOnNewProfile(u"Work", &work_profile);
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), PageUrl("a.test", "one"),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  SetCookie(active(), "work");
  const int index = strip()->active_index();

  DeleteArciumProfile(browser()->profile(), work_profile);

  EXPECT_EQ(1u, model()->profiles().size());
  EXPECT_EQ(DefaultProfileId(), model()->ProfileOfSpace(work));
  content::WebContents* after = strip()->GetWebContentsAt(index);
  EXPECT_EQ("", PartitionOf(after));
  ASSERT_TRUE(content::WaitForLoadStop(after));
  EXPECT_EQ("", ReadCookie(after));
}

IN_PROC_BROWSER_TEST_F(ProfileLifecycleTest, DefaultCannotBeDeleted) {
  DeleteArciumProfile(browser()->profile(), DefaultProfileId());
  EXPECT_EQ(1u, model()->profiles().size());
  EXPECT_EQ(DefaultProfileId(), model()->profiles()[0].id);
}
```

- [ ] **Step 2: Run them to see them fail, then write the clearing**

Replace `arcium/browser/profile_data.h`:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_BROWSER_PROFILE_DATA_H_
#define ARCIUM_BROWSER_PROFILE_DATA_H_

#include "arcium/browser/model/entry_id.h"
#include "base/functional/callback_forward.h"

namespace content {
class BrowserContext;
}

namespace arcium {

// Removes everything a site keeps in `profile`: cookies, site storage and
// cache, and nothing else -- history, bookmarks and passwords are shared
// between profiles and are not touched. `done` runs when the removal has
// finished. The profile's tabs are left as they are, showing what they
// already had, as Chrome's own clear does.
//
// A profile nothing has opened this session has its storage built in order
// to be cleared; there is no way to remove a cookie store that does not
// exist yet.
void ClearArciumProfileData(content::BrowserContext* context,
                            const ProfileId& profile,
                            base::OnceClosure done);

}  // namespace arcium

#endif  // ARCIUM_BROWSER_PROFILE_DATA_H_
```

Replace `arcium/browser/profile_data.cc`:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/profile_data.h"

#include <utility>

#include "arcium/browser/profile_partition.h"
#include "base/functional/callback.h"
#include "base/scoped_observation.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browsing_data_filter_builder.h"
#include "content/public/browser/browsing_data_remover.h"

namespace arcium {

namespace {

// Everything Chrome's own dialog removes for "cookies and other site data"
// and "cached images and files", limited to what lives in a storage
// partition: a filter that names a partition may not carry anything else
// (browsing_data_remover_impl.cc DCHECKs it), and the rest -- history, form
// data, site settings -- is shared between profiles anyway.
constexpr uint64_t kProfileDataMask =
    content::BrowsingDataRemover::DATA_TYPE_ON_STORAGE_PARTITION &
    (content::BrowsingDataRemover::DATA_TYPE_COOKIES |
     content::BrowsingDataRemover::DATA_TYPE_DOM_STORAGE |
     content::BrowsingDataRemover::DATA_TYPE_PRIVACY_SANDBOX |
     content::BrowsingDataRemover::DATA_TYPE_DEVICE_BOUND_SESSIONS |
     content::BrowsingDataRemover::DATA_TYPE_CACHE);

// The remover reports a removal to one observer, which has to be watching
// before the removal starts. Chrome's own helper does the same thing in an
// anonymous namespace of its own (browsing_data_important_sites_util.cc).
class OneRemovalObserver : public content::BrowsingDataRemover::Observer {
 public:
  OneRemovalObserver(content::BrowsingDataRemover* remover,
                     base::OnceClosure done)
      : done_(std::move(done)) {
    observation_.Observe(remover);
  }

  void OnBrowsingDataRemoverDone(uint64_t failed_data_types) override {
    observation_.Reset();
    std::move(done_).Run();
    delete this;
  }

 private:
  ~OneRemovalObserver() override = default;

  base::OnceClosure done_;
  base::ScopedObservation<content::BrowsingDataRemover,
                          content::BrowsingDataRemover::Observer>
      observation_{this};
};

}  // namespace

void ClearArciumProfileData(content::BrowserContext* context,
                            const ProfileId& profile,
                            base::OnceClosure done) {
  std::unique_ptr<content::BrowsingDataFilterBuilder> filter =
      content::BrowsingDataFilterBuilder::Create(
          content::BrowsingDataFilterBuilder::Mode::kPreserve);
  // No partition means the default profile, which is Chromium's own
  // partition: an empty preserve filter clears exactly that.
  if (const std::optional<content::StoragePartitionConfig> partition =
          PartitionForProfile(context, profile)) {
    filter->SetStoragePartitionConfig(*partition);
  }
  content::BrowsingDataRemover* remover = context->GetBrowsingDataRemover();
  remover->RemoveWithFilterAndReply(
      base::Time(), base::Time::Max(), kProfileDataMask,
      content::BrowsingDataRemover::ORIGIN_TYPE_UNPROTECTED_WEB,
      std::move(filter), new OneRemovalObserver(remover, std::move(done)));
}

}  // namespace arcium
```

`ORIGIN_TYPE_UNPROTECTED_WEB` alone, exactly as Chrome's dialog uses for site data; the remover ignores cookies without it.

- [ ] **Step 3: The three actions**

Replace `arcium/ui/browser/profile_actions.h`:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_BROWSER_PROFILE_ACTIONS_H_
#define ARCIUM_UI_BROWSER_PROFILE_ACTIONS_H_

#include "arcium/browser/model/entry_id.h"
// So one include gives a caller all three of the menu's operations.
#include "arcium/browser/profile_data.h"

namespace content {
class BrowserContext;
}

namespace arcium {

// Puts `space` on `profile` and reopens every open tab of that space, in
// every window, so each one is logged in as the new profile. Does nothing
// for an unknown space or profile, for a space already on `profile`, or off
// the record.
void MoveSpaceToProfile(content::BrowserContext* context,
                        SpaceId space,
                        ProfileId profile);

// Erases `profile`: its spaces move to Default, which reopens their tabs
// logged out, it is removed from the model, and its stored logins and site
// data are deleted. A profile nothing opened this session goes at once,
// cache and all; one that was opened is emptied now and its folder is
// removed at a later launch, when Chrome's own partition cleanup runs.
// Refuses Default.
void DeleteArciumProfile(content::BrowserContext* context, ProfileId profile);

}  // namespace arcium

#endif  // ARCIUM_UI_BROWSER_PROFILE_ACTIONS_H_
```

Replace `arcium/ui/browser/profile_actions.cc`:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/profile_actions.h"

#include <string>
#include <utility>
#include <vector>

#include "arcium/browser/arcium_profile_state.h"
#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/arcium_profile.h"
#include "arcium/browser/model/space.h"
#include "arcium/browser/profile_partition.h"
#include "arcium/browser/tab_binding.h"
#include "arcium/browser/tab_space.h"
#include "arcium/ui/browser/profile_reopen.h"
#include "arcium/ui/browser/session_rebuild_nudge.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/weak_ptr.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_paths_internal.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/browser_context.h"

namespace arcium {

namespace {

// The partition's HTTP cache is not inside the partition: Chromium keeps it
// under the user's cache directory, which on macOS is a different tree
// altogether. Nothing else deletes it, so a profile that goes takes it.
void DeleteProfileCache(Profile* profile, const ProfileId& id) {
  const base::FilePath partition_path =
      PartitionDirectory(profile->GetPath(), id)
          .Append(FILE_PATH_LITERAL("def"));
  base::FilePath cache_path;
  chrome::GetUserCacheDirectory(partition_path, &cache_path);
  const base::FilePath domain_directory = cache_path.DirName();
  // GetUserCacheDirectory hands back what it was given when it cannot map
  // it, so the guard is on the name: never delete a directory that is not
  // this profile's own.
  if (!IsArciumPartitionDomain(domain_directory.BaseName().MaybeAsASCII())) {
    return;
  }
  base::ThreadPool::PostTask(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::BEST_EFFORT},
      base::BindOnce(base::IgnoreResult(&base::DeletePathRecursively),
                     domain_directory));
}

// A partition something opened this session cannot be deleted, only
// emptied. Chrome's own callers answer this by asking for the cleanup
// sweep at the next launch, which is the only thing that removes such a
// directory.
void AskForCleanupAtNextLaunch(base::WeakPtr<Profile> profile) {
  if (profile) {
    profile->GetPrefs()->SetBoolean(
        prefs::kShouldGarbageCollectStoragePartitions, true);
  }
}

void ObliterateWhenCleared(base::WeakPtr<Profile> profile,
                           const std::string& partition_domain) {
  if (!profile) {
    return;
  }
  profile->AsyncObliterateStoragePartition(
      partition_domain,
      base::BindOnce(&AskForCleanupAtNextLaunch, profile),
      base::DoNothing());
}

}  // namespace

void MoveSpaceToProfile(content::BrowserContext* context,
                        SpaceId space,
                        ProfileId profile) {
  ArciumProfileState* state =
      ArciumProfileState::GetForBrowserContextIfExists(context);
  if (!state || context->IsOffTheRecord()) {
    return;
  }
  ArciumModel* model = state->model();
  if (!model->GetSpace(space) || !model->GetProfile(profile) ||
      model->ProfileOfSpace(space) == profile) {
    return;
  }
  model->SetSpaceProfile(space, profile);

  Profile* chrome_profile = Profile::FromBrowserContext(context);
  ProfileBrowserCollection::GetForProfile(chrome_profile)
      ->ForEach([&](BrowserWindowInterface* window) {
        TabStripModel* strip = window->GetTabStripModel();
        for (int i = 0; i < strip->count(); ++i) {
          if (SpaceOfTab(*model, *state->binding(),
                         strip->GetTabAtIndex(i)->GetHandle()) == space) {
            // In place, so the index still names the same tab afterwards.
            ReopenTabInProfile(strip, i, profile);
          }
        }
        return true;
      });
  // Every tab of the space now records another profile in the session file.
  RequestSessionRebuild(chrome_profile);
}

void DeleteArciumProfile(content::BrowserContext* context,
                         ProfileId profile) {
  ArciumProfileState* state =
      ArciumProfileState::GetForBrowserContextIfExists(context);
  if (!state || context->IsOffTheRecord() || profile == DefaultProfileId() ||
      !state->model()->GetProfile(profile)) {
    return;
  }
  // The spaces move first, so their tabs are already off this storage when
  // it goes.
  std::vector<SpaceId> spaces;
  for (const Space& space : state->model()->spaces()) {
    if (space.profile_id == profile) {
      spaces.push_back(space.id);
    }
  }
  for (const SpaceId& space : spaces) {
    MoveSpaceToProfile(context, space, DefaultProfileId());
  }
  state->model()->RemoveProfile(profile);

  Profile* chrome_profile = Profile::FromBrowserContext(context);
  const std::string partition_domain = PartitionDomainForProfile(profile);
  if (!IsPartitionLoaded(context, profile)) {
    // Nothing holds it open, so content deletes the whole directory now.
    DeleteProfileCache(chrome_profile, profile);
    context->AsyncObliterateStoragePartition(partition_domain,
                                             base::DoNothing(),
                                             base::DoNothing());
    return;
  }
  // Cookies, site data and the cache through the remover -- obliterate
  // does not reach the cache -- and then the partition itself.
  ClearArciumProfileData(
      context, profile,
      base::BindOnce(&ObliterateWhenCleared, chrome_profile->GetWeakPtr(),
                     partition_domain));
}

}  // namespace arcium
```

Check the header for `chrome::GetUserCacheDirectory` in this tree (`chrome/common/chrome_paths_internal.h`) and that `//arcium/ui/browser` may include it; if that header is not reachable, use `chrome/common/chrome_paths.h`'s equivalent, and if neither is, leave the cache to the next launch's cleanup and say so in the comment and in the report.

- [ ] **Step 4: Run, mutate and commit**

```bash
scripts/format arcium/browser/profile_data.h arcium/browser/profile_data.cc arcium/ui/browser/profile_actions.h arcium/ui/browser/profile_actions.cc arcium/test/browser/profile_lifecycle_browsertest.cc
ARCIUM_JOBS=4 scripts/build dev arcium_browsertests
/Volumes/Texternal/chromium/src/out/dev/arcium_browsertests --gtest_filter='ProfileLifecycleTest.*'
```

| Mutation | Test that must fail |
|---|---|
| `ClearArciumProfileData` drops `SetStoragePartitionConfig` | `ClearingOneProfileLeavesTheOthersAlone` (the default tab's cookie goes instead) |
| `MoveSpaceToProfile` walks only the window it was called from | `ChangingASpacesProfileReopensItsTabsEverywhere` |
| `MoveSpaceToProfile` skips `SetSpaceProfile` | `DeletingAProfileMovesItsSpacesAndLogsThemOut` |
| `DeleteArciumProfile` drops the Default check | `DefaultCannotBeDeleted` |

```bash
/Volumes/Texternal/chromium/src/out/dev/arcium_unittests
git add arcium/browser/profile_data.h arcium/browser/profile_data.cc arcium/ui/browser/profile_actions.h arcium/ui/browser/profile_actions.cc arcium/test/browser/profile_lifecycle_browsertest.cc
git commit -F - <<'MSG'
Change, clear and delete a profile across every window

A space that changes profile takes its open tabs with it wherever they
are; clearing one profile logs it out of every site and leaves the others
signed in; deleting one hands its spaces back to Default and erases what
it held, including the cache Chromium keeps outside the profile's own
folder.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
MSG
```

---

### Task 9: Keeping the data Chrome would take away

Two places where Chromium, left alone, loses an Arcium profile's logins. After an extension is uninstalled Chrome sweeps away every storage directory it does not recognise, which is all of them; and it never restores session cookies in a partition other than the default one, so any site that keeps a login in a session cookie would be logged out of every profile at each relaunch. That second one is A3.1.

**Files:**
- Modify: `arcium/browser/profile_data.h`, `arcium/browser/profile_data.cc`, `arcium/test/profile_data_unittest.cc`, `arcium/test/browser/profile_restore_browsertest.cc`
- Create: `patches/0188-storage-cleanup-keeps-profiles.patch`, `patches/0190-session-cookies-profiles.patch`
- Modify: `patches/README.md`
- Modify (in the Chromium checkout, recorded by the patches): `chrome/browser/web_applications/commands/garbage_collect_storage_partitions_command.cc`, `chrome/browser/net/profile_network_context_service.cc`

**Interfaces:**
- Consumes (Task 2): `PartitionDirectory`, `IsArciumPartitionPath`, `ArciumProfileState::model_load_succeeded()`.
- Produces, in `arcium/browser/profile_data.h`: `std::optional<std::unordered_set<base::FilePath>> PartitionPathsToKeep(content::BrowserContext*)`.

- [ ] **Step 1: Write the failing tests**

Replace `arcium/test/profile_data_unittest.cc`:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/browser/profile_data.h"

#include "arcium/browser/arcium_profile_state.h"
#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/arcium_profile.h"
#include "arcium/browser/profile_partition.h"
#include "base/files/file_util.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/test/browser_task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace arcium {
namespace {

class ProfileDataTest : public testing::Test {
 protected:
  content::BrowserTaskEnvironment task_environment_;
  TestingProfile profile_;
};

// The sweep deletes every directory the keep list does not name, so an
// answer given before the model has been read would erase every profile.
TEST_F(ProfileDataTest, NothingIsKeptUntilTheModelHasBeenRead) {
  ArciumProfileState::GetForBrowserContext(&profile_);
  EXPECT_FALSE(PartitionPathsToKeep(&profile_).has_value());
  task_environment_.RunUntilIdle();
  EXPECT_TRUE(PartitionPathsToKeep(&profile_).has_value());
}

TEST_F(ProfileDataTest, NothingIsKeptWhenTheModelCouldNotBeRead) {
  ASSERT_TRUE(base::WriteFile(
      ArciumProfileState::ModelPath(profile_.GetPath()), "{ not json"));
  ArciumProfileState::GetForBrowserContext(&profile_);
  task_environment_.RunUntilIdle();
  EXPECT_FALSE(PartitionPathsToKeep(&profile_).has_value());
}

TEST_F(ProfileDataTest, EveryProfilesDirectoryIsKept) {
  ArciumProfileState* state =
      ArciumProfileState::GetForBrowserContext(&profile_);
  task_environment_.RunUntilIdle();
  const ProfileId work = state->model()->AddProfile(u"Work", 1);
  const ProfileId home = state->model()->AddProfile(u"Home", 2);

  const std::optional<std::unordered_set<base::FilePath>> paths =
      PartitionPathsToKeep(&profile_);
  ASSERT_TRUE(paths);
  EXPECT_EQ(2u, paths->size());
  EXPECT_TRUE(paths->contains(PartitionDirectory(profile_.GetPath(), work)));
  EXPECT_TRUE(paths->contains(PartitionDirectory(profile_.GetPath(), home)));
  // Default is Chromium's own partition and is never swept.
  EXPECT_FALSE(paths->contains(base::FilePath()));
}

// Nothing may be built to answer this: the cleanup runs at startup, and
// building every profile's storage there is what R3.9 exists to avoid.
TEST_F(ProfileDataTest, AskingWhatToKeepBuildsNothing) {
  ArciumProfileState* state =
      ArciumProfileState::GetForBrowserContext(&profile_);
  task_environment_.RunUntilIdle();
  const ProfileId work = state->model()->AddProfile(u"Work", 1);
  EXPECT_TRUE(PartitionPathsToKeep(&profile_).has_value());
  EXPECT_FALSE(IsPartitionLoaded(&profile_, work));
}

}  // namespace
}  // namespace arcium
```

Append to `arcium/test/browser/profile_restore_browsertest.cc`:

```cpp
// A login kept in a session cookie: Chromium restores those only for the
// default partition unless it is told otherwise, and A3.1 says both
// accounts are still signed in after a relaunch.
IN_PROC_BROWSER_TEST_F(ProfileRestoreTest, PRE_ASessionCookieSurvivesARestart) {
  RestoreSessionAtNextLaunch();
  ProfileId work_profile;
  AddSpaceOnNewProfile(u"Work", &work_profile);
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), PageUrl("a.test", "one"),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  SetCookie(active(), "work", /*session_only=*/true);
  FlushSessionAndModel();
}

IN_PROC_BROWSER_TEST_F(ProfileRestoreTest, ASessionCookieSurvivesARestart) {
  ASSERT_EQ(2u, model()->spaces().size());
  const SpaceId work = model()->spaces()[1].id;
  content::WebContents* work_tab = nullptr;
  for (int i = 0; i < strip()->count(); ++i) {
    if (switcher()->SpaceOfTabAt(i) == work) {
      work_tab = strip()->GetWebContentsAt(i);
    }
  }
  ASSERT_TRUE(work_tab);
  content::TestNavigationObserver observer(work_tab);
  strip()->ActivateTabAt(strip()->GetIndexOfWebContents(work_tab));
  observer.Wait();
  EXPECT_EQ("who=work", ReadCookie(work_tab));
}
```

- [ ] **Step 2: Implement the keep list**

In `arcium/browser/profile_data.h`, add `#include <optional>`, `#include <unordered_set>`, `#include "base/files/file_path.h"` and:

```cpp
// The partition directories Chrome's own cleanup must keep: one per
// non-default profile in the model. Nullopt means "do not sweep at all this
// launch", which is the answer until the model file has been read
// successfully -- a sweep with an incomplete list erases a profile's logins
// for good, while a sweep skipped costs a directory that lingers until the
// next launch.
//
// The paths are built from the profile ids. Nothing here asks for a
// partition object: that would build every profile's storage at startup.
std::optional<std::unordered_set<base::FilePath>> PartitionPathsToKeep(
    content::BrowserContext* context);
```

In `arcium/browser/profile_data.cc`:

```cpp
std::optional<std::unordered_set<base::FilePath>> PartitionPathsToKeep(
    content::BrowserContext* context) {
  ArciumProfileState* state =
      ArciumProfileState::GetForBrowserContextIfExists(context);
  if (!state || !state->model_load_succeeded()) {
    return std::nullopt;
  }
  std::unordered_set<base::FilePath> paths;
  for (const ArciumProfile& profile : state->model()->profiles()) {
    const base::FilePath path =
        PartitionDirectory(context->GetPath(), profile.id);
    if (!path.empty()) {
      paths.insert(path);
    }
  }
  return paths;
}
```

with `#include "arcium/browser/arcium_profile_state.h"`, `#include "arcium/browser/model/arcium_model.h"` and `#include "arcium/browser/model/arcium_profile.h"`.

- [ ] **Step 3: The cleanup hook**

In `/Volumes/Texternal/chromium/src/chrome/browser/web_applications/commands/garbage_collect_storage_partitions_command.cc`, add `#include "arcium/browser/profile_data.h"` as the first quoted include and, at the top of `DoGarbageCollection` before the install gate is registered:

```cpp
  // Arcium: an Arcium profile's partition is on no list here, and the sweep
  // deletes what no list names, so without the model there is nothing to
  // keep it. Skipping costs a directory that lingers one more launch;
  // sweeping without the model costs every profile's logins
  // (arcium/browser/profile_data.h).
  std::optional<std::unordered_set<base::FilePath>> arcium_paths =
      arcium::PartitionPathsToKeep(profile_);
  if (!arcium_paths) {
    profile_->GetPrefs()->SetBoolean(
        prefs::kShouldGarbageCollectStoragePartitions, true);
    CompleteAndSelfDestruct(CommandResult::kSuccess);
    return;
  }
```

and, after the two existing `allowlist.merge(...)` calls:

```cpp
  allowlist.merge(*arcium_paths);
```

This patch carries a condition, which no other hook does: the pref has already been reset to false by the time this runs (`ResetStorageGarbageCollectPref`), so "leave it for the next launch" can only be said here, and `prefs::kShouldGarbageCollectStoragePartitions` is a `//chrome/common` name that `//arcium/browser` may not have. Say exactly that in the patch header.

```bash
git -C /Volumes/Texternal/chromium/src diff -- chrome/browser/web_applications/commands/garbage_collect_storage_partitions_command.cc > /private/tmp/0188.diff
```

```
Seam: GarbageCollectStoragePartitionsCommand::DoGarbageCollection in
      chrome/browser/web_applications/commands/garbage_collect_storage_partitions_command.cc,
      where the list of storage directories to keep is built.
Why: after an extension or an isolated web app is uninstalled, Chrome
     deletes every storage directory that is not on that list and not
     currently open. Arcium profiles are on no list, and R3.9 means most of
     them are not open at startup, so one uninstall would erase every
     profile's logins. The hook adds their directories, or -- when the model
     file has not been read yet, so the list cannot be complete -- puts the
     sweep off to a later launch.
     The two statements in the skip branch are the exception to "a patch
     carries no logic": the command has already reset the pref by the time
     it runs, and prefs::kShouldGarbageCollectStoragePartitions is a
     //chrome/common name that //arcium/browser must not depend on.
Delegates to: arcium::PartitionPathsToKeep
```

- [ ] **Step 4: The session-cookie hook**

In `/Volumes/Texternal/chromium/src/chrome/browser/net/profile_network_context_service.cc`, add `#include "arcium/browser/profile_partition.h"` as the first quoted include and widen the condition:

```cpp
    // Arcium: an Arcium profile's partition keeps session cookies exactly as
    // the default partition does, or every profile would be logged out of
    // any site that keeps its login in one (arcium/browser/profile_partition.h).
    if (relative_partition_path.empty() ||
        arcium::IsArciumPartitionPath(relative_partition_path)) {
      network_context_params->restore_old_session_cookies =
          profile_->ShouldRestoreOldSessionCookies();
      network_context_params->persist_session_cookies =
          profile_->ShouldPersistSessionCookies();
    } else {
```

```bash
git -C /Volumes/Texternal/chromium/src diff -- chrome/browser/net/profile_network_context_service.cc > /private/tmp/0190.diff
```

```
Seam: ProfileNetworkContextService::ConfigureNetworkContextParamsInternal in
      chrome/browser/net/profile_network_context_service.cc, at the
      main-partition test that decides session cookies.
Why: Chromium keeps session cookies only for the default partition, copying
     what app request contexts used to do. A site that keeps its login in a
     session cookie would therefore log every Arcium profile out at each
     relaunch, which A3.1 forbids. An Arcium partition now follows the
     profile's own setting, exactly as the default partition does.
Delegates to: arcium::IsArciumPartitionPath
```

```bash
scripts/sync
scripts/sync
scripts/format arcium/browser/profile_data.h arcium/browser/profile_data.cc arcium/test/profile_data_unittest.cc arcium/test/browser/profile_restore_browsertest.cc
ARCIUM_JOBS=4 scripts/build dev arcium_unittests
/Volumes/Texternal/chromium/src/out/dev/arcium_unittests --gtest_filter='ProfileDataTest.*'
ARCIUM_JOBS=4 scripts/build dev arcium_browsertests
/Volumes/Texternal/chromium/src/out/dev/arcium_browsertests --gtest_filter='ProfileRestoreTest.*'
```

| Mutation | Test that must fail |
|---|---|
| `PartitionPathsToKeep` returns an empty set instead of nullopt when the model has not been read | `NothingIsKeptUntilTheModelHasBeenRead` |
| `PartitionPathsToKeep` uses `model_load_finished()` | `NothingIsKeptWhenTheModelCouldNotBeRead` |
| the session-cookie condition drops the `IsArciumPartitionPath` half | `ASessionCookieSurvivesARestart` |

- [ ] **Step 5: Record and commit**

In `patches/README.md`, in number order:

```markdown
| `0188-storage-cleanup-keeps-profiles.patch` | `GarbageCollectStoragePartitionsCommand::DoGarbageCollection` in `chrome/browser/web_applications/commands/garbage_collect_storage_partitions_command.cc` | `arcium::PartitionPathsToKeep` |
| `0190-session-cookies-profiles.patch` | `ProfileNetworkContextService::ConfigureNetworkContextParamsInternal` in `chrome/browser/net/profile_network_context_service.cc` | `arcium::IsArciumPartitionPath` |
```

```bash
/Volumes/Texternal/chromium/src/out/dev/arcium_unittests
git add arcium/browser/profile_data.h arcium/browser/profile_data.cc arcium/test/profile_data_unittest.cc arcium/test/browser/profile_restore_browsertest.cc patches/0188-storage-cleanup-keeps-profiles.patch patches/0190-session-cookies-profiles.patch patches/README.md
git commit -F - <<'MSG'
Stop Chrome throwing a profile's logins away

Chrome sweeps away every storage folder it does not recognise after an
extension is uninstalled, and Arcium's profiles are on none of its lists,
so one uninstall would have logged every profile out; when the model has
not been read yet the sweep waits for a later launch rather than guess.
Session cookies now follow the profile's own setting in a profile's
storage, as they always have in the shared one, which is what keeps both
accounts signed in across a quit.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
MSG
```

---

### Task 10: The warning in front of Chrome's Clear browsing data

Chrome's own Clear browsing data reaches one profile's cookies: the shared one. Someone who uses it expecting to be signed out everywhere would be left signed in, and someone who expects only the space they are in to be cleared would be surprised the other way. So the button asks first, in a dialog that says plainly what each answer does, and then Chrome's own removal runs either way.

There is no third answer. Stopping the removal is not offered, because the page's own script shows "Data deleted" as soon as it hears back and has no way to be told nothing happened (`clear_browsing_data_dialog.ts`).

**Files:**
- Replace (skeletons from Task 0): `arcium/ui/browser/clear_data_warning.h`, `arcium/ui/browser/clear_data_warning.cc`, `arcium/test/clear_data_warning_unittest.cc`
- Create: `patches/0192-clear-data-warning.patch`
- Modify: `patches/README.md`
- Modify (in the Chromium checkout, recorded by the patch): `chrome/browser/ui/webui/settings/clear_browsing_data_handler.cc`

**Interfaces:**
- Consumes (Task 8): `ClearArciumProfileData`; (Task 1) `ArciumModel::profiles()`.
- Produces, in `arcium/ui/browser/clear_data_warning.h`: `bool AskWhichProfilesToClear(content::WebContents*, base::OnceClosure resume)`, and for tests `void SetClearDataWarningAnswerForTesting(std::optional<bool> clear_every_profile)`.

- [ ] **Step 1: Write the failing tests**

Replace `arcium/test/clear_data_warning_unittest.cc`:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/clear_data_warning.h"

#include <optional>

#include "arcium/browser/arcium_profile_state.h"
#include "arcium/browser/model/arcium_model.h"
#include "base/functional/bind.h"
#include "base/test/bind.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "content/public/browser/web_contents.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace arcium {
namespace {

class ClearDataWarningTest : public BrowserWithTestWindowTest {
 protected:
  void TearDown() override {
    SetClearDataWarningAnswerForTesting(std::nullopt);
    BrowserWithTestWindowTest::TearDown();
  }

  content::WebContents* settings_page() {
    AddTab(browser(), GURL("chrome://settings/clearBrowserData"));
    return browser()->tab_strip_model()->GetWebContentsAt(0);
  }
};

// Nothing to warn about while Default is the only profile: the removal
// runs unchanged, with no dialog in the way.
TEST_F(ClearDataWarningTest, OneProfileMeansNoWarning) {
  EXPECT_FALSE(
      AskWhichProfilesToClear(settings_page(), base::DoNothing()));
}

TEST_F(ClearDataWarningTest, TheRemovalWaitsForTheAnswerAndThenRuns) {
  ArciumProfileState::GetForBrowserContext(profile())->model()->AddProfile(
      u"Work", 1);
  SetClearDataWarningAnswerForTesting(false);
  bool resumed = false;
  EXPECT_TRUE(AskWhichProfilesToClear(
      settings_page(),
      base::BindLambdaForTesting([&] { resumed = true; })));
  EXPECT_TRUE(resumed);
}

// The answer is spent once: Chrome's handler is called again to do the
// removal, and that call must go through rather than ask a second time.
TEST_F(ClearDataWarningTest, TheSecondCallIsTheRemovalItselfAndIsNotAsked) {
  ArciumProfileState::GetForBrowserContext(profile())->model()->AddProfile(
      u"Work", 1);
  SetClearDataWarningAnswerForTesting(true);
  content::WebContents* page = settings_page();
  bool resumed = false;
  ASSERT_TRUE(AskWhichProfilesToClear(
      page, base::BindLambdaForTesting([&] {
        resumed = true;
        EXPECT_FALSE(AskWhichProfilesToClear(page, base::DoNothing()));
      })));
  EXPECT_TRUE(resumed);
  // And asked again afterwards, because a later clear is a new question.
  EXPECT_TRUE(AskWhichProfilesToClear(page, base::DoNothing()));
}

}  // namespace
}  // namespace arcium
```

And a browser test, appended to `arcium/test/browser/profile_lifecycle_browsertest.cc`:

```cpp
// The point of the warning: "clear everything" really does reach a space's
// own logins, which Chrome's own removal never touches.
IN_PROC_BROWSER_TEST_F(ProfileLifecycleTest, ClearingEveryProfileReachesThemAll) {
  const GURL url = PageUrl("a.test", "one");
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), url, WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  content::WebContents* default_tab = active();
  SetCookie(default_tab, "default");

  ProfileId work_profile;
  AddSpaceOnNewProfile(u"Work", &work_profile);
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), url, WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  content::WebContents* work_tab = active();
  SetCookie(work_tab, "work");

  SetClearDataWarningAnswerForTesting(true);
  content::WebContents* settings =
      ui_test_utils::NavigateToURLWithDisposition(
          browser(), GURL("chrome://settings/clearBrowserData"),
          WindowOpenDisposition::NEW_FOREGROUND_TAB,
          ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP),
      active();
  ASSERT_TRUE(content::ExecJs(
      settings,
      "document.querySelector('settings-ui').shadowRoot"
      ".querySelector('settings-main')"));
  ClearBrowsingDataFromSettings(settings);

  EXPECT_TRUE(base::test::RunUntil([&] {
    return ReadCookie(work_tab).empty() && ReadCookie(default_tab).empty();
  }));
  SetClearDataWarningAnswerForTesting(std::nullopt);
}
```

`ClearBrowsingDataFromSettings` is a helper on the harness from Task 3; if driving the settings page from a test turns out to need more than a `content::ExecJs` of the dialog's confirm button, replace the browser test with a direct call of the handler's message (`content::WebUIMessageHandler` is reachable through `content::WebContents::GetWebUI()->ProcessWebUIMessage`) and say in the report which of the two you used.

- [ ] **Step 2: Run them to see them fail, then write the warning**

Replace `arcium/ui/browser/clear_data_warning.h`:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_BROWSER_CLEAR_DATA_WARNING_H_
#define ARCIUM_UI_BROWSER_CLEAR_DATA_WARNING_H_

#include <optional>

#include "base/functional/callback_forward.h"

namespace content {
class WebContents;
}

namespace arcium {

// Asks, before Chrome's Clear browsing data removes anything, whether the
// removal should reach the spaces that keep their own logins. Chrome's own
// removal covers the shared logins and nothing else, so without this a
// "clear everything" would quietly leave those spaces signed in.
//
// Returns true when the question has been put and `resume` will run once it
// is answered -- the caller must do nothing more until then. Returns false
// when there is nothing to ask: only one profile exists, or this page has
// just been answered and is calling back to do the removal.
//
// Whichever answer is given, the removal goes ahead; there is no way to
// stop it, because the settings page reports a deletion as soon as it hears
// back. "Clear them all" also empties each space's own storage, in the
// background, as the answer is taken.
bool AskWhichProfilesToClear(content::WebContents* settings_page,
                             base::OnceClosure resume);

// Answers the question without a dialog. std::nullopt puts the dialog back.
void SetClearDataWarningAnswerForTesting(
    std::optional<bool> clear_every_profile);

}  // namespace arcium

#endif  // ARCIUM_UI_BROWSER_CLEAR_DATA_WARNING_H_
```

Replace `arcium/ui/browser/clear_data_warning.cc`:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/clear_data_warning.h"

#include <utility>
#include <vector>

#include "arcium/browser/arcium_profile_state.h"
#include "arcium/browser/model/arcium_model.h"
#include "arcium/browser/model/arcium_profile.h"
#include "arcium/ui/browser/profile_actions.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/functional/callback_helpers.h"
#include "base/no_destructor.h"
#include "base/strings/utf_string_conversions.h"
#include "components/constrained_window/constrained_window_views.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_user_data.h"
#include "ui/base/models/dialog_model.h"
#include "ui/views/bubble/bubble_dialog_model_host.h"

namespace arcium {

namespace {

std::optional<bool>& TestAnswer() {
  static base::NoDestructor<std::optional<bool>> answer;
  return *answer;
}

// Marks the settings page that has just been answered, so the removal it
// then asks for is let through instead of asking again.
class AlreadyAsked : public content::WebContentsUserData<AlreadyAsked> {
 public:
  ~AlreadyAsked() override = default;

  // One removal per answer: a later clear on the same page is a new
  // question.
  static bool TakeFrom(content::WebContents* contents) {
    if (!FromWebContents(contents)) {
      return false;
    }
    contents->RemoveUserData(UserDataKey());
    return true;
  }

 private:
  friend class content::WebContentsUserData<AlreadyAsked>;
  explicit AlreadyAsked(content::WebContents* contents)
      : content::WebContentsUserData<AlreadyAsked>(*contents) {}

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

WEB_CONTENTS_USER_DATA_KEY_IMPL(AlreadyAsked);

void Answer(base::WeakPtr<content::WebContents> settings_page,
            base::OnceClosure resume,
            bool clear_every_profile) {
  if (!settings_page) {
    return;
  }
  if (clear_every_profile) {
    content::BrowserContext* context = settings_page->GetBrowserContext();
    ArciumProfileState* state =
        ArciumProfileState::GetForBrowserContextIfExists(context);
    if (state) {
      for (const ArciumProfile& profile : state->model()->profiles()) {
        if (profile.id != DefaultProfileId()) {
          // Chrome's own removal, which is about to run, covers Default.
          ClearArciumProfileData(context, profile.id, base::DoNothing());
        }
      }
    }
  }
  AlreadyAsked::CreateForWebContents(settings_page.get());
  std::move(resume).Run();
}

}  // namespace

bool AskWhichProfilesToClear(content::WebContents* settings_page,
                             base::OnceClosure resume) {
  if (!settings_page || AlreadyAsked::TakeFrom(settings_page)) {
    return false;
  }
  ArciumProfileState* state =
      ArciumProfileState::GetForBrowserContextIfExists(
          settings_page->GetBrowserContext());
  if (!state || state->model()->profiles().size() <= 1) {
    return false;
  }
  if (TestAnswer().has_value()) {
    Answer(settings_page->GetWeakPtr(), std::move(resume), *TestAnswer());
    return true;
  }

  auto split = base::SplitOnceCallback(base::BindOnce(
      &Answer, settings_page->GetWeakPtr(), std::move(resume)));
  std::unique_ptr<ui::DialogModel> dialog =
      ui::DialogModel::Builder()
          .SetTitle(u"Some spaces keep their own logins")
          .AddParagraph(ui::DialogModelLabel(
              u"This can sign you out of every account in every space, "
              u"including the ones that stay signed in separately. There is "
              u"no way to undo it."))
          .AddOkButton(base::BindOnce(std::move(split.first), false),
                       ui::DialogModel::Button::Params().SetLabel(
                           u"Clear shared logins only"))
          .AddCancelButton(base::BindOnce(std::move(split.second), true),
                           ui::DialogModel::Button::Params().SetLabel(
                               u"Clear every space too"))
          .Build();
  constrained_window::ShowWebModal(std::move(dialog), settings_page);
  return true;
}

void SetClearDataWarningAnswerForTesting(
    std::optional<bool> clear_every_profile) {
  TestAnswer() = clear_every_profile;
}

}  // namespace arcium
```

The safe answer is the default button, so "clear shared logins only" is the OK button and the wider one is the other. Check the dialog helpers this tree actually has: `constrained_window::ShowWebModal` may be named `ShowWebModalDialogViews` and may want a `views::BubbleDialogModelHost`; match whatever `arcium/ui/sidebar`'s existing confirmation dialog does (the space-delete confirmation from Stage 3a) and use the same call, so there is one way of putting a question in this codebase rather than two. If the dialog is dismissed without either button -- the tab closes, the page navigates -- neither callback runs and the removal never happens, which is the one safe outcome available.

- [ ] **Step 3: The hook**

In `/Volumes/Texternal/chromium/src/chrome/browser/ui/webui/settings/clear_browsing_data_handler.cc`, add `#include "arcium/ui/browser/clear_data_warning.h"` as the first quoted include and, as the first statement of `ClearBrowsingDataHandler::HandleClearBrowsingData`:

```cpp
  // Arcium: this removal reaches the shared logins only, so a space with
  // its own logins would be left signed in. Ask first, then come back here
  // and remove (arcium/ui/browser/clear_data_warning.h).
  if (arcium::AskWhichProfilesToClear(
          web_ui()->GetWebContents(),
          base::BindOnce(&ClearBrowsingDataHandler::HandleClearBrowsingData,
                         weak_ptr_factory_.GetWeakPtr(), args.Clone()))) {
    return;
  }
```

Check the parameter's name and type in this tree (`const base::Value::List& args`) and the weak factory's name; if the handler has no weak factory, bind through `AllowJavascript`'s existing pattern in the same file rather than adding one.

```bash
git -C /Volumes/Texternal/chromium/src diff -- chrome/browser/ui/webui/settings/clear_browsing_data_handler.cc > /private/tmp/0192.diff
```

```
Seam: ClearBrowsingDataHandler::HandleClearBrowsingData in
      chrome/browser/ui/webui/settings/clear_browsing_data_handler.cc, at
      the start of the removal the settings page asks for.
Why: the removal covers the default storage only, so a space that keeps its
     own logins survives a "clear everything" untouched. The hook puts the
     question first and calls the handler again with the same arguments
     once it is answered; the removal itself is unchanged, because the
     settings page reports a deletion as soon as it hears back and cannot be
     told that nothing happened.
Delegates to: arcium::AskWhichProfilesToClear
```

- [ ] **Step 4: Run, mutate and commit**

```bash
scripts/sync
scripts/sync
scripts/format arcium/ui/browser/clear_data_warning.h arcium/ui/browser/clear_data_warning.cc arcium/test/clear_data_warning_unittest.cc arcium/test/browser/profile_lifecycle_browsertest.cc
ARCIUM_JOBS=4 scripts/build dev arcium_unittests
/Volumes/Texternal/chromium/src/out/dev/arcium_unittests --gtest_filter='ClearDataWarningTest.*'
ARCIUM_JOBS=4 scripts/build dev arcium_browsertests
/Volumes/Texternal/chromium/src/out/dev/arcium_browsertests --gtest_filter='ProfileLifecycleTest.ClearingEveryProfileReachesThemAll'
```

| Mutation | Test that must fail |
|---|---|
| `AskWhichProfilesToClear` returns true with one profile | `OneProfileMeansNoWarning` |
| `Answer` never runs `resume` | `TheRemovalWaitsForTheAnswerAndThenRuns` |
| `AlreadyAsked::TakeFrom` leaves the mark in place | `TheSecondCallIsTheRemovalItselfAndIsNotAsked` (second half) |
| `Answer` skips `CreateForWebContents` | `TheSecondCallIsTheRemovalItselfAndIsNotAsked` (first half: the removal asks again forever) |
| `Answer` skips the per-profile loop | `ClearingEveryProfileReachesThemAll` |

In `patches/README.md`:

```markdown
| `0192-clear-data-warning.patch` | `ClearBrowsingDataHandler::HandleClearBrowsingData` in `chrome/browser/ui/webui/settings/clear_browsing_data_handler.cc` | `arcium::AskWhichProfilesToClear` |
```

```bash
/Volumes/Texternal/chromium/src/out/dev/arcium_unittests
git add arcium/ui/browser/clear_data_warning.h arcium/ui/browser/clear_data_warning.cc arcium/test/clear_data_warning_unittest.cc arcium/test/browser/profile_lifecycle_browsertest.cc patches/0192-clear-data-warning.patch patches/README.md
git commit -F - <<'MSG'
Say what Clear browsing data will and will not reach

Chrome's clear only ever reaches the logins that spaces share, so someone
clearing everything would have been left signed in where it mattered most.
It now asks, in front of the removal, whether the spaces that keep their
own logins should go too, and says plainly that it cannot be undone.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
MSG
```

---

### Task 11: The sidebar learns profiles

The sidebar's model interface gains profiles and a profile on every space;
the fake model, the playground and the real model implement it. The real
model forwards the three operations that touch tabs or storage to the
functions Task 8 built. No menu yet.

**Files:**
- Create: `arcium/ui/sidebar/profile_colors.h`, `arcium/ui/sidebar/profile_colors.cc`,
  `arcium/ui/playground/fake_sidebar_profiles.cc`,
  `arcium/ui/browser/sidebar_tab_model_profiles.cc`,
  `arcium/test/sidebar_profiles_unittest.cc`
- Modify: `arcium/ui/sidebar/sidebar_model.h`,
  `arcium/ui/playground/fake_sidebar_model.h`,
  `arcium/ui/playground/fake_sidebar_spaces.cc`,
  `arcium/ui/browser/sidebar_tab_model.h`,
  `arcium/ui/browser/sidebar_tab_model_spaces.cc`

**Interfaces:**
- Consumes (Task 1): `ArciumModel::profiles()`, `AddProfile`, `RenameProfile`,
  `SetProfileColor`, `ProfileOfSpace`, `AddSpace(name, profile)`,
  `DefaultProfileId()`.
- Consumes (Task 8): `arcium::MoveSpaceToProfile(content::BrowserContext*, SpaceId, ProfileId)`.
- Consumes (Task 8): `arcium::DeleteArciumProfile(content::BrowserContext*, ProfileId)`,
  `arcium::ClearArciumProfileData(content::BrowserContext*, ProfileId, base::OnceClosure)`.
- Produces, in `arcium/ui/sidebar/sidebar_model.h`: `struct SidebarProfile { ProfileId id; std::u16string name; int color; }`,
  `SidebarSpace::profile_id`, and on `SidebarModel`:
  `virtual std::vector<SidebarProfile> profiles() const = 0;`
  `virtual void CreateProfileForSpace(SpaceId space, const std::u16string& name, int color) = 0;`
  `virtual void SetSpaceProfile(SpaceId space, ProfileId profile) = 0;`
  `virtual void RenameProfile(ProfileId id, const std::u16string& name) = 0;`
  `virtual void SetProfileColor(ProfileId id, int color) = 0;`
  `virtual void ClearProfileData(ProfileId id) = 0;`
  `virtual void DeleteProfile(ProfileId id) = 0;`
- Produces, in `arcium/ui/sidebar/profile_colors.h`: `struct ProfileColor { SkColor color; std::u16string_view name; }`,
  `base::span<const ProfileColor> ProfileColors()`, `const ProfileColor& ProfileColorAt(int index)`.
- Produces, on `FakeSidebarModel`: `ProfileId AddProfileForTesting(const std::u16string& name, int color)`,
  `const std::vector<ProfileId>& cleared_profiles_for_testing() const`.

- [ ] **Step 1: Write the failing tests**

Fill `arcium/test/sidebar_profiles_unittest.cc`:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include "arcium/browser/model/arcium_profile.h"
#include "arcium/ui/playground/fake_sidebar_model.h"
#include "arcium/ui/sidebar/profile_colors.h"
#include "arcium/ui/sidebar/sidebar_model.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace arcium {
namespace {

TEST(ProfileColorsTest, EveryPresetHasANameAndAnOutOfRangeIndexDrawsTheFirst) {
  ASSERT_EQ(8u, ProfileColors().size());
  for (const ProfileColor& preset : ProfileColors()) {
    EXPECT_FALSE(preset.name.empty());
  }
  EXPECT_EQ(ProfileColors()[0].color, ProfileColorAt(-1).color);
  EXPECT_EQ(ProfileColors()[0].color, ProfileColorAt(99).color);
  EXPECT_EQ(ProfileColors()[3].color, ProfileColorAt(3).color);
}

TEST(FakeSidebarProfilesTest, StartsWithDefaultAndEverySpaceOnIt) {
  FakeSidebarModel model;
  ASSERT_EQ(1u, model.profiles().size());
  EXPECT_EQ(DefaultProfileId(), model.profiles()[0].id);
  EXPECT_EQ(DefaultProfileId(), model.spaces()[0].profile_id);
}

TEST(FakeSidebarProfilesTest, ANewSpaceStartsOnTheProfileOfTheSpaceYouAreIn) {
  FakeSidebarModel model;
  const ProfileId work = model.AddProfileForTesting(u"Work", 2);
  model.SetSpaceProfile(model.spaces()[0].id, work);

  model.AddSpace(u"New space");

  ASSERT_EQ(2u, model.spaces().size());
  EXPECT_EQ(work, model.spaces()[1].profile_id);
}

TEST(FakeSidebarProfilesTest, CreatingAProfileForASpacePutsTheSpaceOnIt) {
  FakeSidebarModel model;
  const SpaceId space = model.spaces()[0].id;
  model.CreateProfileForSpace(space, u"Work", 4);

  ASSERT_EQ(2u, model.profiles().size());
  EXPECT_EQ(u"Work", model.profiles()[1].name);
  EXPECT_EQ(4, model.profiles()[1].color);
  EXPECT_EQ(model.profiles()[1].id, model.spaces()[0].profile_id);
}

TEST(FakeSidebarProfilesTest, DeletingAProfileMovesItsSpacesToDefault) {
  FakeSidebarModel model;
  const ProfileId work = model.AddProfileForTesting(u"Work", 1);
  model.SetSpaceProfile(model.spaces()[0].id, work);

  model.DeleteProfile(work);

  ASSERT_EQ(1u, model.profiles().size());
  EXPECT_EQ(DefaultProfileId(), model.spaces()[0].profile_id);
}

TEST(FakeSidebarProfilesTest, DefaultCannotBeDeleted) {
  FakeSidebarModel model;
  model.DeleteProfile(DefaultProfileId());
  EXPECT_EQ(1u, model.profiles().size());
}

TEST(FakeSidebarProfilesTest, ClearingAProfileIsRecordedAndChangesNothingElse) {
  FakeSidebarModel model;
  const ProfileId work = model.AddProfileForTesting(u"Work", 1);
  model.ClearProfileData(work);
  ASSERT_EQ(1u, model.cleared_profiles_for_testing().size());
  EXPECT_EQ(work, model.cleared_profiles_for_testing()[0]);
  EXPECT_EQ(2u, model.profiles().size());
}

}  // namespace
}  // namespace arcium
```

Run: `ARCIUM_JOBS=4 scripts/build dev arcium_unittests`
Expected: compile errors — `profile_colors.h`, `profiles()`, `AddProfileForTesting` and `SidebarSpace::profile_id` do not exist.

- [ ] **Step 2: The palette**

`arcium/ui/sidebar/profile_colors.h`:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_PROFILE_COLORS_H_
#define ARCIUM_UI_SIDEBAR_PROFILE_COLORS_H_

#include <string_view>

#include "base/containers/span.h"
#include "third_party/skia/include/core/SkColor.h"

namespace arcium {

// One colour a profile can choose: what its badge is filled with, and the
// name the colour menu lists it under. One value for both colour modes: the
// badge is a small solid disc, and every preset is a mid tone that reads on
// the light and the dark sidebar alike.
struct ProfileColor {
  SkColor color;
  std::u16string_view name;
};

// Every preset, in the order the colour menu lists them. A profile stores
// the index, never the colour, as a space stores its gradient
// (space_gradients.h). Preset 0 is the accent the badge drew before profiles
// existed, so Default looks as it always did.
base::span<const ProfileColor> ProfileColors();

// The preset `index` names, or preset 0 for an index outside the table: a
// file written by a later build with a longer palette still draws.
const ProfileColor& ProfileColorAt(int index);

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_PROFILE_COLORS_H_
```

`arcium/ui/sidebar/profile_colors.cc`:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/profile_colors.h"

namespace arcium {

namespace {

constexpr ProfileColor kColors[] = {
    {.color = SkColorSetRGB(0x8A, 0x8A, 0xFF), .name = u"Lavender"},
    {.color = SkColorSetRGB(0x4C, 0x8D, 0xF6), .name = u"Blue"},
    {.color = SkColorSetRGB(0x2F, 0xB5, 0x8A), .name = u"Green"},
    {.color = SkColorSetRGB(0xE8, 0xA3, 0x3D), .name = u"Amber"},
    {.color = SkColorSetRGB(0xE5, 0x5B, 0x5B), .name = u"Red"},
    {.color = SkColorSetRGB(0xD9, 0x6C, 0xC4), .name = u"Pink"},
    {.color = SkColorSetRGB(0x2C, 0xAE, 0xC4), .name = u"Teal"},
    {.color = SkColorSetRGB(0x8C, 0x93, 0x9E), .name = u"Grey"},
};

}  // namespace

base::span<const ProfileColor> ProfileColors() {
  return kColors;
}

const ProfileColor& ProfileColorAt(int index) {
  const base::span<const ProfileColor> colors = ProfileColors();
  if (index < 0 || static_cast<size_t>(index) >= colors.size()) {
    return colors[0];
  }
  return colors[static_cast<size_t>(index)];
}

}  // namespace arcium
```

- [ ] **Step 3: The interface**

In `arcium/ui/sidebar/sidebar_model.h`, add
`#include "arcium/browser/model/arcium_profile.h"`; add before `struct SidebarSpace`:

```cpp
// One profile as the space menu lists it. Prepared by the model, like
// SidebarSpace.
struct SidebarProfile {
  ProfileId id;
  std::u16string name;
  // An index into profile_colors.h's palette.
  int color = 0;
};
```

In `struct SidebarSpace`, after `int gradient = 0;`:

```cpp
  // Whose logins this space's tabs use. The badge draws the active space's.
  ProfileId profile_id = DefaultProfileId();
```

After the Space commands block (after `MoveEntryToSpace`):

```cpp
  // Profile commands. A profile belongs to the whole browser, so every one
  // of these reaches every window, not only this one.
  //
  // Every profile, Default first.
  virtual std::vector<SidebarProfile> profiles() const = 0;
  // "New profile…": makes a profile and puts `space` on it.
  virtual void CreateProfileForSpace(SpaceId space,
                                     const std::u16string& name,
                                     int color) = 0;
  // Puts `space` on another profile. Its open tabs reopen there, logged in
  // as that profile; that is the model's rule, not the menu's.
  virtual void SetSpaceProfile(SpaceId space, ProfileId profile) = 0;
  virtual void RenameProfile(ProfileId id, const std::u16string& name) = 0;
  // An index into the palette, not a colour, as for a space's gradient.
  virtual void SetProfileColor(ProfileId id, int color) = 0;
  // Logs every site in the profile out: its cookies, site data and cache go.
  // Open tabs are not reloaded, as Chrome's own clear does not reload them.
  virtual void ClearProfileData(ProfileId id) = 0;
  // Erases the profile. Its spaces move to Default and their tabs reopen
  // there. Default itself is refused.
  virtual void DeleteProfile(ProfileId id) = 0;
```

- [ ] **Step 4: The fake**

In `arcium/ui/playground/fake_sidebar_model.h`, beside `AddSpaceForTesting`:

```cpp
  // Seeds a profile without assigning it to any space.
  ProfileId AddProfileForTesting(const std::u16string& name, int color);
  // Every profile ClearProfileData was asked to clear, in order.
  const std::vector<ProfileId>& cleared_profiles_for_testing() const {
    return cleared_profiles_;
  }
```

beside the space overrides:

```cpp
  std::vector<SidebarProfile> profiles() const override;
  void CreateProfileForSpace(SpaceId space,
                             const std::u16string& name,
                             int color) override;
  void SetSpaceProfile(SpaceId space, ProfileId profile) override;
  void RenameProfile(ProfileId id, const std::u16string& name) override;
  void SetProfileColor(ProfileId id, int color) override;
  void ClearProfileData(ProfileId id) override;
  void DeleteProfile(ProfileId id) override;
```

beside `SidebarSpace* FindSpace(SpaceId id);`:

```cpp
  SidebarProfile* FindProfile(ProfileId id);
```

and beside the `spaces_` member:

```cpp
  // Default first, as the real model keeps it.
  std::vector<SidebarProfile> profiles_ = {
      {.id = DefaultProfileId(), .name = u"Default", .color = 0}};
  std::vector<ProfileId> cleared_profiles_;
```

Create `arcium/ui/playground/fake_sidebar_profiles.cc`:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The fake's half of profiles: an in-memory stand-in for ArciumModel's
// profiles and the operations that reopen tabs and clear storage, kept simple
// enough for a view test. There are no tabs to reopen and no storage to
// clear, so those parts are the model changes alone.

#include <algorithm>
#include <utility>

#include "arcium/ui/playground/fake_sidebar_model.h"

namespace arcium {

SidebarProfile* FakeSidebarModel::FindProfile(ProfileId id) {
  for (SidebarProfile& profile : profiles_) {
    if (profile.id == id) {
      return &profile;
    }
  }
  return nullptr;
}

ProfileId FakeSidebarModel::AddProfileForTesting(const std::u16string& name,
                                                 int color) {
  SidebarProfile profile;
  profile.id = ProfileId::Generate();
  profile.name = name;
  profile.color = color;
  profiles_.push_back(profile);
  Notify();
  return profile.id;
}

std::vector<SidebarProfile> FakeSidebarModel::profiles() const {
  return profiles_;
}

void FakeSidebarModel::CreateProfileForSpace(SpaceId space,
                                             const std::u16string& name,
                                             int color) {
  if (!FindSpace(space)) {
    return;
  }
  const ProfileId id = AddProfileForTesting(name, color);
  SetSpaceProfile(space, id);
}

void FakeSidebarModel::SetSpaceProfile(SpaceId space_id, ProfileId profile) {
  SidebarSpace* space = FindSpace(space_id);
  if (!space || !FindProfile(profile) || space->profile_id == profile) {
    return;
  }
  space->profile_id = profile;
  Notify();
}

void FakeSidebarModel::RenameProfile(ProfileId id,
                                     const std::u16string& name) {
  if (SidebarProfile* profile = FindProfile(id)) {
    profile->name = name;
    Notify();
  }
}

void FakeSidebarModel::SetProfileColor(ProfileId id, int color) {
  if (SidebarProfile* profile = FindProfile(id)) {
    profile->color = color;
    Notify();
  }
}

void FakeSidebarModel::ClearProfileData(ProfileId id) {
  if (FindProfile(id)) {
    cleared_profiles_.push_back(id);
  }
}

void FakeSidebarModel::DeleteProfile(ProfileId id) {
  if (id == DefaultProfileId() || !FindProfile(id)) {
    return;
  }
  std::erase_if(profiles_,
                [id](const SidebarProfile& p) { return p.id == id; });
  for (SidebarSpace& space : spaces_) {
    if (space.profile_id == id) {
      space.profile_id = DefaultProfileId();
    }
  }
  Notify();
}

}  // namespace arcium
```

In `arcium/ui/playground/fake_sidebar_spaces.cc`, make `AddSpace` follow the
real rule (decision 5):

```cpp
void FakeSidebarModel::AddSpace(const std::u16string& name) {
  SidebarSpace space;
  space.id = SpaceId::Generate();
  space.name = name;
  // A new space starts on the profile of the space you are in, as Zen
  // creates a workspace in the selected tab's container.
  if (const SidebarSpace* current = FindSpace(ActiveSpaceId())) {
    space.profile_id = current->profile_id;
  }
  const SpaceId id = space.id;
  spaces_.push_back(std::move(space));
  // A new space is one you are put into, not just a dot that appears.
  MarkActiveSpace(id);
  Notify();
}
```

- [ ] **Step 5: The real model**

In `arcium/ui/browser/sidebar_tab_model.h`, beside the space overrides, add
the seven profile overrides with the same signatures as Step 4's fake.

In `arcium/ui/browser/sidebar_tab_model_spaces.cc`, in `spaces()` after
`out.gradient = space.gradient;`:

```cpp
    out.profile_id = arcium_model_->ProfileOfSpace(space.id);
```

and make `AddSpace` start the space on the current space's profile:

```cpp
void SidebarTabModel::AddSpace(const std::u16string& name) {
  // On the profile of the space you are in, as Zen creates a workspace in
  // the selected tab's container. Changing it before the space has tabs
  // costs nothing.
  const SpaceId id = arcium_model_->AddSpace(
      name, arcium_model_->ProfileOfSpace(active_space()));
  // A new space is one you are put into: it is empty, so the switch opens
  // its blank tab and the quick entry over it, which is where a new space
  // starts from.
  if (switcher_) {
    switcher_->SwitchTo(id);
  }
}
```

Create `arcium/ui/browser/sidebar_tab_model_profiles.cc`:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The space menu's profile commands. Naming and colouring are the model's
// alone; moving a space, clearing and deleting reach tabs and storage in
// every window, so they go to profile_actions.h, which owns that walk.

#include <utility>

#include "arcium/browser/model/arcium_profile.h"
#include "arcium/ui/browser/profile_actions.h"
#include "arcium/ui/browser/sidebar_tab_model.h"
#include "base/functional/callback_helpers.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"

namespace arcium {

std::vector<SidebarProfile> SidebarTabModel::profiles() const {
  std::vector<SidebarProfile> result;
  for (const ArciumProfile& profile : arcium_model_->profiles()) {
    result.push_back(
        {.id = profile.id, .name = profile.name, .color = profile.color});
  }
  return result;
}

void SidebarTabModel::CreateProfileForSpace(SpaceId space,
                                            const std::u16string& name,
                                            int color) {
  if (!arcium_model_->GetSpace(space)) {
    return;
  }
  SetSpaceProfile(space, arcium_model_->AddProfile(name, color));
}

void SidebarTabModel::SetSpaceProfile(SpaceId space, ProfileId profile) {
  MoveSpaceToProfile(tab_strip_model_->profile(), space, profile);
}

void SidebarTabModel::RenameProfile(ProfileId id, const std::u16string& name) {
  arcium_model_->RenameProfile(id, name);
}

void SidebarTabModel::SetProfileColor(ProfileId id, int color) {
  arcium_model_->SetProfileColor(id, color);
}

void SidebarTabModel::ClearProfileData(ProfileId id) {
  ClearArciumProfileData(tab_strip_model_->profile(), id, base::DoNothing());
}

void SidebarTabModel::DeleteProfile(ProfileId id) {
  DeleteArciumProfile(tab_strip_model_->profile(), id);
}

}  // namespace arcium
```

(`tab_strip_model_` is the member `SidebarTabModel` already holds; confirm its
name in `sidebar_tab_model.h` and use it.)

- [ ] **Step 6: Run the tests, then mutation checks**

```bash
scripts/format arcium/ui/sidebar/profile_colors.* arcium/ui/sidebar/sidebar_model.h arcium/ui/playground/fake_sidebar_model.h arcium/ui/playground/fake_sidebar_profiles.cc arcium/ui/playground/fake_sidebar_spaces.cc arcium/ui/browser/sidebar_tab_model.h arcium/ui/browser/sidebar_tab_model_spaces.cc arcium/ui/browser/sidebar_tab_model_profiles.cc arcium/test/sidebar_profiles_unittest.cc
ARCIUM_JOBS=4 scripts/build dev arcium_unittests
/Volumes/Texternal/chromium/src/out/dev/arcium_unittests --gtest_filter='ProfileColorsTest.*:FakeSidebarProfilesTest.*:SpaceBarTest.*'
```

| Mutation | Test that must fail |
|---|---|
| `ProfileColorAt` drops the range check's upper bound | `EveryPresetHasANameAndAnOutOfRangeIndexDrawsTheFirst` (under ASan/checked span it crashes; either counts) |
| fake `AddSpace` leaves `profile_id` at Default | `ANewSpaceStartsOnTheProfileOfTheSpaceYouAreIn` |
| fake `DeleteProfile` skips the spaces loop | `DeletingAProfileMovesItsSpacesToDefault` |

Then the full suite, and commit:

```bash
/Volumes/Texternal/chromium/src/out/dev/arcium_unittests
git add arcium/ui/sidebar/profile_colors.h arcium/ui/sidebar/profile_colors.cc arcium/ui/sidebar/sidebar_model.h arcium/ui/playground/fake_sidebar_model.h arcium/ui/playground/fake_sidebar_profiles.cc arcium/ui/playground/fake_sidebar_spaces.cc arcium/ui/browser/sidebar_tab_model.h arcium/ui/browser/sidebar_tab_model_spaces.cc arcium/ui/browser/sidebar_tab_model_profiles.cc arcium/test/sidebar_profiles_unittest.cc
git commit -F - <<'MSG'
The sidebar can list profiles and put a space on one

What a space's profile is, and what the menu will be able to do with it,
now reaches the views through the same interface the space bar is drawn
from; a new space starts on the profile of the space you were in, as Zen
does.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
MSG
```

---

### Task 12: The Profile menu and the badge

Right-click a space: a Profile submenu with every profile ticked for the
space's own, "New profile…", "Rename profile…", "Change colour", "Clear this
profile's data…" and "Delete profile…". The badge in the space bar becomes
the active space's profile colour with its name as the tooltip.

**Files:**
- Create: `arcium/ui/sidebar/profile_menu.h`, `arcium/ui/sidebar/profile_menu.cc`,
  `arcium/test/profile_menu_unittest.cc`
- Modify: `arcium/ui/sidebar/space_bar_view.h`, `arcium/ui/sidebar/space_bar_view.cc`,
  `arcium/ui/playground/sidebar_example.cc`

**Interfaces:**
- Consumes (Task 11): the `SidebarModel` profile commands, `SidebarProfile`,
  `SidebarSpace::profile_id`, `ProfileColors()`, `ProfileColorAt(int)`.
- Produces: `class ProfileMenu` with `ui::SimpleMenuModel* model()`,
  `void SetSpace(SpaceId space)` (rebuilds the items for that space),
  command ids `kProfileFirst = 200`, `kColorFirst = 100`, `kNewProfile = 1`,
  `kRenameProfile`, `kChangeColor`, `kClearData`, `kDeleteProfile`, and the
  testing seams `pending_text_for_testing()`, `AnswerForTesting(bool)`,
  `SubmitNewProfileForTesting(name, color)`, `SubmitRenameForTesting(name)`.
- Produces, on `SpaceBarView`: `kProfile` menu command (the submenu) and
  `profile_menu_for_testing()`; `views::View* profile_badge_for_testing()`.

- [ ] **Step 1: Write the failing tests**

Fill `arcium/test/profile_menu_unittest.cc`:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/profile_menu.h"

#include <string>

#include "arcium/browser/model/arcium_profile.h"
#include "arcium/test/test_app_activation.h"
#include "arcium/ui/playground/fake_sidebar_model.h"
#include "arcium/ui/sidebar/profile_colors.h"
#include "arcium/ui/sidebar/space_bar_view.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/test/views_test_base.h"

namespace arcium {
namespace {

class ProfileMenuTest : public views::ViewsTestBase {
 public:
  void SetUp() override {
    arcium::test::SuppressTestAppActivation();
    views::ViewsTestBase::SetUp();
  }
};

bool HasLabel(ui::MenuModel* model, const std::u16string& label) {
  for (size_t i = 0; i < model->GetItemCount(); ++i) {
    if (model->GetLabelAt(i) == label) {
      return true;
    }
  }
  return false;
}

TEST_F(ProfileMenuTest, ListsEveryProfileWithATickOnTheSpacesOwn) {
  FakeSidebarModel model;
  const ProfileId work = model.AddProfileForTesting(u"Work", 1);
  model.SetSpaceProfile(model.spaces()[0].id, work);
  ProfileMenu menu(&model, /*anchor=*/nullptr);
  menu.SetSpace(model.spaces()[0].id);

  EXPECT_TRUE(HasLabel(menu.model(), u"Default"));
  EXPECT_TRUE(HasLabel(menu.model(), u"Work"));
  EXPECT_FALSE(menu.IsCommandIdChecked(ProfileMenu::kProfileFirst + 0));
  EXPECT_TRUE(menu.IsCommandIdChecked(ProfileMenu::kProfileFirst + 1));
  EXPECT_TRUE(HasLabel(menu.model(), u"New profile…"));
  EXPECT_TRUE(HasLabel(menu.model(), u"Clear this profile's data…"));
  EXPECT_TRUE(HasLabel(menu.model(), u"Delete profile…"));
}

TEST_F(ProfileMenuTest, DefaultOffersNoDelete) {
  FakeSidebarModel model;
  ProfileMenu menu(&model, nullptr);
  menu.SetSpace(model.spaces()[0].id);
  EXPECT_FALSE(HasLabel(menu.model(), u"Delete profile…"));
}

TEST_F(ProfileMenuTest, ChoosingAProfileMovesTheSpace) {
  FakeSidebarModel model;
  const ProfileId work = model.AddProfileForTesting(u"Work", 1);
  ProfileMenu menu(&model, nullptr);
  menu.SetSpace(model.spaces()[0].id);
  menu.ExecuteCommand(ProfileMenu::kProfileFirst + 1, 0);
  EXPECT_EQ(work, model.spaces()[0].profile_id);
}

TEST_F(ProfileMenuTest, NewProfileCreatesItOnThisSpace) {
  FakeSidebarModel model;
  ProfileMenu menu(&model, nullptr);
  menu.SetSpace(model.spaces()[0].id);
  menu.ExecuteCommand(ProfileMenu::kNewProfile, 0);
  menu.SubmitNewProfileForTesting(u"Work", 3);
  ASSERT_EQ(2u, model.profiles().size());
  EXPECT_EQ(u"Work", model.profiles()[1].name);
  EXPECT_EQ(3, model.profiles()[1].color);
  EXPECT_EQ(model.profiles()[1].id, model.spaces()[0].profile_id);
}

TEST_F(ProfileMenuTest, AnEmptyNameMakesNoProfile) {
  FakeSidebarModel model;
  ProfileMenu menu(&model, nullptr);
  menu.SetSpace(model.spaces()[0].id);
  menu.ExecuteCommand(ProfileMenu::kNewProfile, 0);
  menu.SubmitNewProfileForTesting(u"", 3);
  EXPECT_EQ(1u, model.profiles().size());
}

TEST_F(ProfileMenuTest, RenameAndRecolourActOnTheSpacesProfile) {
  FakeSidebarModel model;
  const ProfileId work = model.AddProfileForTesting(u"Work", 1);
  model.SetSpaceProfile(model.spaces()[0].id, work);
  ProfileMenu menu(&model, nullptr);
  menu.SetSpace(model.spaces()[0].id);

  menu.ExecuteCommand(ProfileMenu::kRenameProfile, 0);
  menu.SubmitRenameForTesting(u"Job");
  menu.ExecuteCommand(ProfileMenu::kColorFirst + 5, 0);

  EXPECT_EQ(u"Job", model.profiles()[1].name);
  EXPECT_EQ(5, model.profiles()[1].color);
  EXPECT_TRUE(menu.IsCommandIdChecked(ProfileMenu::kColorFirst + 5));
}

TEST_F(ProfileMenuTest, ClearingAsksFirstAndSaysWhatStays) {
  FakeSidebarModel model;
  const ProfileId work = model.AddProfileForTesting(u"Work", 1);
  model.SetSpaceProfile(model.spaces()[0].id, work);
  ProfileMenu menu(&model, nullptr);
  menu.SetSpace(model.spaces()[0].id);

  menu.ExecuteCommand(ProfileMenu::kClearData, 0);
  const std::u16string text = menu.pending_text_for_testing();
  EXPECT_NE(std::u16string::npos, text.find(u"Work"));
  EXPECT_NE(std::u16string::npos, text.find(u"signed out of every site"));
  EXPECT_NE(std::u16string::npos, text.find(u"History, passwords"));
  menu.AnswerForTesting(/*accept=*/false);
  EXPECT_TRUE(model.cleared_profiles_for_testing().empty());

  menu.ExecuteCommand(ProfileMenu::kClearData, 0);
  menu.AnswerForTesting(/*accept=*/true);
  ASSERT_EQ(1u, model.cleared_profiles_for_testing().size());
}

TEST_F(ProfileMenuTest, DeletingCountsSpacesAndTabsAndNamesDefault) {
  FakeSidebarModel model;
  const ProfileId work = model.AddProfileForTesting(u"Work", 1);
  const SpaceId first = model.spaces()[0].id;
  model.SetSpaceProfile(first, work);
  model.AddTabInSpaceForTesting(u"One", "https://w1.example/", first);
  model.AddTabInSpaceForTesting(u"Two", "https://w2.example/", first);
  ProfileMenu menu(&model, nullptr);
  menu.SetSpace(first);

  menu.ExecuteCommand(ProfileMenu::kDeleteProfile, 0);
  const std::u16string text = menu.pending_text_for_testing();
  EXPECT_NE(std::u16string::npos, text.find(u"Work"));
  EXPECT_NE(std::u16string::npos, text.find(u"erased for good"));
  EXPECT_NE(std::u16string::npos, text.find(u"1 space"));
  EXPECT_NE(std::u16string::npos, text.find(u"Default"));
  menu.AnswerForTesting(/*accept=*/true);
  EXPECT_EQ(1u, model.profiles().size());
  EXPECT_EQ(DefaultProfileId(), model.spaces()[0].profile_id);
}

TEST_F(ProfileMenuTest, TheBadgeShowsTheActiveSpacesProfile) {
  FakeSidebarModel model;
  const ProfileId work = model.AddProfileForTesting(u"Work", 2);
  SpaceBarView bar(&model);
  EXPECT_EQ(u"Profile: Default",
            bar.profile_badge_for_testing()->GetRenderedTooltipText(
                gfx::Point()));
  model.SetSpaceProfile(model.spaces()[0].id, work);
  EXPECT_EQ(u"Profile: Work",
            bar.profile_badge_for_testing()->GetRenderedTooltipText(
                gfx::Point()));
}

TEST_F(ProfileMenuTest, TheSpaceMenuCarriesTheProfileSubmenu) {
  FakeSidebarModel model;
  SpaceBarView bar(&model);
  bar.BuildMenuForTesting(model.spaces()[0].id);
  EXPECT_TRUE(bar.IsCommandIdEnabled(SpaceBarView::kProfile));
  ASSERT_TRUE(bar.profile_menu_for_testing());
  EXPECT_TRUE(HasLabel(bar.profile_menu_for_testing()->model(), u"Default"));
}

}  // namespace
}  // namespace arcium
```

Run: `ARCIUM_JOBS=4 scripts/build dev arcium_unittests`
Expected: compile errors — `profile_menu.h` does not exist.

- [ ] **Step 2: The menu**

`arcium/ui/sidebar/profile_menu.h`:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_PROFILE_MENU_H_
#define ARCIUM_UI_SIDEBAR_PROFILE_MENU_H_

#include <memory>
#include <string>

#include "arcium/ui/sidebar/sidebar_model.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "ui/menus/simple_menu_model.h"

namespace views {
class View;
class Widget;
}  // namespace views

namespace arcium {

// The space menu's "Profile" submenu, and the dialogs it opens: a new
// profile's name and colour, a rename, and the confirmations for clearing
// and deleting. Its own file because the space bar is already at its size
// limit; the bar owns one and adds its model as a submenu.
//
// Acts on the space the space menu was opened on, and on that space's
// profile. Rebuilt each time the menu opens, because the list of profiles
// changes and a SimpleMenuModel's items are fixed once built.
class ProfileMenu : public ui::SimpleMenuModel::Delegate {
 public:
  enum Command {
    kNewProfile = 1,
    kRenameProfile,
    kChangeColor,
    kClearData,
    kDeleteProfile,
    // kColorFirst + i chooses preset i of profile_colors.h.
    kColorFirst = 100,
    // kProfileFirst + i puts the space on the i-th profile.
    kProfileFirst = 200,
  };

  // `model` must outlive this. `anchor` is where dialogs point; null means
  // there is nowhere to show one, so they stay pending for a test to answer.
  ProfileMenu(SidebarModel* model, views::View* anchor);
  ProfileMenu(const ProfileMenu&) = delete;
  ProfileMenu& operator=(const ProfileMenu&) = delete;
  ~ProfileMenu() override;

  ui::SimpleMenuModel* model() { return menu_.get(); }
  // Points the menu at `space` and rebuilds its items.
  void SetSpace(SpaceId space);

  // ui::SimpleMenuModel::Delegate:
  bool IsCommandIdChecked(int command_id) const override;
  void ExecuteCommand(int command_id, int event_flags) override;

  // The sentence the pending confirmation shows; empty when none is.
  const std::u16string& pending_text_for_testing() const {
    return pending_text_;
  }
  // Answers the pending confirmation as its buttons would.
  void AnswerForTesting(bool accept);
  // Submits the pending "New profile…" or rename dialog as its OK would.
  void SubmitNewProfileForTesting(const std::u16string& name, int color);
  void SubmitRenameForTesting(const std::u16string& name);

 private:
  enum class Pending { kNone, kNewProfile, kRename, kClear, kDelete };

  // The space the menu acts on, and its profile, if both still exist.
  const SidebarSpace* MenuSpace(const std::vector<SidebarSpace>& spaces) const;
  std::optional<SidebarProfile> MenuProfile() const;

  void AskForNewProfile();
  void AskForRename(const SidebarProfile& profile);
  void Confirm(Pending kind, const std::u16string& title,
               const std::u16string& text, const std::u16string& ok_label);
  void OnAnswer(int serial, bool accept);
  void OnNewProfile(int serial, const std::u16string& name, int color);
  void OnRename(int serial, const std::u16string& name);
  void CloseDialog();

  raw_ptr<SidebarModel> model_;
  raw_ptr<views::View> anchor_;
  SpaceId space_;

  Pending pending_ = Pending::kNone;
  ProfileId pending_profile_;
  std::u16string pending_text_;
  int serial_ = 0;
  base::WeakPtr<views::Widget> dialog_widget_;

  // Declared before `menu_`, which keeps a bare pointer to it.
  std::unique_ptr<ui::SimpleMenuModel> color_menu_;
  std::unique_ptr<ui::SimpleMenuModel> menu_;

  base::WeakPtrFactory<ProfileMenu> weak_factory_{this};
};

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_PROFILE_MENU_H_
```

`arcium/ui/sidebar/profile_menu.cc`:

```cpp
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/profile_menu.h"

#include <optional>
#include <utility>
#include <vector>

#include "arcium/browser/model/arcium_profile.h"
#include "arcium/ui/sidebar/profile_colors.h"
#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "ui/base/models/dialog_model.h"
#include "ui/base/models/simple_combobox_model.h"
#include "ui/views/bubble/bubble_border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/bubble/bubble_dialog_model_host.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace arcium {

namespace {

constexpr int kProfileGroup = 1;
constexpr int kColorGroup = 2;

DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kProfileNameField);
DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kProfileColorField);

std::u16string CountPhrase(int count,
                           const std::u16string& one,
                           const std::u16string& many) {
  return base::StrCat(
      {base::NumberToString16(count), u" ", count == 1 ? one : many});
}

std::optional<int> ColorForCommand(int command_id) {
  const int preset = command_id - ProfileMenu::kColorFirst;
  if (preset < 0 || static_cast<size_t>(preset) >= ProfileColors().size()) {
    return std::nullopt;
  }
  return preset;
}

}  // namespace

ProfileMenu::ProfileMenu(SidebarModel* model, views::View* anchor)
    : model_(model), anchor_(anchor) {
  color_menu_ = std::make_unique<ui::SimpleMenuModel>(this);
  const base::span<const ProfileColor> colors = ProfileColors();
  for (size_t i = 0; i < colors.size(); ++i) {
    color_menu_->AddRadioItem(kColorFirst + static_cast<int>(i),
                              std::u16string(colors[i].name), kColorGroup);
  }
  menu_ = std::make_unique<ui::SimpleMenuModel>(this);
}

ProfileMenu::~ProfileMenu() {
  CloseDialog();
}

void ProfileMenu::SetSpace(SpaceId space) {
  space_ = space;
  menu_->Clear();
  const std::vector<SidebarProfile> profiles = model_->profiles();
  for (size_t i = 0; i < profiles.size(); ++i) {
    menu_->AddRadioItem(kProfileFirst + static_cast<int>(i), profiles[i].name,
                        kProfileGroup);
  }
  menu_->AddSeparator(ui::NORMAL_SEPARATOR);
  menu_->AddItem(kNewProfile, u"New profile…");
  menu_->AddItem(kRenameProfile, u"Rename profile…");
  menu_->AddSubMenu(kChangeColor, u"Change colour", color_menu_.get());
  menu_->AddSeparator(ui::NORMAL_SEPARATOR);
  menu_->AddItem(kClearData, u"Clear this profile's data…");
  const std::optional<SidebarProfile> profile = MenuProfile();
  // Default is the one profile every space can always fall back to.
  if (profile && profile->id != DefaultProfileId()) {
    menu_->AddItem(kDeleteProfile, u"Delete profile…");
  }
}

bool ProfileMenu::IsCommandIdChecked(int command_id) const {
  const std::optional<SidebarProfile> profile = MenuProfile();
  if (!profile) {
    return false;
  }
  if (const std::optional<int> color = ColorForCommand(command_id)) {
    return profile->color == *color;
  }
  const int index = command_id - kProfileFirst;
  const std::vector<SidebarProfile> profiles = model_->profiles();
  return index >= 0 && static_cast<size_t>(index) < profiles.size() &&
         profiles[static_cast<size_t>(index)].id == profile->id;
}

void ProfileMenu::ExecuteCommand(int command_id, int event_flags) {
  const std::optional<SidebarProfile> profile = MenuProfile();
  if (!profile) {
    return;
  }
  if (const std::optional<int> color = ColorForCommand(command_id)) {
    model_->SetProfileColor(profile->id, *color);
    return;
  }
  if (command_id >= kProfileFirst) {
    const std::vector<SidebarProfile> profiles = model_->profiles();
    const size_t index = static_cast<size_t>(command_id - kProfileFirst);
    if (index < profiles.size()) {
      model_->SetSpaceProfile(space_, profiles[index].id);
    }
    return;
  }
  switch (command_id) {
    case kNewProfile:
      AskForNewProfile();
      return;
    case kRenameProfile:
      AskForRename(*profile);
      return;
    case kClearData:
      pending_profile_ = profile->id;
      Confirm(Pending::kClear, u"Clear this profile's data?",
              base::StrCat({u"You'll be signed out of every site in “",
                            profile->name,
                            u"”. History, passwords and bookmarks are "
                            u"shared by every profile and stay."}),
              u"Clear data");
      return;
    case kDeleteProfile: {
      int spaces = 0;
      int tabs = 0;
      for (const SidebarSpace& space : model_->spaces()) {
        if (space.profile_id == profile->id) {
          ++spaces;
          tabs += space.open_tab_count;
        }
      }
      pending_profile_ = profile->id;
      Confirm(Pending::kDelete, u"Delete profile?",
              base::StrCat({u"“", profile->name,
                            u"” and its logins will be erased for good. ",
                            CountPhrase(spaces, u"space", u"spaces"),
                            u" will switch to Default, and ",
                            CountPhrase(tabs, u"open tab", u"open tabs"),
                            u" will reopen signed out."}),
              u"Delete profile");
      return;
    }
    default:
      return;
  }
}

const SidebarSpace* ProfileMenu::MenuSpace(
    const std::vector<SidebarSpace>& spaces) const {
  for (const SidebarSpace& space : spaces) {
    if (space.id == space_) {
      return &space;
    }
  }
  return nullptr;
}

std::optional<SidebarProfile> ProfileMenu::MenuProfile() const {
  const std::vector<SidebarSpace> spaces = model_->spaces();
  const SidebarSpace* space = MenuSpace(spaces);
  if (!space) {
    return std::nullopt;
  }
  for (const SidebarProfile& profile : model_->profiles()) {
    if (profile.id == space->profile_id) {
      return profile;
    }
  }
  return std::nullopt;
}

void ProfileMenu::AskForNewProfile() {
  CloseDialog();
  pending_ = Pending::kNewProfile;
  const int serial = ++serial_;
  if (!anchor_ || !anchor_->GetWidget()) {
    return;
  }
  std::vector<std::u16string> names;
  for (const ProfileColor& color : ProfileColors()) {
    names.emplace_back(color.name);
  }
  // A colour nobody has yet, so two new profiles do not look alike by
  // default.
  const int suggested = static_cast<int>(model_->profiles().size() %
                                         ProfileColors().size());
  auto combobox = std::make_unique<ui::SimpleComboboxModel>(
      std::vector<ui::SimpleComboboxModel::Item>(names.begin(), names.end()));
  base::WeakPtr<ProfileMenu> weak = weak_factory_.GetWeakPtr();
  ui::DialogModel::Builder builder;
  ui::DialogModel* dialog_model = builder.model();
  auto dialog =
      builder.SetTitle(u"New profile")
          .AddParagraph(ui::DialogModelLabel(
              u"A profile has its own logins. History, passwords and "
              u"bookmarks are shared."))
          .AddTextfield(kProfileNameField, u"Name", std::u16string())
          .AddCombobox(kProfileColorField, u"Colour", std::move(combobox),
                       ui::DialogModelCombobox::Params().SetInitialIndex(
                           static_cast<size_t>(suggested)))
          .AddOkButton(
              base::BindOnce(
                  [](base::WeakPtr<ProfileMenu> weak, int serial,
                     ui::DialogModel* model) {
                    if (!weak) {
                      return;
                    }
                    weak->OnNewProfile(
                        serial,
                        model->GetTextfieldByUniqueId(kProfileNameField)
                            ->text(),
                        static_cast<int>(
                            model->GetComboboxByUniqueId(kProfileColorField)
                                ->selected_index()
                                .value_or(0)));
                  },
                  weak, serial, dialog_model),
              ui::DialogModel::Button::Params().SetLabel(u"Create"))
          .AddCancelButton(base::DoNothing())
          .Build();
  auto bubble = std::make_unique<views::BubbleDialogModelHost>(
      std::move(dialog), anchor_, views::BubbleBorder::BOTTOM_LEFT);
  views::Widget* widget = views::BubbleDialogDelegate::CreateBubbleDeprecated(
      std::move(bubble), views::Widget::InitParams::NATIVE_WIDGET_OWNS_WIDGET);
  dialog_widget_ = widget->GetWeakPtr();
  widget->Show();
}

void ProfileMenu::AskForRename(const SidebarProfile& profile) {
  CloseDialog();
  pending_ = Pending::kRename;
  pending_profile_ = profile.id;
  const int serial = ++serial_;
  if (!anchor_ || !anchor_->GetWidget()) {
    return;
  }
  base::WeakPtr<ProfileMenu> weak = weak_factory_.GetWeakPtr();
  ui::DialogModel::Builder builder;
  ui::DialogModel* dialog_model = builder.model();
  auto dialog =
      builder.SetTitle(u"Rename profile")
          .AddTextfield(kProfileNameField, u"Name", profile.name)
          .AddOkButton(
              base::BindOnce(
                  [](base::WeakPtr<ProfileMenu> weak, int serial,
                     ui::DialogModel* model) {
                    if (weak) {
                      weak->OnRename(
                          serial,
                          model->GetTextfieldByUniqueId(kProfileNameField)
                              ->text());
                    }
                  },
                  weak, serial, dialog_model),
              ui::DialogModel::Button::Params().SetLabel(u"Rename"))
          .AddCancelButton(base::DoNothing())
          .Build();
  auto bubble = std::make_unique<views::BubbleDialogModelHost>(
      std::move(dialog), anchor_, views::BubbleBorder::BOTTOM_LEFT);
  views::Widget* widget = views::BubbleDialogDelegate::CreateBubbleDeprecated(
      std::move(bubble), views::Widget::InitParams::NATIVE_WIDGET_OWNS_WIDGET);
  dialog_widget_ = widget->GetWeakPtr();
  widget->Show();
}

void ProfileMenu::Confirm(Pending kind,
                          const std::u16string& title,
                          const std::u16string& text,
                          const std::u16string& ok_label) {
  CloseDialog();
  pending_ = kind;
  pending_text_ = text;
  const int serial = ++serial_;
  if (!anchor_ || !anchor_->GetWidget()) {
    return;
  }
  base::WeakPtr<ProfileMenu> weak = weak_factory_.GetWeakPtr();
  auto dialog =
      ui::DialogModel::Builder()
          .SetTitle(title)
          .AddParagraph(ui::DialogModelLabel(text))
          .AddOkButton(base::BindOnce(&ProfileMenu::OnAnswer, weak, serial,
                                      /*accept=*/true),
                       ui::DialogModel::Button::Params().SetLabel(ok_label))
          .AddCancelButton(base::BindOnce(&ProfileMenu::OnAnswer, weak, serial,
                                          /*accept=*/false))
          .SetCloseActionCallback(base::BindOnce(&ProfileMenu::OnAnswer, weak,
                                                 serial, /*accept=*/false))
          // Enter keeps the data. The default button of a dialog that
          // destroys something must be the one that does not.
          .OverrideDefaultButton(ui::mojom::DialogButton::kCancel)
          .Build();
  auto bubble = std::make_unique<views::BubbleDialogModelHost>(
      std::move(dialog), anchor_, views::BubbleBorder::BOTTOM_LEFT);
  views::Widget* widget = views::BubbleDialogDelegate::CreateBubbleDeprecated(
      std::move(bubble), views::Widget::InitParams::NATIVE_WIDGET_OWNS_WIDGET);
  dialog_widget_ = widget->GetWeakPtr();
  widget->Show();
}

void ProfileMenu::OnAnswer(int serial, bool accept) {
  if (serial != serial_ ||
      (pending_ != Pending::kClear && pending_ != Pending::kDelete)) {
    return;
  }
  const Pending kind = pending_;
  const ProfileId profile = pending_profile_;
  pending_ = Pending::kNone;
  pending_text_.clear();
  if (!accept) {
    return;
  }
  if (kind == Pending::kClear) {
    model_->ClearProfileData(profile);
  } else {
    model_->DeleteProfile(profile);
  }
}

void ProfileMenu::OnNewProfile(int serial,
                               const std::u16string& name,
                               int color) {
  if (serial != serial_ || pending_ != Pending::kNewProfile) {
    return;
  }
  pending_ = Pending::kNone;
  // A profile with no name has nothing to list it by.
  if (!name.empty()) {
    model_->CreateProfileForSpace(space_, name, color);
  }
}

void ProfileMenu::OnRename(int serial, const std::u16string& name) {
  if (serial != serial_ || pending_ != Pending::kRename) {
    return;
  }
  pending_ = Pending::kNone;
  if (!name.empty()) {
    model_->RenameProfile(pending_profile_, name);
  }
}

void ProfileMenu::CloseDialog() {
  if (dialog_widget_) {
    dialog_widget_->Close();
  }
}

void ProfileMenu::AnswerForTesting(bool accept) {
  OnAnswer(serial_, accept);
}

void ProfileMenu::SubmitNewProfileForTesting(const std::u16string& name,
                                             int color) {
  OnNewProfile(serial_, name, color);
}

void ProfileMenu::SubmitRenameForTesting(const std::u16string& name) {
  OnRename(serial_, name);
}

}  // namespace arcium
```

Check the exact `ui::DialogModel` API names in this tree before building —
`AddTextfield`, `AddCombobox`, `DialogModelCombobox::Params::SetInitialIndex`,
`GetTextfieldByUniqueId`, `GetComboboxByUniqueId`, `selected_index()` — in
`ui/base/models/dialog_model.h` and `dialog_model_field.h`, and adjust the
names (not the behaviour) to what is declared there. Record any rename in the
report.

- [ ] **Step 3: Wire it into the space bar and draw the badge**

In `arcium/ui/sidebar/space_bar_view.h`: add `class ProfileMenu;` to the
forward declarations; add `kProfile,` to `MenuCommand` after `kMoveRight`
(before `kGradientFirst`); add public

```cpp
  ProfileMenu* profile_menu_for_testing() { return profile_menu_.get(); }
  views::View* profile_badge_for_testing() { return profile_badge_; }
```

and, declared before `menu_model_` (the menu keeps a bare pointer to the
submenu's model):

```cpp
  std::unique_ptr<ProfileMenu> profile_menu_;
```

and a private `void UpdateProfileBadge();`.

In `arcium/ui/sidebar/space_bar_view.cc`: include
`arcium/ui/sidebar/profile_colors.h` and `arcium/ui/sidebar/profile_menu.h`.
In the constructor, before `menu_model_` is built:

```cpp
  profile_menu_ = std::make_unique<ProfileMenu>(model_, profile_badge_);
```

and in the menu, after `AddSubMenu(kArchiveTimeout, …)`:

```cpp
  menu_model_->AddSubMenu(kProfile, u"Profile", profile_menu_->model());
```

`SetMenuSpace` also points the profile menu at the space (replace the inline
body in the header with a declaration and define it in the .cc):

```cpp
void SpaceBarView::SetMenuSpace(SpaceId id) {
  menu_space_ = id;
  profile_menu_->SetSpace(id);
}
```

At the end of `Rebuild()`, before `PlaceEditField();`:

```cpp
  UpdateProfileBadge();
```

Replace `OnThemeChanged`'s badge line with a call to `UpdateProfileBadge()`,
and add:

```cpp
void SpaceBarView::UpdateProfileBadge() {
  // The active space's profile: the one whose logins the tab on screen uses.
  ProfileId profile_id = DefaultProfileId();
  for (const SidebarSpace& space : model_->spaces()) {
    if (space.is_active) {
      profile_id = space.profile_id;
    }
  }
  SidebarProfile profile{.id = DefaultProfileId(), .name = u"Default"};
  for (const SidebarProfile& candidate : model_->profiles()) {
    if (candidate.id == profile_id) {
      profile = candidate;
    }
  }
  profile_badge_->SetBackground(views::CreateRoundedRectBackground(
      ProfileColorAt(profile.color).color,
      static_cast<float>(metrics::kProfileBadgeSize) / 2));
  const std::u16string label = base::StrCat({u"Profile: ", profile.name});
  profile_badge_->SetTooltipText(label);
  profile_badge_->GetViewAccessibility().SetName(label);
}
```

Delete the constructor's `profile_badge_->SetTooltipText(u"Profile: Default");`
line; `UpdateProfileBadge` sets it.

- [ ] **Step 4: The playground**

In `arcium/ui/playground/sidebar_example.cc`, after the example's spaces are
seeded, add a second profile and put the second space on it, so the badge
and the submenu can be checked there:

```cpp
  const ProfileId work = model_->AddProfileForTesting(u"Work", 2);
  model_->SetSpaceProfile(model_->spaces()[1].id, work);
```

(If the example seeds only one space, seed `AddSpaceForTesting(u"Work", u"", 4)`
first.)

- [ ] **Step 5: Run the tests, then mutation checks**

```bash
scripts/format arcium/ui/sidebar/profile_menu.* arcium/ui/sidebar/space_bar_view.* arcium/ui/playground/sidebar_example.cc arcium/test/profile_menu_unittest.cc
ARCIUM_JOBS=4 scripts/build dev arcium_unittests
/Volumes/Texternal/chromium/src/out/dev/arcium_unittests --gtest_filter='ProfileMenuTest.*:SpaceBarTest.*'
ARCIUM_JOBS=4 scripts/build dev arcium_playground
```

| Mutation | Test that must fail |
|---|---|
| `SetSpace` adds "Delete profile…" for Default too | `DefaultOffersNoDelete` |
| `OnAnswer` acts when `accept` is false | `ClearingAsksFirstAndSaysWhatStays` |
| `OnNewProfile` drops the empty-name check | `AnEmptyNameMakesNoProfile` |
| `UpdateProfileBadge` always shows Default | `TheBadgeShowsTheActiveSpacesProfile` |
| `SetMenuSpace` no longer points the profile menu | `TheSpaceMenuCarriesTheProfileSubmenu` stays green but `ListsEveryProfile…` is unaffected — instead mutate `ProfileMenu::SetSpace` to skip the radio items and confirm `ListsEveryProfileWithATickOnTheSpacesOwn` fails |

Then the snapshot check and commit:

```bash
/Volumes/Texternal/chromium/src/out/dev/arcium_unittests
/Volumes/Texternal/chromium/src/out/dev/arcium_playground --snapshot=/private/tmp/claude-501/-Volumes-Texternal-repositories-arcium/701eac15-b3e1-4ed6-903a-e6062705f228/scratchpad/profiles.png
git add arcium/ui/sidebar/profile_menu.h arcium/ui/sidebar/profile_menu.cc arcium/ui/sidebar/space_bar_view.h arcium/ui/sidebar/space_bar_view.cc arcium/ui/playground/sidebar_example.cc arcium/test/profile_menu_unittest.cc
git commit -F - <<'MSG'
Profiles are made, chosen, cleared and deleted from the space menu

Every destructive step asks first with Cancel as the default, and says
what stays: history, passwords and bookmarks are shared by every profile.
The badge in the space bar now shows whose logins the tab on screen uses.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
MSG
```

---

### Task 13: Acceptance, documentation and performance

Everything is built; this task proves it, writes down what the proving found, and corrects the places in the project's own documents that still describe profiles the way they were imagined rather than the way they are.

**Files:**
- Create: `docs/stage3b-findings.md`
- Modify: `docs/superpowers/specs/2026-09-05-arcium-browser-design.md`, `CLAUDE.md`, `README.md`
- Create: `docs/perf/2026-09-12-stage3b.md` (name it for the day the run happens)

- [ ] **Step 1: Run everything that runs by itself**

```bash
pgrep -f siso; pgrep -f "Arcium.app/Contents/MacOS/Arcium"
ARCIUM_JOBS=4 scripts/build dev arcium_unittests
/Volumes/Texternal/chromium/src/out/dev/arcium_unittests
ARCIUM_JOBS=4 scripts/build dev arcium_browsertests
/Volumes/Texternal/chromium/src/out/dev/arcium_browsertests
```

Both suites must be green with no filter. A browser test that is flaky rather than failing is still a failure: run the suite a second time and record both results. Do not build `chrome` or `arcium_browsertests` while the owner's Arcium is running from `out/dev`.

- [ ] **Step 2: The acceptance list, by hand**

Build and launch:

```bash
ARCIUM_JOBS=4 scripts/build dev
scripts/run
```

Work through the list below, writing the result of each line as it happens. A3.1 needs two real accounts on one site and only the owner can sign in, so that one is the owner's to run; everything else can be run against the local four-host harness from Stage 2.6 or any two sites.

| Check | What to do | What must happen |
|---|---|---|
| A3b.1 (automated) | Confirm the browser-test suite ran green in step 1 | Every way a tab can be made lands in its space's own logins |
| A3b.2 | Sign in to a site in a space with its own profile, then drag that tab to a space on another profile | The tab stays where it was dropped, shows the same page and its back history, and is signed in as the space it landed in |
| A3b.3 | Clear one profile from the space menu | That space is signed out, every other space is untouched, and no page reloads |
| A3b.4 | Delete a profile, then quit and launch again | Its spaces stay, moved to the shared logins and signed out; the profile is gone from the menu and is still gone after the relaunch |
| A3b.5 | Run Chrome's Clear browsing data with cookies selected and a second profile in use | The warning appears first and says it cannot be undone; "shared logins only" leaves the second profile signed in; "every space too" logs both out |
| A3b.6 | Install an extension, uninstall it, then launch twice | Every profile is still signed in, both times |
| A3b.7 | Open an extension's options page from a space with its own profile, and from a Default space | Both show the same saved settings |
| A3.1 (owner) | Sign in to the same site as two different accounts in two spaces, quit, launch again | Both accounts are still signed in, each in its own space, and nothing loads until a tab is clicked |
| A3.3 | Watch memory and process count with three spaces and two profiles open | No process per profile while nothing is loaded, and no growth per background space |
| Guard | In a space with its own profile, open a link that says it wants no connection to its opener | The page opens in that space's profile and does not leave a stray tab behind |
| Browser pages | Type a settings address into a tab in a space with its own profile | It opens normally, in shared storage, and no empty tab is left behind |

Anything that does not happen as written is a defect: fix it in its own commit with a test that fails without the fix, and record it in the findings document with what was wrong and what changed.

- [ ] **Step 3: Performance**

```bash
scripts/perf --label stage3b
```

Run it when the machine is otherwise quiet. Record the numbers in `docs/perf/<date>-stage3b.md` beside the Stage 3a and R3.9 runs, and answer the four questions in `CLAUDE.md` against what was measured, not against what was expected:

1. A profile adds no process of its own while its spaces sit in the background, because a space with no loaded tab has no storage built and nothing to run.
2. Idle memory per window is unchanged; a loaded profile costs what its open pages cost, the same as those pages would cost anyway.
3. Startup does no more than before: the model file is read as it already was, and nothing asks for a profile's storage until a tab in it loads.
4. Nothing new runs on the UI thread per frame; the removals and deletions are asynchronous, and the only added work per navigation is a string comparison in the guard.

If a measurement contradicts one of those four, say so plainly in the findings and in the perf note; do not adjust the sentence to fit.

- [ ] **Step 4: Correct the project's documents**

In `docs/superpowers/specs/2026-09-05-arcium-browser-design.md`, section 4.4 (profiles) and the risk table still describe profiles as they were imagined. Correct them to what was built, keeping the original wording wherever it is still true:

- A profile is a storage area inside the one Chromium profile, named by an id, and a space points at one. The default profile is Chromium's own storage and has no directory of its own.
- Moving a tab between profiles reopens it at the same address with its history; nothing unsaved survives, because no page is asked whether it may close.
- History, bookmarks, passwords, extensions and settings are shared by every profile. Only cookies, site storage and cache are separate.
- A site that keeps its login in a session cookie stays signed in across a quit in every profile, which needed an upstream change.
- Chrome's own Clear browsing data reaches the shared logins only unless the warning's wider answer is chosen.
- The risk row about Chromium's storage cleanup deleting profiles is now answered, and how: the cleanup is given the list of directories to keep, and is put off entirely when that list cannot be built.

In `CLAUDE.md`, replace the Stage 3 row's trailing "3b profiles as storage partitions not started" with a sentence in the same shape as the others, naming the findings document, the design document, which acceptance lines were run by hand and on what date, how many defects were found, and whether perf was measured.

In `README.md`, update the Stage 3b block written in Task 0 to describe what the finished feature does rather than what it will do.

- [ ] **Step 5: Write the findings**

Create `docs/stage3b-findings.md`, in the shape of `docs/stage3a-findings.md`: what was built, what the acceptance pass found, every defect with its cause and its fix, every ruling made during execution that changed the plan, what is known to be imperfect and was left alone, and what is deliberately out of scope. Say the things a later reader would otherwise have to rediscover, including at least:

- A tab that changes profile loses anything unsaved on the page, and its per-tab storage, because it is genuinely a new page.
- Prerendering is off in a profile's storage, so a page that Chrome would have loaded ahead of time is loaded when it is clicked.
- Deleting a profile that something has opened this session leaves an empty folder behind until a later launch removes it.
- Chrome's own Clear browsing data cannot be stopped once its button is pressed, and why.

- [ ] **Step 6: Commit**

```bash
scripts/format
git add docs/stage3b-findings.md docs/superpowers/specs/2026-09-05-arcium-browser-design.md docs/perf/<date>-stage3b.md CLAUDE.md README.md
git commit -F - <<'MSG'
Record what the profiles pass found and correct the design document

The master design still described profiles as they were imagined before
they were built: what is shared between them, what happens to a tab that
changes profile, and what Chrome's own clear can reach are all written
down now, along with the acceptance pass, the measurements and the things
left imperfect on purpose.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
MSG
```

