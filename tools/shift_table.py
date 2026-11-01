#!/usr/bin/env python3
"""Makes another version's function table (another language) from the reference version's, with
the shifts tools/port_version.py found (tools/versions/<code>.txt):

    shift_table.py <reference table.txt> tools/versions/<code>.txt <out: code.txt>
    shift_table.py --all <funcs folder>

--all: every version in tools/versions whose reference table (IRBO.txt, CPUE.txt, ...) is in the
folder, written beside it as <code>.txt (what the builds run after making their tables).

The overlay functions' first bytes are left as ?, for the program to read from the player's ROM
(frontend/funcwatch.h), so the table holds addresses and names only.
"""
import glob
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def module_key(module):
    m = re.match(r"ov0*(\d+)$", module)
    return "ov" + m.group(1) if m else module


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "--all":
        for shifts_path in sorted(glob.glob(os.path.join(HERE, "versions", "*.txt"))):
            like = next((l.split()[2] for l in open(shifts_path) if l.startswith("# like ")), None)
            table = os.path.join(sys.argv[2], "%s.txt" % like)
            if like and os.path.exists(table):
                code = os.path.splitext(os.path.basename(shifts_path))[0]
                shift(table, shifts_path, os.path.join(sys.argv[2], code + ".txt"))
        return
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    shift(*sys.argv[1:4])


def shift(table, shifts_path, out_path):
    shifts = {}
    for line in open(shifts_path):
        p = line.split("#", 1)[0].split()
        if len(p) >= 3:
            shifts.setdefault(module_key(p[0]), []).append((int(p[1], 16), int(p[2], 16)))
    for v in shifts.values():
        v.sort()
    rows = []
    for line in open(table):
        p = line.split()
        if len(p) < 5:
            continue
        addr, kind, module, _first, name = int(p[0], 16), p[1], p[2], p[3], p[4]
        delta = 0
        for start, d in shifts.get(module_key(module), ()):
            if start > addr:
                break
            delta = d
        first = "?" if module.startswith("ov") else "-"
        rows.append((addr + delta, kind, module, first, name))
    with open(out_path, "w") as f:
        for r in sorted(rows):
            f.write("%08x %s %s %s %s\n" % r)
    print(f"{out_path}: {len(rows)} functions")


if __name__ == "__main__":
    main()
