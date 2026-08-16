// Tests for the PG* Lua bindings (mod script surface).
#include "core/lua_host.h"
#include "core/pg_bindings.h"

extern "C" {
#include "lua.h"
}

#include <cstdio>
#include <string>

namespace {

int failures = 0;
void check(bool c, const char* w) {
    std::printf("%s: %s\n", c ? "ok" : "FAIL", w);
    if (!c) ++failures;
}

void testCreateThread() {
    eaw::LuaHost lua;
    eaw::registerPgBindings(lua);
    lua.runScript(
        "result = 0\n"
        "function worker(v)\n"
        "  result = v * 2\n"
        "end\n"
        "id = Create_Thread('worker', 21)\n");
    lua_getglobal(lua.state(), "result");
    int r = static_cast<int>(lua_tointeger(lua.state(), -1));
    lua_pop(lua.state(), 1);
    check(r == 42, "Create_Thread runs the function with param");
}

void testThreadId() {
    eaw::LuaHost lua;
    eaw::registerPgBindings(lua);
    lua.runScript(
        "function noop() end\n"
        "a = Create_Thread('noop')\n"
        "b = Create_Thread('noop')\n");
    lua_getglobal(lua.state(), "a");
    int a = static_cast<int>(lua_tointeger(lua.state(), -1));
    lua_pop(lua.state(), 1);
    lua_getglobal(lua.state(), "b");
    int b = static_cast<int>(lua_tointeger(lua.state(), -1));
    lua_pop(lua.state(), 1);
    check(a == 1 && b == 2, "thread ids increment");
}

void testGlobalValue() {
    eaw::LuaHost lua;
    eaw::registerPgBindings(lua);
    lua.runScript(
        "GlobalValue_Set('key', 7)\n"
        "v = GlobalValue_Get('key')\n");
    lua_getglobal(lua.state(), "v");
    int v = static_cast<int>(lua_tointeger(lua.state(), -1));
    lua_pop(lua.state(), 1);
    check(v == 7, "GlobalValue set/get roundtrip");
}

void testGameRandom() {
    eaw::LuaHost lua;
    eaw::registerPgBindings(lua);
    lua.runScript(
        "x = GameRandom(1, 1)\n"
        "f = GameRandom_Get_Float()\n");
    lua_getglobal(lua.state(), "x");
    int x = static_cast<int>(lua_tointeger(lua.state(), -1));
    lua_pop(lua.state(), 1);
    check(x == 1, "GameRandom(1,1) == 1");
}

void testModStyleScriptWithThreads() {
    // A script in the style of the game's AI plans: defines a worker,
    // spawns it via Create_Thread, uses GlobalValue.
    eaw::LuaHost lua;
    eaw::registerPgBindings(lua);
    lua.runScript(
        "GlobalValue_Set('Build_Queue', {})\n"
        "function Build_Plan()\n"
        "  GlobalValue_Set('Phase', 'building')\n"
        "end\n"
        "Create_Thread('Build_Plan')\n");
    lua.runScript("phase = GlobalValue_Get('Phase')\n");
    lua_getglobal(lua.state(), "phase");
    const char* p = lua_tostring(lua.state(), -1);
    check(p && std::string(p) == "building", "mod-style script thread writes global");
    lua_pop(lua.state(), 1);
}

} // namespace

int main() {
    testCreateThread();
    testThreadId();
    testGlobalValue();
    testGameRandom();
    testModStyleScriptWithThreads();
    if (failures == 0) { std::printf("ALL TESTS PASSED\n"); return 0; }
    std::printf("%d FAILURES\n", failures);
    return 1;
}
