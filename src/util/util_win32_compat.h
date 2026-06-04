#pragma once

#if defined(__unix__) || defined(__APPLE__)

#include <windows.h>
#include <dlfcn.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <vector>
#include <mutex>
#include <condition_variable>

#include "log/log.h"

#if defined(__APPLE__)
#include "util_env.h"
#include "util_string.h"
#endif

inline HMODULE LoadLibraryA(LPCSTR lpLibFileName) {
  if (HMODULE module = dlopen(lpLibFileName, RTLD_NOW))
    return module;

#if defined(__APPLE__)
  // Homebrew libraries live outside dyld's default search path. When
  // DYLD_LIBRARY_PATH is narrowed, bare sonames fail unless the fallback path
  // was configured; probe known prefixes directly as a backstop.
  for (const auto& prefix : dxvk::env::getHomebrewPrefixes()) {
    const std::string path = dxvk::str::format(prefix, "/lib/", lpLibFileName);

    if (HMODULE module = dlopen(path.c_str(), RTLD_NOW))
      return module;
  }
#endif

  return nullptr;
}

inline void FreeLibrary(HMODULE module) {
  dlclose(module);
}

inline void* GetProcAddress(HMODULE module, LPCSTR lpProcName) {
  if (!module)
    return nullptr;

  return dlsym(module, lpProcName);
}

// INFINITE is not declared by the native <windows.h> shim; the real Win32
// header defines it as 0xFFFFFFFF.  WAIT_OBJECT_0 / WAIT_TIMEOUT / WAIT_FAILED
// already come from windows_base.h.
#ifndef INFINITE
#define INFINITE 0xFFFFFFFFu
#endif

// ---------------------------------------------------------------------------
// Native handle infrastructure
//
// HANDLE is void* on non-Windows.  We allocate small tagged structs on the
// heap and cast their pointers to HANDLE.  Every handle carries a kind tag so
// CloseHandle / WaitForSingleObject / DuplicateHandle can dispatch on type, and
// a reference count so DuplicateHandle and CloseHandle can share ownership of a
// single underlying object (matching Win32 handle semantics).
// ---------------------------------------------------------------------------

enum class NativeHandleKind : uint32_t {
  Semaphore = 0x534D5048u,  // 'SMPH'
  Event     = 0x45564E54u,  // 'EVNT'
  GdiDc     = 0x47444943u,  // 'GDIC'
};

struct NativeHandleHeader {
  NativeHandleKind      kind;
  std::atomic<uint32_t> refCount;
};

// Semaphore handle backed by a mutex + condition variable.
// Windows HANDLE is void*; we cast the pointer to HANDLE and back.
struct NativeSemaphoreHandle {
  NativeHandleHeader      header;
  LONG                    maxCount;
  std::mutex              mtx;
  std::condition_variable cv;
  LONG                    count;
};

// Event handle backed by a mutex + condition variable.  Supports both
// manual-reset events (stay signalled until ResetEvent) and auto-reset events
// (a single successful wait clears the signalled state).
struct NativeEventHandle {
  NativeHandleHeader      header;
  std::mutex              mtx;
  std::condition_variable cv;
  bool                    manualReset;
  bool                    signaled;
};

// Minimal memory-DC handle for native builds. GDI itself is Windows-only, but
// some Windows-facing code only needs CreateCompatibleDC/DeleteDC to hand out a
// stable non-null token for setup/teardown paths.
struct NativeGdiDcHandle {
  NativeHandleHeader      header;
};

// Validate a HANDLE and return its header if it is one of our tagged native
// objects, or nullptr otherwise.  This is intended for handles returned by this
// shim layer; arbitrary foreign pointers cannot be probed safely in portable C++.
inline NativeHandleHeader* GetNativeHandleHeader(HANDLE hObject) {
  if (!hObject || hObject == INVALID_HANDLE_VALUE)
    return nullptr;

  auto* header = static_cast<NativeHandleHeader*>(hObject);
  switch (header->kind) {
    case NativeHandleKind::Semaphore:
    case NativeHandleKind::Event:
    case NativeHandleKind::GdiDc:
      return header;
    default:
      return nullptr;
  }
}

// ---------------------------------------------------------------------------
// CreateSemaphoreA / ReleaseSemaphore
// ---------------------------------------------------------------------------

