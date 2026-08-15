# Empire at War Multi-Threaded Engine

Research and development project for a multi-threaded engine targeting the 2024 64-bit
remaster of *Star Wars: Empire at War* (Steam, `StarWarsG.exe`).

## Strategy

Hybrid: first a **performance patch DLL** injected into the official 64-bit exe, later a
**full reimplementation** of the engine core. Full mod compatibility (Thrawn's Revenge,
EAW Remake, etc.) is a hard requirement.

## Status

Research phase — static and dynamic analysis of the remaster binary.

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

C++20, cross-compiled from Linux with MinGW to a Windows DLL. CMake build.
Research tooling: Ghidra headless + radare2 + Python (`pefile`).

## License

TBD
