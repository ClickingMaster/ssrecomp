#!/usr/bin/env python3
"""Maps a game's function table onto a related game built from the same code (Platinum's onto
Diamond and Pearl): a developer tool, run once with both ROMs; the table it writes holds
addresses and names only.

    port_family.py <reference table.txt> <reference.nds> <other.nds> <out table.txt> [-j N]

Unlike another language of the same game (tools/port_version.py), a related game has its
overlays numbered and laid out differently, so functions are looked for in every module:

1. Each function's first instructions, masked (branch targets, literal-pool offsets and
   addresses), are looked up in an index of every position of the other ROM's code.
2. Functions found in exactly one place are anchors; each reference module is paired with the
   other ROM's module most of its anchors fall in.
3. Every function of the module is then taken at the candidate in the paired module nearest to
   where its neighbouring anchors put it (or found by a shorter prefix near there).

The index is built with numpy, and the modules are matched on all processor cores.
"""
import difflib
import os
import re
import sys
from multiprocessing import get_context

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from port_version import Modules  # noqa: E402

KEY = 10       # halfwords (Thumb) or words (ARM) in a function's key
SHORT = 4      # the shorter key, tried near where a function should be
NEAR = 0x400   # how far from the expected place a function may be
MIN_SIZE = 12  # bytes: a shorter function is not an anchor
FUZZY = 0x800  # how far past the last function found the most alike place is looked for
WINDOW = 0x8000  # the widest stretch searched between two functions found
COMPARED = 0x80  # bytes of a function compared for likeness
SAME = 0.75    # likeness from which it is the same function
BETWEEN = 0.6  # the same, for one found between its neighbours
TIGHT = 0.4    # the same, between the two functions next to it


def mask_thumb(data):
    """Each halfword with what moves between builds masked: BL halves, PC-relative loads and
    branches (their offsets)."""
    h = np.frombuffer(data[:len(data) & ~1], dtype="<u2").astype(np.uint32)
    top5 = h >> 11
    m = h.copy()
    bl = (top5 == 0x1E) | (top5 == 0x1F) | (top5 == 0x1D)
    m[bl] = top5[bl] << 11
    ldr = (top5 == 0x09) | (top5 == 0x14)           # ldr rX, [pc, #]; add rX, pc, #
    m[ldr] = h[ldr] & 0xFF00
    bcond = ((h >> 12) == 0xD) & (((h >> 8) & 0xF) < 0xE)
    m[bcond] = h[bcond] & 0xFF00
    m[top5 == 0x1C] = 0xE000                        # b
    return m


def mask_arm(data):
    w = np.frombuffer(data[:len(data) & ~3], dtype="<u4").astype(np.uint64)
    m = w.copy()
    br = ((w >> 25) & 7) == 5
    m[br] = w[br] & 0xFF000000
    ldr = (w & 0x0F7F0000) == 0x051F0000
    m[ldr] = w[ldr] & 0xFFFFF000
    addr = ((w >= 0x01FF8000) & (w < 0x02400000))
    m[addr] = 0xADD0ADD0
    return m


PRIME = np.uint64(1000003)


def keys(masked, n):
    """A hash of every run of n units starting at each position."""
    if len(masked) < n:
        return np.zeros(0, dtype=np.uint64)
    win = np.lib.stride_tricks.sliding_window_view(masked.astype(np.uint64), n)
    pw = PRIME ** np.arange(n, dtype=np.uint64)
    with np.errstate(over="ignore"):
        return (win * pw).sum(axis=1, dtype=np.uint64)


