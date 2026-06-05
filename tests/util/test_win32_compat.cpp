// Unit tests for the native Win32 compatibility shims in
// src/util/util_win32_compat.h (Milestone E).
//
// These cover the handle objects SpockD3D9 emulates on macOS/Linux:
// semaphores, events (auto- and manual-reset), WaitForSingleObject(Ex),
// WaitForMultipleObjects(Ex), timeouts, DuplicateHandle reference sharing,
// CloseHandle teardown, and minimal GDI memory-DC lifecycle shims.
//
// The test is intentionally hermetic: it includes the real shim header and
// stubs the single Logger symbol the header references, so it builds with a
// bare C++ toolchain (no Vulkan/SDL/meson required). Build from the repo root:
//
//   g++ -std=c++17 -pthread -Isrc/util -Iinclude/native/windows
//       tests/util/test_win32_compat.cpp -o test_win32_compat
//   ./test_win32_compat

#include "util_win32_compat.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

// The shim only ODR-uses dxvk::Logger::warn (on the unknown-handle path).
// Provide a stub so the test links without the rest of the util library.
namespace dxvk {
  void Logger::warn(const std::string& message) {
    std::fprintf(stderr, "[warn] %s\n", message.c_str());
  }
}

static int g_failures = 0;

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      std::printf("FAIL: %s (line %d)\n", #cond, __LINE__);              \
      ++g_failures;                                                       \
    }                                                                     \
  } while (0)

static void test_semaphore_counting() {
  HANDLE s = CreateSemaphoreA(nullptr, 1, 2, nullptr);
  CHECK(s != nullptr);

  // Initial count 1 -> one immediate acquire succeeds, the next polls out.
  CHECK(WaitForSingleObject(s, 0) == WAIT_OBJECT_0);
  CHECK(WaitForSingleObject(s, 0) == WAIT_TIMEOUT);

  LONG previous = -1;
  CHECK(ReleaseSemaphore(s, 2, &previous) == TRUE);
  CHECK(previous == 0);

  // Releasing past the maximum count must fail and not change the count.
  CHECK(ReleaseSemaphore(s, 1, nullptr) == FALSE);
  CHECK(WaitForSingleObject(s, 0) == WAIT_OBJECT_0);
  CHECK(WaitForSingleObject(s, 0) == WAIT_OBJECT_0);
  CHECK(WaitForSingleObject(s, 0) == WAIT_TIMEOUT);

  CHECK(CloseHandle(s) == TRUE);
}

