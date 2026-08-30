#!/bin/sh
# Recopie web/ → firmware/data/ avant pio run -t uploadfs
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
DEST="$ROOT/firmware/data"
rm -rf "$DEST"
mkdir -p "$DEST"
cp -a "$ROOT/web/index.html" "$ROOT/web/manifest.json" "$ROOT/web/sw.js" "$ROOT/web/icon.svg" \
      "$ROOT/web/css" "$ROOT/web/js" "$DEST/"
echo "LittleFS data/ synchronisé depuis web/"
