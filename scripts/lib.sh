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

# Every symlink we place inside the Chromium tree: "<path inside src> <target>"
# The first is absolute (points at this repo); the rest are relative and point back into src/arcium.
ARCIUM_SYMLINKS=(
  "arcium $ARCIUM_ROOT/arcium"
  "chrome/app/theme/arcium ../../../arcium/branding/theme"
  "chrome/app/theme/default_100_percent/arcium ../../../../arcium/branding/default_100_percent"
  "chrome/app/theme/default_200_percent/arcium ../../../../arcium/branding/default_200_percent"
)
