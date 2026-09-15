# Stage 4b: peek, the outside-link window, routing rules and box commands

Date: 2026-09-15. Covers what Stage 4 has left after 4a took the pill (R4.7) and
the extension homes (R4.8): R4.1's missing half (commands in the box), R4.3
(Little Arc), R4.4 (Peek), R4.5 (Air Traffic Control) and R4.6 (site search
shortcuts). Bookmarks stay dropped at the owner's decision.

**How this stage is being built.** The owner asked for all four to be written
while a forced full rebuild runs (an Xcode and macOS SDK update on 2026-09-15
invalidated every object file), and for them to be verified and refined
together in one testing pass afterwards. So nothing here was compiled while it
was written, and the owner approved building all four without a separate design
review for each. Every decision below that Zen does not settle was made here,
with its reason, so the testing pass can overturn it knowingly.

## The mechanism all of it shares: a loose page

Zen's Glance is not a separate kind of browser. It is an ordinary tab, hidden
from the tab list and drawn over the tab that opened it
(`ZenGlanceManager.mjs`, `#createBrowserElement`, `fullyOpenGlance`). Arcium
copies that shape, because the alternative -- a bare WebContents in a view --
has no tab helpers: Chromium restricts `TabHelpers::AttachTabHelpers` to a
friend list, and without them the page has no password manager, no extension
tab identity and no JavaScript dialogs. The iCloud Passwords bubble and the
Claude extension are the two things the owner uses the browser for.

A **loose page** is a tab in the window's one `TabStripModel` that carries a
marker saying it is not drawn in the sidebar:

- The marker lives on the WebContents, beside the space tag, as one of
  `kPeek` or `kOutsideLink`.
- `SpaceOfTab` returns an invalid space for a loose page. Every place that
  walks tabs asks "is this tab in space S" with a valid S, so the sidebar's
  rows, the space-scoped tab commands (Ctrl+Tab, Cmd+1..9, close-others), the
  tab search behind the box and the space switcher's last-active bookkeeping
  all skip it without each learning a new rule. The archive skips it
  explicitly, because it reads a space's timeout from a space it cannot find.
- A loose page is created by Arcium, not by `chrome::Navigate`: the
  WebContents is built with the storage of the space it belongs to (the same
  `SiteInstanceForProfile` a new tab gets), tagged with that space and
  marked loose *before* it is inserted, then inserted without activation.
  Marking after insertion would let the sidebar draw it for one frame, since
  insertion is what the sidebar listens to. `TabModel` attaches the tab
  helpers on insertion, as it does for any tab.
- **Promoting** a loose page clears the marker, moves the tab to the end of
  the strip, switches the window to its space and activates it. It is then an
  ordinary Today tab in that space, with its history, its scroll position and
  its logins, because it never stopped being a tab.
- **Dismissing** one closes its tab.
- Only the window shows a loose page; nothing is restored. If Arcium quits
  with one open, session restore writes it like any tab and it comes back as
  a Today tab. Accepted for now: it loses nothing, and excluding one tab from
  the session file needs an upstream hook this stage does not otherwise need.

Four questions: no process exists while no loose page is open, and one that
is open costs exactly the renderer a tab would; nothing is allocated per window
until one opens and everything is freed when it closes; nothing runs at
startup; the only UI-thread work is creating the views when one opens.

## Peek (R4.4)

