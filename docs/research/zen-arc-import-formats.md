# Import formats: Arc's StorableSidebar.json and Zen's zen-sessions.jsonlz4

Reference for a C++ importer that recreates spaces, pinned tabs, favourites and folders from Arc
(macOS) and from Zen (macOS). Compiled 2026-09-24 from public sources only; no file under
`~/Library` on this machine was read.

How to read the confidence marks:

- **Source** means the claim comes from the browser's own code (Zen and Firefox are open source).
- **Observed** means the claim comes from third-party parsers, test fixtures or write-ups. Arc is
  closed source, so every Arc claim is at best Observed. Where several independent projects agree,
  the number of agreeing sources is given.
- **UNCONFIRMED** means no source settled it. These are also collected at the end of each part.

Versions the Source claims were read at: Zen `desktop` commit `7dfcd34` (2026-09-23, Zen 1.22.3b on
Firefox 156.0.1, see [zen-surfer]); Firefox tag `FIREFOX_156_0_1_RELEASE`. Link definitions are at
the bottom of the file.

---

## 1. Arc: `~/Library/Application Support/Arc/StorableSidebar.json`

### 1.1 Location and neighbours

- The file sits directly in `~/Library/Application Support/Arc/`; Arc also writes timestamped
  copies such as `StorableSidebar.2026-02-11-20-47-39-018.json` in the same folder. Observed
  ([boom] L8-L33, [ice-dm] "Related files", [arcexport] L147-L150, [arc2zen-paths] L27-L28).
- The file is plain UTF-8 JSON, with `"key" : value` spacing and forward slashes written as `\/`.
  Both are legal JSON, so a conforming parser needs no special handling. Observed ([ice-dm] "Known
  quirks", [refrax] L86-L95, [vrag] L153).
- Chromium-level per-profile data lives in `~/Library/Application Support/Arc/User Data/<dir>`,
  where `<dir>` is `Default`, `Profile 1`, `Profile 2` and so on. Observed ([boom] L13-L21,
  [arc2zen-paths] L104-L116, [mhadi-conftest] L226-L250). `User Data/Local State` has the usual
  Chromium `profile.info_cache` naming each directory; a test fixture also shows an internal
  `__ARC_SYSTEM_PROFILE` entry, which is not a user profile. Observed ([mhadi-conftest] L236-L247).
- `StorableProfiles.json` is described as "profile definitions" ([boom] L198-L212). Its contents
  are **UNCONFIRMED**; nothing below depends on it.

### 1.2 Top level

```json
{
  "version": 1,
  "sidebar": { "containers": [ { "global": {} }, { "spaces": [...], "items": [...], "topAppsContainerIDs": [...] } ] },
  "sidebarSyncState": { "container": {...}, "spaceModels": [...], "items": [...], ... },
  "firebaseSyncState": { "syncData": { "orderedSpaceIDs": {...}, "spaceModels": [...], "items": [...] }, ... }
}
```

- `version` is `1` in every fixture seen. Observed, 3 fixtures ([adlio-fixture], [jtotty-fixture],
  [zarrar-fixture]).
- `sidebar` is the authoritative local state. `sidebarSyncState` is a CloudKit mirror whose
  `items` and `spaceModels` hold `{ "value": <same object>, "encodedCKRecordFields": "<base64
  bplist>" }` wrappers; `firebaseSyncState.syncData` is a second mirror whose wrappers are
  `{ "id", "lastChangeDate", "lastChangedDevice", "value" }`. Observed ([ice-dm] "Sync mirror",
  [arcmcp] L35-L62, [adlio-fixture]). Read `sidebar` and ignore both mirrors: the mirrors can be
  empty (`{}` in [jtotty-fixture]) or stale, and [phi-parser] L13-L45 prefers `sidebar` and uses
  `sidebarSyncState` only to fill gaps.
- Note that [arc2zen-extract] L94-L150 reads space titles, icons and profiles from
  `firebaseSyncState.syncData.spaceModels`. That mirror was empty in two of three fixtures
  ([adlio-fixture], [zarrar-fixture] show `orderedSpaceIDs.value: []`), so do not copy that choice.

### 1.3 Interleaved arrays

Arc serialises ordered dictionaries as flat arrays `[key, value, key, value, ...]`. This applies
to `sidebar.containers`, `spaces`, `items`, `topAppsContainerIDs`, `containerIDs`,
`newContainerIDs` and the mirrors' `spaceModels`/`items`. Observed, 8+ sources ([boom] L55-L58,
[ice-dm] "Alternating list format", [arcae] L18-L36, [crest] L92-L112, [phi-parser] L50-L73,
[mhadi-sidebar] L152-L171, [arcmcp] L216-L227, [leohku] L4-L10).

- In `spaces` and `items` the key is the id string and the value is an object that also carries
  `id` (the two are equal). Observed ([arcmcp] L216-L227, [adlio-fixture]).
- In `sidebar.containers` the key is itself an object: `{"global": {}}`, `{"littleBrowser": {"_0":
  "<uuid>"}}` or `{"standalone": {"_0": "<uuid>"}}`, and the value is that container's data. The
  main sidebar is **the object immediately after `{"global": {}}`**. Observed: a public sample
  holds ten entries, four key/value pairs for `littleBrowser`/`standalone` before the `global`
  pair ([zarrar-fixture] L20-L300); [arcexport] L174-L181 selects "the container after `global`";
  [mhadi-sidebar] L133-L150 and [xiaogliu] L106-L109 instead take "the first container with
  `items` and `spaces`", which would pick a Little Arc window's container in that sample. What
  `standalone` containers are is **UNCONFIRMED**; skip them.
- Parse defensively: walk in steps of two, require the key to be a string (or, for containers and
  favourites, an object), and skip a malformed pair instead of failing ([arcae] L20-L36).

### 1.4 Spaces

A space object (value in `spaces`):

| Field | Meaning | Evidence |
|---|---|---|
| `id` | Space id. Usually an uppercase UUID, but Arc's first space can use `thebrowser.company.defaultPersonalSpaceID`, so treat ids as opaque strings. | Observed ([adlio-fixture] L338-L342, [ice-dm] "Known quirks") |
| `title` | Space name; may be absent or empty, so provide a fallback name. | Observed ([arcexport] L198-L202, [phi-parser] L218-L222) |
| `profile` | Which Arc profile the space uses, see below. | Observed, 6 sources |
| `containerIDs` | Interleaved `[marker, containerId]` with string markers `"pinned"` and `"unpinned"`. | Observed, 6 sources |
| `newContainerIDs` | The same pairs with object markers: `{"pinned": {}}` and `{"unpinned": {"_0": {"shared": {}}}}` (a test fixture also uses `{"unpinned": {}}`). | Observed ([adlio-fixture], [jtotty-fixture], [arcmcp] L181-L189, [sumi] L495-L513, [mhadi-conftest] L163-L173) |
| `customInfo.iconType` | Space icon, see below. | Observed, 5 sources |
| `customInfo.windowTheme` | Space colour theme, see below. | Observed ([phi-parser] L688-L765, [phi-tests] L60-L92) |

