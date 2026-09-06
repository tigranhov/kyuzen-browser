# Stage 1: Visual MVP Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Arcium shows the complete Zen-classic sidebar bound to live tabs, with the native tab strip and toolbar hidden, the content area inset and rounded on a tinted background, Cmd+T quick entry, and the Stage 0 carry-over fixes, usable as a daily browser.

**Architecture:** Two layers. `arcium/ui/sidebar/` is pure Views code driven by an abstract `SidebarModel` interface, so every component runs in a standalone playground binary against a fake model. `arcium/ui/browser/` adapts Chromium to that interface: `SidebarTabModel` observes the window's `TabStripModel`, `BrowserSidebarController` owns the sidebar inside `BrowserView`, and small hook patches give the sidebar its column, hide the strip and toolbar, and reparent the location bar into the URL pill. Column allocation uses Chromium's own `BrowserLayoutParams` seam: one hook insets the visual client area from the leading edge, and every existing layout rule then works in the remaining space.

**Tech Stack:** Chromium 152.0.7977.83, Views (C++), GN, gtest via `BrowserWithTestWindowTest`, the Views examples framework for the playground.

**Spec:** `docs/superpowers/specs/2026-09-06-stage-1-visual-mvp-design.md`, with `docs/superpowers/specs/2026-09-05-arcium-browser-design.md` section 5 Stage 1 and `docs/stage0-carryover.md`.

## Global Constraints

- All Arcium code under `arcium/`. Upstream files change only through `patches/NNNN-name.patch`; each patch is a hook of a few lines delegating to `arcium/`, with a header stating seam, reason, and target.
- Views for everything in the sidebar; no WebUI; no Swift or AppKit.
- No new processes, no per-tab work beyond one row view and one observer callback, no I/O on the UI thread, no timers in the sidebar.
- Visual constants from the spec: sidebar width 250, row height 32, favicon 16, favourites 4 per row, corner radius 8 for rows, 12 for the content area, content inset 8.
- Chromium C++ style, `git cl format` from the checkout before every commit. Files under ~500 lines.
- Every component hostable in the playground; screenshots into `docs/screens/stage1/`.
- Perf recorded with `scripts/perf --label stage1`, not gated (spec section 3 is provisional).
- Commits end with `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`.
- Shell is zsh; scripts are bash. After changing a patch, run `scripts/sync` to apply it; after changing GN files, `scripts/build dev` regenerates.

## Deviations from the spec, decided while planning

1. **Reuse list narrowed.** `TabIcon` and `ToolbarButton` live in `chrome/browser/ui` and would drag the whole browser into the playground binary. The sidebar draws its own favicon-plus-throbber and uses `views::ImageButton` with vector icons shipped in `arcium/ui/sidebar/icons/`. Chromium pieces reused: `tabs::TabData` (browser side only), the colour provider and mixers, `views::BubbleDialogDelegateView`, `AutocompleteClassifier` for quick-entry input.
2. **Browser test deferred.** Building Chromium's `browser_tests` costs hours; an `arcium_browsertests` executable is set up in Stage 2 together with persistence. Stage 1 verification is `arcium_unittests` for the model plus the hand-run acceptance list and CDP checks.
3. **No Cmd+S sidebar toggle.** Arrives with collapse in Stage 5; the toggle button in the nav row is present but only hides and shows the sidebar within the session.

## Patch inventory for this stage

| Patch | Seam | Purpose |
|---|---|---|
| `0010-gn-arcium-ui.patch` | `chrome/browser/ui/BUILD.gn` | Dependency on `//arcium/ui/browser` with circular includes allowed |
| `0020-api-keys-infobar.patch` | `chrome/browser/ui/startup/infobar_utils.cc` | Suppress the missing-API-keys infobar |
| `0030-signin-not-allowed.patch` | `chrome/browser/signin/account_consistency_mode_manager.cc` | Sign-in disabled, which hides the settings controls |
| `0040-ua-brand.patch` | `components/embedder_support/user_agent_utils.cc` | Client-hints brand "Google Chrome" |
| `0050-browser-view-sidebar.patch` | `chrome/browser/ui/views/frame/browser_view.cc/.h` | Create the sidebar controller; caption hit-test; hide tab strip |
| `0060-layout-params-sidebar.patch` | `chrome/browser/ui/views/frame/layout/browser_view_layout_delegate_impl.cc` | Inset the visual client area by the sidebar width; toolbar hidden |
| `0070-layout-sidebar-bounds.patch` | `chrome/browser/ui/views/frame/layout/browser_view_layout_impl.cc` | Position the sidebar after the proposed layout is applied |
| `0080-color-mixer.patch` | `chrome/browser/ui/color/chrome_color_mixers.cc` | Register the Arcium colour mixer |
| `0090-new-tab-quick-entry.patch` | `chrome/browser/ui/browser_command_controller.cc` | Cmd+T opens the quick entry |

Product strings need no patch: `branding_path_product = "arcium"` plus two symlinked `.grd` files.

## File structure

```
arcium/
  common/
    BUILD.gn
    arcium_features.h / .cc         # base::Feature kArciumSidebar, switch --arcium-no-sidebar
    branding_hooks.h / .cc          # ShouldShowGoogleApiKeysInfoBar(), IsSigninAllowed(), UserAgentBrand()
  branding/strings/
    arcium_strings.grd              # copy of chrome/app/chromium_strings.grd, renamed
    components_arcium_strings.grd   # copy of components/components_chromium_strings.grd, renamed
  ui/sidebar/                       # pure Views, no chrome/browser deps
    BUILD.gn
    icons/ (back.icon forward.icon reload.icon close.icon add.icon sidebar.icon)
    sidebar_metrics.h               # the constants
    sidebar_model.h                 # SidebarModel interface + SidebarRow struct + observer
    sidebar_colors.h / .cc          # colour ids and mixer
    sidebar_view.h / .cc
    nav_row_view.h / .cc
    url_pill_view.h / .cc
    favorites_grid_view.h / .cc
    tab_row_view.h / .cc
    tab_list_view.h / .cc           # one class used for Pinned and Today
    section_divider_view.h / .cc
    space_bar_view.h / .cc
  ui/browser/                       # Chromium adapters
    BUILD.gn
    sidebar_tab_model.h / .cc       # TabStripModelObserver -> SidebarModel
    browser_sidebar_controller.h / .cc  # owns SidebarView in BrowserView, layout hook targets
    quick_entry_bubble.h / .cc
  ui/playground/
    BUILD.gn
    fake_sidebar_model.h / .cc
    sidebar_example.h / .cc
    arcium_playground_main.cc
  test/
    BUILD.gn                        # test("arcium_unittests")
    sidebar_tab_model_unittest.cc
```

---

### Task 1: Feature flag, GN wiring, first patch

**Files:**
- Create: `arcium/common/BUILD.gn`, `arcium/common/arcium_features.h`, `arcium/common/arcium_features.cc`
- Create: `arcium/ui/browser/BUILD.gn` (empty source set for now)
- Create: `patches/0010-gn-arcium-ui.patch`

**Interfaces:**
- Produces: `arcium::features::kArciumSidebar` (`base::Feature`, enabled by default), `arcium::features::IsSidebarEnabled()` which is false when `--arcium-no-sidebar` is on the command line.
- Produces: GN target `//arcium/common` and `//arcium/ui/browser`, the latter depended on by `//chrome/browser/ui:ui`.

- [ ] **Step 1: Write the feature source**

```bash
mkdir -p arcium/common arcium/ui/browser
cat > arcium/common/BUILD.gn <<'EOF'
source_set("common") {
  sources = [
    "arcium_features.cc",
    "arcium_features.h",
  ]
  deps = [ "//base" ]
}
EOF
cat > arcium/common/arcium_features.h <<'EOF'
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_COMMON_ARCIUM_FEATURES_H_
#define ARCIUM_COMMON_ARCIUM_FEATURES_H_

#include "base/feature_list.h"

namespace arcium::features {

// The sidebar-first window layout. When disabled the window is stock Chromium.
BASE_DECLARE_FEATURE(kArciumSidebar);

// Command line switch that turns the sidebar off for one run, for debugging.
inline constexpr char kNoSidebarSwitch[] = "arcium-no-sidebar";

// True when the sidebar layout should be used for normal tabbed windows.
bool IsSidebarEnabled();

}  // namespace arcium::features

#endif  // ARCIUM_COMMON_ARCIUM_FEATURES_H_
EOF
cat > arcium/common/arcium_features.cc <<'EOF'
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/common/arcium_features.h"

#include "base/command_line.h"

namespace arcium::features {

BASE_FEATURE(kArciumSidebar, base::FEATURE_ENABLED_BY_DEFAULT);

bool IsSidebarEnabled() {
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(kNoSidebarSwitch)) {
    return false;
  }
  return base::FeatureList::IsEnabled(kArciumSidebar);
}

}  // namespace arcium::features
EOF
cat > arcium/ui/browser/BUILD.gn <<'EOF'
# Chromium-side adapters for the Arcium sidebar. Depends on //chrome/browser/ui,
# which depends back on this target; chrome/browser/ui/BUILD.gn allows the cycle.
source_set("browser") {
  sources = []
  deps = [
    "//arcium/common",
    "//base",
  ]
}
EOF
```

- [ ] **Step 2: Create the GN hook patch**

Edit the upstream file in the checkout, then capture the diff as the patch. In `chrome/browser/ui/BUILD.gn`, inside `static_library("ui")`, find the line `allow_circular_includes_from = [` (around line 995) and the `deps = [` list of the same target. Add `"//arcium/ui/browser",` to both.

```bash
cd /Volumes/Texternal/chromium/src
python3 - <<'EOF'
p = 'chrome/browser/ui/BUILD.gn'
s = open(p).read()
marker = '  allow_circular_includes_from = [\n'
assert s.count(marker) == 1
s = s.replace(marker, marker + '    # Arcium: sidebar adapters include browser_view.h and are included by it.\n    "//arcium/ui/browser",\n', 1)
# Add the dep just before the allow_circular_includes_from block's target deps: find the
# static_library("ui") deps list start after the target opening.
start = s.index('static_library("ui") {')
deps_at = s.index('  deps = [\n', start)
s = s[:deps_at] + '  deps = [\n    "//arcium/ui/browser",\n' + s[deps_at + len('  deps = [\n'):]
open(p, 'w').write(s)
EOF
git diff --stat
```
Expected: `chrome/browser/ui/BUILD.gn | 3 +++`. If the first `deps = [` after the target opening belongs to a nested `if` block, move the line into the unconditional list; verify with `gn desc out/dev //chrome/browser/ui:ui deps | grep arcium` after Step 3.

```bash
cd /Volumes/Texternal/repositories/arcium
{
  cat <<'EOF'
Seam: chrome/browser/ui/BUILD.gn, static_library("ui").
Why: Arcium's browser-side adapters live in //arcium/ui/browser and include
     browser_view.h; browser_view.cc calls into them. GN needs the cycle allowed.
Delegates to: //arcium/ui/browser.

EOF
  git -C /Volumes/Texternal/chromium/src diff chrome/browser/ui/BUILD.gn
} > patches/0010-gn-arcium-ui.patch
git -C /Volumes/Texternal/chromium/src checkout -- chrome/browser/ui/BUILD.gn
scripts/sync
```
Expected: `applied 0010-gn-arcium-ui.patch`, `patches: applied=1 skipped=0 failed=0`. `git apply` ignores the prose header before the first `diff --git` line; keep it that way in every patch.

- [ ] **Step 3: Build and verify the dependency is wired**

```bash
scripts/build dev 2>&1 | tail -2
cd /Volumes/Texternal/chromium/src && gn desc out/dev //chrome/browser/ui:ui deps | grep -c arcium
```
Expected: build succeeds (a GN change re-generates; compile is small), and the count is `1`.

- [ ] **Step 4: Commit**

```bash
git add arcium/common arcium/ui/browser patches/0010-gn-arcium-ui.patch
git commit -m "Stage 1: feature flag and GN wiring for arcium/ui/browser

First patch of the project: a two-line dependency hook in
chrome/browser/ui/BUILD.gn.

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 2: Product strings say Arcium

**Files:**
- Create: `arcium/branding/strings/arcium_strings.grd`, `arcium/branding/strings/components_arcium_strings.grd`
- Modify: `scripts/lib.sh` (two symlinks), `build/common.gni` (`branding_path_product`)

**Interfaces:**
- Produces: `chrome/app/arcium_strings.grd` and `components/components_arcium_strings.grd` as symlinks; GN selects them through `branding_path_product = "arcium"` (`chrome/app/BUILD.gn:127`, `components/strings/BUILD.gn:57`).

- [ ] **Step 1: Copy and rename the two string tables**

Only message text changes. Identifiers, the `.xtb` translation paths and `chromium.org` URLs stay.

```bash
mkdir -p arcium/branding/strings
python3 - <<'EOF'
import re
SRC = '/Volumes/Texternal/chromium/src'
pairs = [
    (f'{SRC}/chrome/app/chromium_strings.grd', 'arcium/branding/strings/arcium_strings.grd'),
    (f'{SRC}/components/components_chromium_strings.grd', 'arcium/branding/strings/components_arcium_strings.grd'),
]
# Replace the product name in prose only: not in "Chromium Authors", not in URLs or identifiers.
pat = re.compile(r'\bChromium\b(?![_\.]|(?:\s+Authors)|(?:\s*OS\b))')
for src, dst in pairs:
    text = open(src).read()
    out_lines = []
    for line in text.splitlines(keepends=True):
        if '<file path=' in line or 'chromium.org' in line or 'chromium.googlesource' in line:
            out_lines.append(line)
        else:
            out_lines.append(pat.sub('Arcium', line))
    open(dst, 'w').write(''.join(out_lines))
    print(dst, 'Arcium count:', ''.join(out_lines).count('Arcium'))
EOF
grep -c "Chromium" arcium/branding/strings/arcium_strings.grd
```
Expected: a few hundred replacements in the first file; remaining "Chromium" occurrences are `.xtb` paths, URLs and the Authors line.

- [ ] **Step 2: Add the symlinks and the GN arg**

```bash
python3 - <<'EOF'
p = 'scripts/lib.sh'
s = open(p).read()
s = s.replace('''  "components/vector_icons/arcium ../../arcium/branding/vector_icons"
)''', '''  "components/vector_icons/arcium ../../arcium/branding/vector_icons"
  "chrome/app/arcium_strings.grd ../../arcium/branding/strings/arcium_strings.grd"
  "components/components_arcium_strings.grd ../arcium/branding/strings/components_arcium_strings.grd"
)''')
open(p, 'w').write(s)
p = 'build/common.gni'
s = open(p).read()
s = s.replace('branding_path_component = "arcium"\n', 'branding_path_component = "arcium"\n# Product strings: chrome/app/arcium_strings.grd and components/components_arcium_strings.grd (symlinks).\nbranding_path_product = "arcium"\n')
open(p, 'w').write(s)
EOF
scripts/sync
```
Expected: two `linked ...` lines. `ensure_symlinks` checks `-e "$SRC/$link/"`; for file links the trailing slash makes the check fail. Change that line in `scripts/apply-patches` to `[ -e "$SRC/$link" ] || die ...` (without the slash) as part of this step.

- [ ] **Step 3: Build and verify**

```bash
scripts/build dev 2>&1 | tail -2
```
Expected: GN regenerates, grit rebuilds the branded string packs, link. Then launch and check:

```bash
scripts/run chrome://version & sleep 6
P="$HOME/Library/Application Support/Arcium-dev"; C=/private/tmp/claude-501/-Volumes-Texternal-repositories-arcium/a7e02089-c144-4b33-aa52-dffb6d4781a3/scratchpad/cdp.py
```
Use the `cdp.py` helper from Stage 0 with `--remote-debugging-port=0` on `scripts/run` and evaluate `document.body.innerText.includes('Arcium')` on the `chrome://version` tab. Expected `true`, and the first line reads `Arcium 152.0.7977.83`. Quit with `osascript -e 'tell application "Arcium" to quit'`.

If GN fails with a missing `chrome/installer/util` string input on macOS, that target reads `${branding_path_product}_strings.grd` from `chrome/app/` too, which the symlink provides; report the exact error before changing anything else.

- [ ] **Step 4: Commit**

```bash
git add arcium/branding/strings scripts/lib.sh scripts/apply-patches build/common.gni
git commit -m "Product strings: Arcium instead of Chromium via branding_path_product

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 3: Three carry-over hooks: infobar, sign-in, client-hints brand

**Files:**
- Create: `arcium/common/branding_hooks.h`, `arcium/common/branding_hooks.cc`; add to `arcium/common/BUILD.gn`
- Create: `patches/0020-api-keys-infobar.patch`, `patches/0030-signin-not-allowed.patch`, `patches/0040-ua-brand.patch`

**Interfaces:**
- Produces: `arcium::ShouldShowGoogleApiKeysInfoBar()` returns false; `arcium::IsSigninAllowed()` returns false; `arcium::UserAgentBrand()` returns `"Google Chrome"`.

- [ ] **Step 1: Write the hooks**

```bash
cat > arcium/common/branding_hooks.h <<'EOF'
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_COMMON_BRANDING_HOOKS_H_
#define ARCIUM_COMMON_BRANDING_HOOKS_H_

#include <string>

// Decisions that upstream Chromium makes from its own branding, overridden for
// Arcium. Each function is called from exactly one hook patch.
namespace arcium {

// Arcium ships without Google API keys on purpose; the infobar is noise.
bool ShouldShowGoogleApiKeysInfoBar();

// Google account sign-in cannot work without OAuth keys; saying so up front
// hides the sign-in and sync controls in Settings.
bool IsSigninAllowed();

// Brand reported in user-agent client hints. "Google Chrome" makes the Chrome
// Web Store treat Arcium as Chrome (spec D6, decision 2026-09-06).
std::string UserAgentBrand();

}  // namespace arcium

#endif  // ARCIUM_COMMON_BRANDING_HOOKS_H_
EOF
cat > arcium/common/branding_hooks.cc <<'EOF'
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/common/branding_hooks.h"

namespace arcium {

bool ShouldShowGoogleApiKeysInfoBar() {
  return false;
}

bool IsSigninAllowed() {
  return false;
}

std::string UserAgentBrand() {
  return "Google Chrome";
}

}  // namespace arcium
EOF
python3 - <<'EOF'
p = 'arcium/common/BUILD.gn'
s = open(p).read()
s = s.replace('    "arcium_features.h",\n', '    "arcium_features.h",\n    "branding_hooks.cc",\n    "branding_hooks.h",\n')
open(p, 'w').write(s)
EOF
```

- [ ] **Step 2: Make the three upstream edits**

```bash
cd /Volumes/Texternal/chromium/src
python3 - <<'EOF'
def edit(path, old, new, include=None):
    s = open(path).read()
    assert s.count(old) == 1, (path, old)
    s = s.replace(old, new)
    if include and include not in s:
        # Insert after the first #include line.
        i = s.index('#include ')
        j = s.index('\n', i) + 1
        s = s[:j] + include + '\n' + s[j:]
    open(path, 'w').write(s)

