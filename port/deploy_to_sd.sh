#!/bin/bash
set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$DIR/.." && pwd)"
USER_NAME="${USER:-$(id -un)}"

echo "=== Despliegue de ssrecomp para TrimUI Smart Pro / CrossMix ==="

# The bullseye build targets the TrimUI firmware's glibc 2.33; the older
# build-arm64 (Ubuntu 22.04) needs glibc 2.34 and will not start there.
BIN="${SSR_BIN:-}"
if [ -z "$BIN" ]; then
  for candidate in \
    "$REPO/build-tsp/ssrecomp" \
    "$REPO/build-arm64-bullseye/ssrecomp" \
    "$REPO/build-arm64/ssrecomp" \
    "$DIR/soulsilver/ssrecomp"; do
    if [ -x "$candidate" ]; then
      BIN="$candidate"
      break
    fi
  done
fi
if [ ! -x "$BIN" ]; then
  echo "[ERROR] No encuentro el binario aarch64. Ejecuta primero port/build_tsp.sh." >&2
  exit 1
fi
echo "[BIN] $BIN"

# Your own ROM: port/soulsilver/soulsilver.nds (ignored by git) or SSR_ROM.
ROM_SRC="${SSR_ROM:-$DIR/soulsilver/soulsilver.nds}"

# Find a mounted CrossMix/JukaMix card. The marker folders are what tell it
# apart from an arbitrary removable drive.
find_sd() {
  local m
  for m in /mnt/SDCARD "/run/media/$USER_NAME"/* "/media/$USER_NAME"/* /media/*/*; do
    [ -d "$m" ] || continue
    if [ -d "$m/Roms" ] || [ -d "$m/roms" ] || [ -d "$m/Apps/PortMaster" ]; then
      printf '%s\n' "$m"
      return 0
    fi
  done
  return 1
}

find_ports() {
  local sd="$1" p
  for p in "$sd/Roms/PORTS" "$sd/Roms/ports" "$sd/Ports" "$sd/ports" "$sd/roms/ports"; do
    if [ -d "$p" ]; then
      printf '%s\n' "$p"
      return 0
    fi
  done
  return 1
}

deploy_local() {
  local sd="$1" ports="$2" data="$3"

  if [ ! -w "$sd" ]; then
    echo "[ERROR] $sd está montada en solo lectura. Vuelve a montarla en lectura/escritura." >&2
    exit 1
  fi

  mkdir -p "$data"
  cp -f "$BIN" "$data/ssrecomp"
  chmod +x "$data/ssrecomp"
  echo "[OK] binario -> $data/ssrecomp"

  [ -f "$DIR/soulsilver/ssrecomp.cfg" ] && cp -f "$DIR/soulsilver/ssrecomp.cfg" "$data/ssrecomp.cfg"
  [ -f "$DIR/soulsilver/gamecontrollerdb.txt" ] && cp -f "$DIR/soulsilver/gamecontrollerdb.txt" "$data/gamecontrollerdb.txt"

  if [ ! -f "$data/soulsilver.nds" ] || [ "${FORCE_ROM:-0}" = "1" ]; then
    if [ -f "$ROM_SRC" ]; then
      cp -f "$ROM_SRC" "$data/soulsilver.nds"
      echo "[OK] ROM -> $data/soulsilver.nds"
    else
      echo "[AVISO] No hay ROM en $ROM_SRC; colócala a mano en $data/ o usa SSR_ROM=..." >&2
    fi
  else
    echo "[OK] ROM ya presente en $data/soulsilver.nds (no se sobrescribe)"
  fi

  cp -f "$DIR/Pokemon SoulSilver.sh" "$ports/Pokemon SoulSilver.sh"
  chmod +x "$ports/Pokemon SoulSilver.sh"
  echo "[OK] launcher -> $ports/Pokemon SoulSilver.sh"

  sync
  echo "[OK] Despliegue local completado. Datos: $data"
}

