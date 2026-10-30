#!/usr/bin/env python3
"""ROM-free smoke test of the clang-cl builds on Windows: the desktop program, and the
Xbox (UWP) app packed and signed into an MSIX.

Usage: python tests/clang_cl_smoke.py --build-dir <dir> [--desktop] [--xbox]
Needs Visual Studio with clang-cl (and the UWP C++ tools for --xbox). The game code is
the synthetic fixture of native_smoke.py and the package has no ROM: never put game
content in here.
"""
import argparse
import hashlib
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tarfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
sys.path.insert(0, str(ROOT / 'tools' / 'xbox'))
import native_smoke
import xbox

SDL2_VERSION = '2.32.10'
SDL2_SOURCE = (f'https://github.com/libsdl-org/SDL/releases/download/release-{SDL2_VERSION}/SDL2-{SDL2_VERSION}.tar.gz',
               '5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165')
SDL2_VC = f'https://github.com/libsdl-org/SDL/releases/download/release-{SDL2_VERSION}/SDL2-devel-{SDL2_VERSION}-VC.zip'


def run(cmd, env=None):
    if env is not None:
        import shutil
        cmd = [shutil.which(str(cmd[0]), path=env.get('Path') or env.get('PATH')) or cmd[0], *cmd[1:]]
    print('+', shlex.join(map(str, cmd)), flush=True)
    subprocess.run(list(map(str, cmd)), check=True, env=env)


def fetch(url, dest, sha256=None):
    if not dest.exists():
        dest.parent.mkdir(parents=True, exist_ok=True)
        with urllib.request.urlopen(url) as r:
            dest.write_bytes(r.read())
    if sha256:
        assert hashlib.sha256(dest.read_bytes()).hexdigest() == sha256, url
    return dest


def vcvars_env(vs, *args):
    out = subprocess.run(f'"{vs["vcvarsall"]}" {" ".join(args)} >nul && set', shell=True,
                         capture_output=True, text=True, check=True)
    return dict(l.split('=', 1) for l in out.stdout.splitlines() if '=' in l and not l.startswith('='))


def configure(build, gen, vs, env, *extra):
    clang = vs['clang_cl'].replace('\\', '/')
    run(['cmake', '-S', ROOT, '-B', build, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=RelWithDebInfo',
         '-DSSR_GENERATED_DIR=' + str(gen), '-DCMAKE_C_COMPILER=' + clang, '-DCMAKE_CXX_COMPILER=' + clang,
         '-DCMAKE_ASM_COMPILER=' + clang, '-DCMAKE_LINKER=' + vs['lld_link'].replace('\\', '/'), *extra], env=env)
    run(['cmake', '--build', build, '--parallel', '2'], env=env)


def desktop(build, gen, vs):
    env = vcvars_env(vs, 'x64')
    zip_path = fetch(SDL2_VC, build / 'downloads' / f'SDL2-devel-{SDL2_VERSION}-VC.zip')
    with zipfile.ZipFile(zip_path) as z:
        z.extractall(build)
    sdl = build / f'SDL2-{SDL2_VERSION}'
    configure(build / 'desktop', gen, vs, env, '-DSSR_PORTABLE_X86=ON', '-DSDL2_DIR=' + str(sdl / 'cmake'))
    exe = build / 'desktop' / 'ssrecomp.exe'
    (build / 'desktop' / 'SDL2.dll').write_bytes((sdl / 'lib' / 'x64' / 'SDL2.dll').read_bytes())
    # --help returns before a ROM is loaded: exercises the linked frontend entry point.
    result = subprocess.run([str(exe), '--help'], capture_output=True, text=True)
    assert result.returncode == 2 and 'usage: ssrecomp' in result.stderr, result
    print('clang-cl desktop build passed.', flush=True)


def uwp(build, gen, vs, *cmake_args, picture=None):
    """SDL2 for UWP, the app from gen and its signed MSIX in build/package; returns the
    MSIX. Also used by tools/xbox/launcher.py."""
    env = xbox.uwp_env(vs)
    tar_path = fetch(SDL2_SOURCE[0], build / 'downloads' / f'SDL2-{SDL2_VERSION}.tar.gz', SDL2_SOURCE[1])
    with tarfile.open(tar_path) as t:
        t.extractall(build)
    sdl_prefix = build / 'sdl2-uwp'
    sdl_build = build / 'build-sdl2-uwp'
    run(['cmake', '-S', build / f'SDL2-{SDL2_VERSION}', '-B', sdl_build, '-G', vs['generator'], '-A', 'x64',
         '-DCMAKE_GENERATOR_INSTANCE=' + vs['root'].replace('\\', '/'),
         '-DCMAKE_SYSTEM_NAME=WindowsStore', '-DCMAKE_SYSTEM_VERSION=10.0', '-DSDL_TEST=OFF', '-DSDL_STATIC=OFF'], env=env)
    run(['cmake', '--build', sdl_build, '--config', 'Release', '--parallel', '2'], env=env)
    run(['cmake', '--install', sdl_build, '--config', 'Release', '--prefix', sdl_prefix], env=env)
    configure(build / 'uwp', gen, vs, env, '-DSSR_UWP=ON', '-DCMAKE_PREFIX_PATH=' + str(sdl_prefix).replace('\\', '/'),
              *cmake_args)
    msix = xbox.package(str(build / 'uwp' / 'ssrecomp.exe'), str(sdl_prefix / 'bin' / 'SDL2.dll'),
                        str(ROOT / 'tools' / 'appimage' / 'ssrecomp-setup.png'), '0.0.0', str(build / 'package'),
                        str(build / 'keys'), env, lambda cmd, env=None: run(cmd, env), print,
                        picture=picture)
    assert os.path.getsize(msix) > 0
    return msix


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--build-dir', type=Path, required=True)
    ap.add_argument('--desktop', action='store_true')
    ap.add_argument('--xbox', action='store_true')
    args = ap.parse_args()
    build = args.build_dir.resolve()
    gen = build / 'generated fixtures with spaces'
    gen.mkdir(parents=True, exist_ok=True)
    native_smoke.fixture(gen)
    vs = xbox.find_visual_studio() if args.xbox else xbox.find_visual_studio(uwp=False)
    if args.desktop or not args.xbox:
        desktop(build, gen, vs)
    if args.xbox:
        # The player's own picture is scaled by Windows: try that path with ours.
        msix = uwp(build, gen, vs, picture=str(ROOT / 'tools' / 'appimage' / 'ssrecomp-setup.png'))
        print('clang-cl Xbox (UWP) build and MSIX passed: ' + msix, flush=True)


if __name__ == '__main__':
    main()
