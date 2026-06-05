/**
 * Gamebryo / Fallout 3-style D3D9 device creation probe (Milestone F).
 *
 * Exercises the native SpockD3D9 path (Track A / MoltenVK):
 *   - Format queries (BCn, A8L8, D24S8/D16; RT formats logged)
 *   - Display mode enumeration, MSAA, SM3 caps
 *   - State blocks, occlusion + event + timestamp queries
 *   - Render states: viewport, scissor, alpha blend/test, stencil, fog
 *   - Vertex buffers (MANAGED + DYNAMIC), 16-bit index buffer
 *   - DrawPrimitive, DrawIndexedPrimitive, DrawPrimitiveUP (fixed-function)
 *   - Vertex declaration (D3DVERTEXELEMENT9 / SetVertexDeclaration)
 *   - Texture A8R8G8B8 (mips, lock/upload, sampler), DXT1 create
 *   - Cube map (A8R8G8B8, lock/fill), volume texture (A8R8G8B8 16×16×4)
 *   - Render-to-texture (A8R8G8B8 RT + GetRenderTargetData)
 *   - MRT (2× A8R8G8B8, if NumSimultaneousRTs≥2)
 *   - GetFrontBufferData (logged, non-fatal)
 *   - Present + Reset
 * Intended for CI on macOS alongside d3d9-clear; does not require the game binary.
 *
 * Exit 0 when the probe prints "d3d9-gamebryo-probe: OK".
 */

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#if defined(D3D9_CLEAR_SDL3)
#include <SDL3/SDL.h>
#if defined(__APPLE__)
#include <SDL3/SDL_vulkan.h>
#endif
#define D3D9_PROBE_WSI_DRIVER "SDL3"
#else
#include <SDL.h>
#if defined(__APPLE__)
#include <SDL_vulkan.h>
#endif
#define D3D9_PROBE_WSI_DRIVER "SDL2"
#endif
#include <windows.h>
#include <d3d9.h>

#include "../../util/util_env.h"
#include "../../util/util_string.h"

namespace {

  void configureWsiDriver() {
#if defined(_WIN32)
    _putenv_s("DXVK_WSI_DRIVER", D3D9_PROBE_WSI_DRIVER);
#else
    setenv("DXVK_WSI_DRIVER", D3D9_PROBE_WSI_DRIVER, 1);
#endif
  }

  int parseFrameCount(int argc, char** argv) {
    if (argc < 2)
      return 3;

    char* end = nullptr;
    const long value = std::strtol(argv[1], &end, 10);
    if (end == argv[1] || value < 1)
      return 3;

    return int(value);
  }

#if defined(__APPLE__)
  bool vulkanLibraryLoaded(const char* path) {
#if defined(D3D9_CLEAR_SDL3)
    return SDL_Vulkan_LoadLibrary(path);
#else
    return SDL_Vulkan_LoadLibrary(path) == 0;
#endif
  }

  bool loadVulkanPortabilityLibrary() {
    if (vulkanLibraryLoaded(nullptr))
      return true;

    if (const char* envPath = std::getenv("SDL_VULKAN_LIBRARY")) {
      if (envPath[0] != '\0' && vulkanLibraryLoaded(envPath))
        return true;
    }

    static const char* const vulkanLibs[] = {
      "libMoltenVK.dylib",
      "libvulkan.1.dylib",
    };

    for (const auto& prefix : dxvk::env::getHomebrewPrefixes()) {
      for (const char* libName : vulkanLibs) {
        const std::string path = dxvk::str::format(prefix, "/lib/", libName);

        if (vulkanLibraryLoaded(path.c_str()))
          return true;
      }
    }

    return false;
  }
#endif

  bool checkFormat(IDirect3D9* d3d9, UINT adapter, D3DFORMAT format, const char* name) {
    const HRESULT hr = d3d9->CheckDeviceFormat(
      adapter,
      D3DDEVTYPE_HAL,
      D3DFMT_X8R8G8B8,
      0,
      D3DRTYPE_TEXTURE,
      format);

    if (FAILED(hr)) {
      std::fprintf(stderr, "d3d9-gamebryo-probe: CheckDeviceFormat %s failed (0x%08lx)\n",
        name, hr);
      return false;
    }

    std::printf("d3d9-gamebryo-probe: CheckDeviceFormat %s OK\n", name);
    return true;
  }

  bool checkDepthFormat(IDirect3D9* d3d9, UINT adapter, D3DFORMAT format, const char* name) {
    const HRESULT hr = d3d9->CheckDeviceFormat(
      adapter,
      D3DDEVTYPE_HAL,
      D3DFMT_X8R8G8B8,
      D3DUSAGE_DEPTHSTENCIL,
      D3DRTYPE_SURFACE,
      format);

    if (FAILED(hr)) {
      std::fprintf(stderr, "d3d9-gamebryo-probe: CheckDeviceFormat %s failed (0x%08lx)\n",
        name, hr);
      return false;
    }

    std::printf("d3d9-gamebryo-probe: CheckDeviceFormat %s OK\n", name);
    return true;
  }

  void logRenderTargetFormat(IDirect3D9* d3d9, UINT adapter, D3DFORMAT format, const char* name) {
    const HRESULT hr = d3d9->CheckDeviceFormat(
      adapter,
      D3DDEVTYPE_HAL,
      D3DFMT_X8R8G8B8,
      D3DUSAGE_RENDERTARGET,
      D3DRTYPE_SURFACE,
      format);

    if (SUCCEEDED(hr))
      std::printf("d3d9-gamebryo-probe: RT %s OK\n", name);
    else
      std::printf("d3d9-gamebryo-probe: RT %s unavailable (0x%08lx)\n", name, hr);
  }

