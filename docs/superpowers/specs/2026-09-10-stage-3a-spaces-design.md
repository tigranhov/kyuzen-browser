# Stage 3a design: spaces

Status: approved in the design session of 2026-09-10. Implementation plan:
`docs/superpowers/plans/2026-09-10-stage-3a-spaces.md`, whose three rulings
(the `IDC_SELECT_TAB_n` count in §4.2, the delete flow in §6, and A3a.6) are
already folded into the text below. Stage 3 was split in that
session into a partition spike (done), 3a spaces (this document), 3b profiles,
and R3.9 on its own. 3a implements R3.1, R3.2, R3.3 and R3.8 of the master
spec; every space in 3a uses the default profile.

Research this design rests on: `docs/research/stage3-partition-spike.md`, which
confirmed that a space can point at a profile and a profile at a storage
partition, so the data model below does not need to change when 3b arrives.

---

## 1. Goal

One window holds several spaces. Each has a name, an icon and a gradient, its
own favourites, its own pins and folders, and its own Today. Switching spaces
slides the sidebar and shows that space's last active tab. Everything survives
a restart, and the window reopens on the space that was active at quit.

## 2. Decisions made in the design session

- **Favourites and pins belong to the space; logins belong to the profile.**
  This is spec R3.3 as written. It is stricter than Zen, whose shipped default
  (`zen.workspaces.separate-essentials: true`) shares essentials across every
  space that uses the same container (`ZenSpaceManager.mjs:2291`). The same
  site pinned in two spaces is two tabs.
- **Deleting a space destroys it, as in Zen.** A confirmation, then its tabs,
  pins, favourites, folders and archive rows are gone. This is the one
  destructive delete in Arcium and is recorded as a deviation (§7).
- **"Move to space" is in 3a, from the row menu only.** Today, pinned and
  favourite rows. Dragging a row onto a space's dot comes later.
- **One tab strip with a side table (master spec §4.3).** Every space's tabs
  stay in the single `TabStripModel`; Arcium records which space each tab is
  in and limits Chromium's strip-wide behaviour to the active space. Swapping
  the strip's contents on each switch and a tab group per space were both
  rejected: the first moves every tab on every switch and drops hidden tabs
  from the session, the second is ruled out by deviation D2-1.

## 3. Model and persistence

### 3.1 `Space`

`arcium/browser/model/space.h` gains three fields:

- `icon` — one emoji, or empty to draw the first letter of the name;
- `gradient` — the id of a preset from a small fixed palette. A preset is a
  pair of stops for light and a pair for dark; preset 0 is today's
  `kColorArciumSidebarBackgroundTop` / `Bottom`, so an existing space looks
  exactly as it does now. A custom gradient editor is Stage 6's;
- `last_active_tab` — a `TabKey` (§3.3), or empty.

The model file becomes schema version 3. The migration step from 2 fills the
three defaults; `model_migration.cc`'s `static_assert` already requires the
step to exist. The model also records `last_active_space`, which space was
active at quit, so the window can reopen on it.

### 3.2 `ArciumModel`

New: `AddSpace`, `RenameSpace`, `SetSpaceIcon`, `SetSpaceGradient`,
`ReorderSpace`, `RemoveSpace`, `MoveEntryToSpace`, `SetLastActiveTab`,
`SetLastActiveSpace`.

- `RemoveSpace` removes the space's entries and folders and refuses the last
  space. Its archive rows are deleted by `ArchiveService` on the background
  sequence, never on the UI thread.
- `AddEntry` takes the space explicitly; there is no default.
- `MoveEntryToSpace` puts the entry at the end of the target space's top level,
  with no folder, since folders do not cross spaces.

`default_space_id()` keeps its meaning — the first space — but only as a
fallback. Its non-test uses (17 references in 9 files) currently mean "the
space being drawn"; they change to the window's active space (§4.1).

### 3.3 Which space a tab is in

- **A pinned or favourite tab** is in its entry's space. This is not stored on
  the tab, so moving an entry cannot leave its tab behind.
- **A Today tab** carries a space tag.
- **Every tab** carries a `TabKey`, a random id. `Space::last_active_tab` needs
  to name a Today tab across a restart, and Chromium's `SessionID` does not
  survive one (Stage 2 finding 1).

