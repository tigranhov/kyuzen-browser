# Stage 5a: split view

Date: 2026-09-22. Covers R5.1 alone -- two pages sharing the screen, dragged
into place, with a divider between them. The rest of Stage 5 (R5.2 the
auto-hiding sidebar, R5.3 compact mode, R5.4 full screen) is not in this
design and is not started by it.

R5.1 asks for two to four panes and says to reuse Chromium's native split view
where it fits. The owner settled the count at two on 2026-09-22, so three and
four are deferred, not dropped; the reason and what it would cost are recorded
under "Two panes, not four" at the end.

## What Chromium already gives us

Split view shipped upstream and is on in our pinned tag. It is not behind a
feature flag: `BrowserCommandController` enables `IDC_NEW_SPLIT_TAB` for any
normal browser, and `BrowserView` creates a `MultiContentsView` in every
window whether or not anything is split.

What exists, and what Arcium therefore does not write:

- **The model.** A split is a collection in the window's one `TabStripModel`,
  not a second strip and not a second `Browser`. `AddToNewSplit(indices,
  visual_data, source)` forms one, `RemoveSplit(id)` breaks it,
  `GetSplitForTab(index)` asks whether a tab is in one, `UpdateSplitLayout`
  and `UpdateSplitRatio` set the orientation and where the divider sits, and
  `ReverseTabsInSplit` swaps the sides. All of that is Arcium's one strip, so
  the one-window-one-strip rule is untouched.
- **The panes.** `MultiContentsView` lays out two `ContentsContainerView`s
  with a `MultiContentsResizeArea` between them, keeps each pane at least
  200px or 10% of the width, and draws a small strip over the pane that is not
  focused: favicon, domain, close button, and nothing else.
- **The drag target.** `MultiContentsViewDropTargetController` already raises
  a drop zone at the page's edge when a link or a Chromium tab header is
  dragged towards it.
- **Session restore.** `session_service_commands.cc` writes and reads split
  ids, so a Today tab in a split comes back in that split without Arcium
  asking.

What is missing is every way in. Arcium hides the tab strip, and the tab strip
is where Chrome puts all of them: the tab context menu, the tab drag, the
split toolbar button. Today a split cannot be formed in Arcium at all. This
stage is therefore mostly about reaching machinery that is already there, and
about the one thing Chromium cannot know -- what a split means to the sidebar,
to a space, and to a relaunch.

## Where the decision lives

`arcium/ui/browser/split_controller.{h,cc}`, one per window, owned by
`BrowserSidebarController` the way the peek controller is. It is the only
place that decides:

- whether two tabs may share the screen (`CanSplit(a, b)`),
- how a split is formed from a sidebar row, an entry, or a tab index
  (`SplitWithActive(...)`), including opening a cold entry's page first,
- when a split ends, and
- what is written down so it survives a relaunch.

Everything it does is three or four calls on `TabStripModel`. No logic goes
into a patch, and **this stage adds no patch at all**: the drop zone is a view
the sidebar controller already has a place to create and lay out (patch 0050
creates the controller, patch 0070 calls `LayoutSidebar` with the host's
bounds), and every other entry point is an Arcium view or an Arcium command
that already exists.

### The rule about who may share a screen

`CanSplit` refuses unless all of these hold:

- Both are tabs in this window's strip. A cold entry is opened first and the
  question is asked again about the tab that results.
- Both are in the same space, by `SpaceSwitcher::SpaceOfTabAt`. Two spaces
  may sit on two profiles, which would put two sets of logins on one screen
  and make the partition guard reason about a tab visible in a space it does
  not belong to; and even on one profile the sidebar has nowhere to draw a
  page that belongs to a space the window is not showing. Refused rather than
  resolved.
- Neither is a loose page -- a peek or the page in an outside-link window.
  Stage 4b gives those no space, and the rule above already refuses them; this
  is stated so the refusal is deliberate rather than incidental.
- Neither is already in a split. Chromium's own keyboard path makes the same
  check (`if (!...GetActiveTab()->IsSplit())`). Replacing a pane is a later
  question, not this stage's.

## The four ways in

The owner asked for all four on 2026-09-22.

### 1. Drag a row out of the sidebar onto the page

The main gesture, and Arc's. The sidebar already drags its rows: `RowDragData`
rides in a private clipboard format, and `RowDragSession` says while a drag is
in flight -- it exists because an empty section has nothing to drop on, and
the same signal is what raises the split zone here.

- While a row drag is in flight, `SplitDropView` -- a `BrowserView` child the
  sidebar controller creates and `LayoutSidebar` positions over the page --
  becomes visible, split into a left half and a right half. It is invisible
  and hit-test-transparent at every other moment, so it costs nothing when
  nobody is dragging.
- It accepts only `RowDragData::Format()`, so a drag from outside Arcium is
  refused and Chromium's own link zone keeps that job.
- Dropping puts the dragged row's tab on the side it was dropped on, beside
  the page already showing. A row whose entry has no tab (a cold favourite or
  pinned entry) opens its URL first and the new tab takes that side.