inline HANDLE CreateSemaphoreA(
        SECURITY_ATTRIBUTES*  /* lpSemaphoreAttributes */,
        LONG                  lInitialCount,
        LONG                  lMaximumCount,
        LPCSTR                /* lpName */) {
  if (lInitialCount < 0 || lMaximumCount < 1 || lInitialCount > lMaximumCount)
    return nullptr;

  auto* s = new NativeSemaphoreHandle();
  s->header.kind = NativeHandleKind::Semaphore;
  s->header.refCount.store(1, std::memory_order_relaxed);
  s->maxCount    = lMaximumCount;
  s->count       = lInitialCount;
  return static_cast<HANDLE>(s);
}
#define CreateSemaphore CreateSemaphoreA

inline BOOL ReleaseSemaphore(
        HANDLE hSemaphore,
        LONG   lReleaseCount,
        LONG*  lpPreviousCount) {
  if (lReleaseCount < 1)
    return FALSE;

  // Check the kind tag through the base header before casting to the full type.
  auto* header = GetNativeHandleHeader(hSemaphore);
  if (!header || header->kind != NativeHandleKind::Semaphore)
    return FALSE;

  auto* s = static_cast<NativeSemaphoreHandle*>(hSemaphore);
  std::unique_lock<std::mutex> lock(s->mtx);

  // Guard against overflow and against exceeding the declared maximum count.
  if (lReleaseCount > s->maxCount - s->count)
    return FALSE;

  if (lpPreviousCount)
    *lpPreviousCount = s->count;

  s->count += lReleaseCount;
  lock.unlock();

  for (LONG i = 0; i < lReleaseCount; ++i)
    s->cv.notify_one();

  return TRUE;
}

// ---------------------------------------------------------------------------
// CreateEventA / CreateEventW / SetEvent / ResetEvent
// ---------------------------------------------------------------------------

inline HANDLE CreateEventA(
        SECURITY_ATTRIBUTES*  /* lpEventAttributes */,
        BOOL                  bManualReset,
        BOOL                  bInitialState,
        LPCSTR                /* lpName */) {
  auto* e = new NativeEventHandle();
  e->header.kind = NativeHandleKind::Event;
  e->header.refCount.store(1, std::memory_order_relaxed);
  e->manualReset = bManualReset != FALSE;
  e->signaled    = bInitialState != FALSE;
  return static_cast<HANDLE>(e);
}

inline HANDLE CreateEventW(
        SECURITY_ATTRIBUTES*  lpEventAttributes,
        BOOL                  bManualReset,
        BOOL                  bInitialState,
        LPCWSTR               /* lpName */) {
  return CreateEventA(lpEventAttributes, bManualReset, bInitialState, nullptr);
}
#define CreateEvent CreateEventA

inline BOOL SetEvent(HANDLE hEvent) {
  auto* header = GetNativeHandleHeader(hEvent);
  if (!header || header->kind != NativeHandleKind::Event)
    return FALSE;

  auto* e = static_cast<NativeEventHandle*>(hEvent);
  std::lock_guard<std::mutex> lock(e->mtx);
  e->signaled = true;

  // Manual-reset events release every waiter; auto-reset events release one,
  // and the woken waiter consumes the signal.
  if (e->manualReset)
    e->cv.notify_all();
  else
    e->cv.notify_one();

  return TRUE;
}

inline BOOL ResetEvent(HANDLE hEvent) {
  auto* header = GetNativeHandleHeader(hEvent);
  if (!header || header->kind != NativeHandleKind::Event)
    return FALSE;

  auto* e = static_cast<NativeEventHandle*>(hEvent);
  std::lock_guard<std::mutex> lock(e->mtx);
  e->signaled = false;
  return TRUE;
}

// ---------------------------------------------------------------------------
// WaitForSingleObject / WaitForSingleObjectEx
//
// Waits on a semaphore (decrementing its count) or an event (clearing the
// signal for auto-reset events).  A timeout of INFINITE blocks indefinitely;
// any other value is treated as a millisecond timeout, with 0 acting as a poll.
// ---------------------------------------------------------------------------

