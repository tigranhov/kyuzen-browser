# Updating Itself Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A released copy of Kyuzen finds newer versions, asks before installing one, and replaces itself with it.

**Architecture:** Sparkle 2.10.0 ships inside the application bundle. A small controller in `arcium/browser/update/` owns it, reads one preference and answers status questions through an interface that a fake also implements, so everything except the framework itself is testable. Chromium's About page is redirected to that controller by a patch. A release script builds, signs, publishes a disk image to GitHub Releases and appends an entry to a feed served by GitHub Pages.

**Tech Stack:** C++ and one Objective-C++ file, Chromium Views (no new interface), GN, Sparkle 2.10.0, bash, GitHub Releases and Pages.

**Spec:** `docs/superpowers/specs/2026-09-21-stage-8-updates-design.md`

## Global Constraints

- Sparkle is pinned to **2.10.0** and verified by SHA-256 before use. Never "latest".
- Feed address: `https://tigranhov.github.io/kyuzen-browser/appcast.xml`.
- Bundle identifier: `io.github.tigranhov.kyuzen`. Product version starts at `0.1.0`, build number `1`.
- The default setting value is **ask before installing**.
- All logic lives in `arcium/`. Patches are wiring only, each with a `Seam:`/`Why:`/`Delegates to:` header. Run `scripts/sync` after touching `patches/` and confirm the count.
- No Cocoa outside the single `.mm` file that owns the Sparkle object. No new interface of our own.
- Nothing runs before first paint; no work on the UI thread beyond reading a preference and posting a task.
- Format with `scripts/format`. Never `git cl format`.
- Commit by explicit path, never `git add -A`. Messages say why, carry no task numbers, and end with `Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>`. Do not push.
- Never build while a browser is running from the output directory being built.

---

### Task 1: A version of its own

**Files:**
- Create: `KYUZEN_VERSION`
- Modify: `patches/0240-product-dir-name.patch`
- Test: verified by reading the built bundle, plus `arcium/test/version_unittest.cc`

**Interfaces:**
- Consumes: nothing.
- Produces: `arcium::ProductVersion()` returning `"0.1.0"` and `arcium::BuildNumber()` returning `1`, both from generated constants; the built bundle's `CFBundleShortVersionString` is the product version and its `CFBundleVersion` is the build number.

- [ ] **Step 1: Write the file**

`KYUZEN_VERSION`:

```
VERSION=0.1.0
BUILD=1
```

- [ ] **Step 2: Write the failing test**

`arcium/test/version_unittest.cc`:

```cpp
TEST(VersionTest, TheProductVersionIsNotChromiums) {
  // The browser has to be able to say which Kyuzen a reader is running, and
  // Chromium's number cannot: two Kyuzen releases can carry the same one.
  EXPECT_EQ("0.1.0", arcium::ProductVersion());
  EXPECT_GE(arcium::BuildNumber(), 1);
}
```

- [ ] **Step 3: Run it and watch it fail**

Run: `scripts/build dev arcium_unittests && out/dev/arcium_unittests --gtest_filter=VersionTest.*`
Expected: does not compile -- no such function.

- [ ] **Step 4: Generate the constants**

Read `KYUZEN_VERSION` in `arcium/common/BUILD.gn` with `read_file`, and write `arcium/common/product_version.h` declaring the two functions with a `.cc` returning the values as `constexpr` strings.

- [ ] **Step 5: Put them in the bundle**

Extend `patches/0240-product-dir-name.patch` so `app-Info.plist` carries
`CFBundleShortVersionString = ${KYUZEN_PRODUCT_VERSION}` and
`CFBundleVersion = ${KYUZEN_BUILD_NUMBER}`, and add both to the
`extra_substitutions` of `chrome_app` in `chrome/BUILD.gn` from the same
values. Re-run `scripts/sync` and confirm the patch count is unchanged at 41.

- [ ] **Step 6: Verify against a real build**

Run: `scripts/build release chrome`, then read the bundle:
`/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' out/release/Kyuzen.app/Contents/Info.plist`
Expected: `0.1.0`, and `CFBundleVersion` prints `1`.

- [ ] **Step 7: Check nothing else read the old bundle version**

Launch the built browser, open the About page and `chrome://version`, and
confirm neither reports an empty or zero version. Record what they show.

- [ ] **Step 8: Commit**

```bash
git add KYUZEN_VERSION arcium/common/product_version.h arcium/common/product_version.cc arcium/common/BUILD.gn arcium/test/version_unittest.cc arcium/test/BUILD.gn patches/0240-product-dir-name.patch
git commit -m "Give the browser a version of its own"
```

---

### Task 2: Fetch Sparkle, pinned and verified