The tag and the key live on the `WebContents` and ride in the session's
`extra_data` beside the entry id, through the same three writers
(`arcium/browser/session_tab_entry.h`): the rebuild writer, the restore
stash-and-bind, and patch 0140's in-session writer for Cmd+Shift+T. They live
in a new `arcium/browser/tab_space.{h,cc}` beside `session_tab_entry`.

A tab that arrives with no tag, or with a tag naming a space that no longer
exists, is in the first space.

### 3.4 Which space a new tab joins

A new tab joins its opener's space, or the active space when it has no opener. "Opener" here is the tab strip's opener, the one `browser_navigator.cc` records for a link click, not `window.opener`: a Cmd+click has the first and not the second.
Links from other applications open in the active space. The decision is made in
the `TabStripModel` observer Arcium already has; it needs no patch.

## 4. Limiting Chromium to the active space

### 4.1 `SpaceSwitcher`

`arcium/ui/browser/space_switcher.{h,cc}`, one per window, owned by
`BrowserSidebarController`. It holds the active space, answers "does this tab
belong to the active space", and performs switches (§5). The sidebar model,
the archive service and tab search read the active space from it.

### 4.2 Strip-wide commands — `patches/0155-tab-commands-space.patch`

Seam: `BrowserCommandController::ExecuteCommandWithDisposition`, the seam patch
0090 uses for `IDC_NEW_TAB`. Delegates to `arcium::HandleTabCommand` in
`arcium/ui/browser/tab_commands.{h,cc}`, which returns false for any command it
does not own, exactly as `HandleNewTabCommand` does.

| Command | Meaning in a space |
|---|---|
| `IDC_SELECT_NEXT_TAB` / `IDC_SELECT_PREVIOUS_TAB` | next / previous open tab of the active space, in sidebar order, wrapping; cold rows are skipped, so cycling never loads a page |
| `IDC_SELECT_TAB_0` … `IDC_SELECT_TAB_7` | the nth **open** tab of the active space in sidebar order — favourites, then pinned, then Today. Cold rows are skipped and never opened: a cold row's place depends on which folders are collapsed, which only the view knows, so a count made in the browser layer would disagree with the screen. A digit past the space's last open tab does nothing |
| `IDC_SELECT_LAST_TAB` | the last open tab of the active space |
| `IDC_MOVE_TAB_NEXT` / `IDC_MOVE_TAB_PREVIOUS` | a Today tab swaps with the next / previous Today tab *of the same space*; on an entry's tab it does nothing, since entries are ordered by the model, not the strip |
| `IDC_CLOSE_TAB` | when the tab is the active space's last open tab, a blank tab is opened in the space first (§4.3); otherwise Chromium's own close |
| `IDC_WINDOW_CLOSE_OTHER_TABS` / `IDC_WINDOW_CLOSE_TABS_TO_RIGHT` | only the active space's Today tabs; never an entry's tab, never another space's |

### 4.3 The next tab after the active one closes — `patches/0160-tab-strip-selection-space.patch`

`TabStripModel::DetermineNewSelectedIndex`
(`chrome/browser/ui/tabs/tab_strip_model.h:1385`) is the single choke point
behind both close paths (`tab_strip_model.cc:803` and `:4565`). The patch has
it first ask a new method on `TabStripModelDelegate`
(`chrome/browser/ui/tabs/tab_strip_model_delegate.h`) that returns
`std::optional<int>` and defaults to `std::nullopt`, so a delegate that does not
answer changes nothing. `BrowserTabStripModelDelegate` answers by calling
`arcium::NextSelectedIndexInSpace`.

The indirection is forced by the build graph, not chosen: `tab_strip_model.cc`
is in `//chrome/browser/ui/tabs:tab_strip_impl`, below `//chrome/browser/ui`,
and only `//chrome/browser/ui` reaches `arcium/` (patch 0010). The tab-strip
half of the patch can only declare the question.

Arcium's answer: the tab that Chromium would pick, if it is in the active
space; otherwise the nearest open tab of the active space in sidebar order.

