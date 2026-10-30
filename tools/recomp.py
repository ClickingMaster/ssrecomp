#!/usr/bin/env python3
"""Static recompiler for the pokeheartgold ARM9 build output.

Reads the linked NEF (for section addresses and $a/$t/$d mapping symbols) and
the per-section .sbin images, and emits C++ in which every ARM9 instruction is
a labelled statement inside a "chunk" function. The melonDS core supplies the
hardware and the cycle model; see runtime/recomp_gen.h for the macros.
"""
import argparse
import filecmp
import shutil
import collections
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import recomp_inline

try:
    from elftools.elf.elffile import ELFFile   # only needed with a build directory
except ImportError:
    ELFFile = None

CHUNK_MAX = 256        # instructions per chunk function
FILE_MAX = 24000       # instructions per generated source file


def parse_instr_tables(path):
    text = open(path).read()
    tables = {}
    for m in re.finditer(r'INSTRFUNC_PROTO\((\w+)\[(\d+)\]\)\s*=\s*\{(.*?)\};', text, re.S):
        body = re.sub(r'//[^\n]*', '', m.group(3))
        names = [n.strip() for n in body.split(',') if n.strip()]
        assert len(names) == int(m.group(2)), (m.group(1), len(names))
        tables[m.group(1)] = names
    return tables['ARMInstrTable'], tables['THUMBInstrTable']


class Section:
    def __init__(self, name, base, image):
        self.name = name
        self.base = base
        self.image = image
        self.ranges = []   # (start, end, thumb)


def autoload_image(main_image, base):
    """The ITCM block of the main program. Autoload info sits at the end of the main
    image: (address, size, bss size) per block, with the blocks stored back to back
    after the static module."""
    pos = len(main_image) - 0x24
    entries = [struct.unpack_from('<3I', main_image, pos + i * 12) for i in range(2)]
    data_off = len(main_image) - 0x24 - sum(e[1] for e in entries)
    for addr, size, _ in entries:
        if addr == base:
            return main_image[data_off:data_off + size]
        data_off += size
    raise SystemExit('ITCM autoload block not found')


def bios_section(melonds_src):
    """The interrupt path of melonDS's built-in ARM9 BIOS (FreeBIOS, in FreeBIOS.cpp),
    which runs a few hundred times a frame (an HBlank interrupt every line): the vector at
    0x18 and the handler it branches to. As for any section, the dispatcher only enters it
    after comparing the bytes with the BIOS actually loaded."""
    src = open(os.path.join(melonds_src, 'FreeBIOS.cpp')).read()
    start = src.index('bios_arm9_bin')
    body = src[src.index('{', start):src.index('}', start)]
    image = bytes(int(x, 16) for x in re.findall(r'0x([0-9A-Fa-f]{2})', body))
    assert len(image) == 0x1000, len(image)
    sec = Section('bios9', 0xFFFF0000, image)
    sec.ranges = [(0xFFFF0018, 0xFFFF001C, False), (0xFFFF06DC, 0xFFFF06FC, False)]
    sec.named = {0xFFFF0018, 0xFFFF06DC}
    return sec


def load_sections_from_layout(layout_path, rom_path):
    """The same sections, from the player's own ROM: the code bytes come from the ROM,
    and where code starts and ends (which the decompilation's symbols tell) from the
    layout file made by tools/make_layout.py, which holds addresses only."""
    import hashlib
    import json
    from ndsrom import read_rom
    layout = json.load(open(layout_path))
    main_image, overlays = read_rom(rom_path)
    sections = []
    for entry in layout['sections']:
        src = entry['source']
        if src == 'main':
            image = main_image
        elif src == 'itcm':
            image = autoload_image(main_image, entry['base'])
        else:
            if src not in overlays:
                raise SystemExit(f'This ROM has no overlay {src}: it is not {layout["game"]}.')
            image = overlays[src][1]
        if hashlib.sha1(image).hexdigest() != entry['sha1']:
            raise SystemExit(f'Section {entry["name"]} of this ROM differs from {layout["game"]}: '
                             'wrong game, region or revision, or a modified ROM.')
        sec = Section(entry['name'], entry['base'], image)
        sec.named = set(entry['named'])
        sec.ranges = [tuple(r) for r in entry['ranges']]
        sections.append(sec)
    return sections


