// Tests for ScriptManager: script loading from the file manager,
// per-frame thread pumping, engine time.
#include "core/meg_file.h"
#include "core/meg_manager.h"
#include "core/script_manager.h"

extern "C" {
#include "lua.h"
}

#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;
void check(bool c, const char* w) {
    std::printf("%s: %s\n", c ? "ok" : "FAIL", w);
    if (!c) ++failures;
}

// Minimal synthetic meg with one file (same layout as test_meg_manager).
std::vector<uint8_t> makeMeg(const std::string& name, const std::string& content) {
    std::vector<uint8_t> b;
    auto put16 = [&](uint16_t v) { b.push_back(v & 0xff); b.push_back(v >> 8); };
    auto put32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) { b.push_back(v & 0xff); v >>= 8; } };
    put32(1); put32(1);
    put16(static_cast<uint16_t>(name.size()));
    b.insert(b.end(), name.begin(), name.end());
    size_t dataStart = 8 + 2 + name.size() + 20;
    put32(0xdeadbeef); put32(0); put32(static_cast<uint32_t>(content.size()));
    put32(static_cast<uint32_t>(dataStart)); put32(0);
    b.insert(b.end(), content.begin(), content.end());
    return b;
}

void testLoadFromFiles() {
    auto meg = makeMeg("DATA\\SCRIPTS\\AI\\TESTPLAN.LUA",
                       "function Plan() return 1 end\n");
    eaw::MegFile mf = eaw::MegFile::Parse(meg);
    eaw::MegaFileManager files;
    files.addArchive("test.meg", meg, mf);

    eaw::ScriptManager sm(files);
    sm.loadScript("DATA\\SCRIPTS\\AI\\TESTPLAN.LUA");
    sm.runScript("got = Plan()\n");
    lua_getglobal(sm.state(), "got");
    check(lua_tointeger(sm.state(), -1) == 1, "script loaded from meg runs");
    lua_pop(sm.state(), 1);
}

void testLooseOverride() {
    auto meg = makeMeg("DATA\\SCRIPTS\\AI\\TESTPLAN.LUA",
                       "function Plan() return 1 end\n");
    eaw::MegFile mf = eaw::MegFile::Parse(meg);
    eaw::MegaFileManager files;
    files.addArchive("test.meg", meg, mf);
    // Loose file overrides the meg (the mod mechanism).
    files.addLooseFile("DATA\\SCRIPTS\\AI\\TESTPLAN.LUA",
                       std::vector<uint8_t>{
                           'f','u','n','c','t','i','o','n',' ','P','l','a','n','(',
                           ')',' ','r','e','t','u','r','n',' ','2',' ','e','n','d','\n'});

    eaw::ScriptManager sm(files);
    sm.loadScript("DATA\\SCRIPTS\\AI\\TESTPLAN.LUA");
    sm.runScript("got = Plan()\n");
    lua_getglobal(sm.state(), "got");
    check(lua_tointeger(sm.state(), -1) == 2, "loose file overrides meg script");
    lua_pop(sm.state(), 1);
}

void testPumpAdvancesTime() {
    eaw::MegaFileManager files;
    eaw::ScriptManager sm(files);
    check(sm.time() == 0.0, "time starts at 0");
    sm.pump(0.1);
    sm.pump(0.2);
    check(sm.time() > 0.299 && sm.time() < 0.301, "pump advances engine time");
    sm.runScript("t = GetCurrentTime()\n");
    lua_getglobal(sm.state(), "t");
    double t = lua_tonumber(sm.state(), -1);
    check(t > 0.299 && t < 0.301, "scripts see engine time");
    lua_pop(sm.state(), 1);
}

void testThreadLifecycle() {
    eaw::MegaFileManager files;
    eaw::ScriptManager sm(files);
    check(sm.threadCount() == 0, "no threads initially");
    sm.runScript(
        "count = 0\n"
        "function worker()\n"
        "  count = count + 1\n"
        "  coroutine.yield()\n"
        "  count = count + 10\n"
        "end\n"
        "Create_Thread('worker')\n");
    check(sm.threadCount() == 1, "one thread after Create_Thread");
    sm.pump(0.016); // count = 1, yields
    check(sm.threadCount() == 1, "yielded thread still alive");
    sm.pump(0.016); // count = 11, finishes
    check(sm.threadCount() == 0, "finished thread removed");
    lua_getglobal(sm.state(), "count");
    check(lua_tointeger(sm.state(), -1) == 11, "thread ran across pumps");
    lua_pop(sm.state(), 1);
}

void testThreadErrorPropagates() {
    eaw::MegaFileManager files;
    eaw::ScriptManager sm(files);
    sm.runScript(
        "function bad()\n"
        "  error('kaboom')\n"
        "end\n"
        "Create_Thread('bad')\n");
    bool threw = false;
    try { sm.pump(0.016); }
    catch (const eaw::LuaError& e) {
        threw = std::string(e.what()).find("kaboom") != std::string::npos;
    }
    check(threw, "thread error propagates with message");
}

void testRequireResolvesMegScript() {
    // require("PGTaskForce") must resolve through the file manager to
    // DATA\SCRIPTS\LIBRARY\PGTASKFORCE.LUA (the game's module convention).
    auto lib = makeMeg("DATA\\SCRIPTS\\LIBRARY\\PGTASKFORCE.LUA",
                       "function TF_Assemble() return 42 end\n");
    eaw::MegFile mf = eaw::MegFile::Parse(lib);
    eaw::MegaFileManager files;
    files.addArchive("lib.meg", lib, mf);

    eaw::ScriptManager sm(files);
    sm.runScript("require('PGTaskForce')\ngot = TF_Assemble()\n");
    lua_getglobal(sm.state(), "got");
    check(lua_tointeger(sm.state(), -1) == 42,
          "require resolves a library script from the meg");
    lua_pop(sm.state(), 1);
}

void testRequireNativeModuleStub() {
    // The game's native binding modules (PGAICommands, pgcommands,
    // PGBaseDefinitions) have no Lua file; require must return an empty table
    // so library scripts can load and define their functions.
    eaw::MegaFileManager files;
    eaw::ScriptManager sm(files);
    sm.runScript(
        "m = require('PGAICommands')\n"
        "ok = type(m) == 'table'\n");
    lua_getglobal(sm.state(), "ok");
    check(lua_toboolean(sm.state(), -1), "native module require returns a table");
    lua_pop(sm.state(), 1);
}

} // namespace

int main() {
    testLoadFromFiles();
    testLooseOverride();
    testPumpAdvancesTime();
    testThreadLifecycle();
    testThreadErrorPropagates();
    testRequireResolvesMegScript();
    testRequireNativeModuleStub();
    if (failures == 0) { std::printf("ALL TESTS PASSED\n"); return 0; }
    std::printf("%d FAILURES\n", failures);
    return 1;
}
