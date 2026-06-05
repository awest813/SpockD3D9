# SpockD3D9 Roadmap

**Mission: full compatibility with Windows D3D9 games on macOS.** SpockD3D9 translates D3D9 API calls to Vulkan (MoltenVK → Metal) so that Windows Direct3D 9 titles can ultimately run on Apple hardware. This document tracks what is done, what is in progress, and what remains to get there — from the native translation library, through the Win32 compatibility shims and Windows game hosting, to the first retail title (Fallout 3).

## Goals

The overarching goal is **full compatibility with Windows D3D9 games on macOS**. It breaks down into:

- **Run unmodified Windows D3D9 games on macOS** — close the Win32 compatibility gaps and provide (or integrate with) a host/wrapper layer that routes a game's `d3d9.dll` calls into SpockD3D9
- **Achieve playable compatibility with Fallout 3 (Steam, Windows, Gamebryo/D3D9)** as the first retail title, then use **Fallout: New Vegas**, **Dragon Age: Origins**, and **Galactic Civilizations II** as follow-on benchmark titles for broader Windows D3D9 coverage
- Ship a rock-solid native `libdxvk_d3d9.dylib` for Apple Silicon (arm64) and Intel Mac (x86_64) — the translation foundation everything else builds on
- Support SDL2, SDL3, and GLFW for window/surface integration (the native replacement for Win32 windowing)
- Optimize for Apple tiler GPUs via MoltenVK detection and upstream tiler heuristics
- Keep default builds **D3D9-only** (D3D8/10/11/DXGI disabled in `meson_options.txt`) to stay focused on the target API

## Architecture

```
┌─────────────┐
│  Your App   │  D3D9 API (native, SDL/GLFW window)
├─────────────┤
│  SpockD3D9  │  D3D9 → Vulkan
├─────────────┤
│  MoltenVK   │  Vulkan → Metal
├─────────────┤
│    Metal    │
└─────────────┘
```

### Graphics tracks (MoltenVK now, Metal later)

SpockD3D9 follows two planned graphics paths on macOS:

| Track | Stack | Status |
|-------|--------|--------|
| **A — MoltenVK (default)** | D3D9 → Vulkan → MoltenVK → Metal | Active — all milestones below |
| **B — Direct Metal (future)** | D3D9 → MSL → Metal (no Vulkan) | Planned — see [docs/DX9_METAL_ROADMAP.md](docs/DX9_METAL_ROADMAP.md) |

Track A ships retail compatibility first (Milestone F, MoltenVK tuning). Track B is a separate multi-phase program (RHI extraction, Metal WSI, SPIRV-Cross or native MSL, draw-path parity). **Do not start Track B backend work until Track A reaches in-game validation on at least one benchmark title** (decision gate G0 in the Metal roadmap).