**Pinned and unpinned containers.** Read the marker, never the position. Real files have both
orders: `["pinned", P, "unpinned", U]` ([adlio-fixture]) and `["unpinned", U, "pinned", P]`
([boom] L128-L138; [leohku] L109-L116 hard-codes index 1 as unpinned and index 3 as pinned). A space
may have only an unpinned container ([zarrar-fixture], first space). Each container id names an
item whose `data.itemContainer.containerType.spaceItems._0` is the space id ([boom] L100-L115,
[ice-dm] "data.itemContainer subfields"). The pinned container holds the space's pinned tabs and
folders; the unpinned container holds its "Today" tabs, the ones Arc auto-archives ([ice-dm] "Tree
navigation", [phi-tests] L135-L150). Prefer `containerIDs` and fall back to `newContainerIDs`
([axis] L143-L156 checks `newContainerIDs` first; [refrax] L188-L205 assumes `newContainerIDs` is
`[{"pinned": "<id>"}]`, a shape not seen in any fixture, so do not copy it). What `"shared"` in the
unpinned marker distinguishes is **UNCONFIRMED**.

**Profile.** Three shapes are observed:

```json
"profile": { "default": true }
"profile": { "default": {} }
"profile": { "custom": { "_0": { "directoryBasename": "Profile 1", "machineID": "<UUID>" } } }
```

- `{"default": true}` in [jtotty-fixture], [zarrar-fixture], [phi-tests]; `{"default": {}}` in
  [adlio-fixture] and [mhadi-conftest] L167. Test for the **presence** of the `default` key, not
  its value: [arcae] L67-L75 requires `=== true` and [phi-parser] L465-L468 decodes a Bool, so both
  would misread `{}`.
- A default space uses `User Data/Default`; a custom one uses `User Data/<directoryBasename>`.
  The literal `"Default"` never appears in the JSON ([phi-parser] L443-L457, [arc2zen-extract]
  L114-L123, [axis] L65-L74).
- Validate `directoryBasename` as one path component (not empty, no `/` or `\`, not `.` or `..`)
  before joining it to a path ([phi-parser] L470-L486).
- A space with no `profile` key is treated as default by [phi-parser] L677-L681. Whether Arc ever
  omits the key is **UNCONFIRMED**.
- `machineID` identifies the Mac that created the profile. Because Arc syncs the sidebar, the file
  can carry profile tags from other Macs ([mhadi-sidebar] L235-L260). How to learn this Mac's
  `machineID` is **UNCONFIRMED**; see 1.6 for the consequence.

**Icon** (`customInfo.iconType`), three shapes observed:

```json
{ "emoji_v2": "<emoji text>", "emoji": 127969 }   // text plus leading code point (older field)
{ "emoji": 127970 }                                // older data: code point only
{ "icon": "planet" }                               // one of Arc's named icons
```

Prefer `emoji_v2`, then the `emoji` integer as a Unicode scalar (reject values that are not valid
scalars, such as surrogates), then `icon`. `customInfo` or `iconType` can be missing or `null`.
Observed ([phi-parser] L726-L751, [phi-tests] L178-L205, [adlio-fixture], [arc2zen-icons] L28-L40,
[orbit-tests] L540-L552). The full list of named icons is **UNCONFIRMED**.

**Colour** (`customInfo.windowTheme`), optional. Arc encodes enums as `{"<case>": {"_0": payload}}`.
The space colour is `windowTheme.background.single._0.style.color._0.blendedSingleColor._0.color`
or, for gradients, the first of `...blendedGradient._0.baseColors`. Each colour is
`{red, green, blue, alpha, colorSpace: "extendedSRGB"}` with components that can fall outside 0-1
and must be clamped. Observed from verbatim captures ([phi-parser] L688-L765, [phi-tests]
L60-L92). [arc2zen-extract] L125-L139 reads `windowTheme.primaryColorPalette.midTone` instead;
both keys exist in [adlio-fixture]. Which one Arc shows as "the" space colour is **UNCONFIRMED**.

**Order of spaces.** Use the order of the `spaces` array ([phi-tests] L111-L119 asserts sidebar
order equals array order). `sidebarSyncState.container.value.orderedSpaceIDs` also exists and
matches the array in [adlio-fixture]; [arcmcp] L216-L233 appends to both when it creates a space.
Which one wins when they disagree is **UNCONFIRMED**.

### 1.5 Items

Every value in `items` has these fields (Observed, [boom] L60-L115, [ice-dm] "Item shape",
[adlio-fixture]):

| Field | Meaning |
|---|---|
| `id` | Item id (opaque string; usually uppercase UUID). |
| `parentID` | Parent item id, or `null` for containers (sidebar roots). |
| `childrenIds` | Ordered child ids. This is the display order. |
| `title` | User-set title, or `null`. For tabs it overrides `savedTitle`. |
| `createdAt` | Seconds since 2001-01-01 UTC (Core Data epoch), float. |
| `isUnread`, `originatingDevice` | Not needed for import. |
| `data` | An object with exactly one key naming the item kind. |

Kinds of `data` seen so far:

| `data` key | What it is | Import | Evidence |
|---|---|---|---|
| `tab` | A tab (pinned, Today or favourite, depending on the container above it). | yes | Observed, 10+ sources |
| `list` | A folder. `data.list` is `{}`; the name is the item's `title`. Nested folders are `list` items whose `parentID` is another `list`. | yes | Observed ([boom] L88-L99, [ice-dm], [adlio-fixture]) |
| `itemContainer` | Root of a space section (`containerType.spaceItems._0` = space id) or of a favourites row (`containerType.topApps._0` = profile tag). `parentID` is `null`. | as root | Observed ([ice-dm], [adlio-fixture], [mhadi-sidebar] L235-L272) |
| `splitView` | A split view; `childrenIds` are its tabs. | yes | Observed ([phi-parser] L159-L207, [phi-tests] L322-L336, [mhadi-conftest] L203) |
| `arcDocument` | An Arc Note: `{ "arcDocumentID": "<id>" }`. | skip | Observed ([leohku] L16-L33) |
| `easel` | An Easel: `{ "easelID": "<id>", "title": ... }`. | skip | Observed ([leohku] L16-L37) |
| `welcomeToArc` | Onboarding item, e.g. `{ "timeLastActiveAt": ..., "tabType": "legacy" }`. | skip | Observed ([jtotty-fixture]) |

Skip any other key rather than failing. Whether notes and easels can hold children is
**UNCONFIRMED**; [leohku] L30-L37 treats them as leaves.

**Tab** (`data.tab`), keys observed ([boom] L65-L87, [adlio-fixture], [jtotty-fixture],
[arcmcp] L83-L90):

| Key | Meaning |
|---|---|
| `savedURL` | The tab's address. The pinned address for a pinned tab. |
| `savedTitle` | Page title Arc last saw. |
| `savedMuteStatus` | e.g. `"allowAudio"`. |
| `timeLastActiveAt` | Seconds since 2001-01-01 UTC. |
| `activeTabBeforeCreationID`, `referrerID` | Ids of other items; not needed. |

- Display title: the item's `title` if non-empty, else `savedTitle`, else the URL. This order is
  used by [arcexport] L237-L238, [arcae] L77-L83, [refrax] L307-L308 and matches [boom] L70
  ("custom title override or null"). [phi-parser] L105 prefers `savedTitle`, which would lose a
  user's rename.
- `savedURL` can be an empty string ([mhadi-conftest] L217 "Dangling"); skip such tabs.
  [ice-dm] mentions a `currentURL` fallback; it does not appear in any fixture (**UNCONFIRMED**).
- **Favicon: no field for it has been seen in any tab.** [arc2zen-fav] L47-L96 gets icons from the
  profile's Chromium `Favicons` SQLite (`icon_mapping.page_url` joined to `favicon_bitmaps`), for
  each of `User Data/Default` and `User Data/Profile */` ([arc2zen-paths] L104-L116). That no
  favicon field exists anywhere is **UNCONFIRMED** (absence across four fixtures only).

**Folder** (`data.list`): the name is `title` (fall back to "Untitled folder"); children are
`childrenIds` in order. Keeping or dropping empty folders is a policy choice: [arcae] L113-L123
drops them and [phi-parser] L374-L378 drops empty untitled ones.

**Split view** (`data.splitView`), captured from a real file:

```json
{ "splitView": { "layoutOrientation": "horizontal", "itemWidthFactors": [], "customInfo": null, "focusItemID": null } }
```

- Its `childrenIds` are the tabs it shows together, in order; Arc allows up to four
  ([phi-parser] L161-L166).
- `layoutOrientation` names the axis the panes run along: `"horizontal"` is side by side,
  `"vertical"` is stacked ([phi-parser] L197-L207).
- Split views appear in pinned sections, inside folders, in the favourites row and in Today
  sections ([phi-tests] L322-L440, [mhadi-conftest] L199-L205).
- What `itemWidthFactors` holds when non-empty (presumably per-pane width fractions) and what
  `focusItemID` points at are **UNCONFIRMED**.

### 1.6 Favourites (`topAppsContainerIDs`)

- `topAppsContainerIDs` in the main container is interleaved `[profileTag, containerId, ...]`,
  where `profileTag` uses the same shapes as a space's `profile` ([phi-parser] L249-L276,
  [arcae] L182-L198, [adlio-fixture], [zarrar-fixture]).
- Each `containerId` is an item with `data.itemContainer.containerType.topApps._0` equal to the
  same profile tag, `parentID: null`, and the favourites as `childrenIds` in order
  ([mhadi-sidebar] L235-L290, [arc2zen-extract] L464-L480, [adlio-fixture]).
- Favourites belong to a **profile**, not to a space: every space on that profile shows the same
  row ([arcae] L182-L208, [plainspace README] "Arc stores favorites per Chrome profile"). This
  maps directly onto a model where favourites belong to a profile.
- Children are tabs and split views ([phi-tests] L373-L380). Whether a favourites row can hold a
  folder is **UNCONFIRMED** ([mhadi-sidebar] L290-L307 flattens folders just in case).
- One profile can have **several** favourites containers, one per `machineID`, some of them empty
  ([adlio-fixture] has three for `Profile 1`/`Profile 2` with two `machineID`s;
  [mhadi-sidebar] L247-L250). Match a space to its favourites on `directoryBasename` plus
  `machineID` first ([arcae] L67-L75, [crest] L119-L130), and if that finds nothing or an empty
  row, fall back to the union of all containers with the same `directoryBasename`, removing
  duplicate URLs ([mhadi-sidebar] L262-L272). The default profile's tag carries no `machineID`.
- A favourites container can appear without a preceding profile tag; [phi-tests] L282-L286 keeps
  it as "unknown profile".

### 1.7 Walk, in short

1. Parse JSON; find the container after `{"global": {}}` in `sidebar.containers`.
2. Build `id -> item` from `items` and `id -> space` from `spaces` (stride two).
3. For each space in `spaces` order: read `title`, `profile`, icon, colour; resolve the pinned and
   unpinned container ids from the markers.
4. Walk each container's `childrenIds` depth-first. `tab` becomes a tab, `list` a folder,
   `splitView` a split of its child tabs; skip every other kind. Guard against cycles and missing
   ids ([arcae] L93-L127 carries a `seen` set; [mhadi-sidebar] L309-L313 caps depth at 256).
5. For favourites, walk each `topAppsContainerIDs` container and attach it to the profile.
6. Convert `createdAt`/`timeLastActiveAt` with `unix = arc + 978307200` ([boom] L164-L173).

### 1.8 Arc: UNCONFIRMED

- What `standalone` and `littleBrowser` containers are for (skip them).
- What `"shared"` means in `{"unpinned": {"_0": {"shared": {}}}}`.
- Whether `orderedSpaceIDs` or the `spaces` array order wins when they differ.
- Whether a tab ever carries a favicon field; none seen.
- How to learn this Mac's `machineID` to pick the right favourites container.
- The full set of `data` kinds, named space icons, and `itemWidthFactors` semantics.
- The contents of `StorableProfiles.json`.

---

## 2. Zen (Firefox-based): spaces, pinned tabs, Essentials, folders, containers

### 2.1 Where the files are

- Zen's application folder on macOS is `~/Library/Application Support/zen`, with `profiles.ini`,
  `installs.ini` and a `Profiles/` directory. Observed, 2 tools ([arc2zen-paths] L119-L175,
  [zb-session] L47-L66); the binary name is `zen` in [zen-surfer] L1-L5, but the path itself was not
  traced through Zen's source (**UNCONFIRMED** at Source level).
- `zen-sessions.jsonlz4` is in the profile directory. Source: `FILE_NAME = "zen-sessions.jsonlz4"`
  joined to `PathUtils.profileDir` ([zen-sm-file] L44-L48, L129-L131).
- Zen also keeps `zen-sessions-backup/clean.jsonlz4`, hourly-bucketed
  `zen-sessions-backup/zen-sessions-YYYY-MM-DD-HH.jsonlz4` (up to `zen.session-store.max-backups`,
  default 20, [zen-sm-prefs] L31-L42) and
  `zen-sessions-backup/recovery.baklz4`; it falls back to the first two when the main file is
  empty or unreadable. Source ([zen-sm-init] L101-L152, [zen-sm-read] L254-L280, [zen-sm-save]
  L600-L700).
- Firefox's own session files are `sessionstore.jsonlz4` (clean shutdown),
  `sessionstore-backups/recovery.jsonlz4` and `recovery.baklz4` (while running),
  `sessionstore-backups/previous.jsonlz4` and `upgrade.jsonlz4-<buildid>`. Source
  ([ff-sessionfile] L70-L122).
- `containers.json` is in the profile directory. Source ([ff-cis-path] L1061-L1065).

**Which file to read.** Read `zen-sessions.jsonlz4`. It is the sidebar model that Zen restores
into every window ([zen-sm-restore] L801-L844), it is written on every session save through a
temp file plus rename ([zen-sm-save] L600-L630, [ff-jsonfile] L419-L437), and window sync is on by
default ([zen-window-sync-pref] L5-L6). Firefox's `recovery.jsonlz4` holds the same data per
window as `windows[i].tabs/groups/folders/spaces/splitViewData/activeZenSpace`
([zen-ss-collect] L262-L285), and Zen itself only reads it for a one-time migration
([zen-sm-migrate] L219-L246). Use it only if `zen-sessions.jsonlz4` is missing, and then take the
first normal window.

### 2.2 profiles.ini and the default profile

```ini
[Install2656FF1E876E9973]
Default=Profiles/abcd1234.Default (release)
Locked=1

[Profile0]
Name=Default (release)
IsRelative=1
Path=Profiles/abcd1234.Default (release)
Default=1

[General]
StartWithLastProfile=1
Version=2
```

- The profile this installation actually starts is the one named by `Default=` in the
  `[Install<hash>]` section; its value is the profile's `Path=` descriptor (relative to the app
  folder when `IsRelative=1`). Source ([ff-prof-install] L1125-L1152, [ff-prof-loop] L1244-L1270,
  [ff-prof-setdefault] L1394-L1419).
- `Default=1` in a `[ProfileN]` section is the older "normal default", used when an install has no
  dedicated profile; if there is exactly one non-dev-edition profile it becomes the normal default
  automatically. Source ([ff-prof-loop] L1263-L1289, [ff-prof-setdefault] L1380-L1393).
- `<hash>` is the uppercase hex (no zero padding) of CityHash64 over the UTF-16 install directory,
  the folder holding the executable, e.g. `/Applications/Zen.app/Contents/MacOS` with its case
  normalised. Source ([ff-hash] L844-L899, [ff-hash2] L404-L419). Computing it needs a CityHash64
  implementation, so a practical rule is: if there is one `Install` section use it; if several,
  prefer the one whose profile has the newest `zen-sessions.jsonlz4`, and let the user choose.
- `installs.ini` repeats the `Install` sections for older Firefox versions; with `Version=2`,
  `profiles.ini` is authoritative. Source ([ff-prof-ini] L1050-L1122, [ff-prof-installs-write]
  L2795-L2810). [arc2zen-paths] L143-L174 reads `installs.ini` first, then `Default=1`.

### 2.3 Top level of zen-sessions.jsonlz4 (after decompression)

```json
{ "lastCollected": 1758700009999, "tabs": [...], "folders": [...], "splitViewData": [...], "groups": [...], "spaces": [...] }
```

Source: `#collectWindowData` and `#collectTabsData` ([zen-sm-collect] L735-L792). `lastCollected`
is `Date.now()` in milliseconds. `tabs` is deduplicated across windows by `zenSyncId`; `folders`,
`splitViewData`, `groups` and `spaces` come from the first window. Any of the arrays can be absent
or `undefined` in a young or odd file; treat a missing array as empty ([zb-sidebar] L178-L186 does
the same for `splitViewData`).

### 2.4 Spaces (`spaces[]`)

| Field | Meaning | Evidence |
|---|---|---|
| `uuid` | Space id, an `nsID` string with braces, e.g. `{a1b2c3d4-...}`. | Source ([zen-space-create] L2672-L2678, [zen-uuid] L327-L329) |
| `name` | Space name. | Source ([zen-space-create]) |
| `icon` | An emoji string, or `chrome://browser/skin/zen-icons/selectable/<name>.svg`, or absent. | Source ([zen-icon] L301-L303, [zen-space-create] L2666-L2668); emoji Observed ([arc2zen-icons] L28-L40) |
| `theme` | `{ "type": "gradient", "gradientColors": [...], "opacity": 0.5, "texture": 0 }`; each colour is `{ c: [r,g,b] or CSS string, isCustom, algorithm, isPrimary, lightness, position, type }`. | Source ([zen-theme] L1433-L1440, L1930-L1960) |
| `containerTabId` | The container (`userContextId`) the space opens tabs in; `0` means none. | Source ([zen-space-create] L2656-L2678, [zen-ess-section] L392-L427) |
| `hasCollapsedPinnedTabs` | Session-only UI state; ignore. | Source ([zen-space-ss] L773-L785) |

- Space order is the order of the array; reordering splices the cached list and that list is what
  gets saved. Source ([zen-space-reorder] L1510-L1540, [zen-space-ss]).
- Older migrated data may still carry `position`; it is not written by current code
  ([zen-sm-migrate] L171-L186). Ignore it.

### 2.5 Tabs (`tabs[]`)

A tab is Firefox's session-store tab object plus Zen fields.

| Field | Meaning | Evidence |
|---|---|---|
| `entries[]` | Session history. Each entry has `url` and `title` among many internal keys (`ID`, `docshellUUID`, `triggeringPrincipal_base64`, `cacheKey`, `persist`, ...). | Source ([ff-tabstate-index] L220-L236) |
| `index` | **1-based** index of the current entry in `entries`; if absent the last entry is current. It is not a tab position. | Source ([ff-tabstate-index] L228-L230, [ff-tb-index] L4948-L4956, [zen-pinned-state] L1239-L1243 "starting from 1") |
| `pinned` | `true` for pinned tabs, for every tab inside a folder, and for Essentials. | Source ([ff-tabstate] L71-L78, [zen-tabstate] L17-L20) |
| `zenEssential` | `true` for an Essential. | Source ([zen-tabstate] L17-L20) |
| `zenWorkspace` | Owning space's `uuid`; `null` for Essentials. | Source ([zen-tabstate] L17, [zen-add-ess] L546-L548) |
| `zenSyncId` | Stable tab id, `"<ms timestamp>-<uuid without braces>"`. Also the tab element's DOM id, which folder and split data refer to. | Source ([zen-tabstate] L18, [zen-sync-id] L324-L334) |
| `_zenPinnedInitialState` | `{ "entry": { "url", "title" }, "image" }` for pinned tabs: the address the pin goes back to, and its icon. Trimmed to just `url` and `title` on load. | Source ([zen-pinned-state] L1232-L1285, [zen-pinned-trim] L296-L313) |
| `zenStaticLabel` | Title the user gave the tab; absent when not renamed. | Source ([zen-tabstate] L24, [zen-restore-initial] L27-L29) |
| `image` | Favicon as a URL string (http(s) or `data:`), or `null`; the user's custom icon when `zenHasStaticIcon` is true. | Source ([ff-tabstate] L131-L136, [zen-tabstate] L45-L48) |
| `userContextId` | Container id, `0` for none. | Source ([ff-tabstate] L114) |
| `groupId` | Id of the **innermost** group holding the tab: a folder or a split view. Absent otherwise. | Source ([ff-tabstate] L91-L93) |
| `zenIsEmpty` | `true` for a folder's hidden `about:blank` placeholder; see 2.6. | Source ([zen-tabstate] L23, [zen-folders-create] L712-L721) |
| `zenIsGlance`, `zenGlanceId` | A Glance (peek) tab and its id. Skip Glance tabs. | Source ([zen-tabstate] L26-L27) |
| `zenLiveFolderItemId` | Set on items of a live folder (feed-driven). | Source ([zen-tabstate] L30) |
| `zenDefaultUserContextId`, `zenPinnedIcon`, `zenHasStaticIcon`, `_zenIsActiveTab` | UI state (`zenDefaultUserContextId` is the string `"true"` or `null`). | Source ([zen-tabstate] L21-L29) |
| `lastAccessed`, `hidden`, `attributes`, `searchMode`, `muted`, `extData`, `userTypedValue` | Firefox state; not needed. | Source ([ff-tabstate] L71-L155) |

**Address and title to import.** For a pinned tab use `_zenPinnedInitialState.entry.url`, else the
current entry (`entries[index-1]`); the current entry can differ when the user navigated away from
the pin. Title: `zenStaticLabel`, else the pinned entry's title, else the current entry's title,
else the URL. Observed practice agrees ([zb-sidebar] L194-L208); the `zenStaticLabel` step is
Source ([zen-restore-initial] L27-L29).

### 2.6 Folders (`folders[]`) and groups (`groups[]`)

A folder entry, written by `storeDataForSessionStore` ([zen-folders-store] L1209-L1278):

| Field | Meaning |
|---|---|
| `id` | Folder id, `"<ms timestamp>-<0..100>"`. Same id as its `groups` entry. ([zen-folder-node] L753-L766) |
| `name` | Folder name ("New Folder" by default). |
| `collapsed` | Whether it is closed. |
| `parentId` | Id of the enclosing folder, or `null` for a top-level folder. |
| `workspaceId` | Owning space `uuid` (Zen notes it only remembers this; the tabs' own `zenWorkspace` is what places them). |
| `prevSiblingInfo` | `{ "type": "start" | "tab" | "group", "id": <zenSyncId or folder id or null> }`, or `null`; the element just before the folder. |
| `emptyTabIds` | `zenSyncId`s of the folder's placeholder tabs. |
| `pinned` | Always `true` for a folder ([zen-folder-pinned] L207-L209). |
| `splitViewGroup` | `true` when the entry is a split view nested inside a folder, not a folder. |
| `saveOnWindowClose`, `userIcon`, `isLiveFolder`, `essential` | Optional: `userIcon` is a custom icon URL; `isLiveFolder` marks a feed-driven folder; `essential` is written from a property no folder in this source defines, so it is usually absent. |

The matching `groups` entry is Firefox's tab-group state with Zen's three additions:
`{ "pinned", "essential", "splitView", "id", "name", "color", "collapsed", "saveOnWindowClose" }`,
with `color: "zen-workspace-color"` for folders. Source ([zen-groupstate] L1-L15, [ff-groupstate]
L71-L79, [zen-folder-node] L769). `groups` holds one entry per folder and per split view,
because Zen's group list includes both ([zen-ss-collect], [zen-stored-tabs] L3252-L3265).

**How membership is expressed.** A tab says which folder it is in only through `groupId`, and only
for the innermost folder. A nested folder says which folder it is in through `parentId`. So the
folder path of a tab is: `groupId`, then follow `parentId` to the top. Source ([ff-tabstate]
L91-L93, [zen-folders-store] L1233, L1267, [zen-folders-restore] L1345-L1391). If `groupId`
names a split view that itself sits in a folder, that split view appears in `folders` with
`splitViewGroup: true` and a `parentId` ([zen-folders-store] L1233-L1237, L1262).

**Placeholder tabs.** Every folder Zen creates starts with a hidden pinned `about:blank` tab
(`zenIsEmpty: true`) whose `groupId` is the folder ([zen-folders-create] L712-L721), and its id is
listed in `emptyTabIds` ([zen-folders-store] L1238-L1240). Empty tabs are saved only when they are
in a group ([zen-sm-collect] L752-L754). The placeholder matters on restore: Firefox creates a
group only when a tab refers to it ([ff-tb-group] L4999-L5030, [zen-tb-restore] L554-L577) and Zen
turns only an existing group into a folder ([zen-folders-restore] L1298-L1330). For import: skip
`zenIsEmpty` tabs as content, but keep folders that hold only a subfolder.

**Nesting depth** is limited by `zen.folders.max-subfolders`, default 5 ([zen-folders-max]).

### 2.7 Order

- `tabs` is in sidebar order **within each section**, and sections are concatenated as: Essentials
  sections, then every space's pinned section (the active space first), then every space's normal
  section. Inside a folder, tabs appear in order with nested folders' tabs inline where the nested
  folder sits. Source: the saved tab list is `allStoredTabs` ([zen-ss-collect] L263-L264), built
  in that section order ([zen-stored-tabs] L3191-L3250, [zen-folder-tabs] L156-L170). So group tabs
  by (section, space) and keep their relative order; do not sort by `index`, which is a history
  index (third-party tools that sort by it, [zb-sidebar] L265, L286, and write positions into it,
  [arc2zen-sessions] L141, are wrong on this point).
- On restore Zen appends each tab to its section's fragment in array order and records that order
  as the tab position ([zen-tb-restore] L585, L603-L609).
- A **top-level folder** sits where its first tab (normally its placeholder) occurs in `tabs`,
  because the group node is created when the first tab naming it is met ([ff-tb-group]
  L5016-L5023, [zen-tb-restore] L566-L577).
- A **nested folder** is placed inside its parent right after `prevSiblingInfo` (a tab or a folder
  by id), or at the start of the parent (after the placeholder) when the type is `start` or the
  id is not found ([zen-folders-restore] L1345-L1391).
- Essentials' order is their order in `tabs`.

### 2.8 Split views (`splitViewData[]`)

```json
{ "groupId": "<group id>", "gridType": "vsep", "tabs": ["<zenSyncId>", ...],
  "layoutTree": { "type": "splitter", "direction": "row", "sizeInParent": 100,
                  "children": [ { "type": "leaf", "tabId": "<zenSyncId>", "sizeInParent": 50 }, ... ] } }
```

- Source ([zen-split-store] L2417-L2444). `gridType` is `vsep` (side by side, `row`), `hsep`
  (stacked, `column`) or `grid` ([zen-split-layout] L1639-L1670). At most 4 tabs
  ([zen-split-max] L87).
- The split's tabs carry `groupId` = the split's group id; the `groups` entry has `splitView: true`
  ([zen-groupstate], [zen-split-restore] L2446-L2466).
- Firefox 156 has its own split view (`tabData.splitViewId`, `window.splitViews`) ([ff-tabstate]
  L95-L97); Zen uses its own mechanism above.

### 2.9 Essentials and containers

- An Essential is a tab with `zenEssential: true`; it is always pinned and has no space
  (`zenWorkspace: null`). Source ([zen-tabstate] L19-L20, [zen-add-ess] L518-L570).
- Essentials are grouped by container: a restored Essential goes into the Essentials section for
  its `userContextId` ([zen-restore-container] L555-L567). With `zen.workspaces.separate-essentials`
  on, a space shows only the Essentials whose container equals its `containerTabId`; with it off,
  every Essential sits in section 0 and shows in every space ([zen-ess-section] L392-L427).
- The preference defaults to `true` for new profiles ([zen-ess-yaml] L35-L36), but the code's
  fallback is `false` ([zen-ess-code] L122-L125) and a one-time migration copies an older
  preference that defaulted to `false` ([zen-ess-migration] L103-L109). The
  preference lives in `prefs.js`, not in the session file, so an importer reading only
  `zen-sessions.jsonlz4` cannot know it; grouping Essentials by `userContextId` is right either
  way, and whether they are shared across all spaces is the open question (**UNCONFIRMED** per
  profile).
- Up to `zen.tabs.essentials.max` Essentials, default 12 ([zen-ess-max]).
- This maps onto Arc's model: Arc keys favourites by Chromium profile, Zen keys Essentials by
  container, and a space picks its row by its profile or `containerTabId`.

### 2.10 containers.json

```json
{
  "version": 6,
  "lastUserContextId": 5,
  "identities": [
    { "userContextId": 1, "public": true, "icon": "fingerprint", "color": "blue", "l10nId": "user-context-personal" },
    { "userContextId": 2, "public": true, "icon": "briefcase", "color": "orange", "l10nId": "user-context-work" },
    { "userContextId": 6, "public": true, "icon": "tree", "color": "green", "name": "Side project" },
    { "userContextId": 5, "public": false, "icon": "", "color": "", "name": "userContextIdInternal.thumbnail", "accessKey": "" }
  ],
  "siteAssociations": {}
}
```

- Written by `save()` with `version`, `lastUserContextId`, `identities`, `siteAssociations`.
  Source ([ff-cis-save] L371-L388). Versions 2 to 5 are migrated on load; an unknown version resets
  to defaults ([ff-cis-parse] L648-L700, [ff-cis-migrate] L986-L1058).
- Import only identities with `public: true`. The name is `name` when present, else the localised
  label for `l10nId`; the four defaults are Personal, Work, Banking, Shopping. `update()` replaces
  `l10nId` with `name` once the user renames. Source ([ff-cis-label] L798-L811, [ff-cis-ftl]
  L8-L18, [ff-cis-defaults] L175-L196).
- Colours: `gray`, `yellow`, `orange`, `red`, `pink`, `purple`, `violet`, `blue`, `cyan`, `green`
  (older `turquoise` and `toolbar` map to `cyan` and `gray`). Icons: `fingerprint`, `briefcase`,
  `dollar`, `cart`, `vacation`, `gift`, `food`, `fruit`, `pet`, `tree`, `chill`, `circle`, `fence`.
  Source ([ff-cis-colors] L20-L97, [ff-cis-icons] L99-L113).
- `userContextId` 4294967295 is reserved for extension storage and is private ([ff-cis-defaults]
  L197-L218).

### 2.11 Walk, in short

1. Pick the profile (2.2), read `zen-sessions.jsonlz4` (section 3), parse JSON.
2. Read `containers.json` for container names; map each space's `containerTabId` to one.
3. For each space in `spaces` order, take its tabs in array order: `pinned && !zenEssential &&
   zenWorkspace == uuid` are pinned; `!pinned && zenWorkspace == uuid` are Today-style tabs.
   Skip `zenIsEmpty` and `zenIsGlance` tabs.
4. Rebuild folders from `folders` (skip `splitViewGroup` entries as folders), attach tabs by
   `groupId`, nest by `parentId`, order nested folders by `prevSiblingInfo`.
5. Rebuild split views from `splitViewData` (or from `groups` with `splitView: true`).
6. Essentials: tabs with `zenEssential`, grouped by `userContextId`.

### 2.12 Zen: UNCONFIRMED

- The `~/Library/Application Support/zen` path is observed from tools, not traced in Zen's source.
- Whether `zen.workspaces.separate-essentials` is on for a given profile (it lives in `prefs.js`).
- Whether live folders (`isLiveFolder`) should import as plain folders.
- Field presence for tabs created by versions far older than 1.22 (older files may use
  `zenPinnedId` instead of `zenSyncId`, which Zen still accepts, [zen-restore-initial] L23-L26).

---

## 3. mozLz4 (`.jsonlz4`, `.baklz4`)

### 3.1 Layout

| Offset | Size | Content |
|---|---|---|
| 0 | 8 | Magic `6D 6F 7A 4C 7A 34 30 00` ("mozLz40" and a NUL). |
| 8 | 4 | Decompressed size, unsigned 32-bit **little-endian**. |
| 12 | rest | One raw LZ4 **block** (not the LZ4 frame format): no frame header, no block size, no checksum. |

Source: the format comment and constants ([ff-iou-h] L750-L766); writer: magic, then
`LittleEndian::writeUint32(size)`, then `Compression::LZ4::compress` of the whole input as one
block, header only for empty input ([ff-iou-comp] L2627-L2658); the wrapper calls
`LZ4_compress_default` ([ff-lz4-cpp] L20-L25). Observed: identical readers in [arc2zen-sessions]
L36-L50 and [zb-mozlz4] L20-L56.

### 3.2 How Firefox reads it

Source ([ff-iou-decomp] L2660-L2709):

1. Fail if the file is shorter than 12 bytes, or if the first 8 bytes are not the magic.
2. Read the size; if it is 0, return an empty buffer.
3. Allocate exactly that many bytes and call `LZ4_decompress_safe(src, dst, srcLen, size)`
   ([ff-lz4-cpp] L37-L53). Failure means a corrupt file.
4. Truncate the result to the number of bytes actually produced. Firefox does **not** require that
   number to equal the header's size.

### 3.3 LZ4 block decoding

From the LZ4 block specification ([lz4-block]): a block is a series of sequences. Each starts with
a token byte; the high nibble is the literal count (15 means "add following bytes, each 0-255,
until a byte below 255"), then the literals, then a 2-byte little-endian match offset, then the
low nibble plus 4 is the match length (15 extends the same way). Offset 0 is invalid. The last
sequence has literals only and ends the block; the last 5 bytes are always literals and the last
match starts at least 12 bytes before the end. A match may overlap its own output (offset smaller
than length), so copy byte by byte in that case.

### 3.4 Limits and checks worth enforcing

- Header: at least 12 bytes; exact magic; size read little-endian. ([ff-iou-decomp])
- Size ceiling: Firefox's LZ4 wrapper release-asserts that sizes fit in a signed 32-bit `int`
  ([ff-lz4-cpp] L37-L45), and `IOUtils` refuses to read files over 4 GiB ([ff-iou-read]
  L1306-L1332). An importer should set its own much lower cap on both the file size and the
  declared decompressed size (for example 256 MiB; session files are normally a few MB) and
  refuse before allocating, because the header value is attacker-controlled.
- During decoding reject: a literal run or match that would read past the input or write past the
  declared size; offset 0; an offset larger than the bytes produced so far; length arithmetic that
  overflows. These are exactly the cases `LZ4_decompress_safe` guards ([ff-lz4-h] L61-L80,
  [lz4-block]).
- After decoding, require the output length to equal the declared size. Firefox tolerates a short
  result ([ff-iou-decomp] L2706-L2707), but for an importer a mismatch means a truncated file and
  it is safer to refuse (or fall back to the next backup file, as Zen does).
- Chromium 152's `third_party` has no LZ4 library (checked in this project's local Chromium checkout: only
  `third_party/ots/src/subprojects/lz4.wrap`, a build-system reference), so the decoder has to be
  written or vendored; the block format needs about 50 lines.
- Zen writes through a temp file and rename ([ff-jsonfile] L419-L437), so a reader never sees a
  half-written main file, but copy the file before parsing if Zen may be running.

### 3.5 Test vectors (verified with a reference decoder)

```
{"a":1}  (literals only)
6d 6f 7a 4c 7a 34 30 00 07 00 00 00 70 7b 22 61 22 3a 31 7d

"abcdabcdabcdabcdxyz123456789"  (one overlapping match: 4 literals, offset 4, length 12, then 12 literals)
6d 6f 7a 4c 7a 34 30 00 1c 00 00 00 48 61 62 63 64 04 00 c0 78 79 7a 31 32 33 34 35 36 37 38 39
```

---

## 4. The fixtures in this directory

Both are synthetic: made-up names, `example.com` addresses, invented ids.

**`arc-sidebar-example.json`** follows 1.2-1.6 exactly:

- `sidebar.containers` = `[{"global": {}}, <main>]`; interleaved `spaces` and `items`.
- Space "Personal", default profile (`{"default": true}`), emoji icon with both `emoji_v2` and
  `emoji`, a single-colour theme. Pinned section: folder "Reading" holding a tab and the nested
  folder "Deep dive" (with one tab); a tab renamed by the user (`title` "Team docs" over
  `savedTitle` "Example Docs"); a two-tab split view; and one Arc Note (`arcDocument`) that an
  importer should skip. Today section: one unpinned tab.
- Space "Work", custom profile `Profile 1` with a `machineID`, named icon `planet`, and its
  `containerIDs` in the order `["unpinned", ..., "pinned", ...]` to test reading by marker.
- Favourites: one container for the default profile and one for `Profile 1`, each with one tab.
- The sync mirrors are deliberately almost empty (`sidebarSyncState` has only `orderedSpaceIDs`),
  since an importer should not read them.

**`zen-sessions-example.json`** is the decompressed JSON of a `zen-sessions.jsonlz4`:

- Space "Personal" (`containerTabId` 0) and space "Work" (`containerTabId` 2, which is the default
  "Work" container in a fresh `containers.json`).
- Two Essentials: one in container 0 and one in container 2 (with separate Essentials on, the
  second shows only in "Work").
- Pinned in "Personal": folder "Reading" (placeholder tab plus one tab) with nested folder
  "Deep dive" (placeholder tab plus one tab, `prevSiblingInfo` pointing at the tab before it), and
  a top-level pinned tab renamed via `zenStaticLabel` whose current entry (`index` 2) has moved
  away from its pinned address. Pinned in "Work": one tab in container 2.
- Unpinned in "Personal": one tab.
- `tabs` order follows 2.7: Essentials, "Personal" pinned, "Work" pinned, "Personal" unpinned.
- `splitViewData` is empty (a Zen split was not requested for this fixture).

---

## Sources

Zen, commit 7dfcd34 (https://github.com/zen-browser/desktop):

[zen-surfer]: https://github.com/zen-browser/desktop/blob/7dfcd34fc5b9ff210e9e09e370f581525fa3fcc9/surfer.json#L1-L10
[zen-sm-prefs]: https://github.com/zen-browser/desktop/blob/7dfcd34fc5b9ff210e9e09e370f581525fa3fcc9/src/zen/sessionstore/ZenSessionManager.sys.mjs#L25-L48
[zen-sm-file]: https://github.com/zen-browser/desktop/blob/7dfcd34fc5b9ff210e9e09e370f581525fa3fcc9/src/zen/sessionstore/ZenSessionManager.sys.mjs#L44-L48
[zen-sm-init]: https://github.com/zen-browser/desktop/blob/7dfcd34fc5b9ff210e9e09e370f581525fa3fcc9/src/zen/sessionstore/ZenSessionManager.sys.mjs#L101-L152
[zen-sm-migrate]: https://github.com/zen-browser/desktop/blob/7dfcd34fc5b9ff210e9e09e370f581525fa3fcc9/src/zen/sessionstore/ZenSessionManager.sys.mjs#L159-L252
[zen-sm-read]: https://github.com/zen-browser/desktop/blob/7dfcd34fc5b9ff210e9e09e370f581525fa3fcc9/src/zen/sessionstore/ZenSessionManager.sys.mjs#L254-L280
[zen-sm-save]: https://github.com/zen-browser/desktop/blob/7dfcd34fc5b9ff210e9e09e370f581525fa3fcc9/src/zen/sessionstore/ZenSessionManager.sys.mjs#L600-L705
[zen-sm-collect]: https://github.com/zen-browser/desktop/blob/7dfcd34fc5b9ff210e9e09e370f581525fa3fcc9/src/zen/sessionstore/ZenSessionManager.sys.mjs#L735-L792
[zen-sm-restore]: https://github.com/zen-browser/desktop/blob/7dfcd34fc5b9ff210e9e09e370f581525fa3fcc9/src/zen/sessionstore/ZenSessionManager.sys.mjs#L801-L844
[zen-tabstate]: https://github.com/zen-browser/desktop/blob/7dfcd34fc5b9ff210e9e09e370f581525fa3fcc9/src/browser/components/sessionstore/TabState-sys-mjs.patch#L10-L50
[zen-groupstate]: https://github.com/zen-browser/desktop/blob/7dfcd34fc5b9ff210e9e09e370f581525fa3fcc9/src/browser/components/sessionstore/TabGroupState-sys-mjs.patch#L1-L15
[zen-ss-collect]: https://github.com/zen-browser/desktop/blob/7dfcd34fc5b9ff210e9e09e370f581525fa3fcc9/src/browser/components/sessionstore/SessionStore-sys-mjs.patch#L260-L285
[zen-tb-restore]: https://github.com/zen-browser/desktop/blob/7dfcd34fc5b9ff210e9e09e370f581525fa3fcc9/src/browser/components/tabbrowser/Tabbrowser-sys-mjs.patch#L519-L612
[zen-restore-initial]: https://github.com/zen-browser/desktop/blob/7dfcd34fc5b9ff210e9e09e370f581525fa3fcc9/src/zen/common/modules/ZenSessionStore.mjs#L16-L42
[zen-pinned-trim]: https://github.com/zen-browser/desktop/blob/7dfcd34fc5b9ff210e9e09e370f581525fa3fcc9/src/zen/sessionstore/ZenWindowSync.sys.mjs#L296-L313
[zen-sync-id]: https://github.com/zen-browser/desktop/blob/7dfcd34fc5b9ff210e9e09e370f581525fa3fcc9/src/zen/sessionstore/ZenWindowSync.sys.mjs#L324-L334
[zen-pinned-state]: https://github.com/zen-browser/desktop/blob/7dfcd34fc5b9ff210e9e09e370f581525fa3fcc9/src/zen/sessionstore/ZenWindowSync.sys.mjs#L1232-L1285
[zen-window-sync-pref]: https://github.com/zen-browser/desktop/blob/7dfcd34fc5b9ff210e9e09e370f581525fa3fcc9/prefs/zen/window-sync.yaml#L5-L6
[zen-folders-max]: https://github.com/zen-browser/desktop/blob/7dfcd34fc5b9ff210e9e09e370f581525fa3fcc9/prefs/zen/folders.yaml#L11-L12
[zen-folders-create]: https://github.com/zen-browser/desktop/blob/7dfcd34fc5b9ff210e9e09e370f581525fa3fcc9/src/zen/folders/ZenFolders.mjs#L693-L721
[zen-folder-node]: https://github.com/zen-browser/desktop/blob/7dfcd34fc5b9ff210e9e09e370f581525fa3fcc9/src/zen/folders/ZenFolders.mjs#L753-L791
[zen-folders-store]: https://github.com/zen-browser/desktop/blob/7dfcd34fc5b9ff210e9e09e370f581525fa3fcc9/src/zen/folders/ZenFolders.mjs#L1209-L1278
[zen-folders-restore]: https://github.com/zen-browser/desktop/blob/7dfcd34fc5b9ff210e9e09e370f581525fa3fcc9/src/zen/folders/ZenFolders.mjs#L1280-L1398
[zen-folder-pinned]: https://github.com/zen-browser/desktop/blob/7dfcd34fc5b9ff210e9e09e370f581525fa3fcc9/src/zen/folders/ZenFolder.mjs#L207-L209
[zen-folder-tabs]: https://github.com/zen-browser/desktop/blob/7dfcd34fc5b9ff210e9e09e370f581525fa3fcc9/src/browser/components/tabbrowser/content/tabgroup-js.patch#L156-L170
[zen-split-max]: https://github.com/zen-browser/desktop/blob/7dfcd34fc5b9ff210e9e09e370f581525fa3fcc9/src/zen/split-view/ZenViewSplitter.mjs#L87
[zen-split-layout]: https://github.com/zen-browser/desktop/blob/7dfcd34fc5b9ff210e9e09e370f581525fa3fcc9/src/zen/split-view/ZenViewSplitter.mjs#L1639-L1670
[zen-split-store]: https://github.com/zen-browser/desktop/blob/7dfcd34fc5b9ff210e9e09e370f581525fa3fcc9/src/zen/split-view/ZenViewSplitter.mjs#L2417-L2444
[zen-split-restore]: https://github.com/zen-browser/desktop/blob/7dfcd34fc5b9ff210e9e09e370f581525fa3fcc9/src/zen/split-view/ZenViewSplitter.mjs#L2446-L2500
[zen-ess-section]: https://github.com/zen-browser/desktop/blob/7dfcd34fc5b9ff210e9e09e370f581525fa3fcc9/src/zen/spaces/ZenSpaceManager.mjs#L392-L427
[zen-restore-container]: https://github.com/zen-browser/desktop/blob/7dfcd34fc5b9ff210e9e09e370f581525fa3fcc9/src/zen/spaces/ZenSpaceManager.mjs#L555-L567
[zen-space-ss]: https://github.com/zen-browser/desktop/blob/7dfcd34fc5b9ff210e9e09e370f581525fa3fcc9/src/zen/spaces/ZenSpaceManager.mjs#L773-L785
[zen-space-reorder]: https://github.com/zen-browser/desktop/blob/7dfcd34fc5b9ff210e9e09e370f581525fa3fcc9/src/zen/spaces/ZenSpaceManager.mjs#L1510-L1540
[zen-space-create]: https://github.com/zen-browser/desktop/blob/7dfcd34fc5b9ff210e9e09e370f581525fa3fcc9/src/zen/spaces/ZenSpaceManager.mjs#L2656-L2678
[zen-stored-tabs]: https://github.com/zen-browser/desktop/blob/7dfcd34fc5b9ff210e9e09e370f581525fa3fcc9/src/zen/spaces/ZenSpaceManager.mjs#L3191-L3265
[zen-ess-yaml]: https://github.com/zen-browser/desktop/blob/7dfcd34fc5b9ff210e9e09e370f581525fa3fcc9/prefs/zen/workspaces.yaml#L35-L36
[zen-ess-code]: https://github.com/zen-browser/desktop/blob/7dfcd34fc5b9ff210e9e09e370f581525fa3fcc9/src/zen/spaces/ZenSpaceManager.mjs#L122-L125
[zen-ess-migration]: https://github.com/zen-browser/desktop/blob/7dfcd34fc5b9ff210e9e09e370f581525fa3fcc9/src/zen/common/sys/ZenUIMigration.sys.mjs#L103-L109
[zen-add-ess]: https://github.com/zen-browser/desktop/blob/7dfcd34fc5b9ff210e9e09e370f581525fa3fcc9/src/zen/tabs/ZenPinnedTabManager.mjs#L518-L570
[zen-ess-max]: https://github.com/zen-browser/desktop/blob/7dfcd34fc5b9ff210e9e09e370f581525fa3fcc9/prefs/zen/zen.yaml#L14-L15
[zen-theme]: https://github.com/zen-browser/desktop/blob/7dfcd34fc5b9ff210e9e09e370f581525fa3fcc9/src/zen/spaces/ZenGradientGenerator.mjs#L1433-L1440
[zen-icon]: https://github.com/zen-browser/desktop/blob/7dfcd34fc5b9ff210e9e09e370f581525fa3fcc9/src/zen/common/emojis/ZenEmojiPicker.mjs#L301-L303
[zen-uuid]: https://github.com/zen-browser/desktop/blob/7dfcd34fc5b9ff210e9e09e370f581525fa3fcc9/src/zen/common/modules/ZenUIManager.mjs#L327-L329

Firefox, tag FIREFOX_156_0_1_RELEASE (https://github.com/mozilla-firefox/firefox):

[ff-iou-h]: https://github.com/mozilla-firefox/firefox/blob/FIREFOX_156_0_1_RELEASE/xpcom/ioutils/IOUtils.h#L750-L766
[ff-iou-comp]: https://github.com/mozilla-firefox/firefox/blob/FIREFOX_156_0_1_RELEASE/xpcom/ioutils/IOUtils.cpp#L2627-L2658
[ff-iou-decomp]: https://github.com/mozilla-firefox/firefox/blob/FIREFOX_156_0_1_RELEASE/xpcom/ioutils/IOUtils.cpp#L2660-L2709
[ff-iou-read]: https://github.com/mozilla-firefox/firefox/blob/FIREFOX_156_0_1_RELEASE/xpcom/ioutils/IOUtils.cpp#L1306-L1332
[ff-lz4-h]: https://github.com/mozilla-firefox/firefox/blob/FIREFOX_156_0_1_RELEASE/mozglue/static/Compression.h#L32-L80
[ff-lz4-cpp]: https://github.com/mozilla-firefox/firefox/blob/FIREFOX_156_0_1_RELEASE/mozglue/static/Compression.cpp#L20-L53
[ff-jsonfile]: https://github.com/mozilla-firefox/firefox/blob/FIREFOX_156_0_1_RELEASE/toolkit/modules/JSONFile.sys.mjs#L416-L440
[ff-sessionfile]: https://github.com/mozilla-firefox/firefox/blob/FIREFOX_156_0_1_RELEASE/browser/components/sessionstore/SessionFile.sys.mjs#L70-L122
[ff-tabstate]: https://github.com/mozilla-firefox/firefox/blob/FIREFOX_156_0_1_RELEASE/browser/components/sessionstore/TabState.sys.mjs#L71-L155
[ff-tabstate-index]: https://github.com/mozilla-firefox/firefox/blob/FIREFOX_156_0_1_RELEASE/browser/components/sessionstore/TabState.sys.mjs#L220-L236
[ff-groupstate]: https://github.com/mozilla-firefox/firefox/blob/FIREFOX_156_0_1_RELEASE/browser/components/sessionstore/TabGroupState.sys.mjs#L71-L79
[ff-tb-index]: https://github.com/mozilla-firefox/firefox/blob/FIREFOX_156_0_1_RELEASE/browser/components/tabbrowser/Tabbrowser.sys.mjs#L4948-L4956
[ff-tb-group]: https://github.com/mozilla-firefox/firefox/blob/FIREFOX_156_0_1_RELEASE/browser/components/tabbrowser/Tabbrowser.sys.mjs#L4999-L5030
[ff-cis-colors]: https://github.com/mozilla-firefox/firefox/blob/FIREFOX_156_0_1_RELEASE/toolkit/components/contextualidentity/ContextualIdentityService.sys.mjs#L20-L97
[ff-cis-icons]: https://github.com/mozilla-firefox/firefox/blob/FIREFOX_156_0_1_RELEASE/toolkit/components/contextualidentity/ContextualIdentityService.sys.mjs#L99-L113
[ff-cis-defaults]: https://github.com/mozilla-firefox/firefox/blob/FIREFOX_156_0_1_RELEASE/toolkit/components/contextualidentity/ContextualIdentityService.sys.mjs#L175-L266
[ff-cis-save]: https://github.com/mozilla-firefox/firefox/blob/FIREFOX_156_0_1_RELEASE/toolkit/components/contextualidentity/ContextualIdentityService.sys.mjs#L371-L388
[ff-cis-parse]: https://github.com/mozilla-firefox/firefox/blob/FIREFOX_156_0_1_RELEASE/toolkit/components/contextualidentity/ContextualIdentityService.sys.mjs#L648-L700
[ff-cis-label]: https://github.com/mozilla-firefox/firefox/blob/FIREFOX_156_0_1_RELEASE/toolkit/components/contextualidentity/ContextualIdentityService.sys.mjs#L798-L811
[ff-cis-migrate]: https://github.com/mozilla-firefox/firefox/blob/FIREFOX_156_0_1_RELEASE/toolkit/components/contextualidentity/ContextualIdentityService.sys.mjs#L986-L1058
[ff-cis-path]: https://github.com/mozilla-firefox/firefox/blob/FIREFOX_156_0_1_RELEASE/toolkit/components/contextualidentity/ContextualIdentityService.sys.mjs#L1061-L1065
[ff-cis-ftl]: https://github.com/mozilla-firefox/firefox/blob/FIREFOX_156_0_1_RELEASE/toolkit/locales/en-US/toolkit/global/contextual-identity.ftl#L8-L18
[ff-prof-ini]: https://github.com/mozilla-firefox/firefox/blob/FIREFOX_156_0_1_RELEASE/toolkit/profile/nsToolkitProfileService.cpp#L1050-L1122
[ff-prof-install]: https://github.com/mozilla-firefox/firefox/blob/FIREFOX_156_0_1_RELEASE/toolkit/profile/nsToolkitProfileService.cpp#L1125-L1152
[ff-prof-loop]: https://github.com/mozilla-firefox/firefox/blob/FIREFOX_156_0_1_RELEASE/toolkit/profile/nsToolkitProfileService.cpp#L1178-L1289
[ff-prof-setdefault]: https://github.com/mozilla-firefox/firefox/blob/FIREFOX_156_0_1_RELEASE/toolkit/profile/nsToolkitProfileService.cpp#L1380-L1420
[ff-prof-installs-write]: https://github.com/mozilla-firefox/firefox/blob/FIREFOX_156_0_1_RELEASE/toolkit/profile/nsToolkitProfileService.cpp#L2795-L2810
[ff-hash]: https://github.com/mozilla-firefox/firefox/blob/FIREFOX_156_0_1_RELEASE/toolkit/xre/nsXREDirProvider.cpp#L844-L899
[ff-hash2]: https://github.com/mozilla-firefox/firefox/blob/FIREFOX_156_0_1_RELEASE/toolkit/mozapps/update/common/commonupdatedir.cpp#L404-L419

LZ4:

[lz4-block]: https://github.com/lz4/lz4/blob/dev/doc/lz4_Block_format.md

Arc (third-party; Arc itself is closed source):

[boom]: https://github.com/luizberti/boom/blob/7c9b346aa495f684e0e126aea269537a009bf521/STORAGE.md
[ice-dm]: https://github.com/Icesource/arc-tab-organizer/blob/bb50a2665e15133d730eecf7789b9b51bc152d44/DATA_MODEL.md
[arcae]: https://github.com/plainspace/arcaeologist/blob/e0988674716e2f5b54c583fbe0bfd5d4caf0a1c3/src/parser.js
[plainspace README]: https://github.com/plainspace/arcaeologist/blob/e0988674716e2f5b54c583fbe0bfd5d4caf0a1c3/README.md
[arcexport]: https://github.com/ivnvxd/arc-export/blob/088a888af3467583ab54e6008abf8e0b40c984db/main.py
[arc2zen-extract]: https://github.com/Thinkscape/arc-to-zen/blob/8d4a2bb9b43d42a73130224c2c4eda8f90d8d1d1/src/arc2zen/extract.py
[arc2zen-paths]: https://github.com/Thinkscape/arc-to-zen/blob/8d4a2bb9b43d42a73130224c2c4eda8f90d8d1d1/src/arc2zen/profile_paths.py
[arc2zen-fav]: https://github.com/Thinkscape/arc-to-zen/blob/8d4a2bb9b43d42a73130224c2c4eda8f90d8d1d1/src/arc2zen/favicons.py
[arc2zen-icons]: https://github.com/Thinkscape/arc-to-zen/blob/8d4a2bb9b43d42a73130224c2c4eda8f90d8d1d1/src/arc2zen/workspace_icons.py
[arc2zen-sessions]: https://github.com/Thinkscape/arc-to-zen/blob/8d4a2bb9b43d42a73130224c2c4eda8f90d8d1d1/src/arc2zen/sessions.py
[phi-parser]: https://github.com/phibrowser/phibrowser-mac/blob/ddeb281d8b9268f9035a82ff806676987dd474e3/Sources/UserInterface/Onboarding/Importer/ArcDataParser.swift
[phi-tests]: https://github.com/phibrowser/phibrowser-mac/blob/ddeb281d8b9268f9035a82ff806676987dd474e3/Tests/PhiBrowserTests/ArcDataParserTests.swift
[arcmcp]: https://github.com/happylinks/arc-mcp/blob/6b842304541e1a0af4f043316517c4d566961996/src/arc.ts
[leohku]: https://github.com/leohku/arc-blog/blob/be41751dc11551c002a3feaecf7bdb1e78958b40/test-parser/parser.js
[mhadi-sidebar]: https://github.com/mhadifilms/arc-exporter/blob/1411249b6ab62e16c31c771b75623aa441b0f8d9/arc_exporter/parsers/sidebar.py
[mhadi-conftest]: https://github.com/mhadifilms/arc-exporter/blob/1411249b6ab62e16c31c771b75623aa441b0f8d9/tests/conftest.py
[crest]: https://github.com/pauljoda/Crest/blob/fbb48de01b0a79d19164f9cab6dd0a0adfab97bf/CrestShared/Features/DataPortability/Services/Adapters/Arc/ArcSidebarDocument.swift
[refrax]: https://github.com/kageroumado/refrax-browser/blob/769996d03360d62175c1138319a9f394c693d95f/Refrax/Features/Import/Services/ArcImporter.swift
[sumi]: https://github.com/FedyaLight/sumi-webkit/blob/b6650ce08657ed5d3ce8057ee5fd8167e97ca96c/Sumi/ImportExport/Sources/SumiArcImportParser.swift
[axis]: https://github.com/AbdelrahmanBerchan/Axis-Browser/blob/fcc50d83267cc9a4e32a369f891daca1c16e7bfa/src/axis-storable-sidebar-import.js
[vrag]: https://github.com/vrag99/arc-sidebar-export/blob/443bc1ffb8f5251e8bfe5569b61f63ab18538664/utils/parser.py
[xiaogliu]: https://github.com/xiaogliu/export-arc-bookmarks/blob/dd04528d653d9e57bb8ab3cdbfae8b86542a2326/src/main.js
[orbit-tests]: https://github.com/Seggys116/Orbit/blob/7b588fa045063e0c79a6c47dc5ce8e863546dfd4/OrbitTests/ArcSidebarDocumentTests.swift
[adlio-fixture]: https://github.com/adlio/linkcache/blob/5a269e861b73f5c12b797e79e844794c14bcdc54/test_data/StorableSidebar.json
[jtotty-fixture]: https://github.com/jtotty/arc-exodus/blob/584b3ee409f72d2ce6807176fb9343caf78e4980/tests/fixtures/arc/sidebar.json
[zarrar-fixture]: https://github.com/Zarrar09/A-New-Arc-Begins/blob/41f0df8520c4ff13b7f25d387a9046547b927f31/tests/fixtures/sampleStorableSidebar.json

Zen (third-party):

[zb-mozlz4]: https://github.com/wyattjoh/zen-bookmarks/blob/f8b4f67e61446fc9350dd9e303d2b085a748e3d3/src/mozlz4.ts
[zb-sidebar]: https://github.com/wyattjoh/zen-bookmarks/blob/f8b4f67e61446fc9350dd9e303d2b085a748e3d3/src/sidebar.ts
[zb-session]: https://github.com/wyattjoh/zen-bookmarks/blob/f8b4f67e61446fc9350dd9e303d2b085a748e3d3/src/session-store.ts
