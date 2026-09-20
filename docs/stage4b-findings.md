# Stage 4b: peek, the outside-link window, routing rules and box commands

Design: `docs/superpowers/specs/2026-09-15-stage-4b-navigation-design.md`.

The whole stage was written while a forced full Chromium rebuild held the one
checkout, at the owner's request, so every API below was checked by reading
Chromium's headers rather than by building against them. It has since been
built and run: see "What the first build and the first run found" below.

## What was built

**A loose page.** One idea underneath all four features: a real tab in the
window's one strip carrying a marker that says the sidebar does not draw it,
either `kPeek` or `kOutsideLink`. `SpaceOfTab` answers an invalid space for a
marked tab, so the sidebar's rows, the space-scoped tab commands, tab search
and the space switcher's bookkeeping all pass over it without learning a rule
of their own; the archive refuses one explicitly, because it reads a timeout
from a space it cannot find. Promoting clears the marker, moves the tab to the
end of the strip, switches the window to the tab's space and activates it —
the page keeps its history, its scroll and its logins because it never stopped
being a tab. This is Zen's Glance shape, and it is what buys the password
manager, the extension identity and JavaScript dialogs that a bare WebContents
in a view would not have.

**Peek.** A link that leaves a pinned or favourite entry's home now opens on a
card over that entry instead of in a new foreground tab. The home boundary's
own rule is untouched; only where the diverted navigation lands changed. One
peek per window at a time, closed by Escape, a click on the scrim, the close
button, the page closing itself, or going to another tab. Open-as-tab promotes
it into the entry's space. Behind the flag `ArciumPeek`, on by default; off,
the boundary opens a tab as before.

**The outside-link window.** A link another application hands the running
browser opens in a 480 by 640 window of its own at the top right of the screen
the main window is on, holding the page's domain, one "Open in <space>" button
and the page. The space is a routing rule's, else the space on screen. The
hook is `patches/0220-outside-links-mac.patch`, at the top of
`-[AppController application:openURLs:]`; it declines — and Chromium's own path
opens plain tabs — at a cold launch, when no window has a sidebar, or for
anything that is not http or https. Behind `ArciumOutsideLinkWindow`, on by
default.

**Routing rules.** A site and a space, stored beside the spaces in the model
file at schema version 5. The site is a host, lower-cased and without `www.`,
matching that host and its subdomains on label boundaries, longest rule
winning. Made and unmade from a row's right-click menu. They apply to outside
links and to an address chosen in the command box, and deliberately not to a
tab a page opens, which keeps a sign-in popup in its opener's storage.

**Commands and site search in the box.** Sixteen commands offered when every
word typed starts a word of the command's name or its hidden extra names, at
most three rows, always first, nothing under two letters. Thirteen are
Chromium's own command ids run through the command controller; New space, the
sidebar toggle and Site search shortcuts are Arcium's. A row that searches a
site other than the default engine now says which site it searches.

## Decisions taken while building, beyond the spec

- **The outside-link window is not a `views::WidgetDelegateView`.** That class's
  constructor is private to a closed friend list upstream, so a new subclass
  would need a patch. The window is a plain object that owns a
  `views::WidgetDelegate` and a `views::Widget` (CLIENT_OWNS_WIDGET) with a
  `views::View` of its own as contents. Every open window lives in one
  process-wide list, which owns them and gives the cascade its index.
- **The cascade goes down and to the left**, 24px per window already open,
  because the first window sits against the top right corner.
- **A peek's page and an outside link's page are told they are on screen**
  (`WebContents::WasShown`). They are created hidden and their tab is never the
  active one, so nothing else would ever tell them, and the renderer would have
  no reason to paint.
- **`BrowserSidebarController` was split.** The command box half moved to
  `browser_sidebar_controller_box.cc` when the peek member pushed the file past
  the size where it stops being readable.
- **All or nothing for a set of outside links.** One `file:` URL among them
  sends the whole set down Chromium's own path, which knows what to do with
  each; splitting them would open some windows and some tabs from one gesture.
- **The window a link belongs beside is the last active one with a sidebar**,
  found in activation order. That is also whose "space on screen" the fallback
  means.

## What the first build and the first run found

Built and run on 2026-09-15, the same day it was written. Four defects, all
found by compiling or by running, and all fixed:

- The small window asked the browser for its window and asked for the screen
  through accessors this Chromium no longer has (`Browser::window()`,
  `display::Screen::GetScreen()`); they are now `Browser::GetWindow()`, which
  answers a `ui::BaseWindow`, and `display::Screen::Get()`.
- Three of the new browser tests named the profile type without the
  declaration that says what it derives from, so the call taking a storage
  context refused a profile.
- Typing "clo" also offers duplicating a tab, because that command answers to
  "clone". The new unit test said two commands; there are three.
- Six older menu tests listed what a row's right-click menu offers and did not
  know about the routing item; their expectations now include it. A seventh
  asked whether a rule on a site is "in this space" through a *subdomain*: a
  rule covers the pages under its site when a link is routed, but the menu asks
  the narrower question of whether this exact site is routed here, so that it
  never offers to undo a rule belonging to another site.

675 unit tests pass, and so do all 63 browser tests. The thirteen new ones
among them cover a peek opening, closing, being promoted and being put away;
an outside link opening a window in the rule's space, two links making two
windows, non-web links declined, and the button moving the page into the main
window; an address in the box with and without a rule; and two box commands.

