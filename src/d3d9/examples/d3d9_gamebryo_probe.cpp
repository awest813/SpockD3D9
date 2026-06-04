/**
 * Gamebryo / Fallout 3-style D3D9 device creation probe (Milestone F).
 *
 * Exercises the native SpockD3D9 path with presentation parameters, format
 * queries, and caps checks typical of Gamebryo titles, then clears and
 * presents a few frames. Intended for CI on macOS (MoltenVK) alongside
 * d3d9-clear; does not require the Windows game binary.
 *
 * Exit 0 when the probe prints "d3d9-gamebryo-probe: OK".
 */

#include <cstdio>
#include <cstdlib>
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