- Source `SplitTabCreatedSource::kDragAndDropTab`.

The zone must not fight the sidebar's own drop targets: it covers the page
only, never the sidebar column, so dragging a Today row up into Pinned goes on
working exactly as it does now.

### 2. The row menu and a keyboard shortcut

`RowContextMenu` gains one item, "Split with current page", shown on a row
that `CanSplit` accepts against the active tab and hidden otherwise -- the
menu is already built per row from its section, so this is that pattern.
Source `kTabContextMenu`.

The shortcut is **Cmd+Option+S**: it splits the page on screen with the tab
that was active before it, in the same space, and breaks the split when one is
already showing. Cmd+S is Chromium's Save page and Cmd+Option+S is unclaimed
in `global_keyboard_shortcuts_mac.mm`; the plan re-checks that table before
binding. Source `kKeyboardShortcut`. This is the only way in that works with
no pointer, which is why it is not folded into the menu item.

### 3. A command in the box

The box is one-shot today: a row is either a command or a destination
(`OnCommandBoxAccepted`). Splitting names a partner, so the box gains its
first two-stage command, `kBoxCommandSplit`:

- Typing "split" offers the command row. Taking it does not close the box: the
  field clears and the rows become the open tabs of the active space, each
  reading "Split with <title>".
- Taking one forms the split and closes the box. Esc leaves the mode and
  returns the box to what it was showing, and it does not close on the first
  Esc.
- The mode is a field on the box, and the tab rows come from the suggestion
  source that already answers "open tabs", filtered to the active space and to
  tabs `CanSplit` accepts.

This is the only way to split with a tab that is scrolled out of sight, which
is why it is worth the box's first mode.

### 4. A link dragged to the page's edge

Chromium's, already working. The work here is to confirm rather than to build:
the new tab the drop creates must be tagged into the active space and given
that space's storage, which the Stage 3b new-tab hooks already do for every
tab created through `chrome::Navigate`. A browser test asserts it, because
nothing else in this stage would notice if upstream changed that path.

## Breaking a split

Four ways, all ending in `RemoveSplit`:

- Closing either tab, which is Chromium's own behaviour and needs nothing.
- The close button on the strip over the unfocused pane, likewise.
- Cmd+Option+S while a split is showing.
- Moving either tab to another space, which the split controller ends
  **before** the move runs -- the same ordering Stage 3b's defect taught us,
  where re-tagging after a reopen let the partition guard read a stale tag.

A split whose tabs end up in different spaces must never exist, so the move
path asks the controller first rather than the controller reacting afterwards.

## The sidebar

Both tabs keep their own rows, wherever the row already sits. The owner chose
this over one merged row on 2026-09-22, because closing, renaming, pinning,
returning to a pinned URL and dragging all address one row, and a merged row
hides one of the two tabs from every one of them.

- `SidebarRow` gains `std::optional<split_tabs::SplitTabId> split;` -- absent
  when the row is not in a split -- and the model fills it from
  `TabStripModel::GetSplitForTab`.
- `is_active` moves from `index == active_index()` to membership of
  `GetForegroundTabs()`, which returns the active tab or, when it is split,
  both tabs of the split. Two rows then read as current, which is true.
- When the two rows are neighbours in the same section, `TabListView` draws a
  bracket down their left edge joining them. When they are not -- a pinned
  entry split with a Today tab sits in a different section, and no ordering
  can put those two together -- each row carries a small two-pane mark
  instead. The bracket is the nicer reading and the mark is the honest
  fallback; both say the same thing.
- Nothing else about a row changes. A pinned entry in a split is still a
  pinned entry.

**One consequence to accept.** `AddToNewSplit` reorders the strip so the two
tabs are contiguous. For pinned and favourite entries that is invisible,
because their order comes from the model's `position` and not from the strip.
For two Today rows it moves one of them in the list, and the one that moves is
the one the reader dragged, which is the one they were already thinking about.

## Spaces, profiles and storage

A split lives inside one space by the rule above, so both pages use that
space's profile and the partition guard has nothing new to decide. Switching
away leaves the split where it is -- it is a property of the strip, not of the
window's view -- and switching back shows it again, because `SpaceSwitcher`
lands on the space's last active tab and `MultiContentsView` shows a split
whenever the active tab is in one.

Two smaller obligations follow:

- `SpaceSwitcher` records a space's last active tab as a single `TabKey`. When
  that tab is in a split it records the focused one, and the split brings the
  other back with it.
- The space-scoped tab commands (Ctrl+Tab, Cmd+1..9, close others) already
  walk a space's tabs and need no rule about splits, because both tabs of a
  split are ordinary members of that space.

## After a quit

A split survives a relaunch, and this is the only new persistent state in the
stage.

Every tab already carries a `TabKey` that survives a restart -- generated on
first ask, written into the session file as `arcium.tab_key`, and already used
by `Space::last_active_tab` for exactly this reason. So a space records:

```
split: { first: TabKey, second: TabKey, layout: "side"|"stacked", ratio: 0.5 }
```

