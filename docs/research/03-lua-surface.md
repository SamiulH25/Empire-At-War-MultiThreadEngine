# 03 — Lua Integration Surface

**Status:** Lua manager + thread mechanism located; **bytecode compat issue found** (2026-08-16)
**Last updated:** 2026-08-16

## ⚠️ Key Compat Finding: Game Lua is a Custom Fork (2026-08-16, header verified 2026-08-18)

The game ships its AI scripts as **precompiled Lua bytecode** (not source). Verified by
extracting `DATA\SCRIPTS\AI\BUILDGROUNDFORCESPLAN.LUA` from config.meg — it starts with
the **custom magic `\x1bLup`** (not vanilla `\x1bLua`).

The chunk header (from the installed corruption build, re-verified 2026-08-18 via
`scripts/dump_bytecode_header.py`):

```
game:   1b 4c 75 70 51 01 04 04 04 06 08 09 09 08 b6 09 ...
                ^^  ^^  ^^  ^^  ^^  ^^  ^^  ^^  ^^
vanilla 1b 4c 75 61 51 01 04 04 04 04 08 00 00 00 ...
```

- **Signature `\x1bLup`** — custom fork magic (vanilla: `\x1bLua`). This alone
  makes vanilla `luaU_undump` reject the chunk.
- `0x51` = Lua 5.1 (version matches vanilla)
- `sizeof(Instruction) = 0x06` (vanilla: 0x04) — **non-standard instruction size**
- `sizeof(lua_Number) = 0x08` (double, matches vanilla)
- trailing `09 09 08` vs vanilla `00 00 00` — custom size fields (the fork's
  `sizeof(lu_byte)`/`sizeof(Instruction)`/integral layout differs)

**Conclusion:** Petroglyph compiled a modified Lua 5.1 (custom magic, `Instruction`
size / `lua_Number` layout). Vanilla Lua 5.1.5 **cannot load the game's bytecode**
(verified: `bad header in precompiled chunk`).

**Implications for the engine:**
- The reimplementation **cannot execute the game's precompiled AI scripts** without
  reimplementing the exact fork (opcode layout + VM semantics).
- The mod-compat contract is affected: mods that ship **source** Lua work with any 5.1;
  mods that ship the game's bytecode format only work with the fork.
- Community docs (Alamo Engine Tools) document the *source-level* API (PGBase etc.) —
  mods overwhelmingly ship source, so the engine should target **source Lua 5.1** and
  document the bytecode limitation.

## Bytecode Format Reverse-Engineering (2026-08-18)

The game's loader was located in the exe (`FUN_1407c3f30` header checker,
`FUN_1407c3cb0` LoadFunction, `FUN_1407c4490` LoadString, `FUN_1407c3980` raw
reader, `FUN_1407c4260`/`FUN_1407c3a50` loaders) and the chunk format was
partially decoded. **Confirmed:**

- **Header (22 bytes):** magic `\x1bLup`(4) + version `0x51`(1) + format
  `0x01`(1) + 8 size bytes `04 04 04 06 08 09 09 08`(8) + 8-byte number
  format constant `b6 09 93 68 e7 f5 7d 41`(8). The size bytes are
  endianness(4), sizeof(int)(4), sizeof(size_t)(4), sizeof(Instruction)(4),
  then custom type sizes 6, 8, 9, 9, and sizeof(number)(8).
- **Top-level function:** source string (4-byte length), linedefined(int),
  lastlinedefined(int), nups/numparams/is_vararg/maxstacksize (4 bytes),
  then code count + code (4-byte instructions), constants, upvalues, protos.
- **Constants:** `[type byte][4-byte length][data]` — type 4 = string
  (length-prefixed), 3 = number (8 bytes), 1 = boolean, 0 = nil.
- **Nested proto source:** TValue-like `[type byte][4-byte len][string]`
  (or nil) — different from the top-level plain string.

**Remaining unknown:** the exact field ORDER inside nested protos (the walk
parses the top function but diverges in nested protos — the code count read
goes wrong after the TValue source). The instructions are 4 bytes in the
stream but the VM's `Instruction` is 6 bytes (`FUN_1407c0090` expands them);
the opcode numbering is fork-specific.

**Tooling:** `scripts/dump_bytecode_header.py`, `scripts/walk_all_bytecode.py`,
`scripts/trace_bytecode.py` (partial walker), `ghidra/lua_undump_*.txt`
(loader decompiles). A full loader is the remaining Tier-3 work.

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
(specifically 5.0.2, per community knowledge). The 64-bit remaster's embedded runtime is
the `FUN_1407b9xxx` function cluster (Lua C API calls). Bytecode header confirms **Lua 5.1
with custom fork sizes**.

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
