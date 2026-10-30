#!/bin/bash
# Makes tools/android/skeleton.apk: the manifest, the icon and SDL's Java code, compiled.
# tools/android/apk.py adds the game to it. Only needed when one of those changes; the
# result is kept in the repository, so players need no Java or Android SDK.
#
#   make_skeleton.sh <Android SDK> <SDL2 source> [manifest] [out.apk] [target SDK] [version code]
# Needs a JDK (javac) and, in the SDK, build-tools;34.0.0 and platforms;android-34.
# The optional arguments make another app from the same parts (tools/android/tv_launcher.py).
set -euo pipefail
SDK="$1"
SDL="$2"
HERE="$(cd "$(dirname "$0")" && pwd)"
MANIFEST="${3:-$HERE/AndroidManifest.xml}"
OUT="${4:-$HERE/skeleton.apk}"
TARGET_SDK="${5:-34}"
VERSION_ARGS=()
if [ -n "${6:-}" ]; then VERSION_ARGS=(--version-code "$6" --version-name "$6"); fi
BT="$SDK/build-tools/34.0.0"
JAR="$SDK/platforms/android-34/android.jar"
W="$(mktemp -d)"

# The icon, at the launcher's sizes.
for d in mdpi:48 hdpi:72 xhdpi:96 xxhdpi:144 xxxhdpi:192; do
    mkdir -p "$W/res/mipmap-${d%%:*}"
    python3 - "$HERE/../appimage/ssrecomp-setup.png" "$W/res/mipmap-${d%%:*}/ic_launcher.png" "${d##*:}" <<'PY'
import sys
from PIL import Image
Image.open(sys.argv[1]).convert('RGBA').resize((int(sys.argv[3]),) * 2, Image.LANCZOS).save(sys.argv[2], optimize=True)
PY
done
# The banner Android TV shows for the app on its home screen.
mkdir -p "$W/res/drawable-xhdpi"
python3 - "$HERE/../appimage/ssrecomp-setup.png" "$W/res/drawable-xhdpi/banner.png" <<'PY'
import sys
from PIL import Image, ImageDraw
banner = Image.new('RGBA', (320, 180), (16, 24, 40, 255))
icon = Image.open(sys.argv[1]).convert('RGBA').resize((150, 150), Image.LANCZOS)
banner.alpha_composite(icon, (16, 15))
ImageDraw.Draw(banner).text((180, 82), 'SoulSilver', fill=(255, 215, 94, 255))
banner.save(sys.argv[2], optimize=True)
PY
"$BT/aapt2" compile --dir "$W/res" -o "$W/res.zip"
"$BT/aapt2" link -o "$W/base.apk" -I "$JAR" --manifest "$MANIFEST" \
    --min-sdk-version 24 --target-sdk-version "$TARGET_SDK" "${VERSION_ARGS[@]}" "$W/res.zip"

# SDL's Java code.
mkdir -p "$W/classes" "$W/dex"
javac -nowarn --release 11 -cp "$JAR" -d "$W/classes" $(find "$SDL/android-project/app/src/main/java" -name '*.java') 2>&1 | grep -v '^Note:' || true
"$BT/d8" --release --min-api 24 --lib "$JAR" --output "$W/dex" $(find "$W/classes" -name '*.class')
(cd "$W/dex" && zip -q -X "$W/base.apk" classes.dex)
cp "$W/base.apk" "$OUT"
echo "-> $OUT ($(stat -c %s "$OUT") bytes)"
