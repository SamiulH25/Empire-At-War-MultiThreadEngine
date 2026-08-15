# 06 — Threading Design Proposal

**Status:** Skeleton — fills in as research findings land
**Last updated:** 2026-08-15

> This document is the destination. Everything else feeds it. Current content is the
> working hypothesis; each section gets confirmed or revised as analysis progresses.

## Current State Assessment

The 64-bit remaster already contains threading primitives (`ThreadLockMutexClass`,
`LoadThread`, TBB in the FoCs launcher). The original 2006 game was effectively
single-threaded. So the question is not "add threading from zero" but **"extend partial
threading into a real job-based parallel engine."**

## Design Principles

1. **Determinism first** — RTS sims must produce identical results regardless of thread
   scheduling, or replays/multiplayer break. Any parallel sim work needs deterministic
   merge (per-unit results written to per-unit slots, no cross-unit writes during a tick).
2. **Data-oriented partitioning** — partition by unit/object ID ranges, not by subsystem
   per-thread; keeps caches warm and merge steps simple.
3. **Read-shared, write-partitioned** — spatial queries (map, unit grid) are read-shared
   (immutable during a tick); per-unit state is written only by the owning worker.
4. **Job system over raw threads** — TBB is already in-process; a task-stealing job system
   gives load balancing for free vs. fixed thread-per-subsystem.
5. **Frame boundary sync** — all parallel work completes before the frame's serial section
   (input, Lua event dispatch, render submission).

## Candidate Job Graph (per tick)

```
Serial: input poll → Lua event dispatch (global state) → AI phase 1 (decisions)
    │
Parallel fan-out (job system):
    ├── Perception evaluation per unit group
    ├── Pathfinding continuation per unit group
    ├── Movement integration per unit group
    ├── Combat resolution accumulation (per-target slot writes)
    └── Particle/sound update
    │
Serial barrier: merge + apply results
    │
Serial: render command build → D3D9 present
```

## What Blocks Parallelism (known + suspected)

| Blocker | Detail | Mitigation |
|---|---|---|
| Lua state sharing | AI coroutines in one state | Per-state ownership; or one Lua VM per AI player if the engine supports it |
| `ThreadLockMutexClass` usage | Unknown what it guards | Static analysis must map every call site |
| D3D9 single-threaded | Render API is serial | Build command buffers in parallel, submit serially |
| Perception callbacks into engine | DLL calls back via 11 fn pointers | Verify callback thread-safety; possibly re-entrant guards |

## Patch Phase Architecture (Phase 1 — DLL)

- Inject a DLL into `StarWarsG.exe` (method TBD: d3d9 proxy is classic, but the game
  imports `d3d9.dll` by name with 1 function — easy proxy candidate)
- Hook the sim tick entry point (address found in static analysis)
- Replace serial subsystem loops with job-system dispatch
- Keep original code paths for everything untouched — opt-in parallelization
- Ship as `d3d9.dll` proxy or `winmm.dll` proxy in the game folder

## Reimplementation Phase (Phase 2 — later)

- C++20 engine core with the job system built in from day one
- Keep Lua as the scripting layer (mod compat)
- Keep XML data pipeline (mod compat)
- New renderer (Vulkan or D3D11/12) — but this phase is after patch phase proves value

## Open Design Questions

1. What tick rate does the sim run at? (2006: 30 Hz sim / unlocked FPS)
2. What's the per-tick cost breakdown? (profile: perception vs pathfinding vs combat vs AI)
3. Does the engine already spawn worker threads in the 64-bit port? If yes, what do they do?
4. What does TBB in swfoc.exe actually do — just launcher internals, or does it coordinate
   game threads?
