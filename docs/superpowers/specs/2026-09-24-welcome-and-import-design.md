# The welcome, and bringing a setup over from Zen or Arc

Approved by the owner on 2026-09-24, screen by screen, against the mockups in
`2026-09-24-welcome-mockups/` (direction C, "import with preview", chosen from
three). It covers R6.5's spaces half, pulled forward so someone moving from
Zen or Arc can start in Kyuzen with their setup. Folder sync is a separate
design that comes after this one; the welcome keeps a step for it.

## What the reader sees

On the first launch of a fresh install a card fills the page area, with the
sidebar left visible beside it. One step per screen; a footer holds Back, a
row of dots with "N of M", Skip and Continue; a "Skip setup" link sits under
the card. Every step can be skipped. Quitting halfway brings the welcome back
at the step reached; finishing or skipping ends it for good.

| # | Step | Left column | Right column |
|---|---|---|---|
| 1 | Bring your setup | What was found (Zen, Arc), tick boxes with counts: spaces, pinned tabs, favourites, folders; "Start fresh"; a menu when a browser has more than one profile with spaces; "Import from a file..." | The spaces that will arrive, with icon, name and pinned count |
| 2 | Spaces and logins | Each space with a "Separate logins" toggle; spaces that used a Zen container or an Arc profile start on | What separate logins means, shown as two accounts side by side |
| 3 | Search engine | The engines Chromium offers for this country, as radio rows with a letter icon | A preview of the command box searching with the chosen engine |
| 4 | Default browser | One button; after macOS confirms it reads "Kyuzen is your default browser" with a tick | Mail, Slack and Notes links opening in Kyuzen |
| 5 | Sync (optional) | Shown only once folder sync exists; until then the flow has five steps and the dots count five | What syncs and what stays on this Mac |
| 6 | You're set | Four shortcuts in keycaps | The first imported space as it now looks |

Changes from the mockups, agreed with the owner: the last step has no Skip,
only Back and "Start browsing"; the sync step has no "Not now", since Skip is
the same thing; the default-browser button changes once confirmed; with
"Start fresh" the spaces step lists two example spaces, Personal and Work.
Other browsers' logos are never drawn: Zen and Arc get plain line icons.

Someone updating from an earlier version never sees the welcome. The command
box gains **Import from Zen or Arc**, which opens the same card with only the
first step and an Import button, for anyone at any time.

## What the import brings over

| From | Becomes |
|---|---|
| Zen spaces, Arc spaces (name, emoji icon) | Spaces, in the source's order. Colours wait for themes |
| Zen containers (`containerTabId`), Arc custom profiles | Separate logins: one Kyuzen profile per container or Arc profile, named after it; spaces that shared one share it |
| Zen Essentials, Arc favourites | Favourites. Kyuzen keeps favourites per space, so each one goes to every imported space on the same container or profile (Zen's default behaviour, and Arc's); duplicates by address removed |
| Pinned tabs | Pinned entries in their space, cold, with the saved address and title. Zen's pinned tab keeps the address it was pinned at (`_zenPinnedInitialState`), which is Kyuzen's home address |
| Folders, nested | Folders in the same order and nesting; anything deeper than 4 levels lands in the fourth |
| Unpinned tabs, Zen glance and empty tabs, Arc notes and easels | Left behind |
| Split views | Left behind as splits; their tabs arrive as ordinary pinned tabs |

Rules:

- **Which profile.** Zen: the profile `profiles.ini` marks as the install's
  default; if other profiles have spaces, the found row offers a menu. Arc:
  the one `StorableSidebar.json`.
- **Arc from another Mac.** "Import from a file..." takes a copied
  `StorableSidebar.json` or `zen-sessions.jsonlz4`.
- **Importing twice** adds only what is not there already: a space matches by
  name, an entry by address within its space and kind, a folder by name within
  its parent.
- **The starter space.** A fresh model holds one empty space called "Space".
  If it is still empty when an import adds spaces, it is removed once the
  first imported space is on screen, so no empty space is left behind.
- **Icons.** Neither file carries usable page icons (Arc has none; Zen's are
  addresses Kyuzen would have to fetch), so an imported row draws its icon
  from the favicon database like any cold row, which fills on the first visit.
  Fetching icons at import time would be network traffic nobody asked for.
