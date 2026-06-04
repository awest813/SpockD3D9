# Fallout 3 (Steam, Windows) — Compatibility Checklist

Fallout 3 is SpockD3D9's first retail compatibility target — the beachhead for the project's broader goal of **full compatibility with Windows D3D9 games on macOS**. As a representative Gamebryo/D3D9 title, getting it running end to end exercises nearly every subsystem that later Windows games will also need, so it doubles as the proving ground for the whole compatibility path. This document tracks every subsystem required to get Fallout 3 running on macOS through SpockD3D9; the shared benchmark tracker for Fallout 3, Fallout: New Vegas, Dragon Age: Origins, and Galactic Civilizations II lives in [WINDOWS_D3D9_BENCHMARKS.md](WINDOWS_D3D9_BENCHMARKS.md).

**Game details:**
- **Title:** Fallout 3 (+ DLCs / GOTY)
- **Platform:** Steam (Windows)
- **Engine:** Gamebryo (NetImmerse-derived)
- **Graphics API:** Direct3D 9 (SM2.0 / SM3.0, fixed-function fallbacks)
- **Resolution:** Up to 1920×1200 typically; higher with mods
- **Other APIs used:** DirectInput, DirectSound / XAudio2, Win32 filesystem, Win32 threading

---

## Execution model

Fallout 3 is a Windows `.exe`. To run it on macOS with SpockD3D9, a **wrapper or translation layer** must:

1. Load and execute the Windows binary (or a recompiled/patched version)
2. Intercept `d3d9.dll` imports and route them to `libdxvk_d3d9.dylib`
3. Translate Win32 windowing (HWND) to SDL2/SDL3/GLFW windows
4. Provide stubs or implementations for non-D3D9 APIs (DirectInput, DirectSound, filesystem)

**Decision (2026-06): native-first translator + optional, opt-in PE `d3d9.dll`;
hosting delegated to external hosts, none committed to.** SpockD3D9 keeps the
native `libdxvk_d3d9.dylib` as its canonical, supported artifact and owns only
the D3D9 → Vulkan translation. To enable Windows game hosting without perturbing
that, it additionally emits a Windows-PE `d3d9.dll` behind an **optional,
non-default** Meson target. An external Windows host (e.g. Wine, CrossOver, or
Apple's Game Porting Toolkit — treated as downstream consumers, not officially
targeted platforms) loads that DLL as a `d3d9` override and provides the
non-D3D9 Win32 surface. Full rationale, the options table, and consequences are
in [FALLOUT3_EXECUTION_MODEL.md](FALLOUT3_EXECUTION_MODEL.md). Host setup and the
title profile live in [`tools/fallout3/`](../tools/fallout3/).

**PE `d3d9.dll` scaffold (2026-06):** `-Denable_pe_d3d9=true` Meson option
(default off), `cross/pe-x86_64-w64-mingw32.txt`, and
[`scripts/build-pe-d3d9.sh`](../scripts/build-pe-d3d9.sh) cross-compile an
experimental Windows `d3d9.dll` for external hosts. Boot-to-menu validation is
still pending.

## Current audited status (2026-06-04)

- **Execution model:** Done (native-first translator + optional PE `d3d9.dll`;
  external hosts provide non-D3D9 Win32 services). See
  [FALLOUT3_EXECUTION_MODEL.md](FALLOUT3_EXECUTION_MODEL.md).
- **Build/packaging scaffolding:** Done for the optional PE path (`enable_pe_d3d9`
  Meson option, cross file, helper script, CI coverage) and for native profile
  validation (`tests/conf/test_dxvk_conf_profiles.py`).
- **Native CI probe:** `d3d9-gamebryo-probe` (Track A / MoltenVK) exercises
  Gamebryo-typical `CreateDevice` parameters, BCn/D24S8 format queries, display
  mode enumeration, MSAA checks, SM3 caps, fixed-function `DrawPrimitiveUP`
  (SPIR-V → MSL), Present, and `Reset` on macOS CI. See [TRACK_A.md](TRACK_A.md).
  This covers V1–V2 on the native path only; DXSO SM2/SM3 on retail shaders
  remains manual.
