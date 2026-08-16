# 03 — Lua Integration Surface

**Status:** Lua manager + thread mechanism located (2026-08-16) — version TBD
**Last updated:** 2026-08-16

## What We Know

### Strings Found in StarWarsG.exe

```
"userdata: %p"
"thread: %p"
"nil"
"_Name -- Expected a thread id parameter"
"LuaCreateThread:"
"pected a number for thread id parameter"
"LuaCreateThread::Ki..."
"./?.lua;./?.lc"              (LUA_PATH default — Lua 5.0-5.2 era)
"LuaScriptThread: Main State"
"LuaThreadTable"
"LuaScriptClass::ERROR/ALERT"
```

These are standard Lua 5.x error/format strings. The 2006 original embedded Lua 5.0
(specifically 5.0.2, per community knowledge). The 64-bit remaster's exact version is
**TBD** — the embedded Lua runtime is the `FUN_1407b9xxx` function cluster (Lua C API
calls: `FUN_1407b94a0` setglobal-ish, `FUN_1407b9510`, `FUN_1407b9a60`, `FUN_1407b9540`
register-ish, `FUN_1407b8890`, `FUN_1407b8ef0`). Byte-level version check (opcode
signature) is still pending.

### Lua Script Manager — `FUN_1402488e0` (confirmed)

The Lua script manager init (references `LuaThreadTable` at VA `0x140855e60`):
- Registers a global table named `"LuaThreadTable"` (length 0xe) into the Lua state at
  `param_1 + 0x58`
- Registers bindings `"Flush_G"` and `"Base_Definitions"` (via `FUN_1407b9540`)
- Iterates and runs definitions via `FUN_140243640` (script exec)
- The `param_1 + 0x58` pointer is the **Lua state** (`lua_State*`) — one per script manager

### LuaCreateThread

The game exposes a Lua binding `LuaCreateThread` (takes a Lua **function** per the error
string "Expected a LuaFunction parameter"). Combined with:
- `LuaThreadTable` — a table of live script threads
- `LuaScriptThread: Main State` — a thread named "Main State"
- `LuaScriptThread: %s` — per-thread naming

**Mechanism: Lua coroutines, not OS threads.** Script "threads" are coroutine entries in
the `LuaThreadTable`, scheduled by the engine's script pump (`%s -- Pump_Threads` found in
the census). No OS thread is created by `LuaCreateThread` (the only `_beginthreadex` in
the exe is the loading thread, Task 1.6).

### Registration Pattern

The engine registers C++ bindings into Lua via a wrapper layer (the `FUN_1407b9xxx`
cluster + `LuaWrapperMetaTable` string). The 64-bit port keeps the 2006 binding surface
for mod compat.

## What Mods Need (compat contract side)

Mods like Thrawn's Revenge and EAW Remake ship Lua scripts that:
- Define AI behavior (build orders, attack logic)
- Hook game events
- Read/write game objects through registered Lua bindings

The threaded engine must keep the **Lua API surface byte-compatible** — any reimplementation
phase must register the same class names, function names, and signatures.

## Thread Safety Concerns

- Lua states are not thread-safe. The manager holds one state at `param_1 + 0x58`; if
  multiple managers exist (one per player?), parallel evaluation needs per-state locking
  or per-state worker ownership.
- `LuaCreateThread` is coroutine scheduling inside a single Lua state — parallelizing AI
  across OS threads means coroutines sharing a state must stay on one OS thread.
- Global Lua state mutations (e.g. `setglobals` by mods) create cross-thread hazards.

## Research Tasks

1. ~~Identify Lua version~~ — TBD (embedded runtime `FUN_1407b9xxx`; 5.x confirmed,
   exact minor version pending byte-level opcode check)
2. ~~Find `LuaCreateThread` binding~~ — mechanism confirmed as coroutines (LuaThreadTable)
3. Map the full registered binding surface (compare against known 2006 binding lists) — pending
4. Count Lua states: one global? one per player? one per script? — the manager has one
   state at +0x58; count of managers TBD
5. Find where Lua calls into C++ and where C++ calls Lua — the interop boundaries
6. Determine whether any Lua calls happen on non-main threads already — none found so far

## Open Questions

- Lua 5.0.2 still, or upgraded in 64-bit port? (byte-level opcode check pending)
- Are AI scripts one coroutine per player?
- What's the mutex situation around Lua state access?
- Do mods use `LuaCreateThread`? (search Thrawn's Revenge / EAW Remake scripts)
