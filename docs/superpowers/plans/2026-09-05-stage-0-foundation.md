# Stage 0: Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A branded, de-Googled Chromium 152 named Arcium that checks out, builds, runs, installs Web Store extensions, and can be measured and rebased with one script each.

**Architecture:** Chromium lives outside this repo at `/Volumes/Texternal/chromium/src`, pinned to a stable tag. Our repo holds scripts, GN arg files, a branding directory, and an (initially empty) patch series. Branding is done with zero patches by pointing Chromium's `branding_path_component` GN variable at a directory we symlink into the tree. Google services are removed by GN args (no API keys, no field-trial config, no remoting, no mDNS) and verified by a network audit rather than by code changes.

**Tech Stack:** depot_tools (fetch, gclient, gn, autoninja), bash, Python 3 standard library only, macOS `iconutil`, `osascript`.

**Spec:** `docs/superpowers/specs/2026-09-05-arcium-browser-design.md`, section 5 Stage 0, sections 2 (D1, D4, D6, D7), 4.1.

## Global Constraints

- Chromium checkout and all build output live on the external drive under `/Volumes/Texternal/chromium`. Never on the internal disk (59 GB free).
- Upstream tag for this stage: `152.0.7977.83` (Chromium stable, 2026-09-03). Recorded in `CHROMIUM_VERSION`.
- All Arcium code lives under `arcium/`. Upstream files are changed only via `patches/`. Stage 0 targets zero patches.
- No Swift, AppKit or platform-specific UI code.
- Scripts are bash with `set -euo pipefail`, or Python 3 standard library. No third-party dependencies.
- Perf numbers are recorded, not gated (spec section 3, provisional).
- The machine is shared and often loaded. Every long build runs under `caffeinate -i nice -n 10`, respects `ARCIUM_JOBS`, and logs to a file so it can run in the background.
- Commits are small, messages say why, and end with `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`.

## Time expectations

| Task | Wall time | Notes |
|---|---|---|
| 1 Bootstrap checkout | 1 to 2 h | ~40 GB download, network-bound. Run in background |
| 4 First build | 5 to 10 h | CPU-bound. Run in background, overnight if possible |
| 8 Views playground | 20 to 40 min | Mostly already built by Task 4 |
| Everything else | minutes | |

---

## File structure

| Path | Responsibility |
|---|---|
| `CHROMIUM_VERSION` | The single pinned upstream tag. Read by every script |
| `scripts/lib.sh` | Shared variables and helpers: paths, PATH for depot_tools, logging. Sourced by every script |
| `scripts/bootstrap` | One-time: clone depot_tools, fetch Chromium without history, check out the pinned tag, gclient sync |
| `scripts/sync` | Idempotent: ensure symlinks into the tree, apply any unapplied patches, exclude our symlinks from git status |
| `scripts/build` | `build <dev|perf|release> [target]`: install the GN args file, gn gen, autoninja with logging |
| `scripts/run` | Launch the dev build with a dedicated user-data-dir, passing through extra flags |
| `scripts/netaudit` | Run for N seconds with a net-log, print every host contacted, diff against the allowlist |
| `scripts/perf` | Startup time, idle RSS and process count over N runs; writes a markdown report |
| `scripts/rebase` | `rebase <tag>`: discard patch effects, move the checkout to the tag, gclient sync, reapply patches, report conflicts |
| `build/dev.gn` | Component build, DCHECKs on, minimal symbols. Daily driver |
| `build/perf.gn` | Non-component, optimised, no DCHECKs. For measurements |
| `build/release.gn` | Same as perf plus official-build optimisations. For shipping (Stage 8) |
| `arcium/branding/BRANDING` | Product names and bundle id, consumed by Chromium's build via `branding_path_component = "arcium"` |
| `arcium/branding/theme/` | Copy of `chrome/app/theme/chromium/` with our icon. Symlinked as `chrome/app/theme/arcium` |
| `arcium/branding/default_100_percent/`, `default_200_percent/` | Copies of the matching `chromium` dirs. Symlinked under the same names |
| `arcium/branding/tools/make_icon.py` | Generates the placeholder app icon PNG set and `.icns` with stdlib only |
| `docs/perf/2026-09-XX-stage0-baseline.md` | Baseline numbers written by `scripts/perf` |
| `patches/` | Empty this stage except `README.md` |

---

### Task 1: Pin the version and bootstrap the Chromium checkout

**Files:**
- Create: `CHROMIUM_VERSION`
- Create: `scripts/lib.sh`
- Create: `scripts/bootstrap`

**Interfaces:**
- Produces: `scripts/lib.sh` exporting `ARCIUM_ROOT`, `CHROMIUM_ROOT`, `SRC`, `DEPOT_TOOLS`, `CHROMIUM_TAG`, and functions `log`, `die`, `need_src`. Every later script starts with `source "$(dirname "$0")/lib.sh"`.
- Produces: a Chromium checkout at `$SRC` on tag `152.0.7977.83` with DEPS synced and hooks run.

- [ ] **Step 1: Write the version file and the shared library**

```bash
cd /Volumes/Texternal/repositories/arcium
printf '152.0.7977.83\n' > CHROMIUM_VERSION
cat > scripts/lib.sh <<'EOF'
# Shared by every script in scripts/. Source, do not execute.
set -euo pipefail

ARCIUM_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CHROMIUM_ROOT="${CHROMIUM_ROOT:-/Volumes/Texternal/chromium}"
SRC="$CHROMIUM_ROOT/src"
DEPOT_TOOLS="$CHROMIUM_ROOT/depot_tools"
CHROMIUM_TAG="$(tr -d '[:space:]' < "$ARCIUM_ROOT/CHROMIUM_VERSION")"
ARCIUM_JOBS="${ARCIUM_JOBS:-$(sysctl -n hw.ncpu)}"

export PATH="$DEPOT_TOOLS:$PATH"

log() { printf '\033[1;34m[arcium]\033[0m %s\n' "$*" >&2; }
die() { printf '\033[1;31m[arcium] error:\033[0m %s\n' "$*" >&2; exit 1; }

need_src() {
  [ -d "$SRC/.git" ] || die "no Chromium checkout at $SRC; run scripts/bootstrap first"
}

# Every symlink we place inside the Chromium tree: "<path inside src> <target relative to that path>"
ARCIUM_SYMLINKS=(
  "arcium $ARCIUM_ROOT/arcium"
  "chrome/app/theme/arcium ../../../arcium/branding/theme"
  "chrome/app/theme/default_100_percent/arcium ../../../../arcium/branding/default_100_percent"
  "chrome/app/theme/default_200_percent/arcium ../../../../arcium/branding/default_200_percent"
)
EOF
```

