"""Prepare a separate GBA player copy with Fulldex's touch controls; never edit the source."""
from pathlib import Path
import argparse
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('source', type=Path)
parser.add_argument('output', type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
if args.output.exists():
    parser.error('Output already exists; choose a new directory')
if not (args.source / 'frontend/embed.h').is_file():
    parser.error('Source is not the Pokemon GBA Widescreen player')
with tempfile.TemporaryDirectory(prefix='fulldex-gba-touch-') as folder:
    stage = Path(folder)
    for name in ['CMakeLists.txt', 'LICENSE', 'frontend', 'patches', 'tools', 'art']:
        source = args.source / name
        if source.is_dir():
            shutil.copytree(source, stage / name, ignore=shutil.ignore_patterns('__pycache__', '*.pyc'))
        else:
            shutil.copy2(source, stage / name)
    patch = root / 'tools/gba/touch-controls.patch'
    subprocess.run(['git', 'apply', '--check', str(patch)], cwd=stage, check=True)
    subprocess.run(['git', 'apply', str(patch)], cwd=stage, check=True)
    shutil.copy2(root / 'frontend/touchlayout.h', stage / 'frontend/touchlayout.h')
    shutil.copytree(stage, args.output)
print('Prepared', args.output.resolve())
print('Build Fulldex with -DSSR_GBA_DIR=' + str(args.output.resolve()))
