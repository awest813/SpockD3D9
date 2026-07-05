# Dungeons & Dragons: Dragonshard on macOS via SpockD3D9

This directory holds the host-side compatibility assets for Dungeons & Dragons:
Dragonshard (2005, Liquid Entertainment / Atari), a Windows **Direct3D 9** title.
The GOG re-release is DRM-free, which makes it a simpler hosting target than a
Steam/DRM game (no store overlay hooking `d3d9.dll`, direct-executable launch).

## Why this is a valid target

Unlike OpenGL titles (which SpockD3D9 cannot translate), Dragonshard renders
through Direct3D 9.0c with Hardware T&L — confirmed by its DirectX 9.0c
requirement and its listing on PCGamingWiki's Direct3D 9 games list. Its
renderer is largely **fixed-function**, which is the best-tested path in
SpockD3D9's `d3d9-gamebryo-probe` CI coverage.

## What's here

| File | Purpose |
|------|---------|
| `dragonshard.dxvk.conf` | Title-specific SpockD3D9 configuration profile |
| `README.md` | This guide |

## Execution model

```text
Dragonshard.exe -> external Windows host -> SpockD3D9 d3d9.dll (32-bit)
                -> winevulkan/Vulkan loader -> MoltenVK -> Metal
```

The host (Wine / CrossOver / GPTK / Cosmos — examples, not committed targets)
provides Windows process loading, Win32 windowing, input, audio, filesystem,
registry, and threading. SpockD3D9 provides only the D3D9 -> Vulkan translation.

## Setup (intended workflow)

1. Build the **32-bit** experimental PE `d3d9.dll` (Dragonshard is a 32-bit
   process and can only load a 32-bit DLL):

   ```bash
   ./scripts/build-pe-d3d9.sh --arch x86
   # Output: build-pe-d3d9-x86/d3d9.dll
   ```

   (Or download the `spockd3d9-pe-d3d9-x86-<sha>` artifact from the
   `Experimental PE d3d9.dll (MinGW cross, x86)` CI job.)

2. Copy that `d3d9.dll` next to the active Dragonshard executable
   (`Dragonshard.exe`; verify the name in the installed game folder) or into the
   host prefix's `system32`.

3. Configure the host to prefer that DLL:

   ```bash
   export WINEDLLOVERRIDES="d3d9=n,b"
   ```

4. Copy `dragonshard.dxvk.conf` next to the game executable as `dxvk.conf`, or
   point to it explicitly:

   ```bash
   export DXVK_CONFIG_FILE="$PWD/dragonshard.dxvk.conf"
   ```

5. Launch the game and collect `DXVK_LOG_LEVEL=info` logs. Expected early
   signals (mirroring the boot ladder in `docs/BOOT_TO_MENU.md`):
   - `DXVK: ` version line at startup (host loaded SpockD3D9)
   - Adapter enumeration succeeds after `Direct3DCreate9`
   - `D3D9: CreateDeviceEx OK (` — device created
   - Main menu renders and is interactive

## Cosmos (Whisky-family) bottle walkthrough

Cosmos is a Whisky-family Wine wrapper for Apple Silicon that manages bottles and
exposes graphics-backend / DLL-override toggles. SpockD3D9 plugs in as a native
(non-builtin) `d3d9.dll` so the path is **D3D9 → Vulkan → MoltenVK → Metal**
instead of the wrapper's own D3DMetal/DXVK. See also
[docs/MACOS_TESTING.md §3](../../docs/MACOS_TESTING.md).

### Two Cosmos gotchas to settle first

1. **32-bit support must be on.** Dragonshard is a 32-bit process, and 32-bit is
   the historical sticking point for Apple Silicon Wine wrappers. The bottle's
   Wine must have new-WoW64 (32-on-64) support. Confirm Cosmos launches the
   game's *unmodified* launcher before adding SpockD3D9 — if no 32-bit `.exe`
   runs, that is a host blocker, not a translation bug.
