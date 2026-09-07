# Grand Theft Auto IV on macOS via SpockD3D9

Host-side compatibility assets for Grand Theft Auto IV (2008, Rockstar
Toronto / RAGE engine, Direct3D 9). GTA IV is a **32-bit** title.

## Why this is a valid target

GTA IV has already been run end-to-end on Apple Silicon under Wine by the
d9mt project (neo773/d9mt, a D3D9 → Metal research layer — see
[docs/DX9_METAL_ROADMAP.md](../../docs/DX9_METAL_ROADMAP.md)), reporting
50–90 fps on an M1 Max. That makes its hosting path (Wine + Rosetta 2 +
32-bit D3D9 process) de-risked independent of SpockD3D9's translator.
SpockD3D9 supplies the same slot — a native `d3d9.dll` override — through
its own Track A stack (D3D9 → Vulkan → MoltenVK → Metal).

It complements the Gamebryo titles: RAGE is a different engine generation
(SM3 + deferred post-processing, draw-call heavy), which broadens D3D9
coverage beyond the Gamebryo profile the CI probe models.

## What's here

| File | Purpose |
|------|---------|
| `gta-iv.dxvk.conf` | Title-specific SpockD3D9 configuration profile |
| `README.md` | This guide |

## Execution model

```text
GTAIV.exe -> external Windows host -> SpockD3D9 d3d9.dll (32-bit)
           -> winevulkan/Vulkan loader -> MoltenVK -> Metal
```

## Hosting notes (from d9mt's proven GTA IV setup)

These are host-side facts observed by the d9mt project, not SpockD3D9
translation behavior:

1. **Remove GFWL before anything else.** The stock launcher
   (`PlayGTAIV.exe`) null-dereferences under Wine before D3D9 is ever
   touched. Install the community *xliveless* stub (replaces
   `xlive.dll`) and launch **`GTAIV.exe`** directly.
2. **32-bit process**: build the PE DLL with
   `./scripts/build-pe-d3d9.sh --arch x86`. The engine runs under
   Rosetta 2 in a 64-bit (Win 7/10) bottle; expect CPU-bound,
   playable-but-not-maxed framerates.
3. **First boot compiles a lot of shaders** — a 1–2 minute stall is
   normal while the shader cache fills; subsequent boots are fast.
   Verify the cache appears inside the prefix
   (`drive_c/users/<user>/AppData/Local/dxvk/`).
4. Default/auto graphics settings are the sanest first-boot choice.

## Setup

```bash
# 1. Build the 32-bit override DLL + smoke exe
./scripts/build-pe-d3d9.sh --arch x86

# 2. Validate the bottle boundary without the game (see docs/MACOS_TESTING.md §2a)
WINE=<bottle wine> WINEPREFIX=<bottle> ./scripts/run-pe-smoke.sh --arch x86

# 3. Install xliveless into the game folder, then:
cp build-pe-d3d9-x86/d3d9.dll "<game dir>/"
cp tools/gta-iv/gta-iv.dxvk.conf "<game dir>/dxvk.conf"

# 4. Launch (GTAIV.exe directly, not PlayGTAIV.exe)
WINE=<bottle wine> WINEPREFIX=<bottle> WINEDLLOVERRIDES="d3d9=n,b" \
  <bottle wine> "<game dir>/GTAIV.exe"
```

Or via the generic host helpers (prepare writes `spockd3d9-host.env` and
installs the DLL + profile; launch consumes them):

```bash
./scripts/prepare-steam-d3d9-host.sh --game-dir "<game dir>" \
  --profile tools/gta-iv/gta-iv.dxvk.conf
./scripts/launch-steam-d3d9-host.sh --game-dir "<game dir>" \
  --exe GTAIV.exe --title "GTA IV"
```

(Steam appid 12210 base / 11340 EFLC via `--steam --appid` only if you
must launch through Steam; the direct exe is the tested path.)

## Expected early signals

Same ladder as [docs/BOOT_TO_MENU.md](../../docs/BOOT_TO_MENU.md):
`DXVK:` banner → adapter enumeration → `D3D9: CreateDeviceEx OK (` → menu.

## Troubleshooting

| Symptom | Cause / fix |
|---------|------------|
| Crash before any DXVK line | GFWL still live — install xliveless, launch `GTAIV.exe` |
| Launcher works, game never loads | Launching `PlayGTAIV.exe` — use `GTAIV.exe` |
| "not a valid Win32 application" | 64-bit DLL in the 32-bit game — rebuild `--arch x86` |
| 1–2 min black screen on first boot | Shader compilation; verify the cache dir fills and relaunch |
| Wine builtin d3d9 in log | Override not applied — `WINEDLLOVERRIDES="d3d9=n,b"` |
