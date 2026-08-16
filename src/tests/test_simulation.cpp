// Tests for Simulation: the frame loop end-to-end (meg -> script -> sim).
#include "core/meg_file.h"
#include "core/simulation.h"

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

void testTicksAdvanceTime() {
    eaw::Simulation sim;
    check(sim.time() == 0.0, "sim time starts at 0");
    sim.tick(1.0 / 30.0);
    sim.tick(1.0 / 30.0);
    check(sim.time() > 0.066 && sim.time() < 0.067, "tick advances time");
}

void testScriptDefinesAndSpawns() {
    // A script in the game's style: defines a plan, spawns a unit via thread.
    auto meg = makeMeg("DATA\\SCRIPTS\\AI\\TESTPLAN.LUA",
        "function Plan()\n"
        "  p = Find_Player('REBEL')\n"
        "  pos = Create_Position(5, 6, 7)\n"
        "  list = Spawn_Unit('X_WING', pos, p)\n"
        "  spawned = list[1]\n"
        "  SpawnResult = spawned:Get_Name()\n"
        "end\n");
    eaw::MegFile mf = eaw::MegFile::Parse(meg);
    eaw::Simulation sim;
    sim.files().addArchive("test.meg", meg, mf);
    // setup: player + type
    eaw::Player& rebel = sim.sim().addPlayer("Rebel Alliance", "REBEL");
    rebel.human = true;
    eaw::ObjectType xwing;
    xwing.name = "X_WING";
    xwing.categories = {"Fighter"};
    xwing.properties = {"Unit"};
    sim.sim().addType(std::move(xwing));
    // load the plan and run it directly (it uses globals, not a thread)
    sim.scripts().loadScript("DATA\\SCRIPTS\\AI\\TESTPLAN.LUA");
    sim.scripts().runScript("Plan()\n");
    lua_getglobal(sim.scripts().state(), "SpawnResult");
    const char* r = lua_tostring(sim.scripts().state(), -1);
    check(r && std::string(r) == "X_WING", "script spawns a unit via bindings");
    lua_pop(sim.scripts().state(), 1);
    // verify the object exists in the sim
    auto objs = sim.sim().objectsOfType("X_WING");
    check(objs.size() == 1, "spawned unit is in the sim");
    if (objs.size() == 1) {
        check(objs[0]->position.x == 5.0, "spawned at script position");
    }
}

void testScriptThreadRunsAcrossTicks() {
    auto meg = makeMeg("DATA\\SCRIPTS\\AI\\THREADED.LUA",
        "function Worker()\n"
        "  TickCount = 0\n"
        "  while TickCount < 3 do\n"
        "    TickCount = TickCount + 1\n"
        "    coroutine.yield()\n"
        "  end\n"
        "end\n"
        "Create_Thread('Worker')\n");
    eaw::MegFile mf = eaw::MegFile::Parse(meg);
    eaw::Simulation sim;
    sim.files().addArchive("test.meg", meg, mf);
    sim.scripts().loadScript("DATA\\SCRIPTS\\AI\\THREADED.LUA");
    check(sim.scripts().threadCount() == 1, "script thread created");
    sim.tick(1.0 / 30.0);
    sim.tick(1.0 / 30.0);
    sim.tick(1.0 / 30.0);
    sim.tick(1.0 / 30.0); // 4th tick: loop exits, thread finishes
    lua_getglobal(sim.scripts().state(), "TickCount");
    check(lua_tointeger(sim.scripts().state(), -1) == 3, "thread ran across 3 ticks");
    lua_pop(sim.scripts().state(), 1);
    check(sim.scripts().threadCount() == 0, "thread finished and removed");
}

void testEndToEndPump() {
    // Combined: load a plan, pump several ticks, verify state accumulates.
    auto meg = makeMeg("DATA\\SCRIPTS\\AI\\BUILDPLAN.LUA",
        "function Build_Plan()\n"
        "  ticks = 0\n"
        "  for i = 1, 5 do\n"
        "    ticks = ticks + 1\n"
        "    coroutine.yield()\n"
        "  end\n"
        "end\n"
        "Create_Thread('Build_Plan')\n");
    eaw::MegFile mf = eaw::MegFile::Parse(meg);
    eaw::Simulation sim;
    sim.files().addArchive("test.meg", meg, mf);
    sim.scripts().loadScript("DATA\\SCRIPTS\\AI\\BUILDPLAN.LUA");
    for (int i = 0; i < 5; ++i) sim.tick(1.0 / 30.0);
    lua_getglobal(sim.scripts().state(), "ticks");
    check(lua_tointeger(sim.scripts().state(), -1) == 5, "plan ran 5 ticks");
    lua_pop(sim.scripts().state(), 1);
    check(sim.time() > 0.166 && sim.time() < 0.167, "sim time advanced 5 ticks");
}

