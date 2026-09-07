/**
 * Host-boundary smoke test for the experimental Windows PE d3d9.dll.
 *
 * Unlike d3d9-clear (native dylib, SDL window), this is a plain Win32 console
 * executable meant to run inside a Wine-family host (Wine / CrossOver /
 * Cosmos bottle). It exercises exactly the path a hosted 32-bit game takes —
 * native d3d9.dll override, Win32 HWND, winevulkan surface, Present — without
 * needing a game install:
 *
 *   1. LoadLibrary("d3d9.dll")  -> proves the *native* override was picked up
 *      (prints the loaded module path; system32 means the host's builtin
 *      d3d9 won and WINEDLLOVERRIDES is not applied)
 *   2. Direct3DCreate9 + adapter enumeration
 *   3. CreateDevice (logs "D3D9: CreateDeviceEx OK" from inside the DLL)
 *   4. Clear + Present N frames, alternating two colors so the present path
 *      is visually confirmable in the host window
 *
 * Its stdout plus the DLL's stderr (DXVK banner) are what
 * scripts/check-boot-logs.sh greps for V1-V3. Driven by scripts/run-pe-smoke.sh.
 *
 * Exit codes: 0 OK; 2 d3d9.dll load/export; 3 window; 4 Direct3DCreate9;
 *             5 adapter enumeration; 6 CreateDevice; 7 Present.
 */

#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include <windows.h>
#include <d3d9.h>

namespace {

  typedef IDirect3D9* (WINAPI *PFN_Direct3DCreate9)(UINT);

  constexpr uint32_t kWidth  = 640;
  constexpr uint32_t kHeight = 480;

  bool gWindowClosed = false;

  LRESULT CALLBACK SmokeWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
      case WM_CLOSE:
      case WM_DESTROY:
        gWindowClosed = true;
        PostQuitMessage(0);
        return 0;
      default:
        return DefWindowProcA(hwnd, msg, wp, lp);
    }
  }

  int parseFrameCount(int argc, char** argv) {
    if (argc < 2)
      return 30;

    char* end = nullptr;
    const long value = std::strtol(argv[1], &end, 10);
    if (end == argv[1] || value < 1)
      return 30;

    return int(value);
  }

} // namespace

