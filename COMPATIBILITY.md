# SpockD3D9 Compatibility Matrix

SpockD3D9 is working toward **full compatibility with Windows D3D9 games on macOS**. This table tracks progress toward that goal — known-good titles, broken cases, and suggested `dxvk.conf` profiles for the D3D9 applications and games running on macOS via SpockD3D9 (`libdxvk_d3d9.dylib`).

Compatibility is reached through two usage paths, both serving the same goal:
1. **Native ports** — applications link `libdxvk_d3d9.dylib` directly using SDL2/SDL3/GLFW (the translation foundation, working today).
2. **Windows game hosting** — a wrapper or translation layer hosts Windows game logic and routes its `d3d9.dll` calls through SpockD3D9 (the path to unmodified Windows games, under development).

**Status legend**

| Status | Meaning |
|--------|---------|
| **Works** | Playable; no major rendering or stability issues |
| **Partial** | Runs but with visual glitches, performance issues, or missing features |
| **Broken** | Crashes, black screen, or unusable |
| **Untested** | Expected to work in theory; not yet verified on macOS |
| **Blocked** | Cannot run yet due to missing infrastructure (e.g., Win32 shims, wrapper layer) |

Contributions welcome: test a title, add a row, and open a PR. For bugs use the [macOS bug report template](.github/ISSUE_TEMPLATE/bug_report_macos.md).

---

## Benchmark compatibility targets

| Title | Status | Engine / API | Path | Notes | Detailed tracker |
|-------|--------|--------------|------|-------|-----------------|
| **Fallout 3 (Steam, Windows)** | **Blocked** | Gamebryo / D3D9 | Windows game compat | Primary target; execution model decided ([native-first translator + optional opt-in PE `d3d9.dll`, host-agnostic](docs/FALLOUT3_EXECUTION_MODEL.md)), profile shipped — PE `d3d9.dll` scaffold available via `scripts/build-pe-d3d9.sh`; boot-to-menu validation pending | [docs/FALLOUT3_COMPAT.md](docs/FALLOUT3_COMPAT.md) |
| **Fallout: New Vegas (Windows)** | **Blocked** | Gamebryo / D3D9 | Windows game compat | Benchmark target; profile shipped — shares Fallout 3's PE `d3d9.dll` blocker and Gamebryo validation path | [docs/WINDOWS_D3D9_BENCHMARKS.md](docs/WINDOWS_D3D9_BENCHMARKS.md) |
| **Dragon Age: Origins (Windows)** | **Blocked** | BioWare Eclipse / D3D9 | Windows game compat | Benchmark target; profile shipped — expands coverage beyond Gamebryo to an SM3-heavy RPG renderer | [docs/WINDOWS_D3D9_BENCHMARKS.md](docs/WINDOWS_D3D9_BENCHMARKS.md) |
| **Galactic Civilizations II (Windows)** | **Blocked** | Stardock strategy renderer / D3D9 | Windows game compat | Benchmark target; profile shipped — adds old-strategy UI, mode-picker, and map-rendering coverage | [docs/WINDOWS_D3D9_BENCHMARKS.md](docs/WINDOWS_D3D9_BENCHMARKS.md) |

### Fallout 3 — key compatibility areas

| Area | Status | Notes |
|------|--------|-------|
| Boot / device creation | **In progress** | PE `d3d9.dll` via `scripts/build-pe-d3d9.sh`; boot workflow in [docs/BOOT_TO_MENU.md](docs/BOOT_TO_MENU.md); retail V4 (menu) not yet confirmed |
| Rendering (fixed-function + SM3) | **Untested** | Gamebryo uses mixed fixed-function and shader paths |
| Fullscreen / resolution switching | **Untested** | Expects `Reset` / mode enumeration; SpockD3D9 supports this via WSI |
| Input (keyboard / mouse / gamepad) | **Blocked** | Windows binary uses DirectInput / Win32 messages |
| Audio | **Blocked** | Uses DirectSound / XAudio2; out of SpockD3D9 scope but needed for playability |
| Save / load | **Blocked** | Win32 filesystem APIs; needs wrapper support |
| Shader cache / stutter | **Untested** | MoltenVK SPIR-V → MSL compilation; `dxvk.enableShaderCache = True` recommended |
| DXT texture loading | **Untested** | BCn support on Apple Silicon via MoltenVK; see [MoltenVK caps](docs/MOLTENVK_CAPABILITIES.md) |

See [docs/FALLOUT3_COMPAT.md](docs/FALLOUT3_COMPAT.md) for the full per-subsystem checklist.

---

## Verified samples and tooling

