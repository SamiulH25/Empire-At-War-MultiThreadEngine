# Performance & Determinism

**Date:** 2026-08-16
**Method:** `sim_tool battle --compare` — the same battle with 1 worker vs
(hardware − 1) workers; outcomes must be byte-identical; wall time compared.
Hardware: 16 logical cores (15 workers).

## Determinism: Identical Outcomes at Any Worker Count

The core claim of the multithreading rewrite: **the parallel sim produces
the same battle no matter how many threads run it.**

| Workers | Ticks | Shots | Rebels alive | Empire alive | Outcome |
|---|---|---|---|---|---|
| 1 | 2797 | 7668 | 0/28 | 8/28 | Empire victory |
| 15 | 2797 | 7668 | 0/28 | 8/28 | Empire victory |

Same with real game data:

| Workers | Ticks | Shots | Rebels | Empire | Outcome |
|---|---|---|---|---|---|
| 1 | 1200 | 234 | 15/18 | 18/18 | undecided |
| 15 | 1200 | 234 | 15/18 | 18/18 | undecided |

Also verified with 144 units/side (16,441 shots, identical survivors) and by
tests running the same scenario at 2 vs 4 workers (combat, pathfinding, AI
targeting all assert parallel ≡ serial).

## Why It's Deterministic

1. **Write-partitioned parallelism** — every parallel phase writes only
   per-object/per-search slots (movement: own position; combat: own hull;
   pathfinding: own search state; perception: own score slot).
2. **Read snapshot** — cross-object reads (targeting, hold-position) go
   through a per-tick position snapshot, never live positions another
   worker may be writing.
3. **Stable tie-breaks** — max-reductions compare strictly-greater over
   id-ordered candidates, so ties always pick the same object.
4. **Serial merges** — damage sums and target selection reduce serially
   after the parallel phase.

The one genuine race found (live-position read during parallel movement)
was caught by `--compare` diverging between worker counts and fixed with
the snapshot — the demo harness earns its keep.

## Speedup vs Fleet Size

The crossover is real and data-dependent: coordination overhead dominates
tiny workloads; the O(n²) target acquisition dominates big ones.

| Units/side | Workers | Wall time (1) | Wall time (15) | Speedup |
|---|---|---|---|---|
| 28 | 1 vs 15 | 51 ms | 307 ms | 0.17x |
| 72 | 1 vs 15 | 138 ms | 378 ms | 0.37x |
| 220 | 1 vs 15 | 1128 ms | 1331 ms | 0.85x |
| **760** | **1 vs 15** | **6489 ms** | **2710 ms** | **2.39x** |

Notes:

- The parallel_for fast path (serial under 8 items) exists precisely because
  coordination is slower than the work at small scale.
- At 760 units/side the auto-acquire scan (each shooter scans all others)
  dominates per-tick cost and parallelizes cleanly — that's the measured win.
- Throughput at small scale: ~10k–28k ticks/sec (a 120-second battle in
  ~300 ms wall time).

## Per-Subsystem Parallelism (as Built)

| Subsystem | Parallel | Notes |
|---|---|---|
| Movement integration | Yes | per-object slices |
| Combat phase 1 (fire decisions) | Yes | per-shooter slots |
| Combat phase 2 (damage apply) | Yes | per-target slots |
| Pathfinding searches | Yes | per-search budgets |
| Perception scoring | Yes | per-candidate |
| Lua scripts / events | No | one state, serial pump (matches game) |
| AI target reduce | Serial | tiny cost, deterministic ties |

## Reproduce

```sh
# Small battle (coordination-dominated, multi slower)
sim_tool battle --compare --fighters 64 --capitals 8 --ticks 1800

# Big battle (work-dominated, multi wins 2.4x)
sim_tool battle --compare --fighters 700 --capitals 60 --ticks 900

# Real game data variant
sim_tool battle --compare --game <dir-with-Data\*.XML> --fighters 16 --capitals 2 --ticks 1200
```

Every run ends with either `DETERMINISTIC: identical outcomes` or
`MISMATCH: outcomes differ!` (and a non-zero exit code on mismatch).
