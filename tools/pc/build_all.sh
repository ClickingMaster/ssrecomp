#!/usr/bin/env bash
# Builds the PC all-games download: one program for every game (Pokemon Black, White, Black 2,
# White 2, SoulSilver, HeartGold and Platinum) with a game list when it starts
# (frontend/pc_launcher.cpp). The player puts their ROMs in its roms folder and picks a game.
#
#   tools/pc/build_all.sh windows      your-soulsilver-us.nds
#   tools/pc/build_all.sh linux        your-soulsilver-us.nds
#   tools/pc/build_all.sh linux-arm64  your-soulsilver-us.nds
#   tools/pc/build_all.sh both         your-soulsilver-us.nds
#
# As for the Switch's all-games download (tools/switch/build_game.sh all), the SoulSilver ROM is
# only read for the addresses in SoulSilver and HeartGold's tables, and the Black games' function
# tables come from the decompilations' symbol files: the downloads hold nothing of the games.
#
# The results, in build-pc-dist/:
#   pokemon-ds-wide-pc-windows-x86_64.zip   Pokemon Fulldex.exe, SDL2.dll, data/, roms/
#   pokemon-ds-wide-pc-linux-x86_64.tar.gz  pokemon-ds-wide, libSDL2-2.0.so.0, data/, roms/
#   pokemon-ds-wide-pc-linux-x86_64.AppImage the same as one file (and its .zsync), for Gear Lever
#                                            and other AppImage managers; ROMs, saves and settings
#                                            then go in ~/.local/share/pokemon-ds-wide
#   pokemon-ds-wide-pc-linux-aarch64.tar.gz and .AppImage (linux-arm64): the same, cross-compiled
#                                            for 64-bit ARM (Armada and other ARM handhelds)
#
# Needs Linux or WSL2 (Ubuntu 24.04) with git, CMake, Ninja, Python 3 and curl; for Windows also
# MinGW-w64 (sudo apt install g++-mingw-w64-x86-64-posix), for Linux SDL2's development files
# (sudo apt install libsdl2-dev). SDL2 for Windows is SDL's own MinGW package, fetched once into
# build-pc-deps/ and checked against its known checksum. The Windows program's icon:
# SSR_PC_ICON=your.ico (optional). The game list's pictures, built into the program: SSR_PC_ART, a
# folder of your own with IREO.jpg, IRDO.jpg ... named as for the list's icons/ folder (optional).
# The AppImage's icon: SSR_LINUX_ICON=your.jpg or .png (optional); it is made with appimagetool,
# fetched once into build-pc-deps/, and zsyncmake (sudo apt install zsync) for its .zsync.
# linux-arm64 needs the ARM cross compiler (sudo apt install g++-aarch64-linux-gnu patchelf) and the same
# X11, Wayland, EGL, drm and sound headers the x86_64 build uses (libx11-dev, libwayland-dev,
# libegl1-mesa-dev, libdrm-dev, libgbm-dev, libasound2-dev, libpulse-dev and the rest named if
# one is missing). It does not run the ARM program: it only compiles it.
# GBA games too: SSR_GBA_DIR, Pokemon GBA Widescreen's folder after its setup.sh (optional).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$HERE"
TARGET="${1:-}"
ROM="${2:-}"
BW2_DECOMP=https://github.com/fuddlesworth/pokebw2.git
BW2_COMMIT=753b3fd97683fc619bb98b52e7276afac3d5e32f
W2_COMMIT=be2c125a700c23702ba1c5a4ef63e0e970bcc87a
BW1_DECOMP=https://github.com/Project-Holon/aletheia.git
BW1_COMMIT=0d05b6ccc61fcea090d8318a00af9f1634810e18
SDL_VERSION=2.30.9
SDL_URL="https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VERSION/SDL2-devel-$SDL_VERSION-mingw.tar.gz"
SDL_SHA256=b188d165dc4a0372042bff3d0c18e9bb90772581e92a7b8b47b521b47714f75c
# (Linux: SDL2's source, built here and put in the download; see "Compiling for Linux".)
SDL_SRC_URL="https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VERSION/SDL2-$SDL_VERSION.tar.gz"
SDL_SRC_SHA256=24b574f71c87a763f50704bbb630cbe38298d544a1f890f099a4696b1d6beba4

say() { printf '\n== %s\n' "$*"; }
fail() { printf '\nerror: %s\n' "$*" >&2; exit 1; }

