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

void testUnitNavigatesAroundObstacle() {
    // A unit ordered to move across a wall must path around it (the sim's
    // pathfinding integration), not beeline through.
    eaw::Simulation sim(4);
    eaw::Player& rebel = sim.sim().addPlayer("Rebel Alliance", "REBEL");
    eaw::ObjectType xwing;
    xwing.name = "X_WING";
    xwing.properties = {"Unit"};
    sim.sim().addType(std::move(xwing));
    int unitId = sim.sim().spawnUnit("X_WING", rebel.id, {5, 10, 0});

    // Wall along world x=60 from y=0 to y=40, all altitude bands.
    eaw::PathGrid& g = sim.pathGrid();
    int wallX = g.cellOf(60.0);
    for (int y = 0; y <= 20; ++y)
        for (int z = 0; z < g.depth(); ++z) g.setBlocked(wallX, y, z);

    // Order the unit to move across the wall.
    eaw::GameObject* unit = sim.sim().object(unitId);
    unit->hasMoveTarget = true;
    unit->moveTarget = {100, 10, 0};

    bool detoured = false;
    for (int i = 0; i < 600; ++i) {
        sim.tick(1.0 / 30.0);
        unit = sim.sim().object(unitId);
        if (!unit) break;
        // Crossed the wall line? Then it must have left the blocked y band.
        if (unit->position.x > 60.0 && (unit->position.y < 0.0 || unit->position.y > 40.0)) {
            detoured = true;
        }
    }
    check(detoured, "unit detoured around the wall");
    unit = sim.sim().object(unitId);
    check(unit && !unit->hasMoveTarget, "unit reached its destination");
    check(unit && unit->position.x > 99.0, "unit crossed to the far side");
}

void testConfigureGameConstants() {
    // GameConstants.xml pathfinding knobs must reach the pathfinding system.
    const char* xml =
        "<GameConstants>\n"
        "  <SpacePathfindMaxExpansions>1234</SpacePathfindMaxExpansions>\n"
        "  <SpacePathFailureMaxExpansionsCoefficient>10.0</SpacePathFailureMaxExpansionsCoefficient>\n"
        "  <FramesPerCollisionCheck>4</FramesPerCollisionCheck>\n"
        "</GameConstants>\n";
    eaw::GameConstants gc = eaw::GameConstants::Parse(xml);
    check(gc.spacePathfindMaxExpansions == 1234, "parsed expansion budget");
    check(gc.framesPerCollisionCheck == 4, "parsed collision frames");

    eaw::Simulation sim;
    sim.configure(gc);
    check(sim.pathOptions().expansionsPerTick == 1234,
          "expansion budget applied to pathfinding");
    check(sim.pathOptions().maxTotalExpansions == 12340,
          "failure cap scaled by coefficient");
}

void testConfigureDefaultsUnchanged() {
    // Empty constants must not clobber the defaults.
    const char* xml = "<GameConstants></GameConstants>\n";
    eaw::GameConstants gc = eaw::GameConstants::Parse(xml);
    eaw::Simulation sim;
    sim.configure(gc);
    check(sim.pathOptions().expansionsPerTick == 400,
          "default budget retained when unset");
}

// Serializes the sim's full object state (positions, hull, shield, cooldown,
// orders, alive flags, in id order) into a byte string. Two runs are
// byte-identical iff the whole battle outcome is identical.
std::string serializeState(const eaw::SimState& state) {
    std::string out;
    auto put = [&](const void* p, size_t n) {
        out.append(static_cast<const char*>(p), n);
    };
    for (const eaw::GameObject* o : state.allObjects()) {
        put(&o->id, sizeof(o->id));
        put(&o->position, sizeof(o->position));
        put(&o->hull, sizeof(o->hull));
        put(&o->shield, sizeof(o->shield));
        put(&o->energy, sizeof(o->energy));
        put(&o->alive, sizeof(o->alive));
        put(&o->hidden, sizeof(o->hidden));
        put(&o->attackTargetId, sizeof(o->attackTargetId));
        put(&o->hasMoveTarget, sizeof(o->hasMoveTarget));
        put(&o->moveTarget, sizeof(o->moveTarget));
        put(&o->attackCooldown, sizeof(o->attackCooldown));
    }
    return out;
}

