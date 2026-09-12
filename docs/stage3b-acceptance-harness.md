# Running the Stage 3b acceptance pass

`scripts/acceptance-3b` builds the world the seven remaining rows assume and
launches the browser into it. The pass still needs a person — the rows are
about dragging, clicking and judging — but none of it should need an hour of
setup first.

```
scripts/acceptance-3b          # clean profile, seeded
scripts/acceptance-3b --keep   # same profile again, for the "relaunch" rows
```

It makes three spaces across two profiles (`Personal` and `Reading` on the
shared logins, `Work` on a profile of its own) and serves one page on
loopback that shows which account *this storage* is signed in as, with links
to sign in as `ada` or `grace`. Closing the browser stops the site.

## The one substitution, stated plainly

**A3.1 says two real accounts. This harness uses two local cookies instead.**

That is a deliberate substitution and a reader should not assume otherwise.
The row exists to prove that one site can hold two independent logins in two
spaces and that both survive a relaunch. A cookie set by a loopback page
proves exactly that, because the isolation under test is the storage
partition's, not the remote service's — the same reasoning the Stage 2.6 pass
used when it ran against a local four-host harness, where a harness was the
only way to run the rows at all.

What the substitution does **not** cover: anything a real provider does that a
cookie does not — third-party sign-in flows, federated redirects between
hosts, and long-lived refresh tokens. If you want that covered, run A3.1 a
second time against a real site you already have two accounts for. The
harness is there to make the first pass quick, not to retire the question.

No credential is typed into this harness, and none should be.

## What each row needs

| Row | What to do | What must be true |
|---|---|---|
| A3.1 | Sign in as `ada` in Personal, `grace` in Work. Quit, rerun with `--keep` | Both names stick, each in its own space; after the relaunch both are still signed in and nothing loads until you click a tab |
| A3b.2 | Drag the signed-in Work tab onto Personal | It stays where dropped, same page and back history, and now reads `ada` |
| A3b.3 | Space menu on Work, clear that profile's data | Work reads `nobody`, Personal still reads `ada`, no page reloads |
| A3b.4 | Space menu on Work, delete the profile. Quit, rerun with `--keep` | Work moves to the shared logins and reads `ada`; the profile is gone from the menu and stays gone |
| A3b.5 | Chrome's Clear browsing data, cookies ticked | The warning appears and says it cannot be undone; "shared logins only" leaves Work signed in; "every space too" signs both out |
| A3b.6 | Install an extension, uninstall it, relaunch twice with `--keep` | Every profile still signed in, both times |
| A3b.7 | Open the extension's options page from Work and from Personal | The same saved settings both times |
| A3.3 (in-use half) | Both spaces signed in with pages loaded; watch Activity Monitor while switching | No process appears for a profile whose tabs are not loaded |

A3.3's idle half is already measured and needs nothing from you — see the
A3.3 section of `docs/perf/2026-09-12-stage3b.md`.

## If the browser opens with no spaces

The script checks for this and refuses, but the reason is worth knowing. A
model file this build cannot understand is **moved aside** rather than
ignored, leaving an empty model. If that happened, every row would fail for a
reason that has nothing to do with profiles. The script looks for the
moved-aside file after the browser exits and says so rather than letting a
meaningless pass stand.
