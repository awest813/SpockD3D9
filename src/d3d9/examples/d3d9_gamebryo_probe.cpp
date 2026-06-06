/**
 * Gamebryo / Fallout 3-style D3D9 device creation probe (Milestone F).
 *
 * Exercises the native SpockD3D9 path (Track A / MoltenVK) with presentation
 * parameters, format queries, MSAA checks, display mode enumeration, SM3 caps,
 * a fixed-function DrawPrimitiveUP (GLSL → SPIR-V → MSL), Present, and Reset.
 * Intended for CI on macOS alongside d3d9-clear; does not require the game binary.
 *
 * Exit 0 when the probe prints "d3d9-gamebryo-probe: OK".
 */

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

  // Poll a query to completion, driving GPU progress with Present between
  // attempts. Returns the final HRESULT (D3D_OK on success, S_FALSE if it
  // never resolved within the bounded retry budget).
  HRESULT waitForQueryData(IDirect3DDevice9* device, IDirect3DQuery9* query, void* data, DWORD size) {
    HRESULT hr = S_FALSE;
    for (int attempt = 0; attempt < 256 && hr == S_FALSE; ++attempt) {
      hr = query->GetData(data, size, D3DGETDATA_FLUSH);
      if (hr == S_FALSE)
        device->Present(nullptr, nullptr, nullptr, nullptr);
    }
    return hr;
  }

  // DrawIndexedPrimitive from D3DPOOL_DEFAULT vertex/index buffers populated
  // with Lock/Unlock(DISCARD) — the buffer upload + indexed draw path Gamebryo
  // uses for nearly all geometry. Buffers are released before returning so the
  // device stays resettable.
  bool drawIndexedFromBuffers(IDirect3DDevice9* device) {
    struct Vertex {
      float x, y, z, rhw;
      DWORD color;
    };

    const Vertex vertices[] = {
      { 400.0f, 200.0f, 0.5f, 1.0f, D3DCOLOR_XRGB(200, 200, 64) },
      { 880.0f, 200.0f, 0.5f, 1.0f, D3DCOLOR_XRGB(64, 200, 200) },
      { 880.0f, 520.0f, 0.5f, 1.0f, D3DCOLOR_XRGB(200, 64, 200) },
      { 400.0f, 520.0f, 0.5f, 1.0f, D3DCOLOR_XRGB(200, 200, 200) },
    };
    const uint16_t indices[] = { 0, 1, 2, 0, 2, 3 };

    IDirect3DVertexBuffer9* vbo = nullptr;
    if (FAILED(device->CreateVertexBuffer(sizeof(vertices),
        D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, D3DFVF_XYZRHW | D3DFVF_DIFFUSE,
        D3DPOOL_DEFAULT, &vbo, nullptr)) || !vbo) {
      std::fprintf(stderr, "d3d9-gamebryo-probe: CreateVertexBuffer (indexed draw) failed\n");
      return false;
    }

    IDirect3DIndexBuffer9* ibo = nullptr;
    if (FAILED(device->CreateIndexBuffer(sizeof(indices),
        D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, D3DFMT_INDEX16,
        D3DPOOL_DEFAULT, &ibo, nullptr)) || !ibo) {
      std::fprintf(stderr, "d3d9-gamebryo-probe: CreateIndexBuffer failed\n");
      vbo->Release();
      return false;
    }

    bool ok = false;
    void* mapped = nullptr;
    if (SUCCEEDED(vbo->Lock(0, sizeof(vertices), &mapped, D3DLOCK_DISCARD)) && mapped) {
      std::memcpy(mapped, vertices, sizeof(vertices));
      vbo->Unlock();

      mapped = nullptr;
      if (SUCCEEDED(ibo->Lock(0, sizeof(indices), &mapped, D3DLOCK_DISCARD)) && mapped) {
        std::memcpy(mapped, indices, sizeof(indices));
        ibo->Unlock();

        device->SetRenderState(D3DRS_LIGHTING, FALSE);
        device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
        device->SetStreamSource(0, vbo, 0, sizeof(Vertex));
        device->SetIndices(ibo);

        const HRESULT drawHr = device->DrawIndexedPrimitive(
          D3DPT_TRIANGLELIST, 0, 0, 4, 0, 2);

        if (SUCCEEDED(drawHr))
          ok = true;
        else
          std::fprintf(stderr, "d3d9-gamebryo-probe: DrawIndexedPrimitive failed (0x%08lx)\n", drawHr);

        device->SetIndices(nullptr);
        device->SetStreamSource(0, nullptr, 0, 0);
      }
    }

    ibo->Release();
    vbo->Release();

    if (ok)
      std::printf("d3d9-gamebryo-probe: DrawIndexedPrimitive (DEFAULT VB/IB, Lock DISCARD) OK\n");
    return ok;
  }

  // Occlusion query around a draw — Gamebryo uses these for visibility tests.
  bool runOcclusionQuery(IDirect3DDevice9* device) {
    IDirect3DQuery9* query = nullptr;
    const HRESULT createHr = device->CreateQuery(D3DQUERYTYPE_OCCLUSION, &query);
    if (FAILED(createHr) || !query) {
      std::fprintf(stderr, "d3d9-gamebryo-probe: CreateQuery(OCCLUSION) failed (0x%08lx)\n", createHr);
      return false;
    }

    query->Issue(D3DISSUE_BEGIN);
    const bool drew = drawFixedFunctionTriangle(device);
    query->Issue(D3DISSUE_END);

    DWORD samples = 0;
    const HRESULT dataHr = waitForQueryData(device, query, &samples, sizeof(samples));
    query->Release();

    if (!drew)
      return false;

    if (dataHr != D3D_OK) {
      std::fprintf(stderr, "d3d9-gamebryo-probe: occlusion query did not resolve (0x%08lx)\n", dataHr);
      return false;
    }

    std::printf("d3d9-gamebryo-probe: occlusion query OK (%lu samples)\n", static_cast<unsigned long>(samples));
    return true;
  }

  // Event (fence) query — Gamebryo uses these for CPU/GPU sync.
  bool runEventQuery(IDirect3DDevice9* device) {
    IDirect3DQuery9* query = nullptr;
    const HRESULT createHr = device->CreateQuery(D3DQUERYTYPE_EVENT, &query);
    if (FAILED(createHr) || !query) {
      std::fprintf(stderr, "d3d9-gamebryo-probe: CreateQuery(EVENT) failed (0x%08lx)\n", createHr);
      return false;
    }

    query->Issue(D3DISSUE_END);

    BOOL signaled = FALSE;
    const HRESULT dataHr = waitForQueryData(device, query, &signaled, sizeof(signaled));
    query->Release();

    if (dataHr != D3D_OK || !signaled) {
      std::fprintf(stderr, "d3d9-gamebryo-probe: event query did not signal (0x%08lx)\n", dataHr);
      return false;
    }

    std::printf("d3d9-gamebryo-probe: event query OK\n");
    return true;
  }

  // Render state block capture/apply — Gamebryo uses state blocks to snapshot
  // and restore render state. Create a D3DSBT_ALL block (captures current
  // state), mutate the state, then Apply and confirm the captured value is
  // restored. The block is released before returning so the device stays
  // resettable (state blocks are losable resources).
  bool runStateBlockCheck(IDirect3DDevice9* device) {
    device->SetRenderState(D3DRS_FILLMODE, D3DFILL_WIREFRAME);

    IDirect3DStateBlock9* block = nullptr;
    const HRESULT createHr = device->CreateStateBlock(D3DSBT_ALL, &block);
    if (FAILED(createHr) || !block) {
      std::fprintf(stderr, "d3d9-gamebryo-probe: CreateStateBlock(D3DSBT_ALL) failed (0x%08lx)\n", createHr);
      return false;
    }

    device->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);

    const HRESULT applyHr = block->Apply();
    block->Release();

    if (FAILED(applyHr)) {
      std::fprintf(stderr, "d3d9-gamebryo-probe: state block Apply failed (0x%08lx)\n", applyHr);
      return false;
    }

    DWORD fillMode = 0;
    if (FAILED(device->GetRenderState(D3DRS_FILLMODE, &fillMode))) {
      std::fprintf(stderr, "d3d9-gamebryo-probe: GetRenderState(FILLMODE) failed\n");
      return false;
    }

    if (fillMode != D3DFILL_WIREFRAME) {
      std::fprintf(stderr, "d3d9-gamebryo-probe: state block did not restore FILLMODE (got %lu)\n",
        static_cast<unsigned long>(fillMode));
      return false;
    }

    // Leave the device in the default fill mode for subsequent draws.
    device->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    std::printf("d3d9-gamebryo-probe: state block capture/apply OK\n");
    return true;
  }

  // Validate the device-lost / reset cycle Gamebryo drives on focus loss and
  // resolution changes. A live D3DPOOL_DEFAULT resource must block Reset, which
  // then reports D3DERR_DEVICENOTRESET through TestCooperativeLevel until the
  // resource is released and Reset is retried. This mirrors the
  // TestCooperativeLevel + Reset loop Fallout 3 runs when it regains focus.
  bool exerciseResetCycle(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS presentParams) {
    IDirect3DVertexBuffer9* defaultVbo = nullptr;
    const HRESULT createHr = device->CreateVertexBuffer(
      6 * sizeof(float) * 3,
      D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY,
      D3DFVF_XYZRHW | D3DFVF_DIFFUSE,
      D3DPOOL_DEFAULT,
      &defaultVbo,
      nullptr);

    if (FAILED(createHr) || !defaultVbo) {
      std::fprintf(stderr, "d3d9-gamebryo-probe: CreateVertexBuffer (DEFAULT) failed (0x%08lx)\n", createHr);
      return false;
    }
    std::printf("d3d9-gamebryo-probe: CreateVertexBuffer (D3DPOOL_DEFAULT) OK\n");

    // Reset must be rejected while the DEFAULT-pool buffer is still alive.
    presentParams.BackBufferWidth  = 800;
    presentParams.BackBufferHeight = 600;
    const HRESULT blockedHr = device->Reset(&presentParams);
    if (SUCCEEDED(blockedHr)) {
      std::fprintf(stderr, "d3d9-gamebryo-probe: Reset unexpectedly succeeded with a live D3DPOOL_DEFAULT resource\n");
      defaultVbo->Release();
      return false;
    }
    std::printf("d3d9-gamebryo-probe: Reset correctly rejected with live default resource (0x%08lx)\n", blockedHr);

    // TestCooperativeLevel should now report that the device needs a reset.
    const HRESULT notResetHr = device->TestCooperativeLevel();
    if (notResetHr != D3DERR_DEVICENOTRESET) {
      std::fprintf(stderr, "d3d9-gamebryo-probe: expected D3DERR_DEVICENOTRESET, got 0x%08lx\n", notResetHr);
      defaultVbo->Release();
      return false;
    }
    std::printf("d3d9-gamebryo-probe: TestCooperativeLevel reports D3DERR_DEVICENOTRESET\n");

    // Release the losable resource and retry: Reset must now succeed.
    defaultVbo->Release();
    const HRESULT resetHr = device->Reset(&presentParams);
    if (FAILED(resetHr)) {
      std::fprintf(stderr, "d3d9-gamebryo-probe: Reset after releasing default resource failed (0x%08lx)\n", resetHr);
      return false;
    }

    const HRESULT okHr = device->TestCooperativeLevel();
    if (okHr != D3D_OK) {
      std::fprintf(stderr, "d3d9-gamebryo-probe: TestCooperativeLevel not OK after reset (0x%08lx)\n", okHr);
      return false;
    }
    std::printf("d3d9-gamebryo-probe: device-lost reset cycle OK\n");
    return true;
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
   || !checkDepthFormat(d3d9, adapter, D3DFMT_D24S8, "D24S8")
   || !checkDepthFormat(d3d9, adapter, D3DFMT_D16, "D16")) {
    d3d9->Release();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

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

  if (!drawFixedFunctionTriangle(device)
   || !drawIndexedFromBuffers(device)
   || !runStateBlockCheck(device)
   || !runOcclusionQuery(device)
   || !runEventQuery(device)) {
    device->Release();
    d3d9->Release();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

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

  if (!exerciseResetCycle(device, presentParams)) {
    device->Release();
    d3d9->Release();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

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
