# 05 — Mod Compatibility Contract

**Status:** Structural facts gathered — mod deep-dive pending
**Last updated:** 2026-08-15

## Why This Matters

The hybrid strategy has two phases. Phase 1 (patch DLL) must not break mods.
Phase 2 (reimplementation) must load them. Either way, we need the exact contract.

## The Modding Mechanism (observed in install)

1. **Megafiles** hold game data (XML configs, maps, models, textures, shaders)
2. **Loose files** in `Data/` override megafiles — confirmed by the presence of
   `GameData/Data/*.txt` files (ship name lists) sitting next to the megas
3. **Mod folders** — the 2006 game used a `Mods/` folder; the 64-bit remaster directory
   has none by default, but Steam Workshop mods install there (path TBD on this install)
4. **`megafiles.xml`** defines load order; mods may supply their own
5. **Lua scripts** live inside `config.meg` (or loose `Data/Scripts/...`) and drive AI

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
4. **.meg reading** — same entry lookup and load order
5. **Mod folder discovery** — same paths the game searches
6. **64Patch.meg** — this is new in the remaster; its override position in the load order
   matters for both vanilla and mods

## Research Tasks

1. Download/install Thrawn's Revenge (Steam Workshop) and inspect its file layout
2. Same for EAW Remake
3. Extract a mod's XML and Lua, catalog which engine bindings it uses
4. Compare against the 64-bit exe's actual binding surface
5. Determine how the 64-bit port resolves mods (does it look in `Mods/`?)
6. Check whether Workshop mods in the 64-bit port use a different mechanism (Steam UGC API)

## Open Questions

- Did the 64-bit remaster change the mod folder convention?
- Do current mods even work on the 64-bit port? (Many 2006 mods are 32-bit-era)
- What's the `64Patch.meg` precedence — does it override mod loose files?
- Do any mods ship custom DLLs? (would constrain our own DLL strategy)
