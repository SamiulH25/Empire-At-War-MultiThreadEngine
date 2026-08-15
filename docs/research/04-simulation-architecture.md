# 04 — Simulation Architecture

**Status:** Hypotheses only — static/dynamic analysis pending
**Last updated:** 2026-08-15

## Known Architecture Elements (from binary strings + community knowledge)

### Game Loop
The 2006 original ran a classic single-threaded RTS loop:
1. Input processing
2. Lua script updates (AI + events)
3. Simulation tick (movement, combat, perception)
4. Render
5. Frame sync (fixed tick rate for sim)

The 64-bit port has added at least a `LoadThread` and mutex primitives — the loop itself
may still be single-threaded. **This is the central question of the research.**

### Perception System (PerceptionFunctionG.dll)
The engine's line-of-sight/sensor system is a **separate DLL with a formal callback surface**.
It evaluates "perception functions" (scripted queries over tokens like `IsFriendly`,
`DistanceLessThan`, etc.) against a `PerceptionContextClass`. This is significant because:

- It's already modularized — the engine calls into it via function pointers
- If perception evaluation is pure (context in, result out), it's a prime candidate for
  parallel evaluation across units
- The callback-based design suggests Petroglyph already separated it for perf reasons

### AI Layer
Lua-driven, with `LuaCreateThread` (coroutines?). AI decisions (build queue, attack orders)
are scripted in Lua and run per-player.

### Combat/Movement
Part of the C++ sim tick (not exposed via the DLL). Details unknown until disassembly.

## Parallelization Hypotheses (to test)

| Subsystem | Parallelizable? | Reason |
|---|---|---|
| Perception evaluation | Likely | Pure context->result, already isolated in a DLL |
| AI decision-making | Maybe | Per-player independent, but Lua state sharing is a hazard |
| Pathfinding | Maybe | Per-unit independent, but spatial queries need read-shared map data |
| Particle updates | Likely | Classic embarrassingly parallel work |
| Render command building | Maybe | D3D9 is single-threaded API, but command lists can be built in parallel |
| Load/decompress | Likely | `LoadThread` already exists; megas could decompress on workers |

## What Static Analysis Must Find

1. The main loop structure — where each phase is called
2. Where mutexes are taken — what they guard
3. What `LoadThread` does exactly
4. The sim tick function(s) and their data dependencies
5. Memory layout of unit/object arrays (SOA vs AOS) — critical for cache behavior and
   parallel partitioning

## Dynamic Analysis Plan (Wine or Windows)

1. Attach debugger, break on main loop, count threads
2. Sample CPU profiles during heavy battle (many units)
3. Watch which thread spends time where
4. Verify sim tick rate and frame pacing