There is no answer for a space with no other open tab. `DetermineNewSelectedIndex`
returns `std::nullopt` only when the strip is about to be empty (`count() == 1`);
while other spaces' tabs remain, Chromium must activate a real tab, and the
quick entry is a floating bubble over the current page, not a tab. So a space
is never allowed to run out: when the active space's last open tab is closed by
Cmd+W (§4.2) or from the sidebar, Arcium first opens a blank tab in the space,
activates it, and shows the quick entry over it, and only then closes the old
tab. A close Arcium does not see — a script closing the popup it opened — falls
through to Chromium's pick, and §4.4 then switches to that tab's space. That is
accepted and is the only way the rule in §4.4 can move you without asking.

### 4.4 Activating a tab in another space

Activating a tab from another space — from tab search, a notification, or
Chromium itself — switches to that space. The `TabStripModel` observer sees
the activation; no patch. The rule is that the sidebar never shows one space
while the page belongs to another.

## 5. Switching and inputs

A switch records the current space's active `TabKey` into
`last_active_tab`, activates the target's last active tab — or its first open
tab, or, when it has none, a new blank tab in it with the quick entry shown
over it (§4.3) — and rebuilds the sidebar for the
target space. The sidebar's contents slide horizontally as a compositor layer
animation of about 200 ms. The page swaps at the start of the animation; it is
not animated.

- **Dots.** `SpaceBarView` shows one dot per space, drawn with its icon, and
  an add button. Its existing menu items Rename, Change icon, Edit theme and
  Delete, which do nothing today (`space_bar_view.cc:116` handles only the
  archive timeout), are wired up. Move left and Move right are added for R3.1's
  reorder.
- **Swipe.** A two-finger horizontal swipe over the sidebar switches spaces.
  Over the page it remains Chromium's back and forward.
- **Ctrl+1..9.** Registered on `SidebarView` as Cmd+Shift+Backspace already is
  (`sidebar_view.cc:121`). Like that shortcut they arrive only when the page
  does not consume the key. There is no cap on spaces; the shortcuts reach the
  first nine.
- **Gradient.** `TintBackground` takes its two stops from the active space's
  preset instead of from the colour-mixer ids. It already caches its shader
  and rebuilds it only when the colours change.
- **"Move to space"** is a submenu on Today, pinned and favourite rows, beside
  the existing folder "Move to" (`row_context_menu.cc:108`). Moving the active
  tab takes you to the target space, as Zen does (`ZenSpaceManager.mjs:2963`).

`SidebarModel` and the playground's fake model gain the spaces API the views
need: the list of spaces, the active one, switch, add, rename, icon, gradient,
reorder, delete, and move-to-space.

## 6. Edges

- **Deleting a space.** The confirmation names the space and how many tabs,
  pins and favourites go with it. If it is the active space, the switcher moves
  to the neighbouring space first. The space's tabs close through Chromium's
  normal close, so a page with unsaved work still gets its `beforeunload`
  prompt. The space then leaves the model at once, and any tab still alive —
  one held open by a prompt the user has not answered — is re-tagged into the
  space the window moved to. Chromium offers no cancel signal at the seam
  Arcium has, and a state machine waiting for one can hang on a dialog for
  ever; re-tagging is deterministic and puts the survivor somewhere the user
  can see it, rather than leaving it tagged with a space that is gone, which
  §3.3 would silently move to the first space.
- **Moving a pin or favourite whose tab is open** carries the tab, because the
  tab's space is read from the entry.
- **Closing the last tab in a space** leaves the space with one blank tab and
  the quick entry over it (§4.3), never showing another space's page. What
  Chromium does when the whole strip empties is unchanged.
- **Cmd+Shift+T** reopens a tab in the space it was closed from.
- **Archiving.** Each Today tab ages against its own space's
  `archive_timeout`. D2-2's rule that the window's active tab is never
  archived extends to every space's `last_active_tab`, so a quiet background
  space cannot lose the tab a switch would land on.
- **Background spaces do nothing.** Their tabs stay in the strip, where
  Chromium already throttles hidden tabs. Arcium adds no timer, poll or
  thumbnail per space. Unloading those tabs is Stage 7's (R7.2).

