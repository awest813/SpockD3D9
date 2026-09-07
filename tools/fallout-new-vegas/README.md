# Fallout: New Vegas on macOS via SpockD3D9

This directory holds the host-side compatibility assets for Fallout: New Vegas,
one of the Windows D3D9 benchmark titles for SpockD3D9.

## What's here

| File | Purpose |
|------|---------|
| `fallout-new-vegas.dxvk.conf` | Title-specific SpockD3D9 configuration profile |
| `README.md` | This guide |

## Execution model

```text
FalloutNV.exe -> external Windows host -> SpockD3D9 d3d9.dll
              -> winevulkan/Vulkan loader -> MoltenVK -> Metal
```

The host provides Windows process loading, Win32 windowing, input, audio,
filesystem, registry, and threading. SpockD3D9 provides only the D3D9 to Vulkan
translation. The optional PE `d3d9.dll` build remains the prerequisite for
loading SpockD3D9 from an unmodified Windows game.

## Setup (intended workflow)

1. Copy the experimental SpockD3D9 `d3d9.dll` next to `FalloutNV.exe`, or into
   the host prefix's `system32`.
2. Configure the host to prefer that DLL:

   ```bash
   export WINEDLLOVERRIDES="d3d9=n,b"
   ```

3. Copy `fallout-new-vegas.dxvk.conf` next to `FalloutNV.exe` as `dxvk.conf`,
   or point to it explicitly:

   ```bash
   export DXVK_CONFIG_FILE="$PWD/fallout-new-vegas.dxvk.conf"
   ```

4. Launch the game and collect `DXVK_LOG_LEVEL=info` logs for the benchmark
   tracker in `docs/WINDOWS_D3D9_BENCHMARKS.md`.

## FNV-specific hosting knowledge

Curated from an external D3D9 translation project that ran Fallout: New Vegas
end-to-end under Wine on Apple Silicon (theodorechapman/dx9mt — see
[docs/DX9_METAL_ROADMAP.md](../../docs/DX9_METAL_ROADMAP.md)). These are
*host and game* facts, independent of SpockD3D9's translation:

### Known non-graphics blocker: BSShader factory crash

FNV's shader factory can NULL-deref under Wine when `shader_tbl[29]` is empty
and TLS slot 0 is uninitialized on the IO loading thread (crash near
`FalloutNV.exe+0xB57AA9`). dx9mt ships a vectored-exception-handler patch for
it inside its own DLL and reports it as still required. **If SpockD3D9's FNV
run crashes before D3D9 calls appear in the log, suspect this first** — it is
a game/Wine bug, not a translation bug.

### NVSE launch pattern

Script-extender mods (and most modern FNV setups) launch through
`nvse_loader.exe` rather than `FalloutNV.exe`:

```bash
WINEDLLOVERRIDES="d3d9=n,b;dxsetup.exe=d" \
  wine nvse_loader.exe
```

`dxsetup.exe=d` blocks the bundled DirectX installer shim from running; env
overrides outrank per-app registry `AppDefaults` (the alternative place to
set `d3d9 = native,builtin` for `FalloutNV.exe` / `FalloutNVLauncher.exe`).

### Gamebryo branches on the GPU vendor ID

FNV selects shader paths and formats by adapter identity (dx9mt impersonates
an NVIDIA GTX 280 — VendorId `0x10DE`, DeviceId `0x0611` — and reports that
changing the caps changes how FNV configures shaders). If the honest
Apple-GPU (MoltenVK) identity misroutes the renderer, SpockD3D9 has the same
lever:

```ini
# dxvk.conf — try only if the default identity misbehaves
d3d9.customVendorId = 10DE
d3d9.customDeviceId = 0611
```

### What FNV actually draws (per-frame API profile)

A full per-call trace of one dense FNV frame (published by dx9mt):
**1,411 `DrawIndexedPrimitive`** (no `DrawPrimitive`/UP), ~3,950 VS-constant
and ~2,570 PS-constant sets, 2,184 `SetSamplerState`, 1,869 `SetTexture`,
1,092 `SetRenderState`, ~90 shader binds, 71 declarations, 28 viewports,
14 RT switches, 6 `StretchRect` composites, 15 `Begin/EndScene` pairs,
2 clip-plane sets, 1 `Present`. The native `d3d9-gamebryo-probe` frame-shape
section (`probeFrameShape`) reproduces these ratios at 1:50 scale in CI.

### Shader corpus pre-flight

FNV ships ~15,535 SM1–3 shaders in `Data/Shaders/*.sdp`. Before the first
hosted run, validate that SpockD3D9's DXSO compiler accepts all of them:

```bash
./build-native-test/tests/dxso-corpus --sdp "<FNV install>/Data/Shaders"
# or any meson build dir containing the dxso-corpus target
```

See [docs/MACOS_TESTING.md §2b](../../docs/MACOS_TESTING.md).
