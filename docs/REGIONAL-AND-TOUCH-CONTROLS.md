# Android touch layouts and regional DS support

Android: in a DS game's pause menu choose CONTROLS > TOUCH CONTROLS. In a GBA
game choose TOUCH CONTROLS directly. Drag a control, or select one with PREV/NEXT,
and use -/+ to resize it from 50% to 175%. SAVE applies the layout; CANCEL discards
changes. RESET restores the current orientation's defaults after SAVE. DS/GBA and
portrait/landscape layouts are kept separately. Editing pauses the game.

The interface has one LANGUAGE setting in DISPLAY: AUTO, ENGLISH, ESPAÑOL or
ITALIANO. AUTO uses the system's first preferred language, with English as fallback.
This translates the launcher and DS menus; unknown/dynamic text can remain English.
Translated pause menus expand to fit their labels. GBA's own menus are unchanged.

## Supported regional profiles

| Games | Profiles added |
|---|---|
| Black, White, Black 2, White 2, SoulSilver, HeartGold, Platinum | French revision 0 (#18) |
| White, Platinum, SoulSilver, HeartGold | Spanish (#41) |
| HeartGold | Italian (#9) |
| Diamond and Pearl | Experimental English; Diamond also Spanish (#41), German and Italian; Pearl also French, Spanish, German and Korean |

Each language has a separate save filename, such as heartgold.sav,
heartgold-fr.sav, heartgold-es.sav and heartgold-it.sav. Unsupported language codes
are not silently treated as supported English copies. Existing saves are never
copied over another version's save. These address profiles come from the
contributors; they are not complete playthrough guarantees. Diamond/Pearl remain
experimental and are labelled that way in the game list.

## Building with the GBA touch controls

The GBA player is a separate source input. Preserve its original directory and
prepare an isolated copy from the repository root:

    python3 tools/gba/prepare_touch_controls.py /path/to/gba-player build-gba-touch-ui

Use that output as SSR_GBA_DIR, with the existing patched mGBA and GBAW_PATCHES
inputs. No ROMs, saves or compiled game code are copied by this preparation step.
The shared editor's authoritative header is frontend/touchlayout.h. The preparation
script copies that header into the isolated GBA input after applying the GBA patch.

The regular platform build scripts include the regional function tables. Existing
Apple address-only inputs can be updated locally without re-reading a ROM:

    python3 tools/soulsilver/tables_only.py inputs/build-gen-tables inputs/build-gen-tables-regional

Point the Mac/iOS build at that new table folder. Keep the original inputs intact.

## Verification

The candidate has compile checks for Windows, Linux x86_64/ARM64, Android arm64-v8a/
x86_64 and Switch. Automated UI fixtures cover layout persistence, bounds, hit
handling, save/cancel/reset, localisation, accented text and separate regional saves.
The custom DS/GBA layouts were exercised on a Pixel 9 Pro XL. Private gameplay
snapshots are tested with save writing disabled. See the preview's validation report
for the completed checks and remaining device/gameplay limitations.

Mac Intel/Apple silicon and iOS device/simulator builds also compiled for publication. The device IPA and both Mac ZIPs include the complete regional tables. Regional gameplay coverage remains limited as described above.
