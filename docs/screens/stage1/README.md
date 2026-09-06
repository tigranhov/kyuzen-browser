# Stage 1 screens

Produced by the offscreen paint behind `--snapshot` (playground) and
`--arcium-snapshot` (browser), at 2x. Screen capture needs a macOS permission
an automated session does not have, so these are painted, not captured: web
contents and other layer-backed surfaces come out blank, and the page area is
therefore empty in the browser shots.

| File | What it shows |
|---|---|
| `05-empty-sidebar.png` | The playground's first run: the empty column and the inset page area |
| `07-all-sections.png` | Every zone against the fake model: nav row, URL pill, favourites, pinned, divider, today, new tab row, space bar |
| `09-window.png` | The real browser: no tab strip, no toolbar, live tab rows, page inset and rounded |
| `12-bookmark-bar.png` | Bookmark bar above the page, inside the area left of by the sidebar |
| `12-sixty-tabs.png` | Sixty tabs, after the Today list was made scrollable |

Not here: Cmd+T quick entry, Cmd+L focus and find in page, which need real key
presses. Run those by hand; `--arcium-quick-entry` opens the quick entry
without a keyboard.