**Files:**
- Create: `scripts/fetch-sparkle`
- Modify: `scripts/sync`, `.gitignore`
- Test: the script's own refusal path, exercised by hand

**Interfaces:**
- Consumes: nothing.
- Produces: `arcium/third_party/sparkle/Sparkle.framework` and `arcium/third_party/sparkle/bin/` (the `generate_keys`, `sign_update` and `generate_appcast` tools), both git-ignored.

- [ ] **Step 1: Write the script**

It downloads `https://github.com/sparkle-project/Sparkle/releases/download/2.10.0/Sparkle-2.10.0.tar.xz`, compares its SHA-256 against a constant in the script, refuses loudly on a mismatch without unpacking, and unpacks into `arcium/third_party/sparkle/`. It does nothing when the framework is already present and the recorded version matches.

- [ ] **Step 2: Run it**

Run: `scripts/fetch-sparkle`
Expected: the framework exists and `codesign -dv` on it names the Sparkle Project.

- [ ] **Step 3: Prove the refusal**

Change the expected hash by one character, delete the framework, run again.
Expected: it refuses and leaves nothing behind. Restore the hash.

- [ ] **Step 4: Call it from sync**

`scripts/sync` calls it before applying patches, so a fresh checkout builds.

- [ ] **Step 5: Commit**

```bash
git add scripts/fetch-sparkle scripts/sync .gitignore
git commit -m "Fetch the update framework at a pinned version"
```

---

### Task 3: The setting and the seam, with no framework behind them

**Files:**
- Create: `arcium/browser/update/update_status.h`, `arcium/browser/update/update_preference.h`, `arcium/browser/update/update_preference.cc`, `arcium/browser/update/BUILD.gn`
- Create: `patches/0245-register-arcium-prefs.patch`
- Create: `arcium/test/update_preference_unittest.cc`
- Modify: `arcium/test/BUILD.gn`

**Interfaces:**
- Consumes: Task 1's version constants.
- Produces: `enum class UpdateMode { kAsk, kAutomatic, kOff }`; `UpdateMode GetUpdateMode(PrefService*)`; `void RegisterUpdatePrefs(PrefRegistrySimple*)` registering `arcium.update_mode` in local state with `kAsk` as default; and `class UpdateStatus` with `State CurrentState()`, `bool RelaunchIsPending()`, `void CheckNow()`.

- [ ] **Step 1: Write the failing test**

```cpp
TEST_F(UpdatePreferenceTest, AsksBeforeInstallingUntilTheReaderSaysOtherwise) {
  // Asking is the default because a browser that replaces itself unasked is
  // a surprise even when the new version is wanted.
  EXPECT_EQ(UpdateMode::kAsk, GetUpdateMode(local_state()));
}

TEST_F(UpdatePreferenceTest, AValueThatIsNotOneOfTheThreeReadsAsAsking) {
  local_state()->SetInteger("arcium.update_mode", 47);
  EXPECT_EQ(UpdateMode::kAsk, GetUpdateMode(local_state()));
}
```

- [ ] **Step 2: Run them and watch them fail**

Run: `scripts/build dev arcium_unittests && out/dev/arcium_unittests --gtest_filter=UpdatePreferenceTest.*`
Expected: does not compile.

- [ ] **Step 3: Write the preference and the enum**

- [ ] **Step 4: Run them and watch them pass**

- [ ] **Step 5: Register it where the browser reads local state**

The setting is one per installation rather than one per profile, so it lives
in local state, and Chromium builds that registry in
`chrome/browser/prefs/browser_prefs.cc`. No seam exists there yet: write
`patches/0245-register-arcium-prefs.patch`, a hook calling
`arcium::RegisterUpdatePrefs`. Run `scripts/sync` and expect 42 patches.

- [ ] **Step 6: Commit**

```bash
git add arcium/browser/update patches/0245-register-arcium-prefs.patch arcium/test/update_preference_unittest.cc arcium/test/BUILD.gn
git commit -m "Remember whether updates are installed, offered or ignored"
```

---

### Task 4: The controller, against a fake updater

**Files:**
- Create: `arcium/browser/update/update_controller.h`, `arcium/browser/update/update_controller.cc`, `arcium/browser/update/updater_backend.h`, `arcium/test/fake_updater_backend.h`
- Create: `arcium/test/update_controller_unittest.cc`

**Interfaces:**
- Consumes: Task 3's `UpdateMode`, `UpdateStatus`.
- Produces: `class UpdaterBackend` with `SetChecksAutomatically(bool)`, `SetDownloadsAutomatically(bool)`, `CheckNow()`, `CheckInBackground()`; `class UpdateController : public UpdateStatus` taking a `std::unique_ptr<UpdaterBackend>` and a `PrefService*`.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_F(UpdateControllerTest, AskingChecksButDoesNotDownloadByItself) {
  SetMode(UpdateMode::kAsk);
  auto controller = MakeController();
  EXPECT_TRUE(backend()->checks_automatically());
  EXPECT_FALSE(backend()->downloads_automatically());
}

