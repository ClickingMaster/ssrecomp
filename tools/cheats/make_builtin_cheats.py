#!/usr/bin/env python3
"""Makes frontend/builtin_cheats.h: the cheats built into Fulldex for the DS games.

For every DS game in the launcher, in every release the cheat files cover (USA, Europe,
Spain, Germany, France, Italy), the same 15 most asked-for cheats, picked by name from
libretro-database's cheat files (cht/Nintendo - Nintendo DS, which carry DeadSkullzJr's
database). None is on until the player turns it on.

    git clone --filter=blob:none --sparse https://github.com/libretro/libretro-database
    git -C libretro-database checkout 2fdd069d45883449cecf97deddd493492e77ff29
    git -C libretro-database sparse-checkout set --no-cone '/cht/Nintendo - Nintendo DS/*Pokemon*'
    python3 tools/cheats/make_builtin_cheats.py "libretro-database/cht/Nintendo - Nintendo DS" frontend/builtin_cheats.h
"""
import os, re, sys

LIBRETRO_COMMIT = "2fdd069d45883449cecf97deddd493492e77ff29"

def load(fn):
    t = open(fn, encoding="utf-8", errors="replace").read()
    d = {}
    for m in re.finditer(r'cheat(\d+)_(desc|code|enable) = "?(.*?)"?\n', t):
        d.setdefault(int(m.group(1)), {})[m.group(2)] = m.group(3)
    return [d[k] for k in sorted(d)]

# (the ROM's game code, its cheat file, which family of games)
FILES = [
 ("IRBO","Pokemon - Black Version (USA, Europe).cht","BW"),
 ("IRBS","Pokemon - Edicion Negra (Spain).cht","BW"),
 ("IRBD","Pokemon - Schwarze Edition (Germany).cht","BW"),
 ("IRBF","Pokemon - Version Noire (France).cht","BW"),
 ("IRBI","Pokemon - Versione Nera (Italy).cht","BW"),
 ("IRAO","Pokemon - White Version (USA, Europe).cht","BW"),
 ("IRAS","Pokemon - Edicion Blanca (Spain).cht","BW"),
 ("IRAD","Pokemon - Weisse Edition (Germany).cht","BW"),
 ("IRAF","Pokemon - Version Blanche (France).cht","BW"),
 ("IRAI","Pokemon - Versione Bianca (Italy).cht","BW"),
 ("IREO","Pokemon - Black Version 2 (USA, Europe).cht","B2W2"),
 ("IRES","Pokemon - Edicion Negra 2 (Spain).cht","B2W2"),
 ("IRED","Pokemon - Schwarze Edition 2 (Germany).cht","B2W2"),
 ("IREF","Pokemon - Version Noire 2 (France).cht","B2W2"),
 ("IREI","Pokemon - Versione Nera 2 (Italy).cht","B2W2"),
 ("IRDO","Pokemon - White Version 2 (USA, Europe).cht","B2W2"),
 ("IRDS","Pokemon - Edicion Blanca 2 (Spain).cht","B2W2"),
 ("IRDD","Pokemon - Weisse Edition 2 (Germany).cht","B2W2"),
 ("IRDF","Pokemon - Version Blanche 2 (France).cht","B2W2"),
 ("IRDI","Pokemon - Versione Bianca 2 (Italy).cht","B2W2"),
 ("IPKE","Pokemon - HeartGold Version (USA).cht","HGSS"),
 ("IPKP","Pokemon - HeartGold Version (Europe) (Rev 10).cht","HGSS"),
 ("IPKS","Pokemon - Edicion Oro HeartGold (Spain).cht","HGSS"),
 ("IPKD","Pokemon - Goldene Edition HeartGold (Germany).cht","HGSS"),
 ("IPKF","Pokemon - Version Or HeartGold (France).cht","HGSS"),
 ("IPKI","Pokemon - Versione Oro HeartGold (Italy).cht","HGSS"),
 ("IPGE","Pokemon - SoulSilver Version (USA).cht","HGSS"),
 ("IPGP","Pokemon - SoulSilver Version (Europe) (Rev 10).cht","HGSS"),
 ("IPGS","Pokemon - Edicion Plata SoulSilver (Spain).cht","HGSS"),
 ("IPGD","Pokemon - Silberne Edition SoulSilver (Germany).cht","HGSS"),
 ("IPGF","Pokemon - Version Argent SoulSilver (France).cht","HGSS"),
 ("IPGI","Pokemon - Versione Argento SoulSilver (Italy).cht","HGSS"),
 ("CPUE","Pokemon - Platinum Version (USA).cht","PT"),
 ("CPUP","Pokemon - Platinum Version (Europe) (Rev 10).cht","PT"),
 ("CPUS","Pokemon - Edicion Platino (Spain).cht","PT"),
 ("CPUD","Pokemon - Platin-Edition (Germany).cht","PT"),
 ("CPUF","Pokemon - Version Platine (France).cht","PT"),
 ("CPUI","Pokemon - Versione Platino (Italy).cht","PT"),
 ("ADAE","Pokemon - Diamond Version (USA) (Rev 5).cht","DP"),
 ("ADAP","Pokemon - Diamond Version (Europe) (Rev 13).cht","DP"),
 ("ADAS","Pokemon - Edicion Diamante (Spain) (Rev 5).cht","DP"),
 ("ADAD","Pokemon - Diamant-Edition (Germany) (Rev 5).cht","DP"),
 ("ADAF","Pokemon - Version Diamant (France) (Rev 5).cht","DP"),
 ("ADAI","Pokemon - Versione Diamante (Italy) (Rev 5).cht","DP"),
 ("APAE","Pokemon - Pearl Version (USA) (Rev 5).cht","DP"),
 ("APAP","Pokemon - Pearl Version (Europe) (Rev 13).cht","DP"),
 ("APAS","Pokemon - Edicion Perla (Spain) (Rev 5).cht","DP"),
 ("APAD","Pokemon - Perl-Edition (Germany) (Rev 5).cht","DP"),
 ("APAF","Pokemon - Version Perle (France) (Rev 5).cht","DP"),
 ("APAI","Pokemon - Versione Perla (Italy) (Rev 5).cht","DP"),
]


