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
    // Thread runs on the next pump.
    lua_getglobal(lua.state(), "result");
    check(lua_tointeger(lua.state(), -1) == 0, "thread not run before pump");
    lua_pop(lua.state(), 1);
    eaw::pumpThreads(lua.state());
    lua_getglobal(lua.state(), "result");
    int r = static_cast<int>(lua_tointeger(lua.state(), -1));
    lua_pop(lua.state(), 1);
    check(r == 42, "pump runs the thread with param");
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

void testEngineTime() {
    eaw::LuaHost lua;
    eaw::registerPgBindings(lua);
    lua.runScript("t = GetCurrentTime()\n");
    lua_getglobal(lua.state(), "t");
    check(lua_tonumber(lua.state(), -1) == 0.0, "engine time starts at 0");
    lua_pop(lua.state(), 1);
    eaw::setEngineTime(lua.state(), 12.5);
    lua.runScript("t = GetCurrentTime()\n");
    lua_getglobal(lua.state(), "t");
    check(lua_tonumber(lua.state(), -1) == 12.5, "engine time updates");
    lua_pop(lua.state(), 1);
}

void testYieldPump() {
    // A thread that yields twice then finishes; the pump resumes it once
    // per call, and finished threads are removed.
    eaw::LuaHost lua;
    eaw::registerPgBindings(lua);
    lua.runScript(
        "steps = 0\n"
        "function w()\n"
        "  steps = steps + 1\n"
        "  coroutine.yield()\n"
        "  steps = steps + 10\n"
        "  coroutine.yield()\n"
        "  steps = steps + 100\n"
        "end\n"
        "id = Create_Thread('w')\n");
    eaw::pumpThreads(lua.state()); // steps = 1, yields
    eaw::pumpThreads(lua.state()); // steps = 11, yields
    eaw::pumpThreads(lua.state()); // steps = 111, finishes
    lua_getglobal(lua.state(), "steps");
    check(lua_tointeger(lua.state(), -1) == 111, "thread resumes once per pump");
    lua_pop(lua.state(), 1);
    // Thread finished — resumed again should be a no-op.
    eaw::pumpThreads(lua.state());
    lua_getglobal(lua.state(), "steps");
    check(lua_tointeger(lua.state(), -1) == 111, "finished thread removed");
    lua_pop(lua.state(), 1);
}

void testThreadKill() {
    eaw::LuaHost lua;
    eaw::registerPgBindings(lua);
    lua.runScript(
        "killed = 0\n"
        "function w()\n"
        "  killed = killed + 1\n"
        "  coroutine.yield()\n"
        "  killed = killed + 100\n"
        "end\n"
        "id = Create_Thread('w')\n");
    eaw::pumpThreads(lua.state()); // killed = 1, yields
    lua.runScript("Thread_Kill(id)\n");
    eaw::pumpThreads(lua.state());
    lua_getglobal(lua.state(), "killed");
    check(lua_tointeger(lua.state(), -1) == 1, "Thread_Kill stops the thread");
    lua_pop(lua.state(), 1);
}

void testThreadIsActive() {
    eaw::LuaHost lua;
    eaw::registerPgBindings(lua);
    lua.runScript(
        "function w() coroutine.yield() end\n"
        "id = Create_Thread('w')\n"
        "a = Thread_Is_Thread_Active(id)\n");
    lua_getglobal(lua.state(), "a");
    check(lua_toboolean(lua.state(), -1) == 1, "fresh thread is active");
    lua_pop(lua.state(), 1);
    eaw::pumpThreads(lua.state()); // yields
    eaw::pumpThreads(lua.state()); // finishes, removed
    lua.runScript("b = Thread_Is_Thread_Active(id)\n");
    lua_getglobal(lua.state(), "b");
    check(lua_toboolean(lua.state(), -1) == 0, "finished thread is inactive");
    lua_pop(lua.state(), 1);
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
    eaw::pumpThreads(lua.state());
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
    testEngineTime();
    testYieldPump();
    testThreadKill();
    testThreadIsActive();
    testModStyleScriptWithThreads();
    if (failures == 0) { std::printf("ALL TESTS PASSED\n"); return 0; }
    std::printf("%d FAILURES\n", failures);
    return 1;
}
