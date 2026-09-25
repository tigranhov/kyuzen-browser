# Stage 4a: the address pill, extensions and the command box

Design: `docs/superpowers/specs/2026-09-13-stage-4a-pill-and-extensions-design.md`.
Plan: `docs/superpowers/plans/2026-09-14-stage-4a-pill-and-extensions.md`.

This stage was pulled forward out of Stage 4 for one reason the owner gave
plainly: extensions installed and then had nowhere to appear, and the address
bar in the sidebar looked wrong. Until both were fixed the browser could not be
used for real work, whatever else it could do.

## What shipped

**The pill shows where you are, not the whole address.** The host with a
leading `www.` dropped and the port kept: `google.com`, `mail.google.com`,
`localhost:8899`. Deliberately not the registrable domain, which would answer
`google.com` on `mail.google.com` and tell the reader they are somewhere they
are not. Punycode stays punycode, because unicode is how a spoofed host hides.
A page that is not a website shows nothing rather than a fragment of a scheme.

**Three buttons, hidden until wanted.** Site information at the leading edge,
extensions and copy at the trailing edge, revealed by hover *or by keyboard
focus* — a control only a pointer can reach is not a control everyone can
reach. The warning for a connection that is not secure is the exception and
shows without being asked for: an affordance nobody sees is a click saved, and
a warning nobody sees is the warning gone.

**The address bar behind the pill draws nothing, and is still there.** This is
the finding that shaped the stage. Taking Chromium's `LocationBarView` out was
the tempting fix and the wrong one: Chrome's password and page information
bubbles find their anchor through that view, and a permission request is not
merely anchored to it but is a chip the bar owns, which refuses to appear when
the bar is not in a valid state. So it stays, drawn and silent, with the pill
drawn over it, and the pill steps aside entirely whenever the chip has
something to ask.

**Extensions have a home.** Chromium's strip of pinned extension buttons is
moved out of the toolbar this browser never lays out and into a row above the
favourites — the same move the address bar makes one row up, which keeps the
toolbar's pointer to it valid so extension popups still find their owner. The
row wraps onto a second line rather than dropping what does not fit, because
the strip's own layout is one line and a 250px sidebar holds about seven. An
empty row is nothing at all rather than a thin gap. The strip's menu button
stays out of it: auto-hide mode is the upstream way to ask for exactly that,
and the menu opens from the pill's extensions button instead.

**The box answers while you type.** Cmd+T, Cmd+L and a click on the pill all
open one box. It asks two sources at once: this browser's own live tabs, pinned
entries and archived pages, and Chrome's history, bookmarks and search engine.
What the reader already put somewhere comes first, and a page in both halves is
offered once. A row that is a tab they already have switches to it, and to its
space, rather than loading a second copy. Everything behind the box is built
when it opens and destroyed when it closes, so an idle browser carries no
providers and no timers.

**Bookmarking was dropped permanently**, at the owner's decision: spaces and
pinned tabs already are this browser's answer to it, and Cmd+D is not wired.

## Where the seams are

Three upstream patches, all hooks that carry no logic:

- `0200-location-bar-hosted.patch` — the hosted bar paints no border and no
  background, and tells the pill after each layout what it is showing.
- `0210-extensions-container-autohide.patch` — a window with a sidebar asks for
  auto-hide mode.
- `0090-new-tab-quick-entry.patch`, extended — `IDC_FOCUS_LOCATION` joins
  `IDC_NEW_TAB`, both opening the box.
- `0050-browser-view-sidebar.patch`, extended by one line — the extensions
  strip is reparented at the moment the address bar already is.

## What a test covers, and what a person must

Covered by tests that run without a screen: what the pill shows for nine kinds
of address; when each of its three buttons is visible, including that they go
away again and that a permission request hides all three; that each button runs
its own action and not another; that Chrome's answers become rows in order,
that a row with nowhere to go is dropped, and that the merge puts this
browser's own answers first and offers a page once. 642 unit tests pass.

Sixteen browser tests are written for the rest: the hosted bar drawing nothing
while the permission chip still appears, the pill following the page's
security, copy putting the whole address on the clipboard, page information
opening, a pinned extension being a button in the row, nine of them wrapping,
unpinning taking one back out, the box answering, Enter on an open tab
switching rather than duplicating, Escape leaving the page alone, clicking the
pill opening the box, nothing running while it is closed, and Cmd+L holding the
whole address selected.

**These sixteen have not been run.** They open real windows on macOS and take
over the screen, and the machine was in use for the whole of this stage's
implementation. Running them is the first thing to do before this stage can be
called done. The seven hand rows in `scripts/acceptance-4a` have not been run
either, and no perf measurement has been taken.

## Defects found in use, 2026-09-25

The owner reported three things wrong with the pill and the extensions row in
daily use, and all three were defects rather than design. Each has a test
written first and watched to fail.

- **The pill flickered under the pointer.** It read the pointer arriving on one
  of its own buttons as the pointer leaving, hid the buttons, which put the
  pointer back on the pill, which showed them again, for as long as the pointer
  moved. The pill now counts its buttons as part of itself
  (`UrlPillTest.ReachingForAButtonDoesNotHideIt`). The address also moved
  aside when the site button arrived; at the owner's choice the site button's
  room is now kept at all times, so the address never moves, and that room is
  given up while the bar behind the pill is asking a question, because the
  question starts there (`TheAddressStaysPutWhenTheButtonsArrive`,
  `AQuestionFromTheBarIsClickableAllTheWayAcross`).
- **A large puzzle piece sat over the pill.** The strip's own menu button is
  tucked above the row, where the row's edge cuts it off -- but only for what
  paints into the strip. Lit, as it is while the menu hanging from it is open,
  it draws on layers of its own, which nothing clipped. The row now paints to
  a layer that masks to its bounds. Giving the button no room instead was
  tried and is wrong: the strip then drops pinned extensions
  (`ExtensionsRowTest.APinnedExtensionIsAButtonInTheSidebar`, mutation-checked).
- **A large outlined box surrounded the extensions.** Chromium outlines the
  strip while the pointer is over it; the row makes the strip as wide as the
  sidebar, so the outline was a mostly empty box with the buttons in its
  corner. The outline's layer is removed when the strip is hosted, and each
  button still lights up by itself
  (`ExtensionsRowTest.TheStripDrawsNoOutlineAroundItself`).

The owner then found the row roomier than its icons, which was the design and
not a defect: 26-point buttons 6 apart round icons Chromium always draws at
16, so 16 points of nothing between two icons. Codex Astra proposed two
tighter layouts and the owner took the compact one: 24-point buttons 4 apart,
12 between icons, eight to a line, the first icon in the favicons' column, 4
above the row and 6 below (`ExtensionsRowLayoutTest`). A badge's outer edge
may lose 2 points to the smaller button, which the proposal accepted.

## What this stage does not cover

The command bar's non-navigation half — commands rather than destinations —
is still Stage 4's work and is not here. Nor is anything that would rebuild
Chromium's page information: the site button opens Chrome's own bubble,
deliberately, because it is a security surface Chromium already writes,
maintains and translates, and a copy of it would buy nothing a reader could
see. Zen merges security, permissions and add-ons into one panel; this does
not, and that is a decision rather than an omission.

One judgement worth recording for whoever runs the pass: the spec asked for a
unit test of "whether a URL counts as not secure". There is no URL-shaped
answer to that question — the judgement belongs to `SecurityStateTabHelper`,
which needs a real navigation and a real certificate state to have an opinion —
so it is covered by two browser tests instead, at both ends: a plain `http`
page wears the warning, and an internal page must not be accused of anything.
