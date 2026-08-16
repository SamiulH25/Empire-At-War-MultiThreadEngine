// Unit tests for the Lua host.
#include "core/lua_host.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include <cstdio>
#include <string>

namespace {

int failures = 0;

void check(bool cond, const char* what) {
    if (!cond) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    } else {
        std::printf("ok:   %s\n", what);
    }
}

void testRunScript() {
    eaw::LuaHost lua;
    lua.runScript("counter = 0");
    check(lua.state() != nullptr, "state created");
}

void testGlobalState() {
    eaw::LuaHost lua;
    lua.runScript("x = 41");
    lua.runScript("x = x + 1");
    lua.runScript("function getx() return x end");
    // call getx and check return via raw lua call
    lua_getglobal(lua.state(), "getx");
    lua_pcall(lua.state(), 0, 1, 0);
    int val = static_cast<int>(lua_tointeger(lua.state(), -1));
    lua_pop(lua.state(), 1);
    check(val == 42, "global state persists across scripts");
}

void testCallGlobal() {
    eaw::LuaHost lua;
    lua.runScript("function hello() end");
    lua.callGlobal("hello"); // no throw = ok
    check(true, "callGlobal runs a function");
}

void testError() {
    eaw::LuaHost lua;
    bool threw = false;
    try { lua.runScript("this is not lua !!!"); }
    catch (const eaw::LuaError&) { threw = true; }
    check(threw, "syntax error throws");

    threw = false;
    lua.runScript("function bad() error('boom') end");
    try { lua.callGlobal("bad"); }
    catch (const eaw::LuaError& e) {
        threw = std::string(e.what()).find("boom") != std::string::npos;
    }
    check(threw, "runtime error carries message");
}

void testCoroutine() {
    eaw::LuaHost lua;
    lua.runScript(
        "function worker()\n"
        "  coroutine.yield(1)\n"
        "  coroutine.yield(2)\n"
        "  return 3\n"
        "end\n");
    int h = lua.createCoroutine("worker");
    check(h != LUA_NOREF, "coroutine created");
    bool yielded = lua.resumeCoroutine(h);
    check(yielded, "first resume yields");
    yielded = lua.resumeCoroutine(h);
    check(yielded, "second resume yields");
    bool done = !lua.resumeCoroutine(h);
    check(done, "third resume finishes");
}

void testModStyleScript() {
    // A script in the style of the game's PG* Lua (globals + coroutine-ish)
    eaw::LuaHost lua;
    lua.runScript(
        "Local_Player = nil\n"
        "function Set_Player(p) Local_Player = p end\n"
        "function Get_Player() return Local_Player end\n");
    lua.runScript("Set_Player('rebel')");
    lua_getglobal(lua.state(), "Get_Player");
    lua_pcall(lua.state(), 0, 1, 0);
    const char* v = lua_tostring(lua.state(), -1);
    check(v && std::string(v) == "rebel", "PG-style global get/set works");
    lua_pop(lua.state(), 1);
}

} // namespace

int main() {
    testRunScript();
    testGlobalState();
    testCallGlobal();
    testError();
    testCoroutine();
    testModStyleScript();
    if (failures == 0) {
        std::printf("ALL TESTS PASSED\n");
        return 0;
    }
    std::printf("%d FAILURES\n", failures);
    return 1;
}
