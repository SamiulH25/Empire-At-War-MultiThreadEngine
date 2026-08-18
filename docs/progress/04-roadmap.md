# Roadmap

**Date:** 2026-08-17 (refresh of the 2026-08-16 original)
**Companion docs:** [00-engine-progress.md](00-engine-progress.md),
[01-architecture.md](01-architecture.md), [02-subsystems.md](02-subsystems.md),
[03-performance.md](03-performance.md), [05-whats-missing.md](05-whats-missing.md).

## Where the Project Is

The portable engine core reached its first milestone: deterministic
multithreaded battles on real game data, driven by the documented Lua mod
surface, with perception-driven AI. Tier 1 (land combat, hyperspace, full
rosters, economy, multi-stage AI plans) and the mod loader are **DONE**; the
mod-compat harness has real script *execution* (`mod_tool run`). The next
work splits into "finish the simulation" and "take it back to the real game"
— see [05-whats-missing.md](05-whats-missing.md) for the live handoff list.

## Tier 1 — Finish the Simulation (DONE)

1. **Land combat + land data** — `LandUnit` XML loading, ground battles
   (terrain in the path grid, infantry squads, garrisons).
2. **Hyperspace travel** — galactic fleets in transit (travel time, arrival
   events) instead of instant relocation; `Launch_Units` / `Land_Units`
   force ops.
3. **Richer unit families** — corvettes/frigates/supers from
   `SPACEUNITSCORVETTES.XML`, `SPACEUNITSFRIGATES.XML`, `SPACEUNITSSUPERS.XML`
   so fleet composition is real.
4. **Economy** — player credits, `Give_Money`, tech levels, `Unlock_Tech`,
   build costs into force production (the AI's `Produce_Force`).
5. **Multi-stage AI plans** — taskforce stages (Get_Stage/Set_Stage) driving
   plan phases (assemble → move → attack → retreat) with the event/timer
   system, using the real `AI_GOALS`/`GOALFUNCTIONS` XML.

## Tier 2 — Mod Compatibility Proof (partially done)

6. **Mod loader CLI** — **DONE.** `mod_tool scan|battle|galaxy|bindings|run`
   point at a mod's `Data` folder (Thrawn's Revenge, EAW Remake, Republic at
   War): MegaFileManager load order + loose-file override, unit XML, Lua
   scripts.
7. **Script compatibility harness** — **DONE (2026-08-18).** `mod_tool run`
   loads a real mod script (loose file or meg entry) through
   `ScriptManager::loadScript` and pumps it for N ticks, reporting runtime
   errors; exit code 2 flags scripts that need a documented-but-missing
   binding. The runtime helper gap (FindTarget, FindDeadlyEnemy,
   DebugMessage, TestValid, Project_By_Unit_Range, EvaluatePerception,
   PlayerSpecificName, ScriptExit, BlockOnCommand, GameRandom.Get_Float) is
   closed: all four library scripts (PGTASKFORCE, PGEVENTS, PGSPAWNUNITS,
   PGSTATEMACHINE) load and pump cleanly, and `mod_tool bindings` reports
   **0 documented-but-missing** for Thrawn's Revenge.

## Tier 3 — Back to the Real Game

8. **Patch DLL** — the original hybrid strategy: d3d9 proxy + MinHook on
   the sim tick (`FUN_14025ca30`), proving the parallel slices against the
   real exe. The research phase (docs/research/01, 06) already mapped the
   hook surface; the job system and parallel patterns are proven here.
   Next: validate the hook offsets against the real exe (layout drift) and
   wire the hooked tick into `ParallelTick`/`JobSystem`.
9. **Bytecode compat** — the game ships custom-fork Lua bytecode (doc 03).
   **Header reverse-engineered (2026-08-18):** the fork uses magic `\x1bLup`
   (not `\x1bLua`), version 0x51 (Lua 5.1), and a 6-byte `Instruction`
   (vanilla: 4). A loader for this dialect would let the engine run the
   game's own AI scripts (`scripts/dump_bytecode_header.py` + doc 03).

## Tier 4 — Engine Completeness

10. **Engine fidelity gaps** — **DONE (2026-08-18).** The full documented
    binding surface is now registered: free store / squads (`Get_Free_Store`,
    `Get_Units_In_Free_Store`), pathing helpers (`Find_Path` / `Get_Path` /
    `Is_Path_Blocked` — direct-line approximation), hero/unique
    (`Set_Hero` / `Is_Unique` / `Get_Unique_ID`), planet-owner setters,
    force setters, `Get_Targeting_Priorities`, `Get_Number_Of_Forces`,
    `Get_Player_Count`, and player-level `Set_Faction`. `mod_tool bindings`
    reports 0 documented-but-missing. Covered by `script_helper_tests`
    (30 checks).
11. **Determinism guard-rails** — a repeat-run byte-identical battle test
    (`simulation_tests`) runs the same battle at 1/2/4 workers and asserts
    identical serialized state. A per-tick state-hash "verify mode" is a
    possible next step.
12. **Render/audio** — out of scope for the headless engine; a renderer is
    a separate milestone (the design spec says new renderer after the patch
    phase proves value). This is the eventual visualization layer for
    battles run by the engine.

## Suggested Order

The narrative arc: **Tier 3 (8–9)** takes the engine back to the real game
(the patch DLL is the original project goal; bytecode compat unlocks the
game's own AI); **Tier 4 (10–11)** fills the remaining fidelity gaps that
mod scripts depend on.

## Definition of Done for the Next Milestone

- `mod_tool run` executes the common AI library scripts (PGTASKFORCE,
  PGEVENTS, PGSPAWNUNITS, PGSTATEMACHINE) without runtime errors.
- `d3d9.dll` drops into the real game folder and the parallel tick is
  active in the real exe.
- The game's own bytecode (from `config.meg`) loads and runs in the engine.
- Every new subsystem keeps the determinism invariant (the repeat-run test
  is part of `ctest`).
