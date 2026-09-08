# Zen Browser source research: tab folders

Repository: `zen-browser/desktop` (default branch `dev`, a Firefox fork). All
file paths below are relative to the repo root. Raw content fetched from
`https://raw.githubusercontent.com/zen-browser/desktop/dev/<path>`, browsable
at `https://github.com/zen-browser/desktop/blob/dev/<path>`. Code search used
`gh api search/code` (authenticated GitHub CLI).

Zen's folders are **not** a bespoke sidebar widget. `nsZenFolder` (custom
element `zen-folder`, in `src/zen/folders/ZenFolder.mjs`) is a subclass of
Firefox's own native tab-group custom element, `MozTabbrowserTabGroup`
(`browser/components/tabbrowser/content/tabgroup.js`, patched by Zen via
`src/browser/components/tabbrowser/content/tabgroup-js.patch`). A folder *is*
a Firefox tab group, constrained to live inside the pinned-tabs container and
skinned/behaviourally extended by Zen. Nested folders are tab groups nested
inside other tab groups' DOM containers — an ordinary, currently-shipping
Firefox capability (nested tab groups), not something Zen invented from
scratch. The orchestration logic (creation, drag/drop gating, animation,
session persistence) lives in the singleton `src/zen/folders/ZenFolders.mjs`
(`gZenFolders`), and low-level drag/drop mechanics live in
`src/zen/drag-and-drop/ZenDragAndDrop.js`.

---

## 1. Nesting depth

Zen **does** allow folders inside folders, to a **finite, pref-controlled
depth** — this is not a flat-only model.

Source: `src/zen/folders/ZenFolders.mjs`, line 39:

```js
#ZEN_MAX_SUBFOLDERS = Services.prefs.getIntPref(
  "zen.folders.max-subfolders",
  5
);
```

Default pref value, `prefs/zen/folders.yaml`:

```yaml
- name: zen.folders.max-subfolders
  value: 5
```

`level` is a property of the underlying (patched) `MozTabbrowserTabGroup`,
`src/browser/components/tabbrowser/content/tabgroup-js.patch`:

```js
get level() {
  return this.group?.level + 1 || 0;
}
```

So a top-level folder (not inside any other folder) has `level === 0`; a
folder nested one level in has `level === 1`; and so on. The depth limit is
enforced in **two independent places**, both keyed off `level`, not a
recursive walk-and-count:

