# Stage 3b design: profiles

Status: approved in the design session of 2026-09-11 (sections 1-2 in
conversation; the owner then asked for the rest to be decided by following Zen
where Zen has an answer). Implements R3.4, R3.5, R3.6 and R3.7 of the master
spec, and its acceptance items A3.1 and A3.3. With R3.9 and 3a already done,
this finishes Stage 3.

Research this design rests on:

- `docs/research/stage3-partition-spike.md` (2026-09-10): which ways of making
  a tab keep a fixed storage partition and which leak.
- `docs/research/stage3b-partition-mechanics.md` (2026-09-11, written with this
  document): the mechanism, every navigation path, the Chrome-layer
  assumptions, what a partition costs, and two ways Chrome can lose a
  partition's data.

---

## 1. Goal

A space can use its own profile. A profile has its own logins: its own
cookies, local storage, IndexedDB, cache and service workers. The same site can
be logged in as two accounts in two spaces at once, and both stay logged in
across a relaunch. History, bookmarks, passwords and extensions stay shared
across profiles, as master spec D5 decided.

## 2. Decisions

Made by the owner in the design session:

1. **A tab that moves to a space with another profile is reopened there.** The
   same applies to every open tab of a space whose profile changes. The page
   reloads at the same address, logged in as the new profile, and its back
   history is lost. Zen keeps the old container instead, so a tab can sit in a
   workspace logged in as someone else; the owner chose the stricter rule.
2. **Profiles are managed from the space menu.** A "Profile" submenu lists the
   profiles with a tick on the current one, then "New profile…", "Clear this
   profile's data…" and "Delete profile…". The space bar's badge, a placeholder
   since 3a, shows the profile's colour.
3. **Deleting a profile erases it.** After a confirmation its logins and site
   data are erased, its spaces switch to Default, and their open tabs reopen
   logged out. Zen moves a deleted container's workspaces to no container
   (`ZenSpaceManager.mjs:166-173`); Arcium does the same and also erases.
4. **Chrome's own Clear browsing data clears every profile, with a large
   warning.** When cookies or cache are selected and a second profile exists,
   a confirmation names the profiles that will be logged out, with Cancel as
   the default button.

Decided afterwards, following Zen where it has an answer:

5. **A new space starts on the profile of the space you are in.** Zen creates a
   workspace in the container of the selected tab
   (`ZenSpaceManager.mjs:2543-2547`). Changing it before the space has tabs
   costs nothing.
6. **A profile is a name and a colour** from a fixed palette of eight. Default
   can be renamed but not deleted.
7. **Browser and extension pages use shared storage.** `chrome://`,
   `chrome-extension://` and `devtools://` pages always use the default
   partition, whatever space they open in, so an extension's pages never see a
   different local storage from the extension's own background worker.
8. **Clearing one profile's data does not reload its tabs,** as Chrome's own
   clear does not.
9. **Profiles belong to the whole browser, not a window.** Changing, clearing
   or deleting a profile reaches its tabs in every window.
10. **Incognito ignores profiles.** An incognito window's tabs use incognito's
    own storage, and the Profile submenu is not shown there.

## 3. Approach

Every tab of a non-default profile is created on a `SiteInstance` fixed to
that profile's `StoragePartitionConfig`, inside the one Chromium profile. The
default profile keeps Chromium's own path untouched. Content then keeps the
partition for every navigation the tab makes itself, including cross-site
typed URLs, redirects, iframes, popups with an opener, back and forward and the
back/forward cache, workers, downloads and the PDF viewer
(research §1.1, §2).

Rejected:

- **A real Chromium profile per Arcium profile.** Several `Browser` objects,
  extensions, history and passwords loaded once per profile, and the end of
  "one window, one `Browser`". The spike found no reason to fall back to it.
- **In-memory partitions.** Safe from Chrome's cleanup, but every login is
  gone at quit, which fails A3.1.

## 4. The model

### 4.1 Profiles

`arcium/browser/model/arcium_profile.h`:

```cpp
using ProfileId = TypedId<ProfileIdTag>;

// An Arcium profile: its own logins, inside the one Chromium profile. The
// default profile is the default storage partition; every other profile is a
// partition of its own, named from its id (arcium/browser/profile_partition.h).
struct ArciumProfile {
  ProfileId id;
  std::u16string name;
  // An index into the fixed palette in arcium/ui/sidebar/profile_colors.h.
  int color = 0;
  int position = 0;
};
```

