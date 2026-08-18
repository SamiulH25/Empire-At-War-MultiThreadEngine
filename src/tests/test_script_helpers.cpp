// Tests for the Tier 2 runtime script helpers: FindTarget, FindDeadlyEnemy,
// BlockOnCommand, DebugMessage, TestValid, Project_By_Unit_Range,
// EvaluatePerception, PlayerSpecificName, ScriptExit, and GameRandom.Get_Float.
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

// A pair of opposing players with a fighter type (the engine's weapon range
// comes from the type's maxRange).
void setupBattle(eaw::Simulation& sim) {
    eaw::Player& a = sim.sim().addPlayer("Rebel Alliance", "REBEL");
    a.human = true;
    eaw::Player& b = sim.sim().addPlayer("Galactic Empire", "EMPIRE");
    (void)b;
    eaw::ObjectType fighter;
    fighter.name = "FIGHTER";
    fighter.maxRange = 300.0;
    fighter.hullPoints = 50.0;
    fighter.categories = {"Fighter"};
    fighter.properties = {"Unit"};
    sim.sim().addType(std::move(fighter));
    eaw::ObjectType hero;
    hero.name = "HERO";  // the self unit, uniquely typed for deterministic lookup
    hero.maxRange = 300.0;
    hero.hullPoints = 200.0;
    hero.categories = {"Fighter"};
    hero.properties = {"Unit", "Hero"};
    sim.sim().addType(std::move(hero));
}

void testFindTarget() {
    eaw::Simulation sim;
    setupBattle(sim);
    eaw::SimState& st = sim.sim();
    st.spawnUnit("HERO", 1, {0, 0, 0});
    st.spawnUnit("FIGHTER", 2, {100, 0, 0});   // in range
    st.spawnUnit("FIGHTER", 2, {1000, 0, 0});  // out of range
    sim.scripts().runScript(
        "self = Find_First_Object('HERO')\n"
        "t = FindTarget(self)\n"
        "Found = t ~= nil\n"
        "Dist = self:Get_Distance(t)\n");
    lua_State* L = sim.scripts().state();
    lua_getglobal(L, "Found");
    check(lua_toboolean(L, -1) == 1, "FindTarget finds an in-range enemy");
    lua_pop(L, 1);
    lua_getglobal(L, "Dist");
    double d = lua_tonumber(L, -1);
    lua_pop(L, 1);
    check(d > 90 && d < 110, "FindTarget returns the near enemy (100 units)");
}

void testFindTargetNone() {
    eaw::Simulation sim;
    setupBattle(sim);
    eaw::SimState& st = sim.sim();
    st.spawnUnit("HERO", 1, {0, 0, 0});
    st.spawnUnit("FIGHTER", 2, {1000, 0, 0});  // out of weapon range
    sim.scripts().runScript(
        "self = Find_First_Object('HERO')\n"
        "t = FindTarget(self, 200)\n"
        "Found = t ~= nil\n");
    lua_State* L = sim.scripts().state();
    lua_getglobal(L, "Found");
    check(lua_toboolean(L, -1) == 0, "FindTarget returns nil when out of range");
    lua_pop(L, 1);
}

void testFindDeadlyEnemy() {
    eaw::Simulation sim;
    setupBattle(sim);
    eaw::SimState& st = sim.sim();
    st.spawnUnit("HERO", 1, {0, 0, 0});
    st.spawnUnit("FIGHTER", 2, {250, 0, 0});   // within weapon range
    st.spawnUnit("FIGHTER", 2, {500, 0, 0});   // beyond weapon range
    sim.scripts().runScript(
        "self = Find_First_Object('HERO')\n"
        "d = FindDeadlyEnemy(self)\n"
        "Found = d ~= nil\n");
    lua_State* L = sim.scripts().state();
    lua_getglobal(L, "Found");
    check(lua_toboolean(L, -1) == 1, "FindDeadlyEnemy finds a target in weapon range");
    lua_pop(L, 1);
}

