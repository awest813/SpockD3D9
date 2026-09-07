# macOS Testing Guide

Checklist for validating SpockD3D9 on macOS before game-hosting work
(Fallout 3 / Milestone F). Run these on a real Mac with MoltenVK installed.

## Prerequisites

```bash
brew install meson ninja glslang sdl3 sdl2 molten-vk vulkan-loader vulkan-headers spirv-headers
```

For the experimental PE `d3d9.dll` cross-build:

```bash
brew install mingw-w64
```

## 1. Native build + smoke test (blessed path)

This validates the macOS `libdxvk_d3d9.dylib` and the fixed-function shader
pipeline (including `D3DRS_WRAP0–15`).

```bash
./scripts/test-macos-native.sh
```

Or manually:

```bash
./package-native.sh dev ./build --no-package --dev-build
export DYLD_LIBRARY_PATH="$(find build -path '*/usr/lib' -type d | head -1)"
export DXVK_WSI_DRIVER=SDL3
/path/to/d3d9-clear 60
```

**Pass criteria:** exit code 0, log ends with `d3d9-clear: OK`.

The Gamebryo / Fallout 3-style device probe (`d3d9-gamebryo-probe`) runs the same
way when built (SDL3 preferred). It exercises the **Track A (MoltenVK)** path beyond
`d3d9-clear`: BCn and depth format queries, display mode enumeration, MSAA checks
(logged, non-fatal if unavailable), SM3 caps, a fixed-function `DrawPrimitiveUP`
(SPIR-V → MSL), `Present`, and `Reset`. Pass criteria: log ends with
`d3d9-gamebryo-probe: OK`. See [TRACK_A.md](TRACK_A.md) for the full checklist.

**Optional:** run with validation layers:

```bash
export VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation
export DXVK_LOG_LEVEL=debug
```

## 2. Experimental PE `d3d9.dll` cross-build

Required for hosting unmodified Windows `.exe` files via Wine / CrossOver /
Game Porting Toolkit and Whisky-family wrappers (Cosmos). Not part of the
default native build.

**The DLL architecture must match the game process architecture.** Most classic
D3D9 titles — Fallout 3 / New Vegas, Oblivion, Dragon Age: Origins — are
**32-bit**, so they need an `--arch x86` build; a 32-bit process cannot load a
64-bit DLL (including under Wine/CrossOver wow64 on Apple Silicon).

```bash
./scripts/build-pe-d3d9.sh --arch x86     # 32-bit games (Fallout 3, etc.)
./scripts/build-pe-d3d9.sh --arch x64     # 64-bit games (default)
./scripts/build-pe-d3d9.sh --arch both    # both
file build-pe-d3d9-x86/d3d9.dll
```

**Pass criteria:**

- Script exits 0
- `file` reports `PE32 executable (DLL)` for x86, `PE32+ executable (DLL)` for x64
- Output at `build-pe-d3d9-x86/d3d9.dll` (x86) and/or `build-pe-d3d9/d3d9.dll` (x64)
- A `d3d9-pe-smoke.exe` (host-boundary smoke test) is built next to each DLL
  and included in the CI artifact

Use `--wipe` to force a clean reconfigure:

```bash
./scripts/build-pe-d3d9.sh --arch x86 --wipe
```

### 2a. PE host-boundary smoke test (run before any game)

The PE build also produces `d3d9-pe-smoke.exe` — a Win32 console executable
that loads the native `d3d9.dll` override inside a Wine-family host and drives
adapter enumeration, `CreateDevice`, and `Present`, without needing a game
installed. It validates exactly the layer the native CI smoke tests cannot
reach: `WINEDLLOVERRIDES` resolution, the host's `HWND`/winevulkan surface, and
MoltenVK inside the prefix. Run it before the first retail attempt so a failure
means "host boundary broken", not "game issue":