The default profile has a fixed, well-known id so a model file can always name
it. `Space` gains `ProfileId profile_id`. `ArciumModel` gains the profile list
with add, rename, recolour and remove, and `ProfileOfSpace(SpaceId)`.

### 4.2 Migration

`kModelSchemaVersion` goes from 3 to 4. `MigrateV3ToV4` adds a profile list
holding only Default and sets every space's `profile_id` to Default. A file
from a newer build is refused, as today.

### 4.3 The partition

`arcium/browser/profile_partition.{h,cc}` is the one place that turns a profile
into storage:

```cpp
// The partition a profile's tabs live in. std::nullopt for the default
// profile, which uses Chromium's default partition and its own code paths.
std::optional<content::StoragePartitionConfig> PartitionForProfile(
    content::BrowserContext* context, const ProfileId& profile);

// A SiteInstance for a new tab of `profile`: fixed to its partition, or
// nullptr for the default profile so the caller keeps Chromium's choice.
scoped_refptr<content::SiteInstance> SiteInstanceForProfile(
    content::BrowserContext* context, const ProfileId& profile,
    const GURL& url);

// Whether a partition directory or config is one of Arcium's.
bool IsArciumPartitionDomain(std::string_view partition_domain);
```

A profile's partition is `StoragePartitionConfig::Create(context,
"arcium-" + id, "", /*in_memory=*/false)`. One partition domain per profile,
so erasing a profile is erasing one domain, and the `arcium-` prefix cannot
collide with an extension id, which is 32 letters. Off the record, Chromium
forces the config in-memory, and decision 10 means it is never asked for.

## 5. Every tab in the right storage

### 5.1 Which profile a new tab gets

A tab's profile is its space's profile, and it is decided when the tab is
created, because it cannot change afterwards. The space rule already exists
(3a): a tab opened from another tab joins that tab's space; a tab opened from
nowhere joins the space on screen. The creation hooks below apply the same rule
earlier, from the navigation's source tab, and **tag the new tab with its space
at the same moment**, so the space Arcium records and the storage the tab uses
cannot disagree. The existing rule that a tag is never overwritten keeps it.

### 5.2 Where tabs are created

| Path | Today | Hook |
|---|---|---|
| Cmd+click, "Open link in new tab", the new-tab box, bookmarks, extension-created tabs, the home-boundary divert | default partition | `CreateTargetContents` (`browser_navigator.cc:479`): the source tab's space, else the window's active space, through `SiteInstanceForProfile`; a browser or extension page keeps Chromium's choice (decision 7) |
| Session restore and Cmd+Shift+T | default partition | `CreateRestoredTab` (`browser_tabrestore.cc:75-77`), before the WebContents is made, and its session-storage map keyed to the same partition |
| A tab Chromium discards and recreates | not proven either way | `FinishDiscard` (`tab_lifecycle_unit.cc:274`): the replacement keeps the old tab's `SiteInstance` partition |
| Arcium's blank tab for an empty space | default partition | Arcium's own code, no patch |
| Popups without an opener (`target=_blank`, `noopener`) | default partition | none: the guard (§5.3) reopens them |

A restored tab's profile rides in Chromium's per-tab session `extra_data` as
`arcium.profile_id`, beside the space id. The model file is read on a
background sequence and may not be loaded when restore creates tabs, so the
profile has to come from the session itself.

### 5.3 The guard

A navigation throttle, registered beside the home-boundary throttle, checks
every main-frame navigation of a tab in the sidebar:

- a web page must be in its space's profile's partition;
- a browser or extension page must be in the default partition (decision 7).

When a navigation would land in the wrong storage, the guard cancels it and
opens the same URL in a new tab of the right kind at the same place in the
sidebar; if the old tab had nothing else to show (no committed entry), it is
closed. A POST body travels with the reopened navigation.

The hooks make this rare. It exists for two reasons. Popups without an opener
are created by content, which cannot call into `arcium/`; the spike's
alternative was a content patch carrying its own condition, the one exception
to "a patch is a call into arcium/". The guard makes that patch unnecessary.
And a partition that leaks fails R3.5 silently; the guard turns any path the
hooks missed into a visible reopen instead of a shared login.

The guard reads the tab's space from its tag and the space's profile from the
model. If the model has not loaded, it lets the navigation through: the hooks
have already decided correctly for every path except noopener popups, which
cannot happen before a page has loaded.

### 5.4 Preloading

Prerender cannot keep a page in its tab's partition: it builds its own frame
tree in the default partition, and activation would swap the tab into it
(research §2 f1). `Browser::IsPrerender2Supported` returns disabled for tabs in
a non-default partition, through a hook. Prefetch already refuses non-default
partitions. R3.9's rule, that nothing loads unless asked for, argues for this
anyway.

## 6. Managing profiles

### 6.1 The menu

The space menu (`space_bar_view.cc`) gains a "Profile" submenu, built in its
own file because the space bar is already at 513 lines:

- one radio item per profile, ticked for the space's profile;
- "New profile…": a dialog asking for a name and a colour, which creates the
  profile and assigns it to this space;
- "Rename profile…" and "Change colour", for the space's profile;
- "Clear this profile's data…";
- "Delete profile…", absent for Default.

The space bar's badge becomes a filled circle in the profile's colour, with the
profile's name as its tooltip and accessible name.

### 6.2 Changing a space's profile, and moving a tab

Both end in the same operation: **reopen a tab in a profile**. A new tab is
created at the same address, in the new profile, tagged with the target space,
inserted where the old one was in the strip, bound to the old tab's pinned or
favourite entry if it had one, and activated if the old one was active; then
the old tab closes. A tab that had not loaded (R3.9) reopens unloaded. A
pinned or favourite entry that is closed has no tab and simply moves.

`SpaceSwitcher::MoveTabToSpace` and `MoveEntryToSpace` reopen when the two
spaces' profiles differ and re-tag, as today, when they are the same.

### 6.3 Clearing one profile

A confirmation names the profile and says it will log out of every site in
it. Then one `BrowsingDataRemover` call with a filter naming the profile's
partition removes cookies, site data and cache, the same types Chrome's dialog
removes. Tabs are not reloaded (decision 8). History and passwords are shared,
so they are untouched.

### 6.4 Deleting a profile

A confirmation names the profile and counts its spaces and open tabs. Then, in
this order: every space on the profile switches to Default, which reopens their
tabs (§6.2); the profile is removed from the model and the model saved; then
`AsyncObliterateStoragePartition("arcium-" + id)` erases the partition. A
partition that was loaded this session is still held open until quit; Chrome
then deletes it at the next launch, which the plan confirms against
`storage_partition_impl_map.cc:367-410` before relying on it. No tab can reach
it in between, because no tab is in it.

### 6.5 Chrome's Clear browsing data

Two hooks:

- **The warning**, in `ClearBrowsingDataHandler::HandleClearBrowsingData`
  (`settings_clear_browsing_data_handler.cc:245`), just before the removal
  starts. When cookies and site data or cache are selected and a non-default
  profile exists, a native dialog lists the profiles that will be logged out.
  Cancel is the default. On Cancel the handler resolves the page's promise
  with no notices, which closes the dialog (`clear_browsing_data_dialog.ts:428-436`)
  with nothing removed.
- **Every profile**, in `ChromeBrowsingDataRemoverDelegate`, beside its loop
  over Isolated Web App partitions (`chrome_browsing_data_remover_delegate.cc:1428-1466`):
  when the filter names no partition, the same removal runs once per Arcium
  profile partition.

## 7. Keeping the data

### 7.1 Chrome's partition cleanup

After an extension's data is deleted or an Isolated Web App is removed, Chrome
sets a pref, and at the next startup deletes every partition directory it does
not recognise and is not holding open (`garbage_collect_storage_partitions_command.cc:64-87`,
`storage_partition_impl_map.cc:412-432`). Partitions open lazily and R3.9
defers every restored tab, so at that moment most Arcium profiles are not open.
Without a hook, uninstalling one extension would erase every Arcium profile's
logins.

A hook in the cleanup command adds the partition path of every profile in the
model to the keep list; the paths are computed from the ids, with no disk
listing. The model is read on a background sequence and may not be loaded when
the command runs, so when it is not, the hook tells the command to skip the
cleanup for this launch and leave its pref set, and the cleanup runs at a later
launch. Skipping never loses data; running with an incomplete list would. A
deleted profile's partition is erased by §6.4, not by this sweep.

### 7.2 Session cookies

Chromium never restores session cookies in a partition other than the default
one, even with "Continue where you left off" on
(`profile_network_context_service.cc:1449-1457`). Sites that keep a login in a
session cookie would log out of every non-default profile at each relaunch,
which fails A3.1. A hook lets Arcium partitions follow the profile's own
setting, exactly as the default partition does.

## 8. Edges

- **Several windows.** Profiles and the model are per Chromium profile, so
  every window sees the same profiles; §6's operations walk every window's tab
  strip.
- **A New Tab Page** opened in a space on a non-default profile is a browser
  page, so it is in shared storage (decision 7); the first site typed into it
  is reopened by the guard in the space's profile. Cmd+T opens Arcium's quick
  entry rather than that page, so this is rare.
- **The spare renderer** is per partition. The first page loaded in a
  profile's space misses it and starts a renderer of its own.
- **Extensions that manage cookies** see only the default partition; Chrome's
  site-data settings page shows only the default partition (master spec §4.4,
  accepted).
- **Split view and Little Arc** do not exist yet; when they do, they create
  tabs through the paths in §5.2.
- **A crash between a profile's deletion and the session's next write** can
  restore a tab into the erased partition's id. The guard sees a tab whose
  space is now on Default and reopens it there.

## 9. Master spec corrections

- §4.4's sentence "Popups and child frames inherit the partition" becomes:
  "Child frames and popups with an opener inherit the partition. Every other
  new tab is created in its space's profile by a hook, and a navigation guard
  reopens any page that would land in the wrong partition." (spike
  recommendation).
- §4.4 gains: "Browser and extension pages always use the default partition."
- §6's risk row for fixed partitions is marked resolved by the spike and this
  design.

## 10. Testing

Unit tests in `arcium/test/`, each failing first and passing a mutation check:

- the model: profiles added, renamed, recoloured, removed; a removed profile's
  spaces fall back to Default; `ProfileOfSpace`;
- migration from version 3 and the serializer round trip;
- the partition mapping: Default gives none, another profile gives
  `arcium-<id>`, `IsArciumPartitionDomain`;
- the new-tab rule: source tab's space, else the active space;
- the guard's decision, as a pure function of (page kind, tab's partition,
  space's profile);
- the reopen operation: position, entry binding, activation, unloaded stays
  unloaded;
- the views: the Profile submenu's items and tick, the badge colour, both
  confirmations' wording.

**An automated isolation check.** A leak shows nothing, so the paths of §5.2
and the spike's table are checked by machine, not only by hand: a local server
answers on two hosts, each tab reads back a cookie set per partition, and every
path must read its own profile's cookie. The preferred form is an
`arcium_browsertests` target of in-process browser tests. The plan's first step
proves such a target builds and runs one test on this machine; if it cannot,
the check is a script that drives the dev build over the DevTools protocol,
as the spike did, kept in `scripts/`.

## 11. Acceptance

Executed by hand, except where marked.

- **A3.1** (master spec) Two spaces, two profiles, two accounts on the same
  site; quit and relaunch; both still logged in.
- **A3.3** (master spec) Idle memory with three spaces and two profiles, and
  the measured cost of one more partition, recorded in `docs/perf/`.
- **A3b.1** (automated, §10) Every creation path of §5.2 lands in the source
  space's profile; restore, Cmd+Shift+T and a discarded tab's reload keep
  theirs; a `target=_blank` link is reopened by the guard in the right one.
- **A3b.2** Move a logged-in tab to a space on another profile: it reopens at
  the same address, logged in as that profile.
- **A3b.3** Clear one profile: its spaces are logged out, the other profile's
  are not.
- **A3b.4** Delete a profile: its spaces show Default's badge, their tabs are
  logged out, and after a relaunch its partition directory is gone.
- **A3b.5** Chrome's Clear browsing data with cookies selected: the warning
  names the second profile; Cancel clears nothing; confirming logs out both.
- **A3b.6** Uninstall an extension, relaunch twice: every profile's logins
  survive.
- **A3b.7** An extension's options page opened from a space on a non-default
  profile shows the same saved settings as from Default.

## 12. Performance

1. **Processes:** none while only Default is used. A profile's first loaded
   page needs a renderer of its own, because renderers and the spare renderer
   are never shared across partitions. None at idle.
2. **Idle memory:** per profile that has loaded a page this session, one
   network context and a partition's storage contexts, measured by A3.3.
   Nothing per tab; nothing for a profile none of whose tabs has loaded.
3. **Startup:** nothing before first paint; the model file gains a short list.
   A partition is created when its first tab loads, which after R3.9 means on
   the first click.
4. **UI thread:** creating a partition builds about seventeen storage contexts
   on the UI thread (`storage_partition_impl.cc:1391-1640`), once per profile
   per session, measured by `Storage.StoragePartition.InitializeDuration`. The
   guard is one comparison per main-frame navigation.

## 13. Out of scope

- Per-profile history, passwords or extensions (D5).
- Air Traffic Control's routing rules (R4.5), which will route to a space and
  so to its profile.
- A site-data view per profile (`BrowsingDataModel::BuildFromNonDefaultStoragePartition`
  exists for it).
- Syncing profiles.