inline DWORD WaitForSingleObjectEx(
        HANDLE hHandle,
        DWORD  dwMilliseconds,
        BOOL   /* bAlertable */) {
  auto* header = GetNativeHandleHeader(hHandle);
  if (!header)
    return WAIT_FAILED;

  const auto timeout = std::chrono::milliseconds(dwMilliseconds);

  switch (header->kind) {
    case NativeHandleKind::Semaphore: {
      auto* s = static_cast<NativeSemaphoreHandle*>(hHandle);
      std::unique_lock<std::mutex> lock(s->mtx);
      auto ready = [&] { return s->count > 0; };

      if (dwMilliseconds == INFINITE)
        s->cv.wait(lock, ready);
      else if (!s->cv.wait_for(lock, timeout, ready))
        return WAIT_TIMEOUT;

      s->count -= 1;
      return WAIT_OBJECT_0;
    }

    case NativeHandleKind::Event: {
      auto* e = static_cast<NativeEventHandle*>(hHandle);
      std::unique_lock<std::mutex> lock(e->mtx);
      auto ready = [&] { return e->signaled; };

      if (dwMilliseconds == INFINITE)
        e->cv.wait(lock, ready);
      else if (!e->cv.wait_for(lock, timeout, ready))
        return WAIT_TIMEOUT;

      // Auto-reset events consume the signal on a successful wait.
      if (!e->manualReset)
        e->signaled = false;

      return WAIT_OBJECT_0;
    }

    default:
      return WAIT_FAILED;
  }
}

inline DWORD WaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds) {
  return WaitForSingleObjectEx(hHandle, dwMilliseconds, FALSE);
}

// ---------------------------------------------------------------------------
// WaitForMultipleObjects / WaitForMultipleObjectsEx
//
// Gamebryo and other D3D9 titles synchronize worker threads with multiple
// kernel objects.  We support waits on semaphores and events only (the kinds
// SpockD3D9 creates natively).  Handles are locked in pointer order to avoid
// deadlocks when waiting on more than one object at a time.
// ---------------------------------------------------------------------------

#ifndef MAXIMUM_WAIT_OBJECTS
#define MAXIMUM_WAIT_OBJECTS 64
#endif

namespace detail {

inline bool nativeHandleReadyLocked(NativeHandleHeader* header) {
  switch (header->kind) {
    case NativeHandleKind::Semaphore:
      return reinterpret_cast<NativeSemaphoreHandle*>(header)->count > 0;
    case NativeHandleKind::Event:
      return reinterpret_cast<NativeEventHandle*>(header)->signaled;
    default:
      return false;
  }
}

inline bool acquireNativeHandleLocked(NativeHandleHeader* header) {
  switch (header->kind) {
    case NativeHandleKind::Semaphore: {
      auto* s = reinterpret_cast<NativeSemaphoreHandle*>(header);
      if (s->count <= 0)
        return false;
      s->count -= 1;
      return true;
    }

    case NativeHandleKind::Event: {
      auto* e = reinterpret_cast<NativeEventHandle*>(header);
      if (!e->signaled)
        return false;
      if (!e->manualReset)
        e->signaled = false;
      return true;
    }

    default:
      return false;
  }
}

inline std::mutex* nativeHandleMutex(NativeHandleHeader* header) {
  switch (header->kind) {
    case NativeHandleKind::Semaphore:
      return &reinterpret_cast<NativeSemaphoreHandle*>(header)->mtx;
    case NativeHandleKind::Event:
      return &reinterpret_cast<NativeEventHandle*>(header)->mtx;
    default:
      return nullptr;
  }
}

inline void waitNativeHandleLocked(
        NativeHandleHeader* header,
        std::unique_lock<std::mutex>& lock,
        const std::chrono::steady_clock::time_point& deadline) {
  switch (header->kind) {
    case NativeHandleKind::Semaphore: {
      auto* s = reinterpret_cast<NativeSemaphoreHandle*>(header);
      auto ready = [&] { return s->count > 0; };

      if (deadline == std::chrono::steady_clock::time_point::max())
        s->cv.wait(lock, ready);
      else
        s->cv.wait_until(lock, deadline, ready);
      break;
    }

    case NativeHandleKind::Event: {
      auto* e = reinterpret_cast<NativeEventHandle*>(header);
      auto ready = [&] { return e->signaled; };

      if (deadline == std::chrono::steady_clock::time_point::max())
        e->cv.wait(lock, ready);
      else
        e->cv.wait_until(lock, deadline, ready);
      break;
    }

    default:
      break;
  }
}

} // namespace detail

