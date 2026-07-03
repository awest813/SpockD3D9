# Fallout 3 on macOS via SpockD3D9

This directory holds the host-side assets for SpockD3D9's first retail target,
Fallout 3 (Steam, Windows). The execution model — **native-first translator plus
an optional, opt-in PE `d3d9.dll` that an external Windows host loads as a `d3d9`
override** — is decided in
[docs/FALLOUT3_EXECUTION_MODEL.md](../../docs/FALLOUT3_EXECUTION_MODEL.md). No
single host is officially targeted; the Wine-family hosts used below are
**examples** of downstream consumers, not blessed platforms. The per-subsystem
checklist lives in [docs/FALLOUT3_COMPAT.md](../../docs/FALLOUT3_COMPAT.md).

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

The **host** (any external Windows host) loads the PE and provides DirectInput,
DirectSound, filesystem, registry, threading, and the window. **SpockD3D9**
provides only the D3D9 → Vulkan translation. The example below uses a Wine-family
host because that is the readily available option on macOS today — but the
project does not commit to it; see the decision record for why.

## Prerequisites

* macOS 13+ on Apple Silicon or Intel, with MoltenVK installed
  (`brew install molten-vk`).
* An external Windows host with a working Vulkan + MoltenVK path. Examples
  (downstream consumers, not officially targeted platforms):
  * Apple [Game Porting Toolkit](https://developer.apple.com/games/game-porting-toolkit/)
  * [CrossOver](https://www.codeweavers.com/crossover)
  * vanilla [Wine](https://www.winehq.org/) built with `winevulkan`
* A SpockD3D9 **PE `d3d9.dll`** for the host to load. Fallout 3 is **32-bit**,
  so build the 32-bit DLL with
  [`scripts/build-pe-d3d9.sh --arch x86`](../../scripts/build-pe-d3d9.sh)
  (output: `build-pe-d3d9-x86/d3d9.dll`).

> **Note — the PE build is experimental and opt-in.** SpockD3D9's default build
> produces the native `libdxvk_d3d9.dylib`, which an external host *cannot*
> load. Cross-compile the Windows-PE `d3d9.dll` via
> `./scripts/build-pe-d3d9.sh --arch x86` (requires MinGW-w64). It is not part of the
> default or "blessed" build. Boot-to-menu validation is still pending; treat
> the steps below as the intended workflow rather than a turnkey one.

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

5. **Launch** Fallout 3 under the host. For first-boot diagnostics, raise the
   SpockD3D9 log level:

   ```bash
   export DXVK_LOG_LEVEL=info   # use 'debug' for deep traces
   ```

   **Direct vs Steam launch.** The retail target is the *Steam* build, which
   carries Steam DRM and loads Steam's own D3D9 overlay
   (`gameoverlayrenderer.dll`). Running `Fallout3.exe` directly often trips the
   DRM or silently relaunches through Steam, so use the Steam path for the real
   game and reserve direct-exe launch for DRM-free copies:

   ```bash
   # Retail Steam build (Steam must be installed in the prefix and logged in):
   ./scripts/launch-fallout3-host.sh --game-dir "/path/to/Fallout 3" --steam

   # Fallout 3 (original) is appid 22300; GOTY (default) is 22370:
   ./scripts/launch-fallout3-host.sh --game-dir "$GAME_DIR" --steam --appid 22300

   # Clean D3D9 diagnostics with the Steam overlay's own hook disabled:
   ./scripts/launch-fallout3-host.sh --game-dir "$GAME_DIR" --steam --no-overlay
   ```

   Because Steam launches the game detached, its stdout is not captured;
   SpockD3D9 writes `d3d9.log` into the game directory via `DXVK_LOG_PATH`
   (set automatically by `prepare-fallout3-host.sh`). Check it with
   `./scripts/check-boot-logs.sh "<game dir>/d3d9.log"`.

## Boot to menu (V3)

End-to-end workflow for reaching the main menu:

```bash
./scripts/build-pe-d3d9.sh --arch x86
./scripts/prepare-fallout3-host.sh --game-dir "/path/to/Fallout 3" --build
./scripts/launch-fallout3-host.sh --game-dir "/path/to/Fallout 3"
./scripts/check-boot-logs.sh "/path/to/Fallout 3/fallout3-spockd3d9.log"
```

Full checklist, troubleshooting, and V4 criteria:
[docs/BOOT_TO_MENU.md](../../docs/BOOT_TO_MENU.md).

## Validating progress

Track results against the V1–V10 validation milestones in
[docs/FALLOUT3_COMPAT.md](../../docs/FALLOUT3_COMPAT.md#validation-milestones).
The earliest signal that the override is wired up correctly is a SpockD3D9
banner line in the host's console output and `Direct3DCreate9` succeeding (V1).
After device creation, look for `D3D9: CreateDeviceEx OK` in the log (V3).

## Profile keys

`fallout3.dxvk.conf` uses only documented SpockD3D9 options. CI enforces this:
`tests/conf/test_dxvk_conf_profiles.py` checks every key in this profile against
the option set documented in the repository-root `dxvk.conf`, so the profile
cannot drift onto a misspelled or removed key.