edit('chrome/browser/ui/startup/infobar_utils.cc',
     '  if (!google_apis::HasAPIKeyConfigured()) {\n    GoogleApiKeysInfoBarDelegate::Create(infobar_manager);',
     '  if (!google_apis::HasAPIKeyConfigured() &&\n      arcium::ShouldShowGoogleApiKeysInfoBar()) {\n    GoogleApiKeysInfoBarDelegate::Create(infobar_manager);',
     include='#include "arcium/common/branding_hooks.h"')

edit('chrome/browser/signin/account_consistency_mode_manager.cc',
     '  prefs->SetBoolean(prefs::kSigninAllowed, signin_allowed);',
     '  signin_allowed = signin_allowed && arcium::IsSigninAllowed();\n  prefs->SetBoolean(prefs::kSigninAllowed, signin_allowed);',
     include='#include "arcium/common/branding_hooks.h"')

edit('components/embedder_support/user_agent_utils.cc',
     '  std::optional<std::string> brand;\n#if !BUILDFLAG(CHROMIUM_BRANDING)\n  brand = version_info::GetProductName();\n#endif',
     '  std::optional<std::string> brand;\n#if !BUILDFLAG(CHROMIUM_BRANDING)\n  brand = version_info::GetProductName();\n#endif\n  brand = arcium::UserAgentBrand();',
     include='#include "arcium/common/branding_hooks.h"')
EOF
git diff --stat
```
Expected: three files, a few lines each. Two of these files live outside `chrome/browser/ui`, so their GN targets need `//arcium/common` as a dependency: `chrome/browser/signin` is part of `//chrome/browser` (which already reaches `//chrome/browser/ui`, so the include resolves; if GN complains about a missing dependency, add `"//arcium/common"` to the `deps` of the target that owns the file and include that in the same patch). `components/embedder_support:browser_util` needs `"//arcium/common"` added to its `deps` in `components/embedder_support/BUILD.gn`; do that now and include it in patch 0040.

- [ ] **Step 3: Capture the three patches, restore, and re-apply through sync**

```bash
cd /Volumes/Texternal/repositories/arcium
S=/Volumes/Texternal/chromium/src
mk() { { printf 'Seam: %s\nWhy: %s\nDelegates to: arcium/common/branding_hooks.cc\n\n' "$2" "$3"; git -C "$S" diff -- $4; } > "patches/$1"; }
mk 0020-api-keys-infobar.patch "chrome/browser/ui/startup/infobar_utils.cc" "Arcium ships without Google API keys; the infobar is expected noise." "chrome/browser/ui/startup/infobar_utils.cc"
mk 0030-signin-not-allowed.patch "chrome/browser/signin/account_consistency_mode_manager.cc" "Sign-in cannot work without OAuth keys; disabling it hides the controls." "chrome/browser/signin/account_consistency_mode_manager.cc"
mk 0040-ua-brand.patch "components/embedder_support/user_agent_utils.cc" "Client-hints brand Google Chrome so the Web Store treats Arcium as Chrome." "components/embedder_support/user_agent_utils.cc components/embedder_support/BUILD.gn"
git -C "$S" checkout -- chrome/browser/ui/startup/infobar_utils.cc chrome/browser/signin/account_consistency_mode_manager.cc components/embedder_support/user_agent_utils.cc components/embedder_support/BUILD.gn
scripts/sync
```
Expected: `applied` for 0020, 0030, 0040; `patches: applied=3 skipped=1 failed=0`.

- [ ] **Step 4: Build, then verify all three behaviours**

```bash
scripts/build dev 2>&1 | tail -2
```
Then with the browser running under `--remote-debugging-port=0` and `cdp.py`:
- On any `https://` page evaluate `navigator.userAgentData.brands.map(b=>b.brand).join(',')`. Expected to include `Google Chrome`.
- Open `chrome://settings/people`; the "Sign in to Arcium" controls are gone and the sync row says sign-in is disabled.
- Open a new window; no API-keys infobar. Confirm by eye.
- Open the Web Store detail page for uBlock Origin Lite; the "Switch to Chrome?" banner is absent.

- [ ] **Step 5: Commit**

```bash
git add arcium/common patches/0020-api-keys-infobar.patch patches/0030-signin-not-allowed.patch patches/0040-ua-brand.patch
git commit -m "Carry-over hooks: no API-keys infobar, sign-in disabled, Chrome UA brand

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 4: SidebarModel interface, metrics, and the Chromium adapter with unit tests

**Files:**
- Create: `arcium/ui/sidebar/BUILD.gn`, `arcium/ui/sidebar/sidebar_metrics.h`, `arcium/ui/sidebar/sidebar_model.h`
- Create: `arcium/ui/browser/sidebar_tab_model.h`, `arcium/ui/browser/sidebar_tab_model.cc`; update `arcium/ui/browser/BUILD.gn`
- Create: `arcium/test/BUILD.gn`, `arcium/test/sidebar_tab_model_unittest.cc`

**Interfaces:**
- Produces `arcium::SidebarModel`:

```cpp
enum class SidebarSection { kFavorites, kPinned, kToday };
struct SidebarRow {
  int tab_index;               // index in the window's tab model
  SidebarSection section;
  std::u16string title;
  ui::ImageModel favicon;
  bool is_active, is_loading, is_audible, is_muted;
  GURL url;
};
class SidebarModel {
  virtual std::vector<SidebarRow> rows() const;   // ordered as displayed
  virtual void ActivateTab(int tab_index);
  virtual void CloseTab(int tab_index);
  virtual void MoveTab(int from_index, int to_index);
  virtual void NewTab();
  virtual void ClearToday();                      // close every Today tab
  virtual void AddObserver(Observer*); RemoveObserver(Observer*);
  // Observer::OnSidebarModelChanged() fires after any change, coalesced per model event.
};
```
- Produces `arcium::SidebarTabModel(TabStripModel*)` implementing it.

- [ ] **Step 1: Write the interface and metrics**

```bash
mkdir -p arcium/ui/sidebar arcium/test
cat > arcium/ui/sidebar/sidebar_metrics.h <<'EOF'
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_SIDEBAR_METRICS_H_
#define ARCIUM_UI_SIDEBAR_SIDEBAR_METRICS_H_

// Layout constants from the Stage 1 spec ("comfortable" density).
namespace arcium::metrics {

inline constexpr int kSidebarWidth = 250;
inline constexpr int kSidebarPadding = 8;
inline constexpr int kRowHeight = 32;
inline constexpr int kRowCornerRadius = 8;
inline constexpr int kRowHorizontalPadding = 8;
inline constexpr int kRowIconTextGap = 9;
inline constexpr int kFaviconSize = 16;
inline constexpr int kFavoritesPerRow = 4;
inline constexpr int kFavoriteTileGap = 6;
inline constexpr int kNavButtonSize = 26;
inline constexpr int kUrlPillHeight = 28;
inline constexpr int kSpaceChipHeight = 28;
inline constexpr int kProfileBadgeSize = 20;
inline constexpr int kContentInset = 8;
inline constexpr int kContentCornerRadius = 12;
// Width reserved at the top-left for the macOS traffic lights, plus padding.
// The browser overrides this from the frame's real exclusion area at runtime.
inline constexpr int kDefaultCaptionButtonWidth = 70;

}  // namespace arcium::metrics

#endif  // ARCIUM_UI_SIDEBAR_SIDEBAR_METRICS_H_
EOF
cat > arcium/ui/sidebar/sidebar_model.h <<'EOF'
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_SIDEBAR_MODEL_H_
#define ARCIUM_UI_SIDEBAR_SIDEBAR_MODEL_H_

#include <string>
#include <vector>

#include "base/observer_list_types.h"
#include "ui/base/models/image_model.h"
#include "url/gurl.h"

namespace arcium {

enum class SidebarSection { kFavorites, kPinned, kToday };

// Everything a row needs to paint itself. Derived by the model, never by views.
struct SidebarRow {
  int tab_index = -1;
  SidebarSection section = SidebarSection::kToday;
  std::u16string title;
  ui::ImageModel favicon;
  bool is_active = false;
  bool is_loading = false;
  bool is_audible = false;
  bool is_muted = false;
  GURL url;
};

// The sidebar's view of a window's tabs plus the commands it can issue. The
// browser implements it on top of TabStripModel; the playground uses a fake.
class SidebarModel {
 public:
  class Observer : public base::CheckedObserver {
   public:
    // Fired after any change. Views re-read rows() and rebuild; the model
    // coalesces bursts so one tab-strip event yields one notification.
    virtual void OnSidebarModelChanged() = 0;
  };

  virtual ~SidebarModel() = default;

  virtual std::vector<SidebarRow> rows() const = 0;

  virtual void ActivateTab(int tab_index) = 0;
  virtual void CloseTab(int tab_index) = 0;
  virtual void MoveTab(int from_index, int to_index) = 0;
  virtual void NewTab() = 0;
  virtual void ClearToday() = 0;

  virtual void AddObserver(Observer* observer) = 0;
  virtual void RemoveObserver(Observer* observer) = 0;
};

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_SIDEBAR_MODEL_H_
EOF
cat > arcium/ui/sidebar/BUILD.gn <<'EOF'
import("//components/vector_icons/vector_icons.gni")

# Pure Views. Must not depend on //chrome/browser so the playground stays small.
source_set("sidebar") {
  sources = [
    "sidebar_metrics.h",
    "sidebar_model.h",
  ]
  public_deps = [
    "//base",
    "//ui/base",
    "//url",
  ]
  deps = [
    "//ui/views",
  ]
}
EOF
```

- [ ] **Step 2: Write the failing unit test**

```bash
cat > arcium/test/BUILD.gn <<'EOF'
import("//testing/test.gni")

test("arcium_unittests") {
  sources = [ "sidebar_tab_model_unittest.cc" ]
  deps = [
    "//arcium/ui/browser",
    "//arcium/ui/sidebar",
    "//base/test:test_support",
    "//chrome/browser/ui",
    "//chrome/test:test_support",
    "//chrome/test:test_support_unit",
    "//content/test:test_support",
    "//testing/gtest",
  ]
}
EOF
cat > arcium/test/sidebar_tab_model_unittest.cc <<'EOF'
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/sidebar_tab_model.h"

#include <vector>

#include "arcium/ui/sidebar/sidebar_model.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace arcium {
namespace {

class CountingObserver : public SidebarModel::Observer {
 public:
  void OnSidebarModelChanged() override { ++count; }
  int count = 0;
};

class SidebarTabModelTest : public BrowserWithTestWindowTest {
 protected:
  TabStripModel* strip() { return browser()->tab_strip_model(); }
};

TEST_F(SidebarTabModelTest, RowsFollowTabOrderAndSections) {
  AddTab(browser(), GURL("https://a.example/"));
  AddTab(browser(), GURL("https://b.example/"));
  AddTab(browser(), GURL("https://c.example/"));
  // AddTab inserts at index 0 and activates, so order is c, b, a.
  strip()->SetTabPinned(0, true);

  SidebarTabModel model(strip());
  std::vector<SidebarRow> rows = model.rows();
  ASSERT_EQ(3u, rows.size());
  EXPECT_EQ(SidebarSection::kPinned, rows[0].section);
  EXPECT_EQ(GURL("https://c.example/"), rows[0].url);
  EXPECT_EQ(SidebarSection::kToday, rows[1].section);
  EXPECT_EQ(GURL("https://b.example/"), rows[1].url);
  EXPECT_EQ(SidebarSection::kToday, rows[2].section);
  EXPECT_TRUE(rows[0].is_active);
  EXPECT_FALSE(rows[1].is_active);
}

TEST_F(SidebarTabModelTest, CommandsDriveTheStrip) {
  AddTab(browser(), GURL("https://a.example/"));
  AddTab(browser(), GURL("https://b.example/"));
  SidebarTabModel model(strip());

  model.ActivateTab(1);
  EXPECT_EQ(1, strip()->active_index());

  model.MoveTab(1, 0);
  EXPECT_EQ(GURL("https://a.example/"), strip()->GetWebContentsAt(0)->GetURL());

  model.CloseTab(0);
  EXPECT_EQ(1, strip()->count());
}

TEST_F(SidebarTabModelTest, ClearTodayKeepsPinnedTabs) {
  AddTab(browser(), GURL("https://a.example/"));
  AddTab(browser(), GURL("https://b.example/"));
  AddTab(browser(), GURL("https://c.example/"));
  strip()->SetTabPinned(0, true);
  SidebarTabModel model(strip());

  model.ClearToday();
  ASSERT_EQ(1, strip()->count());
  EXPECT_TRUE(strip()->IsTabPinned(0));
}

TEST_F(SidebarTabModelTest, ObserverFiresOncePerStripEvent) {
  AddTab(browser(), GURL("https://a.example/"));
  SidebarTabModel model(strip());
  CountingObserver observer;
  model.AddObserver(&observer);

  AddTab(browser(), GURL("https://b.example/"));
  EXPECT_EQ(1, observer.count);

  strip()->SetTabPinned(0, true);
  EXPECT_EQ(2, observer.count);

  model.RemoveObserver(&observer);
  AddTab(browser(), GURL("https://c.example/"));
  EXPECT_EQ(2, observer.count);
}

}  // namespace
}  // namespace arcium
EOF
```

- [ ] **Step 3: Run the test to verify it fails to build**

```bash
scripts/build dev arcium_unittests 2>&1 | tr '\r' '\n' | grep -E "error:|finished" | head -3
```
Expected: an error that `arcium/ui/browser/sidebar_tab_model.h` does not exist.

- [ ] **Step 4: Implement SidebarTabModel**

```bash
cat > arcium/ui/browser/sidebar_tab_model.h <<'EOF'
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_BROWSER_SIDEBAR_TAB_MODEL_H_
#define ARCIUM_UI_BROWSER_SIDEBAR_TAB_MODEL_H_

#include <vector>

#include "arcium/ui/sidebar/sidebar_model.h"
#include "base/memory/raw_ptr.h"
#include "base/observer_list.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"

class TabStripModel;

namespace arcium {

// Adapts a window's TabStripModel to the SidebarModel interface. Rows are
// derived on demand from tabs::TabData, so nothing is cached per tab.
class SidebarTabModel : public SidebarModel, public TabStripModelObserver {
 public:
  explicit SidebarTabModel(TabStripModel* tab_strip_model);
  SidebarTabModel(const SidebarTabModel&) = delete;
  SidebarTabModel& operator=(const SidebarTabModel&) = delete;
  ~SidebarTabModel() override;

  // SidebarModel:
  std::vector<SidebarRow> rows() const override;
  void ActivateTab(int tab_index) override;
  void CloseTab(int tab_index) override;
  void MoveTab(int from_index, int to_index) override;
  void NewTab() override;
  void ClearToday() override;
  void AddObserver(Observer* observer) override;
  void RemoveObserver(Observer* observer) override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void TabChangedAt(content::WebContents* contents,
                    int index,
                    TabChangeType change_type) override;
  void TabPinnedStateChanged(TabStripModel* tab_strip_model,
                             content::WebContents* contents,
                             int index) override;
  void OnTabStripModelDestroyed(TabStripModel* tab_strip_model) override;

 private:
  void NotifyChanged();

  raw_ptr<TabStripModel> tab_strip_model_;
  base::ObserverList<Observer> observers_;
};

}  // namespace arcium

#endif  // ARCIUM_UI_BROWSER_SIDEBAR_TAB_MODEL_H_
EOF
cat > arcium/ui/browser/sidebar_tab_model.cc <<'EOF'
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/sidebar_tab_model.h"

#include "chrome/browser/ui/tabs/tab_data.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"