TEST_F(UpdateControllerTest, NeverCheckingAsksTheFeedForNothing) {
  SetMode(UpdateMode::kOff);
  auto controller = MakeController();
  EXPECT_FALSE(backend()->checks_automatically());
  EXPECT_EQ(0, backend()->background_checks());
}

TEST_F(UpdateControllerTest, AManualCheckHappensEvenWhenCheckingIsOff) {
  // Off means "do not go looking", not "refuse to look when asked".
  SetMode(UpdateMode::kOff);
  auto controller = MakeController();
  controller->CheckNow();
  EXPECT_EQ(1, backend()->manual_checks());
}

TEST_F(UpdateControllerTest, ChangingTheSettingTakesEffectWithoutARelaunch) {
  SetMode(UpdateMode::kOff);
  auto controller = MakeController();
  SetMode(UpdateMode::kAutomatic);
  EXPECT_TRUE(backend()->downloads_automatically());
}
```

- [ ] **Step 2: Run them and watch them fail**

- [ ] **Step 3: Write the controller**

It observes the preference, maps it onto the backend, and reports state through `UpdateStatus`.

- [ ] **Step 4: Run them and watch them pass**

- [ ] **Step 5: Mutation-check the last one**

Remove the preference observer and confirm `ChangingTheSettingTakesEffectWithoutARelaunch` fails. Restore it.

- [ ] **Step 6: Commit**

```bash
git add arcium/browser/update arcium/test/fake_updater_backend.h arcium/test/update_controller_unittest.cc arcium/test/BUILD.gn
git commit -m "Drive the updater from the setting"
```

---

### Task 5: Sparkle behind the backend

**Files:**
- Create: `arcium/browser/update/sparkle_backend.mm`, `arcium/browser/update/sparkle_backend.h`
- Create: `patches/0250-sparkle-framework-bundle.patch`
- Modify: `patches/0240-product-dir-name.patch`, `arcium/browser/update/BUILD.gn`

**Interfaces:**
- Consumes: Task 4's `UpdaterBackend`.
- Produces: `std::unique_ptr<UpdaterBackend> MakeSparkleBackend()`; `Sparkle.framework` inside `Kyuzen.app/Contents/Frameworks`; `SUFeedURL` and `SUPublicEDKey` in the application's property list.

- [ ] **Step 1: Make the signing key**

Run `arcium/third_party/sparkle/bin/generate_keys`. Record the public key. **Export the private key and keep it off this machine as well** -- losing it strands every installed copy forever.

- [ ] **Step 2: Write the backend**

One `.mm` file owning an `SPUStandardUpdaterController`, forwarding the four calls. No interface of ours, no AppKit beyond what Sparkle needs.

- [ ] **Step 3: Write the bundling patch**

`chrome/BUILD.gn` copies `Sparkle.framework` into the outer bundle. Header says it carries no call: it is GN wiring.

- [ ] **Step 4: Extend the plist patch**

Add `SUFeedURL` (the constant above) and `SUPublicEDKey` (from step 1).

- [ ] **Step 5: Sync and build**

Run: `scripts/sync` (expect 43 patches), then `scripts/build release chrome`.
Expected: `Kyuzen.app/Contents/Frameworks/Sparkle.framework` exists.

- [ ] **Step 6: Prove it loads**

Launch the built browser and confirm it starts and stays up. A framework that
library validation refuses would prevent launch, which is the open question in
the design.

- [ ] **Step 7: Commit**

```bash
git add arcium/browser/update patches/0250-sparkle-framework-bundle.patch patches/0240-product-dir-name.patch
git commit -m "Carry the update framework inside the application"
```

---

### Task 6: The About page reads our updater

**Files:**
- Create: `patches/0260-version-updater-arcium.patch`, `arcium/browser/update/arcium_version_updater.h`, `arcium/browser/update/arcium_version_updater.cc`
- Create: `arcium/test/browser/update_browsertest.cc`

**Interfaces:**
- Consumes: Task 4's `UpdateStatus`.
- Produces: `std::unique_ptr<VersionUpdater> arcium::MakeVersionUpdater()`, returned from Chromium's `VersionUpdater::Create` on macOS.

- [ ] **Step 1: Write the failing browser test**

Through Chromium's own entry point rather than our function, so it fails if the patch is not applied:

```cpp
IN_PROC_BROWSER_TEST_F(UpdateTest, TheAboutPageReportsWhatTheUpdaterKnows) {
  SetFakeState(UpdateStatus::State::kUpdateAvailable);
  std::unique_ptr<VersionUpdater> updater = VersionUpdater::Create(web_contents());
  StatusRecorder recorder;
  updater->CheckForUpdate(recorder.callback(), {});
  RunLoopUntilIdle();
  EXPECT_EQ(VersionUpdater::UPDATING, recorder.last_status());
}
```

- [ ] **Step 2: Run it and watch it fail**

Run: `scripts/build dev arcium_browsertests && out/dev/arcium_browsertests --gtest_filter=UpdateTest.*`
Expected: fails -- Chromium's own implementation answers, reporting no updater.

- [ ] **Step 3: Write the adapter and the patch**

- [ ] **Step 4: Run it and watch it pass, then sync and count patches (44)**

- [ ] **Step 5: Commit**

```bash
git add arcium/browser/update patches/0260-version-updater-arcium.patch arcium/test/browser/update_browsertest.cc arcium/test/BUILD.gn
git commit -m "Answer the About page from the real updater"
```

---

### Task 7: The setting, where a reader can reach it

**Files:**
- Create: `patches/0270-settings-update-row.patch`
- Modify: `arcium/branding/strings/settings_arcium_strings.grdp`

**Interfaces:**
- Consumes: Task 3's preference.
- Produces: a row in Chromium's settings page bound to `arcium.update_mode`.

- [ ] **Step 1: Add the strings**

"Keep Kyuzen up to date", and the three values, worded as the design states them.

- [ ] **Step 2: Write the patch**

A row in the About section of Chromium's settings resources, bound to the
preference. Header says it carries no call: it is a resource table keyed by
file name.

- [ ] **Step 3: Sync and count patches (45), then verify by hand**

Open the settings page, change the value three times, quit and relaunch, and
confirm the value held and that the About page behaves accordingly.

- [ ] **Step 4: Commit**

```bash
git add patches/0270-settings-update-row.patch arcium/branding/strings/settings_arcium_strings.grdp
git commit -m "Let the reader choose how updates arrive"
```

---

### Task 8: Making a release

**Files:**
- Create: `scripts/release`, `arcium/test/release_entry_unittest.py`
- Create: `docs/releasing.md`

**Interfaces:**
- Consumes: Task 1's version file, Task 5's signing key.
- Produces: a signed disk image on GitHub Releases, an entry in the feed, and a tag.

- [ ] **Step 1: Write the failing test for the entry**

Given a version, a build number, a file and a signature, the entry names all
of them; and a build number that does not increase past the newest entry is
refused.

- [ ] **Step 2: Run it and watch it fail**

- [ ] **Step 3: Write the script's local half**

Refusals, build, sign, disk image, sign the image, write the entry. It says
which Apple signature it used.

- [ ] **Step 4: Run it against a local feed**

Produce version `0.1.0` build `1`, then `0.1.1` build `2`, into a directory.
Expected: two entries, newest first, each with a signature.

- [ ] **Step 5: Add the publishing half**

`gh release create` and a commit to the feed branch, both behind a flag that
defaults to not publishing.

- [ ] **Step 6: Write `docs/releasing.md`**

The steps, where the key lives, and what to do when a release is wrong.

- [ ] **Step 7: Commit**

```bash
git add scripts/release arcium/test/release_entry_unittest.py docs/releasing.md
git commit -m "Make a release in one command that refuses to overwrite one"
```

---

### Task 9: Prove the whole path by hand

**Files:**
- Create: `docs/stage8-findings.md`
- Modify: `CLAUDE.md`

- [ ] **Step 1: Serve a local feed**

Build `0.1.0` build `1` and install it to `/Applications` -- an update
replaces the application where it sits, and the build directory is not where
a released copy lives, so testing from there proves nothing. Then then build `0.1.1`
build `2` and publish it to a feed served over HTTPS from this machine.

- [ ] **Step 2: Watch the offer**

With the setting on asking, wait for the check, confirm the window appears
and names the new version.

- [ ] **Step 3: Install and relaunch**

Accept, let it install, relaunch and confirm the About page reports `0.1.1`
and that the profile, spaces and logins survived.

- [ ] **Step 4: Prove it refuses a bad update**

Corrupt the signature in the feed entry and confirm the update is refused and
the installed copy is untouched.

- [ ] **Step 5: Prove the other two settings**

Quiet installs without asking; never-check asks the feed for nothing, seen
with `scripts/netaudit`.

- [ ] **Step 6: Measure**

Run `scripts/perf` and record it, answering the four questions with numbers.

- [ ] **Step 7: Write the findings and update the stage table**

- [ ] **Step 8: Commit**

```bash
git add docs/stage8-findings.md docs/perf CLAUDE.md
git commit -m "Record what the first update by hand found"
```
