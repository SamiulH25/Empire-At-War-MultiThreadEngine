# Engine Progress Report

**Date:** 2026-08-16
**Status:** Core engine complete — playable headless battles on real game data
**Branches:** `main` (pushed)

## TL;DR

The portable EAW engine core is feature-complete for its first milestone: a
deterministic, multithreaded simulation that loads **real game data** (.meg
archives, unit XML, GameConstants, perception equations), runs **scripted
battles** through the Lua mod surface, and parallelizes four subsystems
(movement, combat, pathfinding, perception) with proven determinism across
worker counts.

## What Exists Now

| Area | Status | Evidence |
|---|---|---|
| Data layer | **Complete** | .meg reader + MegaFileManager (load order, loose override), XML parser, GameConstants typed loader, unit XML → ObjectType stats |
| Job system | **Complete** | Cooperative work-stealing pool, zero-allocation `parallel_for`, serial fast path |
| Lua mod surface | **Substantial** | Threads, objects, actions, events, diplomacy, taskforces, galactic bindings — the documented PG* API |
| Simulation | **Complete** | Fixed-step tick: scripts → AI → movement → pathfinding → combat |
| Pathfinding | **Complete** | 3D voxel grid, frame-sliced A*, parallel across searches, LOS smoothing |
| Combat | **Complete** | Two-phase parallel firing + damage accumulation, shields, cooldowns, auto-acquire |
| Events | **Complete** | Timers, death/attacked/proximity callbacks, per-frame pump |
| Taskforces | **Complete** | AI unit groups with collective orders + galactic movement |
| Galactic mode | **Embryonic** | Planets, fleet relocation, `FindPlanet` |
| Perception | **Complete** | Game's equation DSL parsed (34 real equations load), evaluated against sim |
| AI | **Complete** | Perception-driven targeting loop, parallel candidate scoring |
| Battle runner | **Complete** | `sim_tool` headless battles, `--compare` determinism + speedup demo |
| Tests | **16 suites, all green** | 14 core suites + 2 data suites, clean rebuild |

## Key Numbers

- **~8,000 lines** of tracked C++/Python (core ~4.5k, tests ~2.6k, tools ~450)
- **16 test suites**, 100% passing on a clean rebuild
- **Deterministic battles**: 1 vs 15 workers produce byte-identical outcomes
  (same ticks, shots, survivors)
- **2.39x speedup** at 1,520 units/side (multi vs single worker)
- **34 real perception equations** load from `OFFENSIVESPACEEQUATIONS.XML`
- **14 real fighter types** load from `SPACEUNITSFIGHTERS.XML`

## Verification Chain

Every major subsystem has a test suite; the battle runner (`sim_tool
--compare`) is the integration proof: it boots the same battle with 1 worker
and with hardware-1 workers and asserts identical outcomes, then reports the
speedup. That single command exercises the whole stack — data loading, Lua
scripting, movement, pathfinding, combat, events, and the job system.

## The Story So Far

1. **Research phase** (docs/research/): binary map, .meg format, Lua surface,
   threading design — the grounding for everything.
2. **Portable core**: job system, Lua host, data readers.
3. **Simulation**: object model, frame loop, parallel movement.
4. **Mod surface**: PG* bindings — threads → objects → actions → events →
   diplomacy → taskforces → galactic.
5. **Data fidelity**: unit XML loader, GameConstants wiring, perception DSL.
6. **AI**: perception-driven targeting that actually fights.
7. **Proof**: `sim_tool` — deterministic, faster, on real data.

## How to Run

```sh
# Build (Windows, MinGW + Ninja)
cd src
cmake -B build -G Ninja
cmake --build build

# Tests
cd build && ctest

# Battle (hand-tuned defaults)
sim_tool battle --fighters 12 --capitals 2

# Battle with real game unit stats (extract the XMLs from config.meg first)
sim_tool battle --game <dir-with-Data\*.XML>

# Determinism + speedup demo
sim_tool battle --compare --fighters 700 --capitals 60
```

See [01-architecture.md](01-architecture.md) for the component map,
[02-subsystems.md](02-subsystems.md) for deep dives,
[03-performance.md](03-performance.md) for the measurements, and
[04-roadmap.md](04-roadmap.md) for what's next.