inline DWORD WaitForMultipleObjectsEx(
        DWORD         nCount,
        const HANDLE* lpHandles,
        BOOL          bWaitAll,
        DWORD         dwMilliseconds,
        BOOL          /* bAlertable */) {
  if (!lpHandles || nCount == 0 || nCount > MAXIMUM_WAIT_OBJECTS)
    return WAIT_FAILED;

  if (nCount == 1)
    return WaitForSingleObjectEx(lpHandles[0], dwMilliseconds, FALSE);

  std::vector<NativeHandleHeader*> headers(nCount);
  for (DWORD i = 0; i < nCount; ++i) {
    headers[i] = GetNativeHandleHeader(lpHandles[i]);
    if (!headers[i])
      return WAIT_FAILED;
    if (headers[i]->kind != NativeHandleKind::Semaphore
     && headers[i]->kind != NativeHandleKind::Event)
      return WAIT_FAILED;
  }

  std::vector<DWORD> order(nCount);
  for (DWORD i = 0; i < nCount; ++i)
    order[i] = i;
  std::sort(order.begin(), order.end(), [&](DWORD a, DWORD b) {
    return headers[a] < headers[b];
  });

  const auto deadline = (dwMilliseconds == INFINITE)
    ? std::chrono::steady_clock::time_point::max()
    : std::chrono::steady_clock::now() + std::chrono::milliseconds(dwMilliseconds);

  for (;;) {
    if (bWaitAll) {
      bool allReady = true;
      for (DWORD i = 0; i < nCount; ++i) {
        std::lock_guard<std::mutex> lock(*detail::nativeHandleMutex(headers[i]));
        if (!detail::nativeHandleReadyLocked(headers[i])) {
          allReady = false;
          break;
        }
      }

      if (allReady) {
        std::vector<std::unique_lock<std::mutex>> locks;
        locks.reserve(nCount);
        for (DWORD idx : order)
          locks.emplace_back(*detail::nativeHandleMutex(headers[idx]));

        for (DWORD i = 0; i < nCount; ++i) {
          if (!detail::acquireNativeHandleLocked(headers[i]))
            return WAIT_FAILED;
        }
        return WAIT_OBJECT_0;
      }

      if (std::chrono::steady_clock::now() >= deadline)
        return WAIT_TIMEOUT;

      DWORD wakeOn = 0;
      for (DWORD i = 0; i < nCount; ++i) {
        std::lock_guard<std::mutex> lock(*detail::nativeHandleMutex(headers[i]));
        if (!detail::nativeHandleReadyLocked(headers[i])) {
          wakeOn = i;
          break;
        }
      }

      std::unique_lock<std::mutex> lock(*detail::nativeHandleMutex(headers[wakeOn]));
      detail::waitNativeHandleLocked(headers[wakeOn], lock, deadline);
      continue;
    }

    for (DWORD i = 0; i < nCount; ++i) {
      std::unique_lock<std::mutex> lock(*detail::nativeHandleMutex(headers[i]));
      if (detail::nativeHandleReadyLocked(headers[i])) {
        if (!detail::acquireNativeHandleLocked(headers[i]))
          return WAIT_FAILED;
        return WAIT_OBJECT_0 + i;
      }
    }

    if (std::chrono::steady_clock::now() >= deadline)
      return WAIT_TIMEOUT;

    std::unique_lock<std::mutex> lock(*detail::nativeHandleMutex(headers[order[0]]));
    detail::waitNativeHandleLocked(headers[order[0]], lock, deadline);
  }
}

inline DWORD WaitForMultipleObjects(
        DWORD         nCount,
        const HANDLE* lpHandles,
        BOOL          bWaitAll,
        DWORD         dwMilliseconds) {
  return WaitForMultipleObjectsEx(nCount, lpHandles, bWaitAll, dwMilliseconds, FALSE);
}

// ---------------------------------------------------------------------------
// DuplicateHandle
//
// Win32 hands out a second handle that refers to the same kernel object.  We
// model this by sharing the underlying object through its reference count and
// returning the same pointer; each handle must be closed exactly once.
// ---------------------------------------------------------------------------

