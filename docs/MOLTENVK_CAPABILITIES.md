# MoltenVK Capabilities on macOS

Reaching **full compatibility with Windows D3D9 games on macOS** means working within what MoltenVK and Metal actually support. SpockD3D9 queries Vulkan (MoltenVK) at runtime for format and feature support. This document summarizes what D3D9 applications can expect on macOS, how SpockD3D9 reports caps, and where MoltenVK or Metal impose limits.

For the upstream MoltenVK feature list and known Vulkan gaps, see the [MoltenVK Runtime User Guide](https://github.com/KhronosGroup/MoltenVK/blob/master/Docs/MoltenVK_Runtime_UserGuide.md) and [supported Vulkan extensions](https://github.com/KhronosGroup/MoltenVK#supported-vulkan-features).

## How SpockD3D9 reports support

| D3D9 API | SpockD3D9 behavior |
|----------|-------------------|
| `CheckDeviceFormat` | Maps the D3D9 format to Vulkan, then queries `vkGetPhysicalDeviceFormatProperties` / image format properties for usage (texture, RT, depth, MSAA). |
| `CheckDeviceMultiSampleType` | Validates power-of-two sample counts against `framebufferColorSampleCounts` / `framebufferDepthSampleCounts`, with per-format fallback when needed. |
| `GetDeviceCaps` | Texture dimensions, anisotropy, volume extent, and AA line caps come from queried Vulkan limits (not hard-coded desktop values). |

If a format or usage is unsupported by MoltenVK/Metal, the corresponding D3D9 check returns `D3DERR_NOTAVAILABLE`.

---

## BCn block compression (DXT / BC)

D3D9 fourCC formats map to Vulkan BC block formats:

| D3D9 format | Vulkan format | Typical use |
|-------------|---------------|-------------|
| `D3DFMT_DXT1` | `VK_FORMAT_BC1_*` | Opaque / 1-bit alpha textures |
| `D3DFMT_DXT2`, `D3DFMT_DXT3` | `VK_FORMAT_BC2_*` | Explicit alpha |
| `D3DFMT_DXT4`, `D3DFMT_DXT5` | `VK_FORMAT_BC3_*` | Interpolated alpha |

**MoltenVK / Metal:** BC1–BC3 are supported for sampled textures on Apple Silicon and recent Intel Mac GPUs. Availability is reported through `CheckDeviceFormat`; do not assume support without checking.

**Known gaps:**

- **MSAA + BCn:** `CheckDeviceMultiSampleType` rejects multisampling on DXT surfaces (same rule as upstream DXVK). BC compressed targets cannot be MSAA render targets.
- **BC4 / BC5 / BC6H / BC7:** Not part of core D3D9; SpockD3D9 maps some extended formats where applicable, but most D3D9 titles only use DXT1–5.
- **PVRTC / ASTC:** MoltenVK exposes `VK_IMG_format_pvrtc` and ASTC on some Apple GPUs; D3D9 does not use these directly.

---

## Depth and stencil formats

Common D3D9 depth/stencil formats map to Vulkan depth/stencil images:

| D3D9 format | Notes |
|-------------|-------|
| `D3DFMT_D16` | Widely supported |
| `D3DFMT_D15S1` | Queried per adapter; may be unavailable |
| `D3DFMT_D24X8`, `D3DFMT_D24S8`, `D3DFMT_D24X4S4` | Depends on MoltenVK depth format support |
| `D3DFMT_D32`, `D3DFMT_D32F` | Often available as 32-bit depth |
| Lockable / vendor hacks (`D16_LOCKABLE`, `INTZ`, `RAWZ`, …) | Handled with format-specific rules; some are rejected for MSAA |

**MoltenVK / Metal:**

- Depth/stencil attachments generally work for standard 3D rendering.
- **`D3DFMT_D24S8` as a lockable system-memory surface** may not match Windows behavior; prefer `CheckDeviceFormat` before relying on lockable depth.
- **`D3DUSAGE_QUERY_DEPTHSTENCIL` / post-pixels shader depth** follow queried Vulkan format features.

**HDR / display metadata:** macOS EDID retrieval is implemented in the WSI layer; HDR output paths depend on display EDID and MoltenVK `VK_EXT_hdr_metadata` (macOS only).

---

## Multisample anti-aliasing (MSAA)

MSAA support is **format-dependent** and **power-of-two only** (2×, 4×, 8×, … up to what the device reports).

SpockD3D9 validates sample counts via:

1. `framebufferColorSampleCounts` / `framebufferDepthSampleCounts` from `VkPhysicalDeviceLimits`
2. Per-format optimal tiling queries when the general limit is insufficient

**Caps honesty:**

- `D3DPRASTERCAPS_MULTISAMPLE_TOGGLE` is **not** advertised. Vulkan does not allow toggling MSAA mid-render-pass the way some legacy D3D9 apps expect.
- MSAA is rejected for: lockable depth formats (`D16_LOCKABLE`, `D32F_LOCKABLE`, `D32_LOCKABLE`), `INTZ`, and all DXT formats.
- Maximum sample count varies by GPU and back-buffer format; use `CheckDeviceMultiSampleType` rather than assuming 8× is always valid.

**MoltenVK swapchain note:** Metal allows at most **three** concurrent swapchain images. Use triple-buffering (`BackBufferCount = 2` or `3` in D3D9 terms) for smoother present, especially in fullscreen.

---

## Other D3D9 ↔ MoltenVK gaps relevant to ports

| Area | Status / workaround |
|------|---------------------|
| Wide AA lines | Reported only when `wideLines` is enabled (common on Apple Silicon via MoltenVK). |
| 16K textures | `MaxTextureWidth/Height` follow `maxImageDimension2D` (typically 16 384 on Apple Silicon). |
| Anisotropy | Capped at min(driver limit, 16) in `GetDeviceCaps`. |
| Geometry shaders | Not required on MoltenVK because SpockD3D9's default D3D9 path does not use them. |
| Shader model 3 / fixed function | Translated to SPIR-V → MSL; rare edge cases may need title-specific `dxvk.conf` tweaks. |
| Portability enumeration / subset | MoltenVK is a portability driver. When it advertises `VK_KHR_portability_enumeration`, SpockD3D9 enables that instance extension and sets `VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR`, so the Khronos Vulkan loader reports the MoltenVK adapter instead of failing with "No adapters found". When MoltenVK advertises `VK_KHR_portability_subset`, SpockD3D9 enables that device extension during logical device creation as required by portability drivers. |

---

## Verifying support on your machine

Run with debug logging and exercise your app's format checks:

```bash
export DXVK_LOG_LEVEL=debug
export DXVK_WSI_DRIVER=SDL3   # or SDL2, GLFW
your_app
```

For a minimal end-to-end sanity check, build and run the included smoke test:

```bash
export DYLD_LIBRARY_PATH="/path/to/install/lib"
export DXVK_WSI_DRIVER=SDL3
/path/to/install/lib/d3d9-clear 3
```

MoltenVK version: `brew info molten-vk` or check the loader path printed in DXVK device info logs.

---

## Vulkan feature gaps on macOS

The table below captures Vulkan features that DXVK requires on other platforms but are absent or unreliable on some macOS + MoltenVK configurations.

| Feature | macOS status | SpockD3D9 handling |
|---------|-------------|-------------------|
| `shaderCullDistance` | Not advertised by MoltenVK 1.4.x on macOS 26 (Tahoe) / Apple Silicon | Demoted to optional. D3D9 DXSO shaders do not use cull distances; no functional impact for D3D9. D3D10/11 DXBC shaders using `gl_CullDistance[]` may fail to compile. |
| `VK_EXT_depth_clip_enable` | Not exposed by MoltenVK 1.4.x on macOS 26 (Tahoe) | Demoted to optional. D3D9 always requests depth-clip-enabled (hardware default on Metal), so the `VkPipelineRasterizationDepthClipStateCreateInfoEXT` struct is simply omitted when the extension is absent. D3D10/11 "depth clip off" paths would be broken. Pipeline struct guard also added in `dxvk_shader.cpp`. |
| `VK_EXT_robustness2` features | `robustBufferAccess2` and `nullDescriptor` not advertised on macOS 26 | Both demoted to optional. D3D9 code already guards `robustBufferAccess2` use; `m_nullDescriptors` is zero-initialised as a safe fallback. |
| Swapchain / `vkCreateMetalSurfaceEXT` | On macOS 26 + MoltenVK 1.4.1, `vkGetPhysicalDeviceSurfaceCapabilities2KHR` crashes inside `wsi_unwrap_icd_surface` when SDL2's Vulkan surface is used after `SDL_PumpEvents()` | Workaround not yet found. `d3d9-gamebryo-probe-sdl2` runs all D3D9 API probes successfully; the Present/swapchain step crashes due to this MoltenVK bug. CI targets (macos-13, macos-14) are unaffected. |
| SDL3 `vkGetInstanceProcAddr` | On macOS 26 + MoltenVK 1.4.1, Cocoa_Vulkan_CreateSurface crashes in `MVKInstance::getEntryPoint` with a PAC authentication failure | SDL2 path used as fallback via `d3d9-gamebryo-probe-sdl2`. |
| `shaderClipDistance` | Supported | Required — D3D9 SetClipPlane maps to this. |
| `samplerAnisotropy` | Supported | Required. |

If you see `Skipping: Device does not support required feature 'shaderCullDistance'` in logs, your MoltenVK version is older than the one where SpockD3D9 demoted the requirement. Update to a build that includes the `dxvk_device_info.cpp` change or re-build from source.

### macOS 26 (Tahoe) + MoltenVK 1.4.1 — two distinct blockers for rendering

Full hardware bring-up on macOS 26.5.1 / Apple M1 / MoltenVK 1.4.1 isolated
**two independent issues** that block real rendering (anything past `Clear`).
A pure `Clear`+`Present` loop (`d3d9-clear-sdl2`) is rock-solid at 1/5/20/30/60
frames; the blockers only appear once shaders or vertex data are involved.

#### Blocker 1 — 2048-entry sampler heap requires Metal argument buffers

DXVK's global sampler descriptor set is sized at `DxvkSamplerPool::MaxSamplerCount`
(2048, the Vulkan spec floor) and declared as a partially-bound, update-after-bind
sampler array. The built-in blit/meta/HUD shaders consume it. Metal's non-argument-buffer
binding model caps inline samplers at **16 per stage**, so MoltenVK can only express
the 2048-element array via argument buffers:

```
MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS=0:
  [mvk-error] Shader library compile failed:
  cannot reserve 'sampler' resource locations at index 0
  fragment ... array<sampler, 2048> s_samplers [[sampler(0)]] ...
  → "Failed to create built-in graphics pipeline" (present blitter never compiles)
```

Setting `MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS=2` makes the present shader compile.
Experimentally lowering `MaxSamplerCount` to 16 also lets the blit shader compile
with argument buffers **off** — but 16 is below D3D9's worst case (16 PS + 4 VS = 20
sampler slots), so it is not a safe global fix. A correct fix needs a Metal-aware,
per-stage sampler binding path rather than one global 2048 heap.