`gn check` was not run separately: the target builds, which is the thing the
check was a proxy for.

## What the first pass with a real pointer found

On 2026-09-16 twenty of the acceptance rows were driven against a running
browser from this machine's own keyboard and pointer: the sidebar-to-page
edge, the outside-link window in all four of its behaviours, a local file
declining that window, routing a site to a space and unrouting it, and five
of the six command rows. All twenty passed, including the two that only a
real launch can show: the rule survived a quit and relaunch, and the small
window named the rule's space rather than the space on screen.

Three defects turned up that no row asked about, and all three are fixed with
tests that fail without the fix:

- **A blank tab read "Untitled".** Switching to a space with nothing open in
  it lands the window on a tab that has been nowhere, and Chromium's word for
  a page with no title of its own is the wrong word for that: it describes a
  page that failed to name itself. The row now reads "New tab", which is what
  the tab is, decided by the tab having no committed address at all rather
  than by its title.
- **The pill was empty on a page that is not a website.** The design says a
  page with no host shows a short label rather than an empty pill, and the
  code returned an empty string for every such page -- with a unit test
  freezing that as though it were the rule. A local file is now named by its
  file, a page belonging to the browser says `chrome://settings`, and a tab
  that has gone nowhere says "New tab". `PillDomain` became `PillLabel`,
  because it no longer answers only with a domain.
- **Launching the browser while it was already running opened a second
  window.** One window holding one strip is the shape the sidebar, the spaces
  and the archive are built on, so a second window is a second copy of all of
  it. The launch now raises the window that exists and opens any addresses on
  its command line as tabs in it, routed the same way an address typed into
  the box is; a private window, an installed web app and another profile are
  declined and take Chromium's own path. One new patch,
  `0230-second-launch-one-window.patch`.

## What the second pass found, 2026-09-20

The peek rows, the close-a-tab row and the four site search rows were driven
by hand. All passed, and one defect was found and fixed:

- **The peek drew its card and nothing else.** The page underneath draws
  through a compositor layer of its own, and a view without one paints into
  its parent's layer, which sits beneath every layer inside it. The card
  showed, because a web view brings its own layer; the dimming around it and
  the close and open-as-tab buttons beside it did not, so the peek looked
  like a page that had simply replaced the one behind it, with no visible way
  out. The peek now paints to a layer of its own and is stacked above the
  rest. A browser test written first fails without the fix.

One thing was found that the design never promised, and it is now fixed:
**the box's rows answered the keyboard only.** Nothing happened under the
pointer and a click on a row did nothing at all, while plain Chromium's list,
checked the same afternoon through `--arcium-no-sidebar`, both marks the row
under the pointer and opens it on a click. A row now marks itself under the
pointer and takes itself when clicked, with the row Enter would take keeping
the stronger mark, so moving the mouse never quietly changes what Enter
opens. Writing the test for it turned up a second thing worth keeping: the
clicked row has to be copied rather than remembered by its number, because
answers keep arriving while the box is open and every new set renumbers the
rows, so a posted click would otherwise open whatever had moved into that
place.

## What is still unverified

- Nothing here is unverified by a test any more. The seven Stage 4a browser
  tests that were red on their first ever run were fixed the same day, two of
  them by fixing the browser rather than the test; see the Stage 4a row in
  `CLAUDE.md`.
- Thirty-two of the thirty-four hand rows are done. What is left is a link
  clicked in another application, which needs Arcium to be the default
  browser, and the Cmd+W question below. No perf measurement has been taken.
- No home-boundary *browser* test existed to update. The home boundary's unit
  tests use `BrowserWithTestWindowTest`, whose window is not a `BrowserView`,
  so `ShowPeekForNavigation` answers false there and their expectation — a new
  tab — is still the right one. That is worth re-reading after the first real
  run: it means those tests no longer cover what a real window now does.
- Cmd+W in the small window is the spec's own open question and cannot be
  settled without running it (A4b.2).

## Known risks, and what the build said about them

- `views::WidgetDelegate` used directly, with `SetTitle`, `SetCanResize` and
  `SetContentsView`, and `views::Widget::InitParams(CLIENT_OWNS_WIDGET,
  TYPE_WINDOW)`. It compiles and its tests pass, including the one that closes
  a window and requires the page's tab to go with it.
- `//arcium/ui/browser` gained `//ui/display` and `//components/search_engines`
  deps, and `suggestion_source.cc` includes
  `chrome/browser/search_engines/template_url_service_factory.h`, which is
  reached the way `FaviconServiceFactory` already is rather than through a dep.
  It builds.
- The peek's view is a child of the `BrowserView` that the browser's layout
  does not know about, positioned from `LayoutSidebar` like the sidebar itself.
  If the layout ever clears unknown children's bounds, the peek is where that
  shows.
- `OutsideLinkWindow` frees itself from a posted task after its widget reports
  it is being destroyed. If `CLIENT_OWNS_WIDGET` does not deliver
  `OnWidgetDestroying` for a widget closed by its own title-bar button, the
  window's page would outlive the window.
- The box's command rows carry no destination. Anything downstream that assumes
  a row has a valid URL would trip on them.
