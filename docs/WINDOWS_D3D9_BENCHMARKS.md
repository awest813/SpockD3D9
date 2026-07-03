# Windows D3D9 macOS benchmark targets

SpockD3D9's compatibility work is measured against five retail Windows D3D9
games on macOS:

1. **Fallout 3** - Gamebryo, fixed-function + SM2/SM3
2. **The Elder Scrolls IV: Oblivion** - Gamebryo, fixed-function + SM2/SM3
3. **Fallout: New Vegas** - Gamebryo, fixed-function + SM2/SM3
4. **Dragon Age: Origins** - BioWare Eclipse, SM3 renderer
5. **Galactic Civilizations II** - Stardock strategy renderer, D3D9 UI/map rendering

These titles exercise the compatibility surface that matters most for hosted
Windows D3D9 games: device creation, display mode enumeration, fullscreen/reset
behavior, shader translation, BCn/depth formats on MoltenVK, front/back-buffer
presentation, and old-engine timing assumptions.

## Current blocker

All five are Windows executables. They require an external Windows host plus an
experimental PE `d3d9.dll` build of SpockD3D9. The default macOS build still
emits the native `libdxvk_d3d9.dylib`, which is the canonical translator artifact
but cannot be loaded directly by an unmodified Windows game.

## Shipped profiles

| Title | Executable | Steam App ID | Profile |
|-------|------------|--------------|---------|
| Fallout 3 | `Fallout3.exe` | 22300 (base) / 22370 (GOTY) | `tools/fallout3/fallout3.dxvk.conf` |
| The Elder Scrolls IV: Oblivion | `Oblivion.exe` | 4500 (base) / 22330 (GOTY) | `tools/oblivion/oblivion.dxvk.conf` |
| Fallout: New Vegas | `FalloutNV.exe` | 22380 (base) / 22490 (Ultimate) | `tools/fallout-new-vegas/fallout-new-vegas.dxvk.conf` |
| Dragon Age: Origins | `daorigins.exe` | 47810 | `tools/dragon-age-origins/dragon-age-origins.dxvk.conf` |
| Galactic Civilizations II | `GC2*.exe` | 3590 (Ultimate) | `tools/galactic-civilizations-ii/galactic-civilizations-ii.dxvk.conf` |

The profile validator (`tests/conf/test_dxvk_conf_profiles.py`) discovers every
`tools/**/*.dxvk.conf` file, verifies active keys against `dxvk.conf`, and fails
if any of these benchmark profiles are missing.

All five are Steam titles, so the hosted launch path uses Steam to satisfy DRM
and the overlay. Title-specific wrappers (`launch-fallout3-host.sh`,
`launch-oblivion-host.sh`) or the generic `launch-steam-d3d9-host.sh --steam
--appid <id>` work once the matching profile is installed as `dxvk.conf` in the
game directory.

**Built-in profiles (auto-applied):** Fallout 3, Oblivion, Dragon Age: Origins,
and Galactic Civilizations II also have compiled-in profiles in
`src/util/config/config.cpp` that match the game executable and auto-apply the
compat-relevant subset of the keys above (shader model, refresh-rate lock,
frame latency, focus-loss handling, plus `modeCountCompatibility` for GalCiv II).
Copying the `tools/**/*.dxvk.conf` file is therefore optional — use it to layer
opt-in tuning (e.g. `presentInterval`) on top. Fallout: New Vegas ships a
separate upstream built-in profile targeting the New Vegas Reloaded mod, so its
boot conf must still be copied manually.

## Boot-to-menu workflow (Fallout 3)

Automated helpers and V3/V4 criteria:
[BOOT_TO_MENU.md](BOOT_TO_MENU.md).

## Common first-boot validation

Track every title through the same milestones so regressions are comparable:

| Milestone | Signal |
|-----------|--------|
| V1 - DLL loads | Host loads SpockD3D9 `d3d9.dll`; logs show the SpockD3D9 banner |
| V2 - D3D object | `Direct3DCreate9` succeeds and adapter caps can be queried |
| V3 - Device | `CreateDevice` succeeds in the title's default windowed mode |
| V4 - Menu | Main menu renders with correct text, cursor, and background video/static |
| V5 - New game | First playable scene loads without device removal or shader crashes |
| V6 - Fullscreen/reset | Launcher/in-game resolution changes survive `Reset` |
| V7 - Rendering | Terrain/interiors/characters/effects render without major artifacts |
| V8 - Stability | 30+ minutes of play without crashes or device-loss loops |

## Target-specific rendering checks

### Fallout 3

- DXT1/DXT3/DXT5 world and UI textures
- D24S8 depth/stencil and shadow/effect stencil paths
- Mixed fixed-function and SM3 lighting
- `TestCooperativeLevel` / `Reset` behavior around focus and resolution changes

### The Elder Scrolls IV: Oblivion

- Same Gamebryo baseline as Fallout 3 (SM3 + fixed-function, BCn textures)
- Oblivion-specific water, HDR bloom, and distant-terrain LOD
- Launcher (`OblivionLauncher.exe`) display-mode selection and fullscreen `Reset`
- Engine timing stability when refresh rates above 60 Hz are hidden by profile

### Fallout: New Vegas

- Same Gamebryo baseline as Fallout 3, plus heavier DLC/mod-era shader coverage
- Water, horizon, Pip-Boy, and VATS post-processing
- Engine timing stability when refresh rates above 60 Hz are hidden by profile

### Dragon Age: Origins

- SM3 material and spell-effect shaders
- Character skinning, terrain lighting, and indoor/outdoor area transitions
- Launcher-selected display modes and fullscreen transitions

### Galactic Civilizations II

- Launcher/settings display-mode enumeration with a small Windows-like mode list
- Strategy map, ship previews, and UI compositing
- Long-running turn-based sessions without present/device-loss loops

## Reporting a benchmark result

Include:

1. Title, store/version, DLC/mod state
2. macOS version, Mac model/chip/GPU, and MoltenVK version
3. Host used to load the Windows executable
4. SpockD3D9 commit, profile path, and any local profile edits
5. `DXVK_LOG_LEVEL=info` logs, plus `debug` logs for crashes/device removal
6. Highest milestone reached and the first failing subsystem