1. **Context-menu gate** — disabling "New Subfolder" (`ZenFolders.mjs:126-129`,
   inside the `zenFolderActions` popup's `popupshowing` handler):

   ```js
   const newSubfolderItem = document.getElementById(
     "context_zenFolderNewSubfolder"
   );
   newSubfolderItem.toggleAttribute(
     "disabled",
     folder.level >= this.#ZEN_MAX_SUBFOLDERS - 1
   );
   ```

   i.e. disabled once the folder you right-clicked is already at `level 4`
   (would-be child at `level 5`).

2. **Drag-and-drop gate**, `ZenFolders.mjs:679-683`:

   ```js
   canDropElement(element, targetElement) {
     const isZenFolder = element?.isZenFolder;
     const level = targetElement?.group?.level + 1;
     return !(isZenFolder && level >= this.#ZEN_MAX_SUBFOLDERS);
   }
   ```

   called from the patched tab-move routine in
   `src/browser/components/tabbrowser/content/tabbrowser-js.patch`:

   ```js
   if (!gZenFolders.canDropElement(element, targetElement)) {
     element = element.group;
   }
   ```

   Note this does **not** cancel the drop outright — when dropping a folder
   would exceed the cap, the dragged *folder* is silently swapped for its
   own containing group (`element = element.group`) before the move
   executes, so the operation still does something reasonable (moves the
   folder's parent/context instead of nesting past the cap) rather than
   being a no-op. `highlightGroupOnDragOver` (`ZenFolders.mjs:1396-1422`)
   separately refuses to highlight-as-droppable a folder at/over the cap
   when the dragged item is itself a folder label.

Both gates agree: the maximum *level index* reachable is 4, i.e. **five
folder levels total (0 through 4)** — matching the constant's name
(`max-subfolders: 5`).

Confirmed end-to-end by the browser test
`src/zen/tests/folders/browser_folder_max_subfolders.js`, which repeats
"create subfolder inside the last-created subfolder" `TEST_MAX_FOLDERS - 1`
times (a local constant the test comments say to "keep in sync with the
default value for `zen.folders.max-subfolders`", i.e. also `5`), then
asserts the "New Subfolder" menu item is `disabled` on the 5th folder.

There is **no CSS-side depth cap** — indentation is computed arithmetically
per level with no ceiling (`ZenFolders.mjs:1058-1127`, `setFolderIndentation`):

```js
let level = groupElem?.level + 1 || 0;
...
const baseSpacing = 14; // Base spacing for each level
...
const spacing = (level - tabLevel) * baseSpacing;
...
tabItem.style.setProperty("--zen-folder-indent", `${spacing}px`);
```

If the pref were raised past 5, indentation would just keep growing by 14px
per level; nothing in CSS stops it. The 5-level ceiling is purely the JS
pref/constant above.

---

## 2. What a folder contains

**Pinned tabs only — unconditionally.** `createFolder()` (`ZenFolders.mjs:685-744`)
pins every tab passed to it before adding it, and folders themselves are
inserted into the workspace's `pinnedTabsContainer`:

```js
createFolder(tabs = [], options = {}) {
  const filteredTabs = tabs.map(tab => {
    gBrowser.pinTab(tab);
    ...
  });
  ...
  const pinnedContainer = options.workspaceId && workspacePinned
    ? workspacePinned
    : gZenWorkspaces.pinnedTabsContainer;
  ...
}
```

Confirmed by the browser test `src/zen/tests/folders/browser_folder_create.js`:
`ok(tab.pinned, "Tab is pinned after folder creation")`. There is no code
path that lets an unpinned ("Today"-equivalent) tab sit inside a folder in
its unpinned form — dragging or moving one in pins it as a side effect.
(`on_TabOpen`, `ZenFolders.mjs:417-430`, additionally auto-pins a tab opened
*from* a link inside a folder tab and adds it to the folder, but only when
the pref `zen.folders.owned-tabs-in-folder` is enabled — its default in
`prefs/zen/folders.yaml` is `false`, so by default a new tab opened from
inside a folder tab does **not** join the folder.)

**Essentials/Favourites cannot be folder members.** `nsZenFolder.addTabs()`
(`ZenFolder.mjs:289-311`) strips essential status from any tab as it enters
a folder:

```js
addTabs(tabs) {
  let tabsFromOutside = [];
  for (let tab of tabs) {
    if (tab.hasAttribute("zen-essential")) {
      gZenPinnedTabManager.removeEssentials(tab, false);
    }
    ...
  }
  super.addTabs(tabs);
  ...
}
```

**Folders can contain other folders (subfolders) and split-view groups**,
in addition to plain tabs — `allItems`/`allItemsRecursive`
(`ZenFolder.mjs:185-205`) walk both.

**On collapse: hidden, not unloaded.** Collapsing a folder
(`animateCollapse`, `ZenFolders.mjs:1532-1627`) is a pure DOM/CSS operation:
it animates the collapsed items' `opacity`/`height` to 0 and finally sets
`hidden` on the group's container element (`tabsContainer.setAttribute("hidden", true)`).
Nothing in that path touches the tabs' `linkedBrowser` or asks for a
discard/unload — the underlying page processes and DOM keep running exactly
as they would for a scrolled-out-of-view tab. Actually **unloading** page
content is a separate, explicit, opt-in action: the small reset-style
button on a folder's label (`.tab-reset-button`,
`ZenFolder.mjs.on_click` → `unloadAllTabs()` → `#unloadAllActiveTabs()`)
calls `gZenPinnedTabManager.onCloseTabShortcut(event, this.tabs, { alwaysUnload: true, ... })`,
which is Zen's tab-discard machinery — this only runs when the user clicks
that button, never automatically on collapse.

One nuance: if a folder contains the *currently active/selected* tab (or a
descendant folder does) when it is collapsed, that active tab's row is kept
visible even while the folder shows as collapsed (`animateCollapse` skips
hiding `selectedTabs`/active-group items, and sets a `has-active` attribute)
— i.e. "collapsed" doesn't necessarily mean "every row inside is hidden";
the currently-selected tab peeks through with its own indentation
(`setFolderIndentation(..., forCollapse: true)`).

**A special always-present "empty tab".** Every folder that has ever had a
real tab added to it also carries an invisible/placeholder pinned tab
created with `_forZenEmptyTab: true` (`createFolder`, `ZenFolders.mjs:704-713`),
kept as the folder's first child (`tabs[0]`), and specifically excluded from
`allItems`'s "real content" filtering paths in several places (e.g.
`#normalizeGroupItems` drops it; `#convertFolderToSpace` filters it out
before moving tabs to a new space). It is not documented in-line, but the
call sites strongly suggest its purpose is to keep the underlying Firefox
tab-group DOM node from ever reaching zero real children (Firefox's stock
tab-groups feature can auto-remove/garbage-collect a group once its last
tab closes) — this is an **inference from code structure, not a comment
in the source**, flagged accordingly. It is explicitly cleaned up by both
removal paths (`unpackTabs()` removes it via `gBrowser.removeTab(tab)`;
`delete()` removes all such tabs recursively before calling
`removeTabGroup`, "as removeTabs() inside removeTabGroup does ignore
them" per that method's own comment).

---

## 3. Drag and drop

**What can be dragged into a folder.** Ordinary tabs, tab groups/other
folders (subject to the depth cap in §1), and split-view groups. Essentials
are excluded specially: `#applyDragoverIndicator`
(`ZenDragAndDrop.js:1342-1346`) short-circuits before folder logic runs if
the drop element is essential. "Live folders" (Zen's RSS/GitHub-backed
folders) refuse anything that isn't one of their own generated items —
`#canDropIntoFolder` (`ZenDragAndDrop.js:1259-1278`):

```js
#canDropIntoFolder(dropElement, draggedTab) {
  let folder = dropElement?.classList.contains("tab-group-label-container")
    ? dropElement.parentElement
    : dropElement?.group;
  if (!folder?.isZenFolder) return true;
  if (folder.isLiveFolder) {
    const liveFolderItemId = draggedTab.getAttribute("zen-live-folder-item-id");
    if (!liveFolderItemId || !liveFolderItemId.startsWith(`${folder.id}:`)) {
      return false;
    }
  }
  return true;
}
```

Dragging a folder-label itself onto a *non-pinned* tab, or onto the
new-tab-button area, is refused (`ZenDragAndDrop.js:1384-1395`) — folders
can't be dropped into the unpinned/"Today" region.

**Own-descendant drop.** I found **no explicit application-level guard**
("is target a descendant of the thing being dragged?") anywhere in
`ZenDragAndDrop.js`, `ZenFolders.mjs`, or the two tabbrowser patches. The
only nesting-related check is the depth-cap `canDropElement` in §1, which
is purely arithmetic on `level` and doesn't inspect ancestry. It's plausible
this is implicitly caught by the DOM itself (`Node.insertBefore`/`appendChild`
natively throw `HierarchyRequestError` when asked to insert a node into its
own descendant), which would make an attempted self-nest silently fail
deep in Firefox's stock move code rather than being deliberately refused
by Zen's own logic — **this is inference, not something I could verify by
reading a check that says so explicitly.**

**Drop indicator.** Two distinct visuals, chosen by where in the target's
bounding box the pointer sits (`#applyDragoverIndicator`,
`ZenDragAndDrop.js:1285-1467`):

- **Reordering, between two items** (including "before/after a folder" as
  a sibling): a thin horizontal insertion line — `gZenPinnedTabManager.dragIndicator`,
  positioned via `--indicator-left`/`--indicator-width` custom properties
  and `style.top` set to the boundary between the two rows.
- **Dropping *into* a folder**: a solid full-row highlight box,
  `#zen-dragover-background` (created in `#applyDragOverBackground`,
  `ZenDragAndDrop.js` around line 1230), styled in
  `src/zen/tabs/zen-tabs/vertical-tabs.css:1362-1374`:

  ```css
  #zen-dragover-background {
    position: absolute;
    z-index: -1;
    width: calc(100% + var(--zen-toolbox-padding));
    left: 0;
    pointer-events: none;
    background: var(--zen-primary-color);
    ...
  }
  ```

  i.e. a background wash in the browser's accent color behind the folder
  row, not a line.

Which of the two you get is decided by a **20%-of-row-height threshold**
around the top/bottom edge of the folder's label row, pref
`zen.tabs.folder-dragover-threshold-percent` (`prefs/zen/zen.yaml`,
default `20`, comment: *"Percentage of folder height to trigger
dragover"*), read in `ZenDragAndDrop.js:1365-1374`:

```js
let threshold = Services.prefs.getIntPref(
  "zen.tabs.folder-dragover-threshold-percent"
) / 100;
let dropIntoFolder =
  isZenFolder &&
  (overlapPercent < threshold ||
    (overlapPercent > 1 - threshold &&
      (possibleFolderElement.collapsed ||
        possibleFolderElement.childGroupsAndTabs.length < 2)));
```

i.e. hovering the top 20% or bottom 20% of a folder's label drops *into*
it; the middle 60% (or, if collapsed/near-empty, effectively more of the
row) reorders as a sibling before/after it.

**Spring-loading (hover-to-expand during a drag): not present.** I looked
specifically for an auto-expand-on-hover-while-dragging mechanic and did
not find one. What *does* happen while dragging over a collapsed folder is
purely cosmetic: `highlightGroupOnDragOver` (`ZenFolders.mjs:1396-1422`)
swaps the folder's icon glyph between "open"/"closed" states via
`updateFolderIcon(folder, "open"/"close")`, which only changes an
`state="open|close"` SVG attribute on the folder icon — it does not expand
the folder, reveal its children, or change `collapsed`. Separately, there
*is* a hover-driven popup — `#groupInit`'s `mouseenter` listener
(`ZenFolders.mjs:1172-1198`) opens a small search/list popup
(`zen-folder-tabs-popup`) over a *collapsed* folder after a
`zen.folders.search.hover-delay` pref (default `500` ms,
`prefs/zen/folders.yaml`) — but that listener explicitly bails out during a
drag: `gBrowser.tabContainer.hasAttribute("movingtab")` is checked and
skips opening the popup. So: no spring-loading during drag, by design.

---

## 4. Dragging a tab out of a folder

**Both drag and a bulk context-menu action exist — but not a per-tab
"remove from folder" menu item.** I searched the whole repo for anything
resembling a single-tab "remove from group"/"take out of folder" context
menu entry and found none in Zen's own code. The folder's own context menu
(`zenFolderActions`, `src/browser/base/content/zen-panels/popups.inc:41-62`)
exposes, among other items:

```xml
<menuitem id="context_zenFolderUnpack" data-l10n-id="zen-folders-panel-unpack-folder"/>
<menuitem id="context_zenFolderDelete" data-l10n-id="zen-folders-panel-delete-folder"/>
```

`context_zenFolderUnpack` ("Unpack Folder") is the only context-menu path
that removes tabs from a folder, and it acts on the **whole folder at
once**, not a single tab — `nsZenFolder.unpackTabs()` (`ZenFolder.mjs:162-172`):

```js
async unpackTabs() {
  this.collapsed = false;
  for (let tab of this.allItems.reverse()) {
    tab = tab.group.hasAttribute("split-view-group") ? tab.group : tab;
    if (tab.hasAttribute("zen-empty-tab")) {
      gBrowser.removeTab(tab);
    } else {
      gBrowser.ungroupTab(tab);
    }
  }
}
```

`ungroupTab()` itself is **stock Firefox** tab-groups code (not redefined
anywhere in Zen's patches — I grepped both `tabbrowser-js.patch` and
`tabgroup-js.patch` and found only *call sites*, no definition), so its
exact placement rule (where an ungrouped tab lands relative to its old
group) is inherited from upstream Firefox and I could not verify it by
reading Zen's own source — flagged in §6 below.

For a **single** tab, the only route out of a folder that I could confirm
in Zen's own code is **drag alone**: dragging a tab from inside a folder to
anywhere else in the sidebar goes through the same
`#applyDragoverIndicator`/`#handleTabMove` machinery described in §3, and
the tab lands **at the position it's dropped at** (wherever the insertion
line/highlight indicates), not at some fixed "top of the list" or "bottom
of pinned tabs" position.

**An emptied folder is not auto-deleted.** Nothing in `ZenFolders.mjs` or
`ZenFolder.mjs` removes a folder when its last real tab leaves (via unpack,
drag-out, or the tab being closed) — the folder element persists, empty
except for its always-present placeholder empty-tab (§2), until the user
explicitly deletes it.

---

## 5. Removing a folder

`context_zenFolderDelete` → `nsZenFolder.delete()` (`ZenFolder.mjs:174-183`):

```js
async delete() {
  for (const tab of this.allItemsRecursive) {
    if (tab.hasAttribute("zen-empty-tab")) {
      // Manually remove the empty tabs as removeTabs() inside removeTabGroup
      // does ignore them.
      gBrowser.removeTab(tab);
    }
  }
  await gBrowser.removeTabGroup(this, { isUserTriggered: true });
}
```

`allItemsRecursive` (`ZenFolder.mjs:185-195`) walks into subfolders, so the
manual empty-tab cleanup happens at every nesting depth. The actual removal
of *real* tabs is delegated to `gBrowser.removeTabGroup`, operating on the
group's (patched, recursive — see `tabgroup-js.patch`'s `get tabs()`,
quoted below) `tabs` getter, which **closes them** — it does not reparent
them anywhere:

```js
get tabs() {
  let childs = Array.from(this.groupContainer?.children ?? []);
  const tabsCollect = [];
  for (let item of childs) {
    tabsCollect.push(item);
    if (gBrowser.isTabGroup(item)) {
      tabsCollect.push(...item.tabs); // recurses into subfolders
    }
  }
  return tabsCollect.filter(node => node.matches("tab"));
}
```

Confirmed behaviourally by `src/zen/tests/folders/browser_folder_create.js`:

```js
await removeFolder(folder);
Assert.equal(folder.tabs.length, 0, "Folder is empty after deletion");
ok(tab.closing, "Tab is closing after folder deletion");
```

**"Delete Folder" closes every tab in the folder and all of its
subfolders — it does not move any of them to the parent or the top
level.** The only way to keep the tabs is the *separate* "Unpack Folder"
action (§4), which is a distinct menu item the user must choose instead of
"Delete Folder" — deletion and un-nesting-while-keeping-tabs are two
different, deliberately separate commands in Zen, not one command with two
outcomes.

---

## 6. Collapse state and counts

**Collapse state is persisted, via Firefox's own SessionStore — not a
bespoke Zen store.** `ZenFolders.mjs` has matched
`storeDataForSessionStore()` (1201-1270) / `restoreDataFromSessionStore()`
(1272-1388) methods that (de)serialize each folder's `collapsed` flag
alongside its id, label, `parentId` (for reconstructing nesting — see the
"Nesting folders into each other according to `parentId`" loop at
1339-1377), `prevSiblingInfo` (for exact re-insertion position), and other
folder metadata:

```js
storedData.push({
  pinned: folder.pinned,
  essential: folder.essential,
  splitViewGroup: folder.hasAttribute("split-view-group"),
  id: folder.id,
  name: folder.label,
  collapsed: folder.collapsed,
  saveOnWindowClose: folder.saveOnWindowClose,
  parentId: parentFolder ? parentFolder.id : null,
  prevSiblingInfo,
  emptyTabIds: emptyFolderTabs,
  userIcon: userIcon?.getAttribute("href"),
  isLiveFolder: folder.isLiveFolder,
  workspaceId: folder.getAttribute("zen-workspace-id"),
});
```

This plugs into Firefox's stock `SessionStore` module (the same mechanism
that restores tabs/windows across restarts), so collapse state — and the
whole nesting tree — genuinely survives a browser restart, on disk, not
just in memory. (I did not separately trace the exact
`sessionstore.jsonlz4`-level plumbing beyond confirming this data is fed
into/read from `SessionStore`; see §7 "could not verify".)

