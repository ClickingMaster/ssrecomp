# TrimUI Smart Pro / CrossMix port

This folder contains the extra pieces for running this fork on a TrimUI Smart
Pro (Allwinner A133P, 4x Cortex-A53, 1 GB RAM) with CrossMix/JukaMix:

- `build_tsp.sh` builds an aarch64 binary against glibc 2.31 (runs on the
  firmware's glibc 2.33).
- `deploy_to_sd.sh` copies the binary, settings and launcher to the SD card.
- `Pokemon SoulSilver.sh` is the PortMaster launcher.
- `Pokemon SoulSilver ES.sh` selects your own Spanish ROM with a separate save.
- `diag.sh` collects system and game diagnostics on the console.
- `soulsilver/` holds the settings template and controller database. **The ROM
  and the built binary are not included** and are ignored by git.

## Build

You need Docker, Python 3, and your own legally obtained SoulSilver (USA) ROM.

    ./port/build_tsp.sh /path/to/SoulSilver.nds

The script generates `gen/` from your ROM (never commit it), builds SDL2 2.30.8
and the game in a Debian bullseye container, and leaves the binary in
`port/soulsilver/ssrecomp`.

The build is long: `gen/` has about 1.7 million recompiled instructions.
The build input must still be the USA ROM; the Spanish launcher uses the
interpreter in that binary, not Spanish recompiled code.

For an optimisation comparison:

    SSR_GAMECODE_OPT=O2 ./port/build_tsp.sh /path/to/your/usa.nds

The default is `O1`; changing it rebuilds the generated code. No ARM64 `O2`
speed-up has been established yet.

## Deploy

Insert the SD card in the PC and run:

    ./port/deploy_to_sd.sh

If the ROM is not at `port/soulsilver/soulsilver.nds`, pass it explicitly:

    SSR_ROM=/path/to/SoulSilver.nds ./port/deploy_to_sd.sh

The script detects the CrossMix layout (`Roms/PORTS` + `Data/ports`), keeps
existing saves, settings and ROMs. `SSR_SD_MOUNT=/path/to/card` selects a mounted
card explicitly. `FORCE_ROM=1` replaces ROMs only when explicitly requested.

### Spanish entry (experimental)

Supply your own Spanish SoulSilver ROM (`IPGS`):

    SSR_ROM_ES=/path/to/your/spanish.nds ./port/deploy_to_sd.sh

The script copies it as `soulsilver_es.nds` and adds **Pokemon SoulSilver ES**
beside the English entry. The two entries share settings and the binary, but
use different saves (`soulsilver.sav` and `soulsilver_es.sav`) and quick states.
Keep both launchers together: the Spanish one calls the common launcher.
No ROM, save or generated game code is supplied by this repository.

The interpreter can execute the Spanish ROM without generating Spanish C++.
This does **not** provide a Spanish address map: automatic screen selection,
merged menus and widescreen rules use USA game addresses and recompiler probes,
so they cannot be assumed to work in this mode. Use **SCREEN LAYOUT** and
**LARGE SCREEN** in the controller-operated pause menu when needed. A complete
Spanish playthrough has not been verified.

## Performance on the TSP

The default settings are a performance profile: widescreen 3D, 60 fps
interpolation and smooth scaling are off. The launcher uses `--interp` and
`SSR_NO_ASYNC=1`, following the device comparisons recorded in
[`PERFORMANCE.md`](PERFORMANCE.md). The asynchronous pipeline is already
implemented; it is disabled by the handheld launcher, not missing from the code.

The launcher requests the available 2.0 GHz CPU maximum and stops `syncthing`
and `musicserver` to release memory (they restart on reboot). The firmware may
change the clock again. These settings do not establish stable 60 fps in play.

You can turn them back on one at a time in the pause menu to compare:

- **WIDESCREEN WORLD** — the overworld fills the wide picture.
- **60 FPS** — draws in-between 3D frames.
- **SMOOTH SCALING** — linear filtering.

On a 4-core device, `SSR_NO_RENDER_THREAD=1` (add it to the launcher) turns off
melonDS's 3D render thread and can reduce contention.

For a lightweight live log, enable **PERF LOG** in the pause menu, or set
`audit=1` in the config or `SSR_AUDIT=1` in the launcher. It records displayed
fps, scene, CPU mode, rendering settings, audio queue depth and pipeline mode
every two seconds in `Data/ports/soulsilver/log.txt`.

For phase timings, run once with `SSR_PERF=1`: it reports time in emulation,
scene selection, composition, presentation and waiting. **It disables the
asynchronous pipeline**, so it cannot measure the pipeline's overlap directly.
For system diagnostics, run `./diag.sh /path/to/report.txt` from the deployed
data folder. Missing tools on the firmware are noted and each step is timed out.

## Known issue: force power-off and the SD card

JukaMix uses a 116 GB FAT32 partition. If the console is force-powered off, the
FAT dirty bit stays set; the device's `fsck.fat` can fail with `malloc: Cannot
allocate memory` on a card that size and the console can hang at the boot logo.
Shut down from the menu instead. If it happens, repair the card on a PC:

    sudo fsck.fat -a /dev/sdX1

## Legal

This fork contains no game code or data: no ROM, saves, save states, or the
generated `gen/` directory. Supply your own dumped ROMs; the recompiler build
requires the USA version, while the optional Spanish entry uses the interpreter.
See `../LICENSE` and `../NOTICE`. Do not submit anything from this project to
pret/pokeheartgold; their contribution rules do not accept AI-assisted code.
