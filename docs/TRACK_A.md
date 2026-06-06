# Track A — MoltenVK (D3D9 → Vulkan → Metal)

**Track A is SpockD3D9's active graphics path.** All default builds, CI smoke tests, and benchmark title profiles target this stack. Track B (direct D3D9 → Metal) is documented separately when planned; see [DX9_METAL_ROADMAP.md](DX9_METAL_ROADMAP.md) if present on your branch.

```
D3D9 API  →  SPIR-V (DXSO + fixed-function GLSL)  →  Vulkan (DXVK)  →  MoltenVK  →  Metal
```

---

## Current status

| Area | Status | Notes |
|------|--------|-------|
| Native `libdxvk_d3d9.dylib` | **Working** | arm64 + x86_64 CI |
| MoltenVK loader + ICD auto-discovery | **Done** | `src/vulkan/`, `src/util/util_env.cpp` |
| Portability enumeration | **Done** | `VK_KHR_portability_*` |
| WSI (SDL3 / SDL2 / GLFW) | **Done** | Fullscreen, EDID, occlusion |
| Smoke tests | **Done** | `d3d9-clear`, `d3d9-gamebryo-probe` |
| Retail game boot-to-menu | **Not started** | Needs external Wine-family host + PE `d3d9.dll` |
| DXSO (SM2/SM3) on real titles | **Not started** | Probe covers FF path only today |

---

## CI validation (MoltenVK)

Run locally:

```bash
./scripts/test-macos-native.sh
```

**`d3d9-clear`** — device creation, clear, present.

**`d3d9-gamebryo-probe`** — Gamebryo-style Track A checks:

| Check | Milestone F mapping |
|-------|---------------------|
| BCn formats (DXT1/3/5) | Texture format support |
| D24S8 / D16 depth | Depth formats |
| `GetAdapterDisplayMode` | Display enumeration |
| `CheckDeviceMultiSampleType` (2×/4×, logged) | MSAA query |
| SM3 `GetDeviceCaps` | Shader model |
| `DrawPrimitiveUP` (fixed-function) | FF → SPIR-V → MSL pipeline |
| `DrawIndexedPrimitive` from `DEFAULT` VB/IB (`Lock` DISCARD) | Buffer upload + indexed draw |
| Occlusion + event (`D3DQUERYTYPE_OCCLUSION`/`EVENT`) queries | GPU visibility + fence/sync |
| `Present` + `Reset` | Device lifecycle |
| Device-lost reset cycle (`D3DPOOL_DEFAULT` blocks `Reset` → `D3DERR_DEVICENOTRESET` → `Reset` OK) | Device lost / reset handling |

Pass line: `d3d9-gamebryo-probe: OK`.

---

## Recommended configuration

Use the platform profile or a title profile:

```bash
export DXVK_CONFIG_FILE=/path/to/tools/macos/macos.dxvk.conf
# or tools/fallout3/fallout3.dxvk.conf for hosted Fallout 3
export DXVK_WSI_DRIVER=SDL3
export MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS=0   # if argument-buffer issues appear
```

**Always enable on macOS:**

| Key | Value | Why |
|-----|-------|-----|
| `dxvk.enableShaderCache` | `True` | Caches SPIR-V → MSL / pipeline work across runs |
| `dxvk.tilerMode` | `Auto` | TBDR-friendly render-pass behavior on MoltenVK |

Benchmark profiles under `tools/*/` set both keys.

### Shader cache location

DXVK stores compiled shader/pipeline data under the state cache directory (typically `~/.local/share/dxvk/` on Linux; on macOS native ports the same DXVK state-cache layout applies relative to the configured cache root). With `dxvk.enableShaderCache = True`, the second launch of a title should show far fewer MoltenVK shader compile stalls.

For diagnostics:

```bash
export DXVK_LOG_LEVEL=info    # pipeline / shader creation summaries
export DXVK_LOG_LEVEL=debug   # per-shader detail when debugging compile failures
```

---

## MoltenVK environment reference

| Variable | Purpose |
|----------|---------|
| `VK_ICD_FILENAMES` / `VK_DRIVER_FILES` | Override MoltenVK ICD (usually auto-detected via Homebrew) |
| `DYLD_LIBRARY_PATH` | Point at custom `libMoltenVK.dylib` / `libvulkan.dylib` |
| `MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS` | Set `0` if hits argument-buffer bugs (also set in CI smoke scripts) |
| `MVK_CONFIG_RESUME_LOST_DEVICE` | MoltenVK device-loss recovery (rare for D3D9) |
| `MVK_CONFIG_DEBUG` | Extra MoltenVK logging |
| `MTL_DEBUG_LAYER` | Metal API validation (heavy; use when isolating Metal-side failures) |

Full install and hosting checklist: [MACOS_TESTING.md](MACOS_TESTING.md).

Format and caps honesty: [MOLTENVK_CAPABILITIES.md](MOLTENVK_CAPABILITIES.md).

---

## Near-term Track A priorities

Ordered by dependency (from [ROADMAP.md](../ROADMAP.md) Milestone F):

1. **Boot-to-menu** — PE `d3d9.dll` + external host + `tools/fallout3/` profile
2. **DXSO SM2/SM3** — validate with real Gamebryo shaders after first menu draw
3. **In-game rendering** — outdoor/interior passes; file MoltenVK gaps from logs
4. **Title profile tuning** — `d3d9.floatEmulation`, `d3d9.maxFrameRate` as needed
5. **Upstream MoltenVK** — report `CheckDeviceFormat` / portability subset failures

Track A optimizations (shader cache, tiler mode, triple-buffering) do **not** require Track B.

---

## Related documents

| Document | Content |
|----------|---------|
| [ROADMAP.md](../ROADMAP.md) | Milestones A–F, task checklists |
| [MOLTENVK_CAPABILITIES.md](MOLTENVK_CAPABILITIES.md) | BCn, depth, MSAA on MoltenVK |
| [MACOS_TESTING.md](MACOS_TESTING.md) | Build, smoke, PE DLL, hosting |
| [FALLOUT3_COMPAT.md](FALLOUT3_COMPAT.md) | Fallout 3 subsystem checklist |
| [tools/macos/macos.dxvk.conf](../tools/macos/macos.dxvk.conf) | Platform defaults |
