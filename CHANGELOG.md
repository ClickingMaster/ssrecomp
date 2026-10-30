# Changelog

## Unreleased

- **Xbox (experimental):** the setup program can build the game as an app for an
  Xbox One or Series console in Developer Mode (**Build for Xbox**, or `--xbox`,
  on Windows with Visual Studio). It is compiled with clang-cl and packed into a
  signed MSIX without the ROM, which you upload to the console's DevelopmentFiles
  folder through the Device Portal; see "Xbox"
  in the README for installing it. Its tiles can be made from a picture of your
  own (`xbox/icon.png` or `.jpg`, or `--xbox-icon`).
- **Xbox launcher:** a version of the Xbox app without the game in it, built by
  GitHub, for players without a Windows PC; it plays your ROM on the ordinary
  emulator (slower). The single-screen and widescreen views still switch with the
  game's screens: it watches the game's code, read from your ROM on the console.
- **Android TV launcher:** a TV version without the game in it, built by GitHub,
  with a start menu that finds your ROM in the Download folder or on a USB stick;
  it plays with a remote or a controller and keeps the save on the TV's storage.
- **Xbox One and TV speed:** the Xbox app is compiled for the Xbox One's processor
  and uses its cores better for the widescreen 3D. When the console or TV cannot
  keep up, the Xbox and TV apps turn off the 60 fps in-between frames and then
  lower the world detail on their own, and raise them again when there is time to
  spare (a message says so; changing either in the pause menu stops this). Frame
  timings go to `game.log` for reports.
- The game can be built with clang-cl on Windows, as well as with MSYS2's GCC.
- The setup program for macOS can now check for updates and update itself from
  the setup window and with `--update`, matching Linux and Windows.
- A single-screen build of the decompilation (the one-screen game) is recognised when it
  starts and shown as the screen the game itself has chosen, with no merging or
  widescreen changes. The choice is not saved in the settings.

## v0.4.2 — Faster widescreen

**Downloads:** the same setup programs as before; update from the setup window
and run the setup again to rebuild the game (your save is kept). Android
players: press **Build for Android** again and install the new app over the old
one.

- **The widescreen 3D picture takes about half the processor time it did.** It is
  drawn several pixels at a time (SIMD: AVX2 or SSE on PCs, NEON on phones), and
  its working memory now stays in the processor's cache. On PCs it also uses fewer
  threads (about one per core), which leaves more room for the game itself. The
  picture is the same, apart from the odd pixel along the edge of a polygon.
- The widescreen battle view looks for the stats window in one pass instead of four.

## v0.4.1 — Android controllers and update notice

**Downloads:** the same setup programs as before; update from the setup window
and run the setup again to rebuild the game (your save is kept). Android
players: press **Build for Android** again and install the new app over the old
one.

- **Android: controllers such as the Backbone One work.** SDL's own USB drivers
  took over some pads (the Backbone One, PlayStation pads) from Android's
  input, and lost them when they could not open them themselves. All controllers
  now come through Android, which knows them.
- **Android: the app says when there is a new version.** At start it asks
  GitHub for the newest release; when that is newer, a message at the top of the
  screen says to build the app again on the PC (the app holds your own game, so
  it cannot download itself). It is the only thing the app uses the internet for.