H_ITEMS = r'All Items Codes'
H_EXP = r'Experience Multiplier( Codes)?'
SLOTS = [
 [(r'Walk Through Walls( v1)?( \(.*\))?', 'Walk Through Walls')],
 [(r'Max/Infinite Money', 'Max Money')],
 [(r'All Medicines', 'All Medicines (Rare Candy)'), (H_ITEMS+'>Medicines', 'All Medicines (Rare Candy)')],
 [(H_ITEMS+r'>Poke Balls \(Default\)', 'All Poke Balls (Master Ball)'), (r'All Items', 'All Items')],
 [(r'Wild Shiny Encounters( v1)?', 'Wild Shiny Encounters'), (r'(Hold Select to )?Encounter Shiny Pokemon', 'Wild Shiny Encounters'), (r'Wild Pokemon are Shiny', 'Wild Shiny Encounters'), (r'Poke Radar Shiny Pokemon', 'Poke Radar Always Shiny')],
 [(r'100% Catch Rate', 'Always Catch Pokemon'), (r'100% Capture Ratio', 'Always Catch Pokemon')],
 [(r'Infinite Health', 'Infinite HP in Battle'), (r'Infinite HP', 'Infinite HP in Battle'), (r'Max/Infinite HP', 'Infinite HP in Battle'), (r'HP Never Decrease', 'Infinite HP in Battle')],
 [(r'PP Never Decreases?', 'Infinite PP'), (r'Infinite PP', 'Infinite PP')],
 [(H_EXP+r'>x?2', 'Double EXP')],
 [(r'Encounter Rate Codes>No Encounters', 'No Wild Encounters'), (r'No Wild Pokemon Encounters( v1)?', 'No Wild Encounters'), (r'No Encounter, Hold L for Instant Encounter', 'No Wild Encounters'), (r'No Wild Battles if Running', 'No Wild Battles When Running'), (r'Repel 255 Steps', 'Repel Lasts 255 Steps'), (r'Enable 250 Repel Steps', 'Repel Lasts 250 Steps')],
 [(r'Fast Egg Hatch( v1)?', 'Fast Egg Hatch'), (r'Egg Hatch', 'Force Egg Hatch')],
 [(r'All Badges', 'All Badges'), (r'Complete Badges', 'All Badges')],
 [(r'All TMs', 'All TMs'), (H_ITEMS+r'>TMs/HMs', 'All TMs and HMs'), (H_ITEMS+r'>TMs and HMs', 'All TMs and HMs')],
 [(r'Complete National Pokedex', 'Complete National Pokedex'), (r'Complete Pokedex', 'Complete Pokedex'), (r'All Pokemon Seen and Caught.*', 'Complete Pokedex')],
 [(r'Fly Anywhere', 'Fly From/To Anywhere'), (r'Fly from/to Anywhere', 'Fly From/To Anywhere'), (r'Movement Speed x2', 'Double Walking Speed'), (r'x2 Movement Speed', 'Double Walking Speed'), (r'Walking Speed x2', 'Double Walking Speed'), (r'Double Walking Speed', 'Double Walking Speed'), (r'Double Movement Speed', 'Double Walking Speed'), (r'Have Bike Movement Speed', 'Walk at Bike Speed')],
]
def find(L, spec):
    if '>' in spec:
        h, n = spec.split('>', 1)
        for i, c in enumerate(L):
            if re.fullmatch(h, c['desc'], re.I) and not c.get('code'):
                for j in range(i+1, len(L)):
                    if not L[j].get('code'): break
                    if re.fullmatch(n, L[j]['desc'], re.I): return L[j]
        return None
    for c in L:
        if c.get('code') and re.fullmatch(spec, c['desc'], re.I): return c
    return None

