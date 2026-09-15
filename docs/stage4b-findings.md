# Stage 4b: peek, the outside-link window, routing rules and box commands

Design: `docs/superpowers/specs/2026-09-15-stage-4b-navigation-design.md`.

**Nothing here has been compiled or run.** The whole stage was written while a
forced full Chromium rebuild held the one checkout, at the owner's request, so
every API below was checked by reading Chromium's headers rather than by
building against them. Treat the first build as part of the work, not as a
formality, and read the risk list at the end before starting it.

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

## What is unverified

Everything, in the literal sense: no compile, no unit test run, no browser test
run, no acceptance row, no perf measurement. In particular:

- The sixteen browser tests of Stage 4a are still unrun, and the four new files
  here (`peek_browsertest.cc`, `outside_links_browsertest.cc`,
  `box_commands_browsertest.cc`, plus the existing ones) add to that backlog.
- The unit tests written with the model and command code — routing rules,
  serialiser round trip, the version 4 to 5 migration, command matching, the
  row menu items, the loose-page marker — have not been run either.
- No home-boundary *browser* test existed to update. The home boundary's unit
  tests use `BrowserWithTestWindowTest`, whose window is not a `BrowserView`,
  so `ShowPeekForNavigation` answers false there and their expectation — a new
  tab — is still the right one. That is worth re-reading after the first real
  run: it means those tests no longer cover what a real window now does.
- Cmd+W in the small window is the spec's own open question and cannot be
  settled without running it (A4b.2).

## Known risks for the first build

- `views::WidgetDelegate` used directly, with `SetTitle`, `SetCanResize` and
  `SetContentsView`, and `views::Widget::InitParams(CLIENT_OWNS_WIDGET,
  TYPE_WINDOW)`. Read from the headers and from two upstream callers; not
  compiled.
- `//arcium/ui/browser` gained `//ui/display` and `//components/search_engines`
  deps, and `suggestion_source.cc` includes
  `chrome/browser/search_engines/template_url_service_factory.h`, which is
  reached the way `FaviconServiceFactory` already is rather than through a dep.
  `gn check` on the target is the thing to run first.
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
