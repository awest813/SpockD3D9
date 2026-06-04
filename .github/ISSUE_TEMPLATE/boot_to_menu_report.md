---
name: Boot to menu report (Fallout 3 / hosted)
about: Report V1–V4 progress running a Windows D3D9 title through SpockD3D9 PE d3d9.dll
title: "[boot] "
labels: boot-to-menu
---

## Title and host

- **Game:** Fallout 3 / Fallout NV / other: 
- **Store / version:** 
- **macOS version:** 
- **Mac model / chip / GPU:** 
- **MoltenVK version:** (`brew info molten-vk`)
- **Host:** Wine / CrossOver / GPTK / other (version): 
- **SpockD3D9 commit:** 

## Milestone reached

- [ ] V1 — `DXVK:` line in log (override loaded)
- [ ] V2 — Adapter enumeration
- [ ] V3 — `D3D9: CreateDeviceEx OK` in log
- [ ] V4 — Main menu visible and interactive

## Setup

```text
# How you installed the override (prepare script / manual copy):
```

## `check-boot-logs.sh` output

```text
./scripts/check-boot-logs.sh ...
```

## Logs

Attach `fallout3-spockd3d9.log` or paste relevant excerpts (`DXVK_LOG_LEVEL=info` minimum; `debug` if failed before V3).

## What you see

Describe screen behavior (black screen, crash timing, menu if reached).

## Profile

- [ ] `tools/fallout3/fallout3.dxvk.conf` (unmodified)
- [ ] Custom edits (describe):
