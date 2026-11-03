"""Host-only integration check; no Apple SDK or generated game inputs required.

python tools/ios/check_scene_integration.py /path/to/pristine/SDL2-2.32.10
Requires CMake, Ninja and Git on PATH. Does not compile or run UIKit.
"""
from pathlib import Path
import os
import plistlib
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    source = Path(sys.argv[1]).resolve()
    script = (ROOT / "tools/ios/build_all.sh").read_text(encoding="utf-8")
    template = script.split('<<PLIST\n', 1)[1].split('\nPLIST', 1)[0]
    for platform in ("iPhoneOS", "iPhoneSimulator"):
        xml = template.replace("$PLATFORM", platform).replace("$MIN_IOS", "14.0")
        xml = xml.replace("$ICONS", "").replace("$GBA_DOCS", "")
        info = plistlib.loads(xml.encode())
        manifest = info["UIApplicationSceneManifest"]
        config, = manifest["UISceneConfigurations"]["UIWindowSceneSessionRoleApplication"]
        assert config["UISceneDelegateClassName"] == "SDLUIKitSceneDelegate"
        assert config["UISceneClassName"] == "UIWindowScene"
        assert manifest["UIApplicationSupportsMultipleScenes"] is False
        assert info["MinimumOSVersion"] == "14.0" and info["UIDeviceFamily"] == [1, 2]
        assert not info.get("UIRequiresFullScreen", False)
    # Keep the tarball fixture inside a Git checkout: git apply must not silently
    # skip the files after discovering the enclosing repository.
    with tempfile.TemporaryDirectory(prefix="build-ios-scene-", dir=ROOT) as tmp:
        tmp = Path(tmp)
        sdl = tmp / "sdl"
        for name in ("SDL_uikitappdelegate.h", "SDL_uikitappdelegate.m", "SDL_uikitevents.m", "SDL_uikitvideo.m", "SDL_uikitwindow.m", "SDL_uikitmodes.h", "SDL_uikitmodes.m", "SDL_uikitviewcontroller.m"):
            dest = sdl / "src/video/uikit" / name
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source / "src/video/uikit" / name, dest)
        (sdl / "CMakeLists.txt").write_text("add_library(scene_fixture INTERFACE)\n")
        harness = tmp / "harness"
        harness.mkdir()
        (harness / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.21)\nproject(ScenePatchCheck NONE)\n'
            'set(CMAKE_SYSTEM_NAME iOS)\n'
            f'set(FETCHCONTENT_SOURCE_DIR_SSR_SDL2 "{sdl.as_posix()}")\n'
            f'include("{(ROOT / "cmake/MacDependencies.cmake").as_posix()}")\n')
        command = ["cmake", "-G", "Ninja", "-S", str(harness), "-B", str(tmp / "build")]
        subprocess.run(command, check=True)
        delegate = sdl / "src/video/uikit/SDL_uikitappdelegate.m"
        assert "@implementation SDLUIKitSceneDelegate" in delegate.read_text()
        code = delegate.read_text()
        # Wiring regressions detectable without an Apple SDK. These assertions
        # are source checks, not evidence of scene activation at runtime.
        connection = code.split('- (void)scene:(UIScene *)scene willConnectToSession:', 1)[1].split('- (void)scheduleSDLMain', 1)[0]
        assert 'postFinishLaunch]' not in connection
        assert 'initWithWindowScene:fulldexScene' in connection
        assert 'sceneDidBecomeActive:' in code
        assert 'scene.activationState != UISceneActivationStateForegroundActive' in code
        assert 'bounds.size.width <= 0 || bounds.size.height <= 0' in code
        assert 'if (fulldexMainStarted) return;' in code
        windows = (sdl / "src/video/uikit/SDL_uikitwindow.m").read_text()
        assert '(display == SDL_GetDisplay(0)) ? UIKit_GetApplicationScene() : nil' in windows
        assert '[uiwindow setScreen:' not in windows
        modes = (sdl / "src/video/uikit/SDL_uikitmodes.m").read_text()
        assert 'UIKit_AddDisplay(scene.screen, SDL_FALSE)' in modes
        assert 'display->desktop_mode.w = (int)size.width' in modes
        assert 'SDL_WINDOWEVENT_SIZE_CHANGED' in code
        first = delegate.read_bytes()
        subprocess.run(command, check=True)
        assert delegate.read_bytes() == first, "cached configure changed the patched source"
        # Simulate an existing Mac build using the exact previous patch.
        env = dict(os.environ, GIT_CEILING_DIRECTORIES=str(tmp))
        subprocess.run(["git", "apply", "--reverse", str(ROOT / "tools/ios/sdl2-scene-lifecycle.patch")], cwd=sdl, env=env, check=True)
        subprocess.run(["git", "apply", str(ROOT / "tools/ios/sdl2-scene-lifecycle-v1.patch")], cwd=sdl, env=env, check=True)
        assert '@implementation SDLUIKitSceneDelegate' in delegate.read_text()
        assert 'startSDLIfReady' not in delegate.read_text()
        subprocess.run(command, check=True)
        assert delegate.read_bytes() == first, "previous patch did not upgrade to the current implementation"
        subprocess.run(command, check=True)
        assert delegate.read_bytes() == first
        delegate.write_text("/* incompatible cached SDL source */\n")
        result = subprocess.run(command, capture_output=True, text=True)
        assert result.returncode != 0 and "SDL2 scene patch does not match" in result.stderr
    print("PASS: manifests, scene wiring, fresh/cached patching, previous-patch upgrade, incompatible-source rejection")


if __name__ == "__main__":
    main()