2. **Cosmos's own D3D backend will shadow our DLL.** If Cosmos keeps translating
   `d3d9` itself (D3DMetal/GPTK or bundled DXVK), it intercepts before our DLL
   loads. Force `d3d9` to native and leave the bottle's Vulkan (winevulkan →
   MoltenVK) path intact — our PE DLL calls `vkCreateWin32SurfaceKHR`.

### 0. Get the 32-bit DLL from CI

```bash
cd ~/Downloads
unzip spockd3d9-pe-d3d9-x86-*.zip   # -> d3d9.dll
file d3d9.dll                        # must say PE32 executable (DLL) ... Intel 80386 (not PE32+)
```

### 1. Locate the game folder inside the bottle

In Cosmos: select the bottle → **Open in Finder**. The game lives under
`drive_c` (GOG default `drive_c/GOG Games/...`). Capture it:

```bash
GAME_DIR="$HOME/Library/Containers/<cosmos-container>/Bottles/<id>/drive_c/GOG Games/Dungeons and Dragons - Dragonshard"
ls "$GAME_DIR"/Dragonshard.exe
```

### 2. Install the override + profile

```bash
cp ~/Downloads/d3d9.dll "$GAME_DIR/"
cp dragonshard.dxvk.conf "$GAME_DIR/dxvk.conf"
```

### 3. Configure the bottle (Cosmos GUI)

- **DLL Overrides**: add `d3d9` → **Native then Builtin** (`n,b`).
- **Graphics / D3D backend**: turn **off** Cosmos's built-in D3D9 translation so
  it does not handle `d3d9`; leave Vulkan/MoltenVK intact.
- **Environment variables** (per-bottle):
  ```
  WINEDLLOVERRIDES = d3d9=n,b
  DXVK_LOG_LEVEL   = info
  DXVK_LOG_PATH    = <GAME_DIR>
  MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS = 0
  ```
  Add `DYLD_LIBRARY_PATH = $(brew --prefix molten-vk)/lib` only if Vulkan is not
  already wired inside the bottle.

### 4. Launch and confirm

Run `Dragonshard.exe` from Cosmos (the game exe, not a re-installer), then:

```bash
grep -E "DXVK:|CreateDeviceEx OK|No adapters" "$GAME_DIR"/d3d9.log
```

- `DXVK:` banner → our DLL loaded (if you see Wine's builtin d3d9, revisit 3).
- adapter enumeration succeeds → MoltenVK reachable.
- `D3D9: CreateDeviceEx OK (` → device created (V3).
- menu renders + mouse works → boot-to-menu (V4).

### Cosmos troubleshooting

| Symptom | Cause / fix |
|---------|-------------|
| No `.exe` starts, no DXVK line | 32-bit unsupported by the bottle's Wine (WoW64) — host blocker |
| Wine builtin d3d9 in log, not `DXVK:` | Cosmos D3D backend still owns d3d9, or override not `n,b` |
| `DXVK: No adapters found` | Bottle Vulkan not reaching MoltenVK — ensure winevulkan + `DYLD_LIBRARY_PATH` |
| "not a valid Win32 application" | 64-bit DLL in a 32-bit game — re-check `file d3d9.dll` says `PE32` |
| Black screen after `CreateDeviceEx OK` | Shader compile — relaunch (cache warms); `DXVK_LOG_LEVEL=debug` |

## Notes

- Dragonshard's launcher/settings dialog is historically fragile on modern
  displays (a community DxWrapper "Windows 10 fix" exists). The profile sets
  `d3d9.modeCountCompatibility` and `d3d9.forceRefreshRate = 60` defensively; if
  the settings dialog still misbehaves, that is a host/game issue, not a
  translation bug.
- If Vulkan is not wired automatically inside the prefix:
  `export DYLD_LIBRARY_PATH="$(brew --prefix molten-vk)/lib:$DYLD_LIBRARY_PATH"`
- Do **not** set `DXVK_WSI_DRIVER` for the PE build — the Windows `d3d9.dll`
  presents through the host's `HWND`, not SDL/GLFW.
