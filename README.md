# Empire at War Multi-Threaded Engine

Research and development project for a multi-threaded engine targeting the 2024 64-bit
remaster of *Star Wars: Empire at War* (Steam, `StarWarsG.exe`).

## Strategy

Hybrid: first a **performance patch DLL** injected into the official 64-bit exe, later a
**full reimplementation** of the engine core. Full mod compatibility (Thrawn's Revenge,
EAW Remake, etc.) is a hard requirement.

**Strategy revision (2026-08-16):** leverage the documented modding ecosystem rather than
treating the exe as a black box. The .meg archive format is officially documented
(Petrolution), the Lua/XML surface is fully documented (Alamo Engine Tools), and ready-made
tools exist. The game is confirmed **single-core-bound** (live measurement: main thread
40-75% of one core). The engine work proceeds against the documented data formats +
targeted binary analysis.

## Status

**Engine core complete (2026-08-16)** — a deterministic, multithreaded EAW simulation
running on real game data: 3D pathfinding, two-phase parallel combat, events/timers,
taskforces, planets, perception-driven AI, and the documented Lua mod surface. 16 test
suites green; `sim_tool --compare` proves identical outcomes at any worker count and a
2.39x speedup at 1,520 units/side.

**Progress docs (the build-out story):**

- [00 — Engine progress report](docs/progress/00-engine-progress.md) — status, numbers, how to run
- [01 — Architecture](docs/progress/01-architecture.md) — component map, the tick, threading model
- [02 — Subsystems](docs/progress/02-subsystems.md) — deep dives per subsystem
- [03 — Performance & determinism](docs/progress/03-performance.md) — measurements, speedup table
- [04 — Roadmap](docs/progress/04-roadmap.md) — remaining work, milestones

## Key Facts So Far

- The 64-bit remaster (Oct 2024) contains threading primitives (`ThreadLockMutexClass`,
  `LoadThread`, `LuaCreateThread`) and ships Intel TBB (`tbbR.dll`)
- `PerceptionFunctionG.dll` exports unmangled C++ class names — a free architectural map
  of the engine (`MegaFileManagerClass`, `ThePerceptionFunctionManagerClass`, ...)
- Engine is Lua-driven; game data lives in `.meg` archives with loose-file override
- Tech stack: Direct3D 9, Miles Sound System 64, Bink 2, Steam API

## Documents

- [Design spec](docs/superpowers/specs/2026-08-15-eaw-multithread-engine-design.md)
- [01 — Binary map](docs/research/01-binary-map.md)
- [02 — MegaFile format](docs/research/02-meg-format.md)
- [03 — Lua surface](docs/research/03-lua-surface.md)
- [04 — Simulation architecture](docs/research/04-simulation-architecture.md)
- [05 — Mod compatibility](docs/research/05-mod-compatibility.md)
- [06 — Threading design](docs/research/06-threading-design.md)
- [07 — Toolchain](docs/research/07-toolchain.md)

## Building & Running

```sh
cd src
cmake -B build -G Ninja
cmake --build build
cd build && ctest          # 16 suites
sim_tool battle --fighters 12 --capitals 2                    # scripted battle
sim_tool battle --compare --fighters 700 --capitals 60        # determinism + speedup
sim_tool battle --game <dir-with-Data\*.XML>                  # real game unit stats
```

## Planned Toolchain

C++20, built natively on Windows (MinGW-w64 or MSVC) targeting a Windows DLL. The
portable core also builds natively on Linux with GCC/Clang — no cross-compiling.
Research tooling: Ghidra + x64dbg + Python (`pefile`).

## License

TBD