PAD = ["A", "B", "SELECT", "START", "RIGHT", "LEFT", "UP", "DOWN", "R", "L"]

def activators(code):
    """The buttons a code waits for (its key-check lines), in order, without repeats."""
    w = code.split("+"); out = []
    for i in range(0, len(w) - 1, 2):
        a, v = w[i].upper(), w[i + 1].upper()
        if a[0] != "9": continue
        addr = int(a[1:], 16); mask = int(v[:4], 16); val = int(v[4:], 16)
        if addr == 0x4000130:
            keys = [PAD[b] for b in range(10) if not (mask >> b) & 1 and not (val >> b) & 1]
        elif addr in (0x27FFFA8, 0x23FFFA8):
            keys = [k for b, k in ((10, "X"), (11, "Y")) if not (mask >> b) & 1 and not (val >> b) & 1]
        else:
            continue
        # L and R read best in the order players say them.
        keys.sort(key=lambda k: ["L", "R"].index(k) if k in ("L", "R") else -1)
        k = "+".join(keys)
        if k and k not in out: out.append(k)
    return out

EXTRA = {
    "No Wild Battles When Running": "Hold B to run.",
}

def note(name, code):
    parts = []
    keys = activators(code)
    if len(keys) == 1: parts.append("Button: " + keys[0] + ".")
    elif keys: parts.append("Buttons: " + " / ".join(keys) + ".")
    if name in EXTRA: parts.append(EXTRA[name])
    return " ".join(parts)

def c_str(s):
    s.encode("ascii")
    # (The menus' font has capitals, digits and : - + . / < > ! ( ) only: see Shown in cheats.h.)
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'

def main(db, out_path):
    games = []
    for gc, fn, fam in FILES:
        L = load(os.path.join(db, fn))
        codes = []
        for lst in SLOTS:
            for spec, disp in lst:
                hit = find(L, spec)
                if hit: break
            if not hit: continue
            w = hit["code"].split("+")
            assert len(w) % 2 == 0 and all(re.fullmatch("[0-9A-Fa-f]{8}", x) for x in w), (gc, disp)
            lines = "\n".join(w[i].upper() + " " + w[i + 1].upper() for i in range(0, len(w), 2))
            codes.append((disp, note(disp, hit["code"]), lines, hit["desc"]))
        assert len(codes) >= 14, (gc, len(codes))
        for disp, nt, _, _ in codes:
            assert re.fullmatch(r"[A-Za-z0-9 :+./<>!()-]*", disp + nt) and len(disp) <= 32, (gc, disp, nt)
        games.append((gc, fn, codes))
    o = []
    o.append("#pragma once")
    o.append("// The cheats built into Fulldex for the DS games: for each release (by the ROM's own game")
    o.append("// code), the same 15 most asked-for codes, from libretro-database's cheat files")
    o.append("// (DeadSkullzJr's database), commit " + LIBRETRO_COMMIT + ".")
    o.append("// CHEATS in the pause menu shows them as the folder POPULAR CHEATS, every one off until the")
    o.append("// player turns it on. Made by tools/cheats/make_builtin_cheats.py: don't edit by hand.")
    o.append("#include <cstring>")
    o.append("")
    o.append("namespace BuiltinCheats")
    o.append("{")
    o.append("struct Code { const char* name; const char* note; const char* lines; };")
    o.append("struct Game { const char* code; const Code* codes; int count; };")
    o.append("")
    for gc, fn, codes in games:
        o.append("// " + fn[:-4])
        o.append("static const Code " + gc + "[] = {")
        for disp, nt, lines, src in codes:
            o.append("    { " + c_str(disp) + ", " + c_str(nt) + ", // " + src)
            for ln in lines.split("\n"):
                o.append("      " + c_str(ln + "\n"))
            o[-1] += " },"
        o.append("};")
    o.append("")
    o.append("static const Game Games[] = {")
    for gc, fn, codes in games:
        o.append("    { " + c_str(gc) + ", " + gc + ", " + str(len(codes)) + " },")
    o.append("};")
    o.append("")
    o.append("inline const Game* Find(const char* code)")
    o.append("{")
    o.append("    for (const Game& g : Games)")
    o.append("        if (std::strncmp(g.code, code, 4) == 0) return &g;")
    o.append("    return nullptr;")
    o.append("}")
    o.append("} // namespace BuiltinCheats")
    open(out_path, "w", newline="\n").write("\n".join(o) + "\n")
    print("%d games, %d codes -> %s" % (len(games), sum(len(c) for _, _, c in games), out_path))

if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