namespace arcium {

SidebarTabModel::SidebarTabModel(TabStripModel* tab_strip_model)
    : tab_strip_model_(tab_strip_model) {
  tab_strip_model_->AddObserver(this);
}

SidebarTabModel::~SidebarTabModel() {
  if (tab_strip_model_) {
    tab_strip_model_->RemoveObserver(this);
  }
}

std::vector<SidebarRow> SidebarTabModel::rows() const {
  std::vector<SidebarRow> rows;
  if (!tab_strip_model_) {
    return rows;
  }
  const int count = tab_strip_model_->count();
  rows.reserve(count);
  for (int i = 0; i < count; ++i) {
    tabs::TabInterface* tab = tab_strip_model_->GetTabAtIndex(i);
    const tabs::TabData data = tabs::TabData::FromTabInterface(tab);
    SidebarRow row;
    row.tab_index = i;
    row.section = tab_strip_model_->IsTabPinned(i) ? SidebarSection::kPinned
                                                   : SidebarSection::kToday;
    row.title = data.title;
    row.favicon = data.favicon;
    row.is_active = i == tab_strip_model_->active_index();
    row.is_loading = data.network_state != tabs::TabNetworkState::kNone &&
                     !data.should_hide_throbber;
    row.is_audible = data.alert_state == tabs::TabAlert::kAudioPlaying;
    row.is_muted = data.alert_state == tabs::TabAlert::kAudioMuting;
    row.url = data.visible_url;
    rows.push_back(std::move(row));
  }
  return rows;
}

void SidebarTabModel::ActivateTab(int tab_index) {
  if (tab_index >= 0 && tab_index < tab_strip_model_->count()) {
    tab_strip_model_->ActivateTabAt(tab_index);
  }
}

void SidebarTabModel::CloseTab(int tab_index) {
  if (tab_index >= 0 && tab_index < tab_strip_model_->count()) {
    tab_strip_model_->CloseWebContentsAt(
        tab_index, TabCloseTypes::CLOSE_USER_GESTURE |
                       TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
  }
}

void SidebarTabModel::MoveTab(int from_index, int to_index) {
  const int count = tab_strip_model_->count();
  if (from_index < 0 || from_index >= count || to_index < 0 ||
      to_index >= count || from_index == to_index) {
    return;
  }
  tab_strip_model_->MoveWebContentsAt(from_index, to_index,
                                      /*select_after_move=*/false);
}

void SidebarTabModel::NewTab() {
  tab_strip_model_->delegate()->AddTabAt(GURL(), -1, /*foreground=*/true);
}

void SidebarTabModel::ClearToday() {
  // Close from the end so indices stay valid; pinned tabs are always first.
  for (int i = tab_strip_model_->count() - 1;
       i >= tab_strip_model_->IndexOfFirstNonPinnedTab(); --i) {
    tab_strip_model_->CloseWebContentsAt(
        i, TabCloseTypes::CLOSE_USER_GESTURE |
               TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
  }
}

void SidebarTabModel::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void SidebarTabModel::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void SidebarTabModel::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  NotifyChanged();
}

void SidebarTabModel::TabChangedAt(content::WebContents* contents,
                                   int index,
                                   TabChangeType change_type) {
  NotifyChanged();
}

void SidebarTabModel::TabPinnedStateChanged(TabStripModel* tab_strip_model,
                                            content::WebContents* contents,
                                            int index) {
  NotifyChanged();
}

void SidebarTabModel::OnTabStripModelDestroyed(TabStripModel* tab_strip_model) {
  tab_strip_model_->RemoveObserver(this);
  tab_strip_model_ = nullptr;
}

void SidebarTabModel::NotifyChanged() {
  for (Observer& observer : observers_) {
    observer.OnSidebarModelChanged();
  }
}

}  // namespace arcium
EOF
python3 - <<'EOF'
p = 'arcium/ui/browser/BUILD.gn'
s = open(p).read()
s = s.replace('  sources = []\n  deps = [\n    "//arcium/common",\n    "//base",\n  ]',
              '  sources = [\n    "sidebar_tab_model.cc",\n    "sidebar_tab_model.h",\n  ]\n  deps = [\n    "//arcium/common",\n    "//arcium/ui/sidebar",\n    "//base",\n    "//chrome/browser/ui",\n    "//components/tabs:public",\n    "//content/public/browser",\n  ]')
open(p, 'w').write(s)
EOF
```

Names to confirm against the tree while implementing (they moved recently and may move again): `TabStripModel::GetTabAtIndex`, `tabs::TabNetworkState`, `tabs::TabAlert::kAudioPlaying` and `kAudioMuting` (in `chrome/browser/ui/tabs/alert/tab_alert.h`), `TabStripModelDelegate::AddTabAt`, and the `TabCloseTypes` constants. Use `git grep -n` in the checkout for each and adjust the include or name. If `TabChangedAt`'s signature differs, copy it from `tab_strip_model_observer.h`.

- [ ] **Step 5: Build and run the tests**

```bash
scripts/build dev arcium_unittests 2>&1 | tr '\r' '\n' | grep -E "error:|finished" | head -5
/Volumes/Texternal/chromium/src/out/dev/arcium_unittests --gtest_filter='SidebarTabModelTest.*'
```
Expected: `[  PASSED  ] 4 tests.` The first build of `chrome/test:test_support` takes 20 to 60 minutes; subsequent runs are seconds.

- [ ] **Step 6: Commit**

```bash
cd /Volumes/Texternal/chromium/src && git cl format --full arcium/ >/dev/null 2>&1; cd -
git add arcium/ui/sidebar arcium/ui/browser arcium/test
git commit -m "SidebarModel interface and TabStripModel adapter with unit tests

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 5: Playground binary with a fake model and an empty SidebarView

**Files:**
- Create: `arcium/ui/sidebar/sidebar_view.h`, `arcium/ui/sidebar/sidebar_view.cc`
- Create: `arcium/ui/playground/BUILD.gn`, `fake_sidebar_model.h`, `fake_sidebar_model.cc`, `sidebar_example.h`, `sidebar_example.cc`, `arcium_playground_main.cc`
- Modify: `scripts/playground` (launch `arcium_playground` instead of `views_examples`)

**Interfaces:**
- Produces `arcium::SidebarView(SidebarModel*)`: a `views::View` column, 250 wide, that observes the model and rebuilds its sections. Later tasks add children; this task establishes the container, its background slot and `SetCaptionButtonWidth(int)`.
- Produces `arcium::FakeSidebarModel` with `AddTab(title, url, section, active)` for the playground and later unit tests of views.
- Produces the `arcium_playground` target: `views::examples::ExamplesMainProc` with a single `SidebarExample`.

- [ ] **Step 1: Write SidebarView skeleton**

```bash
cat > arcium/ui/sidebar/sidebar_view.h <<'EOF'
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_SIDEBAR_VIEW_H_
#define ARCIUM_UI_SIDEBAR_SIDEBAR_VIEW_H_

#include "arcium/ui/sidebar/sidebar_model.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

namespace arcium {

class NavRowView;
class UrlPillView;
class FavoritesGridView;
class TabListView;
class SectionDividerView;
class SpaceBarView;

// The sidebar column: nav rows, favourites, pinned, divider, today, space bar.
// Pure Views; everything it shows comes from SidebarModel.
class SidebarView : public views::View, public SidebarModel::Observer {
  METADATA_HEADER(SidebarView, views::View)

 public:
  explicit SidebarView(SidebarModel* model);
  SidebarView(const SidebarView&) = delete;
  SidebarView& operator=(const SidebarView&) = delete;
  ~SidebarView() override;

  // Width of the frame-owned controls (macOS traffic lights) that the first
  // row must leave empty on the leading side.
  void SetCaptionButtonWidth(int width);

  // The slot the browser puts the real location bar into (Task 11). Null in
  // the playground, where UrlPillView paints a placeholder.
  UrlPillView* url_pill() { return url_pill_; }
  NavRowView* nav_row() { return nav_row_; }

  // True for points in the sidebar that should drag the window: the nav row
  // background and any empty space, but not buttons or tab rows.
  bool IsPositionInWindowCaption(const gfx::Point& point) const;

  // SidebarModel::Observer:
  void OnSidebarModelChanged() override;

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void OnThemeChanged() override;

 private:
  void Rebuild();

  raw_ptr<SidebarModel> model_;
  base::ScopedObservation<SidebarModel, SidebarModel::Observer> observation_{
      this};

  raw_ptr<NavRowView> nav_row_ = nullptr;
  raw_ptr<UrlPillView> url_pill_ = nullptr;
  raw_ptr<FavoritesGridView> favorites_ = nullptr;
  raw_ptr<TabListView> pinned_ = nullptr;
  raw_ptr<SectionDividerView> divider_ = nullptr;
  raw_ptr<TabListView> today_ = nullptr;
  raw_ptr<SpaceBarView> space_bar_ = nullptr;
};

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_SIDEBAR_VIEW_H_
EOF
```

The `.cc` for this task builds the column with placeholders for sections that later tasks replace. Write it so it compiles now and each later task swaps one placeholder for the real child:

```bash
cat > arcium/ui/sidebar/sidebar_view.cc <<'EOF'
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/sidebar_view.h"

#include <memory>

#include "arcium/ui/sidebar/sidebar_metrics.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/background.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/layout/flex_layout_types.h"
#include "ui/views/layout/layout_types.h"
#include "ui/views/view_class_properties.h"

namespace arcium {

SidebarView::SidebarView(SidebarModel* model) : model_(model) {
  auto* layout = SetLayoutManager(std::make_unique<views::FlexLayout>());
  layout->SetOrientation(views::LayoutOrientation::kVertical)
      .SetCrossAxisAlignment(views::LayoutAlignment::kStretch)
      .SetInteriorMargin(gfx::Insets::TLBR(metrics::kSidebarPadding,
                                           metrics::kSidebarPadding,
                                           metrics::kSidebarPadding,
                                           metrics::kSidebarPadding))
      .SetDefault(views::kMarginsKey, gfx::Insets::VH(3, 0));
  // Sections are attached by Rebuild(); tasks 6 to 9 fill them in.
  observation_.Observe(model_);
  Rebuild();
}

SidebarView::~SidebarView() = default;

void SidebarView::SetCaptionButtonWidth(int width) {
  // Consumed by NavRowView in Task 7.
  caption_button_width_ = width;
  if (nav_row_) {
    nav_row_->SetLeadingInset(width);
  }
}

bool SidebarView::IsPositionInWindowCaption(const gfx::Point& point) const {
  // Task 7 refines this once NavRowView exists: only the nav row background
  // and empty space are draggable. Until then, the top strip is.
  return point.y() < metrics::kSidebarPadding + metrics::kNavButtonSize;
}

void SidebarView::OnSidebarModelChanged() {
  Rebuild();
}

gfx::Size SidebarView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return gfx::Size(metrics::kSidebarWidth,
                   available_size.height().value_or(0));
}

void SidebarView::OnThemeChanged() {
  views::View::OnThemeChanged();
  // Task 8 installs the tinted background here.
}

void SidebarView::Rebuild() {
  // Rebuilt wholesale on every change: cheap at tens of rows, and the model
  // coalesces bursts. Task 6 makes TabListView diff rows instead.
  if (today_) {
    today_->SetRows(model_->rows());
  }
  if (pinned_) {
    pinned_->SetRows(model_->rows());
  }
  if (favorites_) {
    favorites_->SetRows(model_->rows());
  }
}

BEGIN_METADATA(SidebarView)
END_METADATA

}  // namespace arcium
EOF
```

Add `int caption_button_width_ = 0;` to the private members in the header, and add `"sidebar_view.cc"`, `"sidebar_view.h"` to the `sources` list in `arcium/ui/sidebar/BUILD.gn`. Until Tasks 6 and 7 exist, the forward-declared classes are unused pointers, so the file compiles; the `Rebuild` calls on them are guarded by null checks and the `SetLeadingInset` call must be added in Task 7 together with `NavRowView` (leave that line out until then, or the build fails on an incomplete type).

- [ ] **Step 2: Write the fake model**

```bash
mkdir -p arcium/ui/playground
cat > arcium/ui/playground/fake_sidebar_model.h <<'EOF'
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_PLAYGROUND_FAKE_SIDEBAR_MODEL_H_
#define ARCIUM_UI_PLAYGROUND_FAKE_SIDEBAR_MODEL_H_

#include <string>
#include <vector>

#include "arcium/ui/sidebar/sidebar_model.h"
#include "base/observer_list.h"

namespace arcium {

// In-memory SidebarModel for the playground and view tests. Commands mutate
// the vector the way TabStripModel would and notify observers.
class FakeSidebarModel : public SidebarModel {
 public:
  FakeSidebarModel();
  ~FakeSidebarModel() override;

  void AddTab(const std::u16string& title,
              const std::string& url,
              SidebarSection section,
              bool active);
  void SetLoading(int tab_index, bool loading);
  void SetAudible(int tab_index, bool audible);

  // SidebarModel:
  std::vector<SidebarRow> rows() const override;
  void ActivateTab(int tab_index) override;
  void CloseTab(int tab_index) override;
  void MoveTab(int from_index, int to_index) override;
  void NewTab() override;
  void ClearToday() override;
  void AddObserver(Observer* observer) override;
  void RemoveObserver(Observer* observer) override;

 private:
  void Notify();
  void Reindex();

  std::vector<SidebarRow> rows_;
  base::ObserverList<Observer> observers_;
};

}  // namespace arcium

#endif  // ARCIUM_UI_PLAYGROUND_FAKE_SIDEBAR_MODEL_H_
EOF
cat > arcium/ui/playground/fake_sidebar_model.cc <<'EOF'
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/playground/fake_sidebar_model.h"

#include <algorithm>
#include <utility>

#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/image/canvas_image_source.h"
#include "ui/gfx/image/image_skia.h"

namespace arcium {

namespace {

// A coloured rounded square stands in for a favicon.
class SwatchSource : public gfx::CanvasImageSource {
 public:
  explicit SwatchSource(SkColor color)
      : gfx::CanvasImageSource(gfx::Size(16, 16)), color_(color) {}
  void Draw(gfx::Canvas* canvas) override {
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(color_);
    canvas->DrawRoundRect(gfx::RectF(0, 0, 16, 16), 4, flags);
  }

 private:
  SkColor color_;
};

ui::ImageModel SwatchFor(const std::string& url) {
  static constexpr SkColor kPalette[] = {
      SkColorSetRGB(0xE3, 0x4C, 0x4C), SkColorSetRGB(0x4C, 0x8B, 0xE3),
      SkColorSetRGB(0x3C, 0xB3, 0x71), SkColorSetRGB(0xF0, 0xA0, 0x30),
      SkColorSetRGB(0x58, 0x65, 0xF2), SkColorSetRGB(0x24, 0x29, 0x2E)};
  size_t hash = 0;
  for (char c : url) {
    hash = hash * 31 + static_cast<unsigned char>(c);
  }
  return ui::ImageModel::FromImageSkia(
      gfx::CanvasImageSource::MakeImageSkia<SwatchSource>(
          kPalette[hash % std::size(kPalette)]));
}

}  // namespace

FakeSidebarModel::FakeSidebarModel() = default;
FakeSidebarModel::~FakeSidebarModel() = default;

void FakeSidebarModel::AddTab(const std::u16string& title,
                              const std::string& url,
                              SidebarSection section,
                              bool active) {
  SidebarRow row;
  row.title = title;
  row.url = GURL(url);
  row.section = section;
  row.favicon = SwatchFor(url);
  if (active) {
    for (SidebarRow& r : rows_) {
      r.is_active = false;
    }
  }
  row.is_active = active;
  // Keep favourites first, then pinned, then today, like the real strip.
  auto pos = std::find_if(rows_.begin(), rows_.end(), [&](const SidebarRow& r) {
    return static_cast<int>(r.section) > static_cast<int>(section);
  });
  rows_.insert(pos, std::move(row));
  Reindex();
  Notify();
}

void FakeSidebarModel::SetLoading(int tab_index, bool loading) {
  rows_[tab_index].is_loading = loading;
  Notify();
}

void FakeSidebarModel::SetAudible(int tab_index, bool audible) {
  rows_[tab_index].is_audible = audible;
  Notify();
}

std::vector<SidebarRow> FakeSidebarModel::rows() const {
  return rows_;
}

void FakeSidebarModel::ActivateTab(int tab_index) {
  for (SidebarRow& r : rows_) {
    r.is_active = r.tab_index == tab_index;
  }
  Notify();
}

void FakeSidebarModel::CloseTab(int tab_index) {
  if (tab_index < 0 || tab_index >= static_cast<int>(rows_.size())) {
    return;
  }
  const bool was_active = rows_[tab_index].is_active;
  rows_.erase(rows_.begin() + tab_index);
  Reindex();
  if (was_active && !rows_.empty()) {
    rows_[std::min<size_t>(tab_index, rows_.size() - 1)].is_active = true;
  }
  Notify();
}

void FakeSidebarModel::MoveTab(int from_index, int to_index) {
  if (from_index < 0 || to_index < 0 ||
      from_index >= static_cast<int>(rows_.size()) ||
      to_index >= static_cast<int>(rows_.size())) {
    return;
  }
  SidebarRow row = std::move(rows_[from_index]);
  rows_.erase(rows_.begin() + from_index);
  rows_.insert(rows_.begin() + to_index, std::move(row));
  Reindex();
  Notify();
}

void FakeSidebarModel::NewTab() {
  AddTab(u"New Tab", "about:blank", SidebarSection::kToday, /*active=*/true);
}

void FakeSidebarModel::ClearToday() {
  std::erase_if(rows_, [](const SidebarRow& r) {
    return r.section == SidebarSection::kToday;
  });
  Reindex();
  Notify();
}

void FakeSidebarModel::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void FakeSidebarModel::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void FakeSidebarModel::Notify() {
  for (Observer& o : observers_) {
    o.OnSidebarModelChanged();
  }
}

void FakeSidebarModel::Reindex() {
  for (size_t i = 0; i < rows_.size(); ++i) {
    rows_[i].tab_index = static_cast<int>(i);
  }
}

}  // namespace arcium
EOF
```

- [ ] **Step 3: Write the example and the main**

```bash
cat > arcium/ui/playground/sidebar_example.h <<'EOF'
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_PLAYGROUND_SIDEBAR_EXAMPLE_H_
#define ARCIUM_UI_PLAYGROUND_SIDEBAR_EXAMPLE_H_

#include <memory>

#include "arcium/ui/playground/fake_sidebar_model.h"
#include "ui/views/examples/example_base.h"

namespace arcium {

// Hosts SidebarView next to a grey "page" rectangle, on a fake model seeded
// with a realistic set of tabs.
class SidebarExample : public views::examples::ExampleBase {
 public:
  SidebarExample();
  ~SidebarExample() override;

  // ExampleBase:
  void CreateExampleView(views::View* container) override;

 private:
  std::unique_ptr<FakeSidebarModel> model_;
};

}  // namespace arcium

#endif  // ARCIUM_UI_PLAYGROUND_SIDEBAR_EXAMPLE_H_
EOF
cat > arcium/ui/playground/sidebar_example.cc <<'EOF'
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/playground/sidebar_example.h"

#include "arcium/ui/sidebar/sidebar_metrics.h"
#include "arcium/ui/sidebar/sidebar_view.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/views/background.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/layout/flex_layout_types.h"
#include "ui/views/view.h"
#include "ui/views/view_class_properties.h"

namespace arcium {

SidebarExample::SidebarExample()
    : ExampleBase("Arcium Sidebar"),
      model_(std::make_unique<FakeSidebarModel>()) {
  model_->AddTab(u"GitHub", "https://github.com/", SidebarSection::kFavorites,
                 false);
  model_->AddTab(u"Gmail", "https://mail.google.com/",
                 SidebarSection::kFavorites, false);
  model_->AddTab(u"Calendar", "https://calendar.google.com/",
                 SidebarSection::kFavorites, false);
  model_->AddTab(u"Linear", "https://linear.app/", SidebarSection::kFavorites,
                 false);
  model_->AddTab(u"Discord", "https://discord.com/app",
                 SidebarSection::kPinned, false);
  model_->AddTab(u"Linear · Arcium board", "https://linear.app/arcium",
                 SidebarSection::kPinned, false);
  model_->AddTab(u"tigranhov/arcium", "https://github.com/tigranhov/arcium",
                 SidebarSection::kToday, true);
  model_->AddTab(u"Chromium Views tutorial",
                 "https://www.youtube.com/watch?v=views",
                 SidebarSection::kToday, false);
  model_->AddTab(u"StoragePartitionConfig - Chromium Code Search",
                 "https://source.chromium.org/", SidebarSection::kToday, false);
  model_->AddTab(u"Hacker News", "https://news.ycombinator.com/",
                 SidebarSection::kToday, false);
  model_->SetLoading(8, true);
  model_->SetAudible(7, true);
}

SidebarExample::~SidebarExample() = default;

void SidebarExample::CreateExampleView(views::View* container) {
  auto* layout = container->SetLayoutManager(std::make_unique<views::FlexLayout>());
  layout->SetOrientation(views::LayoutOrientation::kHorizontal)
      .SetCrossAxisAlignment(views::LayoutAlignment::kStretch);

  auto* sidebar = container->AddChildView(std::make_unique<SidebarView>(model_.get()));
  sidebar->SetCaptionButtonWidth(metrics::kDefaultCaptionButtonWidth);

  auto* page = container->AddChildView(std::make_unique<views::View>());
  page->SetBackground(views::CreateRoundedRectBackground(
      SkColorSetRGB(0xFF, 0xFF, 0xFF), metrics::kContentCornerRadius));
  page->SetProperty(views::kMarginsKey,
                    gfx::Insets::TLBR(metrics::kContentInset, 0,
                                      metrics::kContentInset,
                                      metrics::kContentInset));
  page->SetProperty(
      views::kFlexBehaviorKey,
      views::FlexSpecification(views::MinimumFlexSizeRule::kScaleToZero,
                               views::MaximumFlexSizeRule::kUnbounded));
}

}  // namespace arcium
EOF
cat > arcium/ui/playground/arcium_playground_main.cc <<'EOF'
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "arcium/ui/playground/sidebar_example.h"
#include "base/at_exit.h"
#include "base/command_line.h"
#include "ui/views/examples/example_base.h"
#include "ui/views/examples/examples_main_proc.h"

#if BUILDFLAG(IS_MAC)
#include "ui/views/examples/examples_main_mac_support.h"
#endif

int main(int argc, char** argv) {
  base::CommandLine::Init(argc, argv);
  base::AtExitManager at_exit;
#if BUILDFLAG(IS_MAC)
  views::examples::InitializeMacSupport();
#endif
  views::examples::ExampleVector examples;
  examples.push_back(std::make_unique<arcium::SidebarExample>());
  return static_cast<int>(views::examples::ExamplesMainProc(
      /*under_test=*/false, std::move(examples)));
}
EOF
cat > arcium/ui/playground/BUILD.gn <<'EOF'
# Standalone Views host for Arcium's sidebar components. No //chrome deps.
executable("arcium_playground") {
  testonly = true
  sources = [
    "arcium_playground_main.cc",
    "fake_sidebar_model.cc",
    "fake_sidebar_model.h",
    "sidebar_example.cc",
    "sidebar_example.h",
  ]
  deps = [
    "//arcium/ui/sidebar",
    "//base",
    "//skia",
    "//ui/base",
    "//ui/gfx",
    "//ui/views",
    "//ui/views/examples:views_examples_lib",
    "//ui/views/examples:views_examples_proc",
  ]
  if (is_mac) {
    deps += [ "//ui/views/examples:views_examples_main_mac_support" ]
  }
}
EOF
```

Check `ui/views/examples/examples_main.cc` and `examples_main_mac_support.h` for the exact mac initialisation call and copy it; the name above is a stand-in for whatever that file exports. `ExamplesMainProc` may also want `base::i18n::InitializeICU()` or resource setup done inside it already; mirror `examples_main.cc` line for line except for the examples vector.

- [ ] **Step 4: Point the playground script at the new binary and build it**

```bash
python3 - <<'EOF'
p = 'scripts/playground'
s = open(p).read()
s = s.replace('"$ARCIUM_ROOT/scripts/build" "$config" views_examples', '"$ARCIUM_ROOT/scripts/build" "$config" arcium_playground')
s = s.replace('bin="$SRC/out/$config/Views Examples.app/Contents/MacOS/Views Examples"', 'bin="$SRC/out/$config/arcium_playground"')
s = s.replace('views_examples did not produce', 'arcium_playground did not produce')
open(p, 'w').write(s)
EOF
scripts/playground
```
Expected: a window opens with the example list showing "Arcium Sidebar", a 250px dark-grey column on the left (no sections yet) and a white rounded page area. If the executable is produced as an app bundle like `views_examples`, adjust the path in the script the same way. Take a screenshot by hand with Cmd+Shift+4 into `docs/screens/stage1/05-empty-sidebar.png` (the agent cannot capture the screen; the user does this once per task, or skip and rely on the description).

- [ ] **Step 5: Commit**

```bash
git add arcium/ui/sidebar arcium/ui/playground scripts/playground docs/screens
git commit -m "Playground: arcium_playground with FakeSidebarModel and empty SidebarView

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 6: TabRowView and TabListView (Today and Pinned)

**Files:**
- Create: `arcium/ui/sidebar/tab_row_view.h/.cc`, `arcium/ui/sidebar/tab_list_view.h/.cc`
- Create: `arcium/ui/sidebar/icons/close.icon`, `arcium/ui/sidebar/icons/add.icon`, `arcium/ui/sidebar/icons/audio.icon`, `arcium/ui/sidebar/icons/muted.icon` and the GN `aggregate_vector_icons` target
- Modify: `arcium/ui/sidebar/sidebar_view.cc` (attach pinned and today lists), `BUILD.gn`

**Interfaces:**
- Produces `arcium::TabRowView`: `SetRow(const SidebarRow&)`, hover close button, click activates, middle-click closes. Delegate callbacks: `on_activate(int)`, `on_close(int)`, `on_drag_move(int from, int to)`.
- Produces `arcium::TabListView(SidebarModel*, SidebarSection)`: `SetRows(const std::vector<SidebarRow>&)` keeps one `TabRowView` per row of its section, reusing views by `tab_index` order to avoid churn; the Today list appends a "New tab" row.

- [ ] **Step 1: Add the icon files and GN target**

Copy Chromium's own icon sources so the look matches the platform: `close.icon` from `components/vector_icons/close_small.icon`, `add.icon` from `components/vector_icons/add_2.icon`, `audio.icon` and `muted.icon` from `chrome/browser/ui/views/tabs/tab/` alert icons if present, else from `components/vector_icons/` (`volume_up.icon`, `volume_off.icon` or similar; `ls components/vector_icons | grep -i volume`). Then:

```bash
mkdir -p arcium/ui/sidebar/icons
S=/Volumes/Texternal/chromium/src
cp "$S/components/vector_icons/close_small.icon" arcium/ui/sidebar/icons/close.icon
cp "$S/components/vector_icons/add_2.icon" arcium/ui/sidebar/icons/add.icon
ls "$S/components/vector_icons" | grep -iE "volume|audio|mute" | head
```
Pick the two audio icons from that listing and copy them as `audio.icon` and `muted.icon`. Add to `arcium/ui/sidebar/BUILD.gn`:

```gn
aggregate_vector_icons("sidebar_icons") {
  icon_directory = "icons"
  sources = [
    "add.icon",
    "audio.icon",
    "back.icon",
    "close.icon",
    "forward.icon",
    "muted.icon",
    "reload.icon",
    "sidebar.icon",
  ]
}
```
and `":sidebar_icons"` to the `sidebar` target's deps. `back.icon`, `forward.icon`, `reload.icon` are copied from the `*_old.icon` variants in `components/vector_icons` (`back_arrow_old.icon`, `forward_arrow_old.icon`, `reload_custom.icon`); `sidebar.icon` from `components/vector_icons/` whichever icon shows a side panel (`ls | grep -i side`). The generated header is `arcium/ui/sidebar/icons/sidebar_icons.h`... its exact path and the symbol names (`kCloseIcon` etc., prefixed per the template) are printed by the template; check `gen/arcium/ui/sidebar/` after the first build and match the includes below.

- [ ] **Step 2: Write TabRowView**

```bash
cat > arcium/ui/sidebar/tab_row_view.h <<'EOF'
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_TAB_ROW_VIEW_H_
#define ARCIUM_UI_SIDEBAR_TAB_ROW_VIEW_H_

#include "arcium/ui/sidebar/sidebar_model.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/button/button.h"

namespace views {
class ImageButton;
class ImageView;
class Label;
class Throbber;
}  // namespace views

namespace arcium {

// One 32px row: favicon or throbber, title, audio indicator, hover close.
class TabRowView : public views::Button {
  METADATA_HEADER(TabRowView, views::Button)

