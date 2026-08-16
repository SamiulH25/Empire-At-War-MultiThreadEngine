# 06 — Threading Design Proposal

**Status:** Grounded in findings (2026-08-16) — static + dynamic analysis complete
**Last updated:** 2026-08-16

> This document is the destination. Everything else feeds it. Current content is
> grounded in the confirmed findings from Phase 1 (static) and Phase 2 (dynamic).

## Current State Assessment (confirmed)

- **The game is single-core-bound.** Live measurement (Phase 2): main thread 40–75% of
  one core during battle, total process under 1 core on 16 cores. Massive headroom.
- **One persistent secondary worker** exists (TID 15312/7704, ~6–15% CPU, audio/IO
  likely) — not a sim worker. No thread scaling with battle load.
- **Sim tick** (`FUN_14025ca30`): iterates 6 object lists, calls vtable+0x50 Update(dt)
  per object, fixed-step accumulator. **Not mutex-gated** — single-threaded by design.
- **Mutexes** (`ThreadLockMutexClass`, Windows mutex, `WaitForSingleObject` 10s timeout):
  15 call sites, packet handler + manager cluster. Core sim is not locked.
- **Lua**: embedded 5.x, coroutine-based "threads" (`LuaThreadTable`, `LuaCreateThread`),
  one state per script manager at +0x58. No OS threads for Lua.
- **TBB is NOT in the game exe** — only in swfoc.exe (launcher).
- **Data formats are documented** (Petrolution .meg spec, Alamo Engine Tools Lua/XML) —
  the reimplementation can be built against documented formats, not RE'd ones.

## Design Principles

1. **Determinism first** — RTS sims must produce identical results regardless of thread
   scheduling, or replays/multiplayer break. Any parallel sim work needs deterministic
   merge (per-unit results written to per-unit slots, no cross-unit writes during a tick).
2. **Data-oriented partitioning** — partition by unit/object ID ranges, not by subsystem
   per-thread; keeps caches warm and merge steps simple.
3. **Read-shared, write-partitioned** — spatial queries (map, unit grid) are read-shared
   (immutable during a tick); per-unit state is written only by the owning worker.
4. **Job system over raw threads** — a task-stealing job system gives load balancing for
   free vs. fixed thread-per-subsystem. (TBB is NOT in the exe, so a patch DLL would bring
   its own — e.g. a small std::thread-based pool, or link TBB into the proxy.)
5. **Frame boundary sync** — all parallel work completes before the frame's serial section
   (input, Lua event dispatch, render submission).

## Candidate Job Graph (per tick)

```
Serial: input poll → Lua event dispatch (global state) → AI phase 1 (decisions)
    │
Parallel fan-out (job system):
    ├── Perception evaluation per unit group   (PARTIALLY safe — see below)
    ├── Pathfinding continuation per unit group (frame-sliced; budget-tunable)
    ├── Movement integration per unit group
    ├── Combat resolution accumulation (per-target slot writes)
    └── Particle/sound update
    │
Serial barrier: merge + apply results
    │
Serial: render command build → D3D9 present
```

## Parallelism Verdicts (grounded)

| Subsystem | Verdict | Evidence |
|---|---|---|
| Perception evaluation | **PARTIALLY NOW** | 11 callbacks decoded: pure math + read-only string lookup are thread-safe; token-matcher reads shared state; megafile ptr shared. Needs read-only contract or lock (doc 01 — Perception Interop Map) |
| Sim object lists (6 lists in `FUN_14025ca30`) | **AFTER_REFACTOR** | Per-object vtable+0x50 Update(dt); lists are per-manager, not mutex-gated. Partitioning by list or object-range feasible; must verify no cross-object writes |
| Pathfinding | **NOW (tunable)** | `SpacePathfindMaxExpansions`/`FrameDelayDelta` are XML knobs (GameConstants.xml) — budget is data-driven; per-unit pathfinding is independent work |
| AI decision-making (Lua) | **NOT_NOW** | Coroutines share one Lua state (+0x58); Lua states aren't thread-safe. Would need per-player state or serialized Lua |
| Particle/sound | **LIKELY** | Not yet mapped in detail; classic embarrassingly parallel |
| Render | **NOT_NOW** | D3D9 single-threaded API; command-list building could parallelize later |
| Load/decompress | **ALREADY** | `LoadThread` exists (LoadingThreadClass) — meg decompress on background thread confirmed |

## Patch Phase Architecture (Phase 1 — DLL)

- Inject a DLL into `StarWarsG.exe` (d3d9.dll proxy — game imports `Direct3DCreate9`,
  1 import, confirmed in doc 01)
- Hook the sim tick (`FUN_14025ca30`, offset `0x25ca30`) — per-frame, called every tick
- Replace serial subsystem loops with job-system dispatch (bring our own job pool — TBB
  not present in exe)
- **First parallel slice candidate: perception evaluation** (documented callbacks,
  PARTIALLY thread-safe) or **pathfinding** (data-driven budget, independent per unit)
- Keep original code paths for everything untouched — opt-in parallelization

## Reimplementation Phase (Phase 2 — later)

- C++20 engine core with the job system built in from day one
- **Built against documented formats**: .meg (Petrolution spec), XML schema, Lua API
  (Alamo Engine Tools docs) — not RE'd formats
- Keep Lua as the scripting layer (mod compat); keep XML data pipeline (mod compat)
- New renderer (Vulkan or D3D11/12) — after patch phase proves value

## Open Design Questions

1. ~~What tick rate does the sim run at?~~ — fixed-step accumulator confirmed in
   `FUN_14025ca30`; exact Hz still to measure (2006: 30 Hz sim / unlocked FPS)
2. What's the per-tick cost breakdown? (needs WPR hotspot profile — WPT install pending)
3. ~~Does the engine spawn worker threads?~~ — one persistent worker (audio/IO), loading
   thread; no sim workers
4. ~~What does TBB in swfoc.exe do?~~ — launcher internals only; not in the game process
