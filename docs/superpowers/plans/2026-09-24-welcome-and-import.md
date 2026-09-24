# The welcome and Zen/Arc import: implementation plan

Spec: `docs/superpowers/specs/2026-09-24-welcome-and-import-design.md`.
Format reference for the two readers: `docs/research/zen-arc-import-formats.md`.

Global rules: all code under `arcium/`; a patch only if a Chromium seam has no
other way in; `scripts/format`; tests first and watched to fail; only the
tests a task touches run, by filter; files under about 500 lines; no sync I/O
on the UI thread; nothing loads a page.

## Task 1: the mozLz4 decoder

- Create `arcium/browser/import/mozlz4.{h,cc}`, `arcium/browser/import/BUILD.gn`
  (source_set `import`, deps `//base` only), register in the test target.
- `std::optional<std::string> DecodeMozLz4(base::span<const uint8_t> file)`.
  Limits: 64 MiB file and declared size. Refuse: short header, wrong magic,
  over-limit size, literal or match past input or output, offset 0 or beyond
  output, output shorter than declared.
- Test `arcium/test/mozlz4_unittest.cc`: the two reference vectors decode;
  one test per refusal.

## Task 2: the import plan and the Zen reader

- Create `arcium/browser/import/import_plan.h` (plain structs: `ImportProfile
  {key, name}`, `ImportSpace {key, name, icon, profile_key}`, `ImportFolder
  {key, space_key, parent_key, name, collapsed}`, `ImportEntry {space_key,
  kind (favourite, pinned), folder_key, url, title}`, `ImportPlan` with the
  lists, a `source` and `Counts()`), and `zen_reader.{h,cc}`:
  `std::optional<ImportPlan> ReadZenSession(std::string_view json,
  std::string_view containers_json)`.
- Walk per the reference: spaces in order; pinned tabs per space in array
  order; skip empty and glance tabs; folders from `folders` (not split-view
  groups), tabs attached by `groupId`, nesting by `parentId`, sibling order by
  `prevSiblingInfo`; Essentials grouped by `userContextId`, fanned out to every
  space on that container (container 0 to spaces with none); one profile per
  non-zero container used by a space, named from `containers.json`; pinned
  address from `_zenPinnedInitialState` when present, else the current entry.
- Fixture `arcium/test/data/import/zen-sessions.json` (synthetic) plus a split
  view case. Test `zen_reader_unittest.cc`.

## Task 3: the Arc reader

- `arc_reader.{h,cc}`: `std::optional<ImportPlan> ReadArcSidebar(std::string_view json)`.
- Walk per the reference: the container after `{"global":{}}`; interleaved id
  and object arrays; spaces in array order; pinned container from the markers;
  `tab` to entry, `list` to folder, `splitView` to its tabs, anything else
  skipped; a `seen` set against cycles and a depth guard; favourites from
  `topAppsContainerIDs`, all rows of a profile merged, fanned out to that
  profile's spaces; custom profiles become plan profiles named from their
  directory.
- Fixture `arcium/test/data/import/arc-sidebar.json`. Test `arc_reader_unittest.cc`.

## Task 4: the finder

- `import_finder.{h,cc}`: `struct FoundSource {kind, label, path,
  std::vector<std::string> other_profiles, ImportPlan plan}`;
  `std::vector<FoundSource> FindSources(const base::FilePath& home)`, and
  `ReadSourceFile(path)` for "Import from a file...". Blocking; callers post it
  with `base::ThreadPool` and `MayBlock`. Zen: `profiles.ini`, the `Install*`
  default; Arc: `StorableSidebar.json`.
- Test with a temp home directory holding the fixtures.

## Task 5: the applier and the model's two facts

- `arcium/browser/import/import_applier.{h,cc}`:
  `ImportResult ApplyImportPlan(const ImportPlan&, ArciumModel&, const
  ImportChoices&)` with choices (which kinds, which spaces keep separate
  logins). Dedupe, depth cap, starter-space rule.
- `ModelStore` / `ArciumProfileState`: `model_file_was_absent()` and a
  load-finished callback list.
- Pref `arcium.welcome_step`, registered through the existing profile-prefs
  seam.
- Tests `import_applier_unittest.cc` against a real `ArciumModel`, and the
  store's absent flag.

## Task 6: the card

- `arcium/ui/welcome/` (Chrome-free): `welcome_model.h` (interface),
  `welcome_view.{h,cc}` (card, columns, footer, dots), one file per step,
  colours added to the Arcium mixer.
- Playground example with a fake model; snapshots compared by eye with the
  mockups.
- Test `welcome_view_unittest.cc`: step order, Back, Skip, Skip setup, the
  last step's buttons, Start fresh listing example spaces, the sync step
  hidden and the dots counting five, the default-browser button's two states.

## Task 7: the browser side

- `arcium/ui/browser/welcome_controller.{h,cc}`: shows the card when the
  model file was absent or the pref names a step; the real `WelcomeModel`
  (finder, applier, engines, default browser, pref); placement as peek does.
- Command **Import from Zen or Arc** in the box's table and switch.
- Browser tests: fresh directory shows the card, existing model does not;
  import from a fixture home fills the sidebar with no navigation; relaunch
  after finishing and after quitting at step 3; engine choice sets the default.

## Task 8: close out

- `docs/welcome-hand-checks.md`, findings, the CLAUDE.md row, perf when the
  owner says the computer is free.