written by `model_serializer.cc` beside `last_active_tab`, with the model file
version raised from 5 to 6 and a migration that reads a version-5 file as one
with no splits. Absent, malformed or naming a tab that does not come back, it
is dropped and the tabs open unsplit; a split is a convenience and must never
be a reason a model file is refused.

Re-forming happens once per record, as soon as both tabs named by it are in
the strip. The controller watches insertions and asks that question, rather
than waiting for a moment called "restore has finished", which nothing in
Arcium currently names and which pinned and Today tabs do not reach together.
A record whose second tab never arrives simply never fires, which is the same
outcome as dropping it. **R3.9 is not weakened.** The split
you left on screen loads two pages because two pages are on screen, which is
what R3.9 already says about the active space. A split in a space you are not
looking at re-forms cold: two tabs, neither loaded, and clicking either loads
both panes, because both are then on screen.

## The strip over the second pane

Chromium draws a favicon, a domain and a close button over whichever pane is
not focused. It stays. R4.7 says the URL surfaces carry no decoration Arcium
did not put there, and this is not decoration: the sidebar's pill can name one
page, and in a split there are two, so the second pane must say what it is.
What it shows -- identity and a way out, nothing else -- is what R4.7 asks the
pill itself to show. If the pass finds it noisy, trimming it is a later
change and not a reason to build our own.

## The four questions

1. **A process, or one kept alive longer?** No. A split shows a second tab
   that already existed; its renderer is the one that tab would have had.
   Nothing is created and nothing is held open.
2. **Idle memory, per window or per tab?** Per window, one hidden view (the
   drop zone) and one pointer-sized field per row. Per tab, nothing. The
   second page's memory is the memory of a tab the reader chose to look at.
3. **Work before first paint?** None at startup. Re-forming a split happens
   after launch, on the same task that resolves the space's last active tab,
   and touches nothing if no space records one.
4. **UI-thread work not needed for the frame?** None. The drop zone is
   invisible and takes no events unless a row drag is in flight; the sidebar's
   split marks are computed in the row build that already runs, from one
   `GetSplitForTab` call per row; the divider drag is Chromium's.

No sync I/O. The model file is written by `ImportantFileWriter` as it already
is, and a split changes one field in it.

## Tests

Unit tests, in `arcium/test/`:

- `CanSplit` refuses two spaces, a loose page, a tab already split, and
  accepts two tabs of one space.
- The serialiser round-trips a split, and a version-5 file migrates to one
  with no splits.
- A split naming a tab that did not come back is dropped and leaves the model
  usable.
- `MatchBoxCommands` offers the split command, and taking it puts the box in
  its second stage rather than closing it.
- The row menu offers "Split with current page" only where `CanSplit` says so.

Browser tests, in `arcium/test/browser/`:

- Forming a split from the sidebar leaves two tabs foreground and one strip.
- Moving one tab of a split to another space ends the split and leaves two
  ordinary tabs, one in each space -- watched to fail when the ordering is
  reversed, the way Stage 3b's tab-move defect was.
- A split survives a quit and relaunch in the active space, with both pages
  loaded, and in an inactive space with neither loaded until it is entered.
  The second half carries its own positive control -- it then clicks the space
  and requires both to load -- because everything else it asserts is that
  nothing happened.
- A link dragged to the page edge creates a tab in the active space with that
  space's storage.

Each new test is watched to fail before the code that makes it pass.

## Acceptance (A5.1)

The spec's A5.1 is "two-pane split with independent scrolling and navigation".
Walked by hand:

1. Drag a Today row onto the right half of the page; two pages appear.
2. Scroll each; they scroll independently. Navigate one; the other does not
   move.
3. Drag the divider; both panes resize and neither collapses below its
   minimum.
4. The sidebar shows both rows as current, joined where they are neighbours.
5. Split a pinned entry with a Today tab; both rows carry the two-pane mark.
6. Switch space and back; the split is still there.
7. Quit and relaunch; the split is still there, both pages load, and a split
   left in another space loads nothing until that space is entered.
8. Move one of the two to another space; the split ends and both tabs survive.
9. Cmd+Option+S forms and breaks a split.
10. Type "split" in the box, take a named tab, and the split forms.

## Two panes, not four

R5.1 asks for two to four. Chromium's split is exactly two: `SplitTabLayout`
has `kSideBySide` and `kStacked` and nothing else, `SplitTabVisualData` holds
one ratio, and `MultiContentsView`'s own comment says it "shows up to two
contents web views side by side", with `SetActiveIndex` documented as taking
"either 0 or 1 as we currently only support two contents".

Three or four panes therefore means replacing `MultiContentsView` with an
Arcium container, and that view is reached by fullscreen, devtools, find-in-
page, picture-in-picture, the capture border and tab dragging. Replacing it
would be the largest piece of upstream surface in the project and would have
to be defended on every rebase, against a requirement whose common case is
two.

**Recorded as a deviation** in section 7 of the master spec: R5.1 ships with
two panes. Three and four stay open, and if they are built later the way in is
an Arcium container behind a feature flag, not a patch to Chromium's.