def load_sections(build_dir):
    nef = [f for f in os.listdir(build_dir) if f.endswith('.nef')]
    assert len(nef) == 1, nef
    maps = collections.defaultdict(list)
    named = collections.defaultdict(set)
    with open(os.path.join(build_dir, nef[0]), 'rb') as f:
        elf = ELFFile(f)
        secinfo = [(s.name, s['sh_addr']) for s in elf.iter_sections()]
        for sym in elf.get_section_by_name('.symtab').iter_symbols():
            if not isinstance(sym['st_shndx'], int):
                continue
            if sym.name in ('$a', '$t', '$d'):
                maps[sym['st_shndx']].append((sym['st_value'], sym.name))
            elif sym.name and not sym.name.startswith('$'):
                named[sym['st_shndx']].add(sym['st_value'] & ~1)

    main_image = open(os.path.join(build_dir, 'main.sbin'), 'rb').read()
    sections = []
    for idx in sorted(maps):
        name, base = secinfo[idx]
        if name == 'ITCM':
            image = autoload_image(main_image, base)
        else:
            path = os.path.join(build_dir, name + '.sbin')
            if not os.path.exists(path):
                print('warning: no image for section', name, file=sys.stderr)
                continue
            image = open(path, 'rb').read()
        sec = Section(name, base, image)
        sec.named = named[idx]
        syms = sorted(set(maps[idx]))
        end_of_image = base + len(image)
        for (addr, kind), nxt in zip(syms, syms[1:] + [(end_of_image, None)]):
            end = min(nxt[0], end_of_image)
            if kind == '$d' or end <= addr:
                continue
            thumb = kind == '$t'
            step = 2 if thumb else 4
            end -= (end - addr) % step
            if end <= addr:
                continue
            if sec.ranges and sec.ranges[-1][1] == addr and sec.ranges[-1][2] == thumb:
                sec.ranges[-1] = (sec.ranges[-1][0], end, thumb)
            else:
                sec.ranges.append((addr, end, thumb))
        sections.append(sec)
    return sections


ARM_COND = ['EQ', 'NE', 'CS', 'CC', 'MI', 'PL', 'VS', 'VC', 'HI', 'LS', 'GE', 'LT', 'GT', 'LE']


def post(addr, step, target, local, pure=False):
    """The statement after an instruction; `target` is where a direct branch goes.
    A branch to an instruction of the same chunk continues there without a trip through
    the dispatcher, unless something has a name at that address: the frontend watches
    function entries (probes), and those are noticed in the dispatcher."""
    if target is not None and target in local:
        return f'RC_POST_BR(0x{addr + 2 * step:08X}u, 0x{target + step:08X}u, L{local[target]})'
    if pure:
        return 'RC_POST_PURE'
    return f'RC_POST(0x{addr + 2 * step:08X}u)'


def sext(value, bits):
    return value - (1 << bits) if value & (1 << (bits - 1)) else value


def emit_arm(addr, opc, arm_tbl, local):
    cond = opc >> 28
    icode = ((opc >> 4) & 0xF) | ((opc >> 16) & 0xFF0)
    handler = arm_tbl[icode]
    # The common instructions as inline C++, the rest through their handler.
    inline = recomp_inline.arm(handler, opc, addr)
    pure = recomp_inline.is_pure(inline)
    run = recomp_inline.fast(inline) if inline else f'{handler}(cpu);'
    skipped = recomp_inline.cycles(addr + 8)   # AddCycles_C
    if cond == 0xE:
        body = run
    elif cond == 0xF:
        if (opc & 0xFE000000) == 0xFA000000:
            body, pure = 'A_BLX_IMM(cpu);', False
        else:
            body, pure = skipped, True
    else:
        body = f'if (RC_CC_{ARM_COND[cond]}) {run} else {{ {skipped} }}'
    target = None
    if handler == 'A_B' and cond != 0xF:
        target = (addr + 8 + (sext(opc & 0xFFFFFF, 24) << 2)) & 0xFFFFFFFF
    return f'RC_A_FETCH_AT(0x{addr + 8:08X}u) RC_A_GUARD(0x{opc:08X}u) {body} {post(addr, 4, target, local, pure)}'