**No content-count badge is shown, by explicit design.** Stock Firefox tab
groups have a "+N" overflow-count badge (`.tab-group-overflow-count`,
shown when a horizontal group's tabs don't all fit) — Zen's own
`src/zen/folders/zen-folders.css` forcibly disables it for folders:

```css
.tab-group-overflow-count-container {
  display: none !important;
}
```

I found no other counter, direct-children or subtree-total, rendered
anywhere for a folder in the CSS or the two folder JS modules. **Zen
folders do not display a "contains N items" count at all** — neither a
direct-children count nor a whole-subtree count.

---

## 7. Things a straightforward reimplementation would plausibly get wrong

- **Folders are pinned-only, always** — any tab (pinned or not) added to a
  folder gets force-pinned, and folders live in the pinned-tabs container.
  A design that lets a folder hold "Today" (unpinned) tabs in their
  unpinned form diverges from Zen immediately.
- **Essentials are actively stripped on entry** — dragging a Favourite/
  Essential into a folder silently un-favourites it first
  (`gZenPinnedTabManager.removeEssentials`), rather than refusing the drop
  or letting a tab be both essential and folder-contained.
- **"Delete" and "keep the tabs but remove the folder" are two separate
  commands**, not one action with a choice dialog — "Delete Folder" always
  closes every contained tab (recursively into subfolders); "Unpack
  Folder" is the only way to keep them, and it un-nests everything in the
  folder in one shot (no per-tab "take this one out" menu item was found —
  only drag, or unpack-everything).
