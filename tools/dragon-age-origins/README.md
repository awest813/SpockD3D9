# Dragon Age: Origins on macOS via SpockD3D9

This directory holds the host-side compatibility assets for Dragon Age:
Origins, one of the Windows D3D9 benchmark titles for SpockD3D9.

## What's here

| File | Purpose |
|------|---------|
| `dragon-age-origins.dxvk.conf` | Title-specific SpockD3D9 configuration profile |
| `README.md` | This guide |

## Execution model

```text
daorigins.exe -> external Windows host -> SpockD3D9 d3d9.dll
              -> winevulkan/Vulkan loader -> MoltenVK -> Metal
```

The host provides Windows process loading, Win32 windowing, input, audio,
filesystem, registry, and threading. SpockD3D9 provides only the D3D9 to Vulkan
translation. The optional PE `d3d9.dll` build remains the prerequisite for
loading SpockD3D9 from an unmodified Windows game.

## Setup (intended workflow)

1. Copy the experimental SpockD3D9 `d3d9.dll` next to `daorigins.exe`, or into
   the host prefix's `system32`.
2. Configure the host to prefer that DLL:

   ```bash
   export WINEDLLOVERRIDES="d3d9=n,b"
   ```

3. Copy `dragon-age-origins.dxvk.conf` next to `daorigins.exe` as `dxvk.conf`,
   or point to it explicitly:

   ```bash
   export DXVK_CONFIG_FILE="$PWD/dragon-age-origins.dxvk.conf"
   ```

4. Launch the game and collect `DXVK_LOG_LEVEL=info` logs for the benchmark
   tracker in `docs/WINDOWS_D3D9_BENCHMARKS.md`.
