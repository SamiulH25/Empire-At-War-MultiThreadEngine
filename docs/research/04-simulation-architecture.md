# 04 — Simulation Architecture

**Status:** Main loop located (2026-08-16) — static analysis of corruption/StarWarsG.exe
**Last updated:** 2026-08-16

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

1. Attach debugger, break on main loop, count threads
2. Sample CPU profiles during heavy battle (many units)
3. Watch which thread spends time where
4. Verify sim tick rate and frame pacing