**What opens it.** A link click in a pinned or favourite tab that leaves its
home site. Today that click opens a new foreground Today tab (Stage 2.6's home
boundary); now it opens a peek over the entry instead. This is exactly Zen's
rule: `shouldOpenTabInGlance` sends a new tab to Glance when its owner is a
pinned app tab and the link's host differs (`zen.glance.open-essential-external-
links`, on by default). The home boundary's rule itself -- link click, user
gesture, host differs ignoring `www.`, not a back or forward -- is unchanged;
only where the diverted navigation lands changes.

Not copied from Zen: its modifier-click trigger (`zen.glance.activation-method`,
default Ctrl). On macOS Ctrl-click is the context menu and Cmd-click is Chrome's
background tab, so every modifier is taken. Left out until the owner asks.

**What it looks like.** A scrim over the whole page area, and on it a card
holding the page, inset from the page area's edges by 5% on each side, with
the same 12px corner radius the page area uses. To the right of the card, a
column of two round buttons, as in Zen's `zen-glance-sidebar-container`:
close, and open as a tab. Zen's third button, split, waits for split view.

**How it closes.** Escape (when the page does not use the key itself -- the
key reaches the peek only if the page leaves it unhandled, which is Zen's
`event.defaultPrevented` check), a click on the scrim, the close button, or
the page closing itself. A second off-home link from the same entry while a
peek is open replaces the peek rather than stacking a second.

**Open as a tab** promotes the loose page into the entry's space.

**Storage.** The space of the tab that opened it, so a peek from a Work pin
reads Work's logins. That is what `SpaceForNewTab` already answers for a tab
opened from a source.

Feature flag `ArciumPeek`, on by default. Off, the boundary opens a new tab as
before.

## The outside-link window (R4.3)

Zen has no equivalent: a link from another application opens as a new tab in
the current workspace. Arc's Little Arc is the reference, so the decisions
here are Arcium's own.

**What opens it.** A link macOS hands to the running browser -- a click in
Slack, Mail or a terminal while Arcium is the default browser. The hook sits
at the top of `AppController application:openURLs:`. It takes the links only
when a window with a sidebar exists and every link is http or https; anything
else -- a cold launch, where no window exists yet, a `file:` URL, the
`chromium://` scheme -- goes to Chromium's own path unchanged, which opens a
tab. A cold launch with a link therefore still opens a plain tab. Accepted:
Arc does the same only because its window exists by then.

**What it looks like.** A small window of its own, 480 by 640, placed at the
top right of the screen the main window is on and cascaded by 24px per extra
window. It has the system title bar with its close button, a row under it
holding the page's domain and one button, "Open in <space name>", and the page
below. One window per link: two links from Slack are two windows, as in Arc.

**Which space.** A routing rule's space when one matches, otherwise the space
the main window is showing. The page is created in that space's storage, so a
link routed to Work loads signed in to Work.

**Open in <space>** promotes the loose page, brings the main window forward and
closes the small window without closing the tab. **Closing the window** closes
the page.

**Known risk, to check in the testing pass.** Cmd+W while the small window is
key. Chrome's menu command is dispatched to a browser window, and the small
window is not one, so Chromium may close the main window's tab instead. The
window registers its own Cmd+W accelerator; whether macOS lets it answer before
the main menu does is not something that can be known without running it.

Feature flag `ArciumOutsideLinkWindow`, on by default.

## Routing rules (R4.5)

**A rule** is a site and a space: "github.com opens in Work". The site is a
host with `www.` removed and lower-cased, and it matches that host and every
subdomain of it -- `github.com` matches `gist.github.com` but not
`notgithub.com`. When two rules match, the longer site wins, so
`gist.github.com` can go somewhere `github.com` does not. Arc's rules are
substring matches on the URL; a host rule cannot be surprised by a query string
that happens to contain the text.

**What they apply to.**

- Outside links, as above.
- Addresses chosen in the command box that are not already open: the window
  switches to the rule's space and opens the tab there.

**What they deliberately do not apply to:** tabs a page opens -- a
`target=_blank` link, a Cmd-click, `window.open`. Such a tab keeps its opener's
space and storage, which is what keeps a sign-in popup in the storage of the
page waiting for it. A rule silently moving it to another profile would break
exactly the logins spaces exist to keep apart. R4.5 says "new tabs"; this reads
that as the tabs the reader asks for, not the ones a page asks for.

**Where they live.** In the model file beside the spaces, schema version 5,
under `routing_rules`: a list of `{site, space_id}`. Removing a space removes
its rules. A rule naming a space the file does not have is dropped on load, the
way an entry in a missing space is.

