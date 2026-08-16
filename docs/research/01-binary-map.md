# 01 — Binary Map of StarWarsG.exe

**Status:** In progress — static analysis phase
**Last updated:** 2026-08-15

## Target Binaries

| Binary | Location | Notes |
|---|---|---|
| `StarWarsG.exe` (base) | `GameData/StarWarsG.exe` | 11.5 MB, PE32+ x86-64, 7 sections |
| `StarWarsG.exe` (FoCs) | `corruption/StarWarsG.exe` | 12.4 MB, PE32+ x86-64, 7 sections |
| `swfoc.exe` | `corruption/swfoc.exe` | 1.7 MB launcher, links TBB + libcurl |
| `PerceptionFunctionG.dll` | both dirs | Unmangled C++ exports — architectural roadmap |
| `runme.exe` / `runme2.exe` | root | 32-bit Steam launcher stubs |

## PE Facts (FoCs StarWarsG.exe)

- Image size: `0xcc0000`, Entry: `0x76a428`
- TimeDateStamp: `1728062355` (Oct 2024 remaster build)
- DLL characteristics: `0x8160` (ASLR, DEP, NX)
- Sections: `.text` (0x7fb4b7), `.rdata`, `.data`, `.pdata`, `_RDATA`, `.rsrc`, `.reloc`

## Import/Export Tables (from `scripts/pe_deepdive.py`)

Both `StarWarsG.exe` binaries share the identical import surface (18 DLLs):

| DLL | Funcs | Notes |
|---|---|---|
| KERNEL32.dll | 163 | incl. `CreateThread`, `TerminateThread`, `SetThreadPriority` (GameData exe) |
| USER32.dll | 68 | |
| mss64.dll | 38 | Miles Sound System 64 |
| d3dx9_43.dll | 29 | D3DX9 helpers |
| WS2_32.dll | 18 | Winsock2 |
| GDI32.dll | 17 | |
| steam_api64.dll | 11 | |
| PerceptionFunctionG.dll | 10 | `Init_Perception_DLL` + 9 more |
| bink2w64.dll | 11 | Bink 2 video |
| ADVAPI32.dll | 8 | |
| ole32.dll | 4 | |
| OLEAUT32.dll | 4 | |
| WINMM.dll | 4 | |
| SHELL32.dll | 2 | |
| SHLWAPI.dll | 1 | |
| COMCTL32.dll | 1 | |
| POWRPROF.dll | 1 | |
| **d3d9.dll** | **1** | **`Direct3DCreate9`** — the proxy surface |

`corruption/swfoc.exe` (launcher, 28 DLLs): links **TBB** (`tbbR.dll`, 16 funcs —
`task_scheduler_init`, `concurrent_queue_base_v3`, `queuing_rw_mutex::scoped_lock`),
libcurl (`libcurl-x64.dll`, 1 func), WINHTTP (10), MSVCP140/VCRUNTIME (MSVC CRT),
and statically exports **EASTL** allocator symbols (EASTL is the EA STL replacement).
TimeDateStamp `1692167365` (Aug 2023) — built before the 2024 remaster exes.

`tbbR.dll` (216 exports) is a full TBB runtime: `task_scheduler_init`,
`task_arena`, `parallel_pipeline`, spin/queuing/recursive mutexes,
`concurrent_queue/vector`, `tbb_thread`.

`PerceptionFunctionG.dll` exports: `Init_Perception_DLL` plus the unmangled class
methods (see Class Map below), ~66 symbols total (corruption build).

## Threading Evidence Found So Far

### Strings in StarWarsG.exe

| String | Offset (approx) | Meaning |
|---|---|---|
| `ThreadLockMutexClass -- %s failed to obtain mutex within 10 seconds (current owner is %s)` | 0x8505f0 | Engine mutex with timeout diagnostics |
| `ThreadLockMutexClass -- Failed to obtain mutex within 10 seconds` | 0x850650 | Second variant (no owner info) |
| `LoadThread` | 0x803b54 | Threaded loading path exists |
| `LuaCreateThread: _Name -- Expected a thread id parameter` | 0x855858 | Lua can spawn threads via binding |
| `LuaCreateThread::Ki...` | 0x8558a3 | Second Lua thread error string |