## 7. Deviation recorded

**D3-1. Deleting a space is destructive.** Stage 2.5 established that Arcium
has no destructive delete: deleting a folder moves its contents up a level.
Deleting a space breaks that rule on purpose, to match Zen
(`ZenSpaceManager.mjs:1266`, "This action cannot be undone"). A space owns its
favourites in Arcium, which Zen's spaces do not, so an Arcium delete removes
more than Zen's does. The confirmation says what will go. This goes into the
master spec's §7 when 3a lands.

## 8. Testing

Unit tests in `arcium/test/`, written first, each followed by deleting the code
just written and confirming the named test fails.

- **On the real `TabStripModel`** (`BrowserWithTestWindowTest`), with two
  spaces whose tabs are interleaved A, B, A, B, so that any unhooked path lands
  in the wrong space rather than passing by luck:
  every command in §4.2 stays in the active space; closing the active tab
  never activates another space's tab, including when the closed tab's strip
  neighbour is foreign and when the closed tab is the space's last open tab,
  which must leave a blank tab in the space; a new tab joins its opener's space, or the active one;
  activating a foreign tab switches the space.
- **Session round trip:** the space tag and `TabKey` survive all three
  `extra_data` writers; an unknown space falls back to the first.
- **Model:** add, rename, reorder, remove (entries and folders go, the last
  space is refused), `MoveEntryToSpace` landing at the top level, and the
  migration from version 2.
- **Views:** a new `space_bar_unittest.cc` for the dots, the menu and
  switching. `sidebar_views_unittest.cc` is already 2,067 lines and does not
  grow.

## 9. Acceptance list

All executed by hand.

- **A3a.1** Create three spaces with names, icons and gradients; rename one;
  reorder them; delete one after its confirmation. Its tabs, pins and
  favourites are gone; the other two are untouched.
- **A3a.2** Switch with the dots, a swipe and Ctrl+1..3. The sidebar slides and
  each space shows its own last active tab.
- **A3a.3** Each space has its own favourites, pins and Today. The same site
  pinned in two spaces is two tabs.
- **A3a.4** Ctrl+Tab and Cmd+1..8 never reach another space's tab. Closing the
  active tab never shows another space's tab.
- **A3a.5** "Move to space" on a Today tab, a pin and a favourite. The pin lands
  at the target's top level, and you end up in the target space.
- **A3a.6** Cmd+click opens in the current space. The tab-search half of this
  check is not run: nothing in the shipped UI reaches `TabSearchService` yet,
  so §4.4's rule is checked through A3a.8 instead, where Cmd+Shift+T reopens a
  tab of a background space.
- **A3a.7** Quit and relaunch: every space comes back with its entries, its
  Today tabs and its last active tab, and the window opens on the space that
  was active at quit (R3.8).
- **A3a.8** Cmd+Shift+T reopens a closed tab in its own space.
- **A3.2** (master spec) A Perfetto trace of a space switch holds 60 fps.

## 10. Performance

1. **Processes:** none added.
2. **Idle memory:** two short strings per tab, a few bytes per space, one dot
   view per space. Nothing per tab beyond the strings.
3. **Startup:** spaces load in the model file read that already happens.
4. **UI thread:** a switch activates one tab and rebuilds the sidebar rows
   through the existing rebuild. That rebuild must stay under 8 ms for a space
   of about 50 rows; the slide runs on the compositor. If the A3.2 trace shows
   the rebuild over budget, the fallback is to keep one row list per space
   alive instead of rebuilding.

## 11. Out of scope

- Profiles, partitions and per-profile clear data: 3b.
- R3.9, no page loads at launch unless asked: its own piece.
- Dragging rows onto dots, and dragging dots to reorder.
- A custom gradient editor: Stage 6.

## 12. Before implementation

`arcium/ui/browser/browser_sidebar_controller.{h,cc}` must own the
`SpaceSwitcher`, and it and `arcium/ui/sidebar/sidebar_metrics.h` currently
hold 25 lines of the owner's uncommitted work. They are not to be modified,
reverted or committed by anyone but the owner. The plan's first task that
touches them waits until the owner has committed them or said what to do.
