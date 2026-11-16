#!/bin/bash
# install.sh — build iterate-imessage.app, the launcher that alone holds Full Disk Access for the
# iMessage lend, and write its LaunchAgent. The owner runs it on the Mac, as themselves: it never
# uses sudo, loads no job and changes no privacy setting. See ../README.md#set-it-up.
#
#   imessage/launcher/install.sh [--dry-run]
#
# NODE and ITERATE name the programs to fix inside the launcher (defaults: `command -v node` and the
# real path of `command -v iterate`). IMESSAGE_APP_DIR is where the app goes (default
# ~/Applications).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
DRY_RUN=0
for argument in "$@"; do
  case "$argument" in
    --dry-run) DRY_RUN=1 ;;
    *) echo "usage: $0 [--dry-run]" >&2; exit 64 ;;
  esac
done

LABEL="local.iterate-imessage"
APP_DIR="${IMESSAGE_APP_DIR:-$HOME/Applications}"
APP="$APP_DIR/iterate-imessage.app"
EXECUTABLE="$APP/Contents/MacOS/iterate-imessage"
AGENT="$HOME/Library/LaunchAgents/$LABEL.plist"
LOG_FILE="$HOME/Library/Logs/iterate-imessage.log"
CONFIG="$HOME/.config/iterate-imessage/launcher.conf"

die() { echo "install.sh: $*" >&2; exit 1; }

[[ "$(uname -s)" == "Darwin" ]] || die "the launcher is for macOS"
[[ "$(id -u)" != "0" ]] || die "run it as yourself, never with sudo"
command -v cc >/dev/null || die "no C compiler: run xcode-select --install"

NODE="${NODE:-$(command -v node || true)}"
[[ -n "$NODE" ]] || die "node is not on PATH: set NODE to its full path"
ITERATE_BIN="$(command -v iterate || true)"
ITERATE="${ITERATE:-${ITERATE_BIN:+$(realpath "$ITERATE_BIN")}}"
[[ -n "$ITERATE" ]] || die "the iterate CLI is not on PATH: npm install -g iterate@latest"
ENTRY="$(realpath "$ROOT/../src/imessage.ts")"

# Each path becomes a C string in the launcher and a string in a plist: keep to plain characters.
for path in "$NODE" "$ITERATE" "$ENTRY" "$APP" "$LOG_FILE"; do
  [[ "$path" =~ ^/[A-Za-z0-9._/@+-]+$ ]] ||
    die "$path: use an absolute path of letters, digits and . _ / @ + - only"
done
[[ -x "$NODE" && ! -d "$NODE" ]] || die "$NODE is not a program"
[[ -f "$ITERATE" ]] || die "$ITERATE is not a file"
"$NODE" -e 'const [a, b] = process.versions.node.split(".").map(Number);
  process.exit(a > 22 || (a === 22 && b >= 18) ? 0 : 1)' ||
  die "$NODE is older than Node 22.18"

echo "node:    $NODE"
echo "iterate: $ITERATE"
echo "lend:    $ENTRY"

BUILD="$(mktemp -d)"
trap 'rm -rf "$BUILD"' EXIT
NEW="$BUILD/iterate-imessage.app"
mkdir -p "$NEW/Contents/MacOS"
cp "$ROOT/Info.plist" "$NEW/Contents/Info.plist"
cc -std=c11 -O2 -Wall -Wextra -Werror \
  -DLEND_NODE="\"$NODE\"" -DLEND_ITERATE="\"$ITERATE\"" -DLEND_ENTRY="\"$ENTRY\"" \
  -o "$NEW/Contents/MacOS/iterate-imessage" "$ROOT/launcher.c"
# An ad-hoc signature: its designated requirement is the code's hash, so Full Disk Access granted to
# this build covers this build only. The hardened runtime keeps DYLD_* libraries out of it.
codesign --force --sign - --identifier "$LABEL" --options runtime \
  --entitlements "$ROOT/entitlements.plist" "$NEW" 2>/dev/null
codesign --verify --strict "$NEW"

cdhash() { codesign --display --verbose=3 "$1" 2>&1 | sed -n 's/^CDHash=//p'; }
NEW_HASH="$(cdhash "$NEW")"
OLD_HASH=""
[[ -d "$APP" ]] && OLD_HASH="$(cdhash "$APP" || true)"

REGRANT=0
if [[ "$NEW_HASH" == "$OLD_HASH" ]]; then
  echo "up to date: $APP"
elif ((DRY_RUN)); then
  echo "would ${OLD_HASH:+update}${OLD_HASH:-install}: $APP"
else
  mkdir -p "$APP_DIR"
  rm -rf "$APP"
  ditto "$NEW" "$APP"
  echo "${OLD_HASH:+updated}${OLD_HASH:-installed}: $APP"
  REGRANT=1
fi

PLIST="$BUILD/$LABEL.plist"
sed -e "s|@APP_EXECUTABLE@|$EXECUTABLE|g" -e "s|@LOG_FILE@|$LOG_FILE|g" \
  "$ROOT/$LABEL.plist" >"$PLIST"
plutil -lint -s "$PLIST"
if [[ -f "$AGENT" ]] && cmp -s "$PLIST" "$AGENT"; then
  echo "up to date: $AGENT"
elif ((DRY_RUN)); then
  [[ -f "$AGENT" ]] && echo "would back up: $AGENT"
  echo "would write: $AGENT"
else
  mkdir -p "$(dirname "$AGENT")"
  if [[ -f "$AGENT" ]]; then
    BACKUP="$AGENT.$(date +%Y%m%d%H%M%S).bak"
    mv "$AGENT" "$BACKUP"
    echo "backed up: $BACKUP"
  fi
  cp "$PLIST" "$AGENT"
  echo "installed: $AGENT"
fi

if [[ ! -f "$CONFIG" ]]; then
  echo "missing: $CONFIG (MIND and IMESSAGE_ACCOUNT; see the README)"
fi

if ((REGRANT)); then
  cat <<EOF

This build is new, so no Full Disk Access covers it yet. In System Settings → Privacy & Security
→ Full Disk Access, remove any earlier iterate-imessage entry, then press + and choose
$APP
Then start it, or restart it if it is running:
  launchctl bootstrap gui/\$(id -u) $AGENT
  launchctl kickstart -k gui/\$(id -u)/$LABEL
EOF
fi
