# Roadmap

**Date:** 2026-08-16
**Companion docs:** [00-engine-progress.md](00-engine-progress.md),
[01-architecture.md](01-architecture.md), [02-subsystems.md](02-subsystems.md),
[03-performance.md](03-performance.md).

## Where the Project Is

The portable engine core reached its first milestone: deterministic
multithreaded battles on real game data, driven by the documented Lua mod
surface, with perception-driven AI. The next work splits into "finish the
simulation" and "take it back to the real game".

## Tier 1 — Finish the Simulation (highest value per effort)

1. **Land combat + land data** — `LandUnit` XML loading (the loader already
   handles the tag; the aggregate files need the same treatment as the
   space units) and ground battles (terrain in the path grid, infantry
   squads, garrisons already modeled).
2. **Hyperspace travel** — galactic fleets in transit (travel time,
   arrival events) instead of instant relocation; `Launch_Units` /
   `Land_Units` force ops.
3. **Richer unit families** — load corvettes/frigates/supers from
   `SPACEUNITSCORVETTES.XML`, `SPACEUNITSFRIGATES.XML`, `SPACEUNITSSUPERS.XML`
   so fleet composition is real.
4. **Economy** — player credits, `Give_Money`, tech levels, `Unlock_Tech`,
   build costs into force production (the AI's `Produce_Force`).
5. **Multi-stage AI plans** — taskforce stages (Get_Stage/Set_Stage) driving
   plan phases (assemble → move → attack → retreat) with the event/timer
   system, using the real `AI_GOALS`/`GOALFUNCTIONS` XML.

## Tier 2 — Mod Compatibility Proof

6. **Mod loader CLI** — point the engine at a mod's `Data` folder (Thrawn's
   Revenge, EAW Remake, Republic at War): MegaFileManager load order +
   loose-file override, unit XML, Lua scripts. The research doc's contract
   (docs/research/05) made concrete.
7. **Script compatibility harness** — run mod Lua source through the
   ScriptManager, catalog which bindings are still missing, close the gap
   (the 137-binding inventory shows ~60 registered; the rest are
   game-state-dependent).

## Tier 3 — Back to the Real Game

8. **Patch DLL** — the original hybrid strategy: d3d9 proxy + MinHook on
   the sim tick (`FUN_14025ca30`), proving the parallel slices against the
   real exe. The research phase (docs/research/01, 06) already mapped the
   hook surface; the job system and parallel patterns are proven here.
9. **Bytecode compat** — the game ships custom-fork Lua bytecode (doc 03);
   a loader for that format would let the engine run the game's own AI
   scripts, not just mod source.

## Tier 4 — Engine Completeness

10. **Fog of war** — `FogOfWar.Reveal_*`, visibility → perception
    (`TimeLastSeen` currently returns 0 = always visible).
11. **Abilities** — `Activate_Ability`/`Try_Ability`/cooldowns (the
    documented ability surface, currently nil-stubbed).
12. **Render/audio** — out of scope for the headless engine; a renderer is
    a separate milestone (the design spec says new renderer after the patch
    phase proves value).

## Suggested Order

The narrative arc: **1–3** (land + hyperspace + full rosters) makes the sim
a complete game; **6–7** proves the mod-compat thesis; **8** delivers the
original patch-DLL goal; **9–12** fill remaining fidelity gaps.

## Definition of Done for the Next Milestone

- Land battles run through `sim_tool` with real land unit data.
- A mod's `Data` folder loads and produces a battle.
- Hyperspace fleets travel with arrival events.
- Every new subsystem keeps the determinism invariant (extend `--compare`).