### TBB Usage (in swfoc.exe launcher)

`tbbR.dll` imports include:
- `concurrent_queue_base_v3` (ctor, push_if_not_full, pop_if_present, size, empty, clear)
- `task_scheduler_init` (`default_num_threads`, `initialize`)
- `queuing_rw_mutex` + `scoped_lock` (acquire/release)

This confirms **Intel TBB is available in-process** — the remaster's FoCs launcher uses it.
Whether `StarWarsG.exe` itself uses TBB directly is TBD (only 1 "tbb" string match in the base exe).

## PerceptionFunctionG.dll — Class Map

Unmangled exports reveal these engine classes:

- `ThePerceptionFunctionManagerClass` — singleton manager; `System_Initialize(MegaFileManagerClass*, bool)`, `System_Shutdown()`, `Create()`
- `PerceptionFunctionClass` — single perception function; `Build(name, eq)`, `Rebuild()`, `Evaluate(context, &result)`, `Add_Node()`, `Get_Name()`, `Get_Perception_Token_String()`
- `PerceptionFunctionCallClass` — call binding; `Create(name)`, `Add_Binding(tokenType, tokenType)`, `Evaluate()`
- `PerceptionContextClass` — evaluation context
- `PerceptionEvaluationStateClass` — evaluation state
- `DatabaseObjectManagerClass<T>` — generic object DB: `Add_Managed_Object`, `Get_Managed_Object`, `Remove_Managed_Object`, `Get_Iterator`, `String_To_UInt`, `UInt_To_String`, `Cleanup`
- `DatabaseUIntConversionClass` — uint <-> string conversion
- `DynamicEnumConversionClass<T>` — enum conversion
- `DynamicVectorClass<T>` — dynamic array

`Init_Perception_DLL` signature (demangled shape):
```
Init_Perception_DLL(
    DynamicEnumConversionClass<PerceptionTokenType>*,
    fn(*PerceptionContextClass, PerceptionTokenType, PerceptionTokenType) -> bool,
    fn(const *PerceptionContextClass, *PerceptionEvaluationStateClass, *double) -> bool,
    fn(const *PerceptionContextClass, const *PerceptionFunctionClass) -> void,
    fn(float, float) -> float,
    fn(int) -> *void,
    fn(const *basic_string) -> bool,
    fn(const *basic_string) -> void,
    fn(const *basic_string) -> const *DatabaseUIntConversionClass,
    member_fn_ptr(...),
    member_fn_ptr(...),
    *MegaFileManagerClass, *bool,
    const *DynamicVectorClass<basic_string>,
    ...)
```

This is a **callback injection surface**: the engine passes 11 function pointers into the
perception DLL so it can evaluate perception queries (token matching, math, memory alloc,
string lookup, megafile access) without direct engine linkage.

## String Census

Full auto-generated output: [01-string-census.md](01-string-census.md) (via `scripts/string_census.py`).

Most relevant threading/sync strings found across all binaries (file offsets):

