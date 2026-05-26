#!/bin/bash
# Recovery script for users who hit macOS Gatekeeper's "quewi is
# damaged and can't be opened" message.
#
# Strict-mode Gatekeeper on Apple Silicon refuses to open any
# bundle that's both unnotarized AND has the com.apple.quarantine
# extended attribute set — and there's no right-click → Open
# escape hatch from that prompt. The fix is one xattr -cr command;
# this file just runs it without requiring the user to open
# Terminal themselves.
#
# Double-click this file from inside the mounted DMG (or from your
# Applications folder after copying everything across) and the
# script will:
#   1. Find quewi.app in /Applications
#   2. Strip every extended attribute recursively
#   3. Launch quewi
#
# This is a workaround until quewi ships with a real Apple
# Developer ID + notarization. Quewi the application is fine —
# this is purely Apple's anti-malware system being overprotective
# of a small open-source project that hasn't paid the toll.

set -u

APP="/Applications/quewi.app"

clear

cat <<'BANNER'

  ╔══════════════════════════════════════════════════════════════╗
  ║                                                              ║
  ║   quewi — Gatekeeper recovery                                ║
  ║                                                              ║
  ║   This script strips macOS's quarantine flag from quewi      ║
  ║   so it stops saying it's damaged. Safe; reversible by       ║
  ║   redownloading.                                             ║
  ║                                                              ║
  ╚══════════════════════════════════════════════════════════════╝

BANNER

if [ ! -d "$APP" ]; then
    echo "❌ Couldn't find $APP"
    echo
    echo "Before running this script:"
    echo "  1. Open the quewi installer (the disk image you just"
    echo "     downloaded)."
    echo "  2. Drag quewi.app onto the Applications shortcut."
    echo "  3. THEN run this script."
    echo
    echo "Press any key to close."
    read -n 1 -s
    exit 1
fi

echo "Found quewi at: $APP"
echo
echo "Stripping quarantine + extended attributes…"
if xattr -cr "$APP"; then
    echo "  ✓ done"
else
    echo "  ✗ xattr failed (exit $?)"
    echo
    echo "You may need to run this from an admin account, or run:"
    echo "    sudo xattr -cr \"$APP\""
    echo "in Terminal directly."
    echo
    echo "Press any key to close."
    read -n 1 -s
    exit 1
fi

echo
echo "Launching quewi…"
open "$APP"

echo
echo "If quewi opened, you're set — this was a one-time fix."
echo "Press any key to close this window."
read -n 1 -s