void testParallelMoveToIntegration() {
    // Script orders a unit to move; the sim's parallel update integrates it
    // over ticks until it arrives.
    eaw::Simulation sim(4); // 4 workers
    eaw::Player& rebel = sim.sim().addPlayer("Rebel Alliance", "REBEL");
    eaw::ObjectType xwing;
    xwing.name = "X_WING";
    xwing.properties = {"Unit"};
    sim.sim().addType(std::move(xwing));
    eaw::ObjectType isd;
    isd.name = "ISD";
    isd.properties = {"Unit"};
    sim.sim().addType(std::move(isd));
    int xwingId = sim.sim().spawnUnit("X_WING", rebel.id, {0, 0, 0});
    int isdId = sim.sim().spawnUnit("ISD", rebel.id, {100, 0, 0});
    (void)isdId;

    sim.scripts().runScript(
        "o = Find_First_Object('X_WING')\n"
        "target = Find_First_Object('ISD')\n"
        "o:Move_To(target)\n");
    // Move speed 50 u/s, 100 units away -> ~2s at 30Hz = 60 ticks.
    for (int i = 0; i < 120; ++i) sim.tick(1.0 / 30.0);
    const eaw::GameObject* moved = sim.sim().object(xwingId);
    check(!moved->hasMoveTarget, "unit arrived and cleared move target");
    double d = moved->position.distanceTo({100, 0, 0});
    check(d < 0.001, "unit reached the target position");
    check(sim.updateTicks() == 120, "parallel update ran every tick");
}

void testParallelDeterminism() {
    // Two sims with identical setup and worker counts must produce identical
    // positions after the same tick sequence (determinism by construction).
    auto setup = [](eaw::Simulation& sim) {
        eaw::Player& p = sim.sim().addPlayer("Rebel Alliance", "REBEL");
        eaw::ObjectType xwing;
        xwing.name = "X_WING";
        xwing.properties = {"Unit"};
        sim.sim().addType(std::move(xwing));
        sim.sim().spawnUnit("X_WING", p.id, {0, 0, 0});
        sim.sim().spawnUnit("X_WING", p.id, {5, 0, 0});
        sim.scripts().runScript(
            "function w1() coroutine.yield() end\n"
            "Create_Thread('w1')\n");
    };
    eaw::Simulation a(4), b(4);
    setup(a); setup(b);
    // Order both sims' objects to move to the same targets via script.
    for (auto* s : {&a, &b}) {
        s->scripts().runScript(
            "o = Find_First_Object('X_WING')\n"
            "o:Move_To(Find_First_Object('ISD') or o)\n"
            "t = Find_All_Objects_Of_Type('X_WING')\n"
            "if #t > 1 then t[2]:Move_To(t[1]) end\n");
    }
    for (int i = 0; i < 30; ++i) { a.tick(1.0 / 30.0); b.tick(1.0 / 30.0); }
    auto objsA = a.sim().objectsOfType("X_WING");
    auto objsB = b.sim().objectsOfType("X_WING");
    bool same = objsA.size() == objsB.size();
    if (same) {
        for (size_t i = 0; i < objsA.size() && same; ++i) {
            same = objsA[i]->position.distanceTo(objsB[i]->position) < 1e-9 &&
                   objsA[i]->hasMoveTarget == objsB[i]->hasMoveTarget;
        }
    }
    check(same, "parallel update is deterministic across runs");
}

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    testTicksAdvanceTime();
    testScriptDefinesAndSpawns();
    testScriptThreadRunsAcrossTicks();
    testEndToEndPump();
    testParallelMoveToIntegration();
    testParallelDeterminism();
    if (failures == 0) { std::printf("ALL TESTS PASSED\n"); return 0; }
    std::printf("%d FAILURES\n", failures);
    return 1;
}
