#!/usr/bin/env python3
"""Puts the widescreen patches (our own code only) into the player program: writes a C++
header with each game's IPS patch and its .safety list (tools/hookpatch.py --safety).

  embed_patches.py PATCH_DIR OUT.h

PATCH_DIR holds GAME-wide.ips and GAME-wide.safety for each of GAMES (setup.sh puts them in
~/pokemon-gba-wide/build/patches).
"""
import os
import sys

GAMES = ['emerald', 'firered', 'firered11', 'leafgreen', 'leafgreen11',
         'ruby', 'ruby11', 'ruby10', 'sapphire', 'sapphire11', 'sapphire10', 'rubyde', 'sapphirede',
         'rubyfr', 'rubyes', 'rubyit', 'rubyjp', 'sapphirefr', 'sapphirees', 'sapphireit', 'sapphirejp']


def main():
    src, out = sys.argv[1], sys.argv[2]
    parts = ['// Made by tools/embed_patches.py from %s: do not edit.' % os.path.basename(src.rstrip('/')),
             '#pragma once', '#include <cstddef>', '',
             'struct WidePatch', '{', '    const char* name;', '    const unsigned char* ips;',
             '    size_t ipsSize;', '    const char* safety;', '};', '']
    for g in GAMES:
        data = open(os.path.join(src, g + '-wide.ips'), 'rb').read()
        if not data.startswith(b'PATCH') or not data.endswith(b'EOF'):
            sys.exit('%s-wide.ips is not an IPS patch' % g)
        rows = [', '.join('0x%02X' % b for b in data[i:i + 16]) for i in range(0, len(data), 16)]
        parts.append('static const unsigned char k%sIps[] = {\n    %s,\n};' % (g.capitalize(), ',\n    '.join(rows)))
        safety = open(os.path.join(src, g + '-wide.safety')).read()
        lines = ''.join('    "%s\\n"\n' % l for l in safety.splitlines() if l.strip())
        parts.append('static const char k%sSafety[] =\n%s;' % (g.capitalize(), lines.rstrip('\n')))
        parts.append('')
    parts.append('static const WidePatch kWidePatches[] = {')
    for g in GAMES:
        parts.append('    { "%s", k%sIps, sizeof k%sIps, k%sSafety },' % (g, g.capitalize(), g.capitalize(), g.capitalize()))
    parts.append('};')
    tmp = out + '.tmp'
    with open(tmp, 'w') as f:
        f.write('\n'.join(parts) + '\n')
    os.replace(tmp, out)


if __name__ == '__main__':
    main()