static void test_semaphore_blocking() {
  HANDLE s = CreateSemaphoreA(nullptr, 0, 1, nullptr);
  std::atomic<bool> woke{false};

  std::thread waiter([&] {
    CHECK(WaitForSingleObject(s, INFINITE) == WAIT_OBJECT_0);
    woke = true;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  CHECK(!woke.load());
  CHECK(ReleaseSemaphore(s, 1, nullptr) == TRUE);
  waiter.join();
  CHECK(woke.load());

  CHECK(CloseHandle(s) == TRUE);
}

static void test_auto_reset_event() {
  HANDLE e = CreateEventA(nullptr, FALSE /* auto-reset */, FALSE, nullptr);
  CHECK(WaitForSingleObject(e, 0) == WAIT_TIMEOUT);

  CHECK(SetEvent(e) == TRUE);
  CHECK(WaitForSingleObject(e, 0) == WAIT_OBJECT_0); // consumes the signal
  CHECK(WaitForSingleObject(e, 0) == WAIT_TIMEOUT);  // auto-reset cleared it

  // A blocked waiter is released by SetEvent.
  std::atomic<bool> woke{false};
  std::thread waiter([&] {
    CHECK(WaitForSingleObject(e, INFINITE) == WAIT_OBJECT_0);
    woke = true;
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  CHECK(!woke.load());
  CHECK(SetEvent(e) == TRUE);
  waiter.join();
  CHECK(woke.load());

  CHECK(CloseHandle(e) == TRUE);
}

static void test_manual_reset_event() {
  HANDLE e = CreateEventA(nullptr, TRUE /* manual-reset */, TRUE /* signaled */, nullptr);

  // Stays signaled across multiple waits until ResetEvent.
  CHECK(WaitForSingleObject(e, 0) == WAIT_OBJECT_0);
  CHECK(WaitForSingleObject(e, 0) == WAIT_OBJECT_0);
  CHECK(ResetEvent(e) == TRUE);
  CHECK(WaitForSingleObject(e, 0) == WAIT_TIMEOUT);
  CHECK(SetEvent(e) == TRUE);
  CHECK(WaitForSingleObject(e, 0) == WAIT_OBJECT_0);

  CHECK(CloseHandle(e) == TRUE);
}

static void test_duplicate_handle() {
  // Shared ownership: the object survives closing the source while a duplicate
  // still references it. This is the d3d11 frame-latency waitable-object path.
  HANDLE s = CreateSemaphoreA(nullptr, 1, 1, nullptr);
  HANDLE dup = nullptr;
  HANDLE proc = GetCurrentProcess();

  CHECK(DuplicateHandle(proc, s, proc, &dup, 0, FALSE, DUPLICATE_SAME_ACCESS) == TRUE);
  CHECK(dup == s); // same underlying object
  CHECK(CloseHandle(s) == TRUE);
  CHECK(WaitForSingleObject(dup, 0) == WAIT_OBJECT_0); // still usable
  CHECK(CloseHandle(dup) == TRUE);                     // last reference frees it

  // DUPLICATE_CLOSE_SOURCE leaves the net reference count unchanged.
  HANDLE e = CreateEventA(nullptr, TRUE, TRUE, nullptr);
  HANDLE moved = nullptr;
  CHECK(DuplicateHandle(nullptr, e, nullptr, &moved, 0, FALSE, DUPLICATE_CLOSE_SOURCE) == TRUE);
  CHECK(moved == e);
  CHECK(CloseHandle(moved) == TRUE); // only one close needed
}

static void test_gdi_dc_lifecycle() {
  HDC dc = CreateCompatibleDC(nullptr);
  CHECK(dc != nullptr);

  HDC compatible = CreateCompatibleDC(dc);
  CHECK(compatible != nullptr);

  // HDCs are not waitable kernel objects and are deleted with DeleteDC, not
  // CloseHandle or DuplicateHandle.
  CHECK(WaitForSingleObject(static_cast<HANDLE>(dc), 0) == WAIT_FAILED);
  CHECK(CloseHandle(static_cast<HANDLE>(dc)) == FALSE);

  HANDLE dup = reinterpret_cast<HANDLE>(0x1);
  CHECK(DuplicateHandle(nullptr, static_cast<HANDLE>(dc), nullptr, &dup, 0, FALSE, 0) == FALSE);
  CHECK(dup == nullptr);

  CHECK(DeleteDC(nullptr) == FALSE);

  CHECK(DeleteDC(compatible) == TRUE);
  CHECK(DeleteDC(dc) == TRUE);
}

static void test_wait_multiple_any() {
  HANDLE e0 = CreateEventA(nullptr, TRUE, FALSE, nullptr);
  HANDLE e1 = CreateEventA(nullptr, TRUE, FALSE, nullptr);
  const HANDLE handles[] = { e0, e1 };

  CHECK(WaitForMultipleObjects(2, handles, FALSE, 0) == WAIT_TIMEOUT);

  CHECK(SetEvent(e1) == TRUE);
  CHECK(WaitForMultipleObjects(2, handles, FALSE, 0) == WAIT_OBJECT_0 + 1);

  CHECK(SetEvent(e0) == TRUE);
  CHECK(WaitForMultipleObjects(2, handles, FALSE, 0) == WAIT_OBJECT_0);

  CHECK(CloseHandle(e0) == TRUE);
  CHECK(CloseHandle(e1) == TRUE);
}

static void test_wait_multiple_all() {
  HANDLE s0 = CreateSemaphoreA(nullptr, 1, 1, nullptr);
  HANDLE s1 = CreateSemaphoreA(nullptr, 1, 1, nullptr);
  const HANDLE handles[] = { s0, s1 };

  CHECK(WaitForMultipleObjects(2, handles, TRUE, 0) == WAIT_OBJECT_0);
  CHECK(WaitForSingleObject(s0, 0) == WAIT_TIMEOUT);
  CHECK(WaitForSingleObject(s1, 0) == WAIT_TIMEOUT);

  CHECK(ReleaseSemaphore(s0, 1, nullptr) == TRUE);
  CHECK(ReleaseSemaphore(s1, 1, nullptr) == TRUE);
  CHECK(WaitForMultipleObjects(2, handles, TRUE, 0) == WAIT_OBJECT_0);

  CHECK(CloseHandle(s0) == TRUE);
  CHECK(CloseHandle(s1) == TRUE);
}

static void test_wait_multiple_blocking() {
  HANDLE e0 = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  HANDLE e1 = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  const HANDLE handles[] = { e0, e1 };
  std::atomic<bool> woke{false};

  std::thread waiter([&] {
    CHECK(WaitForMultipleObjects(2, handles, FALSE, INFINITE) == WAIT_OBJECT_0 + 1);
    woke = true;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  CHECK(!woke.load());
  CHECK(SetEvent(e1) == TRUE);
  waiter.join();
  CHECK(woke.load());

  CHECK(CloseHandle(e0) == TRUE);
  CHECK(CloseHandle(e1) == TRUE);
}

static void test_wait_multiple_all_concurrent_drain() {
  // Regression: in WaitForMultipleObjectsEx(bWaitAll=TRUE) the readiness check
  // and the acquire must be atomic. A concurrent thread draining one semaphore
  // must never cause a partial acquire that consumes the other handle and then
  // returns WAIT_FAILED. Here a competitor races for s1; the wait-all must
  // either acquire both cleanly or block until both are available again — it
  // must never silently swallow s0's count.
  HANDLE s0 = CreateSemaphoreA(nullptr, 1, 1, nullptr);
  HANDLE s1 = CreateSemaphoreA(nullptr, 1, 1, nullptr);
  const HANDLE handles[] = { s0, s1 };

  std::atomic<bool> stop{false};
  std::atomic<int> competitorWins{0};

  // Competitor repeatedly drains and refills s1, contending with the waiter.
  std::thread competitor([&] {
    while (!stop.load()) {
      if (WaitForSingleObject(s1, 0) == WAIT_OBJECT_0) {
        competitorWins++;
        ReleaseSemaphore(s1, 1, nullptr);
      }
      std::this_thread::yield();
    }
  });

  // The waiter must succeed a bounded number of times without ever wedging or
  // returning WAIT_FAILED. Each success consumes both counts; we refill them.
  for (int i = 0; i < 200; ++i) {
    DWORD r = WaitForMultipleObjects(2, handles, TRUE, 1000);
    CHECK(r == WAIT_OBJECT_0 || r == WAIT_TIMEOUT);
    if (r == WAIT_OBJECT_0) {
      // Both were consumed atomically — refill for the next round.
      ReleaseSemaphore(s0, 1, nullptr);
      ReleaseSemaphore(s1, 1, nullptr);
    } else {
      // Timed out because the competitor held s1; make sure s0 was NOT
      // silently consumed (the partial-acquire bug). It must still be signaled.
      CHECK(WaitForSingleObject(s0, 0) == WAIT_OBJECT_0);
      ReleaseSemaphore(s0, 1, nullptr);
    }
  }

  stop = true;
  competitor.join();

  CHECK(CloseHandle(s0) == TRUE);
  CHECK(CloseHandle(s1) == TRUE);
}

static void test_wait_multiple_duplicate_handle() {
  // A duplicate handle resolves to the same underlying object; the bWaitAll
  // combined-lock path would otherwise lock the same mutex twice (UB). The
  // implementation must reject duplicates with WAIT_FAILED.
  HANDLE e0 = CreateEventA(nullptr, TRUE, TRUE, nullptr);  // manual-reset, signaled
  HANDLE dup = nullptr;
  CHECK(DuplicateHandle(GetCurrentProcess(), e0, GetCurrentProcess(),
                        &dup, 0, FALSE, DUPLICATE_SAME_ACCESS) == TRUE);
  CHECK(dup != nullptr);

  const HANDLE handles[] = { e0, dup };
  CHECK(WaitForMultipleObjects(2, handles, TRUE, 0) == WAIT_FAILED);
  CHECK(WaitForMultipleObjects(2, handles, FALSE, 0) == WAIT_FAILED);

  CHECK(CloseHandle(dup) == TRUE);
  CHECK(CloseHandle(e0) == TRUE);
}

static void test_invalid_inputs() {
  CHECK(SetEvent(nullptr) == FALSE);
  CHECK(ResetEvent(INVALID_HANDLE_VALUE) == FALSE);
  CHECK(WaitForSingleObject(nullptr, 0) == WAIT_FAILED);
  CHECK(ReleaseSemaphore(nullptr, 1, nullptr) == FALSE);
  CHECK(CloseHandle(nullptr) == FALSE);
  CHECK(CloseHandle(GetCurrentProcess()) == FALSE); // pseudo-handle is not ours

  HANDLE target = reinterpret_cast<HANDLE>(0x1);
  CHECK(DuplicateHandle(nullptr, nullptr, nullptr, &target, 0, FALSE, 0) == FALSE);
  CHECK(target == nullptr); // cleared on failure

  HANDLE handles[] = { nullptr };
  CHECK(WaitForMultipleObjects(1, handles, FALSE, 0) == WAIT_FAILED);
  CHECK(WaitForMultipleObjects(0, handles, FALSE, 0) == WAIT_FAILED);
}

int main() {
  test_semaphore_counting();
  test_semaphore_blocking();
  test_auto_reset_event();
  test_manual_reset_event();
  test_duplicate_handle();
  test_gdi_dc_lifecycle();
  test_wait_multiple_any();
  test_wait_multiple_all();
  test_wait_multiple_blocking();
  test_wait_multiple_all_concurrent_drain();
  test_wait_multiple_duplicate_handle();
  test_invalid_inputs();

  if (g_failures == 0) {
    std::printf("ALL WIN32-COMPAT TESTS PASSED\n");
    return 0;
  }

  std::printf("%d CHECK(S) FAILED\n", g_failures);
  return 1;
}