Note on symlink targets: the `arcium` link is absolute (it points at this repo, wherever it is), the three branding links are relative because they point back into `src/arcium`, which is itself the first link. If the repo moves, re-run `scripts/sync` after deleting the old `src/arcium` link.

- [ ] **Step 2: Write the bootstrap script**

```bash
cat > scripts/bootstrap <<'EOF'
#!/usr/bin/env bash
# One-time: depot_tools + shallow Chromium checkout at the pinned tag.
# Safe to re-run; each phase is skipped when already done.
source "$(dirname "$0")/lib.sh"

mkdir -p "$CHROMIUM_ROOT"
cd "$CHROMIUM_ROOT"

if [ ! -d "$DEPOT_TOOLS/.git" ]; then
  log "cloning depot_tools"
  git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git "$DEPOT_TOOLS"
fi

if [ ! -d "$SRC/.git" ]; then
  log "fetching chromium without history (this downloads ~40 GB)"
  caffeinate -i fetch --no-history chromium
fi

cd "$SRC"
if ! git rev-parse -q --verify "refs/tags/$CHROMIUM_TAG" >/dev/null; then
  log "fetching tag $CHROMIUM_TAG"
  git fetch --depth 1 origin "+refs/tags/$CHROMIUM_TAG:refs/tags/$CHROMIUM_TAG"
fi

if [ "$(git describe --tags --exact-match 2>/dev/null || true)" != "$CHROMIUM_TAG" ]; then
  log "checking out $CHROMIUM_TAG"
  git checkout -q "tags/$CHROMIUM_TAG"
fi

log "syncing DEPS and running hooks"
cd "$CHROMIUM_ROOT"
caffeinate -i gclient sync -D --no-history

log "bootstrap complete: $(grep -E 'MAJOR|MINOR|BUILD|PATCH' "$SRC/chrome/VERSION" | tr '\n' ' ')"
EOF
chmod +x scripts/bootstrap
```

- [ ] **Step 3: Run the bootstrap in the background with a log**

```bash
mkdir -p /Volumes/Texternal/chromium
nohup scripts/bootstrap > /Volumes/Texternal/chromium/bootstrap.log 2>&1 &
echo "pid $!"
```

Check progress with `tail -f /Volumes/Texternal/chromium/bootstrap.log`. Expect 1 to 2 hours. If `fetch` fails mid-way on network, re-run `scripts/bootstrap`; `fetch` and `gclient` resume.

- [ ] **Step 4: Verify the checkout is on the pinned tag**

Run:
```bash
source scripts/lib.sh && cd "$SRC" && git describe --tags --exact-match && cat chrome/VERSION
```
Expected:
```
152.0.7977.83
MAJOR=152
MINOR=0
BUILD=7977
PATCH=83
```

- [ ] **Step 5: Verify depot_tools is usable from the library**

Run: `source scripts/lib.sh && which gn autoninja gclient`
Expected: three paths, all under `/Volumes/Texternal/chromium/depot_tools`.

- [ ] **Step 6: Commit**

```bash
git add CHROMIUM_VERSION scripts/lib.sh scripts/bootstrap
git commit -m "Pin Chromium 152.0.7977.83 and add bootstrap script

Shallow checkout on the external drive; the internal disk is too small.

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 2: Idempotent sync with symlinks and patch application

**Files:**
- Create: `scripts/sync`
- Create: `scripts/apply-patches` (sourced by both `sync` and `rebase`)

**Interfaces:**
- Consumes: `ARCIUM_SYMLINKS`, `SRC`, `ARCIUM_ROOT` from `lib.sh`.
- Produces: `scripts/apply-patches` defining `ensure_symlinks` and `apply_patches`, the latter printing `applied=N skipped=N failed=N` and returning non-zero on any failure. `scripts/rebase` (Task 7) reuses both.

- [ ] **Step 1: Write the helper with the two functions**

```bash
cat > scripts/apply-patches <<'EOF'
# Sourced by scripts/sync and scripts/rebase. Requires lib.sh already sourced.

# Place every symlink from ARCIUM_SYMLINKS and hide them from `git status` in the checkout.
ensure_symlinks() {
  local exclude="$SRC/.git/info/exclude"
  local entry link target
  for entry in "${ARCIUM_SYMLINKS[@]}"; do
    link="${entry% *}"
    target="${entry#* }"
    mkdir -p "$(dirname "$SRC/$link")"
    if [ -L "$SRC/$link" ] && [ "$(readlink "$SRC/$link")" = "$target" ]; then
      continue
    fi
    if [ -L "$SRC/$link" ]; then
      rm "$SRC/$link"           # our link, stale target: replace it
    elif [ -e "$SRC/$link" ]; then
      die "$SRC/$link exists and is not a symlink; refusing to overwrite"
    fi
    ln -s "$target" "$SRC/$link"
    log "linked $link -> $target"
    grep -qxF "/$link" "$exclude" 2>/dev/null || printf '/%s\n' "$link" >> "$exclude"
  done
  for entry in "${ARCIUM_SYMLINKS[@]}"; do
    link="${entry% *}"
    [ -e "$SRC/$link/" ] || die "symlink $link is dangling (target missing)"
  done
}