if SD_MOUNT="$(find_sd)"; then
  PORTS_DIR="$(find_ports "$SD_MOUNT")" || {
    echo "[ERROR] He encontrado $SD_MOUNT pero no una carpeta de ports (Roms/PORTS, Ports, roms/ports...)." >&2
    exit 1
  }

  # Keep the existing data folder if the port was already deployed next to the
  # launcher; otherwise use PortMaster's Data/ports convention.
  if [ -d "$PORTS_DIR/soulsilver" ]; then
    DATA_DIR="$PORTS_DIR/soulsilver"
  elif [ -d "$SD_MOUNT/Data/ports/soulsilver" ]; then
    DATA_DIR="$SD_MOUNT/Data/ports/soulsilver"
  else
    DATA_DIR="$SD_MOUNT/Data/ports/soulsilver"
  fi

  echo "[SD] $SD_MOUNT"
  echo "[PORTS] $PORTS_DIR"
  echo "[DATA] $DATA_DIR"
  deploy_local "$SD_MOUNT" "$PORTS_DIR" "$DATA_DIR"
  exit 0
fi

# Optional WiFi/SSH deployment. Set SSR_TARGET_IP (and SSR_SSH_PASS) to use it.
TARGET_IP="${SSR_TARGET_IP:-${1:-}}"
REMOTE_ROOT="${SSR_REMOTE_ROOT:-/mnt/SDCARD}"
SSH_USER="${SSR_SSH_USER:-root}"
SSH_PASS="${SSR_SSH_PASS:-}"

if [ -n "$TARGET_IP" ] && ping -c 1 -W 1 "$TARGET_IP" >/dev/null 2>&1; then
  echo "[WiFi] Desplegando en $SSH_USER@$TARGET_IP:$REMOTE_ROOT ..."
  SSH_OPTS="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o PreferredAuthentications=password -o PubkeyAuthentication=no"
  SSH=(sshpass -p "$SSH_PASS" ssh $SSH_OPTS "$SSH_USER@$TARGET_IP")
  SCP=(sshpass -p "$SSH_PASS" scp $SSH_OPTS)

  REMOTE_PORTS="$REMOTE_ROOT/Roms/PORTS"
  REMOTE_DATA="$REMOTE_ROOT/Data/ports/soulsilver"
  if "${SSH[@]}" "test -d '$REMOTE_ROOT/Roms/PORTS/soulsilver'"; then
    REMOTE_DATA="$REMOTE_ROOT/Roms/PORTS/soulsilver"
  fi

  "${SSH[@]}" "mkdir -p '$REMOTE_PORTS' '$REMOTE_DATA'"
  "${SCP[@]}" "$BIN" "$SSH_USER@$TARGET_IP:$REMOTE_DATA/ssrecomp"
  [ -f "$DIR/soulsilver/ssrecomp.cfg" ] && "${SCP[@]}" "$DIR/soulsilver/ssrecomp.cfg" "$SSH_USER@$TARGET_IP:$REMOTE_DATA/ssrecomp.cfg"
  [ -f "$DIR/soulsilver/gamecontrollerdb.txt" ] && "${SCP[@]}" "$DIR/soulsilver/gamecontrollerdb.txt" "$SSH_USER@$TARGET_IP:$REMOTE_DATA/gamecontrollerdb.txt"
  if "${SSH[@]}" "test ! -f '$REMOTE_DATA/soulsilver.nds'"; then
    [ -f "$ROM_SRC" ] && "${SCP[@]}" "$ROM_SRC" "$SSH_USER@$TARGET_IP:$REMOTE_DATA/soulsilver.nds"
  fi
  "${SCP[@]}" "$DIR/Pokemon SoulSilver.sh" "$SSH_USER@$TARGET_IP:$REMOTE_PORTS/Pokemon SoulSilver.sh"
  "${SSH[@]}" "chmod +x '$REMOTE_DATA/ssrecomp' '$REMOTE_PORTS/Pokemon SoulSilver.sh' && sync"
  echo "[OK] Despliegue por WiFi completado."
  exit 0
fi

echo "[AVISO] No hay tarjeta CrossMix montada ni consola accesible."
echo "  1. Inserta la SD en el PC (se monta en /media/$USER_NAME/<etiqueta>)."
echo "  2. Ejecuta de nuevo: $DIR/deploy_to_sd.sh"
echo "Para WiFi: SSR_TARGET_IP=IP SSR_SSH_PASS=clave $DIR/deploy_to_sd.sh"
