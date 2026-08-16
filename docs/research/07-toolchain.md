# 07 — Toolchain: Native Windows (primary) + Optional Linux Native Build

**Status:** Initial assessment — setup instructions pending
**Last updated:** 2026-08-16

## Goal

Build the C++20 patch DLL natively on Windows 10/11 (the game runs there), and keep
the portable core (job system, sim logic, .meg reader) buildable as a native Linux
executable too — no cross-compiling. The only Windows-specific piece is the injected
DLL (`d3d9.dll` proxy), which is inherently a PE DLL; everything else should stay
portable so Linux can run headless sim/meg tooling natively.

## Current Windows Host State

| Tool | Status |
|---|---|
| Git | Installed (repo cloned) |
| Python 3 + pefile | Installed |
| MinGW-w64 (WinLibs) | Installed — used to build the proxy DLL (naked stubs) |
| CMake + Ninja | Installed |
| Ghidra / x64dbg / WPR / Process Explorer | Installed (see Phase 0 of the plan) |
| MSVC | Optional — MinGW output is a plain PE DLL, MSVC not required |

## Proposed Setup

### Windows (primary)

Toolchain installs are covered in detail in the phased plan (Task 0.2). The summary:

```powershell
winget install BrechtSanders.WinLibs.POSIX.UCRT   # MinGW-w64 (g++, mingw32-make)
winget install Kitware.CMake
winget install Ninja-build.Ninja
winget install Microsoft.Sysinternals.ProcessExplorer
winget install Microsoft.WindowsPerformanceToolkit  # wpr / wpa (as Administrator)
```

MinGW-w64 is required for the d3d9 proxy DLL because it supports
`__attribute__((naked))` on x86-64 (MSVC does not), which the export-forwarding stubs
need. Everything else compiles with either MinGW or MSVC.

### Linux (optional — native executable, not cross-compile)

Build the portable core natively on Linux — no MinGW, no Wine:

```bash
# Ubuntu/Debian family
sudo apt install g++ cmake ninja-build
```

```bash
cmake -B build -G Ninja
cmake --build build
```

The Windows-only `proxy/` target is excluded on non-Windows via CMake guards
(`if(WIN32)`), so Linux builds produce native ELF executables for the portable parts:
sim core, job system, .meg tooling, and any headless tests.

### CMake Top-Level (portable layout)

```cmake
cmake_minimum_required(VERSION 3.20)
project(eaw_engine CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Portable core — builds on Windows AND Linux
add_library(eaw_core STATIC core/...)   # job system, sim, meg reader

# Windows-only: injected proxy DLL (naked stubs, PE target)
if(WIN32)
    add_library(proxy SHARED proxy/d3d9.cpp proxy/d3d9.def)
endif()
```

## Injection Strategy Considerations (Windows-only)

The patch DLL must load into StarWarsG.exe. Candidate methods:

| Method | Mechanism | Pros | Cons |
|---|---|---|---|
| d3d9.dll proxy | Drop our DLL named `d3d9.dll` in game folder, forward all exports to real d3d9 | No exe modification, auto-loads, game imports d3d9 (1 import) | Must implement full export forwarding |
| winmm.dll proxy | Same trick with winmm (4 imports) | Same | Same |
| Launcher patching | swfoc.exe is small; patch its import table to load our DLL | One-time static patch, no proxy surface | Exe modification, Steam may verify |

**Recommendation:** d3d9.dll proxy. The game links `d3d9.dll` with 1 import
(`Direct3DCreate9`). A proxy DLL exporting the same name and forwarding to the real
system d3d9 is minimal surface area and a well-trodden modding pattern.

## Build Output Verification (Windows)

- `objdump -p our.dll` (or `dumpbin /exports` with MSVC) — verify PE headers, exports
- `python -c "import ctypes; ctypes.WinDLL(r'path\to\d3d9.dll')"` — smoke-load the DLL
- Full verification happens in-game: drop `d3d9.dll` into the game folder and launch

## Open Questions

- Do we need debug symbols for StarWarsG.exe? (Unlikely — 2006-era games ship stripped)
- Which Windows toolchain ends up primary for day-to-day builds: MinGW-w64 or MSVC?
  (MinGW is required for the proxy; the portable core can use either)
