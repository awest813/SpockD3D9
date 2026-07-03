# The Elder Scrolls IV: Oblivion on macOS via SpockD3D9

This directory holds the host-side assets for **The Elder Scrolls IV: Oblivion**
(Steam, Windows). Oblivion shares Bethesda's Gamebryo renderer with Fallout 3 and
uses the same hosted Windows D3D9 path: an external Windows host loads SpockD3D9's
experimental PE `d3d9.dll` as a `d3d9` override.

## What's here

| File | Purpose |
|------|---------|
| `oblivion.dxvk.conf` | Title-specific SpockD3D9 configuration profile |
| `README.md` | This guide |

## Architecture recap

```
Oblivion.exe → Wine / CrossOver / GPTK (host) → SpockD3D9 d3d9.dll
            → winevulkan → MoltenVK → Metal
```

The **host** loads the PE and provides DirectInput, DirectSound, filesystem,
registry, threading, and the window. **SpockD3D9** provides only the D3D9 → Vulkan
translation.

## Prerequisites

* macOS 13+ on Apple Silicon or Intel, with MoltenVK installed
  (`brew install molten-vk`).
* An external Windows host with a working Vulkan + MoltenVK path (CrossOver,
  Game Porting Toolkit, or Wine with `winevulkan`).
* A SpockD3D9 **PE `d3d9.dll`** for the host to load. Oblivion is **32-bit**,
  so build the 32-bit DLL with
  [`scripts/build-pe-d3d9.sh --arch x86`](../../scripts/build-pe-d3d9.sh)
  (output: `build-pe-d3d9-x86/d3d9.dll`).

> **Note — the PE build is experimental and opt-in.** Cross-compile via
> `./scripts/build-pe-d3d9.sh --arch x86` (requires MinGW-w64). Boot-to-menu
> validation is still pending; treat the steps below as the intended workflow.

## Boot to menu (quick start)

```bash
./scripts/build-pe-d3d9.sh --arch x86
./scripts/prepare-oblivion-host.sh --game-dir "/path/to/Oblivion" --build
./scripts/launch-oblivion-host.sh --game-dir "/path/to/Oblivion" --steam
./scripts/check-boot-logs.sh "/path/to/Oblivion/d3d9.log"
```

**Steam App IDs:** **22330** = Oblivion Game of the Year Edition (default);
**4500** = Oblivion (original, if still owned).

Because Steam launches the game detached, its stdout is not captured;
SpockD3D9 writes `d3d9.log` into the game directory via `DXVK_LOG_PATH`
(set automatically by `prepare-oblivion-host.sh`).

For clean first-boot D3D9 diagnostics (without Steam's overlay hook):

```bash
./scripts/launch-oblivion-host.sh --game-dir "$GAME_DIR" --steam --no-overlay
```

## Manual setup

1. Copy `build-pe-d3d9-x86/d3d9.dll` next to `Oblivion.exe` in your prefix.
2. Copy `oblivion.dxvk.conf` as `dxvk.conf` beside the executable.
3. Set `WINEDLLOVERRIDES="d3d9=n,b"` and launch via Steam or directly.

Or use the helpers:

```bash
./scripts/prepare-oblivion-host.sh --game-dir "$GAME_DIR"
./scripts/launch-oblivion-host.sh --game-dir "$GAME_DIR" --steam
```

Full checklist and troubleshooting: [docs/BOOT_TO_MENU.md](../../docs/BOOT_TO_MENU.md).

## Validating progress

Track results against the V1–V10 milestones in
[docs/WINDOWS_D3D9_BENCHMARKS.md](../../docs/WINDOWS_D3D9_BENCHMARKS.md).
The earliest signal that the override is wired up correctly is a SpockD3D9
banner line in the log and `Direct3DCreate9` succeeding (V1). After device
creation, look for `D3D9: CreateDeviceEx OK` (V3).

## Profile keys

`oblivion.dxvk.conf` uses only documented SpockD3D9 options. CI enforces this via
`tests/conf/test_dxvk_conf_profiles.py`. A matching built-in profile for
`Oblivion.exe` also exists in `src/util/config/config.cpp`, so copying the file
is optional for the core compat keys — use it to layer opt-in tuning such as
`presentInterval`.
