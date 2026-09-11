# Stage 3 design: no page loads at launch

Status: approved in the design session of 2026-09-11. Implements R3.9 of the
master spec, the piece Stage 3 split off in the design session of 2026-09-10
to be built on its own, before 3b's profiles. Every space still uses the
default profile.

---

## 1. Goal

After a restart only the tab on screen loads. Every other tab that comes back,
pinned, favourite or Today, in any space, keeps its title, favicon and back
history but does not load until it is clicked or its space is switched to.
When it does load, it opens on the page it was left on, and Back works.

In the sidebar, a tab that has not loaded looks the way a closed pinned or
favourite row already looks: dimmed title, faded favicon. Anything that will
have to load when clicked is dimmed; after a restart only the tab on screen is
bright, and each tab brightens as it starts to load.

## 2. Decisions made in the design session

1. **A tab that has not loaded is drawn like a closed one.** The owner chose
   one "not loaded" look over a third look of its own. The cost is that a
   closed pin and an unloaded pin cannot be told apart at a glance; R7.5 asks
   for three distinguishable states, so this is recorded as D3-2 (§6).
2. **The rule applies after a restart only.** Launch and crash recovery go
   through the one path this design hooks. Reopening a closed tab or window
   with Cmd+Shift+T is left exactly as Chromium does it: the user asked for
   those tabs.
3. **No switch to turn the dimming off yet.** The owner deferred it to the
   settings screen of Stage 6, as for the closed-row dimming that shipped at
   `5dd4a7b`.
4. **One feature flag, on by default,** so Chromium's own background loading
   can be brought back for a side-by-side comparison, as `kArciumHomeBoundary`
   allows for the home boundary.

## 3. At a restart

### 3.1 What Chromium does today

Session restore creates every tab of the last session with its navigation
history but loads none of them itself, with one exception: the tab on screen,
which `chrome::AddRestoredTab` loads as soon as it is visible
(`chrome/browser/ui/browser_tabrestore.cc:138`). Every other restored tab is
then handed over in one call:

`SessionRestore` → `SessionRestoreDelegate::RestoreTabs`
(`chrome/browser/sessions/session_restore_delegate.cc:84`) →
`performance_manager::policies::ScheduleLoadForRestoredTabs`
(`chrome/browser/performance_manager/policies/background_tab_loading_policy.cc:90`).

That function does two things for each tab: it asks the favicon driver for
the tab's favicon, "to have some visual indication of its contents", and it
queues the tab with `BackgroundTabLoadingPolicy`, which loads at least 4 and at
most 20 of them (`background_tab_loading_policy.h:254-260`), limited by free
memory and by a 30-day cut-off.

Cmd+Shift+T goes through the same policy from a different call site
(`chrome/browser/ui/browser_live_tab_context.cc:374`), which this design does
not touch.

### 3.2 The hook — `patches/0170-session-restore-defer-loads.patch`

Seam: `SessionRestoreDelegate::RestoreTabs`, at the one line that calls
`ScheduleLoadForRestoredTabs`. The session statistics above it
(`SessionRestoreStatsCollector::TrackTabs`) still run; they finish on the
foreground tab's first paint or when the tabs they watch go away, not on
background loads, so they are unaffected. The plan confirms this against
`session_restore_stats_collector.cc` before relying on it.

```cpp
  // Arcium: restored tabs load when the user asks for them (arcium/browser/
  // restored_tab_loading.h), not four to twenty at a time in the background.
  if (arcium::DeferRestoredTabLoads(web_contents_vector)) {
    return;
  }
  performance_manager::policies::ScheduleLoadForRestoredTabs(
      std::move(web_contents_vector));
```

Delegates to: `arcium::DeferRestoredTabLoads`.

`session_restore_delegate.cc` builds in `//chrome/browser/sessions:impl`,
which already depends on `//arcium/browser` through patch 0125. No GN patch is
needed.

### 3.3 `arcium/browser/restored_tab_loading.{h,cc}`

```cpp
// Takes the restored tabs session restore would hand to Chromium's
// background loader. When Arcium's no-load-at-launch rule is on, it asks for
// each tab's favicon, which is the one useful thing the loader did for a tab
// it had not reached yet, and loads none of them. Returns false, changing
// nothing, when the rule is off.
bool DeferRestoredTabLoads(const std::vector<content::WebContents*>& tabs);
```

The favicon request is the same call the loader makes,
`favicon::ContentFaviconDriver::FetchFavicon(GetActiveURL(), false)`. It reads
the favicon database; it does not load the page.

The rule is on when both `kArciumSidebar` and a new `kArciumNoLoadAtLaunch`
(enabled by default) are on, both declared in
`arcium/common/arcium_features.h`. With either off, the window behaves as
stock Chromium does, which is what a comparison run needs.
`--disable-features=ArciumNoLoadAtLaunch` is the switch for it.

`//arcium/browser` gains two deps in Arcium's own `BUILD.gn`:
`//components/favicon/content` and `//arcium/common`. Neither reaches
`//chrome`; `//arcium/common` depends only on `//base`.

## 4. The unloaded state in the sidebar

### 4.1 Where it comes from

A live tab whose page is not in memory says so already:
`NavigationController::NeedsReload()`
(`content/public/browser/navigation_controller.h:636`) is true for a restored
tab that has not loaded and for a tab Chromium discarded to save memory, and
`WebContents::WasDiscarded()` (`web_contents.h:846`) is true for the second.
Nothing new is stored and nothing is counted.

### 4.2 `SidebarRow`

`SidebarRow` (`arcium/ui/sidebar/sidebar_model.h`) gains

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