| Binary | Offset | String | Why it matters |
|---|---|---|---|
| corruption/StarWarsG.exe | `0x00803B50` | `LoadThread` | Threaded loading path |
| corruption/StarWarsG.exe | `0x008505F0` | `ThreadLockMutexClass -- %s failed to obtain mutex within 10 seconds (current owner is %s)` | Engine mutex w/ timeout + owner diagnostics |
| corruption/StarWarsG.exe | `0x008509E8` | `PacketHandler Thread` | Network packet thread |
| corruption/StarWarsG.exe | `0x00851F30` | `NATUtilsThread` | NAT traversal thread |
| corruption/StarWarsG.exe | `0x008537A8` | `Main Thread` | Named main thread |
| corruption/StarWarsG.exe | `0x008537E8` | `Thread %d (%s) failed to exit - forcing exit` | Thread manager shutdown |
| corruption/StarWarsG.exe | `0x008546D0` | `LuaScriptThread: Main State` | Lua script threads; "Main State" implies coroutine model |
| corruption/StarWarsG.exe | `0x00854860` | `LuaThreadTable` | Table of live Lua threads |
| corruption/StarWarsG.exe | `0x00855948` | `LuaCreateThread -- Expected a LuaFunction parameter.` | `LuaCreateThread` takes a Lua function → coroutine creation |
| corruption/StarWarsG.exe | `0x008563D8` | `GetThreadID` | Lua binding: get thread id |
| corruption/StarWarsG.exe | `0x008563F8` | `Create_Thread` | Lua binding: create thread |
| corruption/StarWarsG.exe | `0x00872D80` | `%s -- Pump_Threads` | Per-frame thread pumping in the game loop |
| corruption/StarWarsG.exe | `0x00883790` | `SpacePathfindMaxExpansions` | Pathfinding budget setting |
| corruption/StarWarsG.exe | `0x008837D0` | `SpacePathfindFrameDelayDelta` | Pathfinding is frame-sliced (work spread across frames) |
| corruption/StarWarsG.exe | `0x00851398` | `Data\MegaFiles.xml` | Megafile load-order file (capital M/F — not `megafiles.xml`) |
| GameData/StarWarsG.exe | `0x009704F8` | `.?AVThreadClass@@` | RTTI: engine ThreadClass exists |
| GameData/StarWarsG.exe | `0x00970520` | `.?AVLoadingThreadClass@@` | RTTI: loading thread is a ThreadClass subclass |
| GameData/StarWarsG.exe | `0x009732F8` | `.?AVMainThreadClass@@` | RTTI: main thread is a ThreadClass subclass |
| corruption/PerceptionFunctionG.dll | `0x000C77B0` | `ThreadLockMutexClass -- ...` | Perception DLL has its own mutex class (corruption build) |
| corruption/PerceptionFunctionG.dll | `0x000F0292` | `InitializeCriticalSectionAndSpinCount` | Import: CS with spin count |
| corruption/PerceptionFunctionG.dll | `0x000F018C` | `CreateMutexA` | Import: named mutexes in perception DLL |
| corruption/swfoc.exe | `0x001036C0` | `C:\Projects\TGW\Eng\Libs\PGLib\Thread.cpp` | **Engine codename leak: "TGW"; PGLib = Petroglyph library** |
| corruption/swfoc.exe | `0x00101FB0` | `C:\Projects\TGW\Eng\Libs\PGLib\FastThreadLock.h` | Fast thread lock (spinlock?) used by launcher |
| corruption/swfoc.exe | `0x00103798` | `Creating heap %p, for thread %d` | ThreadClass gives each thread its own heap |
| corruption/swfoc.exe | `0x0014B380` | `?initialize@task_scheduler_init@tbb@@...` | Confirms TBB linkage in swfoc.exe |

## Next Steps

1. Install Ghidra + radare2
2. Full disassembly of `corruption/StarWarsG.exe`
3. Find main loop, update tick, render path
4. Locate `ThreadLockMutexClass` methods and all call sites
5. Find what `LoadThread` actually threads
6. Locate `LuaCreateThread` binding and the Lua state(s)
7. Map globals: world object database, perception manager, Lua state pointer

## Known Open Questions

- Does StarWarsG.exe itself link/load tbbR.dll, or is TBB only in swfoc.exe?
- How many threads does the game actually spawn at runtime? (dynamic analysis)
- Which mutexes gate the main sim tick?
- Is there a separate render thread in the 64-bit port? (The 2006 original was single-threaded with a "Loading" thread only.)
