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

Research phase — static/dynamic analysis complete; pivoting to ecosystem-based engine work.

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

## Planned Toolchain

C++20, built natively on Windows (MinGW-w64 or MSVC) targeting a Windows DLL. The
portable core also builds natively on Linux with GCC/Clang — no cross-compiling.
Research tooling: Ghidra + x64dbg + Python (`pefile`).

## License

TBD
