# EAW Multi-Threaded Engine — Design Spec

**Date:** 2026-08-15
**Status:** Approved (research phase)
**Author:** SamiulH25 (with CommandCodeBot)

## 1. Project Goal

Build a multi-threaded engine for *Star Wars: Empire at War* (EAW) + *Forces of Corruption* (FoCs) that improves simulation performance while preserving full mod compatibility.

**Strategy: Hybrid** — first a performance patch (DLL injected into the official 64-bit exe), later a full reimplementation of the engine core.

**Target:** Steam Gold Pack, 2024 64-bit remaster (`StarWarsG.exe`, x86-64).

## 2. Scope Decisions

| Question | Decision |
|---|---|
| Goal | Hybrid: patch now, rebuild later |
| Game version | Steam Gold Pack (64-bit remaster) |
| Performance target | Everything — sim, rendering, loading, memory |
| Development environment | **Windows native** (primary), with portable core kept Linux-buildable |
| Language | C++20 (MSVC or MinGW-w64 on Windows; GCC/Clang for Linux native builds) |
| Mod compatibility | Must load Thrawn's Revenge, EAW Remake, and other XML/Lua mods |
| Multiplayer | Single-player focus; netcode research deprioritized |
| Work split | CommandCodeBot does all implementation work |

## 3. Initial Binary Findings (2026-08-15)

From first-pass PE analysis of the installed Steam files:

1. **Two 64-bit exes exist:**
   - `GameData/StarWarsG.exe` — base game, 0xbd8000 image size
   - `corruption/StarWarsG.exe` — FoCs expansion, 0xcc0000 image size
   - Both built 2026-08-15 per TimeDateStamp (1728062355/1728061883 — Oct 2024), 7 sections, x86-64.

2. **The engine already has partial threading:**
   - `ThreadLockMutexClass` strings in the exe (with 10-second timeout diagnostics)
   - `LoadThread`, `LuaCreateThread` strings
   - FoCs launcher `swfoc.exe` links **Intel TBB** (`tbbR.dll`): `concurrent_queue_base_v3`, `task_scheduler_init`, `queuing_rw_mutex`, `scoped_lock`. The TBB runtime ships in both game directories.

3. **`PerceptionFunctionG.dll` leaks unmangled C++ class names** — a free architectural roadmap:
   - `MegaFileManagerClass`, `ThePerceptionFunctionManagerClass`, `PerceptionFunctionClass`, `PerceptionFunctionCallClass`, `PerceptionContextClass`, `PerceptionEvaluationStateClass`, `DatabaseObjectManagerClass<T>`, `DatabaseUIntConversionClass`, `DynamicEnumConversionClass<T>`, `DynamicVectorClass<T>`
   - `Init_Perception_DLL` reveals the engine's callback surface (11 function pointers passed in).

4. **Engine is Lua-driven:** `LuaCreateThread`, Lua error strings, `nil`/`userdata` debug messages — gameplay/AI layer runs on embedded Lua.

5. **64-bit port tech stack:** Direct3D 9 (`d3d9.dll`, `d3dx9_43.dll`), Miles Sound System 64 (`mss64.dll`), Bink 2 (`bink2w64.dll`), Steam API (`steam_api64.dll`), Winsock2, plus `libcurl/libssl/libcrypto` (FoCs launcher only).

6. **Game data lives in .meg archives** (Petroglyph MegaFile format): `config.meg`, `maps.meg`, `models.meg`, `textures.meg`, `shaders.meg`, `Patch.meg`, `Patch2.meg`, `64Patch.meg`, speech/music/movies megas. Load order defined in `megafiles.xml`. Loose files in `Data/` override megas (mod mechanism).

## 4. Research Plan

### 4.1 Research Targets (7 areas)

