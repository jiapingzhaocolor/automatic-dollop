#!/usr/bin/env bash
set -euo pipefail
BUNDLE="${1:-./FilmGrainOFX.ofx.bundle}"
DEST="/Library/OFX/Plugins/FilmGrainOFX.ofx.bundle"
sudo mkdir -p /Library/OFX/Plugins
sudo rm -rf "$DEST"
sudo cp -R "$BUNDLE" "$DEST"
sudo xattr -dr com.apple.quarantine "$DEST" 2>/dev/null || true
echo "Installed. Restart DaVinci Resolve."
