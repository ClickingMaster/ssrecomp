#!/usr/bin/env python3
"""The Android TV launcher: the Android app without the game in it, built with no ROM. Like
the Xbox launcher (tools/xbox/launcher.py) it runs the game on melonDS's interpreter and
watches the game's code, read from the player's ROM, for the single-screen and widescreen
views; its start menu (frontend/launchmenu.h) finds the ROM on the device.
.github/workflows/android-tv-launcher.yml builds it on GitHub.

usage: tv_launcher.py --build-dir <dir> [--sdk <Android SDK>] [--ndk <NDK>] [--key <key file>]
                      [--abi arm64-v8a --abi armeabi-v7a] [--version-code N]
Needs CMake, Ninja, a JDK, Pillow and, in the SDK, build-tools;34.0.0 and platforms;android-34.
The APK ends up in <dir>/SoulSilver-ssrecomp-tv.apk.
"""
import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'android'))
sys.path.insert(0, str(ROOT / 'tools' / 'xbox'))
import apk
import launcher

SDL2_NAME = 'SDL2-2.32.10'
SDL2_URL = f'https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/{SDL2_NAME}.tar.gz'
SDL2_SHA256 = '5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165'
PLATFORM = 'android-24'  # Android 7.0, as the phone app


def run(cmd):
    print('+', ' '.join(map(str, cmd)), flush=True)
    subprocess.run(list(map(str, cmd)), check=True)


def sdl_source(build):
    tar = build / 'downloads' / (SDL2_NAME + '.tar.gz')
    if not tar.exists():
        tar.parent.mkdir(parents=True, exist_ok=True)
        with urllib.request.urlopen(SDL2_URL) as r:
            tar.write_bytes(r.read())
    assert hashlib.sha256(tar.read_bytes()).hexdigest() == SDL2_SHA256
    src = build / SDL2_NAME
    if not src.exists():
        with tarfile.open(tar) as t:
            t.extractall(build)
    return src


def build_abi(build, ndk, sdl_src, gen, abi):
    toolchain = Path(ndk) / 'build' / 'cmake' / 'android.toolchain.cmake'
    common = ['-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', f'-DCMAKE_TOOLCHAIN_FILE={toolchain}',
              f'-DANDROID_ABI={abi}', f'-DANDROID_PLATFORM={PLATFORM}']
    prefix = build / f'sdl2-{abi}'
    if not (prefix / 'lib' / 'libSDL2.so').exists():
        run(['cmake', '-S', sdl_src, '-B', build / f'build-sdl2-{abi}', *common,
             f'-DCMAKE_INSTALL_PREFIX={prefix}', '-DSDL_STATIC=OFF', '-DSDL_TEST=OFF'])
        run(['ninja', '-C', build / f'build-sdl2-{abi}', 'install'])
    game = build / f'build-{abi}'
    run(['cmake', '-S', ROOT, '-B', game, *common, f'-DSSR_GENERATED_DIR={gen}', '-DSSR_LAUNCHER=ON',
         f'-DSDL2_DIR={prefix}/lib/cmake/SDL2'])
    run(['ninja', '-C', game])
    # Without their debug information the libraries are a tenth of the size.
    strip = next((Path(ndk) / 'toolchains' / 'llvm' / 'prebuilt').glob('*/bin/llvm-strip'))
    out = []
    for lib in (game / 'libmain.so', prefix / 'lib' / 'libSDL2.so'):
        stripped = build / f'stripped-{abi}' / lib.name
        stripped.parent.mkdir(exist_ok=True)
        run([strip, '--strip-unneeded', '-o', stripped, lib])
        out.append(stripped)
    return tuple(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--build-dir', type=Path, required=True)
    ap.add_argument('--sdk', default=os.environ.get('ANDROID_HOME') or os.environ.get('ANDROID_SDK_ROOT'))
    ap.add_argument('--ndk', default=os.environ.get('ANDROID_NDK_HOME') or os.environ.get('ANDROID_NDK_LATEST_HOME'))
    ap.add_argument('--key', type=Path, help='signing key (made if missing; keep it to install updates over the app)')
    ap.add_argument('--abi', action='append', help='CPU types (default: arm64-v8a and armeabi-v7a)')
    ap.add_argument('--version-code', default='1')
    args = ap.parse_args()
    if not args.sdk or not args.ndk:
        sys.exit('Set --sdk and --ndk (or ANDROID_HOME and ANDROID_NDK_HOME).')
    build = args.build_dir.resolve()
    build.mkdir(parents=True, exist_ok=True)
    gen = build / 'gen-launcher'
    launcher.write_gen(gen)
    sdl_src = sdl_source(build)
    libs = {abi: build_abi(build, args.ndk, sdl_src, gen, abi) for abi in (args.abi or ['arm64-v8a', 'armeabi-v7a'])}
    skeleton = build / 'tv-skeleton.apk'
    # Android 10 as the target: its storage rules let the app read the ROM from the
    # Download folder and USB sticks (see tools/android/tv/AndroidManifest.xml).
    run(['bash', ROOT / 'tools' / 'android' / 'make_skeleton.sh', args.sdk, sdl_src,
         ROOT / 'tools' / 'android' / 'tv' / 'AndroidManifest.xml', skeleton, '29', args.version_code])
    out = build / 'SoulSilver-ssrecomp-tv.apk'
    apk.build_multi(str(skeleton), {abi: (str(m), str(s)) for abi, (m, s) in libs.items()}, None,
                    str(args.key or build / 'signing-key.json'), str(out))
    print('->', out, out.stat().st_size, 'bytes', flush=True)


if __name__ == '__main__':
    main()
