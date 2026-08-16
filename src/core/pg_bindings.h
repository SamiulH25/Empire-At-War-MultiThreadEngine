// PG* Lua bindings — the documented EAW scripting surface.
//
// Implements the engine-side subset of the hardcoded Lua commands from
// (Alamo Engine Tools doc): thread management (Create_Thread etc.), global
// values, game time, random, and stubs for game-dependent commands.
// Mod scripts written against the documented API load and run against these.
#pragma once

#include "core/lua_host.h"

namespace eaw {

// Registers the PG* binding surface into a Lua state.
// The host must outlive the state.
void registerPgBindings(LuaHost& lua);

// Resumes every live script thread (created by Create_Thread) once, removing
// finished ones. Called by the engine's per-frame script pump. Throws
// LuaError if a script thread errors.
void pumpThreads(lua_State* s);

// Drives the engine time that GetCurrentTime() returns to scripts.
void setEngineTime(lua_State* s, double t);

// Engine-side state backing the bindings (per-script-manager).
struct PgBindingsState {
    // Thread model: each created "thread" is a coroutine of the named function.
    int nextThreadId = 1;
    // GlobalValue.Get/Set store — string -> Lua value, shared across scripts.
    // Implemented as a Lua table named "__PgGlobalValues".
};

} // namespace eaw