def emit_thumb(addr, opc, thumb_tbl, local):
    handler = thumb_tbl[(opc >> 6) & 0x3FF]
    fetch = f'RC_T_FETCH_SHIFT_AT(0x{addr + 4:08X}u)' if (addr & 2) else f'RC_T_FETCH_READ_AT(0x{addr + 4:08X}u)'
    target = None
    if handler == 'T_BCOND':
        target = (addr + 4 + (sext(opc & 0xFF, 8) << 1)) & 0xFFFFFFFF
    elif handler == 'T_B':
        target = (addr + 4 + (sext(opc & 0x7FF, 11) << 1)) & 0xFFFFFFFF
    inline = recomp_inline.thumb(handler, opc, addr)
    pure = recomp_inline.is_pure(inline)
    run = recomp_inline.fast(inline) if inline else f'{handler}(cpu);'
    return f'{fetch} RC_T_GUARD(0x{opc:04X}u) {run} {post(addr, 2, target, local, pure)}'


def write_images(out_dir, sections):
    """Choose the object format using compiler target macros, including cross builds."""
    with open(os.path.join(out_dir, 'rc_images.S'), 'w', newline='\n') as f:
        f.write('#if defined(__APPLE__)\n'
                '    .section __TEXT,__const\n'
                '#define RC_IMAGE(name) _##name\n'
                '#elif defined(_WIN32)\n'
                '    .section .rdata,"dr"\n'
                '#define RC_IMAGE(name) name\n'
                '#else\n'
                '    .section .rodata\n'
                '#define RC_IMAGE(name) name\n'
                '#endif\n')
        for si, sec in enumerate(sections):
            img = f'rc_image_{si}.bin'
            with open(os.path.join(out_dir, img), 'wb') as image:
                image.write(sec.image)
            f.write(f'    .global RC_IMAGE(rc_image_{si})\n    .balign 16\n'
                    f'RC_IMAGE(rc_image_{si}):\n    .incbin "{img}"\n')
        # (%progbits: on 32-bit ARM, @ starts a comment.)
        f.write('#if defined(__ELF__)\n    .section .note.GNU-stack,"",%progbits\n#endif\n')