- **The depth cap is enforced twice, independently, by `level` arithmetic
  — not by walking ancestors.** A reimplementation that computes "current
  depth" by literally walking up through `parentFolder` chains each time
  will behave equivalently, but Zen's actual check is a stored/derived
  `level` getter compared against a constant in two unrelated call sites
  (context menu and DnD); if a reimplementation only guards one of "create
  subfolder" and "drag a folder into another," it'll diverge from Zen's
  double-gated approach.
- **Hitting the depth cap during drag-and-drop does not refuse the drop
  outright** — Zen quietly retargets the *drag* to the folder's own parent
  group (`element = element.group`) instead of doing nothing. A naive
  implementation that just blocks the drop entirely (no visual result at
  the depth boundary) will feel different from Zen's fallback-to-parent
  behavior.
- **The always-present placeholder "empty tab"** is a structural, easily
  missed detail: every non-live folder appears to always carry one hidden
  pinned placeholder tab (`_forZenEmptyTab`) that isn't part of the
  folder's "real" content and is explicitly filtered out in several
  utility methods, and is why "the folder has 2 items" for what looks like
  1 real tab is correct, not a bug — this is easy to omit and then be
  surprised when Firefox's own group-cleanup machinery deletes a folder
  the instant its one real tab closes.
- **Collapsed does not mean hidden-and-unloaded.** Collapse is a pure
  visual/CSS state; tabs keep running. Unloading is a separate, explicit,
  opt-in action reachable only via the folder's reset-icon button. A
  reimplementation that ties "collapsed" to any kind of tab
  discard/suspend would not match Zen.