`SidebarTabModel` sets `is_unloaded` where it builds a row for a live tab,
Today rows and entry rows alike, as
`!is_active && (controller.NeedsReload() || contents->WasDiscarded())`.

### 4.3 How the look stays current

The sidebar already redraws on every tab-strip change:
`SidebarTabModel::OnTabChangedAt` notifies for every change type. A restored
tab that starts loading changes its network state, which reaches the strip as
a loading change; a discard reaches it through
`TabStripModel::UpdateWebContentsStateAt`
(`chrome/browser/resource_coordinator/tab_lifecycle_unit.cc:337` and `:358`).
So a row brightens when its tab starts loading and dims when Chromium unloads
it, with no timer, no poll and no new observer. This is R7.5's rule that
reading the state costs nothing while it is not displayed.

### 4.4 The views

`TabRowView` and `FavoritesGridView` dim a row when `needs_load()` is true,
where they dim a cold row today. The active row keeps its highlight and is
never dimmed; the active check already comes first.

The names that shipped at `5dd4a7b` say "cold", which is now half of what
they cover. They are renamed to say "unloaded": `cold_row_dimming.{h,cc}` to
`unloaded_row_dimming.{h,cc}`, `DimColdFavicon` to `DimUnloadedFavicon`, and
`kColorArciumRowTextCold` to `kColorArciumRowTextUnloaded`. The colour and the
opacity are unchanged.

The playground's fake model gets rows that are unloaded, so the look can be
checked there first, and a seeder for tests.

## 5. Edges

- **Switching spaces** activates the space's last active tab, which loads it,
  and nothing else. Background spaces stay unloaded until visited.
- **The saved tab and the saved space disagree.** Usually the tab on screen at
  a restart is the one left on screen, in the space left active, and the
  launch fallback keeps it (`cb6644c`). If they ever disagree, the window moves
  to the space's own last active tab, and two tabs load instead of one.
  Accepted: nothing else loads.
- **Cmd+Shift+T** on a tab or a window: unchanged, through
  `browser_live_tab_context.cc:374`.
- **Crash recovery** restores through the same path as a launch and gets the
  same rule.
- **Closed pinned and favourite entries** already load only when clicked.
- **A pin left away from its home page** comes back with its history; clicking
  it loads the page it was left on and Back works. Stage 2.6's home boundary
  does not see a reload.
- **Incognito** windows are not restored, so the hook never sees their tabs.
- **The spare renderer.** Chromium keeps one empty renderer process ready at
  all times. It is not a page and the task manager lists it on its own, so the
  acceptance check ignores it.

## 6. Deviation recorded

**D3-2. A tab that has not loaded is drawn like a closed one.** R7.5 says the
sidebar must distinguish three states: cold, warm and discarded, warm and
loaded. The owner chose two looks instead: anything that has to load when
clicked is dimmed, whether it is a closed entry or a tab whose page is not in
memory. The state itself reaches the model as R7.5 requires (`is_unloaded`
beside `is_cold`), so a third look stays a change to the views alone if it is
ever wanted. This goes into the master spec's §7 when this lands.

## 7. Testing

Unit tests, in `arcium/test/`:

- `DeferRestoredTabLoads` with the rule on returns true and requests each
  tab's favicon; with either flag off it returns false and does nothing.
- `SidebarTabModel` marks a restored, not yet loaded tab `is_unloaded`, clears
  it once the tab loads, marks a discarded tab, and never marks the active row
  or a cold row.
- `needs_load()` is true for a cold row and for an unloaded row, false for a
  loaded one.
- The views dim an unloaded row and an unloaded favourite tile exactly as they
  dim a cold one, and do not dim a loaded one.

Each test fails first and passes a mutation check, as every stage has done.

The hook itself is exercised by hand (§8). Arcium has no whole-browser tests;
adding that infrastructure is not this piece's work.

## 8. Acceptance list

All executed by hand.

- **A3.4** (master spec) Pin tabs and open Today tabs in two spaces, quit,
  relaunch: Chromium's task manager shows a renderer for the tab on screen and
  for no other tab. Clicking a pin that was left off its home loads it on that
  page, and Back works.
- **A3.9.1** After that relaunch, only the tab on screen is bright in the
  sidebar. Clicking a dimmed tab brightens it with a spinner, then loads it.
  Switching to the other space loads only the tab it lands on.
- **A3.9.2** Cmd+Shift+T after closing a tab reopens it and loads it.
- **A3.9.3** With `--disable-features=ArciumNoLoadAtLaunch`, the same relaunch
  shows several renderers loading in the background, as stock Chromium does,
  and the rows of loaded tabs are bright.

## 9. Performance

1. **Processes:** fewer. Up to 20 page renderers no longer start after a
   restart; only the tab on screen gets one.
2. **Idle memory:** less, by the pages that no longer load. Nothing per tab is
   added; `is_unloaded` is computed when a row is built.
3. **Startup:** less work after first paint. The favicon lookups happen as
   before; nothing is added before first paint.
4. **UI thread:** one `NeedsReload()` read per live row when the sidebar
   rebuilds, which is a field read.

## 10. Out of scope

- Keeping a reopened window's background tabs unloaded (decision 2).
- A settings switch for the dimming: Stage 6.
- A third look for unloaded tabs (D3-2).
- When Chromium discards tabs, and how many: Stage 7's R7.1 and R7.2.
- Profiles and partitions: 3b. Their restore hook sits where tabs are
  created, not at this handoff.

## 11. Before implementation

Nothing is gated. Work branches from `main` at or after `5dd4a7b`, which the
renames in §4.4 build on.