void testBlockOnCommand() {
    eaw::Simulation sim;
    setupBattle(sim);
    eaw::SimState& st = sim.sim();
    int forceId = st.addTaskForce(1, "AttackPlan");
    st.spawnUnit("HERO", 1, {0, 0, 0});
    // Add the unit to the force.
    int unitId = -1;
    for (const eaw::GameObject* o : st.allObjects()) {
        if (o->typeName == "HERO") { unitId = o->id; break; }
    }
    st.addUnitToForce(forceId, unitId);
    sim.scripts().runScript(
        "self = Find_First_Object('HERO')\n"
        "tf = self:Get_Force()\n"
        "Blocked = tf ~= nil and BlockOnCommand(tf, 'AttackPlan')\n"
        "NotBlocked = tf ~= nil and BlockOnCommand(tf, 'OtherPlan')\n");
    lua_State* L = sim.scripts().state();
    lua_getglobal(L, "Blocked");
    check(lua_toboolean(L, -1) == 1, "BlockOnCommand true for matching command");
    lua_pop(L, 1);
    lua_getglobal(L, "NotBlocked");
    check(lua_toboolean(L, -1) == 0, "BlockOnCommand false for non-matching command");
    lua_pop(L, 1);
}

void testTestValid() {
    eaw::Simulation sim;
    setupBattle(sim);
    eaw::SimState& st = sim.sim();
    st.spawnUnit("HERO", 1, {0, 0, 0});
    sim.scripts().runScript(
        "o = Find_First_Object('HERO')\n"
        "Valid = TestValid(o)\n"
        "NilValid = TestValid(nil)\n");
    lua_State* L = sim.scripts().state();
    lua_getglobal(L, "Valid");
    check(lua_toboolean(L, -1) == 1, "TestValid true for a live object");
    lua_pop(L, 1);
    lua_getglobal(L, "NilValid");
    check(lua_toboolean(L, -1) == 0, "TestValid false for nil");
    lua_pop(L, 1);
}

void testProjectByUnitRange() {
    eaw::Simulation sim;
    setupBattle(sim);
    eaw::SimState& st = sim.sim();
    st.spawnUnit("HERO", 1, {0, 0, 0});
    st.spawnUnit("FIGHTER", 2, {500, 0, 0});  // beyond the 300 weapon range
    sim.scripts().runScript(
        "self = Find_First_Object('HERO')\n"
        "far = Find_All_Objects_Of_Type('FIGHTER')[1]\n"
        "P = Project_By_Unit_Range(self, far)\n");
    lua_State* L = sim.scripts().state();
    lua_getglobal(L, "P");
    double p = lua_tonumber(L, -1);
    lua_pop(L, 1);
    check(p > 295 && p <= 300, "Project_By_Unit_Range clamps to weapon range");
}

void testEvaluatePerception() {
    eaw::Simulation sim;
    setupBattle(sim);
    sim.loadPerceptions(
        "<Equations>\n"
        "  <Attack_Target>\n"
        "    Variable_Target.Health\n"
        "  </Attack_Target>\n"
        "</Equations>\n");
    eaw::SimState& st = sim.sim();
    st.spawnUnit("HERO", 1, {0, 0, 0});
    st.spawnUnit("FIGHTER", 2, {100, 0, 0});
    sim.scripts().runScript(
        "self = Find_First_Object('HERO')\n"
        "target = Find_All_Objects_Of_Type('FIGHTER')[1]\n"
        "S = EvaluatePerception(self, target, 'Attack_Target')\n");
    lua_State* L = sim.scripts().state();
    lua_getglobal(L, "S");
    double s = lua_tonumber(L, -1);
    lua_pop(L, 1);
    // Health is normalized 0..1; the target just spawned at full hull.
    check(s > 0.9 && s <= 1.0, "EvaluatePerception evaluates a real equation");
}

void testMiscHelpers() {
    eaw::Simulation sim;
    setupBattle(sim);
    sim.scripts().runScript(
        "DebugMessage('test')\n"
        "N = PlayerSpecificName('Rebel')\n"
        "ScriptExit()\n"
        "Done = true\n");
    lua_State* L = sim.scripts().state();
    lua_getglobal(L, "N");
    const char* n = lua_tostring(L, -1);
    lua_pop(L, 1);
    check(n && std::string(n) == "Rebel", "PlayerSpecificName passthrough");
    lua_getglobal(L, "Done");
    check(lua_toboolean(L, -1) == 1, "ScriptExit returns control (no error)");
    lua_pop(L, 1);
}