class Index:
    """Every position of a ROM's code by its key: Thumb at each halfword, ARM at each word."""

    def __init__(self, mods, n):
        self.mods = sorted(mods)
        self.n = n
        self.thumb = self._build(mods, n, mask_thumb, 2)
        self.arm = self._build(mods, n, mask_arm, 4)

    def _build(self, mods, n, mask, unit):
        ks, mods_, addrs = [], [], []
        for i, name in enumerate(self.mods):
            base, data = mods[name]
            k = keys(mask(data), n)
            ks.append(k)
            mods_.append(np.full(len(k), i, dtype=np.int32))
            addrs.append(base + np.arange(len(k), dtype=np.int64) * unit)
        k = np.concatenate(ks)
        order = np.argsort(k, kind="stable")
        return k[order], np.concatenate(mods_)[order], np.concatenate(addrs)[order]

    def find(self, key, arm):
        k, mods, addrs = self.arm if arm else self.thumb
        lo, hi = np.searchsorted(k, key), np.searchsorted(k, key, side="right")
        return [(self.mods[mods[i]], int(addrs[i])) for i in range(lo, min(hi, lo + 64))]


def key_of(data, off, arm, n):
    m = mask_arm(data[off:off + 4 * n]) if arm else mask_thumb(data[off:off + 2 * n])
    k = keys(m, n)
    return k[0] if len(k) else None


# Shared with the worker processes (set before the pool starts: fork copies them).
G = {}


def similar(a, b):
    """How alike two masked runs are (0 to 1)."""
    return difflib.SequenceMatcher(None, a.tolist(), b.tolist(), autojunk=False).ratio()