# Apply patches/*.patch in lexical order. Already-applied patches are skipped.
apply_patches() {
  local applied=0 skipped=0 failed=0 p name
  shopt -s nullglob
  for p in "$ARCIUM_ROOT"/patches/*.patch; do
    name="$(basename "$p")"
    if git -C "$SRC" apply --reverse --check "$p" 2>/dev/null; then
      skipped=$((skipped + 1))
      continue
    fi
    if git -C "$SRC" apply --check "$p" 2>/dev/null; then
      git -C "$SRC" apply "$p"
      log "applied $name"
      applied=$((applied + 1))
    else
      log "CONFLICT $name"
      git -C "$SRC" apply --check "$p" 2>&1 | sed 's/^/    /' >&2 || true
      failed=$((failed + 1))
    fi
  done
  shopt -u nullglob
  log "patches: applied=$applied skipped=$skipped failed=$failed"
  [ "$failed" -eq 0 ]
}
EOF
```

- [ ] **Step 2: Write the sync script**

```bash
cat > scripts/sync <<'EOF'
#!/usr/bin/env bash
# Idempotent: make the Chromium tree ready to build Arcium at the pinned tag.
source "$(dirname "$0")/lib.sh"
source "$ARCIUM_ROOT/scripts/apply-patches"
need_src

actual="$(git -C "$SRC" describe --tags --exact-match 2>/dev/null || echo none)"
[ "$actual" = "$CHROMIUM_TAG" ] || die "checkout is at $actual, expected $CHROMIUM_TAG; run scripts/rebase $CHROMIUM_TAG"

ensure_symlinks
apply_patches
log "sync complete"
EOF
chmod +x scripts/sync
```

- [ ] **Step 3: Create the branding directories the symlinks point at, so they are not dangling**

The contents are filled in Task 3. For now the directories must exist.

```bash
mkdir -p arcium/branding/theme arcium/branding/default_100_percent arcium/branding/default_200_percent
touch arcium/branding/theme/.gitkeep arcium/branding/default_100_percent/.gitkeep arcium/branding/default_200_percent/.gitkeep
```

- [ ] **Step 4: Run sync twice and verify it is idempotent**

Run: `scripts/sync && scripts/sync`
Expected: first run prints four `linked ...` lines and `patches: applied=0 skipped=0 failed=0`; second run prints only the patches line and `sync complete`.

- [ ] **Step 5: Verify the checkout's git status is clean despite the symlinks**

Run: `source scripts/lib.sh && git -C "$SRC" status --porcelain | head`
Expected: no output.

- [ ] **Step 6: Verify the symlinks resolve**

Run: `source scripts/lib.sh && ls "$SRC/arcium/" && ls -la "$SRC/chrome/app/theme/arcium"`
Expected: the listing of our `arcium/` directory (browser, ui, webui, common, test, branding), and a symlink line for the theme.

- [ ] **Step 7: Commit**

```bash
git add scripts/sync scripts/apply-patches arcium/branding
git commit -m "Add idempotent sync: symlinks into the Chromium tree and patch application

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 3: Arcium branding directory, zero patches

**Files:**
- Create: `arcium/branding/BRANDING` and the three theme directories, copied from upstream
- Create: `arcium/branding/tools/make_icon.py`

**Interfaces:**
- Consumes: the symlinks from Task 2.
- Produces: `chrome/app/theme/arcium/BRANDING` (via symlink) with `PRODUCT_FULLNAME=Arcium`, `MAC_BUNDLE_ID=org.arcium.browser`, and `chrome/app/theme/arcium/mac/app.icns` as our icon. Task 4's GN args select it with `branding_path_component = "arcium"`.

Background: Chromium's `build/config/chrome_build.gni` defines `branding_path_component` (default `"chromium"`) and `branding_file_path = "//chrome/app/theme/$branding_path_component/BRANDING"`. Icons and product logos are looked up under `chrome/app/theme/$branding_path_component/`, `chrome/app/theme/default_100_percent/$branding_path_component/` and `default_200_percent/`. Providing an `arcium` directory in each place brands the product without touching an upstream file.

- [ ] **Step 1: Copy the upstream chromium branding directories into ours**

```bash
source scripts/lib.sh
rm -f arcium/branding/*/.gitkeep
cp -R "$SRC/chrome/app/theme/chromium/." arcium/branding/theme/
cp -R "$SRC/chrome/app/theme/default_100_percent/chromium/." arcium/branding/default_100_percent/
cp -R "$SRC/chrome/app/theme/default_200_percent/chromium/." arcium/branding/default_200_percent/
ls arcium/branding/theme
```
Expected: `BRANDING`, `mac/`, `linux/`, `win/`, several `product_logo_*.png`. If a `BUILD.gn` is present, keep it; GN references into the branding dir keep working because the directory shape is identical.

- [ ] **Step 2: Rewrite BRANDING for Arcium**

```bash
cat > arcium/branding/theme/BRANDING <<'EOF'
COMPANY_FULLNAME=The Arcium Authors
COMPANY_SHORTNAME=Arcium
PRODUCT_FULLNAME=Arcium
PRODUCT_SHORTNAME=Arcium
PRODUCT_INSTALLER_FULLNAME=Arcium Installer
PRODUCT_INSTALLER_SHORTNAME=Arcium Installer
COPYRIGHT=Copyright @LASTCHANGE_YEAR@ The Arcium Authors. All rights reserved.
MAC_BUNDLE_ID=org.arcium.browser
MAC_CREATOR_CODE=Arcm
MAC_TEAM_ID=
EOF
ln -sf theme/BRANDING arcium/branding/BRANDING
```

The bundle id `org.arcium.browser` is a placeholder until a real organisation domain is chosen; changing it later is a one-line edit and a rebuild of the app bundle only.

- [ ] **Step 3: Write the placeholder icon generator (stdlib only)**

A flat vertical gradient rounded square is enough to tell Arcium apart from Chromium in the Dock. Real artwork replaces the PNGs later without touching the pipeline.

```bash
mkdir -p arcium/branding/tools
cat > arcium/branding/tools/make_icon.py <<'EOF'
#!/usr/bin/env python3
"""Generate Arcium's placeholder app icon as an .icns using only the standard library.

Usage: make_icon.py OUT_DIR   -> writes OUT_DIR/app.icns and OUT_DIR/product_logo_{16,32,48,128,256}.png
"""
import os, struct, subprocess, sys, tempfile, zlib

TOP = (0x5B, 0x6C, 0xFF)      # indigo
BOTTOM = (0xC0, 0x4F, 0xE0)   # violet


def png(width, height, rows):
    def chunk(tag, data):
        c = struct.pack('>I', len(data)) + tag + data
        return c + struct.pack('>I', zlib.crc32(tag + data) & 0xFFFFFFFF)
    raw = b''.join(b'\x00' + r for r in rows)
    return (b'\x89PNG\r\n\x1a\n'
            + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(raw, 9))
            + chunk(b'IEND', b''))


def icon(size):
    radius = size * 0.22
    inset = size * 0.08
    rows = []
    for y in range(size):
        t = y / max(size - 1, 1)
        r = int(TOP[0] + (BOTTOM[0] - TOP[0]) * t)
        g = int(TOP[1] + (BOTTOM[1] - TOP[1]) * t)
        b = int(TOP[2] + (BOTTOM[2] - TOP[2]) * t)
        row = bytearray()
        for x in range(size):
            px, py = x + 0.5, y + 0.5
            lo, hi = inset, size - inset
            cx = min(max(px, lo + radius), hi - radius)
            cy = min(max(py, lo + radius), hi - radius)
            inside = (lo <= px <= hi and lo <= py <= hi
                      and (px - cx) ** 2 + (py - cy) ** 2 <= radius ** 2)
            row += bytes((r, g, b, 255 if inside else 0))
        rows.append(bytes(row))
    return png(size, size, rows)


