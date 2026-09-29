#!/usr/bin/env bash
set -euo pipefail
BUNDLE="${1:-./FilmGrainOFX.ofx.bundle}"
DEST="/usr/OFX/Plugins/FilmGrainOFX.ofx.bundle"
sudo mkdir -p /usr/OFX/Plugins
sudo rm -rf "$DEST"
sudo cp -R "$BUNDLE" "$DEST"
echo "Installed. Restart DaVinci Resolve."
