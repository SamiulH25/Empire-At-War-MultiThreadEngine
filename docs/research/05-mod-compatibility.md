# 05 — Mod Compatibility Contract

**Status:** Ecosystem documented — data surface cataloged (2026-08-16)
**Last updated:** 2026-08-16

## Why This Matters

The hybrid strategy has two phases. Phase 1 (patch DLL) must not break mods.
Phase 2 (reimplementation) must load them. Either way, we need the exact contract.

## The Modding Mechanism (observed + documented)

1. **Megafiles** hold game data (XML configs, maps, models, textures, shaders)
2. **Loose files** in `Data/` override megafiles — confirmed by the presence of
   `GameData/Data/*.txt` files (ship name lists) sitting next to the megas
3. **Mod folders** — the 2006 game used a `Mods/` folder; the 64-bit remaster directory
   has none by default, but Steam Workshop mods install there (path TBD on this install)
4. **`MegaFiles.xml`** (note capital M/F — confirmed in the binary census) defines load
   order; mods may supply their own. Per the Petrolution docs: all megas listed are loaded
   and merged into a **Master File Table**; a file in a later meg overrides earlier ones
5. **Lua scripts** live inside `config.meg` (or loose `Data/Scripts/...`) and drive AI

## The Documented Ecosystem (key resources)

| Resource | What it provides |
|---|---|
| **Petrolution mod-tools** (`modtools.petrolution.net`) | Official .meg format spec (3 formats, CRC-32 file table, optional AES-128 encryption w/ known keys), **Mega File Editor** (GUI) |
| **petro-meg** (GitHub, MIT) | Rust library + CLI for reading/writing meg files |
| **MichaelHinrichs/Megafile-Extractor** (GitHub) | C# extractor for EAW + other Petroglyph games |
| **Alamo Engine Tools** (`aet-sweaw-modding.readthedocs.io`) | Complete Lua library reference (PGBase, PGCommands, PGAICommands, PGMoveUnits, PGStateMachine, PGStoryMode...), XML tag reference, VS Code extension with schema intelligence, ModCheck tool |
| **Modding Hub Wiki** (focumentation) | GameConstants AI settings, AI guide, coding/scripting reference |

This means the **data formats are documented, not RE'd** — a huge de-risking for the
reimplementation phase.

## The Data Surface (from `config.meg`, 2026-08-16)

Full inventory: [config-meg-catalog.md](config-meg-catalog.md)

| Category | Count | Size | Contents |
|---|---|---|---|
| XML | 627 | 15.5 MB | Gameplay config: AI goals, units, factions, GameConstants |
| Lua | 324 | 1.7 MB | AI plans, build orders, story scripts |
| Text | 93 | 219 KB | Campaign dialog |
| Other | 2 | 397 KB | GUI resources |

Key config files confirmed present: `DATA\XML\GAMECONSTANTS.XML` (pathfinding/AI knobs),
`AITESTGAMECONSTANTS.XML`, `HYPERGAMECONSTANTS.XML`.

## Key Mods to Verify Against

| Mod | What it does | Compatibility critical |
|---|---|---|
| Thrawn's Revenge | Era-based GC overhaul, hundreds of units, custom AI | Huge unit counts → sim perf matters; custom Lua AI |
| EAW Remake | Full visual/gameplay remake | Replaces most assets; ships own exe wrapper historically |
| Republic at War | Clone Wars era conversion | Custom maps, GC |
| Battlefront Commander | BF-style ground focus | Large unit counts |

## Contract Elements to Preserve

1. **XML schema** — every tag/attribute mods use must parse identically
2. **Lua API** — every registered C++ binding mods call must exist with same semantics
3. **Loose-file override** — same precedence rules
4. **.meg reading** — same entry lookup and load order (spec now documented)
5. **Mod folder discovery** — same paths the game searches
6. **64Patch.meg** — this is new in the remaster; its override position in the load order
   matters for both vanilla and mods

