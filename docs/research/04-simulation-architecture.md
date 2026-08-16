# 04 — Simulation Architecture

**Status:** Main loop located + runtime baseline captured (2026-08-16)
**Last updated:** 2026-08-16

## Runtime Baseline (2026-08-16)

Captured via `scripts/thread_snapshot.py` (PID 14780, FoCs `corruption/StarWarsG.exe`).

### Menu (main menu, idle)

| TID | CPU time | Notes |
|---|---|---|
| 10996 | 36.4s | **Main thread** (dominant consumer) |
| 15312 | 14.3s | **Second CPU-active worker** (~40% of main) |
| 5024 | 1.3s | Light worker |
| 7464 | 0.8s | Light worker |
| rest (19) | ~0 | Waiting (threadpool/IO) |

Total: **23 threads**.

### Battle (skirmish, medium AI, ~60s in)

| TID | CPU time | Notes |
|---|---|---|
| 10996 | 56.8s | **Main thread** (+20.4s during battle) |
| 15312 | 25.4s | **Worker** (+11.1s — active in battle, ~35% of main's load) |
| 5024 | 2.3s | (+1.0s) |
| 7464 | 1.1s | (+0.3s) |
| rest (16) | ~0 | Waiting |

Total: **20 threads** (3 exited after battle load — the loading thread + menu workers).

Working set: 485 MB (menu) → 562 MB (battle).

### Key observations

- **Main thread (TID 10996) dominates** — consistent with the single-threaded sim loop
  found in static analysis (Task 1.4).
- **A second CPU-active thread (TID 15312) exists and runs during battle** at ~35% of
  main-thread load. Static analysis predicted only a loading thread; this worker is
  persistent (present in menu + battle). Candidates: Miles audio mixer, Steam API,
  or a background packet/NAT thread. **Needs identification** — see open question below.
- Thread count **drops** from menu to battle (23→20): loading thread exits after load;
  the game does not spawn per-battle workers (no thread growth with battle load).
- No thread scaling with battle size observed (2 CPU-active threads regardless).

### Live CPU Split (2026-08-16, via `scripts/cpu_sample.py`)

Sampled during skirmish battles (PID 10828, 16 logical CPUs). Per-thread CPU% over a
~13s window:

### Sample 1 (medium-intensity battle)

| TID | CPU% | Notes |
|---|---|---|
| 3996 | 39.7% | **Main thread** |
| 7704 | 14.6% | Worker (~37% of main) |
| rest | ~0 | |

Total: **56.5% of one core**.

### Sample 2 (heavy battle moment)

| TID | CPU% | Notes |
|---|---|---|
| 3996 | 74.6% | **Main thread** (spiked) |
| 7704 | 6.3% | Worker |
| 11008 | 4.4% | Light worker |
| rest | ~0 | |

Total: **86.4% of one core**.

### Findings

- **The game is single-core-bound**: main thread 40–75% of one core depending on battle
  intensity; total process usage stays under ~1 core (5.4% of 16 cores).
- **Massive parallelization headroom**: ~15 idle cores and most of the main core free.
- The second worker (TID 7704) is persistent but its load varies (6–15%) — consistent
  with an audio/IO/network helper, not a sim worker.
- Main thread CPU scales with battle intensity → the sim tick (Task 1.4's
  `FUN_14025ca30`) is the bottleneck target.

## Open question (from Task 2.1)

- What is TID 15312? Needs a start-address resolution (requires admin for
  `ProcessThread.StartAddress`) or x64dbg attach (Task 2.2) to identify.

## Main Loop (confirmed, corruption/StarWarsG.exe)

Entry `0x14076a578` → CRT → WinMain:

### WinMain — `FUN_14005d990`

- Creates window: `CreateWindowExA(0, "TESTWINCLASS", "Star Wars: Empire at War: Forces", ...)`
- `ShowWindow`, `UpdateWindow`
- Classic message pump: `PeekMessageA` / `TranslateMessage` / `DispatchMessageA`,
  `WaitMessage()` when idle
- **Per-frame body** runs in `while (DAT_140a619ef == '\0')` — calls per frame:
  - `FUN_14025ca30(window, dt)` — **the sim tick** (see below)
  - `FUN_140301750(...)` — render/scene pass (iterates a linked list of objects calling
    vtable+0x58 with a float — likely draw calls)
  - `FUN_14002ffb0(...)`, `FUN_140060330(...)`, `FUN_14001dc60(...)` — other per-frame
    passes (input/gui/particles — TBD)
  - `FUN_14027cb70(&clock)` etc. — frame timing/accumulator

### Sim Tick — `FUN_14025ca30(longlong param_1, float dt)`

- Maintains a **time accumulator** at `param_1 + 8` (fixed-step: `fVar2` is the step;
  when accumulated time exceeds the step, it subtracts and steps)
- Iterates **six object lists** at `param_1 + 0x20 / 0x38 / 0x50 / 0x68 / 0x80 / 0x98`
  (each with pointer + count at +0x18/0x20 style layout)
- For each non-null object, calls **virtual method at vtable+0x50** with `dt` — the
  standard per-entity Update slot
- This is the per-entity simulation update (units, effects, etc.)

### Render Path (D3D9)

- `Direct3DCreate9` thunk called from `FUN_140176160` (device creation, startup)
- Per-frame scene iteration in `FUN_140301750` calls vtable+0x58 on scene objects

### Loop structure summary

```
WinMain (FUN_14005d990)
  └─ while (!quit) {
       PeekMessage/Translate/Dispatch    (input)
       FUN_14025ca30(hwnd, dt)           (sim tick: 6 object lists, vtable+0x50 Update)
       FUN_140301750                     (scene/render pass)
       FUN_14002ffb0 / FUN_140060330 / FUN_14001dc60 (other per-frame passes)
       frame timing (accumulator, FPS calc)
     }
```

The 2006 original ran a classic single-threaded RTS loop; the 64-bit port's loop is
structurally the same per-frame pattern (no render thread found in static analysis so
far — the message loop drives everything). **Whether any phases spawn threads is the
open question for Tasks 1.5/1.6.**

## Known Architecture Elements (from binary strings + community knowledge)

### Perception System (PerceptionFunctionG.dll)
The engine's line-of-sight/sensor system is a **separate DLL with a formal callback surface**.
It evaluates "perception functions" (scripted queries over tokens like `IsFriendly`,
`DistanceLessThan`, etc.) against a `PerceptionContextClass`. This is significant because:

- It's already modularized — the engine calls into it via function pointers
- If perception evaluation is pure (context in, result out), it's a prime candidate for
  parallel evaluation across units
- The callback-based design suggests Petroglyph already separated it for perf reasons

### AI Layer
Lua-driven, with `LuaCreateThread` (coroutines?). AI decisions (build queue, attack orders)
are scripted in Lua and run per-player.

### Combat/Movement
Part of the C++ sim tick (not exposed via the DLL). Details unknown until disassembly.

## Parallelization Hypotheses (to test)

| Subsystem | Parallelizable? | Reason |
|---|---|---|
| Perception evaluation | Likely | Pure context->result, already isolated in a DLL |
| AI decision-making | Maybe | Per-player independent, but Lua state sharing is a hazard |
| Pathfinding | Maybe | Per-unit independent, but spatial queries need read-shared map data |
| Particle updates | Likely | Classic embarrassingly parallel work |
| Render command building | Maybe | D3D9 is single-threaded API, but command lists can be built in parallel |
| Load/decompress | Likely | `LoadThread` already exists; megas could decompress on workers |

## What Static Analysis Must Find

1. ~~The main loop structure~~ — DONE (WinMain FUN_14005d990, sim tick FUN_14025ca30)
2. Where mutexes are taken — what they guard (Task 1.5)
3. What `LoadThread` does exactly (Task 1.6)
4. The sim tick function(s) and their data dependencies (partial — object lists confirmed)
5. Memory layout of unit/object arrays (SOA vs AOS) — critical for cache behavior and
   parallel partitioning

## Dynamic Analysis Plan (Windows)

1. ~~Attach debugger, break on main loop, count threads~~ — DONE (Task 2.1: 20-23
   threads, main + 1 worker active; Task 2.2: loop confirmed via live CPU pattern +
   static addresses)
2. ~~Sample CPU profiles during heavy battle~~ — PARTIAL (Task 2.3: per-thread CPU
   split captured; full WPR hotspot profile blocked — needs Windows Performance Toolkit
   install + admin; method documented below)
3. ~~Watch which thread spends time where~~ — DONE (main thread 40-75%, one worker
   6-15%, rest idle)
4. ~~Verify sim tick rate and frame pacing~~ — PARTIAL (Task 2.4: fixed-step accumulator
   confirmed in FUN_14025ca30; exact Hz inferred at 30 (2006 original); direct measure
   needs x64dbg/WPR)

### Task 2.2/2.3/2.4 status + how to finish

- **WPR CPU profile (Task 2.3 full)**: the inbox WPR (`C:\Windows\System32\wpr.exe`)
  cannot load built-in profiles (`wpr -start CPU` → `0xc5600602`). The full Windows
  Performance Toolkit (with `wprp\cpu.wprp` + `wpa.exe`) is not installed. To finish:
  install the Windows ADK or SDK's WPT component, then run as Administrator:
  `wpr -start CPU -filemode` → play battle → `wpr -stop C:\Temp\eaw.etl` →
  analyze in WPA (CPU Usage Sampled, filter StarWarsG.exe, top-10 by inclusive samples).
- **Tick rate (Task 2.4 direct)**: x64dbg breakpoint on `StarWarsG.exe+0x25ca30` with
  hit counter over 10s, or WPA's sampled-stack periodicity on the sim tick function.
- The partial data already answers the core question: **single-threaded sim, main-core
  bound, huge parallelization headroom**.
