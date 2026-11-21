#!/usr/bin/env python3
"""Writes the route the tests play in Surge the Rabbit's first level
(levels/sunshine-1.lev, started with the startLevel setting): one line per
step, the buttons held on that step (gate-harness.h: U D L R the directions,
A Fire 1 which jumps, B Fire 2, S Start, E Back).

The route runs right the whole time and jumps in bursts, so that the player
meets the level's slopes, springs and enemies, and now and then looks up and
ducks (an ordinary run, not a fast one). It is made from a small fixed
sequence, so the same arguments make the same file.

usage: make-route.py <steps> <out>
"""
import sys

steps = int(sys.argv[1])
lines = []
for i in range(steps):
    keys = ""
    if i < 90:
        pass                     # the level loads and the player waits
    elif i % 997 > 960:
        keys = "D"               # duck now and then
    elif i % 613 > 590:
        keys = "U"               # look up
    else:
        keys = "R"
        if i % 61 < 14 or i % 173 < 30:
            keys += "A"          # jump, in bursts
    lines.append(keys)
open(sys.argv[2], "w").write("# the first level, %d steps\n" % steps + "\n".join(lines) + "\n")