 public:
  struct Delegate {
    base::RepeatingCallback<void(int tab_index)> activate;
    base::RepeatingCallback<void(int tab_index)> close;
    // Called while dragging: the row at `from` wants to move to `to`.
    base::RepeatingCallback<void(int from, int to)> drag_move;
  };

  explicit TabRowView(Delegate delegate);
  TabRowView(const TabRowView&) = delete;
  TabRowView& operator=(const TabRowView&) = delete;
  ~TabRowView() override;

  void SetRow(const SidebarRow& row);
  int tab_index() const { return row_.tab_index; }

  // views::Button / View:
  bool OnMousePressed(const ui::MouseEvent& event) override;
  bool OnMouseDragged(const ui::MouseEvent& event) override;
  void OnMouseReleased(const ui::MouseEvent& event) override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;
  void OnThemeChanged() override;
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;

 private:
  void UpdateVisuals();
  void UpdateCloseButtonVisibility();

  Delegate delegate_;
  SidebarRow row_;
  bool hovered_ = false;
  bool dragging_ = false;
  gfx::Point drag_start_;

  raw_ptr<views::ImageView> favicon_ = nullptr;
  raw_ptr<views::Throbber> throbber_ = nullptr;
  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<views::ImageView> audio_ = nullptr;
  raw_ptr<views::ImageButton> close_ = nullptr;
};

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_TAB_ROW_VIEW_H_
EOF
cat > arcium/ui/sidebar/tab_row_view.cc <<'EOF'
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/tab_row_view.h"

#include <memory>

#include "arcium/ui/sidebar/icons/sidebar_icons.h"
#include "arcium/ui/sidebar/sidebar_colors.h"
#include "arcium/ui/sidebar/sidebar_metrics.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/events/event.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/throbber.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/layout/flex_layout_types.h"
#include "ui/views/view_class_properties.h"

namespace arcium {

namespace {
constexpr int kDragThreshold = 4;
}  // namespace

TabRowView::TabRowView(Delegate delegate)
    : views::Button(base::BindRepeating(
          [](TabRowView* self) { self->delegate_.activate.Run(self->tab_index()); },
          base::Unretained(this))),
      delegate_(std::move(delegate)) {
  SetFocusBehavior(FocusBehavior::ACCESSIBLE_ONLY);
  auto* layout = SetLayoutManager(std::make_unique<views::FlexLayout>());
  layout->SetOrientation(views::LayoutOrientation::kHorizontal)
      .SetCrossAxisAlignment(views::LayoutAlignment::kCenter)
      .SetInteriorMargin(gfx::Insets::VH(0, metrics::kRowHorizontalPadding))
      .SetDefault(views::kMarginsKey,
                  gfx::Insets::VH(0, metrics::kRowIconTextGap / 2));

  favicon_ = AddChildView(std::make_unique<views::ImageView>());
  favicon_->SetImageSize(gfx::Size(metrics::kFaviconSize, metrics::kFaviconSize));
  throbber_ = AddChildView(std::make_unique<views::Throbber>());
  throbber_->SetPreferredSize(gfx::Size(metrics::kFaviconSize, metrics::kFaviconSize));
  throbber_->SetVisible(false);

  title_ = AddChildView(std::make_unique<views::Label>());
  title_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  title_->SetElideBehavior(gfx::ELIDE_TAIL);
  title_->SetProperty(
      views::kFlexBehaviorKey,
      views::FlexSpecification(views::MinimumFlexSizeRule::kScaleToZero,
                               views::MaximumFlexSizeRule::kUnbounded));

  audio_ = AddChildView(std::make_unique<views::ImageView>());
  audio_->SetVisible(false);

  close_ = AddChildView(views::CreateVectorImageButtonWithNativeTheme(
      base::BindRepeating(
          [](TabRowView* self) { self->delegate_.close.Run(self->tab_index()); },
          base::Unretained(this)),
      kArciumCloseIcon, 14));
  close_->SetVisible(false);
  close_->SetFocusBehavior(FocusBehavior::ACCESSIBLE_ONLY);
}

TabRowView::~TabRowView() = default;

void TabRowView::SetRow(const SidebarRow& row) {
  row_ = row;
  UpdateVisuals();
}

void TabRowView::UpdateVisuals() {
  favicon_->SetImage(row_.favicon);
  favicon_->SetVisible(!row_.is_loading);
  throbber_->SetVisible(row_.is_loading);
  if (row_.is_loading) {
    throbber_->Start();
  } else {
    throbber_->Stop();
  }
  title_->SetText(row_.title);
  title_->SetEnabledColor(row_.is_active ? kColorArciumRowTextActive
                                         : kColorArciumRowText);
  audio_->SetVisible(row_.is_audible || row_.is_muted);
  if (row_.is_audible || row_.is_muted) {
    audio_->SetImage(ui::ImageModel::FromVectorIcon(
        row_.is_muted ? kArciumMutedIcon : kArciumAudioIcon,
        kColorArciumRowText, 14));
  }
  SetAccessibleName(row_.title);
  UpdateCloseButtonVisibility();
  OnThemeChanged();
}

void TabRowView::UpdateCloseButtonVisibility() {
  close_->SetVisible(hovered_);
  // The audio indicator yields its slot to the close button on hover.
  audio_->SetVisible(!hovered_ && (row_.is_audible || row_.is_muted));
}

bool TabRowView::OnMousePressed(const ui::MouseEvent& event) {
  if (event.IsOnlyMiddleMouseButton()) {
    delegate_.close.Run(tab_index());
    return true;
  }
  drag_start_ = event.location();
  dragging_ = false;
  return views::Button::OnMousePressed(event);
}

bool TabRowView::OnMouseDragged(const ui::MouseEvent& event) {
  if (!dragging_ &&
      std::abs(event.location().y() - drag_start_.y()) > kDragThreshold) {
    dragging_ = true;
  }
  if (dragging_) {
    // Ask the list to move us when the pointer crosses a neighbour's midline.
    const int rows_moved = (event.location().y() - drag_start_.y()) / height();
    if (rows_moved != 0) {
      delegate_.drag_move.Run(tab_index(), tab_index() + rows_moved);
    }
    return true;
  }
  return views::Button::OnMouseDragged(event);
}

void TabRowView::OnMouseReleased(const ui::MouseEvent& event) {
  const bool was_dragging = dragging_;
  dragging_ = false;
  if (!was_dragging) {
    views::Button::OnMouseReleased(event);
  }
}

void TabRowView::OnMouseEntered(const ui::MouseEvent& event) {
  hovered_ = true;
  UpdateCloseButtonVisibility();
  OnThemeChanged();
}

void TabRowView::OnMouseExited(const ui::MouseEvent& event) {
  hovered_ = false;
  UpdateCloseButtonVisibility();
  OnThemeChanged();
}

void TabRowView::OnThemeChanged() {
  views::Button::OnThemeChanged();
  if (row_.is_active) {
    SetBackground(views::CreateRoundedRectBackground(
        kColorArciumRowActiveBackground, metrics::kRowCornerRadius));
  } else if (hovered_) {
    SetBackground(views::CreateRoundedRectBackground(
        kColorArciumRowHoverBackground, metrics::kRowCornerRadius));
  } else {
    SetBackground(nullptr);
  }
}

gfx::Size TabRowView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return gfx::Size(available_size.width().value_or(metrics::kSidebarWidth),
                   metrics::kRowHeight);
}

BEGIN_METADATA(TabRowView)
END_METADATA

}  // namespace arcium
EOF
```

`sidebar_colors.h` with the colour ids is written in Task 8; for this task create it with only the id enum and no mixer so the build passes:

```bash
cat > arcium/ui/sidebar/sidebar_colors.h <<'EOF'
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_SIDEBAR_COLORS_H_
#define ARCIUM_UI_SIDEBAR_SIDEBAR_COLORS_H_

#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"

namespace ui {
class ColorProviderKey;
}

namespace arcium {

// Colour ids for the sidebar, allocated after Chrome's own range so both can
// live in one ColorProvider. The value must not collide with
// chrome/browser/ui/color/chrome_color_id.h; kChromeColorsEnd is the floor.
enum ArciumColorIds : ui::ColorId {
  kArciumColorsStart = 0x7A000,  // Well above every Chrome and component id.
  kColorArciumSidebarBackgroundTop = kArciumColorsStart,
  kColorArciumSidebarBackgroundBottom,
  kColorArciumRowText,
  kColorArciumRowTextActive,
  kColorArciumRowTextSecondary,
  kColorArciumRowActiveBackground,
  kColorArciumRowHoverBackground,
  kColorArciumControlBackground,
  kColorArciumControlIcon,
  kColorArciumDivider,
  kColorArciumSpaceAccent,
  kColorArciumSpaceChipActiveBackground,
  kArciumColorsEnd,
};

// Adds the Arcium colours to `provider`. Registered by the browser through a
// hook and by the playground directly.
void AddArciumColorMixer(ui::ColorProvider* provider,
                         const ui::ColorProviderKey& key);

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_SIDEBAR_COLORS_H_
EOF
```
Check `ui/color/color_id.h` for the numeric layout of ids: if `ColorId` ranges are enumerated contiguously (`kUiColorsEnd`, `kComponentsColorsEnd`, then Chrome's `kChromeColorsEnd` around a few thousand), any value above them is free; verify `kChromeColorsEnd < 0x7A000` with a `static_assert` in `sidebar_colors.cc` on the browser side (Task 8) where `chrome_color_id.h` is includable.

- [ ] **Step 3: Write TabListView**

```bash
cat > arcium/ui/sidebar/tab_list_view.h <<'EOF'
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_TAB_LIST_VIEW_H_
#define ARCIUM_UI_SIDEBAR_TAB_LIST_VIEW_H_

#include <vector>

#include "arcium/ui/sidebar/sidebar_model.h"
#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

namespace views {
class LabelButton;
}

namespace arcium {

class TabRowView;

// A vertical list of TabRowViews for one section. The Today list also shows
// the "New tab" row at its end.
class TabListView : public views::View {
  METADATA_HEADER(TabListView, views::View)

 public:
  TabListView(SidebarModel* model, SidebarSection section);
  TabListView(const TabListView&) = delete;
  TabListView& operator=(const TabListView&) = delete;
  ~TabListView() override;

  // Filters `rows` to this section and updates children, reusing views.
  void SetRows(const std::vector<SidebarRow>& rows);

  size_t row_count() const { return rows_.size(); }

 private:
  void OnDragMove(int from, int to);