int main(int argc, char** argv) {
    // Wine pipes can swallow buffered output when a process dies mid-run;
    // keep every diagnostic line on the wire as it is printed.
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);

    const int frameCount = parseFrameCount(argc, argv);

    // 1. The override DLL must load through the same mechanism a game uses.
    HMODULE d3d9Module = LoadLibraryA("d3d9.dll");
    if (!d3d9Module) {
      std::fprintf(stderr, "d3d9-pe-smoke: LoadLibrary(\"d3d9.dll\") failed: %lu\n",
                   static_cast<unsigned long>(GetLastError()));
      return 2;
    }

    char modulePath[MAX_PATH] = { };
    GetModuleFileNameA(d3d9Module, modulePath, MAX_PATH);
    std::printf("d3d9-pe-smoke: d3d9.dll loaded from: %s\n", modulePath);
    std::printf("d3d9-pe-smoke: (expect the exe directory; a system32 path means"
                " the host's builtin d3d9 won the override)\n");

    auto pfnCreate9 = reinterpret_cast<PFN_Direct3DCreate9>(
      GetProcAddress(d3d9Module, "Direct3DCreate9"));
    if (!pfnCreate9) {
      std::fprintf(stderr, "d3d9-pe-smoke: GetProcAddress(Direct3DCreate9) failed\n");
      return 2;
    }

    // 2. Win32 window — the PE d3d9.dll presents through the host's HWND.
    WNDCLASSA wc = { };
    wc.style         = CS_OWNDC;
    wc.lpfnWndProc   = SmokeWndProc;
    wc.hInstance     = GetModuleHandleA(nullptr);
    wc.hCursor       = LoadCursorA(nullptr, IDC_ARROW);
    wc.lpszClassName = "SpockD3D9PESmoke";

    if (!RegisterClassA(&wc)) {
      std::fprintf(stderr, "d3d9-pe-smoke: RegisterClassA failed: %lu\n",
                   static_cast<unsigned long>(GetLastError()));
      return 3;
    }

    RECT rect = { 0, 0, static_cast<LONG>(kWidth), static_cast<LONG>(kHeight) };
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    HWND hwnd = CreateWindowA(
      wc.lpszClassName, "SpockD3D9 PE smoke test",
      WS_OVERLAPPEDWINDOW | WS_VISIBLE,
      CW_USEDEFAULT, CW_USEDEFAULT,
      rect.right - rect.left, rect.bottom - rect.top,
      nullptr, nullptr, wc.hInstance, nullptr);

    if (!hwnd) {
      std::fprintf(stderr, "d3d9-pe-smoke: CreateWindowA failed: %lu\n",
                   static_cast<unsigned long>(GetLastError()));
      return 3;
    }

    // 3. Adapter enumeration (V2).
    IDirect3D9* d3d9 = pfnCreate9(D3D_SDK_VERSION);
    if (!d3d9) {
      std::fprintf(stderr, "d3d9-pe-smoke: Direct3DCreate9 failed"
                           " (override DLL or Vulkan loader not reachable)\n");
      return 4;
    }

    const UINT adapterCount = d3d9->GetAdapterCount();
    std::printf("d3d9-pe-smoke: adapterCount=%u\n", adapterCount);
    if (adapterCount == 0) {
      std::fprintf(stderr, "d3d9-pe-smoke: no adapters enumerated"
                           " (MoltenVK / winevulkan not reachable inside the prefix)\n");
      d3d9->Release();
      return 5;
    }

    D3DADAPTER_IDENTIFIER9 identifier = { };
    if (SUCCEEDED(d3d9->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &identifier)))
      std::printf("d3d9-pe-smoke: adapter 0: %s\n", identifier.Description);

    // 4. Device (V3). The DLL logs "D3D9: CreateDeviceEx OK (...)" here.
    D3DPRESENT_PARAMETERS presentParams = { };
    presentParams.Windowed             = TRUE;
    presentParams.SwapEffect           = D3DSWAPEFFECT_DISCARD;
    presentParams.BackBufferCount      = 1;
    presentParams.BackBufferFormat     = D3DFMT_X8R8G8B8;
    presentParams.BackBufferWidth      = kWidth;
    presentParams.BackBufferHeight     = kHeight;
    presentParams.hDeviceWindow        = hwnd;
    presentParams.EnableAutoDepthStencil = FALSE;
    presentParams.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

    IDirect3DDevice9* device = nullptr;
    const HRESULT hr = d3d9->CreateDevice(
      D3DADAPTER_DEFAULT,
      D3DDEVTYPE_HAL,
      hwnd,
      D3DCREATE_HARDWARE_VERTEXPROCESSING,
      &presentParams,
      &device);

    if (FAILED(hr) || !device) {
      std::fprintf(stderr, "d3d9-pe-smoke: CreateDevice failed (HRESULT 0x%08lx)\n",
                   static_cast<unsigned long>(hr));
      d3d9->Release();
      return 6;
    }

    std::printf("d3d9-pe-smoke: presenting %d frame(s)\n", frameCount);

    int presented = 0;
    for (int frame = 0; frame < frameCount && !gWindowClosed; frame++) {
      MSG msg;
      while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT)
          gWindowClosed = true;
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
      }
      if (gWindowClosed)
        break;

      // Alternate two blues so frame cycling is visible in the host window:
      // a static single color hides a stuck/dead present path.
      const D3DCOLOR color = (frame & 1)
        ? D3DCOLOR_XRGB(32, 64, 192)
        : D3DCOLOR_XRGB(16, 32, 96);

      device->Clear(0, nullptr, D3DCLEAR_TARGET, color, 1.0f, 0);

      const HRESULT presentHr = device->Present(nullptr, nullptr, nullptr, nullptr);
      if (FAILED(presentHr)) {
        std::fprintf(stderr, "d3d9-pe-smoke: Present failed at frame %d (HRESULT 0x%08lx)\n",
                     frame, static_cast<unsigned long>(presentHr));
        device->Release();
        d3d9->Release();
        return 7;
      }
      presented++;
    }

    device->Release();
    d3d9->Release();

    if (presented == 0) {
      std::fprintf(stderr, "d3d9-pe-smoke: window closed before any frame presented\n");
      return 7;
    }

    std::printf("d3d9-pe-smoke: OK (%d frame(s) presented)\n", presented);
    return 0;
}
