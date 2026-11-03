# iOS scene lifecycle and black-screen investigation

## What the supplied log proves

The original iOS 27 no-scene-lifecycle crash is resolved. The new log reaches
`[launcher] no window: No connected UIWindowScene for SDL display`. This error
comes from the previous SDL backend patch, before a native SDL window, renderer
or launcher texture has been created. The update worker can continue logging
while the launcher has already returned; that is not evidence of rendering.

In that implementation only `scene:willConnectToSession:options:` schedules the
launcher entry point. Reaching the launcher therefore establishes that the scene
delegate was registered, instantiated and called, assuming the supplied binary
matches this source. A plist-only change cannot fix the window creation failure.

The old lookup requires a member of `UIApplication.connectedScenes` whose screen
is pointer-equal to SDL's cached display screen and whose state is not unattached.
It schedules SDL_main immediately after connection, without waiting for activation.
SDL still enumerates `UIScreen.screens` independently. The error establishes that
no candidate satisfied both conditions; the supplied log does not distinguish
an attachment timing issue from a screen identity mismatch. We cannot reproduce
that distinction on Windows. The fix removes both dependencies rather than
ignoring the failure or substituting an arbitrary connected scene.

## Changes

- The UIKit-supplied application scene is retained explicitly. During connection
  a native scene-owned bootstrap window lets UIKit finish activation. After
  `sceneDidBecomeActive:`, a deferred callback checks foreground-active state,
  screen availability and positive bounds before entering SDL_main once. Failed
  readiness checks return to UIKit; activation and geometry callbacks retry.
  There is no sleep loop, semaphore or nested run-loop wait in window creation.
- SDL display zero is initialised from that scene's screen. Other displays are
  enumerated only for connected window scenes, not inactive physical screens. Primary window
  creation uses the explicit scene rather than rediscovering it through cached
  screen identity. Secondary displays still require an exact active scene match.
  Native windows are created with `initWithWindowScene:`; `setScreen:` is no
  longer called after attaching a scene. Showing the real SDL window replaces
  the bootstrap window and retains SDL keyboard/mouse focus on display zero.
- Scene geometry, foreground and reconnect callbacks rebind surviving primary
  SDL windows and their existing controllers/views. Display bounds/modes track
  scene geometry, and stale physical-screen mode objects are released when the
  scene's screen changes. A physical screen disconnect does not delete the
  application's logical display. A different display's window is not moved
  into the application scene.
- Views use the full window bounds. View scale/layout and SDL resize/expose
  events update drawable dimensions and renderer viewports, including pixel
  density changes without a point-size change. Both the iOS 14 geometry callback
  and the SDK-guarded iOS 26+ effective-geometry callback are handled.
- The launcher still creates its renderer for its own SDL window, draws into
  that renderer and calls SDL_RenderPresent on it. New logs record the association,
  point and pixel sizes, composition size and the first submitted frame after
  each drawable-size change. Zero-size/unavailable drawables return to the event
  loop instead of allocating a 1-pixel canvas. A present log means the call was
  submitted; only a simulator/device screenshot proves pixels were displayed.
- Existing iOS deployment target, device families, manifest, orientation hints,
  touch paths and emulator/gameplay code are unchanged by this follow-up.

The SDL patch remains against hash-pinned SDL 2.32.10. CMake recognises and
reverses the exact v1 patch before applying this revision to an existing build
cache. Arbitrary modified caches still fail rather than being overwritten.
Other platforms retain upstream SDL; launcher diagnostics are guarded by SSR_IOS.

## Diagnostics to capture on the Mac

Early messages may precede redirection into Documents/game.log. Capture simulator
console output as well as game.log. Expected order (geometry messages may interleave):

1. `[ios-scene] application launched; scene delegate registered=1`
2. `SDLUIKitSceneDelegate instantiated`, then `will-connect`
3. `did-become-active`, then `starting-SDL-main` once
4. `[ios-display] initialising ...` and scene/display dimensions
5. `[ios-window] create ...`, then `shown ... key=1 hidden=0`
6. `[launcher] renderer ...`, `association=correct`, `drawable ...`
7. `[launcher] frame presented ...`