def main(out_dir):
    os.makedirs(out_dir, exist_ok=True)
    for s in (16, 32, 48, 128, 256):
        with open(os.path.join(out_dir, f'product_logo_{s}.png'), 'wb') as f:
            f.write(icon(s))
    with tempfile.TemporaryDirectory() as tmp:
        iconset = os.path.join(tmp, 'app.iconset')
        os.mkdir(iconset)
        for s in (16, 32, 128, 256, 512):
            with open(os.path.join(iconset, f'icon_{s}x{s}.png'), 'wb') as f:
                f.write(icon(s))
            with open(os.path.join(iconset, f'icon_{s}x{s}@2x.png'), 'wb') as f:
                f.write(icon(s * 2))
        subprocess.run(['iconutil', '-c', 'icns', iconset, '-o',
                        os.path.join(out_dir, 'app.icns')], check=True)
    print('wrote icons to', out_dir)


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else '.')
EOF
chmod +x arcium/branding/tools/make_icon.py
```

- [ ] **Step 4: Generate the icon into a scratch dir and verify it is a valid icns**

```bash
python3 arcium/branding/tools/make_icon.py /private/tmp/claude-501/-Volumes-Texternal-repositories-arcium/a7e02089-c144-4b33-aa52-dffb6d4781a3/scratchpad/icon
file /private/tmp/claude-501/-Volumes-Texternal-repositories-arcium/a7e02089-c144-4b33-aa52-dffb6d4781a3/scratchpad/icon/app.icns
```
Expected: `Mac OS X icon, ... bytes, "icns" type`. The script may take up to a minute at 1024 px because it is pure Python; that is acceptable for a one-off.

- [ ] **Step 5: Install the icon and logos into the branding directories**

```bash
S=/private/tmp/claude-501/-Volumes-Texternal-repositories-arcium/a7e02089-c144-4b33-aa52-dffb6d4781a3/scratchpad/icon
cp "$S/app.icns" arcium/branding/theme/mac/app.icns
for s in 16 32 48 128 256; do
  [ -f "arcium/branding/theme/product_logo_$s.png" ] && cp "$S/product_logo_$s.png" "arcium/branding/theme/product_logo_$s.png"
done
git status --short arcium/branding | head -20
```
Expected: modified `app.icns` and the `product_logo_*.png` files that existed upstream. Leave any other upstream PNGs (for example `product_logo_name_*.png`, document icons) as they are for this stage.

- [ ] **Step 6: Verify the symlinked view from inside the Chromium tree**

Run: `source scripts/lib.sh && cat "$SRC/chrome/app/theme/arcium/BRANDING" | head -3 && file "$SRC/chrome/app/theme/arcium/mac/app.icns"`
Expected: `COMPANY_FULLNAME=The Arcium Authors` ... and the icns file description.

- [ ] **Step 7: Commit**

```bash
git add arcium/branding
git commit -m "Add Arcium branding directory selected via branding_path_component

Copies Chromium's branding tree and replaces names, bundle id and the app
icon. No upstream file is patched.

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 4: GN arg files, build script, first build

**Files:**
- Create: `build/dev.gn`, `build/perf.gn`, `build/release.gn`
- Create: `scripts/build`

**Interfaces:**
- Consumes: branding directory from Task 3, symlinks from Task 2.
- Produces: `scripts/build <config> [target]` and the app bundle at `$SRC/out/<config>/Arcium.app`. `scripts/run`, `scripts/perf` and `scripts/netaudit` use `$SRC/out/dev/Arcium.app/Contents/MacOS/Arcium`.

- [ ] **Step 1: Write the three GN arg files**

