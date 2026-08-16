#include "core/pg_event_bindings.h"

#include "core/lua_wrappers.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

namespace eaw {

namespace {

// Upvalue layout for event bindings: 1 = EventSystem*, 2 = SimState*.
EventSystem* eventsFromUpvalue(lua_State* s, int idx) {
    return static_cast<EventSystem*>(lua_touserdata(s, lua_upvalueindex(idx)));
}

// ---- registration -------------------------------------------------------

int registerTimer(lua_State* s) {
    EventSystem* ev = eventsFromUpvalue(s, 1);
    // (func, timeout, param?)
    if (!lua_isfunction(s, 1)) {
        return luaL_error(s, "Register_Timer: expected function");
    }
    double timeout = luaL_checknumber(s, 2);
    double now = 0.0;
    // The bindings can't see engine time directly; the ScriptManager stashes
    // it in a registry field (see pg_bindings GetCurrentTime).
    lua_getfield(s, LUA_REGISTRYINDEX, "__PgEngineTime");
    now = lua_tonumber(s, -1);
    lua_pop(s, 1);

    // luaL_ref pops the value, so save copies of the args first and restore
    // the frame before returning (a C function must leave its args in place).
    int nargs = lua_gettop(s);
    lua_pushvalue(s, 1);                   // [func, timeout, param?, func]
    int funcRef = luaL_ref(s, LUA_REGISTRYINDEX); // pops func copy
    int paramRef = LUA_NOREF;
    if (nargs >= 3 && !lua_isnoneornil(s, 3)) {
        lua_pushvalue(s, 3);               // [func, timeout, param, param]
        paramRef = luaL_ref(s, LUA_REGISTRYINDEX); // pops param copy
    }
    ev->registerTimer(funcRef, paramRef, now, timeout);
    return 0;
}

int registerDeathEvent(lua_State* s) {
    EventSystem* ev = eventsFromUpvalue(s, 1);
    SimState* sim = simFromUpvalue(s, 2);
    Wrapper* w = checkWrapper(s, 1);
    if (w->kind != WrapperKind::Object) return luaL_error(s, "Register_Death_Event: expected object");
    if (!lua_isfunction(s, 2)) return luaL_error(s, "Register_Death_Event: expected function");
    if (!sim->object(w->id)) return 0; // object already gone — ignore
    lua_pushvalue(s, 2);
    int funcRef = luaL_ref(s, LUA_REGISTRYINDEX);
    ev->registerDeath(w->id, funcRef);
    return 0;
}

int registerAttackedEvent(lua_State* s) {
    EventSystem* ev = eventsFromUpvalue(s, 1);
    Wrapper* w = checkWrapper(s, 1);
    if (w->kind != WrapperKind::Object) return luaL_error(s, "Register_Attacked_Event: expected object");
    if (!lua_isfunction(s, 2)) return luaL_error(s, "Register_Attacked_Event: expected function");
    lua_pushvalue(s, 2);
    int funcRef = luaL_ref(s, LUA_REGISTRYINDEX);
    ev->registerAttacked(w->id, funcRef);
    return 0;
}

int cancelAttackedEvent(lua_State* s) {
    EventSystem* ev = eventsFromUpvalue(s, 1);
    Wrapper* w = checkWrapper(s, 1);
    if (w->kind != WrapperKind::Object) return luaL_error(s, "Cancel_Attacked_Event: expected object");
    ev->cancelAttacked(w->id);
    return 0;
}

int registerProx(lua_State* s) {
    EventSystem* ev = eventsFromUpvalue(s, 1);
    Wrapper* w = checkWrapper(s, 1);
    if (w->kind != WrapperKind::Object) return luaL_error(s, "Register_Prox: expected object");
    if (!lua_isfunction(s, 2)) return luaL_error(s, "Register_Prox: expected function");
    double range = luaL_checknumber(s, 3);
    int playerFilter = 0;
    if (!lua_isnoneornil(s, 4)) {
        Wrapper* p = checkWrapper(s, 4);
        if (p->kind != WrapperKind::Player) return luaL_error(s, "Register_Prox: expected player filter");
        playerFilter = p->id;
    }
    lua_pushvalue(s, 2);
    int funcRef = luaL_ref(s, LUA_REGISTRYINDEX);
    ev->registerProx(w->id, funcRef, range, playerFilter);
    return 0;
}

// ---- processing ---------------------------------------------------------

int processTimers(lua_State* s) {
    EventSystem* ev = eventsFromUpvalue(s, 1);
    lua_getfield(s, LUA_REGISTRYINDEX, "__PgEngineTime");
    double now = lua_tonumber(s, -1);
    lua_pop(s, 1);
    ev->processTimers(now);
    return 0;
}

int processDeathEvents(lua_State* s) {
    eventsFromUpvalue(s, 1)->processDeaths();
    return 0;
}

int processAttackedEvents(lua_State* s) {
    eventsFromUpvalue(s, 1)->processAttacked();
    return 0;
}

int processProximities(lua_State* s) {
    eventsFromUpvalue(s, 1)->processProximities();
    return 0;
}

int pumpService(lua_State* s) {
    EventSystem* ev = eventsFromUpvalue(s, 1);
    lua_getfield(s, LUA_REGISTRYINDEX, "__PgEngineTime");
    double now = lua_tonumber(s, -1);
    lua_pop(s, 1);
    ev->pump(now);
    return 0;
}

} // namespace

void registerEventBindings(LuaHost& lua, EventSystem& events, SimState& sim) {
    lua_State* s = lua.state();

    // All event bindings share upvalues: (EventSystem*, SimState*).
    auto reg = [&](const char* name, lua_CFunction fn) {
        lua_pushlightuserdata(s, &events);
        lua_pushlightuserdata(s, &sim);
        lua_pushcclosure(s, fn, 2);
        lua_setglobal(s, name);
    };

    reg("Register_Timer", registerTimer);
    reg("Register_Death_Event", registerDeathEvent);
    reg("Register_Attacked_Event", registerAttackedEvent);
    reg("Cancel_Attacked_Event", cancelAttackedEvent);
    reg("Register_Prox", registerProx);
    reg("Process_Timers", processTimers);
    reg("Process_Death_Events", processDeathEvents);
    reg("Process_Attacked_Events", processAttackedEvents);
    reg("Process_Proximities", processProximities);
    reg("Pump_Service", pumpService);
}

} // namespace eaw