  raw_ptr<SidebarModel> model_;
  const SidebarSection section_;
  std::vector<raw_ptr<TabRowView>> rows_;
  raw_ptr<views::LabelButton> new_tab_ = nullptr;
};

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_TAB_LIST_VIEW_H_
EOF
cat > arcium/ui/sidebar/tab_list_view.cc <<'EOF'
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/tab_list_view.h"

#include <memory>

#include "arcium/ui/sidebar/icons/sidebar_icons.h"
#include "arcium/ui/sidebar/sidebar_colors.h"
#include "arcium/ui/sidebar/sidebar_metrics.h"
#include "arcium/ui/sidebar/tab_row_view.h"
#include "base/functional/bind.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/layout/box_layout.h"

namespace arcium {

TabListView::TabListView(SidebarModel* model, SidebarSection section)
    : model_(model), section_(section) {
  SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 2));
  if (section_ == SidebarSection::kToday) {
    new_tab_ = AddChildView(std::make_unique<views::LabelButton>(
        base::BindRepeating(&SidebarModel::NewTab, base::Unretained(model_)),
        u"New tab"));
    new_tab_->SetImageModel(
        views::Button::STATE_NORMAL,
        ui::ImageModel::FromVectorIcon(kArciumAddIcon, kColorArciumRowTextSecondary, 16));
    new_tab_->SetEnabledTextColors(kColorArciumRowTextSecondary);
    new_tab_->SetBorder(views::CreateEmptyBorder(
        gfx::Insets::VH(0, metrics::kRowHorizontalPadding)));
    new_tab_->SetMinSize(gfx::Size(0, metrics::kRowHeight));
    new_tab_->SetImageLabelSpacing(metrics::kRowIconTextGap);
    new_tab_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  }
}

TabListView::~TabListView() = default;

void TabListView::SetRows(const std::vector<SidebarRow>& all_rows) {
  std::vector<const SidebarRow*> mine;
  for (const SidebarRow& row : all_rows) {
    if (row.section == section_) {
      mine.push_back(&row);
    }
  }
  // Grow or shrink the pool of row views, then assign in order. Views are
  // reused by position so a title change or reorder does not allocate.
  while (rows_.size() < mine.size()) {
    TabRowView::Delegate delegate;
    delegate.activate = base::BindRepeating(&SidebarModel::ActivateTab,
                                            base::Unretained(model_));
    delegate.close = base::BindRepeating(&SidebarModel::CloseTab,
                                         base::Unretained(model_));
    delegate.drag_move = base::BindRepeating(&TabListView::OnDragMove,
                                             base::Unretained(this));
    auto* row = AddChildViewAt(std::make_unique<TabRowView>(std::move(delegate)),
                               rows_.size());
    rows_.push_back(row);
  }
  while (rows_.size() > mine.size()) {
    TabRowView* row = rows_.back();
    rows_.pop_back();
    RemoveChildViewT(row);
  }
  for (size_t i = 0; i < mine.size(); ++i) {
    rows_[i]->SetRow(*mine[i]);
  }
  SetVisible(!rows_.empty() || new_tab_);
  InvalidateLayout();
}

void TabListView::OnDragMove(int from, int to) {
  // Clamp to this section's range in tab-index space.
  if (rows_.empty()) {
    return;
  }
  const int first = rows_.front()->tab_index();
  const int last = rows_.back()->tab_index();
  to = std::clamp(to, first, last);
  if (to != from) {
    model_->MoveTab(from, to);
  }
}

BEGIN_METADATA(TabListView)
END_METADATA

}  // namespace arcium
EOF
```

- [ ] **Step 4: Attach the lists in SidebarView and build the playground**

In `sidebar_view.cc` constructor, after the layout setup and before `Rebuild()`:

```cpp
  pinned_ = AddChildView(std::make_unique<TabListView>(model_, SidebarSection::kPinned));
  today_ = AddChildView(std::make_unique<TabListView>(model_, SidebarSection::kToday));
  today_->SetProperty(
      views::kFlexBehaviorKey,
      views::FlexSpecification(views::MinimumFlexSizeRule::kScaleToZero,
                               views::MaximumFlexSizeRule::kPreferred));
```
with `#include "arcium/ui/sidebar/tab_list_view.h"`. Add the four new files to `BUILD.gn` sources (`tab_row_view.*`, `tab_list_view.*`, `sidebar_colors.h`). Then:

```bash
scripts/playground
```
Expected: pinned rows (Discord, Linear) then today rows (four, "tigranhov/arcium" highlighted, "StoragePartitionConfig" with a spinning throbber, "Chromium Views tutorial" with an audio icon) and a "New tab" row. Hover shows the close button; clicking a row highlights it; middle-click closes; dragging a row down two rows reorders. Until Task 8 the colours resolve to the provider's defaults (black text), which is expected.

- [ ] **Step 5: Commit**

```bash
git add arcium/ui/sidebar
git commit -m "Sidebar: TabRowView and TabListView for Pinned and Today

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 7: NavRowView, UrlPillView placeholder, FavoritesGridView, SectionDividerView, SpaceBarView

**Files:**
- Create: `arcium/ui/sidebar/nav_row_view.h/.cc`, `url_pill_view.h/.cc`, `favorites_grid_view.h/.cc`, `section_divider_view.h/.cc`, `space_bar_view.h/.cc`
- Modify: `sidebar_view.cc/.h` (attach all sections; caption hit-test), `BUILD.gn`

**Interfaces:**
- `NavRowView(Delegate)` with `Delegate{ toggle_sidebar, back, forward, reload }` callbacks and `SetLeadingInset(int)`; `SetBackEnabled(bool)`, `SetForwardEnabled(bool)`.
- `UrlPillView`: `SetPlaceholderText(std::u16string)`; `SetHostedView(std::unique_ptr<views::View>)` replaces the placeholder with the real location bar (Task 10); `on_click` callback for the placeholder.
- `FavoritesGridView(SidebarModel*)`: `SetRows(...)` shows rows of section kFavorites as 4-per-row tiles; click activates.
- `SectionDividerView(base::RepeatingClosure on_clear)`: hairline; "Clear" label button visible on hover.
- `SpaceBarView`: one chip "Default" active, profile badge; right-click shows a context menu with disabled items "Rename space", "Edit theme…", "Change icon", "Delete space".

- [ ] **Step 1: NavRowView**

```bash
cat > arcium/ui/sidebar/nav_row_view.h <<'EOF'
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_NAV_ROW_VIEW_H_
#define ARCIUM_UI_SIDEBAR_NAV_ROW_VIEW_H_

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

namespace views {
class ImageButton;
}

namespace arcium {

// Row 1 of the sidebar: [caption inset][sidebar toggle] ... [back][fwd][reload]
class NavRowView : public views::View {
  METADATA_HEADER(NavRowView, views::View)

 public:
  struct Delegate {
    base::RepeatingClosure toggle_sidebar;
    base::RepeatingClosure back;
    base::RepeatingClosure forward;
    base::RepeatingClosure reload;
  };

  explicit NavRowView(Delegate delegate);
  NavRowView(const NavRowView&) = delete;
  NavRowView& operator=(const NavRowView&) = delete;
  ~NavRowView() override;

  void SetLeadingInset(int inset);
  void SetBackEnabled(bool enabled);
  void SetForwardEnabled(bool enabled);

  // True if `point` (in this view's coordinates) is not over a button.
  bool IsPointOnBackground(const gfx::Point& point) const;

 private:
  views::ImageButton* AddButton(base::RepeatingClosure callback,
                                const gfx::VectorIcon& icon,
                                const std::u16string& tooltip);

  Delegate delegate_;
  raw_ptr<views::View> leading_spacer_ = nullptr;
  raw_ptr<views::ImageButton> toggle_ = nullptr;
  raw_ptr<views::View> flex_spacer_ = nullptr;
  raw_ptr<views::ImageButton> back_ = nullptr;
  raw_ptr<views::ImageButton> forward_ = nullptr;
  raw_ptr<views::ImageButton> reload_ = nullptr;
};

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_NAV_ROW_VIEW_H_
EOF
cat > arcium/ui/sidebar/nav_row_view.cc <<'EOF'
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/nav_row_view.h"

#include <memory>

#include "arcium/ui/sidebar/icons/sidebar_icons.h"
#include "arcium/ui/sidebar/sidebar_colors.h"
#include "arcium/ui/sidebar/sidebar_metrics.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/layout/flex_layout_types.h"
#include "ui/views/view_class_properties.h"

namespace arcium {

NavRowView::NavRowView(Delegate delegate) : delegate_(std::move(delegate)) {
  auto* layout = SetLayoutManager(std::make_unique<views::FlexLayout>());
  layout->SetOrientation(views::LayoutOrientation::kHorizontal)
      .SetCrossAxisAlignment(views::LayoutAlignment::kCenter)
      .SetDefault(views::kMarginsKey, gfx::Insets::VH(0, 2));

  leading_spacer_ = AddChildView(std::make_unique<views::View>());
  toggle_ = AddButton(delegate_.toggle_sidebar, kArciumSidebarIcon, u"Hide sidebar");

  flex_spacer_ = AddChildView(std::make_unique<views::View>());
  flex_spacer_->SetProperty(
      views::kFlexBehaviorKey,
      views::FlexSpecification(views::MinimumFlexSizeRule::kScaleToZero,
                               views::MaximumFlexSizeRule::kUnbounded));

  back_ = AddButton(delegate_.back, kArciumBackIcon, u"Back");
  forward_ = AddButton(delegate_.forward, kArciumForwardIcon, u"Forward");
  reload_ = AddButton(delegate_.reload, kArciumReloadIcon, u"Reload");
  SetLeadingInset(0);
}

NavRowView::~NavRowView() = default;

views::ImageButton* NavRowView::AddButton(base::RepeatingClosure callback,
                                          const gfx::VectorIcon& icon,
                                          const std::u16string& tooltip) {
  auto button = views::CreateVectorImageButtonWithNativeTheme(
      std::move(callback), icon, 16);
  button->SetTooltipText(tooltip);
  button->SetPreferredSize(
      gfx::Size(metrics::kNavButtonSize, metrics::kNavButtonSize));
  views::InstallRoundRectHighlightPathGenerator(button.get(), gfx::Insets(), 6);
  return AddChildView(std::move(button));
}

void NavRowView::SetLeadingInset(int inset) {
  leading_spacer_->SetPreferredSize(gfx::Size(inset, metrics::kNavButtonSize));
  InvalidateLayout();
}

void NavRowView::SetBackEnabled(bool enabled) {
  back_->SetEnabled(enabled);
}

void NavRowView::SetForwardEnabled(bool enabled) {
  forward_->SetEnabled(enabled);
}

bool NavRowView::IsPointOnBackground(const gfx::Point& point) const {
  for (const views::View* child : children()) {
    if (child != leading_spacer_ && child != flex_spacer_ &&
        child->bounds().Contains(point)) {
      return false;
    }
  }
  return true;
}

BEGIN_METADATA(NavRowView)
END_METADATA

}  // namespace arcium
EOF
```

- [ ] **Step 2: UrlPillView**

```bash
cat > arcium/ui/sidebar/url_pill_view.h <<'EOF'
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_URL_PILL_VIEW_H_
#define ARCIUM_UI_SIDEBAR_URL_PILL_VIEW_H_

#include <memory>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

namespace views {
class Label;
}

namespace arcium {

// Row 2: a rounded pill. In the browser it hosts Chromium's LocationBarView
// (Task 10). In the playground, or as the fallback, it shows text and calls
// `on_click`.
class UrlPillView : public views::View {
  METADATA_HEADER(UrlPillView, views::View)

 public:
  explicit UrlPillView(base::RepeatingClosure on_click);
  UrlPillView(const UrlPillView&) = delete;
  UrlPillView& operator=(const UrlPillView&) = delete;
  ~UrlPillView() override;

  void SetPlaceholderText(const std::u16string& text);
  // Replaces the placeholder with `view`, which fills the pill.
  views::View* SetHostedView(std::unique_ptr<views::View> view);
  bool has_hosted_view() const { return hosted_ != nullptr; }

  // views::View:
  bool OnMousePressed(const ui::MouseEvent& event) override;
  void OnThemeChanged() override;
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;

 private:
  base::RepeatingClosure on_click_;
  raw_ptr<views::Label> placeholder_ = nullptr;
  raw_ptr<views::View> hosted_ = nullptr;
};

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_URL_PILL_VIEW_H_
EOF
cat > arcium/ui/sidebar/url_pill_view.cc <<'EOF'
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/url_pill_view.h"

#include "arcium/ui/sidebar/sidebar_colors.h"
#include "arcium/ui/sidebar/sidebar_metrics.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/events/event.h"
#include "ui/views/background.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/fill_layout.h"

namespace arcium {

UrlPillView::UrlPillView(base::RepeatingClosure on_click)
    : on_click_(std::move(on_click)) {
  SetLayoutManager(std::make_unique<views::FillLayout>());
  placeholder_ = AddChildView(std::make_unique<views::Label>());
  placeholder_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  placeholder_->SetElideBehavior(gfx::ELIDE_TAIL);
  placeholder_->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(0, 10)));
  placeholder_->SetEnabledColor(kColorArciumRowTextSecondary);
}

UrlPillView::~UrlPillView() = default;

void UrlPillView::SetPlaceholderText(const std::u16string& text) {
  placeholder_->SetText(text);
}

views::View* UrlPillView::SetHostedView(std::unique_ptr<views::View> view) {
  placeholder_->SetVisible(false);
  hosted_ = AddChildView(std::move(view));
  InvalidateLayout();
  return hosted_;
}

bool UrlPillView::OnMousePressed(const ui::MouseEvent& event) {
  if (!hosted_ && event.IsOnlyLeftMouseButton()) {
    on_click_.Run();
    return true;
  }
  return views::View::OnMousePressed(event);
}

void UrlPillView::OnThemeChanged() {
  views::View::OnThemeChanged();
  SetBackground(views::CreateRoundedRectBackground(
      kColorArciumControlBackground, metrics::kUrlPillHeight / 3));
}

gfx::Size UrlPillView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return gfx::Size(available_size.width().value_or(metrics::kSidebarWidth),
                   metrics::kUrlPillHeight);
}

BEGIN_METADATA(UrlPillView)
END_METADATA

}  // namespace arcium
EOF
```

- [ ] **Step 3: FavoritesGridView**

```bash
cat > arcium/ui/sidebar/favorites_grid_view.h <<'EOF'
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_FAVORITES_GRID_VIEW_H_
#define ARCIUM_UI_SIDEBAR_FAVORITES_GRID_VIEW_H_

#include <vector>

#include "arcium/ui/sidebar/sidebar_model.h"
#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

namespace views {
class ImageButton;
}

namespace arcium {

// Square tiles, four per row, one per row of section kFavorites.
class FavoritesGridView : public views::View {
  METADATA_HEADER(FavoritesGridView, views::View)

 public:
  explicit FavoritesGridView(SidebarModel* model);
  FavoritesGridView(const FavoritesGridView&) = delete;
  FavoritesGridView& operator=(const FavoritesGridView&) = delete;
  ~FavoritesGridView() override;

  void SetRows(const std::vector<SidebarRow>& rows);

  // views::View:
  void Layout(PassKey) override;
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;

 private:
  raw_ptr<SidebarModel> model_;
  std::vector<raw_ptr<views::ImageButton>> tiles_;
  std::vector<int> tab_indices_;
};

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_FAVORITES_GRID_VIEW_H_
EOF
cat > arcium/ui/sidebar/favorites_grid_view.cc <<'EOF'
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/favorites_grid_view.h"

#include <memory>

#include "arcium/ui/sidebar/sidebar_colors.h"
#include "arcium/ui/sidebar/sidebar_metrics.h"
#include "base/functional/bind.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/image_button.h"

namespace arcium {

namespace {

int TileSize(int width) {
  const int gaps = (metrics::kFavoritesPerRow - 1) * metrics::kFavoriteTileGap;
  return (width - gaps) / metrics::kFavoritesPerRow;
}

}  // namespace

FavoritesGridView::FavoritesGridView(SidebarModel* model) : model_(model) {}

FavoritesGridView::~FavoritesGridView() = default;

void FavoritesGridView::SetRows(const std::vector<SidebarRow>& rows) {
  std::vector<const SidebarRow*> mine;
  for (const SidebarRow& row : rows) {
    if (row.section == SidebarSection::kFavorites) {
      mine.push_back(&row);
    }
  }
  while (tiles_.size() < mine.size()) {
    auto tile = std::make_unique<views::ImageButton>();
    tile->SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
    tile->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
    tiles_.push_back(AddChildView(std::move(tile)));
  }
  while (tiles_.size() > mine.size()) {
    RemoveChildViewT(tiles_.back().get());
    tiles_.pop_back();
  }
  tab_indices_.clear();
  for (size_t i = 0; i < mine.size(); ++i) {
    const SidebarRow& row = *mine[i];
    tab_indices_.push_back(row.tab_index);
    tiles_[i]->SetImageModel(views::Button::STATE_NORMAL, row.favicon);
    tiles_[i]->SetTooltipText(row.title);
    tiles_[i]->SetAccessibleName(row.title);
    tiles_[i]->SetCallback(base::BindRepeating(
        &SidebarModel::ActivateTab, base::Unretained(model_), row.tab_index));
    tiles_[i]->SetBackground(views::CreateRoundedRectBackground(
        row.is_active ? kColorArciumRowActiveBackground
                      : kColorArciumControlBackground,
        metrics::kRowCornerRadius));
  }
  SetVisible(!tiles_.empty());
  InvalidateLayout();
}

void FavoritesGridView::Layout(PassKey) {
  const int size = TileSize(width());
  for (size_t i = 0; i < tiles_.size(); ++i) {
    const int col = i % metrics::kFavoritesPerRow;
    const int row = i / metrics::kFavoritesPerRow;
    tiles_[i]->SetBounds(col * (size + metrics::kFavoriteTileGap),
                         row * (size + metrics::kFavoriteTileGap), size, size);
  }
}

gfx::Size FavoritesGridView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  const int width = available_size.width().value_or(
      metrics::kSidebarWidth - 2 * metrics::kSidebarPadding);
  const int rows =
      (static_cast<int>(tiles_.size()) + metrics::kFavoritesPerRow - 1) /
      metrics::kFavoritesPerRow;
  const int size = TileSize(width);
  return gfx::Size(width, rows == 0 ? 0
                                    : rows * size +
                                          (rows - 1) * metrics::kFavoriteTileGap);
}

BEGIN_METADATA(FavoritesGridView)
END_METADATA

}  // namespace arcium
EOF
```

- [ ] **Step 4: SectionDividerView and SpaceBarView**

```bash
cat > arcium/ui/sidebar/section_divider_view.h <<'EOF'
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_SECTION_DIVIDER_VIEW_H_
#define ARCIUM_UI_SIDEBAR_SECTION_DIVIDER_VIEW_H_

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

namespace views {
class LabelButton;
class Separator;
}  // namespace views

namespace arcium {

// Hairline between Pinned and Today. Hovering reveals "Clear", which closes
// every Today tab.
class SectionDividerView : public views::View {
  METADATA_HEADER(SectionDividerView, views::View)

 public:
  explicit SectionDividerView(base::RepeatingClosure on_clear);
  SectionDividerView(const SectionDividerView&) = delete;
  SectionDividerView& operator=(const SectionDividerView&) = delete;
  ~SectionDividerView() override;

  // views::View:
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;

