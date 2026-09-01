#!/usr/bin/env python3
# Recopie web/ → firmware/data/ (sans serve.py ni tests).
# Usage : python sync_web.py
from __future__ import print_function
import os
import shutil
import sys

SKIP_NAMES = {"serve.py", "test"}


def sync(root=None):
    if root is None:
        root = os.path.dirname(os.path.abspath(__file__))
    src = os.path.join(root, "web")
    dst = os.path.join(root, "firmware", "data")
    if not os.path.isdir(src):
        print("web/ introuvable :", src, file=sys.stderr)
        sys.exit(1)
    if os.path.isdir(dst):
        shutil.rmtree(dst)
    os.makedirs(dst)
    for name in os.listdir(src):
        if name in SKIP_NAMES or name.startswith("."):
            continue
        s = os.path.join(src, name)
        d = os.path.join(dst, name)
        if os.path.isdir(s):
            shutil.copytree(s, d)
        else:
            shutil.copy2(s, d)
    print("LittleFS data/ synchronise depuis web/")


if __name__ == "__main__":
    sync()