- **A folder that becomes empty (via unpack, drag-out, or its last real
  tab closing) is never auto-deleted** — it just sits there, empty, until
  the user explicitly deletes it. If a reimplementation auto-removes empty
  folders, that's a deliberate divergence to be aware of, not an oversight
  to "fix" toward Zen.
- **No item-count badge anywhere** — Zen actively suppresses the stock
  Firefox tab-group overflow-count for folders. Any folder UI that shows a
  number badge (direct-child count or subtree count) is Arcium's own
  design choice with no Zen precedent to match against either way.
- **Folders/spaces interaction**: a folder can be converted wholesale into
  its own Space via `#convertFolderToSpace()` (`ZenFolders.mjs:588-639`,
  bound to `context_zenFolderToSpace`) — all of the folder's non-empty-tab
  tabs get moved into the new Space's pinned-tabs container and the folder
  itself is deleted. Separately, `changeFolderToSpace()`
  (`ZenFolders.mjs:641-677`) lets a folder (and, by walking `folder.tabs`,
  its subfolders) be re-parented to a *different existing* workspace/Space
  without being unpacked — every tab in the tree gets its
  `zen-workspace-id` attribute rewritten. Both are folder-level operations
  with no direct analogue mentioned in Arcium's spec as of this writing;
  worth knowing they exist as "what Zen does with folders across spaces"
  precedent.