- **Runtime game milestones:** Retail boot-to-menu (V3+) still unverified on real
  host/game runs. Treat the V1–V10 table below as the active tracker for manual progression.
- **Operational checklist:** Use [MACOS_TESTING.md](MACOS_TESTING.md) for local
  validation flow and [WINDOWS_D3D9_BENCHMARKS.md](WINDOWS_D3D9_BENCHMARKS.md)
  when reporting benchmark outcomes.

---

## D3D9 subsystem checklist

These are the D3D9 features Fallout 3 / Gamebryo is known to use. SpockD3D9 must handle all of them correctly.

### Device lifecycle

- [ ] `Direct3DCreate9` → `IDirect3D9` creation
- [ ] `IDirect3D9::CreateDevice` with appropriate flags
- [ ] `IDirect3DDevice9::TestCooperativeLevel` (focus loss / device lost)
- [ ] `IDirect3DDevice9::Reset` (resolution change, fullscreen toggle)
- [ ] Adapter enumeration (`GetAdapterCount`, `GetAdapterIdentifier`)
- [ ] Display mode enumeration (`EnumAdapterModes`, `GetAdapterDisplayMode`)
- [ ] `CheckDeviceFormat` for all formats Gamebryo queries
- [ ] `CheckDeviceMultiSampleType` (Gamebryo supports MSAA)
- [ ] `GetDeviceCaps` — verify SM3 caps, texture limits, render target caps

### Rendering

- [ ] Fixed-function vertex processing (Gamebryo uses FFP for some paths)
- [ ] Vertex shaders (SM2.0 and SM3.0)
- [ ] Pixel shaders (SM2.0 and SM3.0)
- [ ] Multiple render targets (MRT) — Gamebryo deferred lighting
- [ ] Alpha blending and alpha testing
- [ ] Stencil operations (shadow volumes, effects)
- [ ] Fog (vertex and table fog)
- [ ] Texture stage states (fixed-function multi-texturing)
- [ ] `DrawPrimitive` / `DrawIndexedPrimitive` (main draw paths)
- [ ] `DrawPrimitiveUP` / `DrawIndexedPrimitiveUP` (immediate-mode draws)
- [ ] Scissor test
- [ ] Viewport management

### Textures and formats

- [ ] DXT1 (BC1) — most world textures
- [ ] DXT3 (BC2) — some UI / alpha textures
- [ ] DXT5 (BC3) — normal maps, detail textures
- [ ] A8R8G8B8, X8R8G8B8 — render targets, UI
- [ ] R16F, R32F — HDR / float render targets (if Gamebryo HDR is enabled)
- [ ] D24S8 — primary depth/stencil
- [ ] D16 — shadow map depth (some configurations)
- [ ] L8, A8L8 — lightmaps, grayscale textures
- [ ] Volume textures (3D) — rare but possible in effects
- [ ] Cube maps — environment reflections
- [ ] Mipmapping and anisotropic filtering
- [ ] Texture addressing modes (wrap, clamp, mirror, border)

### Swap chain and presentation

- [ ] `Present` with various swap effects (`D3DSWAPEFFECT_DISCARD`, `_FLIP`, `_COPY`)
- [ ] Windowed mode presentation
- [ ] Exclusive fullscreen presentation
- [ ] `D3DPRESENT_INTERVAL_ONE` / `_IMMEDIATE` (vsync control)
- [ ] Back buffer format negotiation
- [ ] Triple buffering (`BackBufferCount = 2`)
- [ ] `GetFrontBufferData` (screenshots — used by some mods)
- [ ] `GetRenderTargetData` (render-to-texture readback)

### State management

- [ ] Render state block (`CreateStateBlock`, `BeginStateBlock`, `EndStateBlock`)
- [ ] All render states Gamebryo uses (see upstream DXVK for coverage)
- [ ] Sampler states (filtering, addressing, LOD bias, max anisotropy)
- [ ] Texture stage states
- [ ] Stream source management
- [ ] Vertex declaration

