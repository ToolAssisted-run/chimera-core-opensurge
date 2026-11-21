#!/usr/bin/env python3
"""Zips Surge the Rabbit's files (the engine's own folders of game data) into
one stored zip: sorted, one timestamp, no compression (the sound and the
pictures are compressed already), so the same sources make the same bytes."""
import os, sys, zipfile

src, out = sys.argv[1:3]
FOLDERS = ["characters", "fonts", "images", "inputs", "languages", "levels", "musics", "quests", "samples",
           "scripts", "sprites", "themes"]
FILES = ["surge.cfg", "surge.rocks", "LICENSE"]
names = []
for f in FOLDERS:
    for root, dirs, files in os.walk(os.path.join(src, f)):
        dirs.sort()
        for n in sorted(files):
            names.append(os.path.relpath(os.path.join(root, n), src))
names += [f for f in FILES if os.path.exists(os.path.join(src, f))]
names.sort()
with zipfile.ZipFile(out, "w", zipfile.ZIP_STORED) as z:
    for n in names:
        i = zipfile.ZipInfo(n.replace(os.sep, "/"), date_time=(1980, 1, 1, 0, 0, 0))
        i.compress_type = zipfile.ZIP_STORED
        i.external_attr = 0o644 << 16
        z.writestr(i, open(os.path.join(src, n), "rb").read())
print("%d files -> %s" % (len(names), out))
