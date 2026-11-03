# iOS scene lifecycle

## Diagnosis

The reported `___UIApplicationEvaluateRuntimeIssueForNoSceneLifecycleAdoption_block_invoke`
trap matches Apple's iOS 27 requirement for apps linked against the latest SDK:
https://developer.apple.com/documentation/uikit/transitioning-to-the-uikit-scene-based-life-cycle

The original packaged Info.plist had no `UIApplicationSceneManifest`. The pinned
SDL 2.32.10 backend scheduled `SDL_main` from `didFinishLaunchingWithOptions:` and
created `UIWindow` objects using screen bounds without attaching a window scene.
This is a source-level confirmation of the lifecycle mismatch, not a reproduction
of the supplied simulator crash on this Windows development machine.

## Implementation

`cmake/MacDependencies.cmake` applies `tools/ios/sdl2-scene-lifecycle.patch` to the
hash-pinned SDL source before configuring it, for iOS only. It also handles an
existing populated source cache. An incompatible source cache fails configuration.
macOS and other platforms retain upstream SDL behaviour.

The scene manifest declares one application scene and `SDLUIKitSceneDelegate`.
Scene connection schedules the existing SDL main entry point once, after UIKit
finishes connecting. Every SDL window is attached to the connected `UIWindowScene`
before the existing SDL controller and views are installed. This also covers
launcher/game window replacement and the optional GBA integration. Scene reconnect
reattaches the surviving window without restarting the emulator. Scene notifications
feed SDL's existing foreground/background handlers; file-opening scene callbacks
feed its existing drop-file path, including initial launch URLs.

Scene/window bounds replace screen bounds and the old orientation dimension-swap
workaround when sizing views. SDL's existing flexible views, layout resize events
and renderer drawable-size queries continue to handle geometry changes and touch
coordinates. The full-screen-only restriction is removed to allow resizing and
fold/unfold changes. The iPad manifest permits all four orientations; launcher
and game orientation hints remain in place. Status bar visibility is controlled
by SDL's view controller. No SDK version, deployment metadata or runtime checks
are spoofed. Minimum iOS remains 14.0 and device families remain iPhone and iPad.

## Checks performed on Windows

- CMake 3.31.10/Ninja isolated dependency integration: pristine SDL patching,
  repeat cached configuration and incompatible-source rejection passed.
- Device and simulator plist templates parse, declare the scene class/delegate,
  retain iOS 14.0 and both device families, and allow resizing.
- Build script shell syntax check and Git whitespace checks passed.

Repeat the host checks with a pristine SDL 2.32.10 source tree, CMake, Ninja and Git:

```sh
python3 tools/ios/check_scene_integration.py /path/to/SDL2-2.32.10
```

These checks do not compile Objective-C or prove runtime behaviour. Windows has
no Xcode/UIKit SDK, iOS simulator or connected physical iOS device. No iOS binary
was built here, and launcher, touch, gameplay and rendering preservation still
need the runtime checks below.

## Mac verification required

Use the existing generated inputs (substitute their actual paths):

```sh
SSR_IOS_TARGET=simulator SSR_IOS_ARCH=arm64 tools/ios/build_all.sh TABLES ART ICON
xcrun simctl install booted "build-ios-dist/Pokemon Fulldex.app"
xcrun simctl launch booted io.github.mamesj.pokemonds
```

1. Cold launch on the iOS 27.2 iPhone Duo simulator, folded and unfolded. Verify
   the launcher appears and no lifecycle trap occurs. Inspect the installed
   Info.plist for the scene manifest if testing an old cached installation.
2. Fold/unfold and rotate while the launcher, settings and a game are visible.
   Verify the view fills the available scene, the drawable updates without large
   compatibility margins, and touch controls and menu hit targets stay aligned.
   Intentional game aspect-ratio padding may still be present.
3. Launch DS and optional GBA games, return to the launcher, and repeat. Verify
   orientation changes, rendering, audio, controller input and touch input.
4. Background/foreground, lock/unlock, and disconnect/reconnect the scene. Verify
   gameplay resumes and the launcher/main entry point does not start twice.
5. Open a supported GBA document from Files both with the app closed and running.
6. Repeat launch, rotation, touch and suspend/resume checks on an iOS 14 simulator
   where available, a physical iPhone and an iPad. On iPad also resize the app.
7. Build the device package with `SSR_IOS_TARGET=device` using the same command;
   install with the existing signing/sideloading process and repeat the checks.

The existing GitHub iOS workflow remains useful for basic iPhone/iPad first-start
coverage, but its macOS Intel runner is not evidence of ARM64 iOS 27 Duo behaviour.