 private:
  raw_ptr<views::Separator> line_ = nullptr;
  raw_ptr<views::LabelButton> clear_ = nullptr;
};

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_SECTION_DIVIDER_VIEW_H_
EOF
cat > arcium/ui/sidebar/section_divider_view.cc <<'EOF'
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/section_divider_view.h"

#include <memory>

#include "arcium/ui/sidebar/sidebar_colors.h"
#include "arcium/ui/sidebar/sidebar_metrics.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/separator.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/layout/flex_layout_types.h"
#include "ui/views/view_class_properties.h"

namespace arcium {

SectionDividerView::SectionDividerView(base::RepeatingClosure on_clear) {
  auto* layout = SetLayoutManager(std::make_unique<views::FlexLayout>());
  layout->SetOrientation(views::LayoutOrientation::kHorizontal)
      .SetCrossAxisAlignment(views::LayoutAlignment::kCenter)
      .SetInteriorMargin(gfx::Insets::VH(0, metrics::kRowHorizontalPadding));
  line_ = AddChildView(std::make_unique<views::Separator>());
  line_->SetColorId(kColorArciumDivider);
  line_->SetProperty(
      views::kFlexBehaviorKey,
      views::FlexSpecification(views::MinimumFlexSizeRule::kScaleToZero,
                               views::MaximumFlexSizeRule::kUnbounded));
  clear_ = AddChildView(std::make_unique<views::LabelButton>(std::move(on_clear), u"Clear"));
  clear_->SetEnabledTextColors(kColorArciumRowTextSecondary);
  clear_->SetVisible(false);
  clear_->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(0, 8, 0, 0));
}

SectionDividerView::~SectionDividerView() = default;

void SectionDividerView::OnMouseEntered(const ui::MouseEvent& event) {
  clear_->SetVisible(true);
}

void SectionDividerView::OnMouseExited(const ui::MouseEvent& event) {
  // Keep it while the pointer is over the button itself.
  if (!clear_->IsMouseHovered()) {
    clear_->SetVisible(false);
  }
}

gfx::Size SectionDividerView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return gfx::Size(available_size.width().value_or(metrics::kSidebarWidth), 20);
}

BEGIN_METADATA(SectionDividerView)
END_METADATA

}  // namespace arcium
EOF
cat > arcium/ui/sidebar/space_bar_view.h <<'EOF'
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_SPACE_BAR_VIEW_H_
#define ARCIUM_UI_SIDEBAR_SPACE_BAR_VIEW_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/models/simple_menu_model.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/view.h"

namespace views {
class LabelButton;
class MenuRunner;
}  // namespace views

namespace arcium {

// Bottom bar: space chips (one, "Default", in Stage 1) and the profile badge.
// The context menu exists with every item disabled; Stages 3 and 6 enable them.
class SpaceBarView : public views::View,
                     public views::ContextMenuController,
                     public ui::SimpleMenuModel::Delegate {
  METADATA_HEADER(SpaceBarView, views::View)

 public:
  enum MenuCommand { kRename = 1, kEditTheme, kChangeIcon, kDelete };

  SpaceBarView();
  SpaceBarView(const SpaceBarView&) = delete;
  SpaceBarView& operator=(const SpaceBarView&) = delete;
  ~SpaceBarView() override;

  // views::ContextMenuController:
  void ShowContextMenuForViewImpl(views::View* source,
                                  const gfx::Point& point,
                                  ui::mojom::MenuSourceType source_type) override;

  // ui::SimpleMenuModel::Delegate:
  bool IsCommandIdEnabled(int command_id) const override;
  void ExecuteCommand(int command_id, int event_flags) override;

  // views::View:
  void OnThemeChanged() override;

 private:
  raw_ptr<views::LabelButton> active_chip_ = nullptr;
  raw_ptr<views::View> profile_badge_ = nullptr;
  std::unique_ptr<ui::SimpleMenuModel> menu_model_;
  std::unique_ptr<views::MenuRunner> menu_runner_;
};

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_SPACE_BAR_VIEW_H_
EOF
cat > arcium/ui/sidebar/space_bar_view.cc <<'EOF'
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/space_bar_view.h"

#include "arcium/ui/sidebar/sidebar_colors.h"
#include "arcium/ui/sidebar/sidebar_metrics.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/layout/flex_layout_types.h"
#include "ui/views/view_class_properties.h"

namespace arcium {

SpaceBarView::SpaceBarView() {
  auto* layout = SetLayoutManager(std::make_unique<views::FlexLayout>());
  layout->SetOrientation(views::LayoutOrientation::kHorizontal)
      .SetCrossAxisAlignment(views::LayoutAlignment::kCenter)
      .SetInteriorMargin(gfx::Insets::TLBR(8, 0, 0, 0))
      .SetDefault(views::kMarginsKey, gfx::Insets::VH(0, 3));

  active_chip_ = AddChildView(std::make_unique<views::LabelButton>(
      views::Button::PressedCallback(), u"💼 Default"));
  active_chip_->SetMinSize(gfx::Size(0, metrics::kSpaceChipHeight));
  active_chip_->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(0, 9)));
  active_chip_->set_context_menu_controller(this);

  auto* spacer = AddChildView(std::make_unique<views::View>());
  spacer->SetProperty(
      views::kFlexBehaviorKey,
      views::FlexSpecification(views::MinimumFlexSizeRule::kScaleToZero,
                               views::MaximumFlexSizeRule::kUnbounded));

  profile_badge_ = AddChildView(std::make_unique<views::View>());
  profile_badge_->SetPreferredSize(
      gfx::Size(metrics::kProfileBadgeSize, metrics::kProfileBadgeSize));
  profile_badge_->SetTooltipText(u"Profile: Default");

  menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  menu_model_->AddItem(kRename, u"Rename space");
  menu_model_->AddItem(kEditTheme, u"Edit theme…");
  menu_model_->AddItem(kChangeIcon, u"Change icon");
  menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  menu_model_->AddItem(kDelete, u"Delete space");
}

SpaceBarView::~SpaceBarView() = default;

void SpaceBarView::ShowContextMenuForViewImpl(
    views::View* source,
    const gfx::Point& point,
    ui::mojom::MenuSourceType source_type) {
  menu_runner_ = std::make_unique<views::MenuRunner>(
      menu_model_.get(), views::MenuRunner::CONTEXT_MENU);
  menu_runner_->RunMenuAt(source->GetWidget(), nullptr,
                          gfx::Rect(point, gfx::Size()),
                          views::MenuAnchorPosition::kTopLeft, source_type);
}

bool SpaceBarView::IsCommandIdEnabled(int command_id) const {
  return false;  // Stage 3 (rename, icon, delete) and Stage 6 (theme).
}

void SpaceBarView::ExecuteCommand(int command_id, int event_flags) {}

void SpaceBarView::OnThemeChanged() {
  views::View::OnThemeChanged();
  active_chip_->SetBackground(views::CreateRoundedRectBackground(
      kColorArciumSpaceChipActiveBackground, 8));
  active_chip_->SetEnabledTextColors(kColorArciumRowTextActive);
  profile_badge_->SetBackground(views::CreateRoundedRectBackground(
      kColorArciumSpaceAccent, metrics::kProfileBadgeSize / 2));
}

BEGIN_METADATA(SpaceBarView)
END_METADATA

}  // namespace arcium
EOF
```

- [ ] **Step 5: Assemble SidebarView**

Replace the constructor body and `IsPositionInWindowCaption` in `sidebar_view.cc`:

```cpp
SidebarView::SidebarView(SidebarModel* model, Delegate delegate)
    : model_(model), delegate_(std::move(delegate)) {
  // ... FlexLayout setup as before ...
  NavRowView::Delegate nav;
  nav.toggle_sidebar = delegate_.toggle_sidebar;
  nav.back = delegate_.back;
  nav.forward = delegate_.forward;
  nav.reload = delegate_.reload;
  nav_row_ = AddChildView(std::make_unique<NavRowView>(std::move(nav)));
  url_pill_ = AddChildView(std::make_unique<UrlPillView>(delegate_.edit_url));
  favorites_ = AddChildView(std::make_unique<FavoritesGridView>(model_));
  pinned_ = AddChildView(std::make_unique<TabListView>(model_, SidebarSection::kPinned));
  divider_ = AddChildView(std::make_unique<SectionDividerView>(
      base::BindRepeating(&SidebarModel::ClearToday, base::Unretained(model_))));
  today_ = AddChildView(std::make_unique<TabListView>(model_, SidebarSection::kToday));
  today_->SetProperty(views::kFlexBehaviorKey,
      views::FlexSpecification(views::MinimumFlexSizeRule::kScaleToZero,
                               views::MaximumFlexSizeRule::kPreferred));
  auto* spacer = AddChildView(std::make_unique<views::View>());
  spacer->SetProperty(views::kFlexBehaviorKey,
      views::FlexSpecification(views::MinimumFlexSizeRule::kScaleToZero,
                               views::MaximumFlexSizeRule::kUnbounded));
  space_bar_ = AddChildView(std::make_unique<SpaceBarView>());
  observation_.Observe(model_);
  Rebuild();
}

bool SidebarView::IsPositionInWindowCaption(const gfx::Point& point) const {
  gfx::Point p = point;
  ConvertPointToTarget(this, nav_row_, &p);
  if (nav_row_->bounds().Contains(point)) {
    return nav_row_->IsPointOnBackground(p);
  }
  // Empty space below the last row and above the space bar drags the window.
  return point.y() > today_->bounds().bottom() && point.y() < space_bar_->y();
}
```
Add to the header a `struct Delegate { base::RepeatingClosure toggle_sidebar, back, forward, reload, edit_url; }`, a `Delegate delegate_;` member, and the includes for all five section headers. Also fill `SetCaptionButtonWidth` with `nav_row_->SetLeadingInset(width)`. Playground: pass a `Delegate` whose closures log via `LOG(INFO)` and, for `edit_url`, do nothing. The URL pill placeholder text in the playground is the active row's URL host: in `Rebuild()`, after the section updates, find the active row and call `url_pill_->SetPlaceholderText(base::UTF8ToUTF16(row.url.host()))` when `!url_pill_->has_hosted_view()`.

Add all new files to `BUILD.gn` sources. Build and run:

```bash
scripts/playground
```
Expected: every zone from the mockup present in order: nav row with the traffic-light gap and toggle on the left, back, forward, reload on the right; URL pill reading `github.com`; favourites 2 rows of 4 tiles; two pinned rows; divider that shows "Clear" on hover, and clicking it empties Today; Today rows and "New tab"; the space bar with "💼 Default" and a coloured circle; right-click on the chip shows the four disabled items. Screenshot to `docs/screens/stage1/07-all-sections.png`.

- [ ] **Step 6: Commit**

```bash
git add arcium/ui/sidebar arcium/ui/playground docs/screens
git commit -m "Sidebar: nav row, URL pill, favourites grid, divider, space bar

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 8: Colours and the tinted background

**Files:**
- Create: `arcium/ui/sidebar/sidebar_colors.cc`
- Create: `arcium/ui/sidebar/tint_background.h/.cc`
- Modify: `sidebar_view.cc` (`OnThemeChanged` installs the background), playground main (register mixer), `BUILD.gn`
- Create: `patches/0080-color-mixer.patch`

**Interfaces:**
- `AddArciumColorMixer(provider, key)` defines every `kColorArcium*` id for light and dark, from a single accent (`SkColorSetRGB(0x8A, 0x8A, 0xFF)` for the default space).
- `TintBackground`: a `views::Background` painting a two-stop linear gradient top-left to bottom, cached per size.

- [ ] **Step 1: The mixer**

```bash
cat > arcium/ui/sidebar/sidebar_colors.cc <<'EOF'
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/sidebar_colors.h"

#include "third_party/skia/include/core/SkColor.h"
#include "ui/color/color_mixer.h"
#include "ui/color/color_provider_key.h"
#include "ui/color/color_recipe.h"
#include "ui/color/color_transform.h"

namespace arcium {

void AddArciumColorMixer(ui::ColorProvider* provider,
                         const ui::ColorProviderKey& key) {
  const bool dark = key.color_mode == ui::ColorProviderKey::ColorMode::kDark;
  ui::ColorMixer& mixer = provider->AddMixer();

  // Default space accent. Stage 3 makes this per space.
  constexpr SkColor kAccent = SkColorSetRGB(0x8A, 0x8A, 0xFF);
  const SkColor surface = dark ? SkColorSetRGB(0x17, 0x17, 0x1D)
                               : SkColorSetRGB(0xF2, 0xF2, 0xF6);
  const SkColor text = dark ? SkColorSetRGB(0xC3, 0xC3, 0xCC)
                            : SkColorSetRGB(0x33, 0x33, 0x3D);

  mixer[kColorArciumSidebarBackgroundBottom] = {surface};
  mixer[kColorArciumSidebarBackgroundTop] = {
      ui::AlphaBlend(kAccent, surface, dark ? 0x2E : 0x1A)};
  mixer[kColorArciumRowText] = {text};
  mixer[kColorArciumRowTextActive] = {dark ? SK_ColorWHITE
                                           : SkColorSetRGB(0x11, 0x11, 0x16)};
  mixer[kColorArciumRowTextSecondary] = {ui::SetAlpha(text, 0x99)};
  mixer[kColorArciumRowActiveBackground] = {
      ui::SetAlpha(dark ? SK_ColorWHITE : SK_ColorBLACK, dark ? 0x1F : 0x14)};
  mixer[kColorArciumRowHoverBackground] = {
      ui::SetAlpha(dark ? SK_ColorWHITE : SK_ColorBLACK, dark ? 0x10 : 0x0A)};
  mixer[kColorArciumControlBackground] = {
      ui::SetAlpha(dark ? SK_ColorWHITE : SK_ColorBLACK, dark ? 0x14 : 0x0C)};
  mixer[kColorArciumControlIcon] = {ui::SetAlpha(text, 0xB3)};
  mixer[kColorArciumDivider] = {
      ui::SetAlpha(dark ? SK_ColorWHITE : SK_ColorBLACK, 0x14)};
  mixer[kColorArciumSpaceAccent] = {kAccent};
  mixer[kColorArciumSpaceChipActiveBackground] = {ui::SetAlpha(kAccent, 0x38)};
}

}  // namespace arcium
EOF
```
`ui::AlphaBlend(fg, bg, alpha)` and `ui::SetAlpha` are in `ui/color/color_transform.h`; if `AlphaBlend` takes a different signature at this tag, use `ui::BlendTowardMaxContrast`-style helpers listed in that header and note it.

- [ ] **Step 2: The gradient background**

```bash
cat > arcium/ui/sidebar/tint_background.h <<'EOF'
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_SIDEBAR_TINT_BACKGROUND_H_
#define ARCIUM_UI_SIDEBAR_TINT_BACKGROUND_H_

#include "cc/paint/paint_shader.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/background.h"

namespace arcium {

// Two-stop linear gradient from kColorArciumSidebarBackgroundTop at the top
// leading corner to kColorArciumSidebarBackgroundBottom at the bottom. The
// shader is rebuilt only when the size or colours change.
class TintBackground : public views::Background {
 public:
  TintBackground();
  ~TintBackground() override;

  // views::Background:
  void Paint(gfx::Canvas* canvas, views::View* view) const override;
  void OnViewThemeChanged(views::View* view) override;

 private:
  mutable gfx::Size cached_size_;
  mutable SkColor cached_top_ = 0;
  mutable SkColor cached_bottom_ = 0;
  mutable sk_sp<cc::PaintShader> shader_;
};

}  // namespace arcium

#endif  // ARCIUM_UI_SIDEBAR_TINT_BACKGROUND_H_
EOF
cat > arcium/ui/sidebar/tint_background.cc <<'EOF'
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/sidebar/tint_background.h"

#include "arcium/ui/sidebar/sidebar_colors.h"
#include "cc/paint/paint_flags.h"
#include "third_party/skia/include/core/SkPoint.h"
#include "third_party/skia/include/core/SkTileMode.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/views/view.h"

namespace arcium {

TintBackground::TintBackground() = default;
TintBackground::~TintBackground() = default;

void TintBackground::Paint(gfx::Canvas* canvas, views::View* view) const {
  const ui::ColorProvider* cp = view->GetColorProvider();
  const SkColor top = cp->GetColor(kColorArciumSidebarBackgroundTop);
  const SkColor bottom = cp->GetColor(kColorArciumSidebarBackgroundBottom);
  const gfx::Size size = view->size();
  if (!shader_ || size != cached_size_ || top != cached_top_ ||
      bottom != cached_bottom_) {
    const SkPoint points[2] = {SkPoint::Make(0, 0),
                               SkPoint::Make(size.width() * 0.4f,
                                             size.height() * 0.55f)};
    const SkColor4f colors[2] = {SkColor4f::FromColor(top),
                                 SkColor4f::FromColor(bottom)};
    shader_ = cc::PaintShader::MakeLinearGradient(points, colors, nullptr, 2,
                                                  SkTileMode::kClamp);
    cached_size_ = size;
    cached_top_ = top;
    cached_bottom_ = bottom;
  }
  cc::PaintFlags flags;
  flags.setShader(shader_);
  canvas->DrawRect(gfx::Rect(size), flags);
}

void TintBackground::OnViewThemeChanged(views::View* view) {
  shader_ = nullptr;
  view->SchedulePaint();
}

}  // namespace arcium
EOF
```
In `SidebarView::OnThemeChanged`: `SetBackground(std::make_unique<TintBackground>());` (only once; guard with a bool). In `arcium_playground_main.cc`, before `ExamplesMainProc`: `ui::ColorProviderManager::Get().AppendColorProviderInitializer(base::BindRepeating(&arcium::AddArciumColorMixer));` with the include for `ui/color/color_provider_manager.h`. Add sources to `BUILD.gn` and `"//cc/paint"`, `"//ui/color"` to deps.

- [ ] **Step 3: The browser-side mixer hook**

```bash
cd /Volumes/Texternal/chromium/src
python3 - <<'EOF'
p = 'chrome/browser/ui/color/chrome_color_mixers.cc'
s = open(p).read()
old = '  AddNativeChromeColorMixer(provider, key);\n'
assert s.count(old) == 1
s = s.replace(old, old + '  arcium::AddArciumColorMixer(provider, key);\n')
i = s.index('#include '); j = s.index('\n', i) + 1
s = s[:j] + '#include "arcium/ui/sidebar/sidebar_colors.h"\n' + s[j:]
open(p, 'w').write(s)
EOF
cd /Volumes/Texternal/repositories/arcium
{ printf 'Seam: chrome/browser/ui/color/chrome_color_mixers.cc, AddChromeColorMixers.\nWhy: Arcium colour ids must resolve in every browser ColorProvider.\nDelegates to: arcium/ui/sidebar/sidebar_colors.cc\n\n'; git -C /Volumes/Texternal/chromium/src diff chrome/browser/ui/color/chrome_color_mixers.cc; } > patches/0080-color-mixer.patch
git -C /Volumes/Texternal/chromium/src checkout -- chrome/browser/ui/color/chrome_color_mixers.cc
scripts/sync
```
The `chrome/browser/ui/color` target needs `//arcium/ui/sidebar` in its deps; if it is a separate GN target (check `chrome/browser/ui/color/BUILD.gn`), add the dep there and include that file in the same patch. Add a `static_assert(kArciumColorsStart > kChromeColorsEnd)` in a new `arcium/ui/browser/color_ids_check.cc` that includes `chrome/browser/ui/color/chrome_color_id.h` to prove the ranges are disjoint.