#### Blocker 2 — MoltenVK leaves dynamic vertex buffers unbacked (null MTLBuffer)

The hard blocker. Any draw that sources vertex data from a DXVK dynamic/upload
buffer (`DrawPrimitiveUP`, `D3DUSAGE_DYNAMIC` VBs) crashes **deterministically** on
the DXVK CS thread:

```
EXC_BAD_ACCESS (KERN_INVALID_ADDRESS at 0x58)
  MVKBuffer::getMTLBuffer()                      ← buffer has no backing MTLBuffer
  MVKCmdBindVertexBuffers<2ul>::setContent
  vkCmdBindVertexBuffers2
  dxvk::DxvkContext::updateVertexBufferBindings()
  dxvk::DxvkContext::commitGraphicsState<...>
  dxvk::DxvkContext::drawGeneric<...>            ← DrawPrimitiveUP
  dxvk::DxvkCsThread::threadFunc()
```

This is conclusively a **MoltenVK-internal buffer-backing bug**, not a DXVK misuse —
it reproduces with `MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS` = 0, 1, and 2, with and
without `MVK_CONFIG_USE_MTLHEAP=0`, and with or without
`VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT` on the buffer. `getMTLBuffer()` returning
a pointer faulting at offset 0x58 means MoltenVK never created (or lost) the underlying
`MTLBuffer` for that allocation. The crash site is **deterministic** for a fixed binary
(4/5 runs identical), confirming a logic bug in the driver, not a race.