| Title / sample | Status | WSI | Notes | `dxvk.conf` |
|----------------|--------|-----|-------|-------------|
| `d3d9-clear` (built-in smoke test) | **Works** | SDL3 | Clears back buffer and presents; exercised in CI | *(none)* |
| `d3d9-clear-sdl2` (optional smoke test) | **Works** | SDL2 | Built when both SDL3 and SDL2 are available | *(none)* |

---

## Shipped `dxvk.conf` profiles

CI validates every profile under `tools/**/*.dxvk.conf` against the options documented in the root [`dxvk.conf`](dxvk.conf). Copy a profile next to your app and rename it to `dxvk.conf`, or set `DXVK_CONFIG_FILE`.

| Profile | Path | Use case |
|---------|------|----------|
| **macOS platform (native ports)** | [`tools/macos/macos.dxvk.conf`](tools/macos/macos.dxvk.conf) | Starting point for SDL3/GLFW native D3D9 ports on Apple Silicon / Intel |
| **Fallout 3 (Gamebryo)** | [`tools/fallout3/fallout3.dxvk.conf`](tools/fallout3/fallout3.dxvk.conf) | Windows host + PE `d3d9.dll` override (when available) |
| **Fallout: New Vegas** | [`tools/fallout-new-vegas/fallout-new-vegas.dxvk.conf`](tools/fallout-new-vegas/fallout-new-vegas.dxvk.conf) | Gamebryo benchmark title |
| **Dragon Age: Origins** | [`tools/dragon-age-origins/dragon-age-origins.dxvk.conf`](tools/dragon-age-origins/dragon-age-origins.dxvk.conf) | SM3-heavy BioWare RPG |
| **Galactic Civilizations II** | [`tools/galactic-civilizations-ii/galactic-civilizations-ii.dxvk.conf`](tools/galactic-civilizations-ii/galactic-civilizations-ii.dxvk.conf) | Strategy UI / mode-picker coverage |

---

## Reference DXVK Native ports (macOS untested)

