# Subsystem Deep Dives

**Date:** 2026-08-16
**Companion docs:** [01-architecture.md](01-architecture.md) (map),
[03-performance.md](03-performance.md) (measurements).

Each subsystem below lists what it does, how it maps to the research
findings, and where the code lives.

## Job System (`core/job_system.*`)

Cooperative task-stealing thread pool.

- Fixed pool (hardware − 1); caller participates in every parallel call.
- `parallel_for`: ranges pulled from a shared atomic index — **no per-range
  allocation**; a mutex/CV handles completion; serial fast path for ≤8 items
  (coordination dominates tiny workloads).
- `parallel_invoke`: fan-out for heterogeneous work.
- Design provenance: doc 06's "job system over raw threads" + TBB-not-in-exe
  finding (we bring our own pool).

## Lua Host + PG* Bindings (`core/lua_host.*`, `core/pg_*.cpp`)

The documented EAW scripting surface (Alamo Engine Tools), so mod scripts
load and run.

- **Threads** (`pg_bindings.cpp`): `Create_Thread`/`Thread` are callable
  tables (`__call`) with dotted fields (`Thread.Create`, `.Kill`,
  `.Is_Thread_Active`, `.Get_Current_ID`) — matching the game exactly.
  Script threads are coroutines; per-frame pump resumes each once, tracks
  first-resume arg counts, and reaps finished threads.
- **Objects/actions** (`pg_object_bindings.cpp`): `Find_Player`,
  `Find_Object_Type`, `Find_All_Objects_Of_Type`, `Find_First_Object`,
  `Find_Nearest`, `Spawn_Unit`, `Reinforce_Unit`, `Create_Position`, and
  ~40 wrapper methods (Get_Hull/Shield/Health/Owner/Type/Position/Distance,
  Is_Category/Has_Property/Is_Valid/Is_Hero, Move_To, Attack_Target,
  Take_Damage, garrison ops, CommandBlock IsFinished/Result).
- **Events** (`pg_event_bindings.cpp` + `event_system.*`): `Register_Timer`,
  `Register_Death_Event`, `Register_Attacked_Event`, `Register_Prox`,
  `Cancel_Attacked_Event`, `Process_*`, `Pump_Service`. Callbacks are Lua
  registry refs; death callbacks receive the dying object (still readable;
  `Is_Valid()` is the liveness check).
- **Diplomacy**: `Make_Ally`/`Make_Enemy`/`Is_Ally`/`Is_Enemy`/`Get_Enemy`.
  Combat respects relations (no allied fire).
- **Taskforces** (`pg_taskforce_bindings.cpp`): the TaskForceClass surface —
  unit table, force count, stage, plan result, threat, collective
  Move_To/Attack_Target/Garrison that fan out to the force's units.
- **Galactic**: `FindPlanet`, planet wrapper (Get_Name/Get_Owner),
  force `Move_To(planet)` relocation + `Get_Planet`.

The binding inventory (137 engine bindings from the game's own Lua
libraries) is documented in docs/research/05-mod-compatibility.md;
`scripts/probe_lua_bindings.py` + `binding_gap.py` regenerate it.

## Object Model (`core/object_model.*`)

The sim's data: `Player` (relations), `ObjectType` (stats, categories,
properties), `GameObject` (hull/shield/energy, move+path state, combat
state, garrison), `TaskForce` (units, stage, planet), `Planet`. `SimState`
owns the registries (deque-backed so references stay valid) plus all
queries: by type/player/category, nearest, taskforce management, diplomacy,
planets.

## Movement + Pathfinding (`core/path_grid.*`, `core/pathfinding.*`)

- `PathGrid`: 3D voxel grid (x/y/z cells) with Amanatides-Woo DDA
  line-of-sight (visits every cell a segment passes through).
- `PathfindingSystem`: frame-sliced A* — sparse per-search node maps,
  lazy-deletion binary heaps, 6-connected expansion, a z-step cost (prefer
  staying on a plane), per-tick expansion budget (wired from
  `SpacePathfindMaxExpansions` via `Simulation::configure`), total cap,
  LOS waypoint smoothing. All active searches step **in parallel**.
- Sim integration: units whose direct line is blocked request a path; the
  unit walks waypoints; failed searches cancel the move; out-of-grid moves
  beeline (the world is bigger than the grid).

## Combat (`simulation.cpp` runCombat)

Two-phase parallel resolution (doc 06's "per-target slot writes"):

1. **Fire decisions** (parallel, per-shooter slot): range check vs
   `maxRange`, no friendly fire (alliance-aware), cooldown from
   `attackRate`, auto-acquire nearest enemy when the target dies.
2. **Damage application** (parallel, per-target slot): sum incoming shots,
   shield absorbs first, spill to hull, lethal damage flags death events.

## Events (`core/event_system.*`)

- Timers fire after a timeout with a param; death events on alive→dead;
  attacked events on hull drop (edge detection); proximity when an object
  enters range (player filter supported). All pumped after script threads
  each tick.

## Perception (`core/perception.*`)

The game's AI scoring DSL (`AI/PERCEPTUALEQUATIONS/*.XML`):

- Tokenizer + recursive-descent parser: arithmetic, comparisons as 0/1,
  parens, parameterized calls (`Parameter_Category`/`Parameter_Type`),
  `Function_<name>.Evaluate` chaining.
- Queries: Health, Shield, Friendly/Enemy/Force hull sums (category-filtered,
  range-limited), DistanceToNearestFriendly/Enemy (huge no-match sentinel so
  `1000 > dist` fails correctly), IsType, IsDefender, BaseLevel, ContainsHero,
  Game.Age, Game.IsCampaignGame, Location delegation.
- **Verified**: all 34 equations from the real `OFFENSIVESPACEEQUATIONS.XML`
  load; unknown equations/variables evaluate to 0 (game-tolerant).

## AI (`core/ai_targeting.*`, `core/ai_driver.*`)

- `AiTargeting::findTarget`: score every enemy candidate via a perception
  equation (**parallel** across candidates — pure reads), serial max-reduce
  with id-order tie-breaks; parallel ≡ serial (tested).
- `AiDriver::runStep`: per tick, every non-human player's attack-goal
  taskforce picks its target and issues the collective attack order.
  `Set_Plan_Result(true)` stops the force. Wired into `Simulation::tick`.

## Data Loaders (`core/unit_data_loader.*`, `core/game_constants.*`)

- `UnitDataLoader`: `SpaceUnit`/`LandUnit` XML → `ObjectType` — damage
  normalized to a health fraction (7 dmg / 50 health = 0.14), shield
  fraction, fire recharge → rate, range, cost, tech, affiliation,
  categories, properties, ship class, hero. Duplicate names last-wins.
- `GameConstants`: typed pathfinding knobs; `Simulation::configure` applies
  them.

## Battle Runner (`cli/sim_tool.cpp`)

Headless scripted battles (Rebel vs Empire fleets), with optional real game
data (`--game`), fleet-size/worker/tick controls, and `--compare` (1 vs N
workers, asserts identical outcomes, prints speedup). The integration proof
for the whole stack.
