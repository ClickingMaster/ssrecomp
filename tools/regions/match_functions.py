#!/usr/bin/env python3
"""Match named functions between two personally supplied DS ROMs.

Emits address metadata only. It never embeds instructions or ROM data. Branch
and literal-pool offsets are normalised with the project's existing matcher.
Ambiguous functions remain unresolved unless independently matched neighbours
on both sides agree on their displacement and the complete body matches.
"""
import argparse
from collections import defaultdict
import json
from pathlib import Path
import re
import struct
import sys

TOOLS = Path(__file__).resolve().parents[1]
# Names that frontend/main.cpp puts together instead of spelling out, so the search for quoted
# names cannot see them: each weather with particles and its phases (bw2.weatherObj).
COMPOSED = {weather + phase
            for weather in ('Flake', 'Blizzard', 'Flutter', 'Speck', 'Rain', 'Thunder', 'GustyRain', 'Sandstorm')
            for phase in ('_Main', '_FadeIn', '_FadeOut')}
sys.path.insert(0, str(TOOLS))
sys.path.insert(0, str(TOOLS / 'bw1'))
from ndsrom import read_rom
from match_bw2 import tokens


def images(path):
    with open(path, 'rb') as rom:
        header = rom.read(0x200)
    main, overlays = read_rom(path)
    base = struct.unpack_from('<I', header, 0x28)[0]
    return header[12:16].decode('ascii'), {'main': (base, main),
            **{f'ov{oid}': value for oid, value in overlays.items()}}


def module_key(name):
    match = re.fullmatch(r'ov(\d+)', name)
    return 'ov' + str(int(match[1])) if match else name


def load_functions(path):
    modules = defaultdict(list)
    for line in Path(path).read_text(encoding='utf-8').splitlines():
        fields = line.split()
        if len(fields) != 5 or fields[1] not in ('a', 't'):
            continue
        address, isa, module, _, name = fields
        modules[module_key(module)].append({'addr': int(address, 16),
            'isa': 'thumb' if isa == 't' else 'arm', 'name': name, 'module': module_key(module)})
    for funcs in modules.values():
        funcs.sort(key=lambda f: f['addr'])
    return modules


