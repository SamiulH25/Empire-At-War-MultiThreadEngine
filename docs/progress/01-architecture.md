# Engine Architecture

**Date:** 2026-08-16
**Companion docs:** [00-engine-progress.md](00-engine-progress.md) (status),
[02-subsystems.md](02-subsystems.md) (deep dives), [03-performance.md](03-performance.md)
(measurements), [04-roadmap.md](04-roadmap.md) (next steps).

## Component Map

```
                        ┌─────────────────────────────┐
                        │         Simulation          │
                        │  (the frame loop / tick)    │
                        └──────┬───────┬───────┬──────┘
                               │       │       │
              ┌────────────────┘       │       └────────────────┐
              ▼                        ▼                        ▼
   ┌───────────────────┐   ┌──────────────────┐   ┌──────────────────┐
   │   ScriptManager   │   │  PathfindingSys  │   │   AiDriver       │
   │  (Lua host + all  │   │  (3D grid + A*)  │   │  (perception-    │
   │   PG* bindings)   │   │                  │   │   driven target  │
   └────────┬──────────┘   └──────────────────┘   │   selection)     │
            │                                     └────────┬─────────┘
            ▼                                              ▼
   ┌───────────────────┐                        ┌──────────────────┐
   │  MegaFileManager  │                        │ PerceptionSystem │
   │  (megs + loose)   │                        │ (equation DSL)   │
   └────────┬──────────┘                        └──────────────────┘
            ▼
   ┌───────────────────┐   ┌──────────────────┐
   │     SimState      │   │    JobSystem     │
   │ (players, types,  │◄──┤ (cooperative     │  ← shared by every
   │  objects, forces, │   │  work stealing)  │    parallel phase
   │  planets, events) │   └──────────────────┘
   └───────────────────┘
```

## The Tick (Serial Spine)

`Simulation::tick(dt)` — the fixed-step frame loop everything hangs off
(docs/research/06-threading-design.md's serial section):

1. **Script pump** — advance engine time, resume every Lua script thread,
   fire event callbacks (timers, deaths, attacked, proximities). Serial.
2. **Prune** — remove dead units from taskforces. Serial.
3. **AI step** — for each non-human player, every attack-goal taskforce
   scores enemy candidates through a perception equation (**parallel**),
   picks the best, orders the force. Serial reduce.
4. **Position snapshot** — copy all object positions (makes cross-object
   reads race-free). Serial, cheap.
5. **Pathfinding step** — units whose direct line is blocked request paths;
   all active searches step **in parallel** by per-search budget.
6. **Movement integration** — per-object slices **in parallel**; units walk
   waypoints or beeline, hold position when their target is in weapon range.
7. **Combat** — two-phase **parallel** pass: fire decisions (per-shooter
   slots), then damage application (per-target slots).

## Determinism by Construction

The design rule (from doc 06): **read-shared, write-partitioned**. Every
parallel phase partitions objects/searches into disjoint slots; cross-object
reads go through the per-tick position snapshot. Ties in reductions break by
stable object id. Result: identical outcomes regardless of worker count —
proven by `sim_tool --compare` and by tests that run the same scenario at
different worker counts.

## Data Flow

- **Game data** → `.meg` archives (Petrolution spec) → `MegFile` /
  `MegaFileManager` (load order + loose-file override) → byte blobs.
- **Unit stats** → `UnitDataLoader` parses `SpaceUnit`/`LandUnit` XML →
  `ObjectType` (damage normalized to a health fraction; shield fraction;
  move speed; categories; properties) → `SimState::addType`.
- **Tuning** → `GameConstants::Parse` → `Simulation::configure` →
  pathfinding expansion budget.
- **AI scoring** → `PerceptionSystem::loadEquations` parses the game's
  equation XML → expression ASTs → evaluated per candidate.
- **Scripts** → `ScriptManager` loads Lua source (or the game's bytecode
  string tables for binding inventory) → PG* bindings query/mutate the sim
  through wrapper userdata.

## Threading Model

- One `JobSystem` per `Simulation` (workers = hardware − 1; the caller
  participates).
- `parallel_for` uses cooperative range stealing: a shared atomic index, a
  mutex/CV for completion, **zero per-range allocation**, and a serial fast
  path for tiny workloads (coordination is slower than the work below ~1k
  objects).
- Lua states are **never** shared across threads (one state per manager,
  pumped on the sim thread) — matching the game's model and the research
  finding that Lua is `NOT_NOW` for parallelism.
- The four parallel slices are all pure-read/own-slot: movement, combat,
  pathfinding searches, perception scoring.

## Wrapper Userdata

All engine objects exposed to Lua (objects, players, types, positions,
command blocks, taskforces, planets) share one metatable
(`LuaWrapperMetaTable` — a name from the game's own PGBASE bytecode).
`__index` dispatches by wrapper kind to method tables, extended at runtime
via the `__PgWrapperMethods` registry table (so new binding surfaces register
methods without touching the dispatcher).

## Directory Layout

```
src/
  core/       engine: sim, job system, data readers, bindings, AI
  cli/        tools: meg_tool, gameconfig_tool, sim_tool
  tests/      16 test suites (ctest)
scripts/      research tooling (probe_lua_bindings, binding_gap, ...)
docs/
  research/   the grounded findings (01–07)
  progress/   this series (00–04)
third_party/  vendored Lua 5.1.5
```