- **Nothing loads.** Imported entries are cold. The import starts no
  navigation and no renderer.

## How it is built

- **Readers**, in `arcium/browser/import/`, depend on `base` only:
  - a mozLz4 decoder (header, little-endian size, LZ4 block) that refuses a
    file over 64 MiB, a declared size over 64 MiB, any read or write past its
    buffer, a zero or too-large offset, and output shorter than declared;
  - a Zen reader and an Arc reader, each turning the source's JSON into one
    neutral **import plan**: profiles, spaces, folders, entries, each keyed so
    the plan names its own references, plus the counts step 1 shows;
  - a finder that locates Zen's default profile and Arc's file under a home
    directory it is given, reads them, and returns plans. It runs on the
    thread pool; nothing in it touches the UI thread.
  Readers are strict about shape and forgiving about content: a malformed
  file is "could not be read", but an unknown item kind, a missing title or a
  cycle in Arc's children is skipped with the rest kept.
- **The applier** turns a plan into `ArciumModel` calls on the UI thread:
  `AddProfile`, `AddSpace`, `SetSpaceIcon`, `SetSpaceProfile`, `AddFolder`,
  `AddEntry`, `SetEntryFolder`. The spaces it adds hold no tabs, so recording
  their profile in the model is enough. It applies the dedupe, depth and
  starter-space rules and reports what it added.
- **The model** gains two facts: whether the load found no file (a fresh
  install), and a callback when loading finishes. The welcome's progress is a
  per-profile pref, `arcium.welcome_step`: unset means never started, a number
  is the step reached, and done is its own value.
- **The card**, in a Chrome-free target so the playground can host it, is a
  `WelcomeView` over a `WelcomeModel` interface: sources found, the plan's
  preview, spaces and their logins, engines, default-browser state, progress.
  The playground drives it with a fake; the browser with a controller in
  `arcium/ui/browser/` that owns the finder, the applier, the search engine
  list (`TemplateURLService`, prepopulated engines only), the default-browser
  check and request (`shell_integration::DefaultBrowserWorker`), and the pref.
- **Placement** copies peek: a child of the browser view with a layer of its
  own, stacked at the top, laid out to the page area on every window layout.
  The area around the card takes the page background colour, so the blank tab
  behind it does not show.
- **Colours** are new ids in the Arcium colour mixer, light and dark.

## Performance

1. **Processes:** none added. The card is Views in the browser process; the
   readers run on the thread pool.
2. **Idle memory:** none. The card and its controller exist only while shown
   and are destroyed when it ends; nothing is kept for a finished welcome but
   one pref value.
3. **Startup:** no work before first paint. The fresh-install fact comes from
   the load that already happens; looking for Zen and Arc starts only after
   the card is on screen.
4. **UI thread:** file reading, decompression and parsing are off it. Applying
   a plan is model calls on it; the sidebar redraws once per run-loop turn and
   the model file is written 2.5 s after the last change, so a plan of a few
   hundred entries costs one redraw and one write.

## Testing

- Unit: the decoder against the two reference vectors and each refusal; the
  Zen and Arc readers against synthetic fixtures in `arcium/test/data/import/`
  (never the owner's own files); the applier against a real `ArciumModel`
  (spaces, profiles, favourites per space, folders and the depth cap, dedupe
  on a second import, the starter space); the card's flow over the fake
  (steps, Back, Skip, Skip setup, the last step, Start fresh, the hidden sync
  step); the new command's match.
- Browser: a fresh user-data directory shows the card and an existing model
  does not; importing from a fixture home directory fills the sidebar and
  starts no navigation; a relaunch after finishing shows nothing, and one
  after quitting at step 3 shows step 3; choosing an engine sets the default
  search provider.
- Hand: the rows in `docs/welcome-hand-checks.md`, against a real Zen here and
  an Arc file copied from another Mac.

## Out of scope, and next

- Folder sync: its own design, next after this one.
- **Importing from other popular browsers** (Chrome, Firefox, Safari and the
  rest: bookmarks, history, passwords): the owner's next topic after this
  work.
- Space colours and themes (Stage 6), page icons at import time.