```bash
# Default: wine on PATH, 32-bit build (the benchmark titles' path)
./scripts/run-pe-smoke.sh --build

# Inside a specific bottle / host:
WINE=/path/to/bottles/wine WINEPREFIX=/path/to/bottle ./scripts/run-pe-smoke.sh

# From an unzipped CI artifact:
./scripts/run-pe-smoke.sh --from ~/Downloads
```

**Pass criteria:**

- `d3d9-pe-smoke.exe` exits 0 (`OK (N frame(s) presented)`)
- `scripts/check-boot-logs.sh` reports V1 + V3 (V2 rides along via
  `adapterCount=` output)

The exe prints which `d3d9.dll` module it loaded — if the path is `system32`,
the host's builtin d3d9 won and the override is not applied (fix the bottle's
DLL override before blaming the translator).

### 2b. DXSO shader-corpus pre-flight (before the first retail run)

The `dxso-corpus` tool (built with the native meson build) batch-compiles
real D3D9 shader bytecode through SpockD3D9's DXSO → SPIR-V compiler with no
GPU, host, or game process — catching DXSO gaps before they surface as
first-boot black screens:

```bash
# CI-runnable fixtures (also what the CI native job runs):
<build-dir>/tests/dxso-corpus --selftest

# Real corpus from a legally-owned Fallout: New Vegas / Fallout 3 install
# (~15,535 SM1-3 shaders in Data/Shaders/*.sdp):
<build-dir>/tests/dxso-corpus --sdp "<game>/Data/Shaders"

# Shaders collected from any SpockD3D9 run via DXVK_SHADER_DUMP_PATH:
<build-dir>/tests/dxso-corpus --dump "<dump dir>"
```

**Pass criteria:** exit 0 — every bounded shader compiled. Failures print a
histogram keyed by error signature (scan noise — version tokens without end
tokens inside `.sdp` packages — is counted separately and never fails).

Run this against the target title's shader set before its first hosted boot;
a failure here predicts a `CreatePixelShader`-time black screen and is much
cheaper to debug. See [tools/fallout-new-vegas/README.md](../tools/fallout-new-vegas/README.md)
for the FNV specifics.

## 3. Cosmos / Whisky-family bottle workflow

Cosmos is a Whisky-family Wine wrapper (like Whisky / Sikarugir / CrossOver)
for Apple Silicon: it manages Wine **bottles** and exposes graphics-backend and
DLL-override toggles in a GUI. SpockD3D9 plugs in as a native (non-builtin)
`d3d9.dll` inside a bottle, giving the **D3D9 → Vulkan → MoltenVK → Metal** path
instead of the wrapper's own D3DMetal/DXVK.

1. **Build the matching-bitness DLL** (section 2). Check the target game's
   bitness first — Fallout 3 and friends are 32-bit, so `--arch x86`.

   Then **smoke-test the bottle itself** before installing a game (section 2a):

   ```bash
   WINE=/path/to/bottle/wine WINEPREFIX=/path/to/bottle \
     ./scripts/run-pe-smoke.sh --arch x86
   ```

   This catches the two classic bottle blockers — 32-bit (WoW64) support and
   the wrapper's own D3D backend shadowing the override — without a game in
   the loop.

2. **Locate the game directory inside the bottle.** It lives under the bottle's
   `drive_c`, e.g.
   `~/Library/Containers/<bottle>/.../drive_c/Program Files (x86)/.../Fallout 3/`.
   Cosmos' "Open Bottle in Finder" (or the per-bottle path it shows) gets you there.

3. **Install the override next to the game `.exe`:**

   ```bash
   cp build-pe-d3d9-x86/d3d9.dll "/path/to/bottle/.../Fallout 3/"
   cp tools/fallout3/fallout3.dxvk.conf "/path/to/bottle/.../Fallout 3/dxvk.conf"
   ```

   (`scripts/prepare-fallout3-host.sh --game-dir "<that path>"` does both and
   validates the DLL is 32-bit.)