- **Lower world detail for slower devices.** WORLD DETAIL in the pause menu now
  also offers 384, 288 and 192 (the DS's own sharpness) as well as 540 to 1440;
  fewer lines draw faster. Left and right go down and up through the choices.
- **FPS counter.** FPS COUNTER in the pause menu shows the frames shown per
  second in the top right corner.

## v0.4.0 — Faster again, cleaner menus

**Downloads:** the setup programs for Linux (x86_64 and arm64), Windows and now
macOS (Apple Silicon and Intel; the Mac app is unsigned, so open it with
right-click → Open the first time). Use the setup window's update button or
download the new one, then run the setup again to rebuild the game (your save
is kept).

- **The bag shows just the bag.** The item description box from the other
  screen is no longer drawn below it.
- **A tidier Pokémon summary.** The Pokémon's name, item and markings sit in
  their own column and the information from both screens is shown side by side, each
  in a rounded card with its contents centred, and no borders cut off.
- **Menus stay on one layout.** Opening the bag, party, Pokégear and the other
  menus from the start menu no longer shows the top screen and then both screens
  before the menu's own view, and closing them no longer falls back to the plain
  bottom screen while they fade out. The view now follows each menu from the
  moment the game starts loading it until it has closed (the game's start and
  end functions of each menu are watched as well as its main one).
- **Faster again: about 30% in the overworld and 20% in battles** (measured
  headless on a 6-core desktop; slower machines such as the Steam Deck and
  phones gain the most). The bottom screen and anything that does not move are
  no longer redrawn every frame, the CPUs switch less often, waits for the game
  card and the touch screen are skipped, and more instructions are recompiled.
  Nothing else changes: in testing, pictures, sound and memory came out the
  same, apart from a one-line difference in a few frames of battle transitions.
- `run.sh` writes `game.log` next to it, as the setup window's Play button does.
- The log lists every controller SDL sees (name, USB IDs, whether its layout is
  known), the SDL version, and the Steam and container settings that can hide a
  controller, for reports like #8.

## v0.3.0 — Faster, updates, logs

**Downloads:** the same three setup programs; use these instead of the earlier
ones and run the setup again to rebuild the game (your save is kept). From this
version on, the setup window can update itself.

### Faster

- **The recompiled game code is about 6–9% faster than melonDS's interpreter**
  (before it was about 16% slower). The common instructions are now plain C++
  instead of calls into the interpreter, and memory accesses and jumps take a
  shorter path. Every instruction was checked against the interpreter
  (`--lockstep`) over 16,000 frames of booting, walking and battling.

### Fixed

- **White strips beside the screen as a battle opens.** The widescreen view showed
  the half of the opening's background, and the 3D pieces, that the game keeps off
  the screen; a row the screen shows in one colour is now that colour beside it.
- **The trainer's legs over the black bar** as he slides out after throwing the
  Poké Ball: beside the screen he is now behind the message box, as on the screen.
- **Fades at the start and end of a battle** reached the sides of the view a frame
  before the screen, so the picture faded unevenly. The sides now use the palettes
  and brightness the screen was drawn with.
- **The stats after a level up** were cut in two, the top half moved to the right
  with the wild Pokémon's side. Windows like it now stay where the game puts them.

### Added

- **Logs for bug reports.** Started from the setup window, the game's messages now
  go to `game.log` (new each time) and a build's output to `build.log`; **Open logs
  folder** opens the folder with both. On Android `log.txt` moved to
  `Android/data/io.github.clickingmaster.ssrecomp/files`, where a file manager or a
  PC can reach it. The renderer in use is now always in the log.

- **Updates from the setup window.** At start it checks for a newer release (shown
  next to the version number); **Update** downloads the setup program for your
  system, checks it against the release's `SHA256SUMS.txt`, puts it in place of
  the old one and restarts. Then press **Build** once to update the game; the
  save and settings are kept. (This version still has to be downloaded by hand.)
- **Issue forms** on GitHub ask for the version, system and log.

- **Portable mode** (#10): with a file `portable.txt` next to the setup program,
  everything is kept in a folder `ssrecomp-data` beside it instead of
  `%LOCALAPPDATA%` (Windows) or `~/.local/share` (Linux). The ROM's location is
  remembered relative to that folder, so it still works on another PC or drive
  letter.

## v0.2.6 — Controller fixes (prerelease)

**Downloads:** the same three setup programs; use these instead of the earlier
ones and run the setup again to rebuild the game.

### Fixed

- **Controllers not working** (#8). Only the first controller found was used;
  on Windows that is often a virtual or idle device (from Steam, DS4Windows,
  vJoy, a racing wheel...), and then nothing pressed on any controller worked.
  Every connected controller now works at once.
- **Generic USB pads** that SDL does not know the layout of were ignored. They
  are now read with the usual layout: the first buttons as A, B, X, Y, then L,
  R, Select, Start, with the D-pad and the left stick. A device whose stick
  rests at one end (some keyboards and mice report themselves as joysticks) has
  its stick ignored, so the player does not walk on their own.
- The controller now also works while the game window is not focused, as when
  it was started from the setup window.

### Tested

- On Linux with a real Xbox Series controller and virtual ones (an Xbox 360
  pad, a generic USB pad, one with a stuck stick). Compiled for Windows, not yet
  tried with a controller there.

## v0.2.5 — Widescreen battles

**Downloads:** the same three setup programs; use these instead of the earlier
ones and run the setup again to rebuild the game (and the APK, on Android).

### New

- **Battles fill the wide screen** instead of having stretched sides. The
  battle scene is drawn wider than the DS screen: the background and the
  platforms continue past its edges, the HP boxes move out to the sides of the
  view, and the wild Pokémon slides over to the right as your own Pokémon is on
  the left. The trainer throwing the Poké Ball walks all the way off screen.
- Move effects stay with the Pokémon they belong to, and the message box is
  left where it is.

### Fixed

- **The battle strip vanished for a moment** between choosing FIGHT and the
  moves appearing, showing the message box under it.

### Known limitations

- Tried on 16:9 only; wider phone screens (20:9) are untested in battles.
- A move effect crossing the gap between the two Pokémon can look stretched
  there for a moment.

## v0.2.4 — Colour fixes

### Fixed

- **Washed-out colours on Windows** (#5): the game asked SDL for its OpenGL ES
  renderer, which on Windows (seen with AMD graphics) can convert every pixel
  as if it were linear light, so the whole picture looked pale. Windows now uses
  SDL's default, Direct3D.
- **The game was always at night.** The DS clock started at midnight on
  1 January 2000 on every launch, so the overworld always had night lighting.
  It now follows the computer's clock, as on a real DS, including after loading
  a quick save.

## v0.2.3 — Android (experimental)

**Downloads:** the same three setup programs; use these instead of the earlier
ones. There is no APK to download: the Android version contains the game's code,
so it is built from your own ROM on your PC, like the PC version.

### New

- **Build for Android** in the setup program (`--android` without a window).
  It downloads Google's Android NDK and SDL2 (once, checked against their
  published checksums), compiles the game for 64-bit ARM Android 7.0+, and makes
  a signed APK to copy to the phone or handheld and install. No Java or Android
  Studio is needed: the APK is packed and signed by `tools/android/apk.py`.
- **On Android:** full screen in landscape; on-screen buttons (D-pad, A/B/X/Y,
  L/R, Start, Select, Menu), hidden while a controller is being used and back at
  a touch; fingers on the picture touch the game's touch screen, several at
  once; Back opens the pause menu; the save is written when the app goes to the
  background; messages and frame timings go to `log.txt` in the app's storage.
- **Tested in Google's Android emulator** (Android 14): installs, updates over
  itself, loads a save, plays with the on-screen buttons and touch, pause menu.
  **Not yet tried on a real phone or handheld**: reports welcome, with
  `log.txt` if something is wrong.

## v0.2.2 — Windows fixes

**Downloads:** the same three setup programs as before; use these instead of the
earlier ones. Already built? Start the new setup program and press **Build**
once: only the changed files are recompiled and your save is kept.

### Fixed

- **Windows: progress was not saved** (#2). Only the first save ever reached
  `soulsilver.sav`; later ones were left in `soulsilver.sav.tmp`, because
  Windows does not let a file be renamed over an existing one. Saves now replace
  the file. If you lost progress this way, see the note below.
- **Windows: the build stopped with "junk at end of line"** in `rc_images.S`
  (#3). The generated assembly ended with a Linux-only line.
- **Windows: "Could not find a package configuration file provided by SDL2"**
  (#3). The setup program now uses MSYS2's CMake ahead of any other copy on the
  PATH, tells CMake where MSYS2's libraries are, and says when SDL2, libpng or
  zlib are missing.
- **Screen jumping around while saving** (#1). During "Saving a lot of
  data…" the layout fell back to the X menu's (the save card beside the
  overworld) and the picture flipped between widescreen and 4:3, because the
  game hardly runs anything else while it writes. The save screen now stays as
  it is until the save is written.
- **Clearer install instructions** in the setup program (#4): it says to open a
  terminal, has a button to copy the commands, and on SteamOS (Steam Deck)
  gives the container steps, as there programs cannot be installed directly.

### Getting back lost progress on Windows (#2)

Close the game, open `%LOCALAPPDATA%\ssrecomp`, rename `soulsilver.sav` to
`soulsilver.sav.old`, then rename `soulsilver.sav.tmp` to `soulsilver.sav`. The
`.tmp` file holds your latest save.

### Tested

- The whole game cross-compiled for Windows with MinGW and run under Wine:
  identical to the Linux build frame for frame, and saving in the game replaces
  the save file. Still not tried on a real Windows PC.

## v0.2.1 — 64-bit ARM Linux

**Downloads:** `ssrecomp-setup-linux-x86_64`,
`ssrecomp-setup-linux-aarch64.AppImage` (64-bit ARM Linux: Raspberry Pi 4/5
with a 64-bit OS, other arm64 boards and laptops) and
`ssrecomp-setup-windows-x86_64.exe` are setup programs, not the game: start
one, choose your own Pokémon SoulSilver (US) ROM, press Build, then Play. Use
these instead of the v0.2.0 ones.

### New

- **Setup program for 64-bit ARM Linux**, as an AppImage
  (`tools/make_appimage.sh`). The game compiles for arm64; cross-compiled and
  run under emulation it is identical to the x86 build frame for frame. Not yet
  tried on real ARM hardware.

### Fixed

- **The game would not compile on ARM, and would crash on older x86 CPUs**: the
  widescreen renderer was always built with AVX2 instructions. They are now
  used only on x86 CPUs that have them; nothing changes on those.
- The recompiler failed when its output folder was on another drive.

### Known limitations

- Windows and ARM builds not yet tried on real hardware (Windows under Wine,
  ARM under emulation).
- See v0.2.0 and v0.1.0 for the rest.

## v0.2.0 — build from your own ROM

**Downloads:** `ssrecomp-setup-linux-x86_64` and
`ssrecomp-setup-windows-x86_64.exe` are setup programs, not the game: start
one, choose your own Pokémon SoulSilver (US) ROM, press Build, then Play. A
compiler and a few libraries are needed; the program says which and how to get
them. A ready-built game cannot be offered, as it would contain the game's code.

### New

- **Setup programs for Linux and Windows**: one file each, made from this
  release's source (`tools/package_setup.py`). They carry the project's source
  and build the game in a folder of their own.
- **Setup window** (`ssrecomp_setup.py`): pick your ROM, press Build, press
  Play. It checks the ROM is the right game, says what is missing to compile
  and how to install it, picks the number of compile jobs from your memory, and
  shows the progress. `python3 ssrecomp_setup.py --rom <file>` does the same
  without a window.
- **No decompilation needed to play.** The code is recompiled straight from
  your ROM (`tools/ndsrom.py` reads and decompresses it); what the
  decompilation used to add comes from `layouts/soulsilver-us.json`, which holds
  addresses and checksums only. The result is identical to building from the
  decompilation. `setup.sh` remains for working on the game's source.
- **Windows (untested):** the code compiles with MinGW, and the setup window
  supports MSYS2 and copies the needed DLLs next to the game. Not yet tried on
  a Windows machine.

### Known limitations

- Tested on Linux only (Ubuntu 24.04 in a container on Bazzite).
- Only Pokémon SoulSilver (US).
- See v0.1.0 for the rest.

## v0.1.0 — first preview

The first public version. Playable from the start of a new game through the
early routes, towns and gyms; the rest of the game has not been played through
with it yet.

**No ready-made download.** A built program would contain the game's code, so
releases are source only: run `./setup.sh` with your own copy of Pokémon
SoulSilver (US) as described in the README.

### What's in it

- **Static recompilation** of the game's ARM9 code from pret/pokeheartgold's
  build output, running on the melonDS core. Checked against melonDS's
  interpreter instruction by instruction (`--lockstep`).
- **One screen, no switching.** Each situation has its own layout that takes the
  useful parts of both DS screens: the overworld with dialogue, the X menu beside
  the overworld, the bag, party, Pokédex, Pokégear, PC, shops, the intro and the
  starter choice.
- **Widescreen overworld:** the 3D world is drawn across the whole 16:9 view at
  a higher resolution (540–1440 lines, "WORLD DETAIL" in the pause menu); trees
  and buildings at the sides are drawn instead of popping in.
- **Battles across the whole screen**, with the touch-screen buttons as a strip
  along the bottom: FIGHT / BAG / POKéMON / RUN and the four moves, chosen with
  left and right, A to pick, B to go back.
- **60 fps:** in-between frames for the 3D scene, which the game updates 30
  times a second.
- **Controller first:** everything is reachable with buttons; a pause menu (Guide
  button or Esc) holds the settings, save states and fast forward.
- **Runs well:** about 9 ms of a 16.7 ms frame at 2560×1440 on a 6-core desktop
  CPU; the picture is composed on the graphics card.
- **`setup.sh`** installs what is missing, builds the game with the
  decompilation, checks it against the retail ROM's SHA-1 and compiles
  everything in one step.

### Known limitations

- Linux only so far (tested on Ubuntu 24.04 in a container on Bazzite).
  Windows, macOS and Android builds do not exist yet.
- Only Pokémon SoulSilver (US). HeartGold and other regions are not supported.
- Later parts of the game, and some screens (radio, contests, the Pokéathlon,
  wireless features), have not been checked and may show a generic two-screen
  layout.
- The widescreen picture has no edge-marking outlines; battles, menus and the
  intro stay 4:3 apart from the battle sides.
- The recompiled code is not yet faster than melonDS's own interpreter; making
  it so is future work.
