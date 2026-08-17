# What's Still Missing — Session Handoff

**Date:** 2026-08-17 (refresh of the 2026-08-16 original)
**Companion docs:** [00-engine-progress.md](00-engine-progress.md),
[01-architecture.md](01-architecture.md), [02-subsystems.md](02-subsystems.md),
[03-performance.md](03-performance.md), [04-roadmap.md](04-roadmap.md).

This is the working handoff list. The roadmap doc's Tier 1 items (land combat,
hyperspace, full rosters, economy, multi-stage AI plans) are DONE; the new
"missing" list is below, ordered by value. Everything builds with
`cmake --build build` from `src/`; all 26 test suites pass via `ctest`.

## 1. Script Compatibility Harness (Tier 2 finish)

The execution harness exists and **the common AI library scripts now load
and run**: `mod_tool run <mod_dir> <script-path> [--game <dir>] [--ticks N]`
mounts a mod (loose files + megas), loads a script through
`ScriptManager::loadScript`, and pumps it with the engine's real runtime
backbone (thread pump + `Pump_Service`) for N ticks. Verified against
Thrawn's Revenge:

- `DATA\SCRIPTS\LIBRARY\PGEVENTS.LUA` — loads, `require("PGTaskForce")`
  resolves through the mounted file manager, and all its handlers define.
- `PGTASKFORCE`, `PGSPAWNUNITS`, `PGSTATEMACHINE` — load and pump cleanly.

Supporting work:

- **Script module loader** — `ScriptManager` installs a `package.loaders`
  entry resolving `require("PGTaskForce")` → `DATA\SCRIPTS\LIBRARY\
  PGTASKFORCE.LUA` (loose files override meg entries), matching the game's
  module convention.
- **Native binding module stubs** — `require("PGAICommands")` /
  `require("pgcommands")` / `require("PGBaseDefinitions")` return an empty
  table (the game registers these as hardcoded C++ modules, not Lua files),
  so the library scripts define their functions. Calling a stub member
  still errors — that's the honest signal for unimplemented bindings.

What still needs closing:

- The scripts reference global helpers (`FindTarget`, `DebugMessage`,
  `TestValid`, `Project_By_Unit_Range`, `EvaluatePerception`,
  `PlayerSpecificName`, `ScriptExit`, `FindDeadlyEnemy`, `BlockOnCommand`,
  `GameRandom.Get_Float`, ...) that are only called at runtime, not load
  time — calling any of them errors today. Implementing the commonly used
  ones is the next gap.
- `Register_Timer`/`Process_Timers`/`Pump_Service` and the thread pump
  (`pumpThreads` in `pg_bindings.cpp`) are the runtime backbone — script
  execution is already driven through them, not one-shot `runScript`.

## 2. Bytecode Compatibility (Tier 3)

The game ships custom-fork Lua 5.1 bytecode (research doc 03). The
`real_lua_test` smoke test proves *config.meg bytecode loads* in our vendored
Lua 5.1 host for simple chunks, but the real AI plans use a custom bytecode
format (different opcode numbering / header). Full compat means:

- A loader for the game's bytecode dialect (reverse the `luaU_undump` header
  from a real `config.meg` chunk — `test_real_lua.cpp` has the harness).
- Then run the game's *own* AI scripts, not just mod source.
- This is the highest-leverage piece for "run the real game's AI in our
  engine."

## 3. Patch DLL Against the Real Exe (Tier 3)

The proxy + hook infrastructure exists (`src/proxy/d3d9.cpp` — d3d9 proxy
DLL, MinHook on `StarWarsG.exe+0x25ca30`, validated with `fake_game.exe`
tests). What's missing:

- Test against the real game exe (the hook offsets are from Ghidra docs
  `ghidra/simtick_decompile.txt`; layout drift may need re-validation).
- Wire the hooked sim tick into `ParallelTick`/`JobSystem` with the
  deterministic slices.
- Deliverable: drop `d3d9.dll` into the game folder and see the parallel
  tick active in the real game.

## 4. Determinism Guard-Rails

The determinism invariant is enforced by construction (per-slot writes),
verified by `sim_tool --compare`, and now guarded by a **repeat-run
byte-identical test** (`simulation_tests`): the same battle runs at 1, 2,
and 4 workers for 360 ticks and asserts identical serialized state (positions,
hull, shield, cooldowns, orders, alive flags).

Still possible:

- A "verify mode" that hashes per-tick state and checks worker counts agree
  — catches nondeterminism from future subsystems earlier than the
  end-of-battle comparison.

## 5. Render/Audio (Tier 4, out of scope for headless)

The design spec says a new renderer comes after the patch phase proves
value. Nothing to do now; note in the roadmap that this is the eventual
visualization layer for battles run by the engine.

## 6. Engine Fidelity Gaps (Tier 4)

Progress this session:

- **Targeting priority tables** — `Set_Targeting_Priorities` /
  `Set_Land_AI_Targeting_Priorities` now feed the AI targeting loop:
  `AiTargeting::findTarget` applies the priority order as a deterministic
  score tie-break, and a `Variable_Self.IsTargetingPriority` perception
  query lets mod equations consume the tables directly.
- **Formations** — `Set_Formation` / `Formation_Attack` / `Formation_Move`
  are implemented (squad behavior: members follow the leader each tick,
  attack/move fan out; the sim dissolves formations whose leader dies).
- **`Set_Hull` / `Set_Shield` / `Add_Hull` / `Add_Shield` / `Apply_Damage`**
  — implemented (Apply_Damage: shields absorb first, then hull; respects
  invulnerability).
- **`Get_Targeting_Priorities`** getter — still missing.

Remaining gaps:

- **Free store / squads** — `Get_Free_Store`, `Get_Units_In_Free_Store`
  documented, not implemented.
- **Pathing helpers** — `Find_Path`, `Get_Path`, `Is_Path_Blocked`.
- **Hero / unique** — `Set_Hero`, `Is_Unique`, `Get_Unique_ID`.
- **Planet/force setters** — `Set_Faction`, `Get_Current_Planet`,
  `Set_Current_Planet`, `Get_Planet_Owner`/`Set_Planet_Owner`,
  `Get_Planet_Faction`/`Set_Planet_Faction`, `Set_Force_Planet`,
  `Get_Force_Player`, `Set_Force_Player`, `Get_Number_Of_Forces`,
  `Get_Player_Count`.
- **`Get_Faction`/`Get_Force`** — implemented for objects; verify
  player-level `Get_Faction` semantics match the game.

## 7. Docs Update

**DONE.** The roadmap doc (04) marks Tier 1 complete, the mod loader done,
and re-scopes Tiers 2–4 to the list above. This doc is the live handoff list.

## How to Verify Progress

- `cmake --build build` in `src/` (MinGW), `ctest` in `src/build`.
- `mod_tool scan|battle|galaxy|bindings|run <mod_dir> <script> --game
  <game_dir>` against Thrawn's Revenge (installed at
  `C:\Program Files (x86)\Steam\steamapps\workshop\content\32470\1156943126`,
  game at `C:\Program Files (x86)\Steam\steamapps\common\Star Wars Empire at War`).
- `sim_tool battle --compare` for the determinism/speedup demo.