Logs include scene/session identifiers, activation state, screen identity and
bounds. On fold/unfold expect geometry updates, updated drawable dimensions and
another frame-presented message if pixel dimensions changed. Initialising output
also records the legacy main-screen identity to expose differences from scene.screen.

## Checks available on Windows

Run with pristine SDL 2.32.10 sources, CMake 3.31.10, Ninja, Git and Python:

```sh
python3 tools/ios/check_scene_integration.py /path/to/SDL2-2.32.10
```

The check parses device/simulator plist templates, checks scene startup/window
wiring, configures an isolated CMake fixture with the actual patch integration,
reconfigures it, upgrades a fixture containing the exact previous patch, and
checks rejection of an incompatible cache. This is not a UIKit compile or a
runtime lifecycle test. Windows has no Apple SDK or iPhone Duo simulator.

## Rebuild and validate on the Mac

Apply the supplied uncommitted change set to the Mac checkout first. Substitute
the existing generated input paths; no cache deletion should be needed:

```sh
SSR_IOS_TARGET=simulator SSR_IOS_ARCH=arm64 \
  tools/ios/build_all.sh /path/to/TABLES /path/to/ART /path/to/ICON
xcrun simctl install booted "build-ios-dist/Pokemon Fulldex.app"
xcrun simctl launch --terminate-running-process --console booted io.github.mamesj.pokemonds
```

Keep the console attached while testing, using another terminal for screenshots:

```sh
xcrun simctl io booted screenshot /tmp/fulldex-cover.png
# Unfold the simulator, then capture the inner display.
xcrun simctl io booted screenshot /tmp/fulldex-inner.png
DATA="$(xcrun simctl get_app_container booted io.github.mamesj.pokemonds data)"
cat "$DATA/Documents/game.log"
```

Verify cold launch folded and unfolded, game-list visibility to the scene edges,
repeated fold/unfold while running, touch alignment, portrait/landscape transitions,
background/foreground, scene reconnect, and DS/GBA launcher/game round trips.
Check that `starting-SDL-main` occurs once per process and that drawable and canvas
logs change with the display. Intentional gameplay aspect-ratio padding may remain.
Repeat on an iOS 14-compatible runtime where available, a physical iPhone and an
iPad (including app-window resizing), with controller input/audio and Files opening.
Device packaging uses `SSR_IOS_TARGET=device` and the existing signing process.

No iOS build or runtime success is claimed until these Mac/device checks pass.

## GitHub build coverage

The iOS download workflow now uses the Xcode 27 Apple Silicon runner for device
packaging and ARM64 simulator testing. The Intel simulator job remains for older
toolchain coverage. The ARM64 first-start check requires an installed iOS 27+
runtime, prefers iPhone Duo where available, and also tests an iPad. Exact Xcode,
SDK/runtime versions and selected devices are printed in the run logs. This does
not claim a specific 27.2 runtime or automated fold/unfold coverage.

The smoke check requires a correctly associated renderer, positive drawable
sizes, a submitted frame for that drawable, and a nonblank screenshot that fills
the initial full-screen scene. It compares screenshot pixels with drawable
pixels rather than the smaller software-composition canvas. Screenshots still
need visual review; this is failure detection, not a game-list image comparison.
It catches the supplied no-window log and black or uniformly blank screenshots.

Artifacts retain the sideloadable IPA and checksums, and add zipped simulator
apps and architecture-specific screenshots/console/game logs. Cache-upgrade
checks run against a separately downloaded, SHA-256 verified pristine SDL source.
Use Actions > iOS download > Run workflow after publishing the reviewed changes.
The workflow still uses the existing mac-build-inputs release and optional GBA
inputs; no ROM, save or recompiled game code is added.

Host validation:

```sh
python3 -m pip install Pillow
python3 tools/ios/test_simulator_start.py
```
