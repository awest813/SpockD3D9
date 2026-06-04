# SpockD3D9

A macOS-native Direct3D 9 translation layer — translating D3D9 API calls to Vulkan (via [MoltenVK](https://github.com/KhronosGroup/MoltenVK)) on Apple Silicon and Intel Macs.

This is a focused fork of [DXVK](https://github.com/doitsujin/dxvk), stripped down to only the D3D9 translation layer and targeting macOS as a first-class platform. It uses [DXVK Native](https://github.com/doitsujin/dxvk#dxvk-native) mode with SDL3/SDL2/GLFW for window management.

## Project Direction

SpockD3D9's north-star goal is **full compatibility with Windows D3D9 games on macOS** — running real retail titles like Fallout 3 (Steam) on Apple hardware, with the D3D9 rendering path translated natively to Metal via Vulkan/MoltenVK. Everything in this repository is a step toward that goal.

The work happens in two layers:

1. **Native D3D9 → Vulkan translation library** (the foundation) — `libdxvk_d3d9.dylib` implements the D3D9 API and translates it to Vulkan (then Metal), using SDL3/SDL2/GLFW for windowing instead of Win32. This layer works today for native ports and custom renderers, and is the engine every hosted Windows game will ultimately render through.
2. **Windows D3D9 game hosting** (the remaining gap) — a wrapper or translation layer that hosts Windows game logic and routes its `d3d9.dll` calls into SpockD3D9. This layer is under development and is what closes the gap to running unmodified Windows games; it depends on the Win32 compatibility shims tracked in [ROADMAP.md](ROADMAP.md) (Milestone E).

The first retail compatibility target — the beachhead that proves the full path end to end — is **Fallout 3 (Steam, Windows, D3D9 / Gamebryo engine)**. The broader benchmark set is **Fallout 3**, **Fallout: New Vegas**, **Dragon Age: Origins**, and **Galactic Civilizations II**; see [COMPATIBILITY.md](COMPATIBILITY.md) for the per-title tracker, [docs/FALLOUT3_COMPAT.md](docs/FALLOUT3_COMPAT.md) for the detailed Fallout 3 checklist, and [docs/WINDOWS_D3D9_BENCHMARKS.md](docs/WINDOWS_D3D9_BENCHMARKS.md) for the shared benchmark milestones.

## Supported Platforms

| Architecture | Status |
|---|---|
| Apple Silicon (arm64) | ✅ Primary target |
| Intel Mac (x86_64) | ✅ Supported |

## How It Works

SpockD3D9 provides a native shared library (`libdxvk_d3d9.dylib`) that implements the Direct3D 9 API. Under the hood it translates all D3D9 calls to Vulkan, which MoltenVK then translates to Metal. This enables running D3D9 renderers natively on macOS without Wine.

### Window System Integration (WSI)

SpockD3D9 requires one of these windowing backends:

- **SDL3** (recommended) — most complete fullscreen and multi-monitor path on macOS
- **SDL2** — widely available fallback
- **GLFW** — lightweight alternative

Set the backend via the `DXVK_WSI_DRIVER` environment variable:
```bash
export DXVK_WSI_DRIVER=SDL3    # or SDL2, GLFW
```

## Prerequisites

- macOS 13 (Ventura) or later
- [MoltenVK](https://github.com/KhronosGroup/MoltenVK) (Vulkan on Metal)
- [Meson](https://mesonbuild.com/) build system (≥ 0.58)
- A C++17 compiler (Apple Clang from Xcode)
- [glslang](https://github.com/KhronosGroup/glslang) shader compiler
- One of: SDL3 (recommended), SDL2, or GLFW

Install all dependencies with Homebrew:
```bash
brew install meson ninja glslang sdl3 sdl2 molten-vk vulkan-loader vulkan-headers spirv-headers
```

`vulkan-loader` (the Khronos `libvulkan.dylib`) is optional but recommended:
with it installed, SpockD3D9 runs through the Vulkan loader — which is required
for validation layers and lets the loader expose MoltenVK as a portability
adapter (SpockD3D9 enables portability enumeration automatically). Without it,
SpockD3D9 falls back to loading `libMoltenVK.dylib` directly, which renders fine
but cannot load instance layers such as `VK_LAYER_KHRONOS_validation`.

## Building

### Quick Build

```bash
git clone --recursive https://github.com/awest813/SpockD3D9.git
cd SpockD3D9

# Build for the current architecture
./package-native.sh dev ./build --no-package --dev-build
```

### Manual Build

```bash
# Native build (auto-detects arm64 or x86_64)
meson setup --buildtype release --prefix /your/install/dir build
cd build
ninja install
```

The D3D9 shared library will be at `/your/install/dir/lib/libdxvk_d3d9.dylib`.

### Experimental Windows PE `d3d9.dll` (optional)

For hosting unmodified Windows D3D9 games through an external Wine / CrossOver /
Game Porting Toolkit layer, SpockD3D9 can cross-compile an experimental
`d3d9.dll` (not part of the blessed macOS native build):

```bash
brew install mingw-w64   # if not already installed
./scripts/build-pe-d3d9.sh
# Output: build-pe-d3d9/d3d9.dll
```

Use with a Windows host via `WINEDLLOVERRIDES="d3d9=n,b"`. See
[docs/FALLOUT3_EXECUTION_MODEL.md](docs/FALLOUT3_EXECUTION_MODEL.md) for the
Fallout 3 hosting path, [docs/MACOS_TESTING.md](docs/MACOS_TESTING.md) for the
full macOS validation checklist, and [ROADMAP.md](ROADMAP.md) Milestone F for
validation status.

### Smoke test (`d3d9-clear`)

After building, a minimal SDL3 sample is installed next to the library (`d3d9-clear-sdl2` is also built when both SDL3 and SDL2 are available). It creates a D3D9 device, clears the back buffer, and presents a few frames:

```bash
export DYLD_LIBRARY_PATH="/your/install/dir/lib"
export DXVK_WSI_DRIVER=SDL3
/your/install/dir/lib/d3d9-clear 60   # optional frame count (default: 60)
```

> **MoltenVK discovery:** SpockD3D9 automatically searches the common Homebrew
> prefixes (`/opt/homebrew` on Apple Silicon, `/usr/local` on Intel, or
> `$HOMEBREW_PREFIX`) for `libvulkan.dylib` / `libMoltenVK.dylib` and the
> MoltenVK driver manifest, so a Homebrew-installed MoltenVK works without
> setting `DYLD_LIBRARY_PATH`, `VK_DRIVER_FILES`, or legacy
> `VK_ICD_FILENAMES`. Set those variables to override the auto-detected
> MoltenVK (e.g. a custom build). `DYLD_LIBRARY_PATH` above is only needed to
> locate your freshly built `libdxvk_d3d9.dylib`.

On success it prints `d3d9-clear: OK` and exits with code 0.

For a one-command local validation pass (build + smoke test), use
[`scripts/test-macos-native.sh`](scripts/test-macos-native.sh). The full macOS
testing checklist — native build, PE cross-compile, and Fallout 3 hosting — is
in [docs/MACOS_TESTING.md](docs/MACOS_TESTING.md).

### Cross-Architecture Build

CI builds each architecture on a native runner (`arm64` on Apple Silicon, `x86_64`
on Intel). For local per-arch builds on a universal Mac:

```bash
# Build for Apple Silicon
CFLAGS="-arch arm64" CXXFLAGS="-arch arm64" OBJCFLAGS="-arch arm64" OBJCXXFLAGS="-arch arm64" LDFLAGS="-arch arm64" \
  meson setup --buildtype release build.arm64
ninja -C build.arm64

# Build for Intel
CFLAGS="-arch x86_64" CXXFLAGS="-arch x86_64" OBJCFLAGS="-arch x86_64" OBJCXXFLAGS="-arch x86_64" LDFLAGS="-arch x86_64" \
  meson setup --buildtype release build.x86_64
ninja -C build.x86_64
```

An optional fat binary via `lipo` is available manually
(`./package-native.sh … --arch universal`) but is not part of CI — cross-slicing
Objective-C++ WSI sources on Apple Silicon remains fragile.

## Usage in Your Application

Link your application against `libdxvk_d3d9.dylib` and use the D3D9 API as normal. Your application must use one of the supported WSI backends (SDL3, SDL2, or GLFW) for window creation.

```cpp
// Instead of HWND, use SDL_Window* (or equivalent)
// Set DXVK_WSI_DRIVER=SDL3 before running
```

See the [DXVK Native documentation](https://github.com/doitsujin/dxvk#dxvk-native) for details on adapting a D3D9 renderer for native use:
- `HWND` becomes `SDL_Window*` (or GLFW equivalent)
- `__uuidof(type)` is supported, but use `__uuidof_var(variable)` for variables

## Configuration

SpockD3D9 reads configuration from `dxvk.conf`. Key settings:

```ini
# Force a specific GPU
# dxvk.filterDeviceName = "Apple M1"

# Shader cache (recommended — MoltenVK compiles SPIR-V to MSL at runtime)
# dxvk.enableShaderCache = True

# Tiler GPU heuristics (Auto enables on MoltenVK / Apple GPUs)
# dxvk.tilerMode = Auto
```

See `dxvk.conf` for full option documentation, including macOS-specific notes on `dxvk.tilerMode`.

## Debugging

| Variable | Description |
|---|---|
| `DXVK_LOG_LEVEL=none\|error\|warn\|info\|debug` | Control log verbosity |
| `DXVK_LOG_PATH=/some/directory` | Log file output directory |
| `DXVK_HUD=fps,devinfo` | Show FPS and GPU info overlay |
| `DXVK_HUD=full` | Show all HUD elements |
| `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation` | Enable Vulkan validation (requires the Vulkan loader, i.e. `brew install vulkan-loader`) |
| `MTL_DEBUG_LAYER=1` | Enable Metal debug layer (MoltenVK) |

## Architecture

```
┌─────────────┐
│  Your App   │  (D3D9 API calls)
├─────────────┤
│  SpockD3D9  │  (D3D9 → Vulkan translation)
├─────────────┤
│  MoltenVK   │  (Vulkan → Metal translation)
├─────────────┤
│    Metal    │  (Apple GPU driver)
└─────────────┘
```

## Relationship to Other Projects

- **[DXVK](https://github.com/doitsujin/dxvk)**: The upstream project. SpockD3D9 is a focused fork retaining only D3D9 support and targeting macOS natively.
- **[dxmt](https://github.com/3Shain/dxmt)**: A separate project that translates D3D11/D3D10 directly to Metal (no Vulkan intermediate). SpockD3D9 takes architectural inspiration from dxmt's macOS build patterns but uses a different approach (Vulkan via MoltenVK) and targets a different API (D3D9).
- **[MoltenVK](https://github.com/KhronosGroup/MoltenVK)**: The Vulkan-to-Metal translation layer that SpockD3D9 depends on (Track A — see [docs/TRACK_A.md](docs/TRACK_A.md)).

## Known Limitations

- **MoltenVK constraints**: Some Vulkan features are synthesized or unavailable on Metal. SpockD3D9 queries MoltenVK at runtime for format and MSAA support; see [docs/MOLTENVK_CAPABILITIES.md](docs/MOLTENVK_CAPABILITIES.md) for BCn (DXT), depth/stencil, and MSAA behavior on macOS.
- **Tiler GPU behavior**: Apple GPUs are tile-based renderers. SpockD3D9 enables tiler-aware submission when MoltenVK is detected (`dxvk.tilerMode = Auto`). Apps that rely on many mid-pass clears or non-render-pass patterns may perform differently than on desktop GPUs; see `dxvk.conf` for tuning notes.
- **Compatibility matrix**: Community-tested titles and suggested profiles are tracked in [COMPATIBILITY.md](COMPATIBILITY.md).
- **Win32 compatibility shims**: `src/util/util_win32_compat.h` emulates Win32 handle objects natively — semaphores, auto/manual-reset events (`SetEvent`/`ResetEvent`), `WaitForSingleObject`, `WaitForMultipleObjects`, and reference-counted `DuplicateHandle`/`CloseHandle` (unit-tested in `tests/util/test_win32_compat.cpp`). GDI device-context APIs remain safe no-op stubs. See [ROADMAP.md](ROADMAP.md) (Milestone E) for the full status.
- **Windows game hosting**: Running unmodified Windows `.exe` games requires a wrapper or translation layer (e.g., Wine or a custom loader) that routes D3D9 calls to SpockD3D9. This integration path is under development.

## License

SpockD3D9 is licensed under the [zlib/libpng license](LICENSE).