  bool probeOcclusionQuery(IDirect3DDevice9* device) {
    IDirect3DQuery9* query = nullptr;
    HRESULT hr = device->CreateQuery(D3DQUERYTYPE_OCCLUSION, &query);
    if (FAILED(hr)) {
      std::fprintf(stderr, "d3d9-gamebryo-probe: CreateQuery OCCLUSION failed (0x%08lx)\n", hr);
      return false;
    }

    hr = query->Issue(D3DISSUE_BEGIN);
    if (FAILED(hr)) {
      std::fprintf(stderr, "d3d9-gamebryo-probe: OCCLUSION Issue BEGIN failed (0x%08lx)\n", hr);
      query->Release();
      return false;
    }

    hr = query->Issue(D3DISSUE_END);
    if (FAILED(hr)) {
      std::fprintf(stderr, "d3d9-gamebryo-probe: OCCLUSION Issue END failed (0x%08lx)\n", hr);
      query->Release();
      return false;
    }

    DWORD pixelCount = 0;
    // Flush with GetData until result is ready (timeout after 1000 attempts).
    // S_FALSE means still pending; FAILED() means error; S_OK means done.
    for (int i = 0; i < 1000; ++i) {
      hr = query->GetData(&pixelCount, sizeof(pixelCount), D3DGETDATA_FLUSH);
      if (hr == S_OK)
        break;
      if (FAILED(hr)) {
        std::fprintf(stderr, "d3d9-gamebryo-probe: OCCLUSION GetData failed (0x%08lx)\n", hr);
        query->Release();
        return false;
      }
      // hr == S_FALSE: still pending — keep polling
    }

    query->Release();

    if (hr != S_OK) {
      std::fprintf(stderr, "d3d9-gamebryo-probe: OcclusionQuery timed out after 1000 polls (hr=0x%08lx)\n", hr);
      return false;
    }

    std::printf("d3d9-gamebryo-probe: OcclusionQuery OK (pixels=%lu)\n", pixelCount);
    return true;
  }

  bool probeEventQuery(IDirect3DDevice9* device) {
    IDirect3DQuery9* query = nullptr;
    HRESULT hr = device->CreateQuery(D3DQUERYTYPE_EVENT, &query);
    if (FAILED(hr)) {
      std::fprintf(stderr, "d3d9-gamebryo-probe: CreateQuery EVENT failed (0x%08lx)\n", hr);
      return false;
    }

    hr = query->Issue(D3DISSUE_END);
    if (FAILED(hr)) {
      std::fprintf(stderr, "d3d9-gamebryo-probe: EVENT Issue END failed (0x%08lx)\n", hr);
      query->Release();
      return false;
    }

    for (int i = 0; i < 1000; ++i) {
      hr = query->GetData(nullptr, 0, D3DGETDATA_FLUSH);
      if (hr == S_OK)
        break;
      if (FAILED(hr)) {
        std::fprintf(stderr, "d3d9-gamebryo-probe: EVENT GetData failed (0x%08lx)\n", hr);
        query->Release();
        return false;
      }
      // hr == S_FALSE: still pending — keep polling
    }

    query->Release();

    if (hr != S_OK) {
      std::fprintf(stderr, "d3d9-gamebryo-probe: EventQuery timed out after 1000 polls (hr=0x%08lx)\n", hr);
      return false;
    }

    std::printf("d3d9-gamebryo-probe: EventQuery OK\n");
    return true;
  }

  bool probeStateBlock(IDirect3DDevice9* device) {
    // Capture a state block and restore it — exercises CreateStateBlock,
    // BeginStateBlock/EndStateBlock, Apply, and Release.
    IDirect3DStateBlock9* captured = nullptr;
    HRESULT hr = device->CreateStateBlock(D3DSBT_ALL, &captured);
    if (FAILED(hr)) {
      std::fprintf(stderr, "d3d9-gamebryo-probe: CreateStateBlock failed (0x%08lx)\n", hr);
      return false;
    }

    device->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);

    hr = captured->Apply();
    if (FAILED(hr)) {
      std::fprintf(stderr, "d3d9-gamebryo-probe: StateBlock Apply failed (0x%08lx)\n", hr);
      captured->Release();
      return false;
    }
    captured->Release();

    IDirect3DStateBlock9* recorded = nullptr;
    hr = device->BeginStateBlock();
    if (FAILED(hr)) {
      std::fprintf(stderr, "d3d9-gamebryo-probe: BeginStateBlock failed (0x%08lx)\n", hr);
      return false;
    }

    device->SetRenderState(D3DRS_ZENABLE, TRUE);

    hr = device->EndStateBlock(&recorded);
    if (FAILED(hr)) {
      std::fprintf(stderr, "d3d9-gamebryo-probe: EndStateBlock failed (0x%08lx)\n", hr);
      return false;
    }

    hr = recorded->Apply();
    if (FAILED(hr)) {
      std::fprintf(stderr, "d3d9-gamebryo-probe: Recorded StateBlock Apply failed (0x%08lx)\n", hr);
      recorded->Release();
      return false;
    }