#### What passes regardless

All ~40 D3D9 API probes — device/format/caps queries, render-target & MRT creation,
texture upload (incl. cube/volume), sampler-state and address-mode setup, state blocks,
vertex declarations, render-to-texture + `GetRenderTargetData`, and `Clear`/`Present` —
complete successfully. Only the two blockers above prevent drawing real geometry.

#### Bottom line for "all modern Macs"

Both blockers are in MoltenVK 1.4.1 / Metal on macOS 26, not in SpockD3D9:
- Blocker 1 is a Metal architectural limit (16 samplers/stage) vs. DXVK's global-heap
  design; needs either argument buffers (which trip Blocker 2) or a Metal-specific
  small-sampler binding path in DXVK.
- Blocker 2 is a MoltenVK buffer-backing regression and must be fixed upstream.

**CI is unaffected:** macos-13 (Intel) and macos-14 (Apple Silicon) ship an older
MoltenVK that does not exhibit Blocker 2, and `d3d9-clear` validates the present path
there. To unblock macOS 26 / Apple Silicon: file Blocker 2 against
KhronosGroup/MoltenVK with the stack above, or bundle/require a MoltenVK build newer
than 1.4.1 once fixed. Minimal repro for both: `d3d9-gamebryo-probe-sdl2`.

---

## References

- [MoltenVK supported Vulkan features](https://github.com/KhronosGroup/MoltenVK#supported-vulkan-features)
- [MoltenVK known limitations](https://github.com/KhronosGroup/MoltenVK/blob/master/Docs/MoltenVK_Runtime_UserGuide.md#known-moltenvk-limitations)
- SpockD3D9 format mapping: `src/d3d9/d3d9_format.cpp`
- Adapter queries: `src/d3d9/d3d9_adapter.cpp`