// Runs a scripted two-side battle for `ticks` and returns the serialized
// state. Same setup every call (deterministic seed, fixed unit types).
std::string runDeterministicBattle(unsigned workers, int ticks) {
    eaw::Simulation sim(workers);
    eaw::SimState& state = sim.sim();
    eaw::Player& rebel = state.addPlayer("Rebel Alliance", "REBEL");
    eaw::Player& empire = state.addPlayer("Galactic Empire", "EMPIRE");
    rebel.human = true;

    eaw::ObjectType rf;
    rf.name = "REBEL_FIGHTER";
    rf.properties = {"Unit"};
    rf.categories = {"Fighter"};
    rf.damage = 0.010; rf.attackRate = 4.0; rf.maxRange = 300.0;
    rf.moveSpeed = 50.0;
    rf.affiliatedFactions = {"REBEL"};
    state.addType(std::move(rf));
    eaw::ObjectType rc;
    rc.name = "REBEL_CAPITAL";
    rc.properties = {"Unit"};
    rc.categories = {"Capital"};
    rc.damage = 0.050; rc.attackRate = 0.8; rc.maxRange = 500.0;
    rc.moveSpeed = 30.0;
    rc.affiliatedFactions = {"REBEL"};
    state.addType(std::move(rc));
    eaw::ObjectType ef;
    ef.name = "EMPIRE_FIGHTER";
    ef.properties = {"Unit"};
    ef.categories = {"Fighter"};
    ef.damage = 0.011; ef.attackRate = 4.0; ef.maxRange = 300.0;
    ef.moveSpeed = 50.0;
    ef.affiliatedFactions = {"EMPIRE"};
    state.addType(std::move(ef));
    eaw::ObjectType ec;
    ec.name = "EMPIRE_CAPITAL";
    ec.properties = {"Unit"};
    ec.categories = {"Capital"};
    ec.damage = 0.055; ec.attackRate = 0.8; ec.maxRange = 500.0;
    ec.moveSpeed = 30.0;
    ec.affiliatedFactions = {"EMPIRE"};
    state.addType(std::move(ec));

    // Fixed formation: 8 fighters + 2 capitals per side, 600 units apart.
    for (int i = 0; i < 8; ++i) {
        state.spawnUnit("REBEL_FIGHTER", rebel.id, {(i % 8) * 12.0, (i / 8) * 12.0, (i % 3) * 15.0});
        state.spawnUnit("EMPIRE_FIGHTER", empire.id, {600.0 + (i % 8) * 12.0, (i / 8) * 12.0, (i % 3) * 15.0});
    }
    for (int i = 0; i < 2; ++i) {
        state.spawnUnit("REBEL_CAPITAL", rebel.id, {(i % 2) * 36.0, (i / 2) * 36.0, 10.0});
        state.spawnUnit("EMPIRE_CAPITAL", empire.id, {600.0 + (i % 2) * 36.0, (i / 2) * 36.0, 10.0});
    }

    sim.scripts().runScript(
        "for i, o in ipairs(Find_All_Objects_Of_Type('REBEL_FIGHTER')) do\n"
        "  o:Attack_Target(Find_First_Object('EMPIRE_FIGHTER'))\n"
        "  o:Move_To(Find_First_Object('EMPIRE_CAPITAL'))\n"
        "end\n"
        "for i, o in ipairs(Find_All_Objects_Of_Type('REBEL_CAPITAL')) do\n"
        "  o:Attack_Target(Find_First_Object('EMPIRE_CAPITAL'))\n"
        "end\n"
        "for i, o in ipairs(Find_All_Objects_Of_Type('EMPIRE_FIGHTER')) do\n"
        "  o:Attack_Target(Find_First_Object('REBEL_FIGHTER'))\n"
        "  o:Move_To(Find_First_Object('REBEL_CAPITAL'))\n"
        "end\n"
        "for i, o in ipairs(Find_All_Objects_Of_Type('EMPIRE_CAPITAL')) do\n"
        "  o:Attack_Target(Find_First_Object('REBEL_CAPITAL'))\n"
        "end\n");
    // Random numbers used by scripts must be seeded identically per run for
    // the byte-identical check (they are: the engine's GameRandom uses a
    // static mt19937 seeded once — fine, no script calls it here).
    for (int i = 0; i < ticks; ++i) sim.tick(1.0 / 30.0);
    return serializeState(state);
}

void testRepeatRunByteIdentical() {
    // The determinism guard-rail: the same battle run 3 times (1 worker,
    // 2 workers, 4 workers) must produce byte-identical state. This catches
    // nondeterminism from new subsystems (races, unseeded randoms, unordered
    // iteration) before it reaches the --compare demo.
    const int ticks = 360;
    std::string a = runDeterministicBattle(1, ticks);
    std::string b = runDeterministicBattle(2, ticks);
    std::string c = runDeterministicBattle(4, ticks);
    check(a.size() > 0, "serialized battle state is non-empty");
    check(a == b, "1-worker vs 2-worker battle is byte-identical");
    check(a == c, "1-worker vs 4-worker battle is byte-identical");
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
    testUnitNavigatesAroundObstacle();
    testConfigureGameConstants();
    testConfigureDefaultsUnchanged();
    testRepeatRunByteIdentical();
    if (failures == 0) { std::printf("ALL TESTS PASSED\n"); return 0; }
    std::printf("%d FAILURES\n", failures);
    return 1;
}