def unique_match(reference, target, guess, isa, window=1024):
    step = 2 if isa == 'thumb' else 4
    # A longer prefix removes lookalike wrappers; a shorter one may still be
    # unique where a language-dependent constant changes later in the body.
    for length in sorted({min(len(reference), n) // step * step for n in (96, 64, 32, 16)}, reverse=True):
        if length < 12:
            continue
        wanted = tokens(reference[:length], isa)
        hits = [off for off in range(max(0, guess-window)//step*step,
                                    min(len(target)-length, guess+window)+1, step)
                if tokens(target[off:off+length], isa) == wanted]
        if len(hits) == 1:
            return hits[0], length
    # Japanese/Korean builds can move code beyond the local search window.
    # Search the whole module, retaining the requirement for one unique match.
    prefix_size = 2 if isa == 'thumb' else 4
    raw_prefix = reference[:prefix_size]
    normalised = tokens(reference[:4], isa)
    unchanged = normalised and normalised[0] == int.from_bytes(raw_prefix, 'little')
    if unchanged:
        candidates = []
        at = target.find(raw_prefix)
        while at >= 0:
            if at % step == 0:
                candidates.append(at)
            at = target.find(raw_prefix, at + 1)
    else:
        candidates = range(0, len(target), step)
    for length in sorted({min(len(reference), n) // step * step for n in (96, 64, 32, 16)}, reverse=True):
        if length < 12:
            continue
        wanted = tokens(reference[:length], isa)
        hits = [off for off in candidates if off+length <= len(target)
                and tokens(target[off:off+length], isa) == wanted]
        if len(hits) == 1:
            return hits[0], length
    return None


MAP_DATA = {
    'IRBO': ('ov21', 0x021d3700, 0x0209d8b0, 21, 0x199500),
    'IRAO': ('ov21', 0x021d3718, 0x0209d8c8, 21, 0x199500),
    'IREO': ('ov36', 0x021ca01c, 0x0208ee68, 29, 0x1a3e00),
    'IRDO': ('ov36', 0x021ca050, 0x0208ee94, 29, 0x1a3e00),
}


def unique_data_address(reference, target, address, size):
    old_base, old_data = reference
    new_base, new_data = target
    offset = address-old_base
    if offset < 0 or offset+size > len(old_data):
        raise ValueError('Reference data outside its module')
    block = old_data[offset:offset+size]
    found, start = [], 0
    while (at := new_data.find(block, start)) >= 0:
        if at % 4 == 0:
            found.append(at)
        start = at+1
    if len(found) == 1:
        return new_base+found[0]
    if found:
        raise ValueError('Data table has multiple exact matches')
    wanted = tokens(block, 'arm')
    import array
    normalised = array.array('I', tokens(new_data, 'arm')).tobytes()
    needle = array.array('I', wanted).tobytes()
    found = []
    at = normalised.find(needle)
    while at >= 0:
        if at % 4 == 0:
            found.append(at)
        at = normalised.find(needle, at+1)
    if len(found) != 1:
        raise ValueError('Data table does not have a unique match')
    return new_base+found[0]


def pointer_slot_match(function, funcs, hits, source, target):
    """Resolve an application-table slot only with two independently matched peers."""
    address = function['addr'] | (function['isa'] == 'thumb')
    anchors = {funcs[i]['addr'] | (funcs[i]['isa'] == 'thumb'):
               dest | (funcs[i]['isa'] == 'thumb') for i, (dest, _) in hits.items()}
    target_base, target_code = target[function['module']]
    candidates = set()
    for module, (base, data) in source.items():
        if module not in target:
            continue
        new_base, new_data = target[module]
        at = data.find(struct.pack('<I', address))
        while at >= 0:
            if at % 4 == 0:
                for size in (16, 32, 64, 96):
                    for before in range(0, size, 4):
                        start = at-before
                        if start < 0 or start+size > len(data):
                            continue
                        words = struct.unpack_from('<'+'I'*(size//4), data, start)
                        peers = [(j*4, anchors[w]) for j,w in enumerate(words) if w in anchors]
                        if len({v for _,v in peers}) < 2:
                            continue
                        offset, expected = peers[0]
                        starts = []
                        hit = new_data.find(struct.pack('<I', expected))
                        wanted = tokens(data[start:start+size], 'arm')
                        while hit >= 0:
                            candidate = hit-offset
                            if candidate >= 0 and candidate % 4 == 0 and candidate+size <= len(new_data):
                                if all(struct.unpack_from('<I',new_data,candidate+off)[0] == value for off,value in peers):
                                    if tokens(new_data[candidate:candidate+size],'arm') == wanted:
                                        starts.append(candidate)
                            hit = new_data.find(struct.pack('<I',expected),hit+1)
                        if len(starts) != 1:
                            continue
                        matched = starts[0]
                        pointer = struct.unpack_from('<I',new_data,matched+before)[0]
                        dest = pointer & ~1
                        if (pointer & 1) != (function['isa']=='thumb'):
                            continue
                        if target_base <= dest < target_base+len(target_code):
                            candidates.add(dest)
            at = data.find(struct.pack('<I', address), at+1)
    if len(candidates) == 1:
        return candidates.pop()
    return None


def literal_reference_match(function, modules, source, target):
    """Recover a function pointer from a uniquely identical complete caller body."""
    import bisect
    pointer = function['addr'] | (function['isa'] == 'thumb')
    candidates = set()
    dest_base, dest_code = target[function['module']]
    for module, (base, data) in source.items():
        if module not in target or module not in modules:
            continue
        new_base,new_data = target[module]
        funcs=modules[module]; starts=[f['addr'] for f in funcs]
        at=data.find(struct.pack('<I',pointer))
        while at>=0:
            i=bisect.bisect_right(starts,base+at)-1
            if at%4==0 and i>=0:
                caller=funcs[i]; start=caller['addr']-base
                end=funcs[i+1]['addr']-base if i+1<len(funcs) else len(data)
                size=end-start; slot=at-start
                if 16<=size<=4096 and start>=0 and end<=len(data) and slot+4<=size:
                    body=data[start:end]; isa=caller['isa']; step=2 if isa=='thumb' else 4
                    prefix=body[:step]
                    normalised=tokens(body[:4],isa)
                    if normalised and normalised[0]==int.from_bytes(prefix,'little'):
                        positions=[]; hit=new_data.find(prefix); wanted=tokens(body,isa)
                        while hit>=0:
                            if hit%step==0 and hit+size<=len(new_data) and tokens(new_data[hit:hit+size],isa)==wanted:
                                positions.append(hit)
                            hit=new_data.find(prefix,hit+1)
                        if len(positions)==1:
                            new_pointer=struct.unpack_from('<I',new_data,positions[0]+slot)[0]
                            address=new_pointer&~1
                            if new_pointer&1 == (function['isa']=='thumb') and dest_base<=address<dest_base+len(dest_code):
                                candidates.add(address)
            at=data.find(struct.pack('<I',pointer),at+1)
    return candidates.pop() if len(candidates)==1 else None


def match(reference_path, target_path, table_path, frontend):
    source_code, source = images(reference_path)
    target_code, target = images(target_path)
    modules = load_functions(table_path)
    literals = set()
    for path in frontend:
        literals.update(re.findall(r'"([A-Za-z_][A-Za-z_0-9]+)"', Path(path).read_text(encoding='utf-8')))
    literals -= {'input', 'main', 'update'}
    literals |= COMPOSED
    resolved, required, missing = [], [], []
    for module, funcs in modules.items():
        if module not in source or module not in target:
            missing.extend(f for f in funcs if f['name'] in literals)
            continue
        source_base, old_data = source[module]
        target_base, new_data = target[module]
        wanted = {i for i, f in enumerate(funcs) if f['name'] in literals}
        indices = {j for i in wanted for j in range(max(0, i-8), min(len(funcs), i+9))}
        hits = {}
        for i in sorted(indices):
            f = funcs[i]
            end = funcs[i+1]['addr'] if i+1 < len(funcs) else source_base+len(old_data)
            offset = f['addr']-source_base
            if offset < 0 or end-source_base > len(old_data):
                continue
            reference = old_data[offset:end-source_base]
            found = unique_match(reference, new_data, offset, f['isa'])
            if found:
                hits[i] = (target_base+found[0], 'unique-prefix-' + str(found[1]))
        for i in sorted(wanted - hits.keys()):
            left = next((j for j in range(i-1, -1, -1) if j in hits), None)
            right = next((j for j in range(i+1, len(funcs)) if j in hits), None)
            if left is None or right is None:
                continue
            delta = hits[left][0]-funcs[left]['addr']
            if delta != hits[right][0]-funcs[right]['addr']:
                continue
            f = funcs[i]
            size = funcs[i+1]['addr']-f['addr'] if i+1<len(funcs) else 0
            old_offset, new_offset = f['addr']-source_base, f['addr']+delta-target_base
            if size < 4 or new_offset < 0 or new_offset+size > len(new_data):
                continue
            if tokens(old_data[old_offset:old_offset+size], f['isa']) == tokens(new_data[new_offset:new_offset+size], f['isa']):
                hits[i] = (f['addr']+delta, 'bounded-identical-body')
        for i in sorted(wanted - hits.keys()):
            found = pointer_slot_match(funcs[i], funcs, hits, source, target)
            if found is not None:
                hits[i] = (found, 'unique-pointer-slot-two-anchors')
        for i in sorted(wanted - hits.keys()):
            found = literal_reference_match(funcs[i], modules, source, target)
            if found is not None:
                hits[i] = (found, 'unique-complete-caller-pointer')
        for i in sorted(wanted):
            f = funcs[i]
            required.append(f['name'])
            if i in hits:
                address, evidence = hits[i]
                resolved.append({**f, 'target': address, 'evidence': evidence})
            else:
                missing.append(f)
    result = {'source_code': source_code, 'target_code': target_code,
              'functions': resolved, 'missing': missing}
    if source_code in MAP_DATA:
        module, table, heap, count, user = MAP_DATA[source_code]
        new_table = unique_data_address(source[module], target[module], table, count*72)
        new_heap = unique_data_address(source['main'], target['main'], heap, 12)
        result['wide_map'] = {'table': new_table, 'heap_setup': new_heap, 'count': count, 'heap_user': user}
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('reference')
    parser.add_argument('target')
    parser.add_argument('table')
    parser.add_argument('out')
    args = parser.parse_args()
    root = TOOLS.parent
    frontend = [*sorted((root/'frontend').glob('*.h')), root/'frontend/main.cpp']
    result = match(args.reference, args.target, args.table, frontend)
    Path(args.out).write_text(json.dumps(result, indent=1)+'\n', encoding='utf-8', newline='\n')
    print(f'{result["source_code"]} -> {result["target_code"]}: {len(result["functions"])} functions matched; {len(result["missing"])} unresolved')
    for f in result['missing']:
        print(f'{f["module"]} {f["name"]} {f["addr"]:08x}')
    if result['missing']:
        raise SystemExit(1)
