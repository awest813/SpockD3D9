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

**Optional:** run with validation layers:

```bash
export VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation
export DXVK_LOG_LEVEL=debug
```

## 2. Experimental PE `d3d9.dll` cross-build

Required for hosting unmodified Windows `.exe` files via Wine / CrossOver /
Game Porting Toolkit. Not part of the default native build.

```bash
./scripts/build-pe-d3d9.sh
file build-pe-d3d9/d3d9.dll
```

**Pass criteria:**

- Script exits 0
- `file` reports `PE32+ executable (DLL)` (or `PE32 executable (DLL)`)
- Output at `build-pe-d3d9/d3d9.dll`

Use `--wipe` to force a clean reconfigure:

```bash
./scripts/build-pe-d3d9.sh --wipe
```

## 3. Fallout 3 hosting workflow (manual)

Once the PE DLL builds, follow
[tools/fallout3/README.md](../tools/fallout3/README.md):

1. Copy `build-pe-d3d9/d3d9.dll` into the Wine prefix (next to `Fallout3.exe`
   or into `system32`).
2. Set `WINEDLLOVERRIDES="d3d9=n,b"`.
3. Copy `tools/fallout3/fallout3.dxvk.conf` → `dxvk.conf` beside the game.
4. Ensure MoltenVK is reachable from inside the prefix.
5. Launch `Fallout3.exe` under your chosen host.

Track progress against [FALLOUT3_COMPAT.md](FALLOUT3_COMPAT.md) milestones
(V1–V10).

## 4. Environment reference

| Variable | Purpose |
|----------|---------|
| `DXVK_WSI_DRIVER` | `SDL3` (default in samples), `SDL2`, or `GLFW` |
| `DXVK_LOG_LEVEL` | `info` or `debug` for diagnostics |
| `DXVK_CONFIG_FILE` | Path to title profile (e.g. `fallout3.dxvk.conf`) |
| `DYLD_LIBRARY_PATH` | Locate freshly built `libdxvk_d3d9.dylib` |
| `VK_ICD_FILENAMES` / `VK_DRIVER_FILES` | Override MoltenVK ICD (usually auto-detected) |
| `WINEDLLOVERRIDES` | `d3d9=n,b` to load SpockD3D9 PE DLL in Wine hosts |
| `MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS` | Set `0` if hitting MoltenVK argument-buffer issues |

## 5. CI parity

GitHub Actions runs the same native build + `d3d9-clear` smoke test on
`macos-14` (arm64) and `macos-13` (x86_64), plus an optional PE cross-compile
job when `mingw-w64` is available. See `.github/workflows/build-macos.yml`.

## Troubleshooting

| Symptom | Likely cause |
|---------|----------------|
| `DXVK: No adapters found` | MoltenVK not installed or ICD not discoverable |
| `d3d9-clear` missing | SDL3/SDL2 not found at configure time |
| PE build: `x86_64-w64-mingw32-g++ not found` | `brew install mingw-w64` |
| Wine host still uses built-in D3D9 | `WINEDLLOVERRIDES` not set or DLL not on search path |
| Black screen / device lost on focus | Check window lifecycle + present throttling fixes in swapchain |