**How one is made.** From a row's right-click menu: "Always open <site> in
this space", or, when that rule already exists, "Stop opening <site> in this
space". Setting a rule for a site that already has one moves it. A list of
every rule, with editing, is the customisation stage's settings surface; this
stage gives the reader the one gesture that makes a rule and the one that
removes it.

## Commands and site search in the box (R4.1, R4.6)

**Commands.** The box offers browser commands when the words typed start the
words of a command's name or one of its extra names: "clo" offers Close tab,
"dev" offers Developer tools, "hist" offers History. Every typed word has to
start some word of the same command, case ignored, and nothing is offered for
fewer than two letters. At most three command rows, and they come first,
because a reader typing a command's name wants the command. Choosing one closes
the box and then runs it, so a command that moves focus -- Find -- finds the
page rather than the closing box.

The list: Close tab, Reopen closed tab, Duplicate tab, Copy link, Find in page,
Print, Developer tools, History, Downloads, Extensions, Settings, Clear browsing
data, Task manager, New space, Hide or show sidebar, Site search shortcuts. All
but three are Chromium's own command ids, run through the same command
controller a key press uses, so Arcium's space-scoped handling of Close tab
applies. New space, the sidebar toggle and Site search shortcuts are Arcium's.

**Site search.** Chromium already has it: a search engine's keyword typed before
a query -- "youtube.com cats", or "yt cats" once the reader gives YouTube that
keyword -- is a search on that site, and the box's suggestions already come
from Chromium's keyword provider. Two things were missing. The row said only
the query; it now says "Search YouTube for cats" when the search is not the
default engine's. And there was no way to reach the place keywords are set; the
Site search shortcuts command opens Chromium's search engine settings page in
a tab. That page is WebUI, allowed because it opens on demand and its process
exits when the tab closes.

## Upstream hooks

One new patch: `0220-outside-links-mac.patch`, the call at the top of
`application:openURLs:`, plus whatever GN wiring the file's target needs to
reach `//arcium/ui/browser` (checked when written; `app_controller_mac.mm`'s
target already includes Arcium headers through patch 0010's allowance if it is
part of `//chrome/browser/ui`). Peek needs no new hook: the home boundary
throttle already diverts the navigation, and only its destination changes.

## Tests

Unit tests, written first:

- The loose-page marker, and `SpaceOfTab` answering an invalid space for a
  loose page and a valid one once promoted.
- Rule sites: normalising, exact, subdomain, `www.`, the `notgithub.com` case,
  non-http, longest rule wins. The model's rule set, replace and remove, and
  removal with the space. Serialiser round trip, the drop of a rule for a
  missing space, and the version 4 to 5 migration.
- Command matching: word prefixes, case, the two-letter floor, the three-row
  cap, and command rows merged ahead of the rest.
- The row menu offering the "Always open" item on a row with a web address and
  the "Stop opening" item when the rule exists, and neither on a row without
  one.

Browser tests, run in the testing pass:

- An off-home click in a pinned tab opens a peek, the sidebar gains no row, the
  pinned tab stays active and on its home, and open-as-tab turns the peek into a
  Today tab in the pin's space. Escape and the scrim close it and its tab.
- The existing home boundary tests change their expectation from "a new active
  tab" to "a peek".
- An outside link opens a small window whose page uses the routed space's
  storage, and Open in space moves it into that space.
- A command-box address with a rule opens in the rule's space.
- Choosing Close tab in the box closes the tab on screen.

## Acceptance by hand

- A4b.1 Click a link to another site in a pinned tab: a peek opens; Escape
  closes it; do it again and open it as a tab.
- A4b.2 With Arcium as the default browser, click a link in another
  application: the small window opens; Open in space moves the page into the
  main window. Press Cmd+W in the small window and note which tab closes.
- A4b.3 Make "Always open github.com in this space" in a second space, then
  open a GitHub link from another application and from the box: both land in
  that space.
- A4b.4 Type "clo" in the box and choose Close tab; type "dev" and choose
  Developer tools.
- A4b.5 Give YouTube the keyword "yt" through the Site search shortcuts
  command, then type "yt cats": the row reads "Search YouTube for cats" and
  opens a YouTube search.
