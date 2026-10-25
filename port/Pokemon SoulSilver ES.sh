#!/bin/bash
# PORTMASTER: Pokemon SoulSilver ES.sh (ssrecomp TrimUI Smart Pro / CrossMix)

# Share the device settings with the English launcher, but keep saves separate.
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
export SSR_ROM_NAME=soulsilver_es.nds
export SSR_SAVE_NAME=soulsilver_es.sav
exec bash "$SCRIPT_DIR/Pokemon SoulSilver.sh"
