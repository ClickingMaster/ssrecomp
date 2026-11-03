#!/usr/bin/env python3
"""Generate address-only regional HG/SS profiles from the verified French SS baseline.

The baseline's published shifts translate canonical US probes to French addresses.
Every required probe must independently match the supplied target; no preceding
function shift is propagated across unresolved probes.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import tempfile

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS / 'regions'))
sys.path.insert(0, str(TOOLS / 'soulsilver'))
from match_functions import match
from match_french import core_addresses
from ndsrom import read_rom

REFERENCE_SHA1 = 'ba78f6fff3fc1e32aff8fcc4707a2da1ec996f7c'
TARGETS = {'IPKD': ('HeartGold','Germany'), 'IPKJ': ('HeartGold','Japan'),
           'IPKK': ('HeartGold','Korea'), 'IPGI': ('SoulSilver','Italy'),
           'IPGD': ('SoulSilver','Germany'), 'IPGJ': ('SoulSilver','Japan'),
           'IPGK': ('SoulSilver','Korea')}

def fingerprint(path):
    with open(path, 'rb') as f:
        return hashlib.file_digest(f, 'sha1').hexdigest()


def shifted(profile, section, address):
    for start, end, delta in profile['shifts'].get(section, []):
        if start <= address < end:
            return address + delta
    return address


def generate(reference, target, expected_sha1, report):
    if fingerprint(reference) != REFERENCE_SHA1:
        raise ValueError('Reference must be the verified French SoulSilver revision 0 dump')
    if fingerprint(target) != expected_sha1:
        raise ValueError('Target fingerprint does not match the requested dump')
    with open(target,'rb') as f:
        header=f.read(512)
    code=header[12:16].decode('ascii')
    if code not in TARGETS or header[30] != 0:
        raise ValueError('Unsupported target game code or revision')
    layout=json.loads((TOOLS.parent/'layouts/soulsilver-us.json').read_text())
    baseline=json.loads((TOOLS/'soulsilver/soulsilver-fr.json').read_text())
    needed=core_addresses(layout)
    rows=[]; canonical={}; sections={}
    for section in layout['sections']:
        source=section['source']
        module='main' if source=='main' else f'ov{source}' if isinstance(source,int) else 'itcm'
        sections[module]=section
        for start,end,thumb in section['ranges']:
            starts={start,*[a for a in section['named'] if start<a<end],
                    *[a for sec,a in needed if sec==section['name'] and start<=a<end]}
            for address in sorted(starts):
                name=needed.get((section['name'],address),f'address_{address:08x}')
                reference_address=shifted(baseline,section['name'],address)
                key=(module,reference_address)
                if key in canonical and canonical[key]!=(section['name'],address):
                    raise ValueError('Ambiguous canonical source address')
                canonical[key]=(section['name'],address)
                rows.append(f'{reference_address:08x} {"t" if thumb else "a"} {module} ? {name}')
    with tempfile.TemporaryDirectory() as tmp:
        table=Path(tmp)/'reference.txt'; names=Path(tmp)/'names.txt'
        table.write_text('\n'.join(rows));names.write_text('\n'.join(f'"{n}"' for n in needed.values()))
        result=match(reference,target,table,[names])
    result.update(target_sha1=expected_sha1,revision=header[30],reference_sha1=REFERENCE_SHA1)
    Path(report).write_text(json.dumps(result,indent=2)+'\n')
    omitted = set(needed.values()) - {f['name'] for f in result['functions']} - {f['name'] for f in result['missing']}
    if omitted:
        raise ValueError('Required probes absent from the reference table: '+', '.join(sorted(omitted)))
    if result['missing']:
        raise ValueError('Unresolved probes: '+', '.join(f['name'] for f in result['missing']))
    _,overlays=read_rom(target)
    bases={s['name']:overlays[s['source']][0] for s in layout['sections'] if isinstance(s['source'],int)}
    overrides={}
    for f in result['functions']:
        name,address=canonical[(f['module'],f['addr'])]
        overrides.setdefault(name,{})[address]=f['target']-address
    shifts={}
    for section in layout['sections']:
        name,start=section['name'],section['base']; end=start+section['size']
        default=bases.get(name,start)-start; changes=overrides.get(name,{})
        points=sorted(a for a in {start,end,*changes,*[a+2 for a in changes]} if start<=a<=end)
        intervals=[]
        for a,b in zip(points,points[1:]):
            delta=changes.get(a,default)
            if not delta:continue
            if intervals and intervals[-1][1]==a and intervals[-1][2]==delta:intervals[-1][1]=b
            else:intervals.append([a,b,delta])
        if intervals:shifts[name]=intervals
    game,country=TARGETS[code]
    return {'game':f'Pokemon {game} ({country}, revision 0)', 'code':code,'like':'IPGE',
            'revision':0,'rom_sha1':expected_sha1,'reference_sha1':REFERENCE_SHA1,
            'overlay_bases':bases,'shifts':shifts}

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('target');p.add_argument('out');p.add_argument('--reference',required=True)
    p.add_argument('--target-sha1',required=True);p.add_argument('--report',required=True)
    a=p.parse_args()
    try:profile=generate(a.reference,a.target,a.target_sha1,a.report)
    except ValueError as e:p.exit(1,str(e)+'\n')
    Path(a.out).write_text(json.dumps(profile,indent=1)+'\n')
    print(profile['code']+': all required application/control probes matched')
