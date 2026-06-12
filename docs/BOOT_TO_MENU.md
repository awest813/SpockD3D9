# Boot to menu (V3) — Fallout 3 on macOS

**Goal:** Fallout 3 (Steam, Windows) reaches the **main menu** (rendered and interactive) when hosted on macOS with SpockD3D9's experimental PE `d3d9.dll` and an external Windows runtime (Wine-family host).

This is Milestone **V3** in [FALLOUT3_COMPAT.md](FALLOUT3_COMPAT.md) and [WINDOWS_D3D9_BENCHMARKS.md](WINDOWS_D3D9_BENCHMARKS.md).

---

## Prerequisites

| Requirement | Notes |
|-------------|--------|
| macOS 13+ | Apple Silicon or Intel |
| MoltenVK | `brew install molten-vk vulkan-loader` |
| MinGW-w64 | `brew install mingw-w64` (PE `d3d9.dll` build) |
| Fallout 3 installed | Steam Windows build inside a host prefix |
| Windows host | CrossOver, GPTK, or Wine with **winevulkan** (not committed; examples only) |

SpockD3D9 provides **only** `d3d9.dll` + Vulkan translation. DirectInput, audio, registry, and the PE loader come from the host.

---

## Quick start

### 1. Build the override DLL

```bash
./scripts/build-pe-d3d9.sh
# Output: build-pe-d3d9/d3d9.dll
```

### 2. Install into the game directory

```bash
export FALLOUT3_DIR="$HOME/Library/Application Support/CrossOver/.../drive_c/.../Fallout 3"
./scripts/prepare-fallout3-host.sh --game-dir "$FALLOUT3_DIR"
```

This copies `d3d9.dll`, `dxvk.conf` (from `tools/fallout3/fallout3.dxvk.conf`), and writes `spockd3d9-host.env` beside the game.

### 3. Launch

```bash
# Retail Steam build (Steam installed in the prefix and logged in):
./scripts/launch-fallout3-host.sh --game-dir "$FALLOUT3_DIR" --steam

# DRM-free copy / direct executable:
./scripts/launch-fallout3-host.sh --game-dir "$FALLOUT3_DIR"
```

The Steam build carries DRM and loads Steam's own D3D9 overlay, so prefer
`--steam` for the retail target. Steam App IDs: **22370** = Fallout 3 GOTY
(default), **22300** = Fallout 3 (original). Add `--no-overlay` to disable
Steam's `gameoverlayrenderer` D3D9 hook for clean first-boot diagnostics.

Or source the generated env and start the game your host normally uses
(`wine steam.exe -applaunch 22370`, `wine Fallout3.exe`, etc.).

### 4. Check logs

```bash
./scripts/check-boot-logs.sh /path/to/d3d9.log
# or pipe wine output: ... 2>&1 | tee fallout3-boot.log
```

---

## Validation milestones (hosted path)

| ID | Signal | Log / behavior |
|----|--------|----------------|
| **V1** | Host loads SpockD3D9 | `DXVK: ` version line at startup |
| **V2** | D3D9 object alive | Adapter enumeration succeeds; no immediate `D3DERR` from `Direct3DCreate9` |
| **V3** | Device created | `D3D9: CreateDeviceEx OK (` in log |
| **V4 — Boot to menu** | Main menu visible | Splash → menu; UI text readable; mouse works |
| V5+ | In-game, stability | See [FALLOUT3_COMPAT.md](FALLOUT3_COMPAT.md) |

**V4 success criteria:**

- Menu background (video or static) renders — not black screen
- Menu buttons respond to mouse
- No immediate crash after launcher
- No tight loop of `Device lost` / `Device reset` in log

---

## Environment (reference)

The prepare script writes `spockd3d9-host.env` next to the game. Typical contents:

```bash
WINEDLLOVERRIDES="d3d9=n,b"
DXVK_CONFIG_FILE="/path/to/game/dxvk.conf"
DXVK_LOG_LEVEL=info
MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS=0
```

**MoltenVK inside the prefix:** If the host does not wire Vulkan automatically:

```bash
export DYLD_LIBRARY_PATH="$(brew --prefix molten-vk)/lib:$DYLD_LIBRARY_PATH"
```

**Do not set `DXVK_WSI_DRIVER` for the PE build.** The Windows `d3d9.dll` uses Win32 surfaces through the host's `HWND`; SDL/GLFW WSI is for native `libdxvk_d3d9.dylib` only.

---

## Troubleshooting

| Symptom | What to check |
|---------|----------------|
| Host still uses built-in D3D9 | `WINEDLLOVERRIDES="d3d9=n,b"`; `d3d9.dll` next to `Fallout3.exe` |
| `DXVK: No adapters found` | MoltenVK ICD; `VK_ICD_FILENAMES` / Homebrew `molten-vk` |
| Crash before `CreateDeviceEx OK` | `DXVK_LOG_LEVEL=debug`; compare with native `d3d9-gamebryo-probe` |
| Black screen after device created | Shader compile failure — enable `dxvk.enableShaderCache = True`; retry second launch |
| `Device lost` loop on focus | Profile has `d3d9.deviceLossOnFocusLoss = False` (default in `fallout3.dxvk.conf`) |
| Instant exit, no DXVK line | Wrong DLL arch (need x86_64 PE); verify with `file d3d9.dll` |
| Game closes immediately, relaunches | Steam DRM on a direct-exe launch — use `--steam` instead of running `Fallout3.exe` |
| No `d3d9.log` after `--steam` | Steam launches detached; confirm `DXVK_LOG_PATH` points at the game dir (set by `prepare-fallout3-host.sh`) |
| Crash/black screen only with overlay | Steam overlay's D3D9 hook conflicts — retry with `--no-overlay` to isolate |
| `--steam` does nothing | Steam not running/logged in in the prefix, or wrong `--appid` (22370 GOTY / 22300 base) |
| Works on Windows DXVK, not Spock | File issue with macOS-specific caps/format — use macOS bug template |

---

## Reporting results

Use the format in [WINDOWS_D3D9_BENCHMARKS.md](WINDOWS_D3D9_BENCHMARKS.md#reporting-a-benchmark-result) and attach:

1. Host product + version (CrossOver / GPTK / Wine)
2. `check-boot-logs.sh` output
3. `DXVK_LOG_LEVEL=info` log (debug if failed before V3)
4. Screenshot or short description at highest milestone reached

---

## Native vs hosted validation

| Path | Validates | Boot to menu? |
|------|-----------|---------------|
| `d3d9-gamebryo-probe` (native dylib) | V1–V2 equivalent on MoltenVK | No — not the game binary |
| PE `d3d9.dll` + host | Full V1–V4+ | **Yes** — this document |

Run the native probe first to isolate translator issues from host issues:

```bash
./scripts/test-macos-native.sh
```

---

## Related docs

- [tools/fallout3/README.md](../tools/fallout3/README.md) — profile and architecture
- [FALLOUT3_EXECUTION_MODEL.md](FALLOUT3_EXECUTION_MODEL.md) — why hosting is external
- [MACOS_TESTING.md](MACOS_TESTING.md) — build and CI checklist
- [TRACK_A.md](TRACK_A.md) — MoltenVK path (if on branch that includes it)