## Research Tasks

1. ~~Document .meg format~~ — DONE (Petrolution spec + our reader)
2. ~~Catalog the game data surface~~ — DONE (config.meg: 627 XML, 324 Lua)
3. ~~Inventory the engine binding surface from the game's own Lua libraries~~ — DONE (2026-08-16, see below)
4. Extract a mod's XML and Lua, catalog which engine bindings it uses — pending (needs a workshop mod installed)
5. Determine how the 64-bit port resolves mods (does it look in `Mods/`?) — pending

## Engine Binding Surface (from the game's own Lua libraries, 2026-08-16)

The game ships its AI/story Lua as **precompiled bytecode** (custom-fork Lua 5.1, doc 03).
The bytecode string tables are readable, so the engine binding surface the game's own
library layer (`PGBASE`, `PGCOMMANDS`, `PGAICOMMANDS`, `PGSPAWNUNITS`, `PGTASKFORCE`,
`PGMOVEUNITS`, `PGEVENTS`, `PGSTATEMACHINE`, `PGSTORYMODE`, `PGINTERVENTIONS`,
`PGBASEDEFINITIONS`, `PGDEBUG`) calls is fully inventoried: **~137 distinct engine
bindings** (full list regenerable via `scripts/probe_lua_bindings.py` + `binding_gap.py`
against extracted library files).

These are the names mod scripts reach through the documented PG* wrapper API — the
**byte-compatible Lua surface the engine must register** (contract element 2). Grouped:

| Group | Examples | Count |
|---|---|---|
| Object queries | `Get_Hull`, `Get_Shield`, `Get_Owner`, `Get_Type`, `Get_Name`, `Get_ID`, `Get_Distance`, `Get_Unit_Table`, `Get_Garrisoned_Units` | ~20 |
| Object predicates | `Is_Category`, `Is_Good_Against`, `Is_Hero`, `Has_Ability`, `Has_Property`, `Is_A_Taskforce`, `Is_On_Diversion` | ~20 |
| Search | `Find_All_Objects_Of_Type`, `Find_Object_Type`, `Find_Nearest`, `FindDeadlyEnemy`, `FindTarget`, `FindPlanet`, `FindStageArea` | ~12 |
| Orders/actions | `Move_To`, `Attack_Target`, `Attack_Move`, `Formation_Attack`, `Release_Unit`, `Lock_Current_Orders`, `Garrison`, `Leave_Garrison`, `Spawn_Unit`, `Reinforce_Unit` | ~25 |
| Abilities | `Activate_Ability`, `Try_Ability`, `Use_Ability_If_Able`, `Is_Ability_Active`, `Is_Ability_Ready`, `Has_Ability` | ~8 |
| Events/timers | `Register_Attacked_Event`, `Register_Death_Event`, `Register_Timer`, `Cancel_Timer`, `Process_*`, `PumpEvents`, `Event_Object_In_Range` | ~15 |
| AI targeting | `Set_Targeting_Priorities`, `Set_Contrast_Values`, `Set_Land_AI_Targeting_Priorities`, `Should_Switch_Weapons`, `Should_Crush` | ~8 |
| Misc | `Get_Difficulty`, `Get_Game_Mode`, `Get_Faction_Name`, `Get_Tech_Level`, `Get_Build_Cost`, `Story_Event_Trigger`, `PlayerSpecificName` | ~15 |

**Current engine coverage** (`src/core/pg_bindings.cpp`): 12 of ~137 — the core thread/
time/global-value surface. The rest need the sim object model (unit/player DB) behind
them; they are the next implementation target.

## Open Questions

- Did the 64-bit remaster change the mod folder convention?
- Do current mods even work on the 64-bit port? (Many 2006 mods are 32-bit-era)
- What's the `64Patch.meg` precedence — does it override mod loose files?
- Do any mods ship custom DLLs? (would constrain our own DLL strategy)