### Buffers

- [ ] Vertex buffers (MANAGED, DEFAULT, DYNAMIC pools)
- [ ] Index buffers (16-bit and 32-bit)
- [ ] `Lock` / `Unlock` with `DISCARD`, `NOOVERWRITE`, `READONLY` flags
- [ ] Proper MANAGED pool → device-local upload

### Queries

- [ ] Occlusion queries (`D3DQUERYTYPE_OCCLUSION`) — Gamebryo uses these
- [ ] Event queries (`D3DQUERYTYPE_EVENT`) — GPU fence / sync
- [ ] Timestamp queries (if used)

---

## Non-D3D9 dependencies

These are outside SpockD3D9's scope but must be provided by the wrapper layer for Fallout 3 to function.

| Subsystem | Windows API | Notes |
|-----------|-------------|-------|
| Input | DirectInput 8, Win32 messages (`WM_KEYDOWN`, etc.) | Keyboard, mouse, gamepad |
| Audio | DirectSound, XAudio2 | Music, SFX, voice |
| Filesystem | Win32 file APIs (`CreateFile`, `ReadFile`, etc.) | Saves, configs, BSA archives |
| Threading | Win32 threads, critical sections, events | Gamebryo is multi-threaded |
| Registry | `RegOpenKeyEx`, `RegQueryValueEx` | Steam game settings, install path |
| System info | `GetSystemInfo`, `GlobalMemoryStatusEx` | Hardware detection |
| Windowing | `CreateWindowEx`, `SetWindowPos`, message pump | Window creation and management |

---

## `dxvk.conf` profile

The starting Fallout 3 configuration is shipped as a real, CI-validated file at
[`tools/fallout3/fallout3.dxvk.conf`](../tools/fallout3/fallout3.dxvk.conf)
(every key is checked against the documented option set by
`tests/conf/test_dxvk_conf_profiles.py`). Copy it next to `Fallout3.exe` as
`dxvk.conf` or point `DXVK_CONFIG_FILE` at it; see
[`tools/fallout3/README.md`](../tools/fallout3/README.md) for the full host
setup. Adjust the commented tuning keys based on test results.

---

## Validation milestones

| Milestone | Description | Status |
|-----------|-------------|--------|
| **V1 — Library loads** | SpockD3D9 loads and `Direct3DCreate9` returns a valid object | **CI (native)** — `d3d9-gamebryo-probe` |
| **V2 — Device created** | `CreateDevice` succeeds with Gamebryo's requested parameters | **CI (native)** — `d3d9-gamebryo-probe` |
| **V3 — Boot to menu** | Fallout 3 main menu renders and is interactive | **In progress** — [BOOT_TO_MENU.md](BOOT_TO_MENU.md) + host scripts; retail confirmation pending |
| **V4 — New game loads** | Character creation / Vault 101 intro renders | Not started |
| **V5 — Outdoor rendering** | Capital Wasteland renders correctly (terrain, NPCs, sky) | Not started |
| **V6 — Interior rendering** | Indoor environments (Vault, buildings) render correctly | Not started |
| **V7 — Effects** | Particles, lighting, shadows, water render correctly | Not started |
| **V8 — Stability** | 30+ minutes of gameplay without crashes | Not started |
| **V9 — Save/load** | Save and load game works correctly | Not started |
| **V10 — Playable** | Full game is playable from start to finish | Not started |

---

## References

- [Fallout 3 on PCGamingWiki](https://www.pcgamingwiki.com/wiki/Fallout_3) — known issues, fixes, engine details
- [Gamebryo engine overview](https://en.wikipedia.org/wiki/Gamebryo) — engine architecture
- [DXVK Fallout 3 compatibility](https://github.com/doitsujin/dxvk/wiki) — upstream DXVK notes
- [MoltenVK capabilities](MOLTENVK_CAPABILITIES.md) — format and feature support on macOS
- [SpockD3D9 compatibility matrix](../COMPATIBILITY.md) — overall game tracker