These projects ship native builds using [DXVK Native](https://github.com/doitsujin/dxvk#dxvk-native) on Linux. They are strong candidates for a macOS port with SpockD3D9 but have **not** been verified here yet.

| Title | Status | Engine / API | WSI (typical) | Notes | Suggested starting profile |
|-------|--------|--------------|---------------|-------|---------------------------|
| [Perimeter](https://github.com/KranX/Perimeter) | **Untested** | Custom / D3D9 | SDL2 | Open-source RTS; DXVK Native on Linux | `dxvk.enableShaderCache = True` |
| [Momentum Mod](https://momentum-mod.org/) | **Untested** | Source / D3D9 | SDL2 | Source-engine mod; native Linux via DXVK Native | `dxvk.enableShaderCache = True` |
| Portal 2 / Left 4 Dead 2 (Valve) | **Untested** | Source / D3D9 | SDL2 | Valve shipped DXVK Native Linux builds; macOS would need a separate port | `dxvk.enableShaderCache = True` |
| Custom SDL3 D3D9 port (your project) | **Untested** | D3D9 | SDL3 / SDL2 / GLFW | Link `libdxvk_d3d9.dylib`, pass `SDL_Window*` as `HWND` | [`tools/macos/macos.dxvk.conf`](tools/macos/macos.dxvk.conf) |

---

## D3D9 port profiles (from upstream DXVK)

Configuration presets for titles that are known to need D3D9 quirks on Vulkan.

| Title / pattern | Status (native macOS) | Typical issue | Suggested `dxvk.conf` |
|-----------------|----------------------|---------------|----------------------|
| **Fallout 3 (Gamebryo)** | **Blocked** | Requires wrapper layer; potential refresh-rate + float issues | `tools/fallout3/fallout3.dxvk.conf` |
| **Fallout: New Vegas (Gamebryo)** | **Blocked** | Requires wrapper layer; high-refresh timing and Gamebryo reset paths | `tools/fallout-new-vegas/fallout-new-vegas.dxvk.conf` |
| **Dragon Age: Origins (Eclipse)** | **Blocked** | Requires wrapper layer; SM3 effects and launcher/fullscreen paths | `tools/dragon-age-origins/dragon-age-origins.dxvk.conf` |
| **Galactic Civilizations II** | **Blocked** | Requires wrapper layer; old launcher/mode list and strategy UI rendering | `tools/galactic-civilizations-ii/galactic-civilizations-ii.dxvk.conf` |
| The Sims 2 | **Untested** | Non-standard formats (X4R4G4B4), A8 RT misuse | `d3d9.supportX4R4G4B4 = True`, `d3d9.disableA8RT = True` |
| AquaNox / AquaNox 2 | **Untested** | Breaks when too many display modes are enumerated | `d3d9.modeCountCompatibility = True` |
| Halo: Combat Evolved | **Untested** | Wrong sampler/image type combinations | `d3d9.forceSamplerTypeSpecConstants = True` |
| Metal Gear Rising: Revengeance | **Untested** | Picks lowest refresh rate from mode list | `d3d9.forceRefreshRate = 60` |
| Silent Hill 2 (Enhanced Edition mod) | **Untested** | Single-buffer swapchain + front-buffer read | `d3d9.extraFrontbuffer = True` |
| Ultra-wide sensitive titles | **Untested** | Break on exotic aspect ratios | `d3d9.forceAspectRatio = "16:9"` |
| UE3 / UE4 D3D9 renderers | **Untested** | NVAPI / vendor detection paths | `d3d9.hideNvidiaGpu = Auto`, `dxvk.enableShaderCache = True` |
| Fixed-function heavy SM2 titles | **Untested** | Float emulation edge cases | `d3d9.floatEmulation = Strict` (if artifacts) |

---

## WSI backend notes (multi-monitor / fullscreen)

| Backend | Fullscreen on secondary monitor | Borderless desktop | Notes |
|---------|----------------------------------|--------------------|-------|
| **SDL3** | **Works** (recommended) | **Works** | Best-tested path; uses saved mode + closest display mode |
| **SDL2** | **Works** | **Works** | Centers on target display; parity with SDL3 |
| **GLFW** | **Partial** | **Partial** | Improved: deferred `setWindowMode`, closest video mode, monitor bounds; borderless uses undecorated window (not compositor FS) |

SpockD3D9 selects the target monitor from the window position (`getWindowMonitor`) when entering fullscreen, not always the primary display.

Test multi-monitor with:

```bash
export DXVK_WSI_DRIVER=SDL3   # or SDL2, GLFW
export DXVK_LOG_LEVEL=info
your_app
```

---

## Windows game compatibility (in progress)

| Category | Status | Notes |
|----------|--------|-------|
| Windows `.exe` via wrapper + SpockD3D9 | **Blocked** | Requires a wrapper/translation layer to host Windows binaries and route D3D9 to SpockD3D9 |
| Windows `.exe` via Wine + upstream DXVK | Separate project | Use [upstream DXVK](https://github.com/doitsujin/dxvk) with Wine for a proven path today |
| D3D10 / D3D11 titles | Not supported | Disabled in default SpockD3D9 builds (`enable_d3d9` only) |
| Retail Steam D3D9 games (e.g., Fallout 3) | **Blocked** | Primary target; see [Fallout 3 tracker](#primary-compatibility-target) above |

---

## Common configuration snippets

### Native macOS ports — platform profile

For new SDL3/GLFW ports, start from the shipped platform profile:

```bash
cp tools/macos/macos.dxvk.conf /path/to/your/app/dxvk.conf
# or: export DXVK_CONFIG_FILE=/path/to/macos.dxvk.conf
export DXVK_WSI_DRIVER=SDL3
```

See [`tools/macos/macos.dxvk.conf`](tools/macos/macos.dxvk.conf) for MoltenVK-oriented defaults (tiler mode, shader cache, present latency).

### Apple Silicon — manual tuning

SpockD3D9 enables tiler-aware submission when MoltenVK is detected (`VK_DRIVER_ID_MOLTENVK`). Usually leave at default:

```ini
# dxvk.tilerMode = Auto
dxvk.enableShaderCache = True
```

### Stutter / present issues in fullscreen

```ini
# Prefer triple-buffering in app present params (BackBufferCount 2–3).
# If tearing is acceptable for testing:
# dxvk.tearFree = True
```

### Debug a failing title

```ini
# dxvk.enableDebugUtils = True
```

Run with:

```bash
export DXVK_LOG_LEVEL=debug
export DXVK_LOG_PATH=/tmp/dxvk-logs
mkdir -p /tmp/dxvk-logs
```

---

## What to include when reporting compatibility

1. Application name and how it links SpockD3D9
2. macOS version, chip (Apple Silicon / Intel), GPU
3. `DXVK_WSI_DRIVER` (SDL3 / SDL2 / GLFW)
4. MoltenVK version (`brew info molten-vk`)
5. Windowed vs fullscreen, single vs multi-monitor
6. Relevant `dxvk.conf` and `DXVK_LOG_LEVEL=debug` logs

See also [MoltenVK format and MSAA limits](docs/MOLTENVK_CAPABILITIES.md).
