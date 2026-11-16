#!/bin/bash
# uninstall.sh — stop the iMessage lend's LaunchAgent and remove it and iterate-imessage.app. The
# owner runs it as themselves; it keeps ~/.config/iterate-imessage (the key, the allow-list, the
# configuration and the start point) and leaves the privacy settings to the owner.
#
#   imessage/launcher/uninstall.sh [--dry-run]
set -euo pipefail

DRY_RUN=0
for argument in "$@"; do
  case "$argument" in
    --dry-run) DRY_RUN=1 ;;
    *) echo "usage: $0 [--dry-run]" >&2; exit 64 ;;
  esac
done

LABEL="local.iterate-imessage"
APP="${IMESSAGE_APP_DIR:-$HOME/Applications}/iterate-imessage.app"
AGENT="$HOME/Library/LaunchAgents/$LABEL.plist"
SERVICE="gui/$(id -u)/$LABEL"

if launchctl print "$SERVICE" >/dev/null 2>&1; then
  if ((DRY_RUN)); then echo "would stop: $SERVICE"; else
    launchctl bootout "$SERVICE"
    echo "stopped: $SERVICE"
  fi
fi
for path in "$AGENT" "$APP"; do
  [[ -e "$path" ]] || continue
  if ((DRY_RUN)); then echo "would remove: $path"; else
    rm -rf "$path"
    echo "removed: $path"
  fi
done

cat <<EOF

Last, in System Settings → Privacy & Security, select iterate-imessage under Full Disk Access and
press −, and turn it off under Automation.
EOF
