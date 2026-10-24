#!/bin/bash
# Build the TrimUI Smart Pro (aarch64, glibc 2.33) binary from your own ROM.
# Usage: ./port/build_tsp.sh /path/to/SoulSilver.nds
set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$DIR/.." && pwd)"
ROM="${1:-${SSR_ROM:-}}"

if [ -z "$ROM" ] || [ ! -f "$ROM" ]; then
    echo "Uso: $0 /ruta/a/SoulSilver.nds" >&2
    exit 1
fi
for tool in python3 docker; do
    command -v "$tool" >/dev/null || { echo "Falta $tool." >&2; exit 1; }
done

echo "[1/3] Generando gen/ desde tu ROM..."
python3 "$REPO/tools/recomp.py" --rom "$ROM" \
    --layout "$REPO/layouts/soulsilver-us.json" "$REPO/melonDS/src" "$REPO/gen"
python3 "$REPO/tools/apps.py" --layout "$REPO/layouts/soulsilver-us.json" "$REPO/gen"

echo "[2/3] Preparando el toolchain bullseye + SDL2 2.30.8..."
docker build -f "$DIR/Dockerfile.tsp" -t ssrecomp-tsp "$DIR"

echo "[3/3] Compilando (esto tarda; gen/ son ~1,7 millones de instrucciones)..."
docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp \
    -v "$REPO:/src" -w /src ssrecomp-tsp bash -c '
        cmake -S /src -B /src/build-tsp -G Ninja -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_TOOLCHAIN_FILE=/src/toolchain-aarch64.cmake \
            -DCMAKE_PREFIX_PATH=/opt/sdl2 -DSDL2_DIR=/opt/sdl2/lib/cmake/SDL2
        ninja -C /src/build-tsp -j"$(nproc)"
    '

mkdir -p "$DIR/soulsilver"
cp -f "$REPO/build-tsp/ssrecomp" "$DIR/soulsilver/ssrecomp"
chmod +x "$DIR/soulsilver/ssrecomp"
echo "Listo: $DIR/soulsilver/ssrecomp"
echo "Ahora: ./port/deploy_to_sd.sh (o SSR_ROM=\"$ROM\" ./port/deploy_to_sd.sh)"