Near-term MoltenVK priorities that benefit Track A without blocking Track B are listed in [docs/DX9_METAL_ROADMAP.md § Near-term MoltenVK work](docs/DX9_METAL_ROADMAP.md#near-term-moltenvk-work-track-a-enhancements).

## Status Overview

| Area | Status |
|------|--------|
| Meson native build (arm64 / x86_64) | Done |
| `package-native.sh` packaging | Done |
| Universal dylib (`lipo`) packaging | Done |
| GitHub Actions macOS matrix build | Done |
| SDL2 / SDL3 / GLFW WSI backends | Done (multi-monitor FS; GLFW borderless partial) |
| MoltenVK loader (`libvulkan.dylib` / `libMoltenVK.dylib`) | Done (auto-discovers Homebrew prefixes + ICD manifest) |
| Tiler GPU hints (`VK_DRIVER_ID_MOLTENVK`) | Done (upstream) |
| Runtime smoke test / sample app | Done (`d3d9-clear` SDL3 + SDL2 fallback, CI smoke step) |
| Game compatibility matrix | Partial (`COMPATIBILITY.md` — shipped profiles + samples; per-title macOS runs pending) |
| macOS EDID / HDR metadata | Done (EDID read on macOS; colorimetry via GetCurrentOutputDesc) |
| Native D3D9 cursor | Done (SDL2/SDL3/GLFW HW + software compositing) |
| `isOccluded` for present throttling | Done (WSI + D3D9 Present skip when minimized / occluded FS) |
| Window focus/resize → swapchain invalidation | Done (SDL/GLFW lifecycle polling + Win32 WM_SIZE) |
| SDL2 fullscreen parity with SDL3 | Done |
| `GetDeviceCaps` Vulkan-derived limits | Done (anisotropy, texture dims, MSAA honesty) |
| Win32 compat shims (native handle objects) | Done (semaphores, events, `DuplicateHandle`, `WaitForSingleObject`, `WaitForMultipleObjects`; unit-tested) |

---

## Completed

- [x] Fork focused on D3D9-only native builds (`enable_d3d9=true`, others default `false`)
- [x] macOS meson adjustments (no `--build-id`, no static libgcc on darwin, native headers)
- [x] WSI driver selection via `DXVK_WSI_DRIVER` and SDL2/SDL3/GLFW compile-time flags
- [x] Vulkan loader tries MoltenVK on macOS (`src/vulkan/vulkan_loader.cpp`)
- [x] Vulkan loader auto-discovers Homebrew-installed MoltenVK (`/opt/homebrew`, `/usr/local`, `$HOMEBREW_PREFIX`) and points the loader at the MoltenVK driver manifest when no `DYLD_LIBRARY_PATH` / `VK_DRIVER_FILES` / `VK_ICD_FILENAMES` is set (`src/util/util_env.cpp`, `src/vulkan/vulkan_loader.cpp`)
- [x] Homebrew prefix probe order matches the active architecture slice (Intel/x86_64 prefers `/usr/local`, Apple Silicon/arm64 prefers `/opt/homebrew`; universal binaries get per-slice ordering)
- [x] Instance creation enables `VK_KHR_portability_enumeration` + `VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR` when supported, so the Khronos Vulkan loader reports the MoltenVK portability driver instead of returning "No adapters found" (`src/dxvk/dxvk_instance.cpp`)
- [x] CI workflow: build both architectures, upload artifacts (`.github/workflows/build-macos.yml`)
- [x] README: build instructions, configuration, debugging env vars
- [x] D3D9 Vulkan interop exports (`d3d9_interop.cpp`) for native apps that need Vk handles
- [x] `isOccluded` implemented for SDL2, SDL3, and GLFW (focus-loss with 100 ms hysteresis)
- [x] SDL2 `enterFullscreenMode` now uses the mode saved by `setWindowMode` (parity with SDL3)
- [x] GLFW `getDesktopDisplayMode` returns the largest available mode (native resolution)
- [x] Universal binary (`lipo`) via `./package-native.sh … --arch universal` — optional/manual only; CI uses per-arch native runners instead
- [x] SDL/GLFW window lifecycle polling → focus + resize → swapchain extent invalidation
- [x] `GetDeviceCaps` uses Vulkan-derived texture dims, anisotropy, and volume extent; removes false MSAA-toggle and wideLines-conditioned AA-lines cap
- [x] MoltenVK format limits documented (`docs/MOLTENVK_CAPABILITIES.md`, README)
- [x] Game compatibility matrix scaffold (`COMPATIBILITY.md`)
- [x] Tiler mode performance notes in `dxvk.conf` and README
- [x] WSI library sonames resolved from Meson/pkg-config (`wsi_sonames.h`)
- [x] Multi-monitor fullscreen: D3D9 uses `getWindowMonitor`; GLFW/SDL3 WSI fixes (`d3d9_swapchain.cpp`, `wsi_window_glfw.cpp`, `wsi_window_sdl3.cpp`)
- [x] macOS-focused `dxvk.conf` platform profile (`tools/macos/macos.dxvk.conf`)
- [x] SDL3 as recommended WSI backend (README, CI, smoke test defaults)
- [x] Window lifecycle hooks: SDL/GLFW resize + focus → swapchain extent invalidation
- [x] D3D9 `Present` skips Vulkan work when minimized or fullscreen-occluded (parity with DXGI throttling)

---

## Milestones

### Milestone A — Builds and presents a pixel

- [x] CI installs MoltenVK and verifies Vulkan loader is present
- [x] Minimal native sample: `d3d9-clear` (SDL3 primary, SDL2 fallback)
- [x] GLFW `setWindowMode` height typo fix
- [x] GLFW / SDL2 fullscreen targets the requested monitor (not always primary)
- [x] GLFW `getWindowMonitor` uses window position / fullscreen monitor

### Milestone B — Playable windowed D3D9 app

- [x] Window resize/focus without Win32 `WM_*` messages (SDL/GLFW event path → `NotifyWindowActivated`)
- [x] Software/hardware cursor support (`d3d9_cursor_native`, SDL2/SDL3/GLFW)
- [x] `GetDeviceCaps` / adapter caps aligned with queried Vulkan/MoltenVK features
- [x] Document MoltenVK format limits (BCn, depth, MSAA) and known gaps

### Milestone C — Fullscreen and display correctness

- [x] `getMonitorEdid` on macOS (IOKit + CoreGraphics, SDL2/SDL3/GLFW WSI)
- [x] `saveWindowState` / `restoreWindowState` for native WSI (SDL2, SDL3, GLFW)
- [x] SDL2 parity with SDL3 fullscreen path (display bounds, closest mode)
- [x] `isOccluded` for present throttling
- [x] Multi-monitor exclusive/borderless fullscreen (target monitor from window position)

### Milestone D — Production hardening

- [x] Game compatibility table (title → status → `dxvk.conf` profile) — shipped profiles in `COMPATIBILITY.md` + `tools/`; per-title macOS verification still pending
- [x] Universal dylib via `lipo` in `package-native.sh`
- [x] Performance notes for tiler mode (`dxvk.tilerMode` in `dxvk.conf`)
- [x] macOS-focused issue template (`.github/ISSUE_TEMPLATE/bug_report_macos.md`)

### Milestone E — Win32 compatibility shims

Close gaps in `src/util/util_win32_compat.h` and related native shims needed for Windows game compatibility. The handle objects (semaphores, events, reference-counted duplication, multi-object waits) are covered by a hermetic unit test (`tests/util/test_win32_compat.cpp`, run in CI under `.github/workflows/unit-tests.yml`, including a ThreadSanitizer pass).

| Task | Status | Priority | Notes |
|------|--------|----------|-------|
| `GetCurrentProcessId` / `GetCurrentProcess` | **Done** | High | `getpid()` / pseudo-handle `(HANDLE)-1` |
| `CreateSemaphoreA` / `ReleaseSemaphore` | **Done** | High | Counting semaphore backed by `std::mutex` + `std::condition_variable` |
| `CreateEventA` / `CreateEventW` | **Done** | High | Auto- and manual-reset events (mutex + condition variable) |
| `SetEvent` / `ResetEvent` | **Done** | High | Signal / clear event state; auto-reset wakes one waiter, manual-reset wakes all |
| `WaitForSingleObject` / `WaitForSingleObjectEx` | **Done** | High | Waits on semaphores and events with `INFINITE` or millisecond timeout |
| `WaitForMultipleObjects` / `WaitForMultipleObjectsEx` | **Done** | Medium | Wait-any and wait-all on semaphores and events (Gamebryo threading); unit-tested |
| `DuplicateHandle` | **Done** | Medium | Reference-counted handle sharing; honors `DUPLICATE_CLOSE_SOURCE` (D3D11 frame-latency waitable object) |
| `CloseHandle` | **Done** | High | Drops a reference and frees the object at zero; dispatches on `NativeHandleKind` |
| `ProcessIdToSessionId` | **Done** | Low | Returns TRUE, session 0 (no Win32 sessions on macOS) |
| `CreateCompatibleDC` / `DeleteDC` | **Done** | Low | Minimal native memory-DC handle; `DeleteDC` lifecycle covered by `tests/util/test_win32_compat.cpp` |

### Milestone F — Fallout 3 compatibility

Primary target: Fallout 3 (Steam, Windows) running on macOS via SpockD3D9. The execution model is now decided — **native-first translator + an optional, opt-in PE `d3d9.dll`**, with hosting delegated to external Windows hosts (Wine / CrossOver / GPTK as downstream consumers, none officially targeted) ([docs/FALLOUT3_EXECUTION_MODEL.md](docs/FALLOUT3_EXECUTION_MODEL.md)). This unblocks the rest of the milestone without disturbing the native dylib path. See [docs/FALLOUT3_COMPAT.md](docs/FALLOUT3_COMPAT.md) for the detailed checklist.

| Task | Status | Notes |
|------|--------|-------|
| Define execution model (wrapper / translation layer) | **Done** | Native-first translator + optional opt-in PE `d3d9.dll`; hosting delegated to external hosts, none committed to. See [docs/FALLOUT3_EXECUTION_MODEL.md](docs/FALLOUT3_EXECUTION_MODEL.md) |
| Emit SpockD3D9 as an experimental PE `d3d9.dll` | **Scaffold done** | `-Denable_pe_d3d9=true` Meson option (default off), `cross/pe-x86_64-w64-mingw32.txt`, `scripts/build-pe-d3d9.sh`, CI cross-compile job; boot-to-menu workflow in [docs/BOOT_TO_MENU.md](docs/BOOT_TO_MENU.md) — retail V4 pending |
| Audit + polish Milestone F docs | **Done** | Audited status now aligned across [docs/FALLOUT3_COMPAT.md](docs/FALLOUT3_COMPAT.md), [docs/MACOS_TESTING.md](docs/MACOS_TESTING.md), and [docs/WINDOWS_D3D9_BENCHMARKS.md](docs/WINDOWS_D3D9_BENCHMARKS.md) |
| D3D9 device creation (Gamebryo) | **CI probe** | Native `d3d9-gamebryo-probe` covers: CreateDevice, formats, caps, all core geometry/texture/query paths (see below); retail boot-to-menu pending |
| Shader compilation (SM2/SM3 + fixed-function) | **Partial (CI)** | `d3d9-gamebryo-probe` exercises FF paths (DrawPrimitive/DrawIndexedPrimitive/DrawPrimitiveUP) → SPIR-V → MSL; DXSO SM2/SM3 on retail shaders pending |
| Texture format support (DXT1–5, depth, RT) | **CI probe** | DXT1/3/5, A8L8, D24S8/D16; A8R8G8B8 texture + RT; RT format availability logged (R16F, R32F, A16B16G16R16F, A32B32G32R32F); cube map; volume texture |
| Fullscreen / resolution enumeration | **Partial (CI)** | Probe: `EnumAdapterModes`, `GetAdapterDisplayMode`, `Reset`; exclusive fullscreen on host pending |
| Device lost / reset handling | **Done (code)** | `TestCooperativeLevel` + `Reset` + `NotifyWindowActivated` wired; `d3d9.deviceLossOnFocusLoss=False` in Fallout 3 profile; retail run pending |
| Queries (occlusion, event, timestamp) | **CI probe** | `D3DQUERYTYPE_OCCLUSION`, `D3DQUERYTYPE_EVENT` (Issue/GetData); `D3DQUERYTYPE_TIMESTAMP` logged |
| State blocks | **CI probe** | `CreateStateBlock`, `BeginStateBlock`/`EndStateBlock`, `Apply` |
| Buffers + draw paths | **CI probe** | VB (MANAGED + DYNAMIC, Lock/DISCARD/fill), IB (16-bit), DrawPrimitive, DrawIndexedPrimitive |
| Vertex declaration | **CI probe** | `D3DVERTEXELEMENT9` (XYZ+NORMAL+TEX0), `CreateVertexDeclaration`, `SetVertexDeclaration` |
| Render states | **CI probe** | Viewport, scissor, alpha blend/test, stencil, fog |
| Sampler + texture stage states | **CI probe** | MIN/MAG/MIP LINEAR, WRAP, MAXANISOTROPY; COLOROP/COLORARG, ALPHAOP |
| Render-to-texture + MRT | **CI probe** | A8R8G8B8 RT + GetRenderTargetData; 2× MRT (non-fatal if NumSimultaneousRTs<2) |
| `dxvk.conf` Fallout 3 profile | **Done** | [`tools/fallout3/fallout3.dxvk.conf`](tools/fallout3/fallout3.dxvk.conf); CI-validated against documented options |
| Benchmark profiles for Fallout: New Vegas, Dragon Age: Origins, and Galactic Civilizations II | **Done** | [`tools/fallout-new-vegas/fallout-new-vegas.dxvk.conf`](tools/fallout-new-vegas/fallout-new-vegas.dxvk.conf), [`tools/dragon-age-origins/dragon-age-origins.dxvk.conf`](tools/dragon-age-origins/dragon-age-origins.dxvk.conf), [`tools/galactic-civilizations-ii/galactic-civilizations-ii.dxvk.conf`](tools/galactic-civilizations-ii/galactic-civilizations-ii.dxvk.conf); CI-validated against documented options |
| Boot-to-menu validation | **In progress** | Scripts: `prepare-fallout3-host.sh`, `launch-fallout3-host.sh`, `check-boot-logs.sh`; guide: [docs/BOOT_TO_MENU.md](docs/BOOT_TO_MENU.md); retail run pending |
| In-game rendering validation | Not started | Outdoor + interior + NPC + effects |
| Save / load stability | Not started | Requires wrapper filesystem support |

---

## High Priority

### 1. WSI correctness (displays and fullscreen)

| Task | Files |
|------|-------|
| ~~Multi-monitor exclusive/borderless fullscreen~~ | Done — `d3d9_swapchain.cpp`, `wsi_window_glfw.cpp`, `wsi_window_sdl3.cpp`, `wsi_monitor_glfw.cpp` |
| ~~SDL soname from Meson instead of hardcoded dylib names~~ | Done — `src/wsi/wsi_sonames.h.in`, `src/wsi/meson.build` |

### 2. Runtime validation

| Task | Files |
|------|-------|
| ~~CI smoke test with MoltenVK~~ | Done — `.github/workflows/build-macos.yml`, `src/d3d9/examples/d3d9_clear.cpp` |

### 3. MoltenVK capability audit

| Task | Files |
|------|-------|
| ~~Document BCn / depth / MSAA support vs MoltenVK release~~ | Done — `docs/MOLTENVK_CAPABILITIES.md`, README |

### 4. Native cursor

| Task | Files |
|------|-------|
| ~~Implement or delegate `SetCursor` / `ShowCursor`~~ | Done — SDL2/SDL3/GLFW hardware + software cursor (`src/d3d9/d3d9_cursor.cpp`, `d3d9_cursor_native.cpp`) |

---

## Medium Priority

- ~~**Window lifecycle**: hook SDL/GLFW resize and focus → swapchain extent invalidation (`d3d9_swapchain.cpp`, `d3d9_window.cpp`)~~
- ~~**HDR / colorimetry**: depends on EDID path (`d3d9_swapchain.cpp` consumer of `getMonitorEdid`)~~
- ~~**SDL3 as recommended backend**: SDL3 WSI has the most complete fullscreen implementation; align README/CI defaults when stable~~
- ~~**macOS `dxvk.conf` profile**: annotate MoltenVK-relevant keys; de-emphasize DXGI/D3D11 options~~
- ~~**Present stats / VBlank**~~: Done — `GetPresentStats` and `WaitForVBlank` return refresh-count based timing in `d3d9_swapchain.cpp`

---

## Low Priority

- Patch APIs (`DrawRectPatch`, `DrawTriPatch`) — rare in D3D9 titles (`d3d9_device.cpp`)
- ~~**D3DRS_WRAP0–15** (fixed-function texture coordinate wrapping)~~ — Done — `d3d9_fixed_function_vert.vert`, `d3d9_device.cpp`
- Unimplemented render states (tessellation) — `d3d9_device.cpp`
- Fixed-function SM3 edge cases — `d3d9_fixed_function_vert.vert`
- Optional D3D8 build (`-Denable_d3d8=true`) for legacy titles
- Palette / indexed texture paths — `d3d9_device.cpp`

---

## Out of Scope (default builds)

- D3D9On12 (`d3d9_on_12.cpp` stubs)
- DXGI / D3D10 / D3D11 (source retained; disabled via meson options)
- **Direct Metal backend in default builds** — not implemented yet; long-term plan in [docs/DX9_METAL_ROADMAP.md](docs/DX9_METAL_ROADMAP.md) (reference: [dxmt](https://github.com/3Shain/dxmt) for D3D10/11)
- Non-D3D9 game APIs (DirectSound, DirectInput, XInput — needed for full game compatibility but outside SpockD3D9's responsibility; a wrapper layer must provide these)

---

## Track A (MoltenVK) — active path

All default builds and milestones above use **Track A**: D3D9 → Vulkan → MoltenVK → Metal.
Operational guide: [docs/TRACK_A.md](docs/TRACK_A.md).

---

## Contributing

Pick an unchecked item above, open a PR against `master`, and update this file when the milestone or task is completed. For bugs, use the macOS bug report template when available, and include: macOS version, GPU, WSI driver (`DXVK_WSI_DRIVER`), MoltenVK/Vulkan version, and `DXVK_LOG_LEVEL=debug` logs.
