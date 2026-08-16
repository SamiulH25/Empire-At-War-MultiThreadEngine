// Shared Lua wrapper-userdata machinery for the engine bindings.
//
// The game wraps C++ objects (GameObject, Player, ObjectType, Position) as
// Lua userdata with a shared metatable ("LuaWrapperMetaTable", a name that
// appears in the game's own PGBASE bytecode). Both the object bindings and
// the event bindings create/consume these wrappers, so the struct and the
// helpers live here.
#pragma once

#include "core/object_model.h"

extern "C" {
#include "lauxlib.h"
}

namespace eaw {

enum class WrapperKind : int {
    Object = 1,
    Player = 2,
    Type = 3,
    Position = 4,
    Command = 5,
    TaskForce = 6,
};

struct Wrapper {
    SimState* sim = nullptr;
    WrapperKind kind = WrapperKind::Object;
    int id = 0;
    Vec3 pos;          // Position wrappers carry their coords inline
    double result = 0; // Command blocks carry a result value (0 = nil/false)
    bool finished = true;
};

constexpr const char* kWrapperMeta = "LuaWrapperMetaTable";

inline Wrapper* checkWrapper(lua_State* s, int idx) {
    return static_cast<Wrapper*>(luaL_checkudata(s, idx, kWrapperMeta));
}

// Global query functions receive the SimState as a lightuserdata upvalue.
inline SimState* simFromUpvalue(lua_State* s, int idx) {
    return static_cast<SimState*>(lua_touserdata(s, lua_upvalueindex(idx)));
}

// Pushes a wrapper userdata of the given kind/id.
inline void pushWrapper(lua_State* s, SimState* sim, WrapperKind kind, int id) {
    Wrapper* w = static_cast<Wrapper*>(lua_newuserdata(s, sizeof(Wrapper)));
    w->sim = sim;
    w->kind = kind;
    w->id = id;
    luaL_getmetatable(s, kWrapperMeta);
    lua_setmetatable(s, -2);
}

// Pushes a wrapper for a game object, or nil if the object is gone or dead.
inline void pushObject(lua_State* s, SimState* sim, const GameObject* o) {
    if (!o || !o->alive) {
        lua_pushnil(s);
        return;
    }
    pushWrapper(s, sim, WrapperKind::Object, o->id);
}

// Pushes a wrapper for an object by id without the alive check (death-event
// callbacks receive the object even after it died).
inline void pushObjectHandle(lua_State* s, SimState* sim, int id) {
    pushWrapper(s, sim, WrapperKind::Object, id);
}

// Returns the object behind an Object wrapper (null if not an object wrapper
// or gone from the sim). Dead objects remain readable — Is_Valid() is the
// documented liveness check (death callbacks query the dying object).
inline const GameObject* wrapperObject(lua_State* s, Wrapper* w) {
    if (w->kind != WrapperKind::Object) return nullptr;
    return w->sim->object(w->id);
}

// The documented liveness check: the object must exist and be alive.
inline bool wrapperObjectValid(lua_State* s, Wrapper* w) {
    const GameObject* o = wrapperObject(s, w);
    return o && o->alive;
}

// Resolves a target arg (Object or Position wrapper) to a position; returns
// false if the arg is neither.
inline bool targetPosition(lua_State* s, int idx, Vec3& out) {
    if (!lua_isuserdata(s, idx)) return false;
    Wrapper* w = checkWrapper(s, idx);
    if (w->kind == WrapperKind::Position) { out = w->pos; return true; }
    if (w->kind == WrapperKind::Object) {
        const GameObject* o = wrapperObject(s, w);
        if (!o) return false;
        out = o->position;
        return true;
    }
    return false;
}

// Pushes a command block wrapper (finished immediately; sim has no async
// movement for blocks yet, but scripts can poll IsFinished/Result).
inline void pushCommandBlock(lua_State* s, SimState* sim, double result) {
    pushWrapper(s, sim, WrapperKind::Command, 0);
    Wrapper* cw = static_cast<Wrapper*>(lua_touserdata(s, -1));
    cw->result = result;
    cw->finished = true;
}

} // namespace eaw
