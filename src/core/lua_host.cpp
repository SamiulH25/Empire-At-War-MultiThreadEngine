#include "core/lua_host.h"
#include "core/lup_loader.h"

#include <cstring>
#include <stdexcept>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

namespace eaw {

namespace {

// The game's custom bytecode magic: \x1bLup (vanilla Lua is \x1bLua).
constexpr char kLupMagic[4] = {'\x1b', 'L', 'u', 'p'};

} // namespace

// Loads a Lua chunk. Source passes through luaL_loadbuffer; the game's
// custom `\x1bLup` bytecode dialect goes through the reverse-engineered
// loader in lup_loader.cpp.
int LuaHost::loadChunk(const std::string& chunk, const std::string& name) {
    if (chunk.size() >= 4 &&
        std::memcmp(chunk.data(), kLupMagic, 4) == 0) {
        return loadLupChunk(L_, chunk.data(), chunk.size(), name);
    }
    return luaL_loadbuffer(L_, chunk.data(), chunk.size(), name.c_str());
}

LuaHost::LuaHost() {
    L_ = luaL_newstate();
    if (!L_) throw LuaError("failed to create Lua state");
    luaL_openlibs(L_);
}

LuaHost::~LuaHost() {
    if (L_) lua_close(L_);
}

void LuaHost::runScript(const std::string& chunk, const std::string& name) {
    if (loadChunk(chunk, name) != 0) {
        std::string err = lua_tostring(L_, -1);
        lua_pop(L_, 1);
        throw LuaError("lua syntax: " + err);
    }
    if (lua_pcall(L_, 0, 0, 0) != 0) {
        std::string err = lua_tostring(L_, -1);
        lua_pop(L_, 1);
        throw LuaError("lua run: " + err);
    }
}

void LuaHost::callGlobal(const std::string& name) {
    lua_getglobal(L_, name.c_str());
    if (!lua_isfunction(L_, -1)) {
        lua_pop(L_, 1);
        throw LuaError("lua: global '" + name + "' is not a function");
    }
    if (lua_pcall(L_, 0, 0, 0) != 0) {
        std::string err = lua_tostring(L_, -1);
        lua_pop(L_, 1);
        throw LuaError("lua call " + name + ": " + err);
    }
}

int LuaHost::createCoroutine(const std::string& funcName) {
    lua_getglobal(L_, funcName.c_str());
    if (!lua_isfunction(L_, -1)) {
        lua_pop(L_, 1);
        throw LuaError("lua: '" + funcName + "' is not a function for coroutine");
    }
    // Create a thread and move the function onto it as its initial stack.
    lua_newthread(L_);                    // [func, thread]
    lua_State* co = lua_tothread(L_, -1);
    lua_pushvalue(L_, -2);                // [func, thread, func]
    lua_xmove(L_, co, 1);                 // move func onto co's stack: [func, thread]
    int handle = luaL_ref(L_, LUA_REGISTRYINDEX); // store thread in registry, pops it
    lua_pop(L_, 1);                       // pop the original func
    return handle;
}

bool LuaHost::resumeCoroutine(int handle) {
    lua_rawgeti(L_, LUA_REGISTRYINDEX, handle); // the thread
    if (!lua_isthread(L_, -1)) {
        lua_pop(L_, 1);
        throw LuaError("lua: handle is not a thread");
    }
    lua_State* co = lua_tothread(L_, -1);
    int status = lua_resume(co, 0);
    lua_pop(L_, 1);
    if (status == 0) return false;       // finished
    if (status == LUA_YIELD) return true; // yielded
    // error
    std::string err = lua_tostring(co, -1);
    throw LuaError("lua coroutine error: " + err);
}

} // namespace eaw