def match_module(job):
    """The functions of one reference module in its paired module."""
    mod, other_mod, funcs, anchors = job
    rbase, rdata = G["ref"][mod]
    obase, odata = G["other"][other_mod]
    rmask = {False: mask_thumb(rdata), True: mask_arm(rdata)}
    omask = {False: mask_thumb(odata), True: mask_arm(odata)}
    okeys = {(arm, n): keys(omask[arm], n) for arm in (False, True) for n in (KEY, SHORT)}
    anchor_at = sorted(anchors)
    anchored = dict(anchor_at)
    out = {}
    j = 0
    delta = anchor_at[0][1] if anchor_at else obase - rbase
    for addr, arm, size in funcs:
        while j < len(anchor_at) and anchor_at[j][0] <= addr:
            delta = anchor_at[j][1]
            j += 1
        if addr in anchored:
            out[addr] = addr + anchored[addr]
            continue
        unit = 4 if arm else 2
        off = addr - rbase
        if off < 0 or off >= len(rdata):
            continue
        want = (addr + delta - obase) // unit
        n_cmp = max(2, min(size, COMPARED) // unit)
        ref_run = rmask[arm][off // unit:off // unit + n_cmp]
        hit = None
        for n in (KEY, SHORT):
            if size < n * unit or off + n * unit > len(rdata):
                continue
            k = key_of(rdata, off, arm, n)
            ks = okeys[(arm, n)]
            lo, hi = max(0, want - NEAR // unit), min(len(ks), want + NEAR // unit + 1)
            hits = np.nonzero(ks[lo:hi] == k)[0] if lo < hi else []
            if len(hits):
                best = lo + int(hits[np.argmin(np.abs(lo + hits - want))])
                # (A short key can be anywhere: the rest of the function must agree too.)
                if n == KEY or similar(ref_run, omask[arm][best:best + n_cmp]) >= SAME:
                    hit = best
                    break
        if hit is not None:
            out[addr] = obase + hit * unit
            delta = out[addr] - addr
    # Functions found out of order (a short key matched elsewhere) are dropped: the module's code
    # keeps its order.
    kept = dict(increasing(sorted(out.items())))
    out = {a: t for a, t in out.items() if kept.get(a) == t}
    # Second pass, for those changed a little (Platinum's own additions): the most alike place
    # between where the functions either side of it were found, starting with the same
    # instruction.
    order = [a for a, _arm, _s in funcs]
    for i, (addr, arm, size) in enumerate(funcs):
        if addr in out or size < 16:
            continue
        unit = 4 if arm else 2
        off = addr - rbase
        if off < 0 or off >= len(rdata):
            continue
        prev = next((out[a] for a in reversed(order[:i]) if a in out), None)
        nxt = next((out[a] for a in order[i + 1:] if a in out), None)
        if prev is None and nxt is None:
            continue
        lo = (prev - obase) // unit + 1 if prev is not None else (nxt - obase) // unit - FUZZY // unit
        hi = (nxt - obase) // unit if nxt is not None else (prev - obase) // unit + FUZZY // unit
        lo, hi = max(0, lo), min(len(omask[arm]), hi)
        n_cmp = max(2, min(size, COMPARED) // unit)
        ref_run = rmask[arm][off // unit:off // unit + n_cmp]
        om = omask[arm]
        # (Its first instruction alike: a push of other registers still counts.)
        shift = 16 if arm else 8
        # (A wide gap: only next to the functions either side, where it most likely is.)
        spans = [(lo, hi)] if hi - lo <= WINDOW // unit else [(lo, lo + WINDOW // unit), (hi - WINDOW // unit, hi)]
        starts = np.concatenate([np.nonzero((om[a:b] >> shift) == (ref_run[0] >> shift))[0] + a for a, b in spans])
        # (Between its own two neighbours, both found, it can have changed more: rewritten in places.)
        tight = i > 0 and i + 1 < len(order) and order[i - 1] in out and order[i + 1] in out
        best = None
        for c in starts:
            score = similar(ref_run, om[c:c + n_cmp])
            if score >= (TIGHT if tight else BETWEEN) and (best is None or score > best[0]):
                best = (score, int(c))
        if best:
            out[addr] = obase + best[1] * unit
    return mod, other_mod, out


def increasing(points):
    """The longest run of anchors whose other-ROM addresses increase with the reference's."""
    import bisect
    tails, idx, prev = [], [], [-1] * len(points)
    for i, (_a, t) in enumerate(points):
        p = bisect.bisect_left(tails, t)
        if p == len(tails):
            tails.append(t)
            idx.append(i)
        else:
            tails[p] = t
            idx[p] = i
        prev[i] = idx[p - 1] if p else -1
    out, i = [], idx[-1] if idx else -1
    while i >= 0:
        out.append(points[i])
        i = prev[i]
    return out[::-1]


def templates(ref, other, names, mapped, pair_of):
    """Functions pointed to from a table of three (an application's init, main and exit, as
    OverlayManager templates hold them): where both neighbours are mapped, the other ROM's table
    with those two says exactly where the middle one is. Fixes `mapped` in place; returns how many
    it found or corrected, and how many it confirmed."""
    thumb = {}
    for (mod, a), (arm, _n) in names.items():
        if not arm:
            thumb.setdefault(a | 1, []).append(mod)

    def resolve(ptr, near):
        mods = thumb.get(ptr, ())
        mod = near if near in mods else "main" if "main" in mods else mods[0] if len(mods) == 1 else None
        return (mod, ptr - 1) if mod else None

    other_words = {m: np.frombuffer(d[:len(d) & ~3], dtype="<u4") for m, (_b, d) in other.mods.items()}
    ptrs = np.array(sorted(thumb), dtype=np.uint32)
    fixed = confirmed = 0
    for mod, (_base, data) in ref.mods.items():
        w = np.frombuffer(data[:len(data) & ~3], dtype="<u4")
        ok = np.isin(w, ptrs)
        for i in np.nonzero(ok[:-2] & ok[1:-1] & ok[2:])[0] + 1:
            a, f, b = (resolve(int(w[k]), mod) for k in (i - 1, i, i + 1))
            if not (a and f and b) or a not in mapped or b not in mapped or f[0] not in pair_of:
                continue
            ta, tb = mapped[a][1] | 1, mapped[b][1] | 1
            hits = []
            for ow in other_words.values():
                j = np.nonzero((ow[:-2] == ta) & (ow[2:] == tb))[0]
                hits += [int(ow[k + 1]) - 1 for k in j if int(ow[k + 1]) & 1]
            if len(set(hits)) != 1:
                continue
            want = (pair_of[f[0]], hits[0])
            if mapped.get(f) == want:
                confirmed += 1
            else:
                mapped[f] = want
                fixed += 1
    return fixed, confirmed


def bl_targets(base, data, start, end, arm):
    """The targets of the calls (BL, BLX) in [start, end), in order."""
    out = []
    o, stop = start - base, min(end - base, len(data))
    if o < 0:
        return out
    if arm:
        while o + 4 <= stop:
            w = int.from_bytes(data[o:o + 4], "little")
            if (w >> 24) == 0xEB or (w >> 25) == 0x7D:
                off = (w & 0xFFFFFF) - ((w & 0x800000) << 1)
                t = base + o + 8 + off * 4
                out.append((t | 1) + ((w >> 23) & 2) if (w >> 25) == 0x7D else t)
            o += 4
        return out
    while o + 4 <= stop:
        h1 = data[o] | data[o + 1] << 8
        h2 = data[o + 2] | data[o + 3] << 8
        if h1 >> 11 == 0x1E and h2 >> 11 in (0x1F, 0x1D):
            off = ((h1 & 0x7FF) << 12 | (h2 & 0x7FF) << 1)
            off -= (off & 0x400000) << 1
            t = base + o + 4 + off
            out.append(t & ~3 if h2 >> 11 == 0x1D else t | 1)
            o += 4
        else:
            o += 2
    return out


def calls(ref, other, names, mapped):
    """Functions called by those found, found where the other ROM's same function makes the same
    call (its n-th call, when both make as many): small ones too, which have no key of their own.
    Adds to `mapped`; returns how many."""
    starts = {}  # ref: (address | thumb) -> [(mod, address)]
    for (mod, a), (arm, _n) in names.items():
        starts.setdefault(a if arm else a | 1, []).append((mod, a))
    ends = {}
    for (mod, a) in names:
        ends.setdefault(mod, []).append(a)
    for v in ends.values():
        v.sort()
    oends = {}
    for (om, ta) in mapped.values():
        oends.setdefault(om, []).append(ta)
    for v in oends.values():
        v.sort()
    import bisect

    def end_of(lst, a, limit):
        i = bisect.bisect_right(lst, a)
        return min(lst[i] if i < len(lst) else a + limit, a + limit)

    votes = {}
    for (mod, a), (om, ta) in list(mapped.items()):
        arm = names[(mod, a)][0]
        if mod not in ref.mods or om not in other.mods:
            continue
        rb, rd = ref.mods[mod]
        ob, od = other.mods[om]
        x = bl_targets(rb, rd, a, end_of(ends[mod], a, 0x1000), arm)
        y = bl_targets(ob, od, ta, end_of(oends[om], ta, 0x1000), arm)
        if not x or len(x) != len(y):
            continue
        for t, u in zip(x, y):
            for f in starts.get(t, ()):
                if f[0] in (mod, "main", "itcm"):
                    tm = om if f[0] == mod else f[0]
                    votes.setdefault(f, set()).add((tm, u & ~1))
    added = 0
    for f, v in votes.items():
        if f not in mapped and len(v) == 1:
            mapped[f] = next(iter(v))
            added += 1
    return added


def module_key(module):
    m = re.match(r"ov0*(\d+)$", module)
    return "ov%s" % m.group(1) if m else module


def main():
    args = [a for a in sys.argv[1:]]
    jobs = os.cpu_count() or 1
    if "-j" in args:
        i = args.index("-j")
        jobs = int(args[i + 1])
        del args[i:i + 2]
    if len(args) != 4:
        sys.exit(__doc__)
    table, ref_path, other_path, out_path = args
    ref, other = Modules(ref_path), Modules(other_path)
    G["ref"], G["other"] = ref.mods, other.mods
    rows = []
    for line in open(table):
        p = line.split()
        if len(p) >= 5:
            rows.append((int(p[0], 16), p[1] == "a", module_key(p[2]), p[4]))
    by_module = {}
    for addr, arm, mod, name in sorted(set(rows)):
        by_module.setdefault(mod, []).append((addr, arm, name))
    print(f"indexing {other.code}'s code ({len(other.mods)} modules)", file=sys.stderr)
    index = Index(other.mods, KEY)
    # Anchors: functions found in one place only.
    funcs_of, anchors = {}, {}
    for mod, fs in by_module.items():
        if mod not in ref.mods:
            continue
        base, data = ref.mods[mod]
        funcs = []
        for i, (addr, arm, _name) in enumerate(fs):
            nxt = fs[i + 1][0] if i + 1 < len(fs) else base + len(data)
            funcs.append((addr, arm, nxt - addr))
        funcs_of[mod] = funcs
        found = []
        for addr, arm, size in funcs:
            off = addr - base
            if size < MIN_SIZE or off < 0 or off + 4 * KEY > len(data):
                continue
            c = index.find(key_of(data, off, arm, KEY), arm)
            if len(c) == 1:
                found.append((addr, c[0]))
        anchors[mod] = found
    # Each module paired with the one most of its anchors are in; anchors out of order dropped.
    jobs_list = []
    for mod, found in anchors.items():
        votes = {}
        for _addr, (om, _ta) in found:
            votes[om] = votes.get(om, 0) + 1
        if not votes:
            continue
        om = max(votes, key=votes.get)
        pts = increasing([(a, ta) for a, (m, ta) in found if m == om])
        if len(pts) < 2 and len(funcs_of[mod]) > 4:
            continue
        jobs_list.append((mod, om, funcs_of[mod], [(a, ta - a) for a, ta in pts]))
    # (fork: the workers share the ROMs already read, G, instead of reading them again.)
    with get_context("fork").Pool(jobs) as pool:
        results = pool.map(match_module, jobs_list, chunksize=1)
    names = {(mod, a): (arm, n) for mod, fs in by_module.items() for a, arm, n in fs}
    mapped = {(mod, a): (om, ta) for mod, om, found in results for a, ta in found.items()}
    pair_of = {mod: om for mod, om, _f, _a in jobs_list}
    fixed, confirmed = templates(ref, other, names, mapped, pair_of)
    called = 0
    for _round in range(4):
        n = calls(ref, other, names, mapped)
        called += n
        if not n:
            break
    # A place outside the module it was taken in is not one (a call into another overlay).
    for f, (om, ta) in list(mapped.items()):
        ob, od = other.mods[om]
        if not ob <= ta < ob + len(od):
            del mapped[f]
    # Two functions taken for the same place: the one more alike stays.
    by_target = {}
    for f, t in mapped.items():
        by_target.setdefault(t, []).append(f)
    dropped = 0
    for (om, ta), fs in by_target.items():
        if len(fs) < 2:
            continue

        def likeness(f, om=om, ta=ta):
            arm = names[f][0]
            rb, rd = ref.mods[f[0]]
            ob, od = other.mods[om]
            mask = mask_arm if arm else mask_thumb
            n = COMPARED
            return similar(mask(rd[f[1] - rb:f[1] - rb + n]), mask(od[ta - ob:ta - ob + n]))
        keep = max(fs, key=likeness)
        for f in fs:
            if f != keep:
                del mapped[f]
                dropped += 1
    out_rows, total = [], sum(len(f) for f in funcs_of.values())
    for (mod, a), (om, ta) in mapped.items():
        arm, name = names[(mod, a)]
        out_rows.append((ta, "a" if arm else "t", om, "?" if om.startswith("ov") else "-", name))
    with open(out_path, "w") as f:
        for r in sorted(out_rows):
            f.write("%08x %s %s %s %s\n" % r)
    paired = ", ".join(f"{m}->{om}" for m, om, _f, _a in sorted(jobs_list, key=lambda j: (len(j[0]), j[0]))[:12])
    print(f"{other.code}: {len(out_rows)} of {total} functions mapped ({len(jobs_list)} modules paired: {paired}, ...); "
          f"by their tables of pointers: {confirmed} confirmed, {fixed} found or corrected; {called} by the calls of those found; {dropped} dropped (another in the same place)")


if __name__ == "__main__":
    main()
