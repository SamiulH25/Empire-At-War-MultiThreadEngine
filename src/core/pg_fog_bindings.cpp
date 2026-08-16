#include "core/pg_fog_bindings.h"

#include "core/lua_wrappers.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

namespace eaw {

namespace {

// Upvalue 1 = SimState*.
SimState* simFromUpvalue(lua_State* s) {
    return static_cast<SimState*>(lua_touserdata(s, lua_upvalueindex(1)));
}

int fogReveal(lua_State* s) {
    SimState* sim = simFromUpvalue(s);
    Wrapper* p = checkWrapper(s, 1);
    if (p->kind != WrapperKind::Player) {
        return luaL_error(s, "FogOfWar.Reveal: expected player");
    }
    Vec3 center;
    if (!targetPosition(s, 2, center)) {
        return luaL_error(s, "FogOfWar.Reveal: expected position");
    }
    double radius = luaL_checknumber(s, 3);
    sim->revealArea(p->id, center, radius);
    return 0;
}

int fogTemporaryReveal(lua_State* s) {
    // Our fog tier has no timed expiry; treat as permanent.
    return fogReveal(s);
}

int fogRevealAll(lua_State* s) {
    SimState* sim = simFromUpvalue(s);
    Wrapper* p = checkWrapper(s, 1);
    if (p->kind != WrapperKind::Player) {
        return luaL_error(s, "FogOfWar.Reveal_All: expected player");
    }
    sim->revealAll(p->id);
    return 0;
}

int fogDisableRendering(lua_State* s) {
    (void)s; // rendering hint; headless sim ignores it
    return 0;
}

} // namespace

void registerFogBindings(LuaHost& lua, SimState& sim) {
    lua_State* s = lua.state();
    lua_newtable(s); // FogOfWar
    lua_pushlightuserdata(s, &sim);
    lua_pushcclosure(s, fogReveal, 1);
    lua_setfield(s, -2, "Reveal");
    lua_pushlightuserdata(s, &sim);
    lua_pushcclosure(s, fogTemporaryReveal, 1);
    lua_setfield(s, -2, "Temporary_Reveal");
    lua_pushlightuserdata(s, &sim);
    lua_pushcclosure(s, fogRevealAll, 1);
    lua_setfield(s, -2, "Reveal_All");
    lua_pushlightuserdata(s, &sim);
    lua_pushcclosure(s, fogDisableRendering, 1);
    lua_setfield(s, -2, "Disable_Rendering");
    lua_setglobal(s, "FogOfWar");
}

} // namespace eaw