The Google-services block implements spec decision D6: no API keys disables Google sign-in and Sync by construction (Chromium's `docs/api_keys.md`); `disable_fieldtrial_testing_config` removes the baked-in Finch config, and non-branded builds never fetch a variations seed; remoting, mDNS and service discovery are the background services with no place in Arcium. Safe Browsing stays available (`safe_browsing_mode = 1`) as a user toggle.

```bash
cat > build/common.gni <<'EOF'
# Shared by every Arcium build config. scripts/build concatenates this with <config>.gn.

# Branding: chrome/app/theme/arcium is a symlink into arcium/branding (see scripts/sync).
branding_path_component = "arcium"

# macOS toolchain: system Xcode, Apple linker on arm64 (per docs/mac_build_instructions.md).
use_remoteexec = false
use_lld = false
treat_warnings_as_errors = false

# Spec D6: remove Google account services and background Google services.
use_official_google_api_keys = false
google_api_key = ""
google_default_client_id = ""
google_default_client_secret = ""
disable_fieldtrial_testing_config = true
enable_remoting = false
enable_mdns = false
enable_service_discovery = false
enable_hangout_services_extension = false
# Keep Safe Browsing available as a user toggle.
safe_browsing_mode = 1

# Full media support: H.264/AAC and Widevine, like Chrome.
proprietary_codecs = true
ffmpeg_branding = "Chrome"
enable_widevine = true
EOF

cat > build/dev.gn <<'EOF'
# Daily development build: fast incremental links, DCHECKs on, function-level symbols.
is_debug = false
is_component_build = true
dcheck_always_on = true
symbol_level = 1
blink_symbol_level = 0
v8_symbol_level = 0
enable_dsyms = false
enable_stripping = false
EOF

cat > build/perf.gn <<'EOF'
# Measurement build: what a user would run, minus official-build-only optimisations.
is_debug = false
is_component_build = false
dcheck_always_on = false
symbol_level = 0
blink_symbol_level = 0
v8_symbol_level = 0
enable_dsyms = false
enable_stripping = true
EOF

cat > build/release.gn <<'EOF'
# Shipping build (Stage 8). Official build enables LTO and other optimisations; no PGO profile download.
is_debug = false
is_official_build = true
chrome_pgo_phase = 0
is_component_build = false
dcheck_always_on = false
symbol_level = 1
blink_symbol_level = 0
v8_symbol_level = 0
enable_dsyms = true
enable_stripping = true
EOF
```

- [ ] **Step 2: Write the build script**

```bash
cat > scripts/build <<'EOF'
#!/usr/bin/env bash
# Usage: scripts/build <dev|perf|release> [ninja target, default chrome]
# Writes out/<config>/args.gn from build/common.gni + build/<config>.gn, runs gn gen, then autoninja.
# Honors ARCIUM_JOBS. Logs to out/<config>/build.log. Runs under caffeinate and nice.
source "$(dirname "$0")/lib.sh"
need_src

config="${1:-}"
target="${2:-chrome}"
[ -f "$ARCIUM_ROOT/build/$config.gn" ] || die "usage: scripts/build <dev|perf|release> [target]"

out="$SRC/out/$config"
mkdir -p "$out"
{
  echo "# Generated by scripts/build from build/common.gni and build/$config.gn. Edit those, not this."
  cat "$ARCIUM_ROOT/build/common.gni"
  echo
  cat "$ARCIUM_ROOT/build/$config.gn"
} > "$out/args.gn.new"

if ! cmp -s "$out/args.gn.new" "$out/args.gn" 2>/dev/null; then
  mv "$out/args.gn.new" "$out/args.gn"
  log "args.gn changed; running gn gen"
  (cd "$SRC" && gn gen "out/$config")
else
  rm -f "$out/args.gn.new"
fi

log "building $target in out/$config with $ARCIUM_JOBS jobs (log: $out/build.log)"
start=$(date +%s)
(cd "$SRC" && caffeinate -i nice -n 10 autoninja -C "out/$config" -j "$ARCIUM_JOBS" "$target") 2>&1 | tee "$out/build.log"
log "done in $(( ($(date +%s) - start) / 60 )) min"
EOF
chmod +x scripts/build
```

- [ ] **Step 3: Run gn gen alone first, to surface unknown args before a multi-hour build**

```bash
source scripts/lib.sh
mkdir -p "$SRC/out/dev"
{ cat build/common.gni; echo; cat build/dev.gn; } > "$SRC/out/dev/args.gn"
(cd "$SRC" && gn gen out/dev 2>&1 | tail -20)
```
Expected: `Done. Made NNNNN targets from NNNN files in NNNNms.`

If GN reports an argument that does not exist at this tag (most likely candidate: `enable_hangout_services_extension`, which upstream has been removing), delete that line from `build/common.gni`, note it in the commit message, and re-run. Do not add `--args` on the command line; the files are the source of truth.

- [ ] **Step 4: Confirm GN picked up the branding**

```bash
source scripts/lib.sh && (cd "$SRC" && gn args out/dev --list=branding_path_component --short && gn args out/dev --list=branding_file_path --short)
```
Expected:
```
branding_path_component = "arcium"
branding_file_path = "//chrome/app/theme/arcium/BRANDING"
```

- [ ] **Step 5: Start the first build in the background**

```bash
nohup scripts/build dev > /Volumes/Texternal/chromium/first-build.log 2>&1 &
echo "pid $!"
```
Expect 5 to 10 hours on this machine. Watch with `tail -f /Volumes/Texternal/chromium/first-build.log`. If the machine is needed for other work, lower parallelism for later invocations with `ARCIUM_JOBS=6 scripts/build dev`; ninja resumes where it stopped.

If the build fails on a compiler error inside upstream code, the cause is almost always Xcode 26.6 being newer than the SDK Chromium's bots use (26.5). `treat_warnings_as_errors = false` handles the warning cases. For a hard error, record the file and message, and stop for review rather than patching upstream ad hoc.

- [ ] **Step 6: Verify the bundle exists and carries our branding**

```bash
source scripts/lib.sh
ls "$SRC/out/dev/Arcium.app/Contents/MacOS/"
/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' -c 'Print :CFBundleName' "$SRC/out/dev/Arcium.app/Contents/Info.plist"
```
Expected: `Arcium` executable; `org.arcium.browser` and `Arcium`.

- [ ] **Step 7: Commit**

```bash
git add build scripts/build
git commit -m "Add GN configs (dev, perf, release) and build script

common.gni carries branding and the spec D6 Google-services removal;
per-config files carry only optimisation and symbol choices.

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 5: Run script, smoke test, Web Store, network audit

**Files:**
- Create: `scripts/run`
- Create: `scripts/netaudit`
- Create: `docs/netaudit-allowlist.txt`

**Interfaces:**
- Consumes: `$SRC/out/dev/Arcium.app` from Task 4.
- Produces: `scripts/run [flags...]` and `scripts/netaudit [seconds]` printing `unexpected hosts: N`. `scripts/perf` (Task 6) reuses the binary path convention `$SRC/out/${ARCIUM_CONFIG:-dev}/Arcium.app/Contents/MacOS/Arcium`.

- [ ] **Step 1: Write the run script**

```bash
cat > scripts/run <<'EOF'
#!/usr/bin/env bash
# Launch Arcium from out/<ARCIUM_CONFIG, default dev> with its own user-data-dir.
# Extra arguments are passed to the browser, e.g. scripts/run --incognito https://example.com
source "$(dirname "$0")/lib.sh"
need_src
config="${ARCIUM_CONFIG:-dev}"
bin="$SRC/out/$config/Arcium.app/Contents/MacOS/Arcium"
[ -x "$bin" ] || die "no binary at $bin; run scripts/build $config"
data="${ARCIUM_USER_DATA_DIR:-$HOME/Library/Application Support/Arcium-$config}"
exec "$bin" --user-data-dir="$data" --no-first-run --no-default-browser-check "$@"
EOF
chmod +x scripts/run
```

- [ ] **Step 2: Launch and run the manual smoke checklist (spec A0.1)**

Run: `scripts/run https://www.youtube.com` and check each item by hand:

1. Window opens, page renders, tabs open and close.
2. A YouTube video plays with sound (proves `proprietary_codecs` and `ffmpeg_branding`).
3. Open `chrome://version`: Executable path ends in `Arcium.app/Contents/MacOS/Arcium`; Profile path is under `Application Support/Arcium-dev`.
4. DevTools opens with Cmd+Option+I.
5. Open `https://chromewebstore.google.com`, pick uBlock Origin Lite, click "Add to Arcium" (the store shows our product name). Extension installs and its icon appears in the toolbar. If the store refuses with "This browser is not supported", record the exact message: that is the known risk from spec section 6 and needs a user-agent decision before Stage 1.
6. `chrome://settings`: there is no "Sign in" or "Sync" entry at the top of the People or "You and Google" section, or the section is absent.
7. Netflix or another Widevine site plays, or reports a licensing error rather than a missing-plugin error (Widevine is present; a licensing error is acceptable for an unsigned dev build).

Record results in `docs/perf/2026-09-XX-stage0-baseline.md` under a "Smoke" heading (the file is created in Task 6; append then).

- [ ] **Step 3: Write the network audit script**

```bash
cat > scripts/netaudit <<'EOF'
#!/usr/bin/env bash
# Run Arcium with a fresh profile for N seconds (default 60), record a net-log, list contacted hosts,
# and diff against docs/netaudit-allowlist.txt. Exit code 1 if unexpected hosts were contacted.
source "$(dirname "$0")/lib.sh"
need_src
seconds="${1:-60}"
config="${ARCIUM_CONFIG:-dev}"
bin="$SRC/out/$config/Arcium.app/Contents/MacOS/Arcium"
[ -x "$bin" ] || die "no binary at $bin"

work="$(mktemp -d /tmp/arcium-netaudit.XXXXXX)"
netlog="$work/netlog.json"
log "running $seconds s with fresh profile; net-log at $netlog"
"$bin" --user-data-dir="$work/profile" --no-first-run --no-default-browser-check \
  --log-net-log="$netlog" --net-log-capture-mode=Default "about:blank" >/dev/null 2>&1 &
pid=$!
sleep "$seconds"
osascript -e 'tell application "Arcium" to quit' >/dev/null 2>&1 || kill "$pid"
for _ in $(seq 1 30); do kill -0 "$pid" 2>/dev/null || break; sleep 1; done

# Extract hosts with a regex so a truncated log still parses.
python3 - "$netlog" "$ARCIUM_ROOT/docs/netaudit-allowlist.txt" <<'PY'
import re, sys
text = open(sys.argv[1], errors="replace").read()
hosts = sorted(set(m.lower() for m in re.findall(r'"url":"(?:https?|wss?)://([^/":\\]+)', text)))
allow = {l.strip() for l in open(sys.argv[2]) if l.strip() and not l.startswith("#")}
def allowed(h): return any(h == a or h.endswith("." + a) for a in allow)
unexpected = [h for h in hosts if not allowed(h)]
print("contacted hosts:")
for h in hosts: print("  " + ("   " if allowed(h) else " ! ") + h)
print(f"unexpected hosts: {len(unexpected)}")
sys.exit(1 if unexpected else 0)
PY
EOF
chmod +x scripts/netaudit
```

- [ ] **Step 4: Write the allowlist**

These are the hosts spec D6 explicitly keeps: component updater (Widevine, certificate lists, CRL sets), Safe Browsing, and the Web Store. Everything else is a finding.

```bash
cat > docs/netaudit-allowlist.txt <<'EOF'
# Hosts Arcium is allowed to contact at idle with a fresh profile (spec D6).
# Subdomains of a listed host are allowed.
# Component updater (Widevine, CRLSet, certificate transparency, etc.)
update.googleapis.com
clients2.google.com
edgedl.me.gvt1.com
dl.google.com
# Safe Browsing (user toggle, on by default)
safebrowsing.googleapis.com
# Chrome Web Store, only when the user visits it
chromewebstore.google.com
clients2.googleusercontent.com
EOF
```

- [ ] **Step 5: Run the audit**

Run: `scripts/netaudit 60`
Expected: a host list and `unexpected hosts: 0`.

If a host is unexpected, classify it before touching anything: a one-line note per host in `docs/netaudit-findings.md` with the host, what Chromium subsystem owns it (search the checkout for the host string), and one of: "allow, it is component updater or Safe Browsing infrastructure", "disable via a pref default in Stage 1", or "needs a patch, deferred". Typical candidates: `accounts.google.com` (should not appear without API keys), `www.gstatic.com` (often the Safe Browsing or omnibox suggestion assets), `www.google.com` (default search engine prefetch; acceptable until the search-engine decision), `optimizationguide-pa.googleapis.com` (optimisation guide hints; disable via pref default in Stage 1). Add to the allowlist only what D6 keeps.

- [ ] **Step 6: Commit**

```bash
git add scripts/run scripts/netaudit docs/netaudit-allowlist.txt
[ -f docs/netaudit-findings.md ] && git add docs/netaudit-findings.md
git commit -m "Add run script and idle network audit against the D6 allowlist

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 6: Perf script and Stage 0 baseline

**Files:**
- Create: `scripts/perf`
- Create: `docs/perf/2026-09-XX-stage0-baseline.md` (actual date at run time)

**Interfaces:**
- Consumes: binary path convention from Task 5.
- Produces: `scripts/perf [--runs N] [--idle SECONDS] [--label NAME]` writing `docs/perf/<date>-<label>.md`. Later stages compare against the Stage 0 file.

Method, kept deliberately cheap: startup is the time from process spawn until the DevTools HTTP endpoint answers `/json/version`, which happens after the first window and its renderer are up. Idle memory is the sum of resident set size over every process whose command line contains the app bundle path, sampled after the idle period. Process count is the number of those processes. All three are proxies; they are consistent between runs of the same build on the same machine, which is all a baseline needs.

- [ ] **Step 1: Write the perf script**

```bash
cat > scripts/perf <<'EOF'
#!/usr/bin/env python3
"""Cheap, dependency-free perf baseline for Arcium.

Usage: scripts/perf [--runs N] [--idle SECONDS] [--label NAME] [--config dev|perf]
Measures per run: startup_ms (spawn -> DevTools endpoint answers), idle_rss_mb (sum over all
browser processes after the idle period), process_count. Writes docs/perf/<date>-<label>.md.
"""
import argparse, datetime, json, os, pathlib, shutil, statistics, subprocess, sys, tempfile, time, urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent
CHROMIUM_ROOT = pathlib.Path(os.environ.get("CHROMIUM_ROOT", "/Volumes/Texternal/chromium"))
PORT = 9333


def app_path(config):
    return CHROMIUM_ROOT / "src" / "out" / config / "Arcium.app"


def processes(app):
    out = subprocess.run(["ps", "-axo", "pid=,rss=,command="], capture_output=True, text=True).stdout
    rows = []
    for line in out.splitlines():
        parts = line.strip().split(None, 2)
        if len(parts) == 3 and str(app) in parts[2]:
            rows.append((int(parts[0]), int(parts[1])))
    return rows


def one_run(app, idle):
    profile = tempfile.mkdtemp(prefix="arcium-perf-")
    binary = app / "Contents" / "MacOS" / "Arcium"
    start = time.monotonic()
    proc = subprocess.Popen([str(binary), f"--user-data-dir={profile}", "--no-first-run",
                             "--no-default-browser-check", f"--remote-debugging-port={PORT}",
                             "about:blank"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    startup_ms = None
    deadline = start + 60
    while time.monotonic() < deadline:
        try:
            urllib.request.urlopen(f"http://127.0.0.1:{PORT}/json/version", timeout=0.2).read()
            startup_ms = round((time.monotonic() - start) * 1000)
            break
        except Exception:
            time.sleep(0.02)
    if startup_ms is None:
        proc.kill()
        raise SystemExit("browser did not come up within 60 s")
    time.sleep(idle)
    rows = processes(app)
    rss_mb = round(sum(r for _, r in rows) / 1024)
    subprocess.run(["osascript", "-e", 'tell application "Arcium" to quit'],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        proc.wait(timeout=30)
    except subprocess.TimeoutExpired:
        proc.kill()
    shutil.rmtree(profile, ignore_errors=True)
    return {"startup_ms": startup_ms, "idle_rss_mb": rss_mb, "process_count": len(rows)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--runs", type=int, default=3)
    ap.add_argument("--idle", type=int, default=60)
    ap.add_argument("--label", default="baseline")
    ap.add_argument("--config", default=os.environ.get("ARCIUM_CONFIG", "dev"))
    a = ap.parse_args()
    app = app_path(a.config)
    if not app.exists():
        raise SystemExit(f"no app at {app}; run scripts/build {a.config}")
    if processes(app):
        raise SystemExit("Arcium is already running from this out dir; quit it first")
    results = []
    for i in range(a.runs):
        r = one_run(app, a.idle)
        print(f"run {i + 1}: {r}", file=sys.stderr)
        results.append(r)
        time.sleep(2)
    med = {k: statistics.median(r[k] for r in results) for k in results[0]}
    date = datetime.date.today().isoformat()
    tag = (ROOT / "CHROMIUM_VERSION").read_text().strip()
    rev = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=ROOT, capture_output=True, text=True).stdout.strip()
    out = ROOT / "docs" / "perf" / f"{date}-{a.label}.md"
    out.write_text(f"""# Perf: {a.label} ({date})

Config `{a.config}`, Chromium `{tag}`, arcium `{rev}`, {a.runs} runs, {a.idle} s idle, fresh profile, about:blank.
Method: see scripts/perf. Numbers are medians.

| Metric | Median |
|---|---|
| Startup to DevTools endpoint (ms) | {med['startup_ms']:.0f} |
| Idle RSS, all processes (MB) | {med['idle_rss_mb']:.0f} |
| Process count | {med['process_count']:.0f} |

Raw runs: `{json.dumps(results)}`
""")
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
EOF
chmod +x scripts/perf
```

- [ ] **Step 2: Do a quick smoke run of the script with short settings**

Run: `scripts/perf --runs 1 --idle 5 --label smoke`
Expected: one `run 1: {...}` line and `wrote docs/perf/<date>-smoke.md`. Then delete that file: `rm docs/perf/*-smoke.md`.

- [ ] **Step 3: Record the Stage 0 baseline when the machine is quiet**

Run: `scripts/perf --runs 3 --idle 60 --label stage0-baseline`
Expected: `wrote docs/perf/<date>-stage0-baseline.md`. Append the Task 5 smoke checklist results to that file under `## Smoke` as a checklist with pass or fail per item.

This build is functionally vanilla Chromium with Arcium branding, so its numbers are the reference every later stage compares against. Note the machine's load at the time in the file if it was not quiet.

- [ ] **Step 4: Commit**

```bash
git add scripts/perf docs/perf
git commit -m "Add perf script and record the Stage 0 baseline

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 7: Rebase script

**Files:**
- Create: `scripts/rebase`

**Interfaces:**
- Consumes: `ensure_symlinks`, `apply_patches` from `scripts/apply-patches`.
- Produces: `scripts/rebase <tag>` that leaves the checkout on `<tag>` with patches applied, updates `CHROMIUM_VERSION`, and exits non-zero listing conflicting patches.

- [ ] **Step 1: Write the rebase script**

```bash
cat > scripts/rebase <<'EOF'
#!/usr/bin/env bash
# Usage: scripts/rebase <chromium tag>
# Moves the checkout to the tag, syncs DEPS, reapplies patches, updates CHROMIUM_VERSION.
# Upstream files carry no edits outside patches (CLAUDE.md), so discarding the working tree is safe.
source "$(dirname "$0")/lib.sh"
source "$ARCIUM_ROOT/scripts/apply-patches"
need_src

tag="${1:-}"
[ -n "$tag" ] || die "usage: scripts/rebase <tag>   (current: $CHROMIUM_TAG)"

cd "$SRC"
log "discarding patch effects in the working tree"
git checkout -q -- .
git clean -qfd   # keeps ignored paths, so our symlinks (in .git/info/exclude) survive

if ! git rev-parse -q --verify "refs/tags/$tag" >/dev/null; then
  log "fetching tag $tag"
  git fetch --depth 1 origin "+refs/tags/$tag:refs/tags/$tag"
fi
log "checking out $tag"
git checkout -q "tags/$tag"

log "syncing DEPS"
(cd "$CHROMIUM_ROOT" && caffeinate -i gclient sync -D --no-history)

printf '%s\n' "$tag" > "$ARCIUM_ROOT/CHROMIUM_VERSION"
CHROMIUM_TAG="$tag"

ensure_symlinks
if apply_patches; then
  log "rebase to $tag complete with no conflicts; now run scripts/build dev"
else
  die "rebase to $tag has conflicting patches; fix them in patches/ and re-run scripts/sync"
fi
EOF
chmod +x scripts/rebase
```

- [ ] **Step 2: Verify a rebase onto the current tag is a no-op (spec A0.3)**

Run: `scripts/rebase "$(cat CHROMIUM_VERSION)" && git status --short CHROMIUM_VERSION`
Expected: `patches: applied=0 skipped=0 failed=0`, `rebase to 152.0.7977.83 complete with no conflicts`, and no diff in `CHROMIUM_VERSION`. `gclient sync` on an already-synced tree finishes in a minute or two.

- [ ] **Step 3: Verify the build is still incremental afterwards**

Run: `scripts/build dev 2>&1 | tail -3`
Expected: ninja reports a small number of steps or `no work to do`, finishing in under a few minutes. If it rebuilt the world, `gclient sync` moved a dependency; check `git -C "$SRC" status` and `git -C "$SRC" diff DEPS` (should be empty).

- [ ] **Step 4: Commit**

```bash
git add scripts/rebase
git commit -m "Add rebase script: move to a tag, resync, reapply patches, report conflicts

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 8: Views playground target

**Files:**
- Modify: `scripts/build` (no change needed; it takes a target argument)
- Create: `scripts/playground`

**Interfaces:**
- Produces: `scripts/playground` building and launching `views_examples`, the standalone Views demo binary. Stage 1 adds Arcium sidebar components to it.

- [ ] **Step 1: Confirm the target exists at this tag**

Run: `source scripts/lib.sh && (cd "$SRC" && gn ls out/dev //ui/views/examples:* | head)`
Expected: a list including `//ui/views/examples:views_examples`.

- [ ] **Step 2: Write the playground script**

```bash
cat > scripts/playground <<'EOF'
#!/usr/bin/env bash
# Build and launch the standalone Views examples binary for fast UI iteration.
# Stage 1 registers Arcium's sidebar components here. Pass --enable-examples=NAME to open one directly.
source "$(dirname "$0")/lib.sh"
need_src
config="${ARCIUM_CONFIG:-dev}"
"$ARCIUM_ROOT/scripts/build" "$config" views_examples
bin="$SRC/out/$config/views_examples"
[ -x "$bin" ] || die "views_examples did not produce $bin"
exec "$bin" "$@"
EOF
chmod +x scripts/playground
```

- [ ] **Step 3: Build and run it**

Run: `scripts/playground`
Expected: after an incremental build (most of `ui/views` is already compiled by Task 4), a window titled "Views Examples" opens with a combo box of examples. Pick "Button" and confirm buttons render and respond. Quit with Cmd+Q.

- [ ] **Step 4: Commit**

```bash
git add scripts/playground
git commit -m "Add Views playground launcher for UI iteration outside the browser

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 9: Documentation and stage close-out

**Files:**
- Modify: `CLAUDE.md` (Commands section and Stage status table)
- Modify: `patches/README.md` (add the "zero patches so far" note and the rebase routine)

- [ ] **Step 1: Replace the Commands section in CLAUDE.md**

Replace the block from `## Commands` up to (not including) `## Stage status` with:

```markdown
## Commands

All scripts read `CHROMIUM_VERSION` and put depot_tools on PATH themselves.

```
scripts/bootstrap             # one-time: depot_tools + shallow checkout at the pinned tag (1-2 h)
scripts/sync                  # idempotent: symlinks into the tree, apply unapplied patches
scripts/build <config> [tgt]  # dev | perf | release; args from build/common.gni + build/<config>.gn
scripts/run [flags] [url]     # launch out/dev with user-data-dir Application Support/Arcium-dev
scripts/playground            # build + launch views_examples for UI iteration
scripts/netaudit [seconds]    # idle network audit against docs/netaudit-allowlist.txt
scripts/perf [--runs N] [--idle S] [--label L]   # startup, idle RSS, process count -> docs/perf/
scripts/rebase <tag>          # move to a new Chromium tag, resync, reapply patches
```

Environment knobs: `ARCIUM_JOBS` (ninja parallelism, default all cores; lower it when the machine is
busy), `ARCIUM_CONFIG` (dev by default), `ARCIUM_USER_DATA_DIR`, `CHROMIUM_ROOT`.

Long builds: run `nohup scripts/build dev > /Volumes/Texternal/chromium/build.log 2>&1 &` and tail the log.
```

- [ ] **Step 2: Mark Stage 0 done in the status table**

Change the row `| 0 Foundation | not started |` to `| 0 Foundation | done (baseline in docs/perf/) |`.

- [ ] **Step 3: Extend patches/README.md with the routine**

Append:

```markdown

## Status

Stage 0 shipped with zero patches. Branding is selected by GN (`branding_path_component`), and
Google services are removed by GN args in `build/common.gni`. Keep it that way as long as possible.

## Monthly rebase routine

1. Find the new stable tag on https://chromiumdash.appspot.com/releases?platform=Mac
2. `scripts/rebase <tag>`; fix any conflicting patch by moving its hook to a more stable seam.
3. `scripts/build dev`, run the last completed stage's acceptance list by hand.
4. `scripts/netaudit 60`, then `scripts/perf --label rebase-<tag>` when the machine is quiet.
5. Commit `CHROMIUM_VERSION` and any patch changes together with a message naming the tag.
```

- [ ] **Step 4: Run the whole acceptance list once more (spec A0.1 to A0.3) and confirm each passed**

- A0.1: `scripts/run`: browse, video, DevTools, extension install (Task 5 Step 2 checklist, all pass or documented).
- A0.2: `docs/perf/<date>-stage0-baseline.md` exists with three metrics.
- A0.3: `scripts/rebase "$(cat CHROMIUM_VERSION)"` reports zero conflicts (Task 7 Step 2).

- [ ] **Step 5: Commit**

```bash
git add CLAUDE.md patches/README.md
git commit -m "Close Stage 0: document commands, rebase routine, mark stage done

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

## Self-review against the spec

| Requirement | Task |
|---|---|
| R0.1 depot_tools, checkout at stable tag on external drive, CHROMIUM_VERSION | 1 |
| R0.2 Repo layout, `src/arcium` symlink | 2 (symlinks), layout from the initial commit |
| R0.3 Scripts sync, build, run, rebase, perf | 2, 4, 5, 7, 6 |
| R0.4 Dev config component build, clean build completes, incremental in minutes | 4, verified in 7 Step 3 |
| R0.5 Views playground builds and runs | 8 |
| R0.6 Branding name, bundle id, icon, user data dir | 3, verified in 4 Step 6 and 5 Step 2 item 3 |
| R0.7 Google sign-in, Sync, metrics, crash reporting, promos, Finch off; Safe Browsing toggle | 4 (GN args), verified by 5 Steps 2 and 5 |
| R0.8 Web Store installs an extension | 5 Step 2 item 5 |
| R0.9 Baseline perf recorded in docs/perf | 6 |
| A0.1, A0.2, A0.3 | 9 Step 4 |

Gaps accepted for this stage: R0.7's metrics and crash reporting rely on Chromium's non-branded default of never uploading. The network audit is the check; if it shows `clients4.google.com` or `crash*.google.com`, that becomes a pref-default hook in Stage 1. "Promos" without API keys reduce to nothing to promote; the settings check in Task 5 Step 2 item 6 confirms it.