1. **Binary forensics of StarWarsG.exe** — game loop, update tick, render path, thread primitives usage sites, globals map.
2. **64-bit patch history** — what changed from the 2006 32-bit original to the 2024 64-bit remaster.
3. **Megafile format** — full spec for reading/writing .meg archives (needed for rebuild phase and mod-compat testing).
4. **Lua integration surface** — Lua version, C++ class registration mechanism, `LuaCreateThread` behavior, thread safety of Lua state(s).
5. **Simulation architecture** — sim tick contents: pathfinding, perception functions, AI, combat; what mutates shared state vs. pure functions.
6. **Mod ecosystem contract** — what mods rely on: XML in config.meg, Lua AI scripts, loose-file overrides, mod folder convention.
7. **Threading prior art** — OpenRA, 0 A.D., Sins of a Solar Empire 2, modern Petroglyph engines.

### 4.2 Deliverables (research docs)

| Doc | Content |
|---|---|
| `docs/research/01-binary-map.md` | Exe layout, key functions, globals |
| `docs/research/02-meg-format.md` | Archive format spec |
| `docs/research/03-lua-surface.md` | Lua integration, threading constraints |
| `docs/research/04-simulation-architecture.md` | Game loop + sim tick breakdown |
| `docs/research/05-mod-compatibility.md` | Mod ecosystem contract |
| `docs/research/06-threading-design.md` | The multi-threading proposal (job system, partitioning, sync) |
| `docs/research/07-toolchain.md` | Native Windows toolchain for the C++ patch DLL (+ optional Linux native) |

### 4.3 Phases (ordered)

1. **Static analysis** — Ghidra (native Windows install) as primary disassembler; radare2 for quick lookups; string extraction; class map from `PerceptionFunctionG.dll` symbols.
2. **Dynamic analysis** — run the game on Windows with a debugger (x64dbg); confirm game loop, watch threads at runtime, profile hotspots with Windows Performance Recorder.
3. **Data format work** — Python .meg reader/extractor as first code deliverable.
4. **Design synthesis** — threading design: what parallelizes (pathfinding, perception, particles, AI), what stays serialized, job system architecture.
5. **Prototype spike** — minimal C++ DLL injected into StarWarsG.exe proving loop interception + one parallelized slice.

### 4.4 Tooling

- **Disassembler:** Ghidra (primary), radare2 (quick lookups)
- **Runtime:** x64dbg + Windows Performance Recorder (dynamic analysis)
- **Research tooling:** Python 3 (`pefile` confirmed installed)
- **Build:** CMake + C++20, native on Windows (MinGW-w64 or MSVC); portable core also builds on Linux with GCC/Clang
- **Output:** Patch DLL (`d3d9.dll` proxy or launcher-injected DLL) — exact injection method decided in phase 5

## 5. Environment Facts

- Game installed at: Steam → `steamapps/common/Star Wars Empire at War` (Windows)
- **Primary dev machine is Windows** — native toolchain, native debugging, game runs here
- Python 3 with `pefile` available
- C++20 toolchain: CMake + MinGW-w64 (or MSVC); portable core additionally builds on Linux with GCC/Clang
- Ghidra / x64dbg / WPR installed during phase 0
- GitHub account: SamiulH25 (gh CLI authenticated)

## 6. Success Criteria (Research Phase)

- [ ] StarWarsG.exe game loop identified and documented
- [ ] Threading primitives mapped: where the engine already uses threads, where it blocks
- [ ] .meg format spec complete enough to extract and re-verify files
- [ ] Lua integration documented (version, registration, thread safety)
- [ ] Simulation tick broken down into parallelizable vs. serial regions
- [ ] Mod compatibility contract documented and verified against a real mod
- [ ] Threading design written with concrete job-system proposal
- [ ] Toolchain doc + buildable "hello" DLL natively on Windows
- [ ] Prototype spike: DLL hooks the game loop (even if only logging FPS)

## 7. Out of Scope (Research Phase)

- Multiplayer/netcode (documented later if needed)
- Full reimplementation (phase 2 of hybrid strategy)
- Mod tooling beyond what research requires
- Audio/video pipeline changes
