#include "core/pg_bindings.h"

#include <cstdint>
#include <random>
#include <string>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

namespace eaw {

namespace {

// ---- helpers -----------------------------------------------------------

lua_State* L(lua_State* s) { return s; }

int pgError(lua_State* s, const char* msg) {
    lua_pushstring(s, msg);
    return lua_error(s);
}

// ---- global values (GlobalValue.Get/Set) --------------------------------

int globalValueGet(lua_State* s) {
    const char* key = luaL_checkstring(s, 1);
    lua_getfield(s, LUA_REGISTRYINDEX, "__PgGlobalValues");
    lua_getfield(s, -1, key);
    return 1; // value (or nil)
}

int globalValueSet(lua_State* s) {
    const char* key = luaL_checkstring(s, 1);
    lua_getfield(s, LUA_REGISTRYINDEX, "__PgGlobalValues");
    lua_pushvalue(s, 2);
    lua_setfield(s, -2, key);
    lua_pop(s, 1);
    return 0;
}

// ---- game time ----------------------------------------------------------

int getCurrentTime(lua_State* s) {
    lua_pushnumber(s, 0.0); // engine time; sim will drive this
    return 1;
}

// ---- random -------------------------------------------------------------

int gameRandom(lua_State* s) {
    int lo = static_cast<int>(luaL_checkinteger(s, 1));
    int hi = static_cast<int>(luaL_checkinteger(s, 2));
    static std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist(lo, hi);
    lua_pushinteger(s, dist(rng));
    return 1;
}

int gameRandomFloat(lua_State* s) {
    static std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    lua_pushnumber(s, dist(rng));
    return 1;
}

// ---- threads ------------------------------------------------------------
// Create_Thread(name, param): starts _G[name] in a new coroutine next frame.
// We implement it synchronously (start immediately) since we have no frame
// pump yet; the coroutine handle is stored in a table keyed by thread id.

int createThread(lua_State* s) {
    const char* name = luaL_checkstring(s, 1);
    const bool hasParam = !lua_isnoneornil(s, 2); // capture before any pushes
    lua_getglobal(s, name);
    if (!lua_isfunction(s, -1)) {
        return pgError(s, "Create_Thread: function not found");
    }
    // Thread id
    lua_getfield(s, LUA_REGISTRYINDEX, "__PgThreadCounter");
    int id = static_cast<int>(lua_tointeger(s, -1));
    lua_pop(s, 1);
    lua_pushinteger(s, id + 1);
    lua_setfield(s, LUA_REGISTRYINDEX, "__PgThreadCounter");

    // Stack: [name, param?, func] — func is the top value from lua_getglobal.
    // Create the coroutine thread; the coroutine's stack must be
    // [func, args...] — func first, then param.
    lua_newthread(s);                     // [name, param?, func, thread]
    lua_State* co = lua_tothread(s, -1);
    lua_pushvalue(s, -2);                 // copy func (below the thread)
    lua_xmove(s, co, 1);                  // co: [func]
    int nargs = 0;                        // args AFTER the function
    if (hasParam) {
        lua_pushvalue(s, -3);             // copy param
        lua_xmove(s, co, 1);              // co: [func, param]
        nargs = 1;
    }

    // Store thread in __PgThreads[id]
    // Stack: [name, param?, func, thread]
    lua_getfield(s, LUA_REGISTRYINDEX, "__PgThreads"); // [name, param?, func, thread, threads]
    lua_pushvalue(s, -2);                 // [name, param?, func, thread, threads, thread]
    lua_setfield(s, -2, std::to_string(id).c_str());   // [name, param?, func, thread, threads]
    lua_pop(s, 2);                        // [name, param?, func, thread]

    // Start the coroutine now.
    int status = lua_resume(co, nargs);
    if (status != 0 && status != LUA_YIELD) {
        std::string msg = lua_tostring(co, -1) ? lua_tostring(co, -1) : "unknown coroutine error";
        lua_pushstring(s, ("Create_Thread: " + msg).c_str());
        return lua_error(s);
    }
    // Clean up the func/thread values pushed above the C function's frame,
    // then push the result id.
    lua_pop(s, 2);                        // [name, param?]
    lua_pushinteger(s, id);
    return 1;
}

int threadIsActive(lua_State* s) {
    int id = static_cast<int>(luaL_checkinteger(s, 1));
    lua_getfield(s, LUA_REGISTRYINDEX, "__PgThreads");
    lua_getfield(s, -1, std::to_string(id).c_str());
    int active = lua_isthread(s, -1);
    lua_pop(s, 2);
    lua_pushboolean(s, active);
    return 1;
}

int threadKill(lua_State* s) {
    int id = static_cast<int>(luaL_checkinteger(s, 1));
    lua_getfield(s, LUA_REGISTRYINDEX, "__PgThreads");
    lua_pushnil(s);
    lua_setfield(s, -2, std::to_string(id).c_str());
    lua_pop(s, 1);
    return 0;
}

// ---- misc ------------------------------------------------------------------

int getGameMode(lua_State* s) {
    lua_pushstring(s, "Land"); // engine will set this from the sim
    return 1;
}

int scriptMessage(lua_State* s) {
    const char* msg = luaL_checkstring(s, 1);
    std::fprintf(stderr, "[lua] %s\n", msg);
    return 0;
}

// ---- registration ----------------------------------------------------------

void reg(lua_State* s, const char* name, lua_CFunction fn) {
    lua_register(s, name, fn);
}

} // namespace

void registerPgBindings(LuaHost& lua) {
    lua_State* s = lua.state();

    // backing tables
    lua_newtable(s);
    lua_setfield(s, LUA_REGISTRYINDEX, "__PgGlobalValues");
    lua_newtable(s);
    lua_setfield(s, LUA_REGISTRYINDEX, "__PgThreads");
    lua_pushinteger(s, 1);
    lua_setfield(s, LUA_REGISTRYINDEX, "__PgThreadCounter");

    // global values
    reg(s, "GlobalValue_Get", globalValueGet);
    reg(s, "GlobalValue_Set", globalValueSet);

    // Threads (Create_Thread, Thread, Thread.Create...)
    reg(s, "Create_Thread", createThread);
    reg(s, "Thread", createThread);
    reg(s, "Thread_Is_Thread_Active", threadIsActive);
    reg(s, "Thread_Kill", threadKill);

    // time / random
    reg(s, "GetCurrentTime", getCurrentTime);
    reg(s, "GameRandom", gameRandom);
    reg(s, "GameRandom_Get_Float", gameRandomFloat);

    // misc
    reg(s, "Get_Game_Mode", getGameMode);
    reg(s, "_ScriptMessage", scriptMessage);
    reg(s, "lc", scriptMessage);

    // Note: game-dependent commands (Find_Player, Find_All_Objects_Of_Type,
    // Spawn_Unit, Move_To, etc.) are stubbed by returning nil / no-op for now;
    // they need the sim's object database. Registering only what runs in
    // script load + thread management keeps mod scripts loadable.
}

} // namespace eaw
