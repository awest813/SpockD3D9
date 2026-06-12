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

### macOS 26 (Tahoe) + MoltenVK 1.4.x — rendering bring-up (RESOLVED)

Full hardware bring-up on macOS 26.5.1 / Apple M1 / MoltenVK 1.4.1 initially hit
two issues blocking real rendering. **Both are now resolved**: the full
`d3d9-gamebryo-probe-sdl2` (all draws, render-to-texture, MRT, 60 presented
frames, `Reset`) passes with `MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS=2`.

#### Issue 1 — 2048-entry sampler heap requires Metal argument buffers

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

#### Issue 2 (RESOLVED) — null vertex-buffer bindings without `nullDescriptor`

Initially misdiagnosed as a MoltenVK buffer-backing bug; the root cause was in
**SpockD3D9**. Any draw with an input-layout binding that had no vertex buffer
bound crashed deterministically on the DXVK CS thread:

```
EXC_BAD_ACCESS (KERN_INVALID_ADDRESS at 0x58)
  MVKBuffer::getMTLBuffer()                      ← `this` is null: VkBuffer = VK_NULL_HANDLE
  MVKCmdBindVertexBuffers<2ul>::setContent
  vkCmdBindVertexBuffers2
  dxvk::DxvkContext::updateVertexBufferBindings()
```

Upstream DXVK hard-requires the robustness2 `nullDescriptor` feature and freely
binds `VK_NULL_HANDLE` for unbound vertex-buffer slots. MoltenVK does not
advertise `nullDescriptor`, so SpockD3D9 demoted it to optional — but without a
fallback, the null binds became invalid API usage, and MoltenVK dereferenced the
null handle (fault at member offset 0x58). This explains why the crash was
deterministic and identical on MoltenVK 1.3.0 and 1.4.1, across all
argument-buffer tiers, MTLHeap, and BDA settings: it was never a driver bug.

**Fix:** `DxvkContext::updateVertexBufferBindings()` now binds a small
zero-initialised dummy vertex buffer (`getDummyVertexBufferSlice()`) for unbound
slots when `nullDescriptor` is unavailable. Core `robustBufferAccess` keeps
out-of-bounds fetches defined.

#### Validated state on macOS 26 / Apple M1

With `MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS=2`, the full
`d3d9-gamebryo-probe-sdl2` passes (exit 0, repeatedly): all draw paths
(DrawPrimitive / DrawIndexedPrimitive 16+32-bit / DrawPrimitiveUP), textures
incl. cube/volume, render-to-texture + `GetRenderTargetData`, MRT, state blocks,
vertex declarations, 60 presented frames, and device `Reset`.

#### Unbound shader resources — dummy descriptor fallback

The legacy descriptor-update path (`dxvk_context.cpp`,
`updateResourceBindings`) writes `VK_NULL_HANDLE` image views / buffers for
**unbound shader resources**, which also requires `nullDescriptor`. Retail
games (Fallout 3 included) routinely draw with unbound texture stages, so on
devices without the feature SpockD3D9 now substitutes lazily-created dummy
resources instead (`DxvkContext::ensureDummyDescriptorResources`): a 64 KiB
zeroed buffer (uniform/storage/texel, with an `R32_UINT` view) and 1×1 zeroed
images in `VK_IMAGE_LAYOUT_GENERAL` covering 2D/cube (six-layer
cube-compatible) and 3D view types, for both sampled and storage descriptors.
Unbound fetches read zero rather than crashing. The CI probe binds all its
resources, so this path is regression-tested for inertness only; first real
exercise will come from retail content.

---

## References

- [MoltenVK supported Vulkan features](https://github.com/KhronosGroup/MoltenVK#supported-vulkan-features)
- [MoltenVK known limitations](https://github.com/KhronosGroup/MoltenVK/blob/master/Docs/MoltenVK_Runtime_UserGuide.md#known-moltenvk-limitations)
- SpockD3D9 format mapping: `src/d3d9/d3d9_format.cpp`
- Adapter queries: `src/d3d9/d3d9_adapter.cpp`