case "$TARGET" in
    windows|linux|linux-arm64|both) ;;
    *) fail "which? tools/pc/build_all.sh windows|linux|linux-arm64|both your-soulsilver-us.nds" ;;
esac
[ -n "$ROM" ] || fail "give your SoulSilver ROM (US): tools/pc/build_all.sh $TARGET your-soulsilver-us.nds"
[ -f "$ROM" ] || fail "no such file: $ROM"
[ "$(dd if="$ROM" bs=1 skip=12 count=4 2>/dev/null)" = IPGE ] ||
    fail "$ROM is not Pokemon SoulSilver (US, game code IPGE): the tables are made from it"
ROM="$(cd "$(dirname "$ROM")" && pwd)/$(basename "$ROM")"
for tool in git cmake ninja python3 curl; do
    command -v "$tool" >/dev/null || fail "$tool is not installed (Ubuntu: sudo apt install git cmake ninja-build python3 curl)"
done
if [ "$TARGET" = windows ] || [ "$TARGET" = both ]; then
    command -v x86_64-w64-mingw32-g++-posix >/dev/null ||
        fail "MinGW-w64 is not installed (Ubuntu: sudo apt install g++-mingw-w64-x86-64-posix)"
fi
if [ "$TARGET" = linux ] || [ "$TARGET" = both ]; then
    { pkg-config --exists sdl2 2>/dev/null || [ -f /usr/include/SDL2/SDL.h ]; } ||
        fail "SDL2's development files are not installed (Ubuntu: sudo apt install libsdl2-dev)"
fi
if [ "$TARGET" = linux-arm64 ]; then
    command -v aarch64-linux-gnu-g++ >/dev/null ||
        fail "the ARM cross compiler is not installed (Ubuntu: sudo apt install g++-aarch64-linux-gnu)"
    command -v wayland-scanner >/dev/null ||
        fail "wayland-scanner is not installed (Ubuntu: sudo apt install libwayland-bin)"
fi

# The symbol files of a decompilation at a fixed version, without the rest of it.
fetch_symbols() { # dir url commit paths...
    local dir="$1" url="$2" commit="$3"
    shift 3
    if [ ! -d "$dir/.git" ]; then
        git init -q "$dir"
        git -C "$dir" remote add origin "$url"
    fi
    git -C "$dir" sparse-checkout set --no-cone "$@"
    git -C "$dir" fetch -q --depth 1 --filter=blob:none origin "$commit"
    git -C "$dir" checkout -q --force FETCH_HEAD
}

# SoulSilver and HeartGold's tables (addresses only), as for the Switch.
if [ ! -f gen/sources.cmake ]; then
    say "SoulSilver's tables, from $ROM"
    python3 tools/recomp.py --rom "$ROM" --layout layouts/soulsilver-us.json melonDS/src gen
    python3 tools/apps.py --layout layouts/soulsilver-us.json gen
fi
python3 tools/soulsilver/tables_only.py gen build-gen-tables

# The function tables (made without a ROM: the program reads the few bytes they need from the
# player's ROM), one per game, in data/funcs beside the program.
DATA="$HERE/build-pc-data"
rm -rf "$DATA"
mkdir -p "$DATA/funcs"
say "The function tables, from the decompilations' symbol files"
fetch_symbols build-decomp-pokebw2 "$BW2_DECOMP" "$BW2_COMMIT" \
    '/config/b2_us/arm9/symbols.txt' '/config/b2_us/arm9/*/symbols.txt' '/config/b2_us/arm9/overlays/*/symbols.txt'
fetch_symbols build-decomp-pokew2 "$BW2_DECOMP" "$W2_COMMIT" \
    '/config/w2_us/arm9/symbols.txt' '/config/w2_us/arm9/*/symbols.txt' '/config/w2_us/arm9/overlays/*/symbols.txt'
