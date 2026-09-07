# DX9 → Metal Roadmap

This document is the long-term plan for a **Direct3D 9 → Metal** translation path on macOS, alongside the current **D3D9 → Vulkan → MoltenVK → Metal** stack in SpockD3D9.

It answers three questions:

1. **Why** consider bypassing MoltenVK?
2. **What** must be built for D3D9 straight to Metal?
3. **When** does each phase make sense relative to the MoltenVK track in [ROADMAP.md](../ROADMAP.md)?

---

## Executive summary

| Track | Stack | Role |
|-------|--------|------|
| **Track A (current)** | D3D9 → SPIR-V → Vulkan (DXVK) → MoltenVK → Metal | Ship games now; reuse upstream DXVK; MoltenVK handles MSL |
| **Track B (future)** | D3D9 → MSL / Metal IR → Metal | Lower CPU overhead, fewer portability gaps, tile-aware presentation |

**Track A remains the default** until retail benchmark titles (Fallout 3, etc.) boot and render reliably. **Track B** is a multi-phase engineering program, not a drop-in replacement: it implies a new graphics backend and shader target while reusing most of the D3D9 front end.

Reference implementations for direct Metal translation:

| Project | Scope | Status |
|---------|-------|--------|
| [dxmt](https://github.com/3Shain/dxmt) | D3D10/11 → Metal via Wine | Mature reference for the Metal backend shape (resources, encoders, winemetal) |
| [d9mt](https://github.com/neo773/d9mt) | D3D9 → Metal via Wine | Research project: vendored DXVK D3D9 frontend (DXSO → SPIR-V) + SPIRV-Cross to MSL + DXMT's winemetal bridge across the wow64 boundary; Apple Silicon only; **one** tested game (GTA IV, ~50–90 fps on an M1 Max); persistent metallib disk cache, out-of-process shader compilation, async pipeline compilation, command batching, buffer suballocation |
| [dx9mt](https://github.com/theodorechapman/dx9mt) | 32-bit D3D9 → Metal via Wine | Built for Fallout: New Vegas with a different Wine boundary (shared-memory frame replay, no DXVK frontend); in-game rendering reported at early quality (~15 fps dense exteriors on M4 Pro) |

There is still no *general-purpose* mature "dxmt for D3D9" — each project above has exactly one tested title — so SpockD3D9's DXSO layer and fixed-function paths remain the in-repo starting point. But d9mt is a working existence proof for most of Track B (its shader path is exactly Phase 4a), which makes Track B a study-and-adapt problem rather than green-field invention. Their existence does not change the gating: Track A retail validation (gate G0) still comes first.

---

## Current architecture (Track A)

```
┌─────────────────────────────────────────────────────────────┐
│  Windows game / native port                                 │
│  IDirect3DDevice9, textures, shaders, Present               │
└───────────────────────────┬─────────────────────────────────┘
                            │
┌───────────────────────────▼─────────────────────────────────┐
│  src/d3d9/          D3D9 API + state + Gamebryo quirks      │
│  src/dxso/          DXSO bytecode → SPIR-V                   │
│  src/d3d9/shaders/  Fixed-function GLSL → SPIR-V            │
└───────────────────────────┬─────────────────────────────────┘
                            │ SPIR-V
┌───────────────────────────▼─────────────────────────────────┐
│  src/dxvk/          Vulkan device, passes, pipelines, mem   │
│  src/wsi/           SDL3/SDL2/GLFW → VkSurfaceKHR           │
│  src/vulkan/        Loader (libvulkan / libMoltenVK)        │
└───────────────────────────┬─────────────────────────────────┘
                            │ Vulkan 1.x + portability subset
┌───────────────────────────▼─────────────────────────────────┐
│  MoltenVK (external)  SPIR-V → MSL, Vk* → MTL*              │
└───────────────────────────┬─────────────────────────────────┘
                            │
┌───────────────────────────▼─────────────────────────────────┐
│  Metal driver (Apple GPU)                                   │
└─────────────────────────────────────────────────────────────┘
```

**Already integrated for MoltenVK** (see [MOLTENVK_CAPABILITIES.md](MOLTENVK_CAPABILITIES.md)):

| Area | Location |
|------|----------|
| Loader + ICD discovery | `src/vulkan/vulkan_loader.cpp`, `src/util/util_env.cpp` |
| Portability enumeration | `src/dxvk/dxvk_instance.cpp` |
| Portability subset device ext | `src/dxvk/dxvk_device_info.cpp` |
| Tiler heuristics (`VK_DRIVER_ID_MOLTENVK`) | `src/dxvk/dxvk_device.cpp`, `dxvk.tilerMode` |
| Honest caps / formats | `src/d3d9/d3d9_adapter.cpp`, `d3d9_format.cpp` |
| macOS platform profile | `tools/macos/macos.dxvk.conf` |

**Primary pain on Track A:** runtime **SPIR-V → MSL** inside MoltenVK (shader compile stutter, pipeline cache sensitivity). Mitigations are documented in `dxvk.conf` (`dxvk.enableShaderCache`, triple-buffering, tiler mode) — not eliminated without Track B or aggressive offline caching.

---

## Target architecture (Track B)

```
┌─────────────────────────────────────────────────────────────┐
│  D3D9 front end (mostly unchanged)                          │
│  src/d3d9/*, src/dxso/* (parser + analysis)                 │
└───────────────────────────┬─────────────────────────────────┘
                            │ RHI commands (new boundary)
┌───────────────────────────▼─────────────────────────────────┐
│  src/metal/ or src/rhi/     Metal backend                   │
│  MTLDevice, heaps, encoders, pipeline states, argument buf  │
└───────────────────────────┬─────────────────────────────────┘
                            │
┌───────────────────────────▼─────────────────────────────────┐
│  src/wsi/metal/             CAMetalLayer / SDL Metal view   │
└───────────────────────────┬─────────────────────────────────┘
                            │
┌───────────────────────────▼─────────────────────────────────┐
│  Metal (no MoltenVK, no Vulkan instance)                    │
└─────────────────────────────────────────────────────────────┘
```

**Design principle:** introduce a **Render Hardware Interface (RHI)** between `D3D9DeviceEx` and the backend so Track A (Vulkan) and Track B (Metal) can coexist behind Meson options, similar to how upstream DXVK keeps multiple APIs but shares SPIR-V infrastructure.

---

## Why bypass MoltenVK?

| Motivation | Detail |
|------------|--------|
| **Shader compile latency** | MoltenVK compiles SPIR-V → MSL at pipeline creation; D3D9 titles create many unique FF + SM2/SM3 variants |
| **Vulkan portability subset** | Missing or limited features vs desktop Vulkan; workarounds live in DXVK + MoltenVK |
| **Double translation cost** | D3D9 state → Vulkan objects → Metal objects; extra validation and barrier semantics |
| **Tile-based GPUs** | Metal exposes TBDR explicitly; Vulkan-on-Metal inherits MoltenVK’s render-pass lowering |
| **Debugging** | Single API boundary (D3D9 ↔ Metal) vs D3D9 ↔ Vulkan ↔ MoltenVK |
| **Distribution** | Optional standalone dylib without Vulkan loader / ICD on end-user machines |

**Costs of Track B:** large initial investment, two backends to maintain until Track A is retired, and no upstream DXVK merge path for Metal-specific fixes.

---

## Phased roadmap

### Phase 0 — MoltenVK track completion (prerequisite)

**Goal:** Prove the D3D9 front end and title profiles on real hardware before splitting the backend.

| Milestone | Deliverable | ROADMAP link |
|-----------|-------------|--------------|
| F.1 | PE `d3d9.dll` boot-to-menu on external host | Milestone F |
| F.2 | DXSO + fixed-function on Fallout 3 / NV / DAO | Milestone F |
| F.3 | BCn + depth format matrix on MoltenVK | [MOLTENVK_CAPABILITIES.md](MOLTENVK_CAPABILITIES.md) |
| F.4 | Title `dxvk.conf` tuning under load | `tools/*/` |

**Exit criteria for Phase 0:** at least one benchmark title reaches **in-game rendering** on Track A with documented blockers only in non-graphics subsystems (audio, input, etc.).

---

### Phase 1 — RHI design and scaffolding

**Goal:** Stop `d3d9_device.cpp` from calling `DxvkDevice` / `DxvkContext` directly for new code paths; define backend interfaces.

| Task | Description | Primary files |
|------|-------------|---------------|
| 1.1 | Document RHI surface area (resources, passes, draws, queries, present) | `docs/DX9_METAL_ROADMAP.md` (this file), new `include/rhi/rhi_*.h` |
| 1.2 | Extract Vulkan backend behind `IRhiDevice` / `IRhiContext` | Wrap existing `dxvk::*` |
| 1.3 | Meson option `enable_metal_backend` (default `false`) | `meson_options.txt`, `src/metal/meson.build` |
| 1.4 | Stub Metal device that logs and fails gracefully | `src/metal/metal_device.mm` |
| 1.5 | CI: compile Metal stubs on macOS only (no runtime requirement yet) | `.github/workflows/build-macos.yml` |

**Risks:** RHI churn during Milestone F fixes; mitigate with thin adapter over DXVK first, no behavior change.

**Duration signal:** touches ~40+ D3D9 translation units that include `dxvk_*.h` (see grep in `src/d3d9/`).

---

### Phase 2 — Presentation and windowing (Metal WSI)

**Goal:** Present frames without `VkSwapchainKHR`.

| Task | Description | Notes |
|------|-------------|-------|
| 2.1 | `CAMetalLayer` on native NSView / SDL3 Metal view | SDL3 `SDL_WINDOW_METAL` on macOS |
| 2.2 | Drawable acquisition, vsync, occlusion | Mirror `d3d9_swapchain.cpp` throttling |
| 2.3 | Fullscreen / display modes | Reuse `src/wsi/darwin/wsi_edid_darwin.mm` |
| 2.4 | Back-buffer count ≤ 3 | Metal swapchain limit (same as MoltenVK note) |

**Dependency:** Phase 1 RHI `present()` / `resize()` hooks.

**Validation:** Port `d3d9-clear` to Metal-only backend (clear color → present, no Vulkan).

---

### Phase 3 — Resource and format layer

**Goal:** Map D3D9 resources to Metal without Vulkan format enums.

| Task | Description | Reuse from SpockD3D9 |
|------|-------------|----------------------|
| 3.1 | `MTLTexture` / `MTLBuffer` allocation + hazard tracking | Logic from `d3d9_texture.cpp`, `d3d9_mem.cpp` |
| 3.2 | Format table D3D9 → `MTLPixelFormat` | `d3d9_format.cpp` rules, [MOLTENVK_CAPABILITIES.md](MOLTENVK_CAPABILITIES.md) |
| 3.3 | BCn (BC1–BC3) upload and sampling | Same title assumptions as today |
| 3.4 | Depth/stencil attachments + MSAA resolve | Metal MSAA rules differ from Vulkan pass merging |
| 3.5 | Lockable / staging surfaces | Highest parity risk vs Windows |

**Exit criteria:** `CheckDeviceFormat` / `GetDeviceCaps` backed by `MTLDevice` features, not `vkGetPhysicalDevice*`.

---

### Phase 4 — Shader pipeline (critical path)

**Goal:** Produce **MSL** (or Metal libraries) from D3D9 shader inputs without MoltenVK.

| Input path | Current (Track A) | Track B options |
|------------|-------------------|-----------------|
| DXSO (SM1–3) | `src/dxso/` → SPIR-V | **A)** DXSO → SPIR-V → SPIRV-Cross → MSL (offline or runtime) |
| Fixed-function | GLSL → SPIR-V | **B)** MSL templates / metalfx-style generators from FF keys |
| SWVP emulation | `d3d9_swvp_emu.cpp` + FF shaders | Same FF generator |

**Recommended staged approach:**

1. **4a — SPIRV-Cross bridge:** Keep `dxso` + glslang SPIR-V output; add `src/metal/shader_spirv_cross.mm` to emit MSL and `MTLLibrary` at create time. Fastest path to first triangle; still pays cross-compile cost once per module (cacheable on disk). *Validated externally:* d9mt ships exactly this shader path (DXSO → SPIR-V → SPIRV-Cross → MSL) in a running game.
2. **4b — FF MSL generator:** Replace `src/d3d9/shaders/*.glsl` with MSL emission from `D3D9FixedFunctionPipeline` keys (mirror `d3d9_fixed_function.cpp`).
3. **4c — DXSO → MSL (optional):** Only if SPIRV-Cross gaps block titles; highest engineering cost.

| Task | Description | Reference prior art |
|------|-------------|---------------------|
| 4.1 | MSL pipeline cache (hash DXSO + FF key + render state) | d9mt: on-disk metallib cache (`~/Library/Caches/d9mt/`, `D9MT_METALLIB_CACHE`), compiled out-of-process so each shader compiles once ever and reloads without stutter |
| 4.2 | `MTLRenderPipelineState` / depth-stencil / sampler from D3D9 state blocks | d9mt: async pipeline-state creation in its native arm64 unixlib |
| 4.3 | Constant buffer layout ↔ Metal buffer bindings (argument buffers vs discrete buffers) | dxmt argument-buffer design; MoltenVK tier-2 experience from Track A |
| 4.4 | Validator parity with `d3d9_shader_validator.cpp` | — |

**Validation:** `d3d9-gamebryo-probe` on Metal backend; then Fallout 3 menu shaders.

---

### Phase 5 — Draw path and fixed-function parity

**Goal:** Record draws with `MTLRenderCommandEncoder` for the same D3D9 entry points as today.

| Task | Description | Vulkan reference |
|------|-------------|------------------|
| 5.1 | Input layout / vertex streams | `d3d9_vertex_declaration.cpp` |
| 5.2 | Index buffers, primitive types, instancing | `d3d9_device.cpp` draw paths |
| 5.3 | Render states → pipeline state objects | `d3d9_state.cpp`, FF module |
| 5.4 | Texturing, samplers, clip planes, fog | FF pixel/vertex paths |
| 5.5 | Occlusion / event queries | `d3d9_query.cpp` |
| 5.6 | `Reset`, device lost, `TestCooperativeLevel` | Gamebryo requirement (Milestone F) |

**Tiler awareness:** encode load/store actions and pass boundaries for Apple TBDR (today partially via `dxvk.tilerMode` on MoltenVK).

---

### Phase 6 — Performance, caching, and production

| Task | Description | Reference prior art |
|------|-------------|---------------------|
| 6.1 | On-disk MSL + pipeline cache (analogous to `dxvk.enableShaderCache`) | d9mt metallib disk cache (each shader compiles once, ever) |
| 6.2 | Argument buffers / heap residency tuning | dxmt / d9mt argument-buffer usage; MoltenVK tier-2 findings from Track A |
| 6.3 | Per-title Metal profiles (`tools/macos/*.metal.conf` or shared `dxvk.conf` keys) | — |
| 6.4 | PE `d3d9.dll` linked against Metal backend (if Windows host path still needed) | d9mt crosses the Wine boundary via DXMT's winemetal unixlib instead of winevulkan — the fallback design if the 32-bit winevulkan → MoltenVK chain proves unreliable |
| 6.5 | Benchmark vs Track A on same titles (frame time, shader hitches, memory) | d9mt reports command batching + buffer suballocation + clean frame pacing as its main CPU-side wins |

**Success metrics:**

- Boot-to-menu and in-game parity with Track A on benchmark titles
- Measurable reduction in **first-frame / shader-create** stalls vs MoltenVK
- No regression in format/caps honesty

---

## Component reuse matrix

| Component | Track A | Track B reuse |
|-----------|---------|---------------|
| `src/d3d9/` API objects | Yes | **High** — business logic, Gamebryo quirks |
| `src/dxso/` parser, analysis | Yes | **High** — bytecode front end |
| `src/d3d9/shaders/` GLSL | SPIR-V source | **Replace** with MSL generation (Phase 4b) |
| `src/dxvk/` | Full backend | **None** as-is; reference for behavior |
| `src/spirv/`, glslang | Yes | **Medium** if SPIRV-Cross bridge (Phase 4a) |
| `src/wsi/` SDL/GLFW | VkSurface | **Partial** — window events; new Metal layer |
| `src/vulkan/` loader | Yes | **None** on pure Metal builds |
| `tools/*.dxvk.conf` | MoltenVK tuning | **Fork** profiles for Metal-only keys |
| Win32 compat shims | Yes | **Full** — unchanged |

---

## Near-term MoltenVK work (Track A enhancements)

These items improve Track A **without** waiting for Track B and should stay prioritized in [ROADMAP.md](../ROADMAP.md):

| Item | Action |
|------|--------|
| Shader stutter | Enforce `dxvk.enableShaderCache` in title profiles; document cache paths in [MACOS_TESTING.md](MACOS_TESTING.md) |
| Pipeline cache warm-up | Pre-run probes; log `VkPipelineCache` hit rate at `DXVK_LOG_LEVEL=info` |
| MoltenVK env tuning | Document `MVK_CONFIG_*` next to `MTL_DEBUG_LAYER` for title debugging |
| Portability gaps | File upstream MoltenVK issues for any `CheckDeviceFormat` failure on real titles |
| Tiler mode | Keep `dxvk.tilerMode = Auto` for `VK_DRIVER_ID_MOLTENVK`; validate per title |
| Triple buffering | Default `BackBufferCount` guidance in benchmark READMEs |

---

## Decision gates

| Gate | Question | If “no” |
|------|----------|---------|
| **G0** | Does Track A reach in-game on a benchmark title? | Defer Phase 1+; invest in Milestone F only |
| **G1** | Is SPIRV-Cross MSL quality sufficient for SM2/SM3 + FF? | Invest in Phase 4c or title-specific patches |
| **G2** | Is RHI abstraction stable after Phase 1? | Freeze API before Phase 4 shader work |
| **G3** | Does Track B beat Track A on shader-bound scenes? | Keep Track A as default; Track B optional build |

---

## Build and packaging (future)

Proposed Meson layout (not implemented):

```meson
option('graphics_backend', type : 'combo',
       choices : ['vulkan', 'metal', 'vulkan+metal'],
       value : 'vulkan',
       description : 'Graphics API for D3D9 translation')
```

| `graphics_backend` | Artifact | Dependencies |
|--------------------|----------|--------------|
| `vulkan` (default) | `libdxvk_d3d9.dylib` | MoltenVK or Vulkan loader |
| `metal` | `libdxvk_d3d9_metal.dylib` (name TBD) | Metal.framework, no Vulkan |
| `vulkan+metal` | Both; runtime or env selects | Development / A-B testing |

---

## Relationship to other projects

| Project | Relationship |
|---------|--------------|
| **Upstream DXVK** | Track A sync source; Metal backend unlikely upstream |
| **MoltenVK** | Track A runtime; contribute fixes for D3D9-exposed Vulkan gaps |
| **dxmt** | Architectural reference for D3D10/11 → Metal; origin of the winemetal bridge |
| **d9mt** | Working D3D9 → Metal prototype (DXVK D3D9 frontend + SPIRV-Cross + winemetal, GTA IV); Track B study target for shader caching, async pipelines, command batching, and the Wine boundary — note its licenses are deferred to vendored DXVK/DXMT/spirv-cross components, so reuse needs a license review |
| **dx9mt** | 32-bit D3D9 → Metal experiment targeting Fallout: New Vegas via shared-memory frame replay; alternative Wine-boundary design, single-title maturity |
| **Wine / CrossOver / GPTK** | Host PE loader; unchanged — still load `d3d9.dll` / dylib |

---

## Suggested milestone labels (Track B)

| ID | Name | Depends on |
|----|------|------------|
| **M0** | MoltenVK retail validation | — |
| **M1** | RHI + Metal stub compiles | M0 (soft) |
| **M2** | `d3d9-clear` on Metal present | M1 |
| **M3** | Textures + depth formats on Metal | M2 |
| **M4** | SPIRV-Cross SM3 + FF draws | M3 |
| **M5** | Fallout 3 boot-to-menu (Metal) | M4, M0 |
| **M6** | Performance parity / optional default | M5 |

Update this table as phases complete; link PRs to milestone IDs in commit messages.

---

## References

- [ROADMAP.md](../ROADMAP.md) — near-term MoltenVK / Milestone F work
- [MOLTENVK_CAPABILITIES.md](MOLTENVK_CAPABILITIES.md) — format and caps on Track A
- [MACOS_TESTING.md](MACOS_TESTING.md) — build, smoke test, MoltenVK env vars
- [FALLOUT3_EXECUTION_MODEL.md](FALLOUT3_EXECUTION_MODEL.md) — hosting model for Windows games
- [MoltenVK Runtime User Guide](https://github.com/KhronosGroup/MoltenVK/blob/master/Docs/MoltenVK_Runtime_UserGuide.md)
- [dxmt](https://github.com/3Shain/dxmt) — D3D10/11 → Metal reference implementation
- [SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross) — SPIR-V → MSL for Phase 4a
