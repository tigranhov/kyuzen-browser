# Releasing Kyuzen

A release is one command. It refuses rather than overwrites, and it publishes
nothing unless asked.

```bash
scripts/release 0.1.1 --notes notes.html --publish
```

## Before running it

1. Raise the version. `arcium/KYUZEN_VERSION` holds the version a reader sees
   and the build number an installed copy compares:

   ```
   VERSION=0.1.1
   BUILD=2
   ```

   The build number must increase on every release, or the feed refuses the
   entry, because an installed copy would never take it. Commit that change;
   the script refuses a tree with uncommitted work in it, so that a release
   names a commit.

2. Write the release notes as a fragment of marked-up text, for example
   `<p>The sidebar remembers its width.</p>`. They appear in the window that
   offers the update. Without `--notes` the entry says only the version.

## What it does

Builds the release configuration, signs the application, wraps it in a disk
image, signs that image with Sparkle's key, and adds an entry to the feed at
`$CHROMIUM_ROOT/releases/appcast.xml`. With `--publish` it also tags the
commit, uploads the image to GitHub Releases, and commits the feed to the
`gh-pages` branch, which holds the feed and nothing else.

Without `--publish` nothing leaves the machine, which is how the script is
tried out: run it twice into a `--feed-dir` of its own and read the feed it
writes.

## The two signatures

Apple's decides whether a Mac will open the application at all. Sparkle's
decides whether an update is one we published. They are independent: an
update installs when Sparkle's signature validates and the application is
signed at least ad-hoc.

The script signs with a Developer ID certificate when the machine has one and
says so; otherwise it signs ad-hoc and says that instead. An ad-hoc release
updates existing copies perfectly well, but a reader who downloads it by hand
meets Gatekeeper's refusal, so a public download wants a Developer ID.

## The signing key

Sparkle's key pair was made once, on this machine, and its private half lives
in the login keychain under the account `kyuzen`. A copy is kept in the
owner's password manager.

**If that key is lost, every installed copy is stranded on its version
forever**, because an update signed by any other key is refused by design.
There is no recovery except asking every reader to download the browser again.

To put the key on another machine:

```bash
arcium/third_party/sparkle/bin/generate_keys --account kyuzen -f key-file
```

The public half is in `arcium/kyuzen_updates.gni` and is built into every
release. Changing it strands every copy already installed, for the same
reason.

## When a release is wrong

Do not replace it. An installed copy that has already taken it will not take
the same build number again, and a copy that has not will take whatever the
feed says the newest is.

Release the fix instead: raise the version and the build number, and publish
again. If the bad release must not be installed by anyone else, delete its
entry from `appcast.xml` on the `gh-pages` branch and commit that; copies that
already have it are past helping, but nobody new will be offered it.

## What a reader sees

The setting is in Settings, under the version, and says "Keep Kyuzen up to
date": ask before installing, install quietly, or never check. Asking is what
a reader who has chosen nothing gets. A check that finds nothing is silent, a
check that fails is silent and writes one line to the log, and the About page
reports whatever the updater is doing.
