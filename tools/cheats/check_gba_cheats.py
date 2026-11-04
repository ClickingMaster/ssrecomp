#!/usr/bin/env python3
"""Checks the addresses in frontend/gba_cheats.h against pret's symbol files (the symbols branches
of pokeemerald and pokefirered, commits dba968c6 and 3b401eaf):

    git clone --depth 1 --branch symbols https://github.com/pret/pokeemerald
    git clone --depth 1 --branch symbols https://github.com/pret/pokefirered
    python3 tools/cheats/check_gba_cheats.py pokeemerald pokefirered
"""
import pathlib, re, sys

header = (pathlib.Path(__file__).resolve().parents[2] / "frontend/gba_cheats.h").read_text()
def syms(path):
    d = {}
    for line in open(path):
        p = line.split()
        if len(p) >= 4: d.setdefault(p[3], int(p[0], 16))
    return d

em = syms(pathlib.Path(sys.argv[1]) / "pokeemerald.sym")
fr = {n: syms(pathlib.Path(sys.argv[2]) / (n + ".sym")) for n in ("pokefirered", "pokefirered_rev1", "pokeleafgreen", "pokeleafgreen_rev1")}
ram = ["gSaveBlock1Ptr", "gSaveBlock2Ptr", "gPlayerParty", "gPlayerPartyCount", "gEnemyParty", "gBattleMons",
       "gBattlersCount", "gBattlerPartyIndexes", "gBattleTypeFlags"]
def block(name):
    m = re.search(r"inline const Layout& " + name + r"\(\)\s*\{\s*static const Layout l = \{(.*?)\};", header, re.S)
    return [int(x, 16) for x in re.findall(r"0x[0-9a-fA-F]{7,8}", m.group(1))]
bad = 0
for fam, table, s in (("Emerald", block("Emerald"), em), ("FireRedLeafGreen", block("FireRedLeafGreen"), fr["pokefirered"])):
    want = [s[n] for n in ram]
    if table[:len(ram)] != want:
        print(fam, "RAM differs", [hex(x) for x in table[:len(ram)]], [hex(x) for x in want]); bad += 1
    # (gMain is written as gMain + 0x439, the byte with inBattle)
    if s["gMain"] not in table or s["sWildEncountersDisabled"] not in table:
        print(fam, "gMain or sWildEncountersDisabled differs"); bad += 1
# FireRed and LeafGreen versions share their RAM
for n, s in fr.items():
    for r in ram + ["gMain", "sWildEncountersDisabled"]:
        if s[r] != fr["pokefirered"][r]: print(n, r, "differs from FireRed 1.0"); bad += 1
vers = re.findall(r'\{ "(\w{4})", (\d), "[^"]*", (?:true|false), (0x[0-9a-f]+), (0x[0-9a-f]+), (0x[0-9a-f]+) \}', header)
files = {("BPEE", 0): em, ("BPRE", 0): fr["pokefirered"], ("BPRE", 1): fr["pokefirered_rev1"],
         ("BPGE", 0): fr["pokeleafgreen"], ("BPGE", 1): fr["pokeleafgreen_rev1"], ("PUVV", 0): fr["pokefirered"]}
for code, rev, c, b, e in vers:
    s = files[(code, int(rev))]
    if [int(c, 16), int(b, 16), int(e, 16)] != [s["MapGridGetCollisionAt"], s["Cmd_handleballthrow"], s["Cmd_getexp"]]:
        print(code, rev, "functions differ"); bad += 1
print("versions checked:", len(vers))
sys.exit(1 if bad else 0)
