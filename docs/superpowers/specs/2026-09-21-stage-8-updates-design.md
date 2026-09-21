# Stage 8a: updating itself

How a copy of Kyuzen on somebody's Mac learns that a newer one exists, and
replaces itself with it. Covers the updater inside the browser, the setting
that governs it, the feed it reads, and the script that makes a release.

Out of scope: the Developer ID certificate and notarisation (they gate
distribution, not this design), Windows and Linux, and channels other than one.

## Why Sparkle

Updating is the one part of the product that reaches out, downloads code and
replaces the application on disk, and it is the part whose bugs cannot be
fixed by shipping a fix: the broken copy is the one that would have to install
it. Sparkle has been doing this on macOS since 2006, and the owner's reason
for choosing it was exactly that -- not chasing installation and update bugs
later.

The alternatives were writing our own, which puts that risk on us, and
Chromium's own updater, which is built for managing a fleet of machines and
installs a privileged helper to do it.

**The rule it bends.** The project forbids AppKit and Cocoa code, and Sparkle
is a Cocoa framework with windows of its own. That rule is about the browser's
interface, which stays in Chromium's toolkit; updating is a platform mechanism
of the same kind as the ones Chromium already hides behind abstractions. We
take Sparkle's standard interface as it comes and write none of our own, so
the Objective-C++ in this project stays at one small file.

## What a person sees

A setting, **Keep Kyuzen up to date**, with three values:

| Value | Behaviour |
|---|---|
| Ask me (default) | Checks daily. When a version is found, a window shows what changed and offers to install. |
| Install quietly | Checks daily, downloads in the background, installs on the next quit. |
| Never check | No checking, no network traffic. A manual check is still possible. |

The default is asking, which is what Zen does, because a browser replacing
itself without being asked is a surprise even when it is welcome.

Chromium's own About page already asks an updater for its status and offers a
Relaunch button, so it keeps working as built: Kyuzen answers those questions
from Sparkle rather than leaving them unanswered, which is also how a manual
check is made.

Nothing else appears. No dock badge, no notification, no interface of ours.

## Architecture

Four pieces, three of them small.

**`UpdateController` (`arcium/browser/update/`).** Owns the Sparkle updater
object, reads the setting, starts checking, and answers status questions. Its
header is plain C++ with no Objective-C in it; the implementation is one `.mm`
file. It is created after the first window has painted -- a posted task with a
short delay -- so it adds nothing to startup.

**`UpdateStatus`, an interface.** What the About page needs: current state,
whether a relaunch is pending, and a request to check now. `UpdateController`
implements it against Sparkle; a fake implements it in tests. This is the seam
that makes the feature testable without a framework, a network or a feed.

**The framework.** Sparkle 2.10.0, pinned by version and verified by hash,
fetched by `scripts/fetch-sparkle` into `arcium/third_party/sparkle/` and not
committed. `scripts/sync` fetches it if missing, the way it already applies
patches, so a fresh checkout builds without extra steps.

**The feed and the release script**, below.

### Patches

Three, all of them wiring:

1. `chrome/browser/ui/webui/help/version_updater_mac.mm` -- `VersionUpdater::Create`
   returns Kyuzen's implementation, so the About page reads Sparkle.
2. `chrome/BUILD.gn` -- copy `Sparkle.framework` into the outer app bundle's
   `Contents/Frameworks`, beside Chromium's own.
3. `chrome/app/app-Info.plist` -- the feed address and the public signing key.
   This extends patch `0240`, which already adds a key there.

### What the four performance questions say

1. **A process, or one kept alive longer?** No. Sparkle's installer runs as a
   separate short-lived process only while an update is being installed, and
   nothing runs between checks.
2. **Idle memory?** The framework is loaded and one object exists; the daily
   check is a timer. Measured at the end of the stage against the budget.
3. **Work before first paint?** None. The updater is created after the first
   window has painted, on a delay.
4. **UI-thread work that is not for this frame?** Checking, downloading and
   verifying happen off the UI thread inside Sparkle. Our own code reads a
   preference and posts a task.

## Versions

Kyuzen gets a version of its own, starting at **0.1.0**, held in a
`KYUZEN_VERSION` file beside `CHROMIUM_VERSION` together with a build number
that only ever increases.

The application's short version string becomes the Kyuzen version, which is
what the About page and the Finder show, and the bundle version becomes the
build number, which is what Sparkle compares. Chromium's own version stays
visible on the About page beside ours, because knowing which Chromium a build
carries matters when a site misbehaves.

**Risk:** the bundle version is Chromium's today, and something may read it.
Crash reporting does not -- it uses a compile-time constant -- but this is
checked by running the browser and looking for anything that reports a version
of `0`.

## The feed

`https://tigranhov.github.io/kyuzen-browser/appcast.xml`, served by GitHub
Pages from a `gh-pages` branch holding only the feed and release notes, so that
publishing a release never publishes this repository's working documents.

Disk images live on GitHub Releases in the same repository. Both are public and
need no credentials, which an updater on somebody else's machine cannot have.

Each entry names the version, the build number, the download, its length, its
signature, and the minimum macOS version.

## Signing a release

Two signatures, doing different jobs. Apple's, which decides whether the Mac
will open the application at all, and Sparkle's own, which decides whether an
update is the one we published. They are independent: Sparkle accepts an update
when its own signature validates and the application is signed at least
ad-hoc, which is why updating can be tested and used before the Developer ID
certificate exists.

Sparkle's key pair is made once with its own tool and kept in the login
keychain of the machine that makes releases. **If that key is lost, every
installed copy is stranded forever**, because an update signed by a new key is
refused by design. It is exported and kept somewhere else before the first
release.

## Making a release

`scripts/release <version>`: one command, refusing rather than overwriting.

1. Refuse a dirty tree, a version that already has a tag, or a build number
   that does not increase.
2. Build the release configuration.
3. Sign with Developer ID when one is present, ad-hoc otherwise, and say which.
4. Make a disk image.
5. Sign the image with Sparkle's key.
6. Write the entry, with release notes taken from a file the release names.
7. Publish the image to GitHub Releases and the entry to the feed branch.
8. Tag the commit.

Steps 1 to 6 are local and can be run against a local feed, which is how the
script is tested before anything is published.

## Errors

| What happens | What the browser does |
|---|---|
| No network, or the feed is unreachable | Nothing, silently. Tries again at the next check. |
| Feed is malformed | Nothing, and a log line. A feed we broke must not break the browser. |
| Signature does not validate | Refuses the update and says so. Never installs. |
| The offered version is not newer | Refuses. Sparkle compares build numbers. |
| Installation fails | The existing application is left untouched; Sparkle installs by replacing atomically. |
| The reader declines | Offered again at the next check, not immediately. |

## How it is tested

- Unit tests over the mapping from the setting to the updater's behaviour, and
  over what `UpdateStatus` reports in each state, using the fake.
- A browser test that the About page shows what the fake reports, which proves
  the patched seam reaches our code.
- Unit tests over the feed entry the release script writes, including that a
  version which does not increase is refused.
- By hand, once: a local feed offering a newer build, installed into a copy of
  the application, and the relaunched copy reporting the new version. This is
  the only test that proves the whole path, and it is run before the first
  public release.

## Open questions

- **Whether library validation blocks an ad-hoc build from loading Sparkle.**
  Apple's documentation says a signed application may refuse a framework signed
  by somebody else. Our builds do not enable the hardened runtime, so it should
  not apply; the by-hand test settles it.
- **Release notes.** Written by hand per release for now. Generating them from
  commit messages is a later question.
- **One channel only.** No beta. Adding one later means a second feed address
  and a value in the setting.
