# Stage 2.6 — Pinned and favourite home boundary: findings

A cross-host link click inside a pinned or favourite tab opens a new Today tab
instead of navigating the entry away from its home. Five planned tasks,
executed subagent-driven with a fresh implementer per task and a review
between. What follows is what the stage cost and what it taught, not a summary
of the diff.

## What shipped

**The policy is a pure function of three URLs.**
`arcium/browser/model/home_boundary.h` exports one symbol:

```cpp
bool LinkLeavesHome(const GURL& current, const GURL& target, const GURL& home);
```

It has no opinion about tabs, navigations or Chromium. Its target keeps the
model rule — deps are exactly `//base` and `//url`.

**The adapter holds no policy.** `HomeBoundaryThrottle` is a
`content::NavigationThrottle`. `MaybeCreateAndAdd` decides whether this
navigation is even a candidate — primary main frame, not a reload, a tab, a
tab bound to a pinned or favourite entry — and `WillStartRequest` asks the four
questions a throttle is uniquely able to answer (link click, user gesture, not
back/forward) before handing the three URLs to the rule.

**The seam is `NavigationThrottle`, not `OpenURLFromTab`.** A plain same-frame
link click never reaches `WebContentsDelegate::OpenURLFromTab`, so the obvious
seam would have caught nothing. `web_app::TabbedWebAppNavigationThrottle` is
the in-tree precedent for exactly this shape of rule.

**Two upstream lines, in two patches.** `0150` adds the include and the
`MaybeCreateAndAdd` call beside the tabbed-web-app throttle. `0145` gives
`chrome/browser:core` a GN dep on `//arcium/ui/browser`.

## The divergence from Zen

Zen asks one question: is the link's host different from the host of the page
currently loaded? Arcium asks two — different from the current page **and**
different from the host stored on the entry. The second clause is condition 7,
and it is the whole difference.

It matters after a tab has been carried off its home deliberately, by typing in
the URL pill. Under Zen's rule, a link from there back to the entry's own home
host is "cross-host" and gets diverted, so the pinned tab can never walk itself
home. Under condition 7 it stays. Deleting the clause reverts the behaviour
exactly, and the three tests named `Divergence*` are the ones that fail when it
goes — that naming is load-bearing and was made true, not assumed (`a71aeaa`).

## What the tests could not have caught

Two production defects were found by review, not by a red test, and both were
verified against Chromium source before a fix was written.

**The popup blocker eats a gestureless divert.** A programmatic
`anchorElement.click()` *is* a link click — Blink sets
`kWebNavigationTypeLinkClicked` because a triggering event exists — but it
carries no user gesture. `OpenURLParams::FromNavigationHandle` copies
`user_gesture` through, `ShouldBlockPopup` returns `kNoGesture` on a false one,
and `ConsiderForPopupBlocking` treats `NEW_FOREGROUND_TAB` as eligible. The
throttle would have cancelled the navigation and the replacement open would
have been silently dropped: the click does nothing at all. Fixed by requiring
`HasUserGesture()` (`ae6e146`).

**`gn check` catches what `ninja` does not.** `chrome/browser:core` reached
`//arcium/ui/browser` through a *private* dep on `//chrome/browser/ui:ui`,
which does not forward. Ninja links it happily; `gn check` rejects it, and
`//chrome/browser:core` is not in `.gn`'s `no_check_targets`. Fixed by the
companion GN patch rather than by making patch `0010`'s dep public, because
patch `0125`'s header records that widening that dep breaks `scripts/sync`'s
idempotence.

## Mutation testing earned its seat

Every TDD step in this stage was followed by deleting the code just written and
confirming the named test failed. It found three gaps a green suite was hiding:

- `ANonHttpSchemeStays` used `mailto:`, which has no host, so `has_host()`
  caught it before the scheme guard it was named for ever ran.
- `AnUnboundTabGetsNoThrottle` never constructed `ArciumProfileState`, so an
  earlier guard shielded the code under test.
- One reversibility claim in a comment was simply false until the test it
  depended on was split out and renamed.

None of these would have shown up as a failure. All three were tests that
passed for the wrong reason.

## The acceptance pass

A2.6.1 through A2.6.9 executed by hand 2026-09-10 against a local four-host
harness (`home.localhost`, `www.home.localhost`, `other.localhost`,
`third.localhost`, one Python server on loopback). Two items cannot be done on
the real web — A2.6.8 needs a page on a bare host linking to its own `www.`
form, and condition 7 needs a page on a foreign host linking back to the
entry's home — so a harness is not a convenience here, it is the only way to
run them.

**All nine passed. No defects found.** That is the first acceptance pass in
this project to find nothing, and it is worth saying why: two of the three
things that would have failed it were found by review before the browser was
ever launched, and the third was found by mutation testing.

## Still outstanding

- Perf not measured for this stage. Zero new processes, no new allocation per
  tab, and the throttle runs only on navigations in bound tabs, so the four
  questions answer themselves — but `scripts/perf` has not been run.
- `patches/0065-mac-titlebar-height.patch` is untracked and applied by
  `scripts/sync`, so every build behind this stage's evidence includes a patch
  the repository does not contain. Pre-existing on `main`, not introduced here.
- Stage 2.5's A2.5.1 collapse-then-relaunch half is still not run.
