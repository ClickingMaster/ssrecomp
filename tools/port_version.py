#!/usr/bin/env python3
"""Finds where another version of a game (another language: Spanish, French, ...) has the
functions of the version the tables were made for, and writes them as shifts: addresses only,
nothing of the game. A developer tool, run once per version with both ROMs; the shifts it
writes are what the builds use.

    port_version.py funcs <table.txt> <reference.nds> <other.nds> <out.txt>
    port_version.py soulsilver <gen dir> <soulsilver-us.nds> <other.nds> <out.json> "<game name>"

funcs: for the games with a function table (Black, White, Black 2, White 2, Platinum): <table.txt>
is the reference version's table (data/funcs/IRBO.txt, tools/platinum/CPUE.txt, ...). Writes
tools/versions/<code>.txt, from which tools/shift_table.py makes the other version's table.

soulsilver: for SoulSilver and HeartGold, whose tables are SoulSilver's (US) chunks
(<gen dir>/rc_tables.cpp, from tools/recomp.py): writes tools/soulsilver/<name>.json, the form
tools/soulsilver/heartgold-us.json has, which tools/soulsilver/tables_only.py builds in.

A localised version is the same program with its text and some of its code moved: every function
is looked for in the same module of the other ROM, first where the function before it was found
(the running shift), then a little either side, by its instructions once addresses, branch targets
and literal-pool offsets are masked (tools/bw1/match_bw2.py's tokens).
"""
import json
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "bw1"))
from ndsrom import read_rom  # noqa: E402
from match_bw2 import tokens  # noqa: E402
from recomp import autoload_image  # noqa: E402

WINDOW = 0x800      # searched either side of the running shift
COMPARED = 0x100    # bytes of a function compared at most
WIDE = 0x4000       # searched for a long function not found in WINDOW
ITCM = 0x01FF8000


class Modules:
    """module name (main, itcm, ovN) -> (RAM address, bytes) of a ROM."""

    def __init__(self, path):
        rom = open(path, "rb").read()
        self.code = rom[0x0C:0x10].decode("ascii", "replace")
        main, ovs = read_rom(path)
        # (The main program loads at 0x02000000, or 0x02004000 in the DSi-enhanced games.)
        self.mods = {"main": (struct.unpack_from("<I", rom, 0x28)[0], main)}
        try:
            self.mods["itcm"] = (ITCM, autoload_image(main, ITCM))
        except SystemExit:
            pass  # (the DSi-enhanced games keep their autoload list elsewhere: the ITCM is left as it is)
        for oid, (ram, data) in ovs.items():
            self.mods["ov%d" % oid] = (ram, data)

    def get(self, module):
        # (Black's tables write ov001, Platinum's ov1.)
        m = re.match(r"ov0*(\d+)$", module)
        return self.mods.get("ov%s" % m.group(1) if m else module)


