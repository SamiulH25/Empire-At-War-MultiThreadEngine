# 03 — Lua Integration Surface

**Status:** Initial findings — deep analysis pending
**Last updated:** 2026-08-15

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
```

These are standard Lua 5.x error/format strings. The 2006 original embedded Lua 5.0
(specifically 5.0.2, per community knowledge). The 64-bit remaster may have bumped it — TBD.

### LuaCreateThread

The game exposes a Lua binding `LuaCreateThread` that takes a thread id (likely a coroutine
or function reference). This suggests:
- The AI/gameplay scripting layer can spawn logical threads
- These are probably Lua coroutines, not OS threads — TBD

### Likely Registration Pattern

The 2006 engine (Alamo/Petroglyph) registers C++ classes into Lua with a binding layer
("LuaFunctionClass" style wrappers, visible in old modding SDKs). The 64-bit port likely
keeps the same surface since mods depend on it.

## What Mods Need (compat contract side)

Mods like Thrawn's Revenge and EAW Remake ship Lua scripts that:
- Define AI behavior (build orders, attack logic)
- Hook game events
- Read/write game objects through registered Lua bindings

The threaded engine must keep the **Lua API surface byte-compatible** — any reimplementation
phase must register the same class names, function names, and signatures.

## Thread Safety Concerns

- Lua states are not thread-safe. If the game has multiple Lua states (one per AI player?),
  parallel evaluation needs per-state locking or per-state worker ownership.
- `LuaCreateThread` implies coroutine scheduling inside a single Lua state — if we parallelize
  AI across OS threads, coroutines that share a state must stay on one OS thread.
- Global Lua state mutations (e.g. `setglobals` by mods) create cross-thread hazards.

## Research Tasks

1. Identify Lua version (string search for `LUA_VERSION` / version string in exe)
2. Find `LuaCreateThread` binding implementation and its registration table
3. Map the full registered binding surface (compare against known 2006 binding lists)
4. Count Lua states: one global? one per player? one per script?
5. Find where Lua calls into C++ and where C++ calls Lua — the interop boundaries
6. Determine whether any Lua calls happen on non-main threads already

## Open Questions

- Lua 5.0.2 still, or upgraded in 64-bit port?
- Are AI scripts one coroutine per player?
- What's the mutex situation around Lua state access?
- Do mods use `LuaCreateThread`? (search Thrawn's Revenge / EAW Remake scripts)
