# Patch-DLL Path to a Playable, Faster Game — Findings

**Date:** 2026-08-17
**Companion docs:** [00-engine-progress.md](00-engine-progress.md),
[01-architecture.md](01-architecture.md), [03-performance.md](03-performance.md),
[04-roadmap.md](04-roadmap.md), [05-whats-missing.md](05-whats-missing.md).

This doc records what was learned while assessing "what's left until we can
drop the DLL in, play the game, and feel the improved performance." It is the
working findings list for the Tier 3 patch-DLL milestone, not a completed
plan.

## Where Things Stand

The proxy + hook infrastructure builds and is validated against
`fake_game.exe` (the proxy-hook ctest pair). The engine core (sim, Lua
surface, mod loader, `mod_tool run`) is complete for its tier. The game is
installed at `C:\Program Files (x86)\Steam\steamapps\common\Star Wars Empire
at War\GameData\StarWarsG.exe` — note the real layout is `GameData\`, not
`Data\` (that's why the earlier `Data` existence checks said "missing").

## Finding 1 — The hook is additive, not replacing (the core problem)

`HookedTick` (src/proxy/d3d9.cpp) runs `ParallelTick` — which calls the
game's own per-object `Update(dt)` (vtable+0x50) from the engine's worker
pool — and THEN calls `g_originalTick`, which runs the same updates again
serially. Every object is updated **twice per frame**. This cannot make the
game faster; it is at best a correctness probe that the object lists and the
vtable slot are mapped correctly.

A real speedup requires the parallel loop to **replace** the game's serial
loop, not run alongside it. Options:

- **Interpose at the per-object vtable+0x50 call** (detour each object's
  Update) — still double-runs unless the original loops are neutralized.
- **Patch the six loop bodies in the real tick to no-ops** (rewrite the
  `do...while` to skip), then have `ParallelTick` run the same vtable+0x50
  updates on the worker pool before the tick. Real speedup *if* the per-object
  update is thread-safe for disjoint objects.
- **Safe fallback**: parallelize only the pure-read parts (perception,
  pathfinding scoring — the engine already parallelizes these) and keep the
  object updates serial.

## Finding 2 — The per-object update is a shared-write pattern

`parallel_tick.cpp`'s `UpdateObject` mutates each object from worker threads.
That is safe only if the game's per-object update never reads shared
structures (target lists, spatial hashes) that another worker is writing.
Nothing in the Ghidra notes proves this. This is the biggest technical risk:
races/crashes in a real battle are hard to debug without a debugger attached.

## Finding 3 — Layout drift is a real risk; the offsets need validation

The Ghidra analysis (ghidra/simtick_decompile.txt, FUN_14025ca30) was run
against some build of the 64-bit remaster. The installed
`GameData\StarWarsG.exe` is 11,511,808 bytes dated 2026-08-15 — freshly
patched by Steam, plausibly a different build than the analysis. If the tick
moved, the hardcoded `kTickOffset = 0x25ca30` (and possibly the list offsets
0x18..0x90 and vtable+0x50) no longer apply. The list scanner already
degrades gracefully (bad lists are skipped), but a moved tick means no hook
at all, silently.

**Fix**: convert the hardcoded offset to a **signature scan** for the tick's
byte pattern (the `mov eax,[rcx+8]; addss xmm1,...` prologue plus the six
`do...while` loop bodies), resolved at runtime against the loaded exe. This
is the immediate next step — it converts the "layout drift" unknown into a
fact, verifiable by dropping the DLL in and reading `eaw_patch_hits.txt`.

## Finding 4 — The performance profile justifies the effort

`ghidra/battle_cpu_live.txt` (PID 10828, 13.1s in a battle, 16 logical CPUs):

- One thread (TID 3996) at **74.6% CPU**
- Sum thread CPU 86.4% of one core; process total 5.4% of all cores

Classic single-thread bottleneck: the game pegs one core and leaves the rest
idle. There is real headroom for a parallel replacement of the per-object
update loop — IF the thread-safety question (Finding 2) is answered.

## Finding 5 — What is NOT needed for the playable-faster milestone

- Lua/script compatibility work (`mod_tool run`, library scripts) — done.
- The game-bytecode loader (item 2 in 05-whats-missing) — engine
  completeness, not patch-DLL.
- Full AI fidelity — not required to demonstrate the patch.

## Recommended Order

1. **Signature-scan the tick** in the real exe (replaces `kTickOffset`);
   drop the DLL in; confirm via `eaw_patch_hits.txt` that the hook installs
   and object lists register against the current build.
2. **Decide the thread-safety question** for the game's per-object Update:
   probe with a debug build (run the parallel replacement, watch for
   crashes/mismatched state in a scripted skirmish). If unsafe, fall back to
   parallelizing only the pure-read subsystems (still a real win given the
   profile).
3. **Convert additive to replacing**: neutralize the six serial loops in the
   real tick, run the updates on the worker pool, and measure
   (frame-time before/after, tick rate in a big battle).
4. Only then: broadcast the "parallel tick in the real game" demo.

## Risks

- **Thread-safety of the game's per-object update** — unknown; the
  single biggest unknown. Mitigation: probe early with a debug build.
- **Exe build drift** — mitigated by signature scanning.
- **Anti-cheat / Steam validation** — the game is a single-player RTS;
  a local proxy DLL is the standard mod mechanism (EAW mods have shipped
  d3d9 proxies for years). Low risk.
