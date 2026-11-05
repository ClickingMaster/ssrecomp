# GBA player

The source of the GBA player built into Fulldex: [mGBA](https://mgba.io) with this project's
widescreen renderer (`patches/mgba-wide.patch`, applied to the mGBA commit in `patches/mgba.commit`),
the player's own frontend (`frontend/`) and the widescreen patches for each game (`wide-patches/`).
The release builds point `SSR_GBA_DIR` at this folder.

Not included: the games' box art (`art/*.jpg`, Nintendo's artwork), which the build embeds if it is there.

Licences: mGBA is under the Mozilla Public License 2.0; xBRZ (`frontend/third_party/xbrz`) is GPLv3;
stb_image is public domain or MIT; the rest is under this repository's licence (GPLv3).
