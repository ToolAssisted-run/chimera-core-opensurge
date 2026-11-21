#!/usr/bin/env python3
"""Makes the zips the tests give the core as a project's files:

  game.zip   Surge the Rabbit's own files in a folder of a zip ("mygame/..."),
             as a person would zip a game's folder: what the "game" slot takes.
             Played, it must be the same game as the one in the program.
  saves.zip  one file for the user folder, as Export Save Data writes them:
             what the "savedata" slot takes.
  bad.zip    a zip that is not a game's (no surge.cfg), and not readable.

usage: make-zips.py <engine folder (extern/opensurge)> <out folder>
"""
import os, sys, zipfile

src, out = sys.argv[1:3]
os.makedirs(out, exist_ok=True)
FOLDERS = ["characters", "fonts", "images", "inputs", "languages", "levels", "musics", "quests", "samples",
           "scripts", "sprites", "themes"]
names = []
for f in FOLDERS:
    for root, dirs, files in os.walk(os.path.join(src, f)):
        dirs.sort()
        for n in sorted(files):
            names.append(os.path.relpath(os.path.join(root, n), src))
names += ["surge.cfg", "surge.rocks", "LICENSE"]
names.sort()
with zipfile.ZipFile(os.path.join(out, "game.zip"), "w", zipfile.ZIP_DEFLATED) as z:
    for n in names:
        i = zipfile.ZipInfo("mygame/" + n.replace(os.sep, "/"), date_time=(1980, 1, 1, 0, 0, 0))
        i.compress_type = zipfile.ZIP_STORED if n.endswith((".ogg", ".png", ".wav")) else zipfile.ZIP_DEFLATED
        z.writestr(i, open(os.path.join(src, n), "rb").read())
with zipfile.ZipFile(os.path.join(out, "saves.zip"), "w", zipfile.ZIP_STORED) as z:
    i = zipfile.ZipInfo("notes/test.txt", date_time=(1980, 1, 1, 0, 0, 0))
    z.writestr(i, b"a file of the user folder, put there by the project\n")
open(os.path.join(out, "bad.zip"), "wb").write(b"this is not a zip file at all")