fetch_symbols build-decomp-aletheia "$BW1_DECOMP" "$BW1_COMMIT" '/layout/function_addrs.tsv'
python3 tools/bw2/funcmap.py build-decomp-pokebw2/config/b2_us/arm9 - "$DATA/funcs/IREO.txt"
python3 tools/bw2/funcmap.py build-decomp-pokew2/config/w2_us/arm9 - "$DATA/funcs/IRDO.txt"
python3 tools/bw1/funcmap.py build-decomp-aletheia/layout/function_addrs.tsv - "$DATA/funcs/IRBO.txt"
python3 tools/bw1/funcmap.py build-decomp-aletheia/layout/function_addrs.tsv - "$DATA/funcs/IRAO.txt" --white
cp tools/platinum/CPUE.txt "$DATA/funcs/CPUE.txt"
cp tools/platinum/ADAE.txt "$DATA/funcs/ADAE.txt" # Diamond: Platinum's names (tools/port_family.py)
# The other languages' tables (tools/versions: Spanish White, Platinum, ...), from the English ones.
python3 tools/shift_table.py --all "$DATA/funcs"
cp tools/regions/*.txt "$DATA/funcs/"
python3 tools/regions/package_profiles.py --funcs "$DATA/funcs" --portable "$HERE/build-gen-tables/rc_portable.cpp"

# The build: the JIT for every game, no libpng (screenshots are for tests), OpenGL for WORLD ON
# GPU (WGL on Windows, EGL on Linux), and SSE rather than the building machine's AVX2
# (SSR_PORTABLE_X86), so that it runs on any 64-bit PC.
COMMON=(-G Ninja -DCMAKE_BUILD_TYPE=Release -DSSR_JIT=ON -DSSR_PC_ALL_GAMES=ON -DSSR_PNG=OFF -DSSR_GPU3D=ON
        -DSSR_PORTABLE_X86=ON -DSSR_GENERATED_DIR="$HERE/build-gen-tables")
ART="${SSR_PC_ART:-}"
[ -n "$ART" ] && { [ -d "$ART" ] || fail "SSR_PC_ART: no such folder: $ART"; ART="$(realpath "$ART")"; say "Pictures for the game list: $ART"; }
COMMON+=(-DSSR_PC_ART="$ART")
# GBA games too (Pokemon GBA Widescreen built in): SSR_GBA_DIR, that project's folder, after its
# setup.sh (which makes mGBA and the widescreen patches in ~/pokemon-gba-wide).
GBA="${SSR_GBA_DIR:-}"
[ -n "$GBA" ] && { [ -f "$GBA/frontend/embed.h" ] || fail "SSR_GBA_DIR: not Pokemon GBA Widescreen: $GBA"; GBA="$(realpath "$GBA")"; say "GBA games too: $GBA"; }
COMMON+=(-DSSR_GBA_DIR="$GBA")
GBA_WORK="${GBAW_WORK:-$HOME/pokemon-gba-wide}"
[ -z "$GBA" ] || [ -f "$GBA_WORK/mgba/LICENSE" ] || fail "no mGBA in $GBA_WORK (Pokemon GBA Widescreen's setup.sh makes it; GBAW_WORK if elsewhere)"
UPDATE_BUILDS=()
mkdir -p build-pc-dist

# The folder the player unpacks: the program, its function tables, an empty roms folder and the
# notes. $1: the folder, $2: the program's file name.
stage() {
    local dir="$1"
    rm -rf "$dir"
    mkdir -p "$dir/roms" "$dir/data"
    cp -r "$DATA/funcs" "$dir/data/funcs"
    cp LICENSE "$dir/LICENSE.txt"
    cp LICENSE-Noto.txt "$dir/LICENSE-Noto.txt"
    if [ -n "$GBA" ]; then
        # (With the GBA games: their part of the notes before SOURCE AND LICENCE, and mGBA's licence.)
        awk -v gba=tools/pc/README-GBA.txt '/^SOURCE AND LICENCE$/ { while ((getline l < gba) > 0) print l } { print }' \
            tools/pc/README.txt > "$dir/README.txt"
        cp "$GBA_WORK/mgba/LICENSE" "$dir/LICENSE-mGBA.txt"
    else
        cp tools/pc/README.txt "$dir/README.txt"
    fi
}

# Only these files may be in a download: the program, SDL2 and its licence, the function tables,
# the notes, the licence and the empty roms folder; never a ROM, save, save state or game code.
check_contents() { # list of paths, one per line, relative to the download's top folder
    python3 -c "
import re, sys
regional_codes = '|'.join(p['code'] for p in __import__('json').loads(__import__('pathlib').Path('tools/regions/profiles.json').read_text())['profiles'] if p['kind']=='functions')
ok = re.compile(r'^pokemon-ds-wide/(pokemon-ds-wide|Pokemon Fulldex\.exe|SDL2\.dll|libSDL2-2\.0\.so\.0|LICENSE\.txt|LICENSE-SDL2\.txt|LICENSE-mGBA\.txt|LICENSE-Noto\.txt|README\.txt|roms/|data/|data/funcs/|data/funcs/(?:IREO|IRDO|IRBO|IRAO|CPUE|ADAE|APAE|ADAS|APAF|APAS|APAD|ADAD|ADAI|IRAS|CPUS|IREF|IRDF|CPUF|IRBF|IRAF|' + regional_codes + r')\.txt)?$')
bad = [l for l in sys.stdin.read().splitlines() if l and not ok.match(l)]
if bad: sys.exit('unexpected files in the download: %s' % bad)
"
}

if [ "$TARGET" = windows ] || [ "$TARGET" = both ]; then
    DEPS="$HERE/build-pc-deps"
    SDL_ROOT="$DEPS/SDL2-$SDL_VERSION/x86_64-w64-mingw32"
    if [ ! -f "$SDL_ROOT/include/SDL2/SDL.h" ]; then
        say "SDL2 $SDL_VERSION for Windows (SDL's MinGW package)"
        mkdir -p "$DEPS"
        curl -fsSL -o "$DEPS/sdl2.tar.gz" "$SDL_URL"
        echo "$SDL_SHA256  $DEPS/sdl2.tar.gz" | sha256sum -c --quiet ||
            fail "SDL2's package does not match its checksum: $DEPS/sdl2.tar.gz"
        tar -xzf "$DEPS/sdl2.tar.gz" -C "$DEPS"
    fi
    say "Compiling for Windows"
    # The program's icon: SSR_PC_ICON, an .ico of your own (none comes with the source).
    ICON="${SSR_PC_ICON:-}"
    [ -n "$ICON" ] && { [ -f "$ICON" ] || fail "SSR_PC_ICON: no such file: $ICON"; ICON="$(realpath "$ICON")"; say "Icon: $ICON"; }
    cmake -S . -B build-pc-windows "${COMMON[@]}" -DCMAKE_TOOLCHAIN_FILE="$HERE/tools/pc/mingw-w64.cmake" \
        -DSDL2_DIR="$HERE/tools/pc/sdl2-mingw" -DSSR_SDL2_ROOT="$SDL_ROOT" -DSSR_PC_ICON="$ICON" >/dev/null
    ninja -C build-pc-windows ssrecomp
    OUT=build-pc-dist/windows/pokemon-ds-wide
    stage "$OUT"
    cp build-pc-windows/ssrecomp.exe "$OUT/Pokemon Fulldex.exe"
    x86_64-w64-mingw32-strip "$OUT/Pokemon Fulldex.exe"
    cp "$SDL_ROOT/bin/SDL2.dll" "$OUT/"
    cp "$DEPS/SDL2-$SDL_VERSION/LICENSE.txt" "$OUT/LICENSE-SDL2.txt"
    ZIP=pokemon-ds-wide-pc-windows-x86_64.zip
    UPDATE_BUILDS+=(--build "$ZIP=$HERE/build-pc-windows/fulldex-build-id.txt")
    rm -f "build-pc-dist/$ZIP"
    (cd build-pc-dist/windows && python3 -c "
import os, sys, zipfile
with zipfile.ZipFile(sys.argv[1], 'w', zipfile.ZIP_DEFLATED) as z:
    for root, dirs, files in os.walk('pokemon-ds-wide'):
        dirs.sort()
        d = zipfile.ZipInfo(root + '/')
        d.external_attr = (0o40755 << 16) | 0x10  # a folder, for Unix and Windows unzippers
        z.writestr(d, '')
        for f in sorted(files): z.write(os.path.join(root, f))
" "../$ZIP")
    python3 -c "import sys, zipfile; print('\n'.join(zipfile.ZipFile(sys.argv[1]).namelist()))" "build-pc-dist/$ZIP" | check_contents
    say "Done: $HERE/build-pc-dist/$ZIP"
fi

# Headers and tiny ARM shared libraries so SDL2 can be cross-compiled with the same video and
# sound drivers as the x86_64 download (loaded only when used). The libraries are not shipped:
# at run time the handheld's own copies are opened by those names.
stage_aarch64_deps() {
    A64_STAGE="$HERE/build-pc-deps/aarch64-stage"
    local inc="$A64_STAGE/include" lib="$A64_STAGE/lib"
    mkdir -p "$inc" "$lib/pkgconfig"
    local missing=()
    local need
    for need in X11/Xlib.h wayland-client.h EGL/egl.h alsa/asoundlib.h pulse/pulseaudio.h \
            xkbcommon/xkbcommon.h gbm.h xf86drm.h libudev.h libdecor-0/libdecor.h; do
        [ -e "/usr/include/$need" ] || missing+=("$need")
    done
    if [ "${#missing[@]}" -gt 0 ]; then
        fail "ARM64 Linux needs the same headers as the x86_64 build, and these are missing: ${missing[*]} (Ubuntu: sudo apt install libx11-dev libxext-dev libxcursor-dev libxi-dev libxfixes-dev libxrandr-dev libxrender-dev libxss-dev libwayland-dev libxkbcommon-dev libdecor-0-dev libegl1-mesa-dev libgles2-mesa-dev libdrm-dev libgbm-dev libasound2-dev libpulse-dev libudev-dev)"
    fi
    local name
    for name in X11 alsa pulse xkbcommon EGL KHR GLES2 GLES3 GL libdrm libdecor-0; do
        [ -e "/usr/include/$name" ] && ln -sfn "/usr/include/$name" "$inc/$name"
    done
    for name in wayland-client.h wayland-client-core.h wayland-client-protocol.h wayland-egl.h \
            wayland-cursor.h wayland-util.h wayland-version.h wayland-server.h \
            wayland-server-core.h wayland-server-protocol.h xf86drm.h xf86drmMode.h gbm.h \
            libudev.h libdecor.h samplerate.h sndio.h; do
        [ -f "/usr/include/$name" ] && ln -sfn "/usr/include/$name" "$inc/$name"
    done
    # A shared library whose file name is the name programs open at run time. Nothing is copied
    # out of it: SDL only records that name, and the game's EGL calls are resolved then too.
    local soname linkname
    stub_so() { # soname link-name [source file with the calls SDL's configure links]
        soname="$1"
        linkname="$2"
        if [ -n "${3:-}" ] || [ ! -f "$lib/$soname" ]; then
            if [ -n "${3:-}" ]; then
                aarch64-linux-gnu-gcc -shared -fPIC -Wl,-soname,"$soname" -o "$lib/$soname" "$3"
            else
                echo 'int ssr_a64_stub;' | aarch64-linux-gnu-gcc -shared -fPIC -Wl,-soname,"$soname" \
                    -o "$lib/$soname" -x c -
            fi
        fi
        ln -sfn "$soname" "$lib/$linkname"
    }
    stub_so libX11.so.6 libX11.so "$HERE/tools/pc/aarch64-x11-stub.c"
    stub_so libXext.so.6 libXext.so
    stub_so libXcursor.so.1 libXcursor.so
    stub_so libXi.so.6 libXi.so
    stub_so libXfixes.so.3 libXfixes.so
    stub_so libXrandr.so.2 libXrandr.so
    stub_so libXrender.so.1 libXrender.so
    stub_so libXss.so.1 libXss.so
    stub_so libasound.so.2 libasound.so
    stub_so libpulse.so.0 libpulse.so
    stub_so libwayland-client.so.0 libwayland-client.so
    stub_so libwayland-egl.so.1 libwayland-egl.so
    stub_so libwayland-cursor.so.0 libwayland-cursor.so
    stub_so libxkbcommon.so.0 libxkbcommon.so
    stub_so libdecor-0.so.0 libdecor-0.so
    stub_so libdrm.so.2 libdrm.so
    stub_so libgbm.so.1 libgbm.so
    stub_so libudev.so.1 libudev.so
    stub_so libsamplerate.so.0 libsamplerate.so
    stub_so libsndio.so.7 libsndio.so
    aarch64-linux-gnu-gcc -shared -fPIC -Wl,-soname,libEGL.so.1 \
        -o "$lib/libEGL.so.1" "$HERE/tools/pc/aarch64-egl-stub.c"
    ln -sfn libEGL.so.1 "$lib/libEGL.so"
    pc_file() { # name version link-flag [include subdirectory]
        local cflags='-I${includedir}'
        [ -n "${4:-}" ] && cflags="-I\${includedir}/$4"
        cat > "$lib/pkgconfig/$1.pc" <<EOF
prefix=${A64_STAGE}
libdir=\${prefix}/lib
includedir=\${prefix}/include
Name: $1
Description: stand-in for the ARM64 cross build
Version: $2
Libs: -L\${libdir} -l$3
Cflags: ${cflags}
EOF
    }
    pc_file wayland-client 1.22.0 wayland-client
    pc_file wayland-egl 1.22.0 wayland-egl
    pc_file wayland-cursor 1.22.0 wayland-cursor
    pc_file egl 1.5 EGL
    pc_file xkbcommon 1.6.0 xkbcommon
    pc_file libpulse 16.1 pulse
    # (xf86drm.h includes <drm.h>, which lives in libdrm/.)
    pc_file libdrm 2.4.120 drm libdrm
    pc_file gbm 24.0.0 gbm
    # (libdecor.h lives in libdecor-0/, which is the -I its pkg-config file gives.)
    pc_file libdecor-0 0.2.2 decor-0 libdecor-0
    pc_file alsa 1.2.11 asound
    [ -f "$inc/sndio.h" ] && pc_file sndio 1.9.0 sndio
    [ -f "$inc/samplerate.h" ] && pc_file samplerate 0.2.2 samplerate
}

# $1: x86_64 (this machine) or aarch64 (cross-compiled; see stage_aarch64_deps).
build_linux() {
    local arch="$1"
    local tag readelf_bin strip_bin dist_parent build_dir ad_name
    local -a sdl_cmake=()
    local -a game_cmake=()
    readelf_bin=readelf
    strip_bin=strip
    dist_parent=build-pc-dist/linux
    build_dir=build-pc-linux
    ad_name=AppDir
    tag=linux
    if [ "$arch" = aarch64 ]; then
        tag=linux-aarch64
        readelf_bin=aarch64-linux-gnu-readelf
        strip_bin=aarch64-linux-gnu-strip
        dist_parent=build-pc-dist/linux-aarch64
        build_dir=build-pc-linux-aarch64
        ad_name=AppDir-aarch64
        stage_aarch64_deps
        sdl_cmake=(-DCMAKE_TOOLCHAIN_FILE="$HERE/tools/pc/aarch64-linux-gnu.cmake"
            -DCMAKE_INSTALL_PREFIX="$HERE/build-pc-deps/SDL2-$SDL_VERSION-$tag/prefix")
        game_cmake=(-DCMAKE_TOOLCHAIN_FILE="$HERE/tools/pc/aarch64-linux-gnu.cmake"
            -DSSR_EGL_LIBRARY="$A64_STAGE/lib/libEGL.so"
            -DSSR_EGL_INCLUDE="$A64_STAGE/include")
    fi
    # SDL2 comes with the download, built here from its source with its display and sound parts
    # (X11, Wayland, PulseAudio, ALSA) loaded only when used, so that it needs nothing but the C
    # library: SteamOS (the Steam Deck) and other systems without SDL2 run the download as it
    # is. The program looks for it beside itself first (RPATH $ORIGIN, see CMakeLists.txt).
    # (Ubuntu's own libSDL2 is linked to some twenty libraries, so it is not the one shipped.)
    local LSDL="$HERE/build-pc-deps/SDL2-$SDL_VERSION-$tag"
    if [ ! -f "$LSDL/build/libSDL2-2.0.so.0" ]; then
        say "SDL2 $SDL_VERSION for Linux ($arch), from its source"
        mkdir -p "$LSDL"
        curl -fsSL -o "$LSDL/src.tar.gz" "$SDL_SRC_URL"
        echo "$SDL_SRC_SHA256  $LSDL/src.tar.gz" | sha256sum -c --quiet ||
            fail "SDL2's source does not match its checksum: $LSDL/src.tar.gz"
        tar -xzf "$LSDL/src.tar.gz" -C "$LSDL"
        if [ "$arch" = aarch64 ]; then
            # pkg-config must see the stand-in libraries, not the machine's x86_64 ones.
            PKG_CONFIG_PATH="$A64_STAGE/lib/pkgconfig" PKG_CONFIG_LIBDIR="$A64_STAGE/lib/pkgconfig" \
            SSR_A64_FIND_ROOT="${A64_STAGE};/usr/aarch64-linux-gnu" \
            cmake -S "$LSDL/SDL2-$SDL_VERSION" -B "$LSDL/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
                -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST=OFF -DSDL_RPATH=OFF "${sdl_cmake[@]}" >/dev/null
        else
            cmake -S "$LSDL/SDL2-$SDL_VERSION" -B "$LSDL/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
                -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST=OFF -DSDL_RPATH=OFF >/dev/null
        fi
        if ! ninja -C "$LSDL/build" >"$LSDL/ninja.log"; then
            tail -n 40 "$LSDL/ninja.log" >&2
            fail "SDL2 did not build (the log is $LSDL/ninja.log)"
        fi
    fi
    if [ "$arch" = aarch64 ]; then
        local cfg="$LSDL/build/include-config-release/SDL2/SDL_config.h"
        local driver
        for driver in SDL_VIDEO_DRIVER_X11 SDL_VIDEO_DRIVER_WAYLAND SDL_VIDEO_DRIVER_KMSDRM \
                SDL_AUDIO_DRIVER_PULSEAUDIO SDL_AUDIO_DRIVER_ALSA; do
            grep -q "#define ${driver} 1" "$cfg" ||
                fail "SDL2 for ARM64 was built without ${driver} (its window or sound would be missing). Delete $LSDL and run this again"
        done
        cmake --install "$LSDL/build" >/dev/null
    fi
    local extra
    # (ld-linux is the dynamic linker, present on every glibc system. The video and sound
    # libraries are opened by name when used, so they must not be listed here.)
    extra="$("$readelf_bin" -d "$LSDL/build/libSDL2-2.0.so.0" | grep NEEDED | grep -v 'libc\.so\|libm\.so\|ld-linux' || true)"
    [ -z "$extra" ] || fail "the SDL2 built for the download needs more than the C library: $extra"
    say "Compiling for Linux ($arch)"
    if [ "$arch" = aarch64 ]; then
        SSR_A64_FIND_ROOT="${LSDL}/prefix;${A64_STAGE};/usr/aarch64-linux-gnu" \
        PKG_CONFIG_PATH= PKG_CONFIG_LIBDIR=/nonexistent \
        cmake -S . -B "$build_dir" "${COMMON[@]}" "${game_cmake[@]}" >/dev/null
    else
        cmake -S . -B "$build_dir" "${COMMON[@]}" >/dev/null
    fi
    if ! ninja -C "$build_dir" ssrecomp >"$build_dir/ninja.log"; then
        tail -n 40 "$build_dir/ninja.log" >&2
        fail "the Linux program did not build (the log is $build_dir/ninja.log)"
    fi
    local OUT="$dist_parent/pokemon-ds-wide"
    stage "$OUT"
    cp "$build_dir/ssrecomp" "$OUT/pokemon-ds-wide"
    "$strip_bin" "$OUT/pokemon-ds-wide"
    if [ "$arch" = aarch64 ]; then
        "$readelf_bin" -h "$OUT/pokemon-ds-wide" | grep -q AArch64 ||
            fail "the ARM64 build is not an ARM64 program"
        # CMake also records the cross-build's SDL and EGL folders. Those exist only on the
        # build machine, so the program must look beside itself and then in the usual places.
        command -v patchelf >/dev/null ||
            fail "patchelf is not installed (Ubuntu: sudo apt install patchelf)"
        patchelf --set-rpath '$ORIGIN' "$OUT/pokemon-ds-wide"
    fi
    "$readelf_bin" -d "$OUT/pokemon-ds-wide" | grep -q 'R.*PATH.*\$ORIGIN' ||
        fail "the Linux program does not look for SDL2 beside itself"
    local rpath
    rpath="$("$readelf_bin" -d "$OUT/pokemon-ds-wide" | grep -E 'RPATH|RUNPATH' || true)"
    printf '%s\n' "$rpath" | grep -q '/' &&
        fail "the Linux program looks for libraries at a fixed path: $rpath"
    cp "$LSDL/build/libSDL2-2.0.so.0" "$OUT/libSDL2-2.0.so.0"
    "$strip_bin" "$OUT/libSDL2-2.0.so.0"
    cp "$LSDL/SDL2-$SDL_VERSION/LICENSE.txt" "$OUT/LICENSE-SDL2.txt"
    local TGZ="pokemon-ds-wide-pc-linux-${arch}.tar.gz"
    UPDATE_BUILDS+=(--build "$TGZ=$build_dir/fulldex-build-id.txt" --build "pokemon-ds-wide-pc-linux-${arch}.AppImage=$build_dir/fulldex-build-id.txt")
    tar -czf "build-pc-dist/$TGZ" -C "$dist_parent" pokemon-ds-wide
    tar -tzf "build-pc-dist/$TGZ" | check_contents
    say "Done: $HERE/build-pc-dist/$TGZ"

    # The same program as one AppImage. The update information in it points at the PC release's
    # .zsync, so AppImage managers (Gear Lever, AppImageUpdate) find new versions by themselves.
    say "The AppImage ($arch)"
    local APPIMAGE="pokemon-ds-wide-pc-linux-${arch}.AppImage"
    local AIT="$HERE/build-pc-deps/appimagetool-x86_64.AppImage"
    if [ ! -x "$AIT" ]; then
        mkdir -p "$HERE/build-pc-deps"
        curl -fsSL -o "$AIT" https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
        chmod +x "$AIT"
    fi
    local -a runtime_args=()
    if [ "$arch" = aarch64 ]; then
        # appimagetool here is the x86_64 one. The ARM64 AppImage needs ARM64's runtime, or it
        # would not start on the handheld.
        local RT="$HERE/build-pc-deps/type2-runtime-aarch64"
        if [ ! -f "$RT" ]; then
            curl -fsSL -o "$RT" https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-aarch64
            chmod +x "$RT"
        fi
        runtime_args=(--runtime-file "$RT")
    fi
    command -v zsyncmake >/dev/null || fail "zsyncmake is not installed (Ubuntu: sudo apt install zsync)"
    local AD="build-pc-dist/$ad_name"
    rm -rf "$AD"
    mkdir -p "$AD/usr/bin"
    cp -r "$OUT/." "$AD/usr/bin/"
    rm -rf "$AD/usr/bin/roms" # (the ROMs go in the data folder, see pc_launcher.cpp)
    ln -s usr/bin/pokemon-ds-wide "$AD/AppRun"
    python3 - "${SSR_LINUX_ICON:-$HERE/tools/appimage/ssrecomp-setup.png}" "$AD/pokemon-ds-wide.png" <<'PY'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert('RGBA')
side = min(im.size)
im = im.crop(((im.width - side) // 2, (im.height - side) // 2, (im.width + side) // 2, (im.height + side) // 2))
im.resize((256, 256), Image.LANCZOS).save(sys.argv[2])
PY
    ln -s pokemon-ds-wide.png "$AD/.DirIcon"
    cat > "$AD/pokemon-ds-wide.desktop" <<'DESKTOP'
[Desktop Entry]
Type=Application
Name=Pokémon Fulldex
Comment=The DS Pokemon games as one widescreen picture
Exec=pokemon-ds-wide
Icon=pokemon-ds-wide
Terminal=false
Categories=Game;
DESKTOP
    rm -f "build-pc-dist/$APPIMAGE" "build-pc-dist/$APPIMAGE.zsync"
    (cd build-pc-dist && ARCH="$arch" APPIMAGE_EXTRACT_AND_RUN=1 "$AIT" --no-appstream \
        "${runtime_args[@]}" \
        -u "gh-releases-zsync|mamesj|pokemon-fulldex|pc-v1|$APPIMAGE.zsync" "$ad_name" "$APPIMAGE")
    [ -f "build-pc-dist/$APPIMAGE.zsync" ] || fail "no .zsync was made for the AppImage"
    # (Its contents: the program, SDL2, the tables and notes, the icon and the desktop file.)
    (cd "$AD" && find . -mindepth 1 \( -type d -printf '%P/\n' \) -o -printf '%P\n' |
        grep -v '^AppRun$\|^\.DirIcon$\|^pokemon-ds-wide\.\(png\|desktop\)$\|^usr/$' |
        sed 's|^usr/bin/|pokemon-ds-wide/|' | check_contents)
    say "Done: $HERE/build-pc-dist/$APPIMAGE"
}

if [ "$TARGET" = linux ] || [ "$TARGET" = both ]; then
    build_linux x86_64
fi
if [ "$TARGET" = linux-arm64 ]; then
    build_linux aarch64
fi
# Build IDs describe the binary, so fast hotfixes are visible without upload-time guesses.
python3 tools/updater/write_checksums.py build-pc-dist "${UPDATE_BUILDS[@]}"
say "Checksums: $HERE/build-pc-dist/SHA256SUMS.txt"
cat build-pc-dist/SHA256SUMS.txt
ls -l build-pc-dist/*.zip build-pc-dist/*.tar.gz build-pc-dist/*.AppImage* 2>/dev/null || true
cat <<EOF

Unpack the download anywhere, put your ROMs in its roms folder (any names: English Black, White,
Black 2, White 2, SoulSilver, HeartGold and Platinum, US or Europe$([ -n "$GBA" ] && printf '; and the GBA games, Emerald,\nFireRed, LeafGreen and Ultra Violet')) and start Pokemon Fulldex.exe
(Linux: pokemon-ds-wide).
The downloads hold only the program: nothing of the games.
EOF
