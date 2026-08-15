# 07 — Toolchain: Linux → Windows Cross-Compile

**Status:** Initial assessment — setup instructions pending
**Last updated:** 2026-08-15

## Goal

Build a C++20 DLL on Linux that runs inside `StarWarsG.exe` (x86-64) on Windows 10/11.
The user will shift to Windows for the actual testing; Linux is the build environment.

## Current Linux Host State

| Tool | Status |
|---|---|
| Python 3 + pefile | Installed |
| Wine | Installed (`/usr/bin/wine`) — useful for smoke tests |
| g++ / clang | Installed (native only — not useful for PE targets) |
| MinGW x86_64 cross-compiler | **Missing — install needed** |
| Ghidra / radare2 | **Missing — install needed** (research tooling) |
| CMake | TBD |

## Proposed Setup

### Cross-Compiler

```bash
# Ubuntu/Debian family
sudo apt install g++-mingw-w64-x86-64 mingw-w64-tools cmake ninja-build
```

Or via Zig (bundles clang with cross targets):
```bash
# Zig cc alternative if MinGW packages are problematic
```

### CMake Toolchain File

```cmake
# toolchain-mingw64.cmake
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
```

### Injection Strategy Considerations

The patch DLL must load into StarWarsG.exe. Candidate methods:

| Method | Mechanism | Pros | Cons |
|---|---|---|---|
| d3d9.dll proxy | Drop our DLL named `d3d9.dll` in game folder, forward all exports to real d3d9 | No exe modification, auto-loads, game imports d3d9 (1 import) | Must implement full export forwarding |
| winmm.dll proxy | Same trick with winmm (4 imports) | Same | Same |
| Launcher patching | swfoc.exe is small; patch its import table to load our DLL | One-time static patch, no proxy surface | Exe modification, Steam may verify |
| Steam launch option | Steam can inject via `LD_PRELOAD`-style on Windows? No — not applicable | — | — |

**Recommendation:** d3d9.dll proxy. The game links `d3d9.dll` with 1 import
(`Direct3DCreate9`). A proxy DLL exporting the same name and forwarding to the real
system d3d9 is minimal surface area and a well-trodden modding pattern.

## Build Output Verification (on Linux)

- `x86_64-w64-mingw32-objdump -p our.dll` — verify PE headers, exports
- Wine smoke test: `wine rundll32 our.dll,...` or load in a trivial PE loader
- Full verification happens on the user's Windows box

## Research Tooling Install Plan

```bash
# Ghidra headless (analysis)
# radare2 (quick lookups)
# apt or snap both; Ghidra needs JDK 17+
```

## Open Questions

- Does the user's Windows box have MSVC? (Not required — MinGW output is a plain PE DLL)
- Do we need debug symbols for StarWarsG.exe? (Unlikely — 2006-era games ship stripped)
