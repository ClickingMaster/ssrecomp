#!/usr/bin/env python3
"""Write release checksums and binary build IDs, preserving IDs only for unchanged assets."""
import argparse, hashlib, re
from pathlib import Path

def write(directory, builds):
    directory=Path(directory); dest=directory/'SHA256SUMS.txt'
    previous={}; versions={}
    if dest.exists():
        for line in dest.read_text().splitlines():
            m=re.fullmatch(r'([0-9a-f]{64})  (.+)',line)
            if m: previous[m[2]]=m[1]
            m=re.fullmatch(r'# fulldex-build (\S+) ([0-9]+)',line)
            if m: versions[m[1]]=int(m[2])
    supplied={}
    for value in builds:
        asset, stamp=value.split('=',1)
        epoch=int(Path(stamp).read_text().strip())
        if epoch<=0: raise ValueError('invalid build ID: '+stamp)
        if not (directory/asset).is_file(): raise ValueError('missing asset: '+asset)
        supplied[asset]=epoch
    lines=[]
    for asset in sorted(directory.iterdir()):
        if not asset.is_file() or not asset.name.startswith('pokemon-ds-wide-'): continue
        if not asset.name.endswith(('.zip','.tar.gz','.AppImage','.apk','.ipa')): continue
        digest=hashlib.file_digest(asset.open('rb'),'sha256').hexdigest()
        lines.append(digest+'  '+asset.name)
        epoch=supplied.get(asset.name)
        if epoch is None and previous.get(asset.name)==digest: epoch=versions.get(asset.name)
        if epoch: lines.append('# fulldex-build '+asset.name+' '+str(epoch))
    if not lines: raise ValueError('no release assets found')
    temp=dest.with_suffix('.tmp');temp.write_text('\n'.join(lines)+'\n');temp.replace(dest)
    return dest
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory');parser.add_argument('--build',action='append',default=[],metavar='ASSET=BUILD_ID_FILE')
    args=parser.parse_args(); print(write(args.directory,args.build))
