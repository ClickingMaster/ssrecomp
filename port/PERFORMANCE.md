# TrimUI Smart Pro: measured results and remaining work

This is the handoff for the TSP work on branch `tsp`. It replaces the earlier
working notes, which mixed older conclusions with later measurements. No ROM,
snapshot, memory dump or generated game code is included here.

## Device and deployment

- Four Cortex-A53 cores, approximately 1 GB RAM, 1280x720 display.
- GPU confirmed by an EGL/GLES probe: **PowerVR Rogue GE8300**, OpenGL ES 3.2.
  SDL calls its video driver `mali`; that name does not identify this GPU.
- TinaLinux kernel 4.9.191; firmware glibc 2.33. The bullseye cross-build uses
  glibc 2.31 for compatibility. Build on the PC, run on the console.
- Supported CPU frequencies include 1.8 and 2.0 GHz. The firmware was observed
  restoring a 1.8 GHz cap, so the launcher reasserts 2.0 GHz before starting.
- Current launcher: interpreter, async disabled, widescreen and 60 fps
  interpolation off in the supplied config. The native 3D render thread remains
  available; `SSR_NO_RENDER_THREAD=1` is a separate comparison.

## Repeated interpreter versus recompiler measurement

The same private interior snapshot was used for each run of the USA ROM
(`IPGE`), with 3000 emulated frames:

    ./ssrecomp <your-usa.nds> --headless --no-save --no-wide \
        --state-load <your-interior.state> --frames 3000

Repeat with `--interp` for the interpreter. Elapsed seconds were measured using
`/proc/uptime` on the console. Throughput is 3000 divided by elapsed seconds.

| Mode | Run 1 | Run 2 | Run 3 |
|---|---|---|---|
| Recompiled | 44.90 s / 66.8 fps | 44.18 s / 67.9 fps | 44.36 s / 67.6 fps |
| Interpreter | 35.94 s / 83.5 fps | 36.82 s / 81.5 fps | 36.80 s / 81.5 fps |

The interpreter provided about **22% more emulated frames per second** in this
test. Recompiled runs reported 0.00% interpreter fallbacks. This supports the
launcher choice for this device and scene; it does not measure live presentation,
Spanish gameplay, overworld movement or battles.

Instruction-cache pressure, generated-code size and per-instruction guards are
possible explanations for the A53 result, not conclusions of a sampled profile.
The PC's reported 6–9% recompiler advantage must not be applied to this device.

## Other observations, with their limits

- Earlier working notes record the async pipeline as about 16% slower on the
  A53. Raw per-run timings were not retained in the handoff, so treat that as a
  provisional result to reproduce. It motivated `SSR_NO_ASYNC=1` in the launcher.
- A live session reported 27–30 displayed fps, with one core's worth of CPU
  use and much of the total CPU idle. That suggests a serial bottleneck, but
  does not identify whether execution, rendering or synchronisation dominates.
- After background services were stopped, the recorded available memory was
  about 564 MB and temperature about 46.5 degrees C. Earlier memory pressure
  cannot explain every subsequent slowdown.
- The GLES probe measured about 0.103 ms for a 256x192 RGBA texture upload and
  1.03 ms for readback. These small operations do not establish the cost of
  complete composition or presentation.
- A local experimental 30 Hz loop cap made the game run at half speed. It is
  not published here and must not be confused with presentation-only frameskip.

The old README's statement that the software 3D rasteriser is the bottleneck
was not established by these measurements. Likewise, the headless/live gap
cannot be assigned to VSync without testing the same scene and settings.

## What is already implemented

- `frontend/main.cpp` has an emulation/presentation pipeline with double
  buffering. `SSR_NO_ASYNC=1` disables it; headless, lockstep, screenshots and
  `SSR_PERF` also select the serial path.
- Native software 3D rendering has its own thread, and widescreen rendering
  has worker threads. Adding four threads is not itself a four-core speed-up.
- `SSR_AUDIT=1` / **PERF LOG** records live status without forcing serial mode.
  Its `fps` counts presented frames, not emulated CPU speed. `interp60` means
  in-between 3D frames; `engine` distinguishes interpreter from recompiler.
- `SSR_PERF=1` records phase timings but disables the async pipeline.
- `SSR_GAMECODE_OPT` selects O1/O2/O3/Os for generated code. O1 remains the
  default; no ARM64 O2 improvement has been verified in this handoff.

## Spanish mode and JIT investigation

The Spanish (`IPGS`) launcher uses `--interp`, separate saves and the same
binary. The build still needs USA-generated sources. USA address-based probes
do not provide reliable scene detection for Spanish or interpreter execution;
manual layout controls remain available. Installation alone is not a verified
Spanish playthrough, and cross-region save compatibility has not been tested.

melonDS includes an ARM64 JIT backend (`melonDS/src/ARMJIT_A64/`). It is **not
enabled or benchmarked here**: the root CMake forces `ENABLE_JIT=OFF`, and
`MakeConsole` in `frontend/main.cpp` passes `args.JIT = std::nullopt`.

Enabling it needs explicit CPU-mode selection and preserving interpreter mode
for comparisons. Recompiler-based scene probes also need an alternative.
Executable-memory permissions, correctness and memory use must be checked on
the real firmware, rather than inferred from the backend's presence. The current
`--lockstep` checks recompiled execution against the interpreter, not JIT.

## Next measurements

1. Capture an overworld and battle snapshot locally. Compare interpreter and
   recompiler three times per scene with identical settings and input.
2. Capture live audit and phase timings in the actual slow scene; distinguish
   emulation, scene/composition work, presentation and intentional waiting.
3. Reproduce async on/off comparisons. For overlapping execution, do not use
   `SSR_PERF` as though it left the pipeline enabled.
4. Capture an interpreter sampling profile with `SSR_SAMPLE` and analyse it
   using `tools/samples.py`, keeping game-derived snapshots private.
5. Finish measuring the O2 build before claiming a speed-up. Evaluate JIT as
   a separate experiment, including the frontend's USA probe dependencies.

Share timings and source changes, not the snapshots or generated binaries.
