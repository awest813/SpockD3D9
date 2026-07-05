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
