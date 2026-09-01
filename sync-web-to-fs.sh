#!/bin/sh
# Recopie web/ → firmware/data/ avant pio run -t uploadfs
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
python3 "$ROOT/sync_web.py"
