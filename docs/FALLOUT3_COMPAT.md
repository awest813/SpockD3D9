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
  Gamebryo-typical `CreateDevice` parameters; BCn/D24S8/A8L8 format queries;
  RT format availability (A8R8G8B8, R16F, R32F, A16B16G16R16F, A32B32G32R32F);
  display mode enumeration; MSAA checks; SM3 caps; state blocks; occlusion and
  event queries; render states (viewport, scissor, alpha blend, alpha test,
  stencil, fog); vertex buffers (MANAGED + DYNAMIC, lock/fill/draw); 16-bit
  index buffer + DrawIndexedPrimitive; texture creation/upload (A8R8G8B8 mips +
  sampler states; DXT1 create); render-to-texture + GetRenderTargetData;
  fixed-function DrawPrimitiveUP (SPIR-V → MSL); Present; Reset. See
  [TRACK_A.md](TRACK_A.md). Covers V1–V2 on the native path; DXSO SM2/SM3 on
  retail shaders, vertex declarations, exclusive fullscreen remain manual.
- **Runtime game milestones:** Retail boot-to-menu (V3+) still unverified on real
  host/game runs. Treat the V1–V10 table below as the active tracker for manual progression.
- **Operational checklist:** Use [MACOS_TESTING.md](MACOS_TESTING.md) for local
  validation flow and [WINDOWS_D3D9_BENCHMARKS.md](WINDOWS_D3D9_BENCHMARKS.md)
  when reporting benchmark outcomes.

---

## D3D9 subsystem checklist

These are the D3D9 features Fallout 3 / Gamebryo is known to use. SpockD3D9 must handle all of them correctly.

### Device lifecycle

- [x] `Direct3DCreate9` → `IDirect3D9` creation — CI probe
- [x] `IDirect3D9::CreateDevice` with appropriate flags — CI probe
- [x] `IDirect3DDevice9::TestCooperativeLevel` (focus loss / device lost) — CI probe + native code; retail run pending
- [x] `IDirect3DDevice9::Reset` (resolution change, fullscreen toggle) — CI probe
- [x] Adapter enumeration (`GetAdapterCount`, `GetAdapterIdentifier`) — CI probe
- [x] Display mode enumeration (`EnumAdapterModes`, `GetAdapterDisplayMode`) — CI probe
- [x] `CheckDeviceFormat` for all formats Gamebryo queries — CI probe (DXT1/3/5, A8R8G8B8, R5G6B5, A1R5G5B5, L8, A8L8, D24S8, D16; RT formats logged)
- [x] `CheckDeviceMultiSampleType` (Gamebryo supports MSAA) — CI probe (logged, non-fatal)
- [x] `GetDeviceCaps` — SM3 caps verified in CI probe; texture limits from Vulkan

### Rendering

- [x] Fixed-function vertex processing — CI probe (DrawPrimitiveUP + textured quad + VB draws)
- [ ] Vertex shaders (SM2.0 and SM3.0) — pending retail DXSO
- [ ] Pixel shaders (SM2.0 and SM3.0) — pending retail DXSO
- [x] Multiple render targets (MRT) — CI probe (2× A8R8G8B8; non-fatal if NumSimultaneousRTs<2)
- [x] Alpha blending and alpha testing — CI probe (render state set/restore)
- [x] Stencil operations (shadow volumes, effects) — CI probe (state set/restore)
- [x] Fog (vertex and table fog) — CI probe (D3DRS_FOGENABLE + D3DFOG_LINEAR)
- [x] Texture stage states (fixed-function multi-texturing) — CI probe (COLOROP/COLORARG, ALPHAOP)
- [x] `DrawPrimitive` / `DrawIndexedPrimitive` (main draw paths) — CI probe (VB MANAGED+DYNAMIC, IB 16-bit)
- [x] `DrawPrimitiveUP` / `DrawIndexedPrimitiveUP` (immediate-mode draws) — CI probe
- [x] Scissor test — CI probe (SetScissorRect + D3DRS_SCISSORTESTENABLE)
- [x] Viewport management — CI probe (SetViewport)

### Textures and formats

- [x] DXT1 (BC1) — CI probe
- [x] DXT3 (BC2) — CI probe
- [x] DXT5 (BC3) — CI probe
- [x] A8R8G8B8, X8R8G8B8 — CI probe (texture + RT availability logged)
- [ ] R16F, R32F — HDR / float render targets — availability logged in CI probe; MoltenVK support hardware-dependent
- [x] D24S8 — CI probe
- [x] D16 — CI probe
- [x] L8, A8L8 — lightmaps, grayscale textures — CI probe
- [x] Volume textures (3D) — CI probe (A8R8G8B8 16×16×4, lock/fill; non-fatal if unavailable)
- [x] Cube maps — CI probe (A8R8G8B8 32×32, lock face 0 + fill)
- [x] Mipmapping (auto-gen, full chain) — CI probe (CreateTexture with mip=0)
- [x] Anisotropic filtering — CI probe (D3DSAMP_MAXANISOTROPY=8)
- [x] Texture addressing modes (wrap) — CI probe (D3DTADDRESS_WRAP); clamp/mirror/border pending

### Swap chain and presentation

- [x] `Present` with `D3DSWAPEFFECT_DISCARD` — CI probe; `_FLIP`/`_COPY` pending retail
- [x] Windowed mode presentation — CI probe
- [ ] Exclusive fullscreen presentation — pending hosted run
- [x] `D3DPRESENT_INTERVAL_ONE` (vsync) — CI probe; `_IMMEDIATE` pending retail
- [x] Back buffer format negotiation (X8R8G8B8) — CI probe
- [x] Triple buffering (`BackBufferCount = 2`) — CI probe
- [x] `GetFrontBufferData` — CI probe (logged, non-fatal; may be unavailable headless)
- [x] `GetRenderTargetData` (render-to-texture readback) — CI probe (logged; non-fatal)

### State management

- [x] Render state block (`CreateStateBlock`, `BeginStateBlock`, `EndStateBlock`) — CI probe
- [x] All render states Gamebryo uses — CI probe covers blend, alpha, stencil, fog, scissor, viewport; full coverage via upstream DXVK
- [x] Sampler states (filtering, addressing, LOD bias, max anisotropy) — CI probe (MIN/MAG/MIP LINEAR, WRAP, MAXANISOTROPY=8)
- [x] Texture stage states — CI probe (COLOROP/COLORARG, ALPHAOP)
- [x] Stream source management — CI probe (SetStreamSource in VB tests)
- [x] Vertex declaration — CI probe (D3DVERTEXELEMENT9 XYZ+NORMAL+TEX0, CreateVertexDeclaration, SetVertexDeclaration)

### Buffers

- [x] Vertex buffers (MANAGED + DYNAMIC) — CI probe (Lock/fill/Draw; D3DLOCK_DISCARD for DYNAMIC)
- [x] Index buffers (16-bit) — CI probe; 32-bit pending retail
- [x] `Lock` / `Unlock` with `DISCARD` (DYNAMIC) and default (MANAGED) — CI probe; `NOOVERWRITE`/`READONLY` pending
- [x] MANAGED pool → device-local upload — CI probe (texture + VB MANAGED with mips)

### Queries

- [x] Occlusion queries (`D3DQUERYTYPE_OCCLUSION`) — CI probe (Issue/GetData round-trip)
- [x] Event queries (`D3DQUERYTYPE_EVENT`) — CI probe (Issue/GetData round-trip)
- [x] Timestamp queries — CI probe (D3DQUERYTYPE_TIMESTAMP + D3DQUERYTYPE_TIMESTAMPFREQ; logged, non-fatal)

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