---

## What I could not verify

- **Exact placement rule for `ungroupTab()`.** This is the stock-Firefox
  method both "Unpack Folder" and unpinning route through to detach a tab
  from its group. It is called in Zen's patches but never redefined by
  them — I could not find its definition inside the `zen-browser/desktop`
  repository (it lives in unpatched, stock `tabbrowser.js`/tab-groups
  code upstream in mozilla-central) and did not fetch mozilla-central to
  pin down precisely where a just-ungrouped tab is inserted (immediately
  after the group at its old level? at the top level regardless of
  nesting?). Everything I found about "unpack" and "drag out" landing
  position is accurate for the *drag* path (§3/§4, which is fully Zen's
  own code and lands at the drop point), but the *unpack-all* and
  *unpin-a-tab* paths inherit stock behavior I did not independently
  confirm.
- **Own-descendant drop protection.** I found no explicit "is this a
  descendant of what I'm dragging" guard in Zen's own JS. I believe (but
  did not prove) this is implicitly caught by the DOM's native
  `HierarchyRequestError` when a move operation tries to insert a node
  into its own descendant — I did not run the browser to confirm what
  actually happens (silent failure, thrown/caught exception, or a visible
  bug) if a user attempts this drag.
- **SessionStore's on-disk format for folder collapse/nesting data.** I
  confirmed `ZenFolders.mjs` produces and consumes a structured JS object
  per folder that includes `collapsed` and `parentId`, and that this
  round-trips through Firefox's `SessionStore` module — but I did not
  trace the on-disk `sessionstore.jsonlz4` schema itself, nor watch an
  actual restart happen.