4. **Force Wine to use our DLL, not its builtin.** Either set the env override
   `WINEDLLOVERRIDES="d3d9=n,b"` for the launch, or add `d3d9` as a **native
   (then builtin)** library in the bottle's Wine configuration GUI. Disable the
   wrapper's own D3DMetal/DXVK backend for D3D9 so it does not shadow our DLL.

5. **Confirm SpockD3D9 actually loaded:** a `d3d9.log` (or the configured
   `DXVK_LOG_PATH`) appears next to the game and the log shows DXVK/SpockD3D9
   banner lines and `D3D9: CreateDeviceEx OK`. If Wine logs its builtin d3d9
   instead, the override did not take (see Troubleshooting).

The dxvk.conf keys that matter most in a bottle are in section 6; the title
profiles under `tools/*/` already set them.

## 4. Fallout 3 boot-to-menu (hosted)

Automated helpers (after PE build):

```bash
./scripts/build-pe-d3d9.sh --arch x86
./scripts/prepare-fallout3-host.sh --game-dir "/path/to/Fallout 3" --build
./scripts/launch-fallout3-host.sh --game-dir "/path/to/Fallout 3"
./scripts/check-boot-logs.sh "/path/to/Fallout 3/fallout3-spockd3d9.log"
```

**Pass criteria for V3 (device):** log contains `D3D9: CreateDeviceEx OK`.
**Pass criteria for V4 (menu):** main menu renders and accepts input — confirm
visually; see [BOOT_TO_MENU.md](BOOT_TO_MENU.md).

Manual steps (same outcome) are in [tools/fallout3/README.md](../tools/fallout3/README.md).
Track milestones V1–V10 in [FALLOUT3_COMPAT.md](FALLOUT3_COMPAT.md).

### Oblivion (Steam, Windows)

Same 32-bit PE workflow as Fallout 3; use the Oblivion helpers:

```bash
./scripts/build-pe-d3d9.sh --arch x86
./scripts/prepare-oblivion-host.sh --game-dir "/path/to/Oblivion" --build
./scripts/launch-oblivion-host.sh --game-dir "/path/to/Oblivion" --steam
./scripts/check-boot-logs.sh "/path/to/Oblivion/d3d9.log"
```

Steam App IDs: **22330** = Oblivion GOTY (default), **4500** = Oblivion (original).
Guide: [tools/oblivion/README.md](../tools/oblivion/README.md).

## 4a. Tested Windows hosts (reference matrix)

Host-side facts gathered from external projects that ran 32-bit D3D9 titles
under Wine on Apple Silicon (d9mt with GTA IV, dx9mt with Fallout: New Vegas
— see [docs/DX9_METAL_ROADMAP.md](DX9_METAL_ROADMAP.md)). None of these are
committed SpockD3D9 targets; they set expectations for hosted runs.

| Host | D3D9 32-bit under Rosetta | Notes |
|------|---------------------------|-------|
| **CrossOver 26** | ✅ (d9mt-tested) | Bundles DXMT/winemetal (only needed by d9mt's Metal path — irrelevant to SpockD3D9's winevulkan route); modern wow64 support |
| **wine-crossover 23.7.1** (CrossOver FOSS sources + marzent msync patch) | ✅ (dx9mt-tested) | Fast Mach-semaphore NT-sync that upstream macOS Wine lacks; launch with `WINEMSYNC=1 WINEESYNC=0`; keep its prefix separate from other Wine builds |
| **Upstream Wine 11** (Homebrew) | ✅ (dx9mt-tested) | Slower pacing than the msync build at matched settings (dx9mt FNV dense exterior: ~77 ms vs ~68 ms/frame); separate prefix |

**Rosetta engine cost:** the 32-bit game engine itself dominates frame time
under Rosetta 2 — dx9mt attributes ~50 ms/frame of FNV's ~68 ms to the engine,
not the translation layer. Expect CPU-bound, playable-not-maxed results on
Gamebryo-era titles, and record host + msync state with every benchmark (§5).

