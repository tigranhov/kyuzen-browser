# Welcome and import hand checks

Fourteen rows, walked against a running browser with this machine's own
keyboard and pointer. They cover what the browser tests cannot: the look of
each step, the macOS default-browser question, a real Zen profile and an Arc
file from another Mac.

Every row but the last two starts from a data directory the browser has never
used, which is what a fresh install is. `scripts/run` passes `--no-first-run`,
so the welcome needs `--arcium-welcome` to show:

```bash
ARCIUM_USER_DATA_DIR="$(mktemp -d)" scripts/run --arcium-welcome
```

Rows 2 to 4 read the Zen on this Mac. To walk them against made-up files
instead, add `--arcium-import-home=<folder>` with a folder laid out as
`Library/Application Support/zen/...` or `Library/Application Support/Arc/...`.

| # | Do this | Look for |
|---|---|---|
| 1 | Launch on a fresh data directory | A card over the page area with the sidebar beside it, at "Step 1 of 5". For a moment it says it is looking for Zen and Arc |
| 2 | Wait for the first step to settle | Zen is found and chosen, with counts of spaces, pinned tabs, favourites and folders that match what Zen shows. The panel lists the spaces that will arrive |
| 3 | With more than one Zen profile holding spaces, open the menu on Zen's row | Each profile by name; choosing one changes the counts and the panel |
| 4 | Untick pinned tabs | Folders untick and grey out, because without pinned tabs there is nothing to file |
| 5 | Choose Start fresh, then Continue | Step 2 lists two example spaces, Personal and Work, with "Example spaces. Rename or remove them any time." |
| 6 | Back to step 1, take "Import from a file…" and pick an Arc `StorableSidebar.json` copied from another Mac | A row for Arc named after the file, chosen, with its counts |
| 7 | On step 2, turn one space's separate logins on and another's off, then Continue | The sidebar fills with the source's spaces; the window is on the first; the empty space called "Space" is gone. The space with separate logins shows its own profile in its menu |
| 8 | Look at the new spaces' pinned rows | Nothing is loading: no spinner, and Activity Monitor shows no new renderer. A row loads only when clicked |
| 9 | On step 3, choose an engine other than the one ticked, then search from the command box after finishing | The search goes to the engine chosen |
| 10 | On step 4, take "Make Kyuzen my default browser" | macOS asks. After saying yes, the button becomes "Kyuzen is your default browser" with a tick |
| 11 | On step 3, quit with Cmd+Q and relaunch with the same data directory | The card comes back at step 3 |
| 12 | Finish with "Start browsing", quit and relaunch | No card. The same after "Skip setup" on a fresh directory |
| 13 | On any profile, open the command box and take "Import from Zen or Arc" | One step, no dots, Cancel and Import. Import adds the spaces; doing it a second time adds nothing |
| 14 | Walk steps 1 to 5 once in light mode and once in dark, and once in a window narrower than about 1000 px | Both themes read clearly. In the narrow window the right-hand panel goes and nothing is cut off |

A row that fails is a finding: write down what you saw, not what should have
happened, and say which row it was.