- **Precise behavior when a folder's last real tab is closed via the tab's
  own close button (not drag, not unpack).** I confirmed the empty-tab
  placeholder likely exists to prevent this from auto-deleting the folder
  (§2), but did not find or read a `TabClose`/group-auto-remove handler
  to confirm the mechanism directly — this is inference from the
  placeholder's existence and cleanup call sites, not a read of the
  removal-trigger code itself.
- **Live folders (RSS/GitHub-backed) were only checked where they affect
  the seven questions above** (drop refusal, §3). Their own content model,
  refresh behavior, and item-identity scheme (`zen-live-folder-item-id`)
  were out of scope for this pass and not otherwise verified.
- **Visual appearance in a running build.** As with the prior URL-bar/
  extensions research, everything above is read from source (JS logic,
  CSS rules, browser-test assertions), not observed in a running browser
  — animation feel, exact pixel behavior of the drag indicators, and
  platform-specific quirks were not visually confirmed.

---

## What this means for Arcium

- **Nesting depth cap: Arcium's choice already matches Zen's default.**
  Zen's `zen.folders.max-subfolders` defaults to `5`, and its enforcement
  produces exactly five usable folder levels (`level` 0 through 4) — the
  same number Arcium has chosen for its own five-level cap. This is
  agreement, not a divergence to reconcile; the only difference worth
  naming is *mechanism*, not *number*: Zen enforces the cap via a stored
  `level` getter checked in two separate call sites (menu-disable and
  drag/drop-retarget) rather than a single recursive ancestor-walk, and
  Zen's drag/drop gate retargets the drag to the parent instead of
  refusing the drop outright.