GPTK/Whisky-family wrappers (Cosmos) are covered in §3.

## 5. Capture benchmark results consistently

After each Fallout 3 (or other Windows D3D9 title) host run, record results in
the format from [WINDOWS_D3D9_BENCHMARKS.md](WINDOWS_D3D9_BENCHMARKS.md#reporting-a-benchmark-result):

1. Host/runtime used (Wine/CrossOver/GPTK variant)
2. macOS + GPU/chip + MoltenVK version
3. SpockD3D9 commit and profile path used
4. Highest V milestone reached and first failing subsystem
5. `DXVK_LOG_LEVEL=info` (and `debug` on failures) logs

## 6. Track A / MoltenVK tuning

SpockD3D9's default macOS path is **D3D9 → Vulkan → MoltenVK → Metal**. Operational
guide: [TRACK_A.md](TRACK_A.md).

**Recommended `dxvk.conf` keys for every macOS run:**

```ini
dxvk.enableShaderCache = True
dxvk.tilerMode = Auto
```

Title profiles under `tools/*/` already set these. The shader cache reduces repeat
**SPIR-V → MSL** compilation stutter inside MoltenVK; the first launch of a heavy
title may still hitch while pipelines compile.

**Shader cache diagnostics:**

```bash
export DXVK_LOG_LEVEL=info    # summarize pipeline / shader work
export DXVK_LOG_LEVEL=debug   # per-shader detail when a compile fails
```

Re-run the same scene twice and compare: the second run should show fewer new
pipeline creations in the log.

## 7. Environment reference

| Variable | Purpose |
|----------|---------|
| `DXVK_WSI_DRIVER` | `SDL3` (default in samples), `SDL2`, or `GLFW` |
| `DXVK_LOG_LEVEL` | `info` or `debug` for diagnostics |
| `DXVK_CONFIG_FILE` | Path to title profile (e.g. `fallout3.dxvk.conf`) |
| `DYLD_LIBRARY_PATH` | Locate freshly built `libdxvk_d3d9.dylib` |
| `VK_ICD_FILENAMES` / `VK_DRIVER_FILES` | Override MoltenVK ICD (usually auto-detected) |
| `WINEDLLOVERRIDES` | `d3d9=n,b` to load SpockD3D9 PE DLL in Wine hosts |
| `MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS` | Set `2` for MoltenVK argument-buffer descriptors on current macOS runners |
| `MVK_CONFIG_DEBUG` | Extra MoltenVK logging |
| `MTL_DEBUG_LAYER` | Metal API validation (heavy; isolate Metal-side failures) |

## 8. CI parity

GitHub Actions runs the native build + `d3d9-clear` smoke test on `macos-14`
(arm64) and `macos-13` (x86_64), plus a PE cross-compile job that builds **both**
the 32-bit (x86) and 64-bit (x64) `d3d9.dll` via a matrix and verifies each one's
bitness. The 32-bit slice is the artifact needed for Fallout 3-class titles.
Universal fat binaries (`--arch universal`) — of the *native* dylib — are not
built in CI; use the per-arch matrix artifacts or build slices locally. See
`.github/workflows/build-macos.yml`.

## 9. Troubleshooting

| Symptom | Likely cause |
|---------|----------------|
| `DXVK: No adapters found` | MoltenVK not installed or ICD not discoverable |
| `d3d9-clear` missing | SDL3/SDL2 not found at configure time |
| PE build: `x86_64-w64-mingw32-g++ not found` | `brew install mingw-w64` |
| Wine host still uses built-in D3D9 | `WINEDLLOVERRIDES` not set, DLL not next to the `.exe`, or wrapper's own D3DMetal/DXVK backend still handling D3D9 |
| Game ignores DLL / "not a valid Win32 application" | Bitness mismatch — 32-bit game needs `--arch x86`, 64-bit game needs `--arch x64` |
| Black screen / device lost on focus | Check window lifecycle + present throttling fixes in swapchain |
