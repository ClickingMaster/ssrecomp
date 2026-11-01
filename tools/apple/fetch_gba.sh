#!/usr/bin/env bash
# Prepares the GBA games for the Mac and iOS builds. build-mac.sh packs a gba/ folder into
# the draft release mac-build-inputs (the widescreen player's sources and its patches, never
# a ROM). This fetches the pinned mGBA, applies patches/mgba-wide.patch, and writes
# INPUTS/gba.env. The build scripts source that. With no gba/ folder the downloads stay
# DS-only, as before.
set -euo pipefail
IN="${1:?which inputs folder? tools/apple/fetch_gba.sh INPUTS}"
SRC="$IN/gba"
ENVF="$IN/gba.env"
rm -f "$ENVF"
if [ ! -f "$SRC/frontend/embed.h" ]; then
    echo "No GBA sources in $SRC (DS games only)"
    exit 0
fi
[ -f "$SRC/patches/mgba.commit" ] || { echo "gba/patches/mgba.commit is missing"; exit 1; }
[ -f "$SRC/patches/mgba-wide.patch" ] || { echo "gba/patches/mgba-wide.patch is missing"; exit 1; }
[ -f "$SRC/wide-patches/emerald-wide.ips" ] || { echo "gba/wide-patches is missing the widescreen patches"; exit 1; }
COMMIT="$(tr -d '[:space:]' < "$SRC/patches/mgba.commit")"
MGBA="$IN/mgba"
if [ ! -f "$MGBA/include/mgba/internal/gba/video.h" ]; then
    rm -rf "$MGBA"
    git init -q "$MGBA"
    git -C "$MGBA" remote add origin https://github.com/mgba-emu/mgba.git
    git -C "$MGBA" fetch -q --depth 1 origin "$COMMIT"
    git -C "$MGBA" checkout -q --force FETCH_HEAD
    git -C "$MGBA" apply "$SRC/patches/mgba-wide.patch"
fi
SRC="$(cd "$SRC" && pwd)"
MGBA="$(cd "$MGBA" && pwd)"
cat > "$ENVF" <<EOF
SSR_GBA_DIR=$SRC
GBAW_MGBA=$MGBA
GBAW_PATCHES=$SRC/wide-patches
EOF
echo "GBA games: $SRC"
