#!/usr/bin/env python3
"""Copy/audit reviewed regional address tables in an unpacked platform package."""
import argparse
import json
from pathlib import Path
import shutil

ROOT=Path(__file__).resolve().parents[2]

def audit(directory,copy=False,portable=None):
    entries=json.loads((ROOT/'tools/regions/profiles.json').read_text())['profiles']
    errors=[]
    for p in entries:
        source=ROOT/p['source']
        if p['kind']=='functions':
            dest=directory/(p['code']+'.txt')
            if copy:
                directory.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,dest)
            if not dest.is_file() or dest.read_bytes()!=source.read_bytes():errors.append(p['code']+': missing/stale function table')
        elif portable is not None:
            if not portable.is_file() or ('{"'+p['code']+'",') not in portable.read_text():errors.append(p['code']+': missing portable profile')
    return errors

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--funcs',type=Path,required=True)
    p.add_argument('--copy',action='store_true');p.add_argument('--portable',type=Path);a=p.parse_args()
    errors=audit(a.funcs,a.copy,a.portable)
    if errors:p.exit(1,'\n'.join(errors)+'\n')
    print('Regional profile package coverage passed')