- [ ] **Step 4: Build playground and browser; verify**

```bash
scripts/playground
```
Expected: the sidebar now has the subtle indigo wash from the top-left fading to the neutral surface, light text on dark, active row highlighted, exactly like mockup B. Switch the OS to light mode and relaunch: light surfaces. Screenshot both to `docs/screens/stage1/08-tint-dark.png` and `08-tint-light.png`.

```bash
scripts/build dev 2>&1 | tail -1
```
Expected: the browser still builds (the mixer is registered but nothing uses it yet).

- [ ] **Step 5: Commit**

```bash
git add arcium/ui patches/0080-color-mixer.patch docs/screens
git commit -m "Sidebar colours: Arcium colour mixer and cached gradient tint

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 9: Sidebar in the browser window: controller and layout hooks

**Files:**
- Create: `arcium/ui/browser/browser_sidebar_controller.h/.cc`; update `arcium/ui/browser/BUILD.gn`
- Create: `patches/0050-browser-view-sidebar.patch`, `patches/0060-layout-params-sidebar.patch`, `patches/0070-layout-sidebar-bounds.patch`

**Interfaces:**
- `arcium::BrowserSidebarController` (per `BrowserView`, owned by it):
  - `static BrowserSidebarController* MaybeCreate(BrowserView*)` returns null unless `features::IsSidebarEnabled()` and the browser is a normal tabbed browser.
  - `SidebarView* view()`; `int width()` (250 or 0 when hidden); `void AdjustLayoutParams(BrowserLayoutParams&)`; `void LayoutSidebar(const gfx::Rect& host_bounds)`; `bool IsPositionInWindowCaption(const gfx::Point& point_in_browser_view)`; `void UpdateContentCorners()`.
- Hooks (patched into upstream):
  1. `BrowserView::InitViews`: after `contents_container_ = AddChildView(...)`, `arcium_sidebar_ = arcium::BrowserSidebarController::MaybeCreate(this);`. Member `std::unique_ptr<arcium::BrowserSidebarController> arcium_sidebar_;` plus accessor `arcium_sidebar()` in `browser_view.h`.
  2. `BrowserView::ShouldDrawTabStrip`: `if (arcium_sidebar_) return false;` as the first line.
  3. `BrowserView::NonClientHitTest` (mac block near line 4572): before the vertical-tab-strip check, `if (arcium_sidebar_ && arcium_sidebar_->IsPositionInWindowCaption(point)) return HTCAPTION;`.
  4. `BrowserViewLayoutDelegateImpl::GetBrowserLayoutParams`: before the final `return params.InLocalCoordinates(...)`, keep the result in a local, then `if (auto* s = browser_view_->arcium_sidebar()) s->AdjustLayoutParams(result);` and return it.
  5. `BrowserViewLayoutDelegateImpl::IsToolbarVisible`: `if (browser_view_->arcium_sidebar()) return false;`.
  6. `BrowserViewLayoutImpl::Layout`: after `ApplyLayout(host, ...)`, `if (auto* s = browser_view()->arcium_sidebar()) s->LayoutSidebar(host->GetLocalBounds());` (the impl reaches `BrowserView` through `views().browser_view`; static_cast it, or add a `BrowserView*` to the layout's constructor if that member is a plain `views::View*`. Check `browser_view_layout.h` line 41 onward and pick the least invasive.)

- [ ] **Step 1: Write the controller**

```bash
cat > arcium/ui/browser/browser_sidebar_controller.h <<'EOF'
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_BROWSER_BROWSER_SIDEBAR_CONTROLLER_H_
#define ARCIUM_UI_BROWSER_BROWSER_SIDEBAR_CONTROLLER_H_

#include <memory>

#include "arcium/ui/browser/sidebar_tab_model.h"
#include "base/memory/raw_ptr.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"

class BrowserView;
struct BrowserLayoutParams;

namespace arcium {

class SidebarView;

// Owns the sidebar inside one BrowserView and answers the layout hooks.
// Created by BrowserView::InitViews when the sidebar feature is on.
class BrowserSidebarController {
 public:
  static std::unique_ptr<BrowserSidebarController> MaybeCreate(
      BrowserView* browser_view);

  BrowserSidebarController(const BrowserSidebarController&) = delete;
  BrowserSidebarController& operator=(const BrowserSidebarController&) = delete;
  ~BrowserSidebarController();

  SidebarView* view() { return view_; }
  int width() const;

  // Layout hooks. See the patch inventory in the Stage 1 plan.
  void AdjustLayoutParams(BrowserLayoutParams& params);
  void LayoutSidebar(const gfx::Rect& host_bounds);
  bool IsPositionInWindowCaption(const gfx::Point& point_in_browser_view) const;

  void ToggleVisibility();

 private:
  explicit BrowserSidebarController(BrowserView* browser_view);

  void UpdateContentCorners();
  void ExecuteCommand(int command_id);

  raw_ptr<BrowserView> browser_view_;
  std::unique_ptr<SidebarTabModel> model_;
  raw_ptr<SidebarView> view_ = nullptr;
  bool visible_ = true;
  int caption_button_width_ = 0;
};

}  // namespace arcium

#endif  // ARCIUM_UI_BROWSER_BROWSER_SIDEBAR_CONTROLLER_H_
EOF
cat > arcium/ui/browser/browser_sidebar_controller.cc <<'EOF'
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/browser_sidebar_controller.h"

#include "arcium/common/arcium_features.h"
#include "arcium/ui/sidebar/sidebar_metrics.h"
#include "arcium/ui/sidebar/sidebar_view.h"
#include "base/functional/bind.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/contents_container_view.h"
#include "chrome/browser/ui/views/frame/layout/browser_view_layout_params.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "ui/gfx/geometry/rounded_corners_f.h"

namespace arcium {

// static
std::unique_ptr<BrowserSidebarController> BrowserSidebarController::MaybeCreate(
    BrowserView* browser_view) {
  if (!features::IsSidebarEnabled() ||
      !browser_view->browser()->is_type_normal()) {
    return nullptr;
  }
  return base::WrapUnique(new BrowserSidebarController(browser_view));
}

BrowserSidebarController::BrowserSidebarController(BrowserView* browser_view)
    : browser_view_(browser_view),
      model_(std::make_unique<SidebarTabModel>(
          browser_view->browser()->tab_strip_model())) {
  SidebarView::Delegate delegate;
  delegate.toggle_sidebar = base::BindRepeating(
      &BrowserSidebarController::ToggleVisibility, base::Unretained(this));
  delegate.back = base::BindRepeating(&BrowserSidebarController::ExecuteCommand,
                                      base::Unretained(this), IDC_BACK);
  delegate.forward = base::BindRepeating(
      &BrowserSidebarController::ExecuteCommand, base::Unretained(this),
      IDC_FORWARD);
  delegate.reload = base::BindRepeating(
      &BrowserSidebarController::ExecuteCommand, base::Unretained(this),
      IDC_RELOAD);
  delegate.edit_url = base::BindRepeating(
      &BrowserSidebarController::ExecuteCommand, base::Unretained(this),
      IDC_FOCUS_LOCATION);
  view_ = browser_view_->AddChildView(
      std::make_unique<SidebarView>(model_.get(), std::move(delegate)));
}

BrowserSidebarController::~BrowserSidebarController() = default;

int BrowserSidebarController::width() const {
  return visible_ ? metrics::kSidebarWidth : 0;
}

void BrowserSidebarController::AdjustLayoutParams(BrowserLayoutParams& params) {
  // Remember the frame's caption-button area so the nav row leaves room for
  // the traffic lights, then take the sidebar column off the leading edge and
  // inset the rest so the page floats on the tinted frame.
  caption_button_width_ = static_cast<int>(
      params.leading_exclusion.ContentWithPadding().width());
  view_->SetCaptionButtonWidth(caption_button_width_);
  params.InsetHorizontal(width(), /*leading=*/true);
  params.leading_exclusion = BrowserLayoutExclusionArea();
  if (visible_) {
    params.Inset(gfx::Insets::TLBR(metrics::kContentInset, 0,
                                   metrics::kContentInset,
                                   metrics::kContentInset));
  }
}

void BrowserSidebarController::LayoutSidebar(const gfx::Rect& host_bounds) {
  view_->SetVisible(visible_);
  view_->SetBoundsRect(
      gfx::Rect(host_bounds.x(), host_bounds.y(), width(), host_bounds.height()));
  UpdateContentCorners();
}

void BrowserSidebarController::UpdateContentCorners() {
  ContentsContainerView* container =
      browser_view_->GetActiveContentsContainerView();
  if (!container) {
    return;
  }
  const float r = visible_ ? metrics::kContentCornerRadius : 0.f;
  container->SetBorderRoundedCornersFrom(gfx::RoundedCornersF(r, r, r, r));
}

bool BrowserSidebarController::IsPositionInWindowCaption(
    const gfx::Point& point_in_browser_view) const {
  if (!visible_ || !view_->bounds().Contains(point_in_browser_view)) {
    return false;
  }
  gfx::Point p = point_in_browser_view;
  views::View::ConvertPointToTarget(browser_view_, view_, &p);
  return view_->IsPositionInWindowCaption(p);
}

void BrowserSidebarController::ToggleVisibility() {
  visible_ = !visible_;
  browser_view_->InvalidateLayout();
}

void BrowserSidebarController::ExecuteCommand(int command_id) {
  chrome::ExecuteCommand(browser_view_->browser(), command_id);
}

}  // namespace arcium
EOF
python3 - <<'EOF'
p = 'arcium/ui/browser/BUILD.gn'
s = open(p).read()
s = s.replace('    "sidebar_tab_model.cc",\n', '    "browser_sidebar_controller.cc",\n    "browser_sidebar_controller.h",\n    "sidebar_tab_model.cc",\n')
s = s.replace('    "//chrome/browser/ui",\n', '    "//chrome/app:command_ids",\n    "//chrome/browser/ui",\n    "//chrome/browser/ui/views/frame/layout",\n')
open(p, 'w').write(s)
EOF
```
`BrowserView::GetActiveContentsContainerView()` exists (`browser_view.cc:1260`); `//chrome/browser/ui/views/frame/layout` may or may not be its own GN target, check `chrome/browser/ui/views/frame/layout/BUILD.gn` and drop the dep if the files belong to `//chrome/browser/ui`.

- [ ] **Step 2: Apply the six hooks and capture three patches**

```bash
cd /Volumes/Texternal/chromium/src
python3 - <<'EOF'
def edit(path, old, new, include=None):
    s = open(path).read()
    assert s.count(old) == 1, (path, old[:60])
    s = s.replace(old, new)
    if include and include not in s:
        i = s.index('#include '); j = s.index('\n', i) + 1
        s = s[:j] + include + '\n' + s[j:]
    open(path, 'w').write(s)

# 0050: browser_view.h / .cc
edit('chrome/browser/ui/views/frame/browser_view.h',
     '  raw_ptr<ToolbarView> toolbar_ = nullptr;\n',
     '  raw_ptr<ToolbarView> toolbar_ = nullptr;\n  // Arcium: sidebar-first layout. Null when the feature is off.\n  std::unique_ptr<arcium::BrowserSidebarController> arcium_sidebar_;\n',
     include='#include "arcium/ui/browser/browser_sidebar_controller.h"')
edit('chrome/browser/ui/views/frame/browser_view.h',
     '  LocationBarView* GetLocationBarView() const;\n',
     '  LocationBarView* GetLocationBarView() const;\n  arcium::BrowserSidebarController* arcium_sidebar() { return arcium_sidebar_.get(); }\n')
edit('chrome/browser/ui/views/frame/browser_view.cc',
     '  contents_container_ = AddChildView(std::move(contents_container));\n  set_contents_view(contents_container_);\n',
     '  contents_container_ = AddChildView(std::move(contents_container));\n  set_contents_view(contents_container_);\n  arcium_sidebar_ = arcium::BrowserSidebarController::MaybeCreate(this);\n')
edit('chrome/browser/ui/views/frame/browser_view.cc',
     'bool BrowserView::ShouldDrawTabStrip() const {\n',
     'bool BrowserView::ShouldDrawTabStrip() const {\n  if (arcium_sidebar_) {\n    return false;\n  }\n')
edit('chrome/browser/ui/views/frame/browser_view.cc',
     '    // The vertical tabstrip is not part of the overlay in immersive mode and\n    // must be tested separately.\n',
     '    if (arcium_sidebar_ && arcium_sidebar_->IsPositionInWindowCaption(point)) {\n      return HTCAPTION;\n    }\n    // The vertical tabstrip is not part of the overlay in immersive mode and\n    // must be tested separately.\n')

# 0060: layout delegate impl
edit('chrome/browser/ui/views/frame/layout/browser_view_layout_delegate_impl.cc',
     '    return params.InLocalCoordinates(\n        use_browser_bounds ? browser_bounds : params.visual_client_area);\n',
     '    BrowserLayoutParams result = params.InLocalCoordinates(\n        use_browser_bounds ? browser_bounds : params.visual_client_area);\n    if (auto* sidebar = browser_view_->arcium_sidebar()) {\n      sidebar->AdjustLayoutParams(result);\n    }\n    return result;\n')
edit('chrome/browser/ui/views/frame/layout/browser_view_layout_delegate_impl.cc',
     'bool BrowserViewLayoutDelegateImpl::IsToolbarVisible() const {\n',
     'bool BrowserViewLayoutDelegateImpl::IsToolbarVisible() const {\n  if (browser_view_->arcium_sidebar()) {\n    return false;\n  }\n')

# 0070: layout impl
edit('chrome/browser/ui/views/frame/layout/browser_view_layout_impl.cc',
     '  std::move(layout).ApplyLayout(host, [this](views::View* view, bool visible) {\n    SetViewVisibility(view, visible);\n  });\n',
     '  std::move(layout).ApplyLayout(host, [this](views::View* view, bool visible) {\n    SetViewVisibility(view, visible);\n  });\n  if (auto* sidebar = static_cast<BrowserView*>(host)->arcium_sidebar()) {\n    sidebar->LayoutSidebar(host->GetLocalBounds());\n  }\n',
     include='#include "chrome/browser/ui/views/frame/browser_view.h"')
EOF
git diff --stat
```
Expected: five files. The `static_cast<BrowserView*>(host)` in 0070 assumes the layout's host is the `BrowserView`; it is (`BrowserView` installs the layout on itself). If the file already includes `browser_view.h` transitively, the include line is harmless.

```bash
cd /Volumes/Texternal/repositories/arcium
S=/Volumes/Texternal/chromium/src
{ printf 'Seam: BrowserView::InitViews, ShouldDrawTabStrip, NonClientHitTest (mac).\nWhy: create the sidebar controller; hide the tab strip; drag the window from the sidebar.\nDelegates to: arcium/ui/browser/browser_sidebar_controller.cc\n\n'; git -C "$S" diff chrome/browser/ui/views/frame/browser_view.h chrome/browser/ui/views/frame/browser_view.cc; } > patches/0050-browser-view-sidebar.patch
{ printf 'Seam: BrowserViewLayoutDelegateImpl::GetBrowserLayoutParams, IsToolbarVisible.\nWhy: reserve the sidebar column from the visual client area; hide the toolbar.\nDelegates to: BrowserSidebarController::AdjustLayoutParams\n\n'; git -C "$S" diff chrome/browser/ui/views/frame/layout/browser_view_layout_delegate_impl.cc; } > patches/0060-layout-params-sidebar.patch
{ printf 'Seam: BrowserViewLayoutImpl::Layout after ApplyLayout.\nWhy: the sidebar is a BrowserView child the layout does not know; position it.\nDelegates to: BrowserSidebarController::LayoutSidebar\n\n'; git -C "$S" diff chrome/browser/ui/views/frame/layout/browser_view_layout_impl.cc; } > patches/0070-layout-sidebar-bounds.patch
git -C "$S" checkout -- chrome/browser/ui/views/frame/browser_view.h chrome/browser/ui/views/frame/browser_view.cc chrome/browser/ui/views/frame/layout/browser_view_layout_delegate_impl.cc chrome/browser/ui/views/frame/layout/browser_view_layout_impl.cc
scripts/sync
```
Expected: three `applied`, `failed=0`.

- [ ] **Step 3: Build and run**

```bash
scripts/build dev 2>&1 | tr '\r' '\n' | grep -E "error:|finished" | head -5
scripts/run
```
Expected on screen: no horizontal tab strip, no toolbar; the sidebar on the left with the tinted background and every zone; the page inset with rounded corners to the right; traffic lights sitting in the gap left by the nav row; live tab titles and favicons. Cmd+T still opens a plain new tab (Task 12 changes that). Cmd+L does nothing visible yet (Task 10). Dragging the empty sidebar area moves the window. Closing tabs from the rows works.

Known fallout to check and record: any `DCHECK` from `ToolbarView` while hidden; whether the bookmark bar and infobars appear in the reduced area (they should, below where the toolbar would be); fullscreen (Ctrl+Cmd+F) hides the sidebar together with the toolbar? If fullscreen breaks, make `AdjustLayoutParams` a no-op when `browser_view_->IsFullscreen()` and `LayoutSidebar` hide the view, and note it for Stage 5.

Perf check right away, since this is the first UI in the window: `scripts/perf --runs 3 --idle 30 --label stage1-task9` while the machine is quiet; process count must still equal the Stage 0 baseline (9).

- [ ] **Step 4: Commit**