void testGameRandomTable() {
    eaw::Simulation sim;
    setupBattle(sim);
    sim.scripts().runScript(
        "x = GameRandom(1, 1)\n"
        "f = GameRandom.Get_Float()\n"
        "i = GameRandom.Get_Integer(5, 5)\n");
    lua_State* L = sim.scripts().state();
    lua_getglobal(L, "x");
    check(lua_tointeger(L, -1) == 1, "GameRandom(min,max) callable table");
    lua_pop(L, 1);
    lua_getglobal(L, "f");
    double f = lua_tonumber(L, -1);
    lua_pop(L, 1);
    check(f >= 0.0 && f <= 1.0, "GameRandom.Get_Float() in [0,1]");
    lua_getglobal(L, "i");
    check(lua_tointeger(L, -1) == 5, "GameRandom.Get_Integer(5,5) == 5");
    lua_pop(L, 1);
}

// ---- Tier 4 fidelity-gap bindings -----------------------------------------

void testHeroUnique() {
    eaw::Simulation sim;
    setupBattle(sim);
    eaw::SimState& st = sim.sim();
    st.spawnUnit("HERO", 1, {0, 0, 0});
    sim.scripts().runScript(
        "o = Find_First_Object('HERO')\n"
        "Before = o:Is_Unique()\n"
        "o:Set_Hero(true)\n"
        "After = o:Is_Unique()\n"
        "Id = o:Get_Unique_ID()\n"
        "o:Set_Hero(false)\n"
        "Cleared = o:Is_Unique()\n");
    lua_State* L = sim.scripts().state();
    lua_getglobal(L, "Before");
    check(lua_toboolean(L, -1) == 0, "Is_Unique false before Set_Hero");
    lua_pop(L, 1);
    lua_getglobal(L, "After");
    check(lua_toboolean(L, -1) == 1, "Is_Unique true after Set_Hero");
    lua_pop(L, 1);
    lua_getglobal(L, "Id");
    check(lua_tointeger(L, -1) > 0, "Get_Unique_ID returns a positive id");
    lua_pop(L, 1);
    lua_getglobal(L, "Cleared");
    check(lua_toboolean(L, -1) == 0, "Is_Unique false after Set_Hero(false)");
    lua_pop(L, 1);
}

void testFreeStore() {
    eaw::Simulation sim;
    setupBattle(sim);
    eaw::SimState& st = sim.sim();
    int forceId = st.addTaskForce(1, "AttackPlan");
    st.spawnUnit("HERO", 1, {0, 0, 0});
    int unitId = -1;
    for (const eaw::GameObject* o : st.allObjects()) {
        if (o->typeName == "HERO") { unitId = o->id; break; }
    }
    st.addUnitToForce(forceId, unitId);
    // Mark the unit as on the free store (the sim-level flag the binding reads).
    eaw::GameObject* o = st.object(unitId);
    o->inFreeStore = true;
    o->freeStoreForceId = forceId;
    sim.scripts().runScript(
        "self = Find_First_Object('HERO')\n"
        "tf = self:Get_Force()\n"
        "list = tf:Get_Free_Store()\n"
        "N = #list\n"
        "N2 = #(tf:Get_Units_In_Free_Store())\n");
    lua_State* L = sim.scripts().state();
    lua_getglobal(L, "N");
    check(lua_tointeger(L, -1) == 1, "Get_Free_Store returns the parked unit");
    lua_pop(L, 1);
    lua_getglobal(L, "N2");
    check(lua_tointeger(L, -1) == 1, "Get_Units_In_Free_Store returns the unit");
    lua_pop(L, 1);
}

void testTargetingPrioritiesGetter() {
    eaw::Simulation sim;
    setupBattle(sim);
    eaw::SimState& st = sim.sim();
    st.spawnUnit("HERO", 1, {0, 0, 0});
    sim.scripts().runScript(
        "o = Find_First_Object('HERO')\n"
        "o:Set_Targeting_Priorities({'Capital', 'Fighter'})\n"
        "p = o:Get_Targeting_Priorities()\n"
        "First = p[1]\n"
        "Second = p[2]\n");
    lua_State* L = sim.scripts().state();
    lua_getglobal(L, "First");
    const char* f = lua_tostring(L, -1);
    lua_pop(L, 1);
    check(f && std::string(f) == "Capital", "Get_Targeting_Priorities returns table");
    lua_getglobal(L, "Second");
    const char* s = lua_tostring(L, -1);
    lua_pop(L, 1);
    check(s && std::string(s) == "Fighter", "priority table order preserved");
}

