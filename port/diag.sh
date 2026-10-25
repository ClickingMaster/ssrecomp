#!/bin/sh
# Collect bounded system and game diagnostics on BusyBox or desktop Linux.
# Usage: ./diag.sh [output.txt]; run beside ssrecomp on the SD card.

OUT="${1:-$HOME/ssrecomp-diag.txt}"
DIR="$(cd "$(dirname "$0")" && pwd)"
export DIR
WORK="$(mktemp -d)" || exit 1
trap 'rm -rf "$WORK"' EXIT
trap 'exit 1' INT TERM
RAW="$WORK/raw.txt"

if ! command -v timeout >/dev/null 2>&1; then
    echo "Falta timeout; no se ejecutan diagnósticos sin límite de tiempo." >&2
    exit 1
fi

safe() {
    label="$1"; cmd="$2"
    printf '\n== %s ==\n' "$label" >> "$RAW"
    printf '%s\n' "$label"
    timeout 8 sh -c "$cmd" >> "$RAW" 2>&1 ||
        printf '%s\n' '(no disponible, falló o agotó el tiempo)' >> "$RAW"
}

# Keep commands literal until sh -c runs them, including paths with spaces.
# Missing commands on the firmware are recorded without stopping the report.
while IFS='|' read -r label cmd; do
    safe "$label" "$cmd"
done <<'EOF'
Sistema|uname -a; cat /etc/os-release
Modelo|cat /proc/device-tree/model
CPU|cat /proc/cpuinfo
Memoria|head -14 /proc/meminfo
Carga|uptime
Discos|df -h
Frecuencias CPU|for c in /sys/devices/system/cpu/cpu*/cpufreq; do echo "== $c"; for f in scaling_governor scaling_cur_freq scaling_max_freq scaling_available_frequencies; do echo "$f:"; cat "$c/$f"; done; done
GPU|ls /sys/class/devfreq/; for f in /sys/class/devfreq/*/cur_freq; do echo "$f:"; cat "$f"; done; ls -l /dev/dri
Librerías GPU|ls /usr/lib /usr/lib/aarch64-linux-gnu /mnt/SDCARD/System/lib /mnt/SDCARD/System/usr/lib 2>/dev/null | grep -iE 'mali|gles|egl|drm|gbm|pvr|libIMG'
Módulos|lsmod
Audio|cat /proc/asound/cards; aplay -l
Temperatura|for t in /sys/class/thermal/thermal_zone*/temp; do echo "$t:"; cat "$t"; done
Batería|for b in /sys/class/power_supply/*/uevent; do echo "== $b"; head -25 "$b"; done
Procesos|top -b -n 1 | head -25
Procesos (memoria)|ps -eo pid,stat,rss,comm | sort -k3 -rn | head -15
ssrecomp: config|cat "$DIR/ssrecomp.cfg"
ssrecomp: log|tail -150 "$DIR/log.txt"
ssrecomp: audit/perf|grep -aE '\[(audit|perf)\]' "$DIR/log.txt" | tail -80
ssrecomp: binario|ls -l "$DIR/ssrecomp"; file "$DIR/ssrecomp"; md5sum "$DIR/ssrecomp"
EOF

printf '\n# ssrecomp diag — %s\n' "$(date)" >> "$RAW"
cp "$RAW" "$OUT" || exit 1
printf 'Listo. Datos en: %s\n' "$OUT"
