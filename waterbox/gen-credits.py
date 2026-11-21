#!/usr/bin/env python3
"""Makes the engine's credits.c from its template and its licence table, as the
engine's CMake does (csv_to_cstr): every line of the table is a C string."""
import sys

template, csv, out = sys.argv[1:4]
lines = open(csv, encoding="utf-8").read().splitlines()
cstr = "".join('\n"%s\\n"' % l for l in lines)
text = open(template, encoding="utf-8").read().replace("@COPYRIGHT_DATA_CSV@", cstr)
open(out, "w", encoding="utf-8").write(text)