def match_module(ref, other, funcs):
    """funcs: sorted (address, size, isa) of one module. Returns {address: shift} for the
    functions found, and how many were not."""
    (rbase, rdata), (obase, odata) = ref, other
    delta = obase - rbase
    found, missed = {}, 0
    for addr, size, isa in funcs:
        size = min(size, COMPARED)
        off = addr - rbase
        if size < 4 or off < 0 or off + size > len(rdata):
            missed += 1
            continue
        want = tokens(rdata[off:off + size], isa)
        step = 4 if isa == "arm" else 2
        hit = None
        for k in range(WINDOW // step + 1):
            for d in ((delta,) if k == 0 else (delta + k * step, delta - k * step)):
                o = off + d - (obase - rbase)
                if 0 <= o and o + size <= len(odata) and tokens(odata[o:o + size], isa) == want:
                    hit = d
                    break
            if hit is not None:
                break
        if hit is None:
            missed += 1
            continue
        # (A short function's masked instructions can be found in more than one place: it moves
        # the shift only where it is where the shift says.)
        if size < 24 and hit != delta:
            missed += 1
            continue
        found[addr] = hit
        delta = hit
    # Second pass over those not found: at the shift of the next function found (code that moved
    # with the one after it), else further away for a function long enough to be told apart.
    for i, (addr, size, isa) in enumerate(funcs):
        size = min(size, COMPARED)
        off = addr - rbase
        if addr in found or size < 4 or off < 0 or off + size > len(rdata):
            continue
        want = tokens(rdata[off:off + size], isa)
        step = 4 if isa == "arm" else 2

        def same(d, off=off, size=size, isa=isa, want=want):
            o = off + d - (obase - rbase)
            return 0 <= o and o + size <= len(odata) and tokens(odata[o:o + size], isa) == want

        nxt = next((found[a] for a, _s, _i in funcs[i + 1:] if a in found), None)
        hit = nxt if nxt is not None and same(nxt) else None
        if hit is None and size >= 24:
            base = nxt if nxt is not None else obase - rbase
            hit = next((d for k in range(WIDE // step + 1) for d in (base + k * step, base - k * step) if same(d)), None)
        if hit is not None:
            found[addr] = hit
            missed -= 1
    return found, missed


def shifts_of(funcs, found, start_delta):
    """Ranges [from, to, shift] over the module's functions: one not found keeps the shift of
    the one before it."""
    segs, delta = [], start_delta
    for addr, size, _isa in funcs:
        delta = found.get(addr, delta)
        if segs and segs[-1][2] == delta:
            segs[-1][1] = addr + size
        else:
            segs.append([addr, addr + size, delta])
    return segs


def port_funcs(table, ref_path, other_path, out_path):
    ref, other = Modules(ref_path), Modules(other_path)
    by_module = {}
    for line in open(table):
        p = line.split()
        if len(p) < 5:
            continue
        by_module.setdefault(p[2], []).append((int(p[0], 16), "thumb" if p[1] == "t" else "arm", p[4]))
    lines, total, found_all = [], 0, 0
    for module in sorted(by_module, key=lambda m: (m != "main", m != "itcm", int(m[2:]) if m[2:].isdigit() else -1)):
        r, o = ref.get(module), other.get(module)
        if not r or not o:
            continue
        rows = sorted(set(by_module[module]))
        funcs = []
        for i, (addr, isa, _name) in enumerate(rows):
            nxt = rows[i + 1][0] if i + 1 < len(rows) else r[0] + len(r[1])
            funcs.append((addr, max(0, nxt - addr), isa))
        found, _missed = match_module(r, o, funcs)
        total += len(funcs)
        found_all += len(found)
        names = {a: n for a, _i, n in rows}
        last = None
        for addr, _size, _isa in funcs:
            if addr in found and found[addr] != last:
                # (The module's first line from its first function: those before the first found keep its shift.)
                at = addr if last is not None else funcs[0][0]
                last = found[addr]
                lines.append("%s %08x %+#x %s" % (module, at, last, names[at]))
        if last is None and o[0] != r[0]:
            lines.append("%s %08x %+#x %s" % (module, funcs[0][0], o[0] - r[0], names[funcs[0][0]]))
    with open(out_path, "w") as f:
        f.write("# like %s\n" % ref.code)
        f.write("# %s: where %s's functions are, as shifts: <module> <%s address from which> "
                "<shift> <the first function so moved>.\n" % (other.code, ref.code, ref.code))
        f.write("# Made by tools/port_version.py; read by tools/shift_table.py.\n")
        for line in lines:
            f.write(line + "\n")
    print(f"{other.code}: {found_all} of {total} functions found, {len(lines)} shift lines")


def port_soulsilver(gen, ref_path, other_path, out_path, title):
    from soulsilver.match_heartgold import chunks_of  # noqa: E402
    layout = json.load(open(os.path.join(HERE, "..", "layouts", "soulsilver-us.json")))
    ref, other = Modules(ref_path), Modules(other_path)
    if ref.code != "IPGE":
        sys.exit(f"{ref_path} is {ref.code}: the tables are SoulSilver's (US, IPGE)")
    chunks, names = chunks_of(gen)
    result, total, found_all = {}, 0, 0
    for e in layout["sections"]:
        module = "main" if e["source"] == "main" else "itcm" if e["source"] == "itcm" else "ov%d" % e["source"]
        r, o = ref.get(module), other.get(module)
        if not r or not o or e["name"] not in names:
            continue
        sec = names.index(e["name"])
        funcs = sorted((s, t - s, "thumb" if th else "arm") for s, t, th, ss in chunks if ss == sec)
        found, missed = match_module(r, o, funcs)
        total += len(funcs)
        found_all += len(found)
        segs = [s for s in shifts_of(funcs, found, o[0] - r[0]) if s[2]]
        if segs:
            result[e["name"]] = segs
        if missed:
            print(f"  {e['name']}: {len(found)} found, {missed} not", file=sys.stderr)
    json.dump({"game": title, "code": other.code, "like": "IPGE", "shifts": result}, open(out_path, "w"), indent=1)
    print(f"{other.code}: {found_all} of {total} chunks found, {sum(map(len, result.values()))} shifted stretches")


def main():
    if len(sys.argv) < 6 or sys.argv[1] not in ("funcs", "soulsilver"):
        sys.exit(__doc__)
    if sys.argv[1] == "funcs":
        port_funcs(*sys.argv[2:6])
    else:
        if len(sys.argv) < 7:
            sys.exit(__doc__)
        port_soulsilver(*sys.argv[2:7])


if __name__ == "__main__":
    main()