    recorded->Release();
    std::printf("d3d9-gamebryo-probe: StateBlock OK\n");
    return true;
  }

  void logMultiSampleType(IDirect3D9* d3d9, UINT adapter, D3DMULTISAMPLE_TYPE samples, const char* name) {
    DWORD quality = 0;
    const HRESULT hr = d3d9->CheckDeviceMultiSampleType(
      adapter,
      D3DDEVTYPE_HAL,
      D3DFMT_X8R8G8B8,
      TRUE,
      samples,
      &quality);

    if (SUCCEEDED(hr))
      std::printf("d3d9-gamebryo-probe: CheckDeviceMultiSampleType %s OK (quality=%lu)\n", name, quality);
    else
      std::printf("d3d9-gamebryo-probe: CheckDeviceMultiSampleType %s unavailable (0x%08lx)\n", name, hr);
  }

  bool drawFixedFunctionTriangle(IDirect3DDevice9* device) {
    struct Vertex {
      float x, y, z, rhw;
      DWORD color;
    };

    const Vertex vertices[] = {
      { 640.0f, 200.0f, 0.5f, 1.0f, D3DCOLOR_XRGB(200, 64, 64) },
      { 400.0f, 520.0f, 0.5f, 1.0f, D3DCOLOR_XRGB(64, 200, 64) },
      { 880.0f, 520.0f, 0.5f, 1.0f, D3DCOLOR_XRGB(64, 64, 200) },
    };

    device->SetRenderState(D3DRS_LIGHTING, FALSE);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRS_ZENABLE, TRUE);
    device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);

    const HRESULT drawHr = device->DrawPrimitiveUP(
      D3DPT_TRIANGLELIST,
      1,
      vertices,
      sizeof(Vertex));

    if (FAILED(drawHr)) {
      std::fprintf(stderr, "d3d9-gamebryo-probe: DrawPrimitiveUP (FF) failed (0x%08lx)\n", drawHr);
      return false;
    }

    std::printf("d3d9-gamebryo-probe: DrawPrimitiveUP fixed-function OK\n");
    return true;
  }

  // --- Vertex / index buffer + DrawPrimitive / DrawIndexedPrimitive ---

  bool probeVertexBuffers(IDirect3DDevice9* device) {
    // 3-vertex XYZ+DIFFUSE triangle for all pool variants
    struct Vert { float x, y, z; DWORD color; };
    const Vert src[] = {
      { 0.0f,  0.5f, 0.5f, D3DCOLOR_XRGB(255, 0, 0) },
      { 0.5f, -0.5f, 0.5f, D3DCOLOR_XRGB(0, 255, 0) },
      {-0.5f, -0.5f, 0.5f, D3DCOLOR_XRGB(0, 0, 255) },
    };
    constexpr DWORD kFVF = D3DFVF_XYZ | D3DFVF_DIFFUSE;
    constexpr UINT  kStride = sizeof(Vert);

    static const struct { D3DPOOL pool; DWORD usage; const char* name; } kVariants[] = {
      { D3DPOOL_MANAGED, 0,                          "MANAGED"  },
      { D3DPOOL_DEFAULT, D3DUSAGE_DYNAMIC,           "DYNAMIC"  },
    };

    device->SetRenderState(D3DRS_LIGHTING, FALSE);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRS_ZENABLE,  FALSE);

    for (const auto& v : kVariants) {
      IDirect3DVertexBuffer9* vb = nullptr;
      HRESULT hr = device->CreateVertexBuffer(
        sizeof(src), v.usage, kFVF, v.pool, &vb, nullptr);
      if (FAILED(hr)) {
        std::fprintf(stderr,
          "d3d9-gamebryo-probe: CreateVertexBuffer %s failed (0x%08lx)\n", v.name, hr);
        return false;
      }

      void* ptr = nullptr;
      const DWORD lockFlags = (v.usage & D3DUSAGE_DYNAMIC) ? D3DLOCK_DISCARD : 0;
      hr = vb->Lock(0, sizeof(src), &ptr, lockFlags);
      if (FAILED(hr)) {
        std::fprintf(stderr,
          "d3d9-gamebryo-probe: VB Lock %s failed (0x%08lx)\n", v.name, hr);
        vb->Release();
        return false;
      }
      std::memcpy(ptr, src, sizeof(src));
      vb->Unlock();

      device->SetStreamSource(0, vb, 0, kStride);
      device->SetFVF(kFVF);
      hr = device->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 1);
      vb->Release();

      if (FAILED(hr)) {
        std::fprintf(stderr,
          "d3d9-gamebryo-probe: DrawPrimitive VB:%s failed (0x%08lx)\n", v.name, hr);
        return false;
      }

      std::printf("d3d9-gamebryo-probe: VertexBuffer %s + DrawPrimitive OK\n", v.name);
    }
    return true;
  }

  bool probeIndexBuffer(IDirect3DDevice9* device) {
    struct Vert { float x, y, z; DWORD color; };
    const Vert verts[] = {
      { 0.0f,  0.5f, 0.5f, D3DCOLOR_XRGB(200, 100, 50) },
      { 0.5f, -0.5f, 0.5f, D3DCOLOR_XRGB(50, 200, 100) },
      {-0.5f, -0.5f, 0.5f, D3DCOLOR_XRGB(100, 50, 200) },
    };
    constexpr WORD indices[] = { 0, 1, 2 };

    IDirect3DVertexBuffer9* vb = nullptr;
    HRESULT hr = device->CreateVertexBuffer(
      sizeof(verts), 0, D3DFVF_XYZ | D3DFVF_DIFFUSE, D3DPOOL_MANAGED, &vb, nullptr);
    if (FAILED(hr)) {
      std::fprintf(stderr,
        "d3d9-gamebryo-probe: IB test: CreateVertexBuffer failed (0x%08lx)\n", hr);
      return false;
    }
    void* vptr = nullptr;
    hr = vb->Lock(0, sizeof(verts), &vptr, 0);
    if (FAILED(hr) || !vptr) {
      std::fprintf(stderr,
        "d3d9-gamebryo-probe: IB test: VB Lock failed (0x%08lx)\n", hr);
      vb->Release();
      return false;
    }
    std::memcpy(vptr, verts, sizeof(verts));
    vb->Unlock();

    IDirect3DIndexBuffer9* ib = nullptr;
    hr = device->CreateIndexBuffer(
      sizeof(indices), 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &ib, nullptr);
    if (FAILED(hr)) {
      std::fprintf(stderr,
        "d3d9-gamebryo-probe: CreateIndexBuffer failed (0x%08lx)\n", hr);
      vb->Release();
      return false;
    }
    void* iptr = nullptr;
    hr = ib->Lock(0, sizeof(indices), &iptr, 0);
    if (FAILED(hr) || !iptr) {
      std::fprintf(stderr,
        "d3d9-gamebryo-probe: IB Lock failed (0x%08lx)\n", hr);
      ib->Release();
      vb->Release();
      return false;
    }
    std::memcpy(iptr, indices, sizeof(indices));
    ib->Unlock();

    device->SetStreamSource(0, vb, 0, sizeof(Vert));
    device->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    device->SetIndices(ib);
    hr = device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 3, 0, 1);

    vb->Release();
    ib->Release();

    if (FAILED(hr)) {
      std::fprintf(stderr,
        "d3d9-gamebryo-probe: DrawIndexedPrimitive failed (0x%08lx)\n", hr);
      return false;
    }

    std::printf("d3d9-gamebryo-probe: IndexBuffer (16-bit) + DrawIndexedPrimitive OK\n");
    return true;
  }

  // --- Texture creation, lock/upload, SetTexture ---

  bool probeTexture(IDirect3DDevice9* device) {
    // Create an A8R8G8B8 MANAGED texture, fill it, bind it.
    IDirect3DTexture9* tex = nullptr;
    HRESULT hr = device->CreateTexture(
      64, 64, 0 /*all mips*/, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, nullptr);
    if (FAILED(hr)) {
      std::fprintf(stderr,
        "d3d9-gamebryo-probe: CreateTexture A8R8G8B8 failed (0x%08lx)\n", hr);
      return false;
    }

    D3DLOCKED_RECT lr = {};
    hr = tex->LockRect(0, &lr, nullptr, 0);
    if (FAILED(hr)) {
      std::fprintf(stderr,
        "d3d9-gamebryo-probe: Texture LockRect failed (0x%08lx)\n", hr);
      tex->Release();
      return false;
    }
    // Fill mip0 with a solid green
    for (int y = 0; y < 64; ++y) {
      DWORD* row = reinterpret_cast<DWORD*>(static_cast<uint8_t*>(lr.pBits) + y * lr.Pitch);
      for (int x = 0; x < 64; ++x)
        row[x] = D3DCOLOR_ARGB(255, 0, 200, 0);
    }
    tex->UnlockRect(0);

    device->SetTexture(0, tex);
    device->SetTextureStageState(0, D3DTSS_COLOROP,  D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    device->SetTextureStageState(0, D3DTSS_ALPHAOP,  D3DTOP_DISABLE);
    device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
    device->SetSamplerState(0, D3DSAMP_ADDRESSU,  D3DTADDRESS_WRAP);
    device->SetSamplerState(0, D3DSAMP_ADDRESSV,  D3DTADDRESS_WRAP);
    device->SetSamplerState(0, D3DSAMP_MAXANISOTROPY, 8);

    struct TexVert { float x, y, z, rhw; float u, v; };
    const TexVert quad[] = {
      {100.f, 100.f, 0.5f, 1.f, 0.f, 0.f},
      {300.f, 100.f, 0.5f, 1.f, 1.f, 0.f},
      {100.f, 300.f, 0.5f, 1.f, 0.f, 1.f},
      {300.f, 300.f, 0.5f, 1.f, 1.f, 1.f},
    };
    device->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
    hr = device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(TexVert));

    device->SetTexture(0, nullptr);
    tex->Release();

    if (FAILED(hr)) {
      std::fprintf(stderr,
        "d3d9-gamebryo-probe: Texture draw failed (0x%08lx)\n", hr);
      return false;
    }

    std::printf("d3d9-gamebryo-probe: Texture A8R8G8B8 (mips, lock/upload, sampler) OK\n");
    return true;
  }

  bool probeDxtTexture(IDirect3DDevice9* device) {
    // DXT1 compressed textures can be created but not LockRect'd on most implementations.
    // Just verify creation succeeds — this proves CheckDeviceFormat result matches reality.
    IDirect3DTexture9* tex = nullptr;
    const HRESULT hr = device->CreateTexture(
      64, 64, 1, 0, D3DFMT_DXT1, D3DPOOL_MANAGED, &tex, nullptr);
    if (FAILED(hr)) {
      std::fprintf(stderr,
        "d3d9-gamebryo-probe: CreateTexture DXT1 failed (0x%08lx)\n", hr);
      return false;
    }
    tex->Release();
    std::printf("d3d9-gamebryo-probe: CreateTexture DXT1 OK\n");
    return true;
  }

  // --- Render target + GetRenderTargetData readback ---

  bool probeRenderTarget(IDirect3DDevice9* device) {
    IDirect3DSurface9* origRT = nullptr;
    IDirect3DSurface9* origDS = nullptr;
    device->GetRenderTarget(0, &origRT);
    device->GetDepthStencilSurface(&origDS);

    IDirect3DTexture9* rtTex = nullptr;
    HRESULT hr = device->CreateTexture(
      128, 128, 1,
      D3DUSAGE_RENDERTARGET,
      D3DFMT_A8R8G8B8,
      D3DPOOL_DEFAULT,
      &rtTex, nullptr);
    if (FAILED(hr)) {
      std::fprintf(stderr,
        "d3d9-gamebryo-probe: CreateTexture RT failed (0x%08lx)\n", hr);
      if (origRT) origRT->Release();
      return false;
    }

    IDirect3DSurface9* rtSurf = nullptr;
    hr = rtTex->GetSurfaceLevel(0, &rtSurf);
    if (FAILED(hr)) {
      rtTex->Release();
      if (origRT) origRT->Release();
      return false;
    }

    hr = device->SetRenderTarget(0, rtSurf);
    if (FAILED(hr)) {
      std::fprintf(stderr,
        "d3d9-gamebryo-probe: SetRenderTarget failed (0x%08lx)\n", hr);
      rtSurf->Release();
      rtTex->Release();
      if (origRT) origRT->Release();
      return false;
    }

    device->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 128, 255), 1.0f, 0);

    // Read back into a system-mem surface
    IDirect3DSurface9* readback = nullptr;
    hr = device->CreateOffscreenPlainSurface(
      128, 128, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &readback, nullptr);
    if (SUCCEEDED(hr)) {
      hr = device->GetRenderTargetData(rtSurf, readback);
      if (SUCCEEDED(hr))
        std::printf("d3d9-gamebryo-probe: GetRenderTargetData OK\n");
      else
        std::printf("d3d9-gamebryo-probe: GetRenderTargetData unavailable (0x%08lx)\n", hr);
      readback->Release();
    }

    // Restore original RT and depth-stencil surface.
    // SetRenderTarget(0, ...) implicitly detaches the depth-stencil on some
    // implementations; restore it explicitly so subsequent tests see correct state.
    if (origRT) {
      device->SetRenderTarget(0, origRT);
      origRT->Release();
    }
    device->SetDepthStencilSurface(origDS);
    if (origDS)
      origDS->Release();
    rtSurf->Release();
    rtTex->Release();

    std::printf("d3d9-gamebryo-probe: RenderTarget A8R8G8B8 OK\n");
    return true;
  }

  // --- Viewport, scissor, blend, alpha test, fog ---

  bool probeRenderStates(IDirect3DDevice9* device) {
    // Viewport
    D3DVIEWPORT9 vp = { 0, 0, 1280, 720, 0.0f, 1.0f };
    HRESULT hr = device->SetViewport(&vp);
    if (FAILED(hr)) {
      std::fprintf(stderr,
        "d3d9-gamebryo-probe: SetViewport failed (0x%08lx)\n", hr);
      return false;
    }

    // Scissor rect
    RECT scissor = { 10, 10, 1270, 710 };
    hr = device->SetScissorRect(&scissor);
    if (FAILED(hr)) {
      std::fprintf(stderr,
        "d3d9-gamebryo-probe: SetScissorRect failed (0x%08lx)\n", hr);
      return false;
    }
    device->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
    device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);

    // Alpha blend
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    device->SetRenderState(D3DRS_SRCBLEND,  D3DBLEND_SRCALPHA);
    device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    device->SetRenderState(D3DRS_BLENDOP,   D3DBLENDOP_ADD);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);

    // Alpha test
    device->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
    device->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);
    device->SetRenderState(D3DRS_ALPHAREF,  128);
    device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);

    // Stencil — set up and clear without drawing
    device->SetRenderState(D3DRS_STENCILENABLE,   TRUE);
    device->SetRenderState(D3DRS_STENCILFUNC,     D3DCMP_ALWAYS);
    device->SetRenderState(D3DRS_STENCILPASS,     D3DSTENCILOP_REPLACE);
    device->SetRenderState(D3DRS_STENCILFAIL,     D3DSTENCILOP_KEEP);
    device->SetRenderState(D3DRS_STENCILZFAIL,    D3DSTENCILOP_KEEP);
    device->SetRenderState(D3DRS_STENCILREF,      1);
    device->SetRenderState(D3DRS_STENCILMASK,     0xFF);
    device->SetRenderState(D3DRS_STENCILWRITEMASK, 0xFF);
    device->SetRenderState(D3DRS_STENCILENABLE,   FALSE);

    // Fog (vertex + table)
    device->SetRenderState(D3DRS_FOGENABLE,      TRUE);
    device->SetRenderState(D3DRS_FOGCOLOR,       D3DCOLOR_XRGB(128, 128, 128));
    device->SetRenderState(D3DRS_FOGVERTEXMODE,  D3DFOG_LINEAR);
    device->SetRenderState(D3DRS_FOGTABLEMODE,   D3DFOG_NONE);
    const float fogStart = 10.0f, fogEnd = 500.0f;
    device->SetRenderState(D3DRS_FOGSTART, *reinterpret_cast<const DWORD*>(&fogStart));
    device->SetRenderState(D3DRS_FOGEND,   *reinterpret_cast<const DWORD*>(&fogEnd));
    device->SetRenderState(D3DRS_FOGENABLE, FALSE);

    std::printf("d3d9-gamebryo-probe: RenderStates (viewport, scissor, blend, alpha, stencil, fog) OK\n");
    return true;
  }

  // --- Vertex declaration (D3DVERTEXELEMENT9) ---

  bool probeVertexDeclaration(IDirect3DDevice9* device) {
    // XYZ + NORMAL + TEXCOORD0 — common Gamebryo layout
    const D3DVERTEXELEMENT9 elements[] = {
      { 0, 0,  D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
      { 0, 12, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL,   0 },
      { 0, 24, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
      D3DDECL_END()
    };

    IDirect3DVertexDeclaration9* decl = nullptr;
    HRESULT hr = device->CreateVertexDeclaration(elements, &decl);
    if (FAILED(hr)) {
      std::fprintf(stderr,
        "d3d9-gamebryo-probe: CreateVertexDeclaration failed (0x%08lx)\n", hr);
      return false;
    }

    hr = device->SetVertexDeclaration(decl);
    if (FAILED(hr)) {
      std::fprintf(stderr,
        "d3d9-gamebryo-probe: SetVertexDeclaration failed (0x%08lx)\n", hr);
      decl->Release();
      return false;
    }

    // Restore FVF-based state to avoid breaking later tests
    device->SetVertexDeclaration(nullptr);
    device->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);

    decl->Release();
    std::printf("d3d9-gamebryo-probe: VertexDeclaration (XYZ+NORMAL+TEX0) OK\n");
    return true;
  }

  // --- Multiple render targets ---

  bool probeMultipleRenderTargets(IDirect3DDevice9* device) {
    D3DCAPS9 caps = {};
    device->GetDeviceCaps(&caps);

    if (caps.NumSimultaneousRTs < 2) {
      std::printf("d3d9-gamebryo-probe: MRT unavailable (NumSimultaneousRTs=%lu)\n",
        caps.NumSimultaneousRTs);
      return true; // non-fatal
    }

    IDirect3DSurface9* origRT0 = nullptr;
    IDirect3DSurface9* origRT1 = nullptr;
    device->GetRenderTarget(0, &origRT0);
    // RT1 may be null; that's fine — we only save/restore what exists
    device->GetRenderTarget(1, &origRT1);

    IDirect3DTexture9* rt0Tex = nullptr;
    IDirect3DTexture9* rt1Tex = nullptr;
    HRESULT hr;

    hr = device->CreateTexture(128, 128, 1, D3DUSAGE_RENDERTARGET,
      D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &rt0Tex, nullptr);
    if (FAILED(hr)) {
      std::fprintf(stderr,
        "d3d9-gamebryo-probe: MRT CreateTexture RT0 failed (0x%08lx)\n", hr);
      if (origRT0) origRT0->Release();
      if (origRT1) origRT1->Release();
      return false;
    }

    hr = device->CreateTexture(128, 128, 1, D3DUSAGE_RENDERTARGET,
      D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &rt1Tex, nullptr);
    if (FAILED(hr)) {
      std::fprintf(stderr,
        "d3d9-gamebryo-probe: MRT CreateTexture RT1 failed (0x%08lx)\n", hr);
      rt0Tex->Release();
      if (origRT0) origRT0->Release();
      if (origRT1) origRT1->Release();
      return false;
    }

    IDirect3DSurface9* rt0Surf = nullptr;
    IDirect3DSurface9* rt1Surf = nullptr;
    rt0Tex->GetSurfaceLevel(0, &rt0Surf);
    rt1Tex->GetSurfaceLevel(0, &rt1Surf);

    device->SetRenderTarget(0, rt0Surf);
    device->SetRenderTarget(1, rt1Surf);
    device->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);

    // Restore
    device->SetRenderTarget(0, origRT0);
    device->SetRenderTarget(1, origRT1 ? origRT1 : nullptr);

    rt1Surf->Release();
    rt0Surf->Release();
    rt1Tex->Release();
    rt0Tex->Release();
    if (origRT0) origRT0->Release();
    if (origRT1) origRT1->Release();

    std::printf("d3d9-gamebryo-probe: MRT (2× A8R8G8B8) OK\n");
    return true;
  }

  // --- Cube map ---

  bool probeCubeTexture(IDirect3DDevice9* device) {
    IDirect3DCubeTexture9* cube = nullptr;
    HRESULT hr = device->CreateCubeTexture(
      32, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &cube, nullptr);
    if (FAILED(hr)) {
      std::fprintf(stderr,
        "d3d9-gamebryo-probe: CreateCubeTexture failed (0x%08lx)\n", hr);
      return false;
    }

    // Fill face 0 (D3DCUBEMAP_FACE_POSITIVE_X)
    D3DLOCKED_RECT lr = {};
    hr = cube->LockRect(D3DCUBEMAP_FACE_POSITIVE_X, 0, &lr, nullptr, 0);
    if (FAILED(hr)) {
      std::fprintf(stderr,
        "d3d9-gamebryo-probe: CubeTexture LockRect failed (0x%08lx)\n", hr);
      cube->Release();
      return false;
    }
    for (int y = 0; y < 32; ++y) {
      DWORD* row = reinterpret_cast<DWORD*>(
        static_cast<uint8_t*>(lr.pBits) + y * lr.Pitch);
      for (int x = 0; x < 32; ++x)
        row[x] = D3DCOLOR_ARGB(255, 100, 150, 255);
    }
    cube->UnlockRect(D3DCUBEMAP_FACE_POSITIVE_X, 0);

    device->SetTexture(0, cube);
    device->SetTexture(0, nullptr);
    cube->Release();

    std::printf("d3d9-gamebryo-probe: CubeTexture A8R8G8B8 OK\n");
    return true;
  }

  // --- Volume texture ---

  bool probeVolumeTexture(IDirect3DDevice9* device) {
    IDirect3DVolumeTexture9* vol = nullptr;
    HRESULT hr = device->CreateVolumeTexture(
      16, 16, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &vol, nullptr);
    if (FAILED(hr)) {
      // Volume textures are optional in Gamebryo; log and continue
      std::printf(
        "d3d9-gamebryo-probe: CreateVolumeTexture unavailable (0x%08lx)\n", hr);
      return true;
    }

    D3DLOCKED_BOX lb = {};
    hr = vol->LockBox(0, &lb, nullptr, 0);
    if (SUCCEEDED(hr)) {
      const int slicePitch = lb.SlicePitch;
      for (int z = 0; z < 4; ++z) {
        for (int y = 0; y < 16; ++y) {
          DWORD* row = reinterpret_cast<DWORD*>(
            static_cast<uint8_t*>(lb.pBits) + z * slicePitch + y * lb.RowPitch);
          for (int x = 0; x < 16; ++x)
            row[x] = D3DCOLOR_ARGB(255, uint8_t(x * 16), uint8_t(y * 16), uint8_t(z * 64));
        }
      }
      vol->UnlockBox(0);
    }
    vol->Release();

    std::printf("d3d9-gamebryo-probe: VolumeTexture A8R8G8B8 (16×16×4) OK\n");
    return true;
  }

  // --- GetFrontBufferData ---

  void logFrontBufferData(IDirect3DDevice9* device, uint32_t width, uint32_t height) {
    IDirect3DSurface9* surf = nullptr;
    HRESULT hr = device->CreateOffscreenPlainSurface(
      width, height, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &surf, nullptr);
    if (FAILED(hr)) {
      std::printf(
        "d3d9-gamebryo-probe: GetFrontBufferData: CreateOffscreenPlainSurface failed (0x%08lx)\n", hr);
      return;
    }
    hr = device->GetFrontBufferData(0, surf);
    surf->Release();
    if (SUCCEEDED(hr))
      std::printf("d3d9-gamebryo-probe: GetFrontBufferData OK\n");
    else
      std::printf("d3d9-gamebryo-probe: GetFrontBufferData unavailable (0x%08lx)\n", hr);
  }

  // --- Timestamp query ---

  void logTimestampQuery(IDirect3DDevice9* device) {
    IDirect3DQuery9* tsQuery = nullptr;
    HRESULT hr = device->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &tsQuery);
    if (FAILED(hr)) {
      std::printf(
        "d3d9-gamebryo-probe: D3DQUERYTYPE_TIMESTAMP unavailable (0x%08lx)\n", hr);
      return;
    }

    IDirect3DQuery9* freqQuery = nullptr;
    device->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ, &freqQuery);

    tsQuery->Issue(D3DISSUE_END);
    UINT64 ts = 0;
    for (int i = 0; i < 1000; ++i) {
      hr = tsQuery->GetData(&ts, sizeof(ts), D3DGETDATA_FLUSH);
      if (hr == S_OK) break;
    }
    tsQuery->Release();
    if (freqQuery) freqQuery->Release();

    if (hr == S_OK)
      std::printf("d3d9-gamebryo-probe: TimestampQuery OK\n");
    else
      std::printf("d3d9-gamebryo-probe: TimestampQuery stalled (0x%08lx)\n", hr);
  }

} // namespace