def main():
    ap = argparse.ArgumentParser(description='Recompile from the decompilation\'s build directory, '
                                 'or from a ROM with --rom and --layout.')
    ap.add_argument('build_dir', nargs='?', help='the decompilation\'s build directory (e.g. build/soulsilver.us)')
    ap.add_argument('melonds_src')
    ap.add_argument('out_dir')
    ap.add_argument('--rom', help='the player\'s own ROM, instead of a build directory')
    ap.add_argument('--layout', help='with --rom: the layout file made by tools/make_layout.py')
    ap.add_argument('--only', help='comma-separated section names to recompile (default: all)')
    args = ap.parse_args()

    arm_tbl, thumb_tbl = parse_instr_tables(os.path.join(args.melonds_src, 'ARM_InstrTable.h'))
    if args.rom:
        if not args.layout:
            ap.error('--rom needs --layout')
        sections = load_sections_from_layout(args.layout, args.rom)
    else:
        if not args.build_dir:
            ap.error('give a build directory, or --rom and --layout')
        sections = load_sections(args.build_dir)
    sections.append(bios_section(args.melonds_src))
    if args.only:
        keep = set(args.only.split(','))
        sections = [s for s in sections if s.name in keep]
    # Everything is written to a scratch directory first and only files whose contents
    # changed are moved into place, so running this again does not make the build
    # recompile 1.7 million unchanged instructions.
    final_dir = args.out_dir
    args.out_dir = final_dir + '.new'
    shutil.rmtree(args.out_dir, ignore_errors=True)
    os.makedirs(args.out_dir)

    files = []
    cur_lines = []
    cur_count = 0
    chunk_rows = []   # (start, end, thumb, section index, function name)
    total = 0

    def flush():
        nonlocal cur_lines, cur_count
        if not cur_lines:
            return
        name = f'rc_{len(files):03d}.cpp'
        with open(os.path.join(args.out_dir, name), 'w', newline='\n') as f:
            f.write('// Generated by tools/recomp.py. Do not edit.\n#include "recomp_gen.h"\n\n')
            f.write('\n'.join(cur_lines))
            f.write('\n')
        files.append(name)
        cur_lines = []
        cur_count = 0

    for si, sec in enumerate(sections):
        for start, end, thumb in sec.ranges:
            step = 2 if thumb else 4
            addr = start
            while addr < end:
                cend = min(end, addr + CHUNK_MAX * step)
                n = (cend - addr) // step
                fn = f'rc_s{si}_{addr:08X}'
                out = [f'void {fn}(melonDS::ARMv5* cpu, melonDS::u32 addr)', '{',
                       '    melonDS::NDS& nds = cpu->NDS;',
                       '    const bool rc_single_here = rc_single;',
                       f'    const melonDS::u32 RC_SECTION = {si};',
                       f'    switch ((addr - 0x{addr:08X}u) >> {1 if thumb else 2})', '    {']
                out += [f'    case {i}: goto L{i};' for i in range(n)]
                out += ['    default: return;', '    }']
                local = {addr + i * step: i for i in range(n) if addr + i * step not in sec.named}
                for i in range(n):
                    a = addr + i * step
                    off = a - sec.base
                    if thumb:
                        opc = struct.unpack_from('<H', sec.image, off)[0]
                        stmt = emit_thumb(a, opc, thumb_tbl, local)
                    else:
                        opc = struct.unpack_from('<I', sec.image, off)[0]
                        stmt = emit_arm(a, opc, arm_tbl, local)
                    out.append(f'L{i}: {stmt}')
                out += ['    return;',
                        f'rc_bail: rc_bail_{"thumb" if thumb else "arm"}(cpu, RC_SECTION);',
                        '    return;', '}', '']
                cur_lines += out
                cur_count += n
                total += n
                chunk_rows.append((addr, cend, int(thumb), si, fn))
                addr = cend
                if cur_count >= FILE_MAX:
                    flush()
    flush()

    write_images(args.out_dir, sections)

    with open(os.path.join(args.out_dir, 'rc_tables.cpp'), 'w', newline='\n') as f:
        f.write('// Generated by tools/recomp.py. Do not edit.\n#include "recomp_rt.h"\n\n')
        f.write('namespace melonDS { class ARMv5; }\n')
        for row in chunk_rows:
            f.write(f'void {row[4]}(melonDS::ARMv5*, melonDS::u32);\n')
        f.write('extern "C" {\n')
        for si in range(len(sections)):
            f.write(f'extern const unsigned char rc_image_{si}[];\n')
        f.write('}\n\n')
        for si, sec in enumerate(sections):
            f.write(f'static const RcRange rc_ranges_{si}[] = {{')
            f.write(', '.join(f'{{0x{s:08X}u, 0x{e:08X}u}}' for s, e, _ in sec.ranges))
            f.write('};\n')
        f.write('\nconst RcChunk rc_chunks[] = {\n')
        for start, end, thumb, si, fn in chunk_rows:
            f.write(f'    {{0x{start:08X}u, 0x{end:08X}u, {thumb}, {si}, {fn}}},\n')
        f.write('};\n')
        f.write(f'const melonDS::u32 rc_num_chunks = {len(chunk_rows)};\n\n')
        f.write('RcSection rc_sections[] = {\n')
        first = 0
        for si, sec in enumerate(sections):
            count = sum(1 for r in chunk_rows if r[3] == si)
            f.write(f'    {{"{sec.name}", 0x{sec.base:08X}u, 0x{len(sec.image):X}u, rc_image_{si}, '
                    f'rc_ranges_{si}, {len(sec.ranges)}, {first}, {count}}},\n')
            first += count
        f.write('};\n')
        f.write(f'const melonDS::u32 rc_num_sections = {len(sections)};\n')

    with open(os.path.join(args.out_dir, 'sources.cmake'), 'w', newline='\n') as f:
        f.write('set(RC_GENERATED_SOURCES\n')
        for name in files + ['rc_tables.cpp', 'rc_images.S']:
            f.write(f'    "${{CMAKE_CURRENT_LIST_DIR}}/{name}"\n')
        f.write(')\n')

    changed = sync_dir(args.out_dir, final_dir)
    print(f'{len(sections)} sections, {len(chunk_rows)} chunks, {total} instructions, {len(files)} files '
          f'({changed} changed)')


def sync_dir(src, dst):
    """Moves the files of src into dst where they differ, removes generated files of dst
    that src no longer has, and deletes src. Returns the number of files changed."""
    os.makedirs(dst, exist_ok=True)
    fresh = set(os.listdir(src))
    changed = 0
    for f in os.listdir(dst):
        if (f.startswith('rc_') or f == 'sources.cmake') and f not in fresh:
            os.remove(os.path.join(dst, f))
            changed += 1
    for f in fresh:
        a, b = os.path.join(src, f), os.path.join(dst, f)
        if os.path.exists(b) and filecmp.cmp(a, b, shallow=False):
            continue
        shutil.move(a, b)  # also works when dst is on another file system
        changed += 1
    shutil.rmtree(src)
    return changed


if __name__ == '__main__':
    main()