- **Deleting a folder: Arcium's stated behaviour (reparent entries and
  subfolders to the grandparent) differs from Zen's (close every tab in
  the folder and its subfolders; nothing is reparented).** In Zen, the
  only way to keep a folder's tabs while removing the folder is a
  separate "Unpack Folder" command, which un-nests *everything at once*
  to the folder's immediate parent level (not the grandparent, and not
  a per-item choice) rather than being a side effect of "Delete." If
  Arcium's "delete reparents to grandparent" is meant to be its
  deletion behavior, that's a deliberate departure from Zen, where
  deletion is destructive and un-nesting is a separate, non-destructive
  action one level at a time.
- **Folder item count: Arcium's assumption that a folder shows a count
  (whole-subtree total vs. direct children) has no Zen precedent either
  way, because Zen shows no count at all** — it explicitly force-hides
  the stock Firefox tab-group overflow-count badge for folders
  (`.tab-group-overflow-count-container { display: none !important; }`).
  Whichever of "direct children" or "whole subtree" Arcium picks, it is
  choosing UI Zen doesn't have, not diverging from a specific Zen
  behaviour that could be matched.
- Two further, smaller differences worth flagging even though not asked
  about explicitly: Zen strips Essential/Favourite status from a tab the
  moment it enters a folder (favourites and folder-membership are
  mutually exclusive in Zen), and Zen folders only ever hold pinned tabs
  (any tab entering a folder gets force-pinned) — if Arcium intends
  folders to hold "Today"/unpinned tabs, or to let a tab be both a
  Favourite and folder member, both are departures from Zen's model, not
  just implementation details.

---

**Source root for all citations above:** https://github.com/zen-browser/desktop
(branch `dev`). Key files, for quick reference:
- `src/zen/folders/ZenFolder.mjs`
- `src/zen/folders/ZenFolders.mjs`
- `src/zen/folders/zen-folders.css`
- `src/zen/drag-and-drop/ZenDragAndDrop.js`
- `src/zen/tabs/zen-tabs/vertical-tabs.css`
- `src/browser/components/tabbrowser/content/tabgroup-js.patch`
- `src/browser/components/tabbrowser/content/tabbrowser-js.patch`
- `src/browser/base/content/zen-panels/popups.inc`
- `prefs/zen/folders.yaml`, `prefs/zen/zen.yaml`
- `src/zen/tests/folders/browser_folder_max_subfolders.js`
- `src/zen/tests/folders/browser_folder_create.js`
- `src/zen/tests/folders/browser_folder_subfolder.js`
- `src/zen/tests/folders/browser_folder_empty_tab.js`
- `src/zen/tests/folders/browser_folder_density.js`
- `src/zen/tests/folders/head.js`