int main(int argc, char** argv) {
  const int frameCount = parseFrameCount(argc, argv);

  configureWsiDriver();

#if defined(D3D9_CLEAR_SDL3)
  if (!SDL_Init(SDL_INIT_VIDEO)) {
#else
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
#endif
    std::fprintf(stderr, "d3d9-gamebryo-probe: SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }

#if defined(__APPLE__)
  if (!loadVulkanPortabilityLibrary()) {
    std::fprintf(stderr, "d3d9-gamebryo-probe: failed to load Vulkan library: %s\n", SDL_GetError());
    SDL_Quit();
    return 1;
  }
#endif

  // Fallout 3 commonly runs at 1280×720 or 1920×1200; use a smaller probe size.
  constexpr uint32_t kWidth  = 1280;
  constexpr uint32_t kHeight = 720;

#if defined(D3D9_CLEAR_SDL3)
  SDL_Window* window = SDL_CreateWindow(
    "SpockD3D9 gamebryo-probe",
    int(kWidth),
    int(kHeight),
    SDL_WINDOW_VULKAN);
#else
  SDL_Window* window = SDL_CreateWindow(
    "SpockD3D9 gamebryo-probe",
    SDL_WINDOWPOS_CENTERED,
    SDL_WINDOWPOS_CENTERED,
    kWidth,
    kHeight,
    SDL_WINDOW_SHOWN | SDL_WINDOW_VULKAN);
#endif

  if (!window) {
    std::fprintf(stderr, "d3d9-gamebryo-probe: SDL_CreateWindow failed: %s\n", SDL_GetError());
    SDL_Quit();
    return 1;
  }

  IDirect3D9* d3d9 = Direct3DCreate9(D3D_SDK_VERSION);
  if (!d3d9) {
    std::fprintf(stderr, "d3d9-gamebryo-probe: Direct3DCreate9 failed\n");
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
  std::printf("d3d9-gamebryo-probe: Direct3DCreate9 OK\n");

  const UINT adapter = D3DADAPTER_DEFAULT;

  D3DADAPTER_IDENTIFIER9 adapterId = { };
  if (FAILED(d3d9->GetAdapterIdentifier(adapter, 0, &adapterId))) {
    std::fprintf(stderr, "d3d9-gamebryo-probe: GetAdapterIdentifier failed\n");
    d3d9->Release();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
  std::printf("d3d9-gamebryo-probe: GetAdapterIdentifier OK (%s)\n", adapterId.Description);

  const UINT modeCount = d3d9->GetAdapterModeCount(adapter, D3DFMT_X8R8G8B8);
  if (modeCount == 0) {
    std::fprintf(stderr, "d3d9-gamebryo-probe: EnumAdapterModes returned no modes\n");
    d3d9->Release();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
  std::printf("d3d9-gamebryo-probe: EnumAdapterModes OK (%u modes)\n", modeCount);

  D3DDISPLAYMODE displayMode = { };
  if (FAILED(d3d9->GetAdapterDisplayMode(adapter, &displayMode))) {
    std::fprintf(stderr, "d3d9-gamebryo-probe: GetAdapterDisplayMode failed\n");
    d3d9->Release();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
  std::printf("d3d9-gamebryo-probe: GetAdapterDisplayMode OK (%ux%u @ %u Hz)\n",
    displayMode.Width, displayMode.Height, displayMode.RefreshRate);

  logMultiSampleType(d3d9, adapter, D3DMULTISAMPLE_2_SAMPLES, "2x MSAA");
  logMultiSampleType(d3d9, adapter, D3DMULTISAMPLE_4_SAMPLES, "4x MSAA");

  if (!checkFormat(d3d9, adapter, D3DFMT_DXT1, "DXT1")
   || !checkFormat(d3d9, adapter, D3DFMT_DXT3, "DXT3")
   || !checkFormat(d3d9, adapter, D3DFMT_DXT5, "DXT5")
   || !checkFormat(d3d9, adapter, D3DFMT_A8R8G8B8, "A8R8G8B8")
   || !checkFormat(d3d9, adapter, D3DFMT_R5G6B5, "R5G6B5")
   || !checkFormat(d3d9, adapter, D3DFMT_A1R5G5B5, "A1R5G5B5")
   || !checkFormat(d3d9, adapter, D3DFMT_L8, "L8")
   || !checkFormat(d3d9, adapter, D3DFMT_A8L8, "A8L8")
   || !checkDepthFormat(d3d9, adapter, D3DFMT_D24S8, "D24S8")
   || !checkDepthFormat(d3d9, adapter, D3DFMT_D16, "D16")) {
    d3d9->Release();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  // Render target format support — logged but non-fatal (MoltenVK may lack R16F/R32F on some hardware)
  logRenderTargetFormat(d3d9, adapter, D3DFMT_A8R8G8B8, "A8R8G8B8");
  logRenderTargetFormat(d3d9, adapter, D3DFMT_X8R8G8B8, "X8R8G8B8");
  logRenderTargetFormat(d3d9, adapter, D3DFMT_R16F,     "R16F");
  logRenderTargetFormat(d3d9, adapter, D3DFMT_R32F,     "R32F");
  logRenderTargetFormat(d3d9, adapter, D3DFMT_A16B16G16R16F, "A16B16G16R16F");
  logRenderTargetFormat(d3d9, adapter, D3DFMT_A32B32G32R32F, "A32B32G32R32F");

  HWND hwnd = reinterpret_cast<HWND>(window);

  D3DPRESENT_PARAMETERS presentParams = { };
  presentParams.Windowed                    = TRUE;
  presentParams.SwapEffect                  = D3DSWAPEFFECT_DISCARD;
  presentParams.BackBufferCount             = 2;
  presentParams.BackBufferFormat            = D3DFMT_X8R8G8B8;
  presentParams.BackBufferWidth             = kWidth;
  presentParams.BackBufferHeight            = kHeight;
  presentParams.hDeviceWindow               = hwnd;
  presentParams.EnableAutoDepthStencil      = TRUE;
  presentParams.AutoDepthStencilFormat      = D3DFMT_D24S8;
  presentParams.PresentationInterval        = D3DPRESENT_INTERVAL_ONE;
  presentParams.FullScreen_RefreshRateInHz  = 60;

  IDirect3DDevice9* device = nullptr;
  const HRESULT createHr = d3d9->CreateDevice(
    adapter,
    D3DDEVTYPE_HAL,
    hwnd,
    D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED | D3DCREATE_FPU_PRESERVE,
    &presentParams,
    &device);

  if (FAILED(createHr) || !device) {
    std::fprintf(stderr, "d3d9-gamebryo-probe: CreateDevice failed (0x%08lx)\n", createHr);
    d3d9->Release();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
  std::printf("d3d9-gamebryo-probe: CreateDevice OK\n");

  D3DCAPS9 caps = { };
  if (FAILED(device->GetDeviceCaps(&caps))) {
    std::fprintf(stderr, "d3d9-gamebryo-probe: GetDeviceCaps failed\n");
    device->Release();
    d3d9->Release();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  if (caps.PixelShaderVersion < D3DPS_VERSION(3, 0)
   || caps.VertexShaderVersion < D3DVS_VERSION(3, 0)) {
    std::fprintf(stderr, "d3d9-gamebryo-probe: SM3 caps missing (PS=0x%x VS=0x%x)\n",
      caps.PixelShaderVersion, caps.VertexShaderVersion);
    device->Release();
    d3d9->Release();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
  std::printf("d3d9-gamebryo-probe: GetDeviceCaps SM3 OK\n");

  const HRESULT coopHr = device->TestCooperativeLevel();
  if (coopHr != D3D_OK) {
    std::fprintf(stderr, "d3d9-gamebryo-probe: TestCooperativeLevel failed (0x%08lx)\n", coopHr);
    device->Release();
    d3d9->Release();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
  std::printf("d3d9-gamebryo-probe: TestCooperativeLevel OK\n");

  if (!probeStateBlock(device)) {
    device->Release();
    d3d9->Release();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  if (!drawFixedFunctionTriangle(device)) {
    device->Release();
    d3d9->Release();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  // Issue event query before present loop so it's resolved during the loop.
  if (!probeEventQuery(device)) {
    device->Release();
    d3d9->Release();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  if (!probeRenderStates(device)) {
    device->Release();
    d3d9->Release();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  if (!probeVertexBuffers(device)) {
    device->Release();
    d3d9->Release();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  if (!probeIndexBuffer(device)) {
    device->Release();
    d3d9->Release();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  if (!probeTexture(device)) {
    device->Release();
    d3d9->Release();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  if (!probeDxtTexture(device)) {
    device->Release();
    d3d9->Release();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  if (!probeRenderTarget(device)) {
    device->Release();
    d3d9->Release();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  if (!probeVertexDeclaration(device)) {
    device->Release();
    d3d9->Release();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  if (!probeMultipleRenderTargets(device)) {
    device->Release();
    d3d9->Release();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  if (!probeCubeTexture(device)) {
    device->Release();
    d3d9->Release();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  if (!probeVolumeTexture(device)) {
    device->Release();
    d3d9->Release();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  // Non-fatal probes (may not work in headless CI or on all drivers)
  logFrontBufferData(device, kWidth, kHeight);
  logTimestampQuery(device);

  std::printf("d3d9-gamebryo-probe: presenting %d frame(s)\n", frameCount);

  for (int frame = 0; frame < frameCount; ++frame) {
    SDL_Event event = { };
    while (SDL_PollEvent(&event)) {
#if defined(D3D9_CLEAR_SDL3)
      if (event.type == SDL_EVENT_QUIT)
#else
      if (event.type == SDL_QUIT)
#endif
        break;
    }

    device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
      D3DCOLOR_XRGB(48, 96, 48), 1.0f, 0);

    if (!drawFixedFunctionTriangle(device)) {
      device->Release();
      d3d9->Release();
      SDL_DestroyWindow(window);
      SDL_Quit();
      return 1;
    }

    // Run occlusion query probe on the first frame to exercise GPU readback.
    if (frame == 0 && !probeOcclusionQuery(device)) {
      device->Release();
      d3d9->Release();
      SDL_DestroyWindow(window);
      SDL_Quit();
      return 1;
    }

    const HRESULT presentHr = device->Present(nullptr, nullptr, nullptr, nullptr);
    if (FAILED(presentHr)) {
      std::fprintf(stderr, "d3d9-gamebryo-probe: Present failed (0x%08lx)\n", presentHr);
      device->Release();
      d3d9->Release();
      SDL_DestroyWindow(window);
      SDL_Quit();
      return 1;
    }
  }

  presentParams.BackBufferWidth  = 1024;
  presentParams.BackBufferHeight = 768;
  const HRESULT resetHr = device->Reset(&presentParams);
  if (FAILED(resetHr)) {
    std::fprintf(stderr, "d3d9-gamebryo-probe: Reset failed (0x%08lx)\n", resetHr);
    device->Release();
    d3d9->Release();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
  std::printf("d3d9-gamebryo-probe: Reset OK\n");

  device->Release();
  d3d9->Release();
  SDL_DestroyWindow(window);
#if defined(__APPLE__)
  SDL_Vulkan_UnloadLibrary();
#endif
  SDL_Quit();

  std::printf("d3d9-gamebryo-probe: OK\n");
  return 0;
}
