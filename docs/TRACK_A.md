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
| DXSO (SM2/SM3) on real titles | **Partial** | Probe draws with hand-assembled SM2.0 VS+PS (incl. unbound-sampler texld); retail SM3 shaders pending |

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
| BCn formats (DXT1/3/5), A8R8G8B8, L8, A8L8 | Texture format support |
| D24S8 / D16 depth | Depth formats |
| RT format availability logged (A8R8G8B8, X8R8G8B8, R16F, R32F, A16B16G16R16F, A32B32G32R32F) | HDR / deferred lighting |
| `GetAdapterDisplayMode` + `EnumAdapterModes` | Display enumeration |
| `CheckDeviceMultiSampleType` (2×/4×, logged) | MSAA query |
| SM3 `GetDeviceCaps` | Shader model |
| `CreateStateBlock`, `BeginStateBlock`/`EndStateBlock`, `Apply` | Render state management |
| `D3DQUERYTYPE_OCCLUSION` (Issue/GetData) | Occlusion queries (Gamebryo visibility culling) |
| `D3DQUERYTYPE_EVENT` (Issue/GetData) | GPU fence / frame sync |
| Viewport, scissor, alpha blend, alpha test, stencil, fog | Core render states |
| Vertex buffers (MANAGED + DYNAMIC, Lock/fill) + `DrawPrimitive` | Buffer management (main game draw path) |
| Lock flags: `D3DLOCK_DISCARD`, `D3DLOCK_NOOVERWRITE` (DYNAMIC), `D3DLOCK_READONLY` (MANAGED) | Buffer lock contract |
| 16-bit + 32-bit index buffers + `DrawIndexedPrimitive` | Indexed geometry |
| Texture A8R8G8B8 (mips, lock/upload, sampler states) + DXT1 create | Texture pipeline |
| Sampler address modes: CLAMP, MIRROR (BORDER non-fatal) | Texture wrapping/edge behavior |
| Render-to-texture (A8R8G8B8 RT + `GetRenderTargetData`) | Shadow maps / deferred |
| `DrawPrimitiveUP` (fixed-function) | FF → SPIR-V → MSL pipeline |
| `DrawIndexedPrimitive` from `DEFAULT` VB/IB (`Lock` DISCARD) | Buffer upload + indexed draw |
| Occlusion + event (`D3DQUERYTYPE_OCCLUSION`/`EVENT`) queries | GPU visibility + fence/sync |
| `CreateStateBlock(D3DSBT_ALL)` capture + `Apply` | Render state block management |
| `Present` + `Reset` | Device lifecycle |
| Device-lost reset cycle (`D3DPOOL_DEFAULT` blocks `Reset` → `D3DERR_DEVICENOTRESET` → `Reset` OK) | Device lost / reset handling |
| Frame-shape loop (`probeFrameShape`): churn-weighted indexed draws + constant/sampler/texture/render-state sets + declaration/viewport churn + offscreen RT switches + `StretchRect` composites + clip plane + `Begin/EndScene`, ratios from a real traced Fallout: New Vegas frame (1,411 DIP, 3,947 VS-const, 2,184 sampler, 1,092 render-state, 14 RT switches, 6 StretchRect per frame; scaled 1:50 for CI) | Gamebryo frame API-pressure profile |
| DXSO shader corpus (`tests/dxso/dxso-corpus --selftest`, fixtures) | SM1–3 bytecode → SPIR-V at scale (real-game `.sdp` corpora locally) |

Pass line: `d3d9-gamebryo-probe: OK`.

---

## Recommended configuration

Use the platform profile or a title profile:

```bash
export DXVK_CONFIG_FILE=/path/to/tools/macos/macos.dxvk.conf
# or tools/fallout3/fallout3.dxvk.conf for hosted Fallout 3
export DXVK_WSI_DRIVER=SDL3
export MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS=2   # current MoltenVK descriptor path
```

**Always enable on macOS:**

| Key | Value | Why |
|-----|-------|-----|
| `dxvk.enableShaderCache` | `True` | Persists compiled shader IR across launches so known shaders are not re-translated (DXSO → IR) |
| `dxvk.tilerMode` | `Auto` | TBDR-friendly render-pass behavior on MoltenVK |

Benchmark profiles under `tools/*/` set both keys. `dxvk.enableShaderCache` is
read at device creation (`dxvk_options.cpp`); `DXVK_SHADER_CACHE=0` is an env
kill-switch, and setting `DXVK_SHADER_DUMP_PATH` implicitly disables the cache
(dump mode).

### Shader cache location

DXVK's on-disk cache stores compiled shader IR per executable as two files,
`<exe-fnv1a64>.dxvk.lut` + `.dxvk.bin`, resolved in this order:

1. `$DXVK_SHADER_CACHE_PATH` (explicit directory)
2. Windows (PE `d3d9.dll` under Wine): `%LOCALAPPDATA%\dxvk` — i.e. inside the
   prefix at `drive_c/users/<user>/AppData/Local/dxvk`
3. Otherwise: `$XDG_CACHE_HOME/dxvk` or `~/.cache/dxvk`

The DXVK cache skips re-translating known shaders (DXSO → IR) on later
launches — DXVK logs cache hits at `DXVK_LOG_LEVEL=info`. The remaining
SPIR-V → MSL compile is MoltenVK-side; expect the biggest first-launch stall
there and judge warm-up by second-launch behavior per title.

For diagnostics:

```bash
export DXVK_LOG_LEVEL=info    # pipeline / shader creation + cache-hit summaries
export DXVK_LOG_LEVEL=debug   # per-shader detail when debugging compile failures
```

---

## MoltenVK environment reference

| Variable | Purpose |
|----------|---------|
| `VK_ICD_FILENAMES` / `VK_DRIVER_FILES` | Override MoltenVK ICD (usually auto-detected via Homebrew) |
| `DYLD_LIBRARY_PATH` | Point at custom `libMoltenVK.dylib` / `libvulkan.dylib` |
| `MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS` | Set `2` for MoltenVK argument-buffer descriptors (also set in CI smoke scripts) |
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