inline BOOL DuplicateHandle(
        HANDLE  /* hSourceProcessHandle */,
        HANDLE  hSourceHandle,
        HANDLE  /* hTargetProcessHandle */,
        HANDLE* lpTargetHandle,
        DWORD   /* dwDesiredAccess */,
        BOOL    /* bInheritHandle */,
        DWORD   dwOptions) {
  if (lpTargetHandle)
    *lpTargetHandle = nullptr;

  auto* header = GetNativeHandleHeader(hSourceHandle);
  if (!header)
    return FALSE;

  if (header->kind != NativeHandleKind::Semaphore
   && header->kind != NativeHandleKind::Event)
    return FALSE;

  // DUPLICATE_CLOSE_SOURCE closes the source as part of the operation, so the
  // net reference count is unchanged (one handle consumed, one produced).
  // Otherwise we add a reference for the freshly minted target handle.
  if (!(dwOptions & DUPLICATE_CLOSE_SOURCE))
    header->refCount.fetch_add(1, std::memory_order_relaxed);

  if (lpTargetHandle)
    *lpTargetHandle = hSourceHandle;

  return TRUE;
}

// ---------------------------------------------------------------------------
// CloseHandle — drops a reference and destroys the object when the last
// reference goes away, dispatching the destructor on NativeHandleKind.
// ---------------------------------------------------------------------------

inline BOOL CloseHandle(HANDLE hObject) {
  // Reject null and INVALID_HANDLE_VALUE (-1), which also covers the
  // GetCurrentProcess() pseudo-handle that returns (HANDLE)-1.
  auto* header = GetNativeHandleHeader(hObject);
  if (!header) {
    if (hObject && hObject != INVALID_HANDLE_VALUE)
      dxvk::Logger::warn("CloseHandle: unknown handle type.");
    return FALSE;
  }

  // HDC objects are intentionally managed by DeleteDC, not CloseHandle.
  if (header->kind == NativeHandleKind::GdiDc)
    return FALSE;

  // Only the thread that observes the count drop to zero destroys the object.
  if (header->refCount.fetch_sub(1, std::memory_order_acq_rel) != 1)
    return TRUE;

  switch (header->kind) {
    case NativeHandleKind::Semaphore:
      delete static_cast<NativeSemaphoreHandle*>(hObject);
      return TRUE;
    case NativeHandleKind::Event:
      delete static_cast<NativeEventHandle*>(hObject);
      return TRUE;
    default:
      // Unreachable: GetNativeHandleHeader already validated the kind.
      return FALSE;
  }
}

// ---------------------------------------------------------------------------
// Process identity
// ---------------------------------------------------------------------------

inline HANDLE GetCurrentProcess() {
  // Windows convention: the pseudo-handle for the current process is -1,
  // which is the same value as INVALID_HANDLE_VALUE.  CloseHandle on this
  // value is a no-op on Windows, and our CloseHandle does the same.
  return INVALID_HANDLE_VALUE;
}

inline DWORD GetCurrentProcessId() {
  return static_cast<DWORD>(getpid());
}

// ---------------------------------------------------------------------------
// Session management — macOS/Linux have no Win32 session concept.
// Return session 0 for any existing process.
// ---------------------------------------------------------------------------

inline BOOL ProcessIdToSessionId(DWORD /* pid */, DWORD* id) {
  if (id)
    *id = 0;
  return TRUE;
}

// ---------------------------------------------------------------------------
// GDI DC compatibility
// ---------------------------------------------------------------------------

inline HDC CreateCompatibleDC(HDC /* hdc */) {
  auto* dc = new NativeGdiDcHandle();
  dc->header.kind = NativeHandleKind::GdiDc;
  dc->header.refCount.store(1, std::memory_order_relaxed);
  return static_cast<HDC>(dc);
}

inline BOOL DeleteDC(HDC hdc) {
  auto* header = GetNativeHandleHeader(static_cast<HANDLE>(hdc));
  if (!header || header->kind != NativeHandleKind::GdiDc)
    return FALSE;

  delete static_cast<NativeGdiDcHandle*>(static_cast<HANDLE>(hdc));
  return TRUE;
}

#endif
