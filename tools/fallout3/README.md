# Fallout 3 on macOS via SpockD3D9

This directory holds the host-side assets for SpockD3D9's first retail target,
Fallout 3 (Steam, Windows). The execution model — **a Wine-family host with
SpockD3D9 as a `d3d9.dll` override** — is decided in
[docs/FALLOUT3_EXECUTION_MODEL.md](../../docs/FALLOUT3_EXECUTION_MODEL.md). The
per-subsystem checklist lives in
[docs/FALLOUT3_COMPAT.md](../../docs/FALLOUT3_COMPAT.md).

## What's here

| File | Purpose |
|------|---------|
| `fallout3.dxvk.conf` | Title-specific SpockD3D9 configuration profile |
| `README.md` | This guide |

## Architecture recap

```
Fallout3.exe → Wine / CrossOver / GPTK (host) → SpockD3D9 d3d9.dll
            → winevulkan → MoltenVK → Metal
```

The **host** (Wine family) loads the PE and provides DirectInput, DirectSound,
filesystem, registry, threading, and the window. **SpockD3D9** provides only the
D3D9 → Vulkan translation. See the decision record for why.

## Prerequisites

* macOS 13+ on Apple Silicon or Intel, with MoltenVK installed
  (`brew install molten-vk`).
* A Wine-family host with a working Vulkan + MoltenVK path. Any of:
  * Apple [Game Porting Toolkit](https://developer.apple.com/games/game-porting-toolkit/)
  * [CrossOver](https://www.codeweavers.com/crossover)
  * vanilla [Wine](https://www.winehq.org/) built with `winevulkan`
* A SpockD3D9 **PE `d3d9.dll`** for the host to load.

> **Note — PE build is the current prerequisite task.** SpockD3D9's default
> build produces the native `libdxvk_d3d9.dylib`, which a Wine host *cannot*
> load. A Windows-PE `d3d9.dll` (MinGW cross-compile) is required and is the
> next active item in [Milestone F](../../ROADMAP.md#milestone-f--fallout-3-compatibility).
> Until it lands, treat the steps below as the intended workflow rather than a
> turnkey one.

## Setup (intended workflow)

1. **Install the override DLL.** Copy the SpockD3D9 `d3d9.dll` into the Fallout 3
   install directory inside your Wine prefix (next to `Fallout3.exe`), or into
   the prefix's `system32`.

2. **Register the override** so the host loads SpockD3D9 instead of its built-in
   D3D9:

   ```bash
   export WINEDLLOVERRIDES="d3d9=n,b"
   ```

   (`n,b` = prefer the native/non-Wine DLL, then fall back to built-in.)

3. **Drop the profile.** Copy `fallout3.dxvk.conf` next to `Fallout3.exe` and
   rename it to `dxvk.conf`, or point the loader at it explicitly:

   ```bash
   export DXVK_CONFIG_FILE="$PWD/fallout3.dxvk.conf"
   ```

4. **Make MoltenVK reachable** from inside the prefix if your host does not wire
   it up automatically (GPTK does this for you):

   ```bash
   export DYLD_LIBRARY_PATH="$(brew --prefix molten-vk)/lib:$DYLD_LIBRARY_PATH"
   ```

5. **Launch** Fallout 3 through Steam (or `Fallout3.exe` directly) under the
   host. For first-boot diagnostics, raise the SpockD3D9 log level:

   ```bash
   export DXVK_LOG_LEVEL=info   # use 'debug' for deep traces
   ```

## Validating progress

Track results against the V1–V10 validation milestones in
[docs/FALLOUT3_COMPAT.md](../../docs/FALLOUT3_COMPAT.md#validation-milestones).
The earliest signal that the override is wired up correctly is a SpockD3D9
banner line in the host's console output and `Direct3DCreate9` succeeding (V1).

## Profile keys

`fallout3.dxvk.conf` uses only documented SpockD3D9 options. CI enforces this:
`tests/conf/test_dxvk_conf_profiles.py` checks every key in this profile against
the option set documented in the repository-root `dxvk.conf`, so the profile
cannot drift onto a misspelled or removed key.
