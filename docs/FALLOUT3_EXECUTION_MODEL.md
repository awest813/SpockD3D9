# Execution Model Decision — Hosting Windows D3D9 Games on macOS

**Status:** Accepted (2026-06)
**Scope:** Milestone F (Fallout 3) and the general "Windows D3D9 game hosting" layer
**Supersedes:** the "Execution model" *Decision needed* note in
[FALLOUT3_COMPAT.md](FALLOUT3_COMPAT.md)

This document resolves the first and gating task of Milestone F —
*"Define execution model (wrapper / translation layer)"* — so that the
remaining integration work has a concrete target to build against.

---

## Context

SpockD3D9 ships a **native** D3D9 → Vulkan translation library
(`libdxvk_d3d9.dylib`) that works today for apps that link it directly and
drive windowing through SDL2/SDL3/GLFW (see [README](../README.md)). That layer
is the *translator*.

Fallout 3, however, is an unmodified Windows `.exe`. To run it on macOS,
something must additionally act as the *host*: load and execute the PE binary,
and provide the non-D3D9 Win32 surface the game expects — PE loader, DirectInput,
DirectSound/XAudio2, the Win32 filesystem, registry, threading, and `HWND`
windowing (enumerated in [FALLOUT3_COMPAT.md](FALLOUT3_COMPAT.md#non-d3d9-dependencies)).
SpockD3D9 does not, and should not, reimplement that surface: it is squarely
out of scope per the [ROADMAP](../ROADMAP.md#out-of-scope-default-builds).

The host and the translator are therefore two separable concerns. The decision
below is only about the **host**; the translator is and remains SpockD3D9.

## Options considered

| # | Approach | Verdict |
|---|----------|---------|
| 1 | **Wine-family host + SpockD3D9 as a `d3d9.dll` override** | **Chosen** |
| 2 | Custom lightweight wrapper (in-house PE loader + Win32 shims) | Rejected |
| 3 | CrossOver / Apple Game Porting Toolkit specifically | Folded into #1 as recommended hosts |
| 4 | Native source port of Fallout 3 | Rejected (source unavailable) |

### Why Wine-family hosting (option 1)

* **The host problem is already solved.** Wine provides a battle-tested PE
  loader and the entire non-D3D9 Win32 surface Fallout 3 needs — DirectInput,
  DirectSound, filesystem, registry, threading, and `HWND` windowing. Every one
  of the "Non-D3D9 dependencies" rows becomes *provided by the host* instead of
  *to be built*.
* **It is the proven shape on macOS.** Apple's own
  [Game Porting Toolkit](https://developer.apple.com/games/game-porting-toolkit/)
  is Wine + a D3D translation layer running over **MoltenVK → Metal**. We slot
  SpockD3D9 into the D3D9 slot that GPTK fills with D3DMetal / upstream DXVK.
  CrossOver and vanilla Wine + DXVK on Linux follow the identical pattern. We
  are adopting an established integration contract, not inventing one.
* **It keeps SpockD3D9 doing only what it is good at.** The DLL-override
  contract (`WINEDLLOVERRIDES="d3d9=n,b"`) is exactly how DXVK is consumed today.
  SpockD3D9 stays a drop-in `d3d9` implementation; the host owns everything else.

### Why *not* a custom wrapper (option 2)

A bespoke wrapper would have to grow an in-house PE loader and re-implement the
DirectInput / DirectSound / filesystem / registry / threading / windowing
surface — i.e. re-create the bulk of Wine. That is a multi-year effort,
redundant with a mature project, and a distraction from the translator that is
SpockD3D9's actual value. The Win32 *handle* shims built in Milestone E
(`src/util/util_win32_compat.h`) exist so the **translator** compiles and links
natively; they are deliberately *not* the seed of a general Win32 host, and this
decision keeps that boundary.

### Addressing the "duplicates upstream DXVK" concern

[FALLOUT3_COMPAT.md](FALLOUT3_COMPAT.md) flagged that "upstream DXVK already
works with Wine." True — but Wine is the *host*, and the *translator* under it
is interchangeable. SpockD3D9's differentiation is precisely in the translator:
MoltenVK loader auto-discovery, `VK_DRIVER_ID_MOLTENVK` tiler heuristics
(`dxvk.tilerMode`), Apple-honest format/MSAA caps, and a D3D9-only build. Running
*under* Wine does not duplicate upstream DXVK any more than upstream DXVK
duplicates Wine — they occupy different layers.

## Decision

> **Host Windows D3D9 games on macOS with a Wine-family runtime (vanilla Wine,
> CrossOver, or Apple's Game Porting Toolkit), and consume SpockD3D9 as a
> `d3d9.dll` DLL override. SpockD3D9 translates D3D9 → Vulkan; Wine's
> `winevulkan` + MoltenVK carry Vulkan → Metal.**

```
┌──────────────────────────────┐
│  Fallout 3 (Windows .exe)     │
├──────────────────────────────┤
│  Wine / CrossOver / GPTK      │  PE loader + DirectInput, DirectSound,
│  (the host)                   │  filesystem, registry, threads, HWND
│      │ d3d9.dll override       │
│      ▼                         │
│  SpockD3D9 (PE d3d9.dll)      │  D3D9 → Vulkan   ← the translator
├──────────────────────────────┤
│  winevulkan → MoltenVK        │  Vulkan → Metal
├──────────────────────────────┤
│  Metal (Apple GPU)            │
└──────────────────────────────┘
```

## Consequences

**Unblocked now (this change):**

* A Fallout 3 `dxvk.conf` profile shipped at
  [`tools/fallout3/fallout3.dxvk.conf`](../tools/fallout3/fallout3.dxvk.conf),
  validated in CI against the documented option set
  (`tests/conf/test_dxvk_conf_profiles.py`).
* A host setup / launch guide at
  [`tools/fallout3/README.md`](../tools/fallout3/README.md).

**New prerequisite this decision creates (the next active task):**

* **Build SpockD3D9 as a PE `d3d9.dll`.** The native default build emits a
  `.dylib`; a Wine DLL override requires a Windows-PE `d3d9.dll`
  (MinGW cross-compile). The DXVK upstream the fork is based on already supports
  this target; the task is to re-introduce it as an **optional, non-default**
  Meson configuration (`enable_d3d9` PE output) without disturbing the native
  dylib that the rest of the project depends on. Tracked in
  [ROADMAP.md](../ROADMAP.md#milestone-f--fallout-3-compatibility).

**Out of scope (provided by the host, not SpockD3D9):** DirectInput,
DirectSound / XAudio2, Win32 filesystem, registry, and `HWND` windowing. If a
specific Fallout 3 issue traces to one of these, it is a host (Wine) bug or
configuration item, not a SpockD3D9 task.

## References

* [Fallout 3 compatibility checklist](FALLOUT3_COMPAT.md)
* [MoltenVK capabilities on macOS](MOLTENVK_CAPABILITIES.md)
* [Apple Game Porting Toolkit](https://developer.apple.com/games/game-porting-toolkit/)
* [DXVK DLL overrides under Wine](https://github.com/doitsujin/dxvk#how-to-use)
