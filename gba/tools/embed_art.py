#!/usr/bin/env python3
"""Builds the games' art (art/<game>.jpg: square, 512x512, as the DS launcher's icons) into the
player program:

  embed_art.py ART_DIR OUT.h

OUT.h holds each .jpg as it is (the program reads them with stb_image) and a table by name
(emerald: the name frontend/games.h gives the game's art). Art for a game is optional: without
it the list shows the game's colour."""
import os
import sys


def main():
    art_dir, out = sys.argv[1], sys.argv[2]
    names = sorted(n for n in os.listdir(art_dir) if n.lower().endswith('.jpg')) if os.path.isdir(art_dir) else []
    lines = ['// Made by tools/embed_art.py from art/: do not edit.', '#pragma once', '',
             'struct WideArt', '{', '    const char* code;', '    const unsigned char* jpg;', '    unsigned size;', '};', '']
    table = []
    for n in names:
        code = os.path.splitext(n)[0]
        if not code.isalnum():
            sys.exit('art/%s: letters and digits only, e.g. emerald.jpg' % n)
        data = open(os.path.join(art_dir, n), 'rb').read()
        if data[:2] != b'\xff\xd8':
            sys.exit('art/%s is not a JPEG' % n)
        lines.append('static const unsigned char kArt_%s[] = {' % code)
        for i in range(0, len(data), 24):
            lines.append('    ' + ','.join(str(b) for b in data[i:i + 24]) + ',')
        lines.append('};')
        table.append('    { "%s", kArt_%s, %d },' % (code, code, len(data)))
    lines.append('static const WideArt kWideArt[] = {')
    lines += table or ['    { "", nullptr, 0 },']
    lines.append('};')
    tmp = out + '.tmp'
    with open(tmp, 'w') as f:
        f.write('\n'.join(lines) + '\n')
    os.replace(tmp, out)


if __name__ == '__main__':
    main()
