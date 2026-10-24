# TrimUI Smart Pro / CrossMix port

This folder contains the extra pieces for running this fork on a TrimUI Smart
Pro (Allwinner A133P, 4x Cortex-A53, 1 GB RAM) with CrossMix/JukaMix:

- `build_tsp.sh` builds an aarch64 binary against glibc 2.31 (runs on the
  firmware's glibc 2.33).
- `deploy_to_sd.sh` copies the binary, settings and launcher to the SD card.
- `Pokemon SoulSilver.sh` is the PortMaster launcher.
- `soulsilver/` holds the settings template and controller database. **The ROM
  and the built binary are not included** and are ignored by git.

## Build

You need Docker, Python 3, and your own legally obtained SoulSilver (USA) ROM.

    ./port/build_tsp.sh /path/to/SoulSilver.nds

The script generates `gen/` from your ROM (never commit it), builds SDL2 2.30.8
and the game in a Debian bullseye container, and leaves the binary in
`port/soulsilver/ssrecomp`.

The build is long: `gen/` has about 1.7 million recompiled instructions.

## Deploy

Insert the SD card in the PC and run:

    ./port/deploy_to_sd.sh

If the ROM is not at `port/soulsilver/soulsilver.nds`, pass it explicitly:

    SSR_ROM=/path/to/SoulSilver.nds ./port/deploy_to_sd.sh

The script detects the CrossMix layout (`Roms/PORTS` + `Data/ports`), keeps
existing saves and does not overwrite the ROM.

## Performance on the TSP

The default settings are a performance profile: widescreen 3D, 60 fps
interpolation and smooth scaling are off. The software 3D rasteriser is the
bottleneck; the widescreen pass and the in-between frames are extra 3D passes
and cost more than this CPU can afford at 60 fps.

You can turn them back on one at a time in the pause menu to compare:

- **WIDESCREEN WORLD** — the overworld fills the wide picture.
- **60 FPS** — draws in-between 3D frames.
- **SMOOTH SCALING** — linear filtering.

On a 4-core device, `SSR_NO_RENDER_THREAD=1` (add it to the launcher) turns off
melonDS's 3D render thread and can reduce contention.

If it still does not keep up, run once with `SSR_PERF=1` in the launcher and
send `Data/ports/soulsilver/log.txt`: it prints the time spent in emulation,
scene selection, composition, presentation and waiting.

## Known issue: force power-off and the SD card

JukaMix uses a 116 GB FAT32 partition. If the console is force-powered off, the
FAT dirty bit stays set; the device's `fsck.fat` can fail with `malloc: Cannot
allocate memory` on a card that size and the console can hang at the boot logo.
Shut down from the menu instead. If it happens, repair the card on a PC:

    sudo fsck.fat -a /dev/sdX1

## Legal

This fork contains no game code or data: no ROM, saves, save states, or the
generated `gen/` directory. You must own SoulSilver (USA) and dump it yourself.
See `../LICENSE` and `../NOTICE`. Do not submit anything from this project to
pret/pokeheartgold; their contribution rules do not accept AI-assisted code.
