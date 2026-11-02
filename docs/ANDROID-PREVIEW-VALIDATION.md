# Android combined preview - 2 November 2026

Ready for review; no public release or main-branch update has been made.

## Included

- Custom DS/GBA touch controls: drag, resize, save, cancel and reset, stored separately by system and orientation.
- Regional support adapted from PRs #41, #18 and #9: French/Spanish profiles, Italian HeartGold, separate regional saves, and one English/Spanish/Italian interface language setting. Diamond/Pearl remains experimental.
- Android launcher: landscape while auto-rotate is off; portrait or landscape when it is on. The game remains landscape.
- Larger launcher settings text in both orientations, wrapped long values, and DS/GBA FPS counters moved inward from screen edges.
- Native libraries rebuilt for 16 KB memory pages, texture recovery on surface resets, graphics-error fallback and early startup diagnostics.

## Evidence

- Final builds passed: Windows x86_64, Linux x86_64/ARM64, Android arm64/x86_64 and Switch.
- Existing launcher regression check passed; touch editor save/cancel/reset, persistence, bounds and regional-language checks passed.
- Earlier integrated regional candidate passed 3,000-frame full ARM9 lockstep and four English-game snapshot checks. Subsequent changes affect only launcher/overlay rendering, Java orientation and library packaging.
- Pixel 9 Pro XL (4 KB pages): native startup, OpenGL ES launcher, enlarged portrait settings and background/resume passed before the phone was disconnected. Final landscape sizing and FPS inset were tested in the emulator.
- Clean Android 14 emulator (4 KB): exact public APK launcher passed. Combined preview passed locked landscape, auto-portrait, auto-landscape, re-lock to landscape and background/resume. Actual private SoulSilver startup showed an inset 59 FPS counter; this is a UI check, not a performance benchmark.
- Android 15 emulator with confirmed 16,384-byte pages: exact public APK crashed inside linker64 ElfReader::LoadSegments during native loading. The preview loaded both native libraries and displayed the launcher on the same emulator. All four packaged ELF libraries pass the 16 KB alignment validator.
- Exact public Windows and Intel/Apple Silicon Mac packages passed isolated empty-library screenshot checks on native hosts, using SDL dummy video. These do not establish physical GPU compatibility.
- Exact public Linux x86_64 package requires GLIBC_2.38 and fails on Ubuntu 22.04. This separate release compatibility issue remains unresolved.
- Public iOS IPA, Switch NRO and Linux ARM64 package pass structure/architecture checks; actual runtime testing is unavailable here. A device IPA cannot be launched as an iOS Simulator app.

## Limits before publication

- Odin 3 failure is not confirmed fixed until tested on that handheld or matched against its logs/page size. The 16 KB failure is reproduced on x86_64 Android; arm64 preview native startup was tested on the 4 KB Pixel.
- No native Vulkan launcher was added. SDL reports opengles2. Android can optionally run OpenGL ES over Vulkan via ANGLE, so a Vulkan-capable device is not inherently an API mismatch.
- No personal phone settings were changed. Automatic software fallback is present but a physical driver failure has not been reproduced.
- Camera-cutout/rounded-corner clearance still needs a final real-device check.
- Regional ROMs were not available for every contributed language/version; there is no claim of complete regional playthrough coverage.
- No ROMs or personal saves are included in this preview folder/source archive.

## References

- Android 16 KB support: https://developer.android.com/guide/practices/page-sizes
- Android OpenGL ES, Vulkan and ANGLE: https://developer.android.com/games/develop/vulkan/overview
- Android orientation policy: https://developer.android.com/guide/topics/manifest/activity-element
