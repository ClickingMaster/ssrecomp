#!/bin/bash
# PORTMASTER: Pokemon SoulSilver.sh (ssrecomp TrimUI Smart Pro / CrossMix)

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}

# JukaMix/CrossMix puts the launcher in Roms/PORTS and the port data either next
# to it or in Data/ports (PortMaster's convention). Resolve our own folder so
# the launcher works no matter which of the two layouts is used.
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Keep an early log outside Roms/PORTS (the ports menu scans that tree and would
# list the log as a game). If the launcher exits before the game starts, this
# records where it stopped.
EARLY_LOG="/mnt/SDCARD/System/logs/soulsilver-launch.log"
mkdir -p "$(dirname "$EARLY_LOG")" 2>/dev/null
if : > "$EARLY_LOG" 2>/dev/null; then
  exec > "$EARLY_LOG" 2>&1
  set -x
fi

controlfolder=""
for candidate in \
  "/mnt/SDCARD/Apps/PortMaster/PortMaster" \
  "/opt/system/Tools/PortMaster" \
  "/opt/tools/PortMaster" \
  "$XDG_DATA_HOME/PortMaster" \
  "/roms/ports/PortMaster"; do
  if [ -f "$candidate/control.txt" ]; then
    controlfolder="$candidate"
    break
  fi
done

if [ -n "$controlfolder" ]; then
  . "$controlfolder/control.txt"
  [ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && . "${controlfolder}/mod_${CFW_NAME}.txt"
  get_controls
fi

GAMEDIR="$SCRIPT_DIR/soulsilver"
[ -d "$GAMEDIR" ] || GAMEDIR="/mnt/SDCARD/Data/ports/soulsilver"
[ -d "$GAMEDIR" ] || GAMEDIR="/$directory/ports/soulsilver"
[ -d "$GAMEDIR" ] || GAMEDIR="/userdata/roms/ports/soulsilver"
if ! cd "$GAMEDIR"; then
  echo "no existe GAMEDIR=$GAMEDIR" >&2
  exit 1
fi

# Set CPU governor to performance at max frequency (unthrottled 1.8GHz)
for cpu in /sys/devices/system/cpu/cpu[0-3]; do
  echo performance > "$cpu/cpufreq/scaling_governor" 2>/dev/null
  if [ -f "$cpu/cpufreq/cpuinfo_max_freq" ]; then
    cat "$cpu/cpufreq/cpuinfo_max_freq" > "$cpu/cpufreq/scaling_max_freq" 2>/dev/null
  else
    echo 1800000 > "$cpu/cpufreq/scaling_max_freq" 2>/dev/null
  fi
done

export TERM=linux
if : > "$GAMEDIR/log.txt" 2>/dev/null; then
  exec > "$GAMEDIR/log.txt" 2>&1
fi
chmod +x "$GAMEDIR/ssrecomp" 2>/dev/null

if [ ! -x "$GAMEDIR/ssrecomp" ]; then
  echo "falta o no es ejecutable $GAMEDIR/ssrecomp" >&2
  exit 1
fi
if [ ! -f "$GAMEDIR/soulsilver.nds" ]; then
  echo "falta la ROM $GAMEDIR/soulsilver.nds" >&2
  exit 1
fi

export SDL_GAMECONTROLLERCONFIG="${sdl_controllerconfig:-03000000091200000031000000010000,TRIMUI Smart Pro Controller,a:b0,b:b1,x:b2,y:b3,back:b4,start:b5,guide:b12,leftstick:b10,rightstick:b11,leftshoulder:b6,rightshoulder:b7,dpup:h0.1,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,leftx:a0,lefty:a1,rightx:a2,righty:a3,lefttrigger:b8,righttrigger:b9,platform:Linux,}"
export SDL_GAMECONTROLLERCONFIG_FILE="$GAMEDIR/gamecontrollerdb.txt"
export SDL_VIDEODRIVER=mali
export SDL_RENDER_DRIVER=opengles2
export SSR_WIDE_THREADS=2

# PortMaster platform helper
if [ -n "$GPTOKEYB" ]; then
  $GPTOKEYB "ssrecomp" &
  sleep 0.6 # For TSP only, do not move/modify this line.
fi
if type pm_platform_helper >/dev/null 2>&1; then
  pm_platform_helper "ssrecomp" >/dev/null 2>&1
fi

./ssrecomp --fullscreen --save "$GAMEDIR/soulsilver.sav" --config "$GAMEDIR/ssrecomp.cfg" "$GAMEDIR/soulsilver.nds"

# Restore governor to schedutil
for cpu in /sys/devices/system/cpu/cpu[0-3]; do
  echo schedutil > "$cpu/cpufreq/scaling_governor" 2>/dev/null
done

if type pm_finish >/dev/null 2>&1; then
  pm_finish
fi
exit 0