```bash
git add arcium/ui/browser patches/0050-browser-view-sidebar.patch patches/0060-layout-params-sidebar.patch patches/0070-layout-sidebar-bounds.patch docs/perf
git commit -m "Sidebar in the window: controller, layout-params inset, hidden strip and toolbar

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 10: Spike, then host the real location bar in the URL pill

**Files:**
- Modify: `arcium/ui/browser/browser_sidebar_controller.cc/.h`
- Possibly modify: `patches/0050-browser-view-sidebar.patch` (one more hook if reparenting needs a post-init step)

**Interfaces:**
- After this task, `browser_view->GetLocationBarView()` is the view inside `UrlPillView`; Cmd+L (`IDC_FOCUS_LOCATION`) focuses it; the omnibox popup anchors below the pill.

- [ ] **Step 1: Spike (30 minutes, throwaway)**

In `BrowserSidebarController` add `void HostLocationBar()` called from a new hook at the end of `BrowserView::InitViews` (after `toolbar_->Init()`; find the call, it follows the `ToolbarView` construction). Implementation to try first:

```cpp
void BrowserSidebarController::HostLocationBar() {
  LocationBarView* bar = browser_view_->GetLocationBarView();
  if (!bar || !bar->parent()) {
    return;
  }
  views::View* toolbar = bar->parent();
  std::unique_ptr<views::View> owned = toolbar->RemoveChildViewT(bar);
  view_->url_pill()->SetHostedView(std::move(owned));
}
```
Build, run, and test: does the URL show in the pill; does typing and Enter navigate; does the suggestions popup open under the pill; do the security chip and page-action icons render; does Cmd+L focus it; any DCHECK on tab switch or window close.

Decide from the outcome:
- Works: keep it, delete nothing.
- Popup anchors to the wrong place: `OmniboxPopupView` anchors to the location bar's bounds in screen coordinates, so it should follow; if it clips, the pill's width is the fix (allow the popup to overhang via `LocationBarView::GetOmniboxPopupView` anchor width; note the file and line).
- DCHECK in `ToolbarView` layout or accessibility about a missing child: add a guard hook in that `ToolbarView` method (`if (!location_bar_view_->parent() == this) return;` style), record as a second small patch `0055-toolbar-hosted-location-bar.patch`.
- Fundamentally broken (the bar depends on `ToolbarView` as parent for delegate calls that crash): fall back. `UrlPillView` keeps the placeholder, shows `url_formatter::FormatUrlForSecurityDisplay(url)` of the active row, and `edit_url` opens the quick entry from Task 12 pre-filled with the current URL. Record the finding in `docs/stage1-findings.md`.

- [ ] **Step 2: Implement the chosen path, add the hook if used**

If reparenting works, add to `0050-browser-view-sidebar.patch` the call after toolbar init: locate `toolbar_->Init();` in `browser_view.cc` and append `if (arcium_sidebar_) { arcium_sidebar_->HostLocationBar(); }`. Regenerate the patch with the same procedure as Task 9 Step 2 (edit, diff, restore, sync).

- [ ] **Step 3: Verify with CDP and by hand**

Type `example.com` into the pill, Enter: the page loads and the pill shows `example.com`. Cmd+L selects the URL text. Open a second tab: the pill follows the active tab. The sidebar's `NavRowView` back button becomes enabled after the navigation (wire `SetBackEnabled` from `chrome::CanGoBack(browser)` inside `SidebarTabModel`'s notification path: call `view_->nav_row()->SetBackEnabled(...)` from a `BrowserSidebarController::OnSidebarModelChanged` observer; add the controller as a second observer of the model).

- [ ] **Step 4: Commit**

```bash
git add arcium/ui/browser patches docs
git commit -m "URL pill hosts the real location bar

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 11: Quick entry on Cmd+T

**Files:**
- Create: `arcium/ui/browser/quick_entry_bubble.h/.cc`; update `BUILD.gn`
- Modify: `browser_sidebar_controller.h/.cc` (`ShowQuickEntry()`)
- Create: `patches/0090-new-tab-quick-entry.patch`

**Interfaces:**
- `arcium::QuickEntryBubble::Show(BrowserView*, base::OnceCallback<void(const std::u16string&)> on_submit)`: a `views::BubbleDialogDelegateView` with no buttons, centred over the contents area, holding a `views::Textfield`. Enter runs `on_submit` with the text and closes; Esc closes.
- `BrowserSidebarController::ShowQuickEntry()` classifies the text with `AutocompleteClassifier` and opens the result with `chrome::AddSelectedTabWithURL(browser, url, ui::PAGE_TRANSITION_TYPED)`.
- Hook: `BrowserCommandController::ExecuteCommandWithDisposition`, `case IDC_NEW_TAB:` becomes `if (!arcium::HandleNewTabCommand(browser_)) { NewTab(...); }`.

- [ ] **Step 1: Write the bubble**

```bash
cat > arcium/ui/browser/quick_entry_bubble.h <<'EOF'
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARCIUM_UI_BROWSER_QUICK_ENTRY_BUBBLE_H_
#define ARCIUM_UI_BROWSER_QUICK_ENTRY_BUBBLE_H_

#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/textfield/textfield_controller.h"

class BrowserView;

namespace views {
class Textfield;
}

namespace arcium {

// Cmd+T: a floating text field over the page. Stage 1 has no suggestions.
class QuickEntryBubble : public views::BubbleDialogDelegateView,
                         public views::TextfieldController {
  METADATA_HEADER(QuickEntryBubble, views::BubbleDialogDelegateView)

 public:
  using SubmitCallback = base::OnceCallback<void(const std::u16string&)>;

  static void Show(BrowserView* browser_view, SubmitCallback on_submit);

  QuickEntryBubble(const QuickEntryBubble&) = delete;
  QuickEntryBubble& operator=(const QuickEntryBubble&) = delete;
  ~QuickEntryBubble() override;

  // views::TextfieldController:
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;

  // views::BubbleDialogDelegateView:
  gfx::Rect GetAnchorRect() const override;

 private:
  QuickEntryBubble(BrowserView* browser_view, SubmitCallback on_submit);

  raw_ptr<BrowserView> browser_view_;
  SubmitCallback on_submit_;
  raw_ptr<views::Textfield> field_ = nullptr;
};

}  // namespace arcium

#endif  // ARCIUM_UI_BROWSER_QUICK_ENTRY_BUBBLE_H_
EOF
cat > arcium/ui/browser/quick_entry_bubble.cc <<'EOF'
// Copyright 2026 The Arcium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arcium/ui/browser/quick_entry_bubble.h"

#include <memory>

#include "chrome/browser/ui/views/frame/browser_view.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/bubble/bubble_border.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/widget/widget.h"

namespace arcium {

namespace {
constexpr int kWidth = 560;
constexpr int kFieldHeight = 40;
}  // namespace

// static
void QuickEntryBubble::Show(BrowserView* browser_view, SubmitCallback on_submit) {
  auto bubble = base::WrapUnique(
      new QuickEntryBubble(browser_view, std::move(on_submit)));
  QuickEntryBubble* raw = bubble.get();
  views::BubbleDialogDelegateView::CreateBubble(std::move(bubble))->Show();
  raw->field_->RequestFocus();
}

QuickEntryBubble::QuickEntryBubble(BrowserView* browser_view,
                                   SubmitCallback on_submit)
    : views::BubbleDialogDelegateView(browser_view,
                                      views::BubbleBorder::NONE),
      browser_view_(browser_view),
      on_submit_(std::move(on_submit)) {
  SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  SetShowCloseButton(false);
  set_margins(gfx::Insets(12));
  set_close_on_deactivate(true);
  SetLayoutManager(std::make_unique<views::FillLayout>());
  field_ = AddChildView(std::make_unique<views::Textfield>());
  field_->set_controller(this);
  field_->SetPlaceholderText(u"Search or enter address");
  field_->SetPreferredSize(gfx::Size(kWidth, kFieldHeight));
}

QuickEntryBubble::~QuickEntryBubble() = default;

bool QuickEntryBubble::HandleKeyEvent(views::Textfield* sender,
                                      const ui::KeyEvent& key_event) {
  if (key_event.type() != ui::EventType::kKeyPressed) {
    return false;
  }
  if (key_event.key_code() == ui::VKEY_RETURN) {
    std::u16string text = field_->GetText();
    GetWidget()->Close();
    if (!text.empty() && on_submit_) {
      std::move(on_submit_).Run(text);
    }
    return true;
  }
  if (key_event.key_code() == ui::VKEY_ESCAPE) {
    GetWidget()->Close();
    return true;
  }
  return false;
}

gfx::Rect QuickEntryBubble::GetAnchorRect() const {
  // Centre horizontally over the contents area, a fifth of the way down.
  const gfx::Rect contents = browser_view_->GetActiveContentsContainerView()
                                 ->GetBoundsInScreen();
  const int x = contents.x() + (contents.width() - kWidth) / 2;
  const int y = contents.y() + contents.height() / 5;
  return gfx::Rect(x, y, kWidth, 1);
}

BEGIN_METADATA(QuickEntryBubble)
END_METADATA

}  // namespace arcium
EOF
```
If `BubbleDialogDelegateView` at this tag anchors only to a `views::View` and ignores `GetAnchorRect` overrides in the way used here, anchor to the contents container view instead and set `set_arrow(views::BubbleBorder::TOP_CENTER)` with `SetAnchorView(container)`; the visual difference is small.

- [ ] **Step 2: Controller entry point and the command hook**

Add to `BrowserSidebarController`:

```cpp
// In the header, public:
void ShowQuickEntry();
// A free function for the hook, declared in browser_sidebar_controller.h:
bool HandleNewTabCommand(Browser* browser);
```
```cpp
// In the .cc:
void BrowserSidebarController::ShowQuickEntry() {
  QuickEntryBubble::Show(
      browser_view_,
      base::BindOnce(
          [](BrowserView* bv, const std::u16string& text) {
            AutocompleteMatch match;
            AutocompleteClassifierFactory::GetForProfile(bv->GetProfile())
                ->Classify(text, false, false,
                           metrics::OmniboxEventProto::INVALID_SPEC, &match,
                           nullptr);
            if (match.destination_url.is_valid()) {
              chrome::AddSelectedTabWithURL(bv->browser(),
                                            match.destination_url,
                                            ui::PAGE_TRANSITION_TYPED);
            }
          },
          browser_view_));
}

bool HandleNewTabCommand(Browser* browser) {
  BrowserView* bv = BrowserView::GetBrowserViewForBrowser(browser);
  if (!bv || !bv->arcium_sidebar()) {
    return false;
  }
  bv->arcium_sidebar()->ShowQuickEntry();
  return true;
}
```
Includes: `chrome/browser/autocomplete/autocomplete_classifier_factory.h`, `components/omnibox/browser/autocomplete_classifier.h`, `components/omnibox/browser/autocomplete_match.h`, `chrome/browser/ui/browser_tabstrip.h`, `third_party/metrics_proto/omnibox_event.pb.h`. Add `quick_entry_bubble.*` to `BUILD.gn` with deps `//components/omnibox/browser`, `//third_party/metrics_proto`.

Hook:

```bash
cd /Volumes/Texternal/chromium/src
python3 - <<'EOF'
p = 'chrome/browser/ui/browser_command_controller.cc'
s = open(p).read()
old = '    case IDC_NEW_TAB: {\n      NewTab(browser_, NewTabTypes::kNewTabCommand);\n      break;\n    }\n'
assert s.count(old) == 1
s = s.replace(old, '    case IDC_NEW_TAB: {\n      if (!arcium::HandleNewTabCommand(browser_)) {\n        NewTab(browser_, NewTabTypes::kNewTabCommand);\n      }\n      break;\n    }\n')
i = s.index('#include '); j = s.index('\n', i) + 1
s = s[:j] + '#include "arcium/ui/browser/browser_sidebar_controller.h"\n' + s[j:]
open(p, 'w').write(s)
EOF
cd /Volumes/Texternal/repositories/arcium
{ printf 'Seam: BrowserCommandController::ExecuteCommandWithDisposition, IDC_NEW_TAB.\nWhy: Cmd+T opens the floating quick entry instead of a bare new tab.\nDelegates to: arcium::HandleNewTabCommand\n\n'; git -C /Volumes/Texternal/chromium/src diff chrome/browser/ui/browser_command_controller.cc; } > patches/0090-new-tab-quick-entry.patch
git -C /Volumes/Texternal/chromium/src checkout -- chrome/browser/ui/browser_command_controller.cc
scripts/sync
```
`browser_` in that class is a `Browser*`; `NewTab(...)` remains the fallback for popups and app windows where no sidebar exists.

- [ ] **Step 3: Build and verify**

Cmd+T shows the bubble centred over the page with focus in the field. Type `chromium.org` Enter: a new Today tab opens on it. Cmd+T, type `arc browser` Enter: a Google search tab (default engine per the Stage 0 decision). Esc closes. Clicking the page closes it. The "New tab" row in the sidebar still opens a plain new tab, which is the Zen behaviour.

- [ ] **Step 4: Commit**

```bash
git add arcium/ui/browser patches/0090-new-tab-quick-entry.patch
git commit -m "Cmd+T quick entry bubble

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 12: Daily-driver hardening

**Files:**
- Modify: `arcium/ui/browser/browser_sidebar_controller.cc`, `arcium/ui/sidebar/*.cc` as findings dictate
- Create: `docs/stage1-findings.md`

This task has no fixed code; it is a checklist executed against the real browser, with each item fixed in the smallest place. Each fix is its own commit.

- [ ] **Step 1: New window (Cmd+N).** Gets its own sidebar and model. Verify `BrowserView` destruction with the controller: close the window, no crash or `DCHECK`.
- [ ] **Step 2: Incognito window.** `is_type_normal()` is true for incognito; the sidebar appears with the incognito colour scheme (the mixer's `key.color_mode` handles dark). Acceptable for Stage 1.
- [ ] **Step 3: Popups and app windows.** No sidebar (`is_type_normal()` false); Chromium layout untouched.
- [ ] **Step 4: Fullscreen.** Ctrl+Cmd+F: the sidebar stays and the page fills the rest, or hides with the toolbar; either is fine if it is stable and returns cleanly. Video fullscreen from a page (YouTube) must cover the whole screen: verify the sidebar does not stay on top of it.
- [ ] **Step 5: Bookmark bar and infobars.** Cmd+Shift+B: the bar appears above the page inside the reduced area. Trigger an infobar (a site asking for location on `https://permission.site`): it appears above the page, not under the sidebar.
- [ ] **Step 6: Find in page (Cmd+F).** The find bar appears at the top of the page area.
- [ ] **Step 7: Downloads.** Downloading a file: the downloads bubble anchors sensibly (its anchor is the toolbar button, which is hidden; if the bubble appears at the window origin, anchor it to the nav row's reload button by hooking `DownloadBubbleUIController`'s anchor, or accept and log for Stage 6's library).
- [ ] **Step 8: Extensions.** Installed extension with a browser action: its icon lived in the hidden toolbar; the action still works via keyboard shortcut. Log "extension icons need a home in the sidebar" for Stage 6.
- [ ] **Step 9: Session restore.** Quit with five tabs, relaunch: the same five rows, same active row.
- [ ] **Step 10: 60 tabs.** Open sixty tabs (a loop over `scripts/run --new-tab`-style CDP `open`): the Today list scrolls or clips. Stage 1 requirement is that nothing breaks; wrap `today_` in a `views::ScrollView` if rows fall off the bottom, since a hidden row is unacceptable.
- [ ] **Step 11: Record findings** in `docs/stage1-findings.md` with the same table shape as `docs/stage0-carryover.md`, and commit after each fix.

---

### Task 13: Acceptance, measurements, docs, close-out

**Files:**
- Modify: `CLAUDE.md` (stage table, commands: playground and tests), `docs/superpowers/specs/2026-09-06-stage-1-visual-mvp-design.md` (deviations section)
- Create: `docs/perf/<date>-stage1.md` via the script, `docs/screens/stage1/README.md`

- [ ] **Step 1: Run the unit tests and the network audit**

```bash
/Volumes/Texternal/chromium/src/out/dev/arcium_unittests
scripts/netaudit 60
```
Expected: all tests pass; the audit shows the same four hosts as Stage 0 minus `accounts.google.com` and `csp.withgoogle.com` (sign-in is disabled). Update `docs/netaudit-findings.md` with the new run.

- [ ] **Step 2: Perf when the machine is quiet**

```bash
scripts/perf --runs 3 --idle 60 --label stage1
```
Compare against `docs/perf/2026-09-06-stage0-baseline.md`: process count equal; startup within noise; idle RSS within a few tens of MB. Write the comparison as a table at the bottom of the new file, with a one-line explanation of any delta.

- [ ] **Step 3: Acceptance A1.1 to A1.3**

- A1.1: use Arcium as the only browser for a working day; every tab operation from the sidebar. Note anything that forced a fallback to Chrome.
- A1.2: screenshot Arcium next to Zen with similar tabs into `docs/screens/stage1/acceptance-vs-zen.png`; list zone by zone that the order matches.
- A1.3: perf recorded; process count unchanged.

- [ ] **Step 4: Documentation**

In `CLAUDE.md`: Stage 1 row to `done`; add `out/dev/arcium_unittests` and `scripts/playground` notes to Commands; add a "Hooks" paragraph under Architecture rules listing the patch files and the rule that a new hook needs a header in the patch. In the Stage 1 spec, add a "Deviations" section mirroring this plan's deviations list plus whatever Task 10 decided. Commit.

- [ ] **Step 5: Push**

```bash
git push
```

---

## Self-review against the spec

| Requirement | Task |
|---|---|
| R1.1 tab strip and toolbar hidden | 9 (hooks 2 and 5) |
| R1.2 sidebar with every zone | 5, 6, 7 |
| R1.3 live titles, favicons, throbber, audio, active | 4 (model), 6 (row) |
| R1.4 click, hover close, Cmd+T adds, drag reorder | 6, 11 |
| R1.5 URL pill shows and edits the URL | 10 |
| R1.6 inset rounded content on gradient | 8, 9 (`AdjustLayoutParams` inset, `UpdateContentCorners`) |
| R1.7 one space, one profile, in-session favourites | 7 (space bar), favourites are section kFavorites which the browser model never produces in Stage 1: the grid is empty in the browser and populated in the playground. Acceptable per R1.7 "may hold only in-session items"; Stage 2 fills it |
| R1.8 shortcuts and session restore keep working | 12 |
| R1.9 every component in the playground | 5, 7, 8 |
| Spec 4.6 carry-over | 2, 3 |
| Spec 6 unit tests | 4; browser test deferred (deviation 2) |
| A1.1 to A1.3 | 13 |

Placeholder scan: the plan contains "check the tree" instructions where Chromium names moved recently (`GetTabAtIndex`, alert enums, icon symbol names, `BubbleDialogDelegateView` anchoring, the mac support function in examples). Each names the file to read and the fallback. No TBDs.

Type consistency: `SidebarModel` methods (`rows`, `ActivateTab`, `CloseTab`, `MoveTab`, `NewTab`, `ClearToday`) are used with the same names in Tasks 4, 5, 6, 7. `SidebarView::Delegate` fields (`toggle_sidebar`, `back`, `forward`, `reload`, `edit_url`) match between Task 7 and Task 9. `UrlPillView::SetHostedView` matches Tasks 7 and 10. `BrowserSidebarController` methods match the hooks in Task 9 and the uses in Tasks 10 and 11.