void testPlanetAndForceSetters() {
    eaw::Simulation sim;
    setupBattle(sim);
    eaw::SimState& st = sim.sim();
    st.addPlanet("Tatooine", "REBEL", {0, 0, 0});
    st.addPlanet("Hoth", "", {100, 0, 0});
    int forceId = st.addTaskForce(1, "AttackPlan");
    st.spawnUnit("HERO", 1, {0, 0, 0});
    int unitId = -1;
    for (const eaw::GameObject* o : st.allObjects()) {
        if (o->typeName == "HERO") { unitId = o->id; break; }
    }
    st.addUnitToForce(forceId, unitId);
    sim.scripts().runScript(
        "p = FindPlanet('Tatooine')\n"
        "p2 = FindPlanet('Hoth')\n"
        "self = Find_First_Object('HERO')\n"
        "tf = self:Get_Force()\n"
        "tf:Set_Force_Planet(p)\n"
        "PlanetName = tf:Get_Current_Planet():Get_Name()\n"
        "p2:Set_Planet_Faction('EMPIRE')\n"
        "OwnerFaction = p2:Get_Planet_Faction()\n"
        "pl = Find_Player('REBEL')\n"
        "pl:Set_Faction('NEWREBEL')\n"
        "Faction = pl:Get_Faction_Name()\n"
        "NF = Get_Number_Of_Forces()\n"
        "PC = Get_Player_Count()\n");
    lua_State* L = sim.scripts().state();
    lua_getglobal(L, "PlanetName");
    const char* pn = lua_tostring(L, -1);
    lua_pop(L, 1);
    check(pn && std::string(pn) == "Tatooine", "Set_Force_Planet + Get_Current_Planet");
    lua_getglobal(L, "OwnerFaction");
    const char* of = lua_tostring(L, -1);
    lua_pop(L, 1);
    check(of && std::string(of) == "EMPIRE", "Set_Planet_Faction + Get_Planet_Faction");
    lua_getglobal(L, "Faction");
    const char* fac = lua_tostring(L, -1);
    lua_pop(L, 1);
    check(fac && std::string(fac) == "NEWREBEL", "player Set_Faction");
    lua_getglobal(L, "NF");
    check(lua_tointeger(L, -1) == 1, "Get_Number_Of_Forces");
    lua_pop(L, 1);
    lua_getglobal(L, "PC");
    check(lua_tointeger(L, -1) == 2, "Get_Player_Count");
    lua_pop(L, 1);
}

void testPathHelpers() {
    eaw::Simulation sim;
    setupBattle(sim);
    // The sim's path grid is empty by default (nothing blocked).
    sim.scripts().runScript(
        "b = Is_Path_Blocked(Create_Position(0,0,0), Create_Position(10,0,0))\n"
        "r = Find_Path(Create_Position(0,0,0), Create_Position(10,0,0))\n"
        "Res = r:Result()\n");
    lua_State* L = sim.scripts().state();
    lua_getglobal(L, "b");
    check(lua_toboolean(L, -1) == 0, "Is_Path_Blocked false on an empty grid");
    lua_pop(L, 1);
    lua_getglobal(L, "Res");
    double res = lua_tonumber(L, -1);
    lua_pop(L, 1);
    check(res > 0.0, "Find_Path returns a positive cost on a clear line");
}

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    testFindTarget();
    testFindTargetNone();
    testFindDeadlyEnemy();
    testBlockOnCommand();
    testTestValid();
    testProjectByUnitRange();
    testEvaluatePerception();
    testMiscHelpers();
    testGameRandomTable();
    testHeroUnique();
    testFreeStore();
    testTargetingPrioritiesGetter();
    testPlanetAndForceSetters();
    testPathHelpers();
    if (failures == 0) { std::printf("ALL TESTS PASSED\n"); return 0; }
    std::printf("%d FAILURES\n", failures);
    return 1;
}
