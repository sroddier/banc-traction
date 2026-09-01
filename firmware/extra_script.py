# Recopie ../web → data/ avant compilation / uploadfs.
# Appelé par PlatformIO (pre:extra_script) et par sync_web.py à la racine.
Import("env")

import os
import sys

ROOT = os.path.normpath(os.path.join(env["PROJECT_DIR"], ".."))
if ROOT not in sys.path:
    sys.path.insert(0, ROOT)

import sync_web

sync_web.sync(ROOT)
