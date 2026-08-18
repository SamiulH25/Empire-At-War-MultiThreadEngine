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

### CONFIRMED CRASH (2026-08-18) — the additive parallel tick races the game

Live test: deployed the proxy to `corruption\`, launched FoCs via Steam,
started a space battle. The game crashed ~30s into the battle when units
were active. Windows Error Reporting:

```
Faulting application: StarWarsG.exe (1.121.13.7360)
Faulting module:      d3d9.dll  (our proxy)  offset 0xc5d2
Exception code:       0xc0000005  (access violation)
```

Mapped `0xc5d2` in the crashed DLL (`d3d9.dll.bak`) to the disassembly:

```
mov   rax, [rcx]          ; o->vtable
test  rax, rax
je    skip
mov   rax, [rax+0x50]     ; fn = vtable[0x50]  (Update)
test  rax, rax
je    skip
movss xmm1, [rsi+0x20]    ; dt
call  rax                 ; <== CRASH inside the game's Update(dt)
```

This is `UpdateObject` in `parallel_tick.cpp` — the crash is **inside the
game's own per-object `Update(dt)`**, invoked concurrently from our worker
pool while the game's serial tick runs the same `Update` on the same
objects. The `call rax` target was validated non-null, so it is not a stale
pointer — it is a **data race**: the game's Update touches shared state
(target lists, spatial structures) that the serial tick is also mutating.
The additive design (parallel pass + serial pass) is **fundamentally
unsafe against the real game**; it worked in `fake_game.exe` only because
that harness has no real shared game state.

**Conclusion:** parallelizing the game's per-object `Update` is not viable
in additive mode. It would only be safe as a *replacement* (serial loops
neutralized to no-ops first) AND after proving the per-object Update is
thread-safe for disjoint objects — which the Ghidra notes do not establish.
The safe incremental win is the pure-read slices (perception, pathfinding
scoring), which the engine already parallelizes and which do not race the
serial tick.

### Slot diagnostics (2026-08-18) — the six-list offsets are CORRECT

Added per-tick slot dumps (`EAW_PATCH_DEBUG` → `eaw_patch_debug.txt`) and
ran the game menu → battle. The full log (24,850 samples) shows the six
context slots at `+0x18..+0x90`:

| Slot | Offset | Menu | Battle | Role |
|---|---|---|---|---|
| 0 | `+0x18` | ptr set, count=1 | count 0–4 | menu/UI list |
| 1 | `+0x30` | null/0 | null/0 | unused |
| 2 | `+0x48` | null/0 | count 1–2 | small list |
| 3 | `+0x60` | null/0 | null/0 | unused |
| 4 | `+0x78` | null/0 | **count 724–938** | **battle unit list** |
| 5 | `+0x90` | null/0 | null/0 | unused |

Slot 4 is the live battle object list: hundreds of objects (724–938 across
the sample window), 5 distinct list pointers over time (reallocations as
units spawn/die). The scanner kept 1–3 lists throughout the battle
(`kept 3 lists` 2,387×). **The six-list offsets and vtable+0x50 mapping
are correct for the installed corruption build.**

The `objs=0` telemetry in `eaw_patch_hits.txt` was misleading: it reads
`LastParallelObjectCount()`, which is only updated when `ParallelTick`
runs — and that is gated behind `EAW_PATCH_PARALLEL_UPDATE` (off by
default). The scanner was finding the lists the whole time; the count
simply wasn't reported.

**Reaffirmed conclusion:** the confirmed crash was the additive parallel
tick racing the game's serial tick — not a wrong offset. The object lists
map correctly; the parallel Update call is the unsafe part.

### Clean-proxy combat validation (2026-08-18) — stable in a real battle

The final proxy build (per-tick list scan, parallel update OFF by default,
no debug file logging) deployed to `corruption\` and tested in a live
combat scene:

- Hook installed at the ASLR-adjusted `0x25ca30` (same build base), 15 workers
- Battle load produced large `dt` spikes (0.3s / 6.5s / 3.4s) as the scene
  loaded, then settled to a steady ~60 Hz (`dt` 0.001–0.005)
- **23,000+ ticks logged with the game stable and responding** throughout
  the battle (446 MB working set) — no crash, no jitter
- This is the first **long-duration in-game stability proof** of the proxy:
  the safe design (scan lists, don't race the serial tick) plays normally

The game remains playable with the proxy installed indefinitely; the
`EAW_PATCH_PARALLEL_UPDATE` env gate keeps the racing path off unless a
controlled experiment explicitly enables it.

## Finding 3 — Layout drift is a real risk; the offsets need validation

The Ghidra analysis (ghidra/simtick_decompile.txt, FUN_14025ca30) was run
against some build of the 64-bit remaster. The installed
`GameData\StarWarsG.exe` is 11,511,808 bytes dated 2026-08-15 — freshly
patched by Steam, plausibly a different build than the analysis. If the tick
moved, the hardcoded `kTickOffset = 0x25ca30` (and possibly the list offsets
0x18..0x90 and vtable+0x50) no longer apply. The list scanner already
degrades gracefully (bad lists are skipped), but a moved tick means no hook
at all, silently.

### Validation (2026-08-18) — offset CONFIRMED against the corruption exe

The layout-drift risk is **resolved for the target exe**. Static verification
(`scripts/check_exe_stamps.py`) against the installed binaries:

| Exe | Timestamp | Tick `0x25ca30` prologue |
|---|---|---|
| `corruption\StarWarsG.exe` (FoCs — the proxy target) | 2024-10-04 | **MATCH** — identical to the Ghidra decompile |
| `GameData\StarWarsG.exe` (base game) | 2024-10-04 | DRIFT — different code at that RVA |

The Ghidra analysis was done against the **corruption (FoCs)** build, and
that is exactly the exe the proxy deploys next to (the game's only d3d9
import resolves from the same folder). The `GameData` drift is irrelevant —
the base game is not the injection target. Re-verify after any Steam update
by running `scripts/check_exe_stamps.py`.

### Live validation (2026-08-18) — hook installs and fires in the real game

Deployed `src/build/d3d9.dll` into `corruption\` (via `deploy_patch.py`) and
launched FoCs standalone:

- `eaw_patch_hits.txt` written: `hook installed at <base+0x25ca30>`, `workers: 15`
- Tick fires at ~60 Hz (`dt` 0.002–0.014), 11,000+ ticks logged, game stable
  and responding throughout
- Object scan in the main menu: `objs=1` — one list with one object
  registered (menu state; the six lists populate during a battle)

The proxy DLL forwards d3d9 correctly (the game renders), and MinHook
installs cleanly on the ASLR-relocated address. **Next validation step:**
start a skirmish battle and re-read `eaw_patch_hits.txt` — the object count
should jump from 1 to the battle's live unit count, proving the six-list
scan against a populated battle context.

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
