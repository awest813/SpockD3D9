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

The host and the translator are therefore two separable concerns. This decision
settles SpockD3D9's posture toward both: the project stays **native-first**, owns
only the **translator**, and emits an **optional** PE artifact that an external
host can load. It deliberately does **not** commit the project to any specific
Windows host.

## Options considered

| # | Approach | Stance |
|---|----------|--------|
| 1 | External Wine-family host (Wine / CrossOver / GPTK) consuming a SpockD3D9 `d3d9.dll` override | **Supported as a downstream consumer** of the optional PE build — not an officially targeted platform |
| 2 | Custom lightweight wrapper (in-house PE loader + Win32 surface) | Rejected |
| 3 | Native source port of Fallout 3 | Rejected (source unavailable) |

### What SpockD3D9 commits to

* **The native `libdxvk_d3d9.dylib` stays the canonical, supported artifact.**
  Everyone who wants the native stack — direct linkers, SDL/GLFW ports — is
  unaffected by anything below. The default build, dependency graph, and install
  layout do not change.
* **SpockD3D9's role is strictly the translator** (D3D9 → Vulkan). It does not
  reimplement the non-D3D9 Win32 surface; that is squarely out of scope per the
  [ROADMAP](../ROADMAP.md#out-of-scope-default-builds).
* **A Windows-PE `d3d9.dll` is emitted behind an optional, non-default Meson
  target**, so that *some* external host can load SpockD3D9 as a DLL override —
  without perturbing the native path. This is the only new build product the
  decision introduces, and it is opt-in.

### Why hosting is delegated to external hosts (and not committed to one)

* **The host problem is already solved elsewhere.** A Wine-family runtime
  provides a battle-tested PE loader and the entire non-D3D9 Win32 surface
  Fallout 3 needs — DirectInput, DirectSound, filesystem, registry, threading,
  and `HWND` windowing. Every "Non-D3D9 dependencies" row becomes *provided by
  the host* instead of *to be built*. SpockD3D9 should consume that, not rebuild
  it.
* **The DLL-override contract is established.** `WINEDLLOVERRIDES="d3d9=n,b"` is
  exactly how DXVK is consumed today; emitting a PE `d3d9.dll` lets SpockD3D9
  slot into it. We adopt an existing integration shape rather than inventing one.
* **But no single host is blessed.** GPTK's story may keep evolving, and native
  DX9 / DXVK / other shim layers have different trade-offs. Locking the project
  to one host would imply a stronger ecosystem bet than emitting a PE artifact
  actually requires. Wine, CrossOver, and Apple's
  [Game Porting Toolkit](https://developer.apple.com/games/game-porting-toolkit/)
  (Wine + a D3D layer over MoltenVK → Metal) are therefore treated as
  *anticipated downstream consumers* of the optional PE build — illustrative
  examples, not first-class supported platforms.

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
works with Wine." True — but the host is interchangeable, and the *translator*
under it is where SpockD3D9 lives. SpockD3D9's differentiation is precisely
there: MoltenVK loader auto-discovery, `VK_DRIVER_ID_MOLTENVK` tiler heuristics
(`dxvk.tilerMode`), Apple-honest format/MSAA caps, and a D3D9-only build.
Emitting a PE `d3d9.dll` so a host can load that translator does not duplicate
upstream DXVK any more than upstream DXVK duplicates Wine — they occupy
different layers.

## Decision

> **SpockD3D9 remains native-first. The `libdxvk_d3d9.dylib` is the canonical,
> supported artifact, and SpockD3D9's role is strictly the D3D9 → Vulkan
> translator. To enable Windows game hosting without perturbing that, the
> project additionally emits a Windows-PE `d3d9.dll` behind an optional,
> non-default Meson target. Hosting is delegated to external Windows hosts
> (e.g. Wine, CrossOver, Apple's Game Porting Toolkit), which are treated as
> downstream consumers of that optional artifact — no single host is an
> officially targeted platform at this time.**

```
┌──────────────────────────────────┐
│  Fallout 3 (Windows .exe)         │
├──────────────────────────────────┤
│  Any external Windows host        │  PE loader + DirectInput, DirectSound,
│  (Wine / CrossOver / GPTK / …)    │  filesystem, registry, threads, HWND
│      │ d3d9.dll override           │  — not committed to by this project
│      ▼                             │
│  SpockD3D9 (optional PE d3d9.dll) │  D3D9 → Vulkan   ← the translator
├──────────────────────────────────┤
│  host's Vulkan → MoltenVK         │  Vulkan → Metal
├──────────────────────────────────┤
│  Metal (Apple GPU)                │
└──────────────────────────────────┘

(unchanged: native ports link libdxvk_d3d9.dylib directly via SDL/GLFW —
 the default, supported path; none of the above affects it.)
```

## Consequences

**Unaffected (the supported path):** the native `libdxvk_d3d9.dylib` build,
its dependency graph, and its install layout are unchanged. Native ports and
direct linkers see nothing new.

**Unblocked now (this change):**

* A Fallout 3 `dxvk.conf` profile shipped at
  [`tools/fallout3/fallout3.dxvk.conf`](../tools/fallout3/fallout3.dxvk.conf),
  validated in CI against the documented option set
  (`tests/conf/test_dxvk_conf_profiles.py`).
* A host setup / launch guide at
  [`tools/fallout3/README.md`](../tools/fallout3/README.md), framing hosts as
  examples rather than blessed targets.

**New prerequisite this decision creates (the next active task):**

* **Emit SpockD3D9 as an experimental PE `d3d9.dll`.** An external host loads a
  Windows-PE `d3d9.dll`, not a `.dylib`, so one must be produced (MinGW
  cross-compile). The DXVK upstream the fork is based on already supports this
  target; the task is to wire it up as an **optional, opt-in, non-default**
  Meson target — explicitly experimental, and *not* part of the default
  configuration, install layout, or "blessed" build — so it cannot perturb the
  native dependency graph. Tracked in
  [ROADMAP.md](../ROADMAP.md#milestone-f--fallout-3-compatibility).

**Out of scope (provided by whichever host, not SpockD3D9):** DirectInput,
DirectSound / XAudio2, Win32 filesystem, registry, and `HWND` windowing. If a
specific Fallout 3 issue traces to one of these, it is a host bug or
configuration item, not a SpockD3D9 task.

## References

* [Fallout 3 compatibility checklist](FALLOUT3_COMPAT.md)
* [MoltenVK capabilities on macOS](MOLTENVK_CAPABILITIES.md)
* [Apple Game Porting Toolkit](https://developer.apple.com/games/game-porting-toolkit/) — one anticipated downstream consumer, not a committed target
* [DXVK DLL overrides under Wine](https://github.com/doitsujin/dxvk#how-to-use)
