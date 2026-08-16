// Tests for the taskforce system (unit groups + collective orders).
#include "core/lua_wrappers.h"
#include "core/simulation.h"

#include <cstdio>
#include <memory>
#include <vector>

namespace {

int failures = 0;
void check(bool c, const char* w) {
    std::printf("%s: %s\n", c ? "ok" : "FAIL", w);
    if (!c) ++failures;
}

struct TfFixture {
    std::unique_ptr<eaw::Simulation> sim;
    int rebelId = 0;
    int empireId = 0;
    int forceId = 0;

    TfFixture() {
        sim = std::make_unique<eaw::Simulation>(4);
        eaw::Player& rebel = sim->sim().addPlayer("Rebel Alliance", "REBEL");
        eaw::Player& empire = sim->sim().addPlayer("Galactic Empire", "EMPIRE");
        rebelId = rebel.id;
        empireId = empire.id;
        eaw::ObjectType xwing;
        xwing.name = "X_WING";
        xwing.properties = {"Unit"};
        xwing.damage = 0.01;
        xwing.attackRate = 2.0;
        xwing.maxRange = 300;
        sim->sim().addType(std::move(xwing));
        eaw::ObjectType isd;
        isd.name = "ISD";
        isd.properties = {"Unit"};
        isd.damage = 0.0;
        isd.maxRange = 500;
        sim->sim().addType(std::move(isd));
        // Two rebel fighters + one empire capital.
        sim->sim().spawnUnit("X_WING", rebelId, {0, 0, 0});
        sim->sim().spawnUnit("X_WING", rebelId, {20, 0, 0});
        sim->sim().spawnUnit("ISD", empireId, {400, 0, 0});
        forceId = sim->sim().addTaskForce(rebelId, "AttackPlan");
    }

    void addFightersToForce() {
        sim->scripts().runScript(
            "list = Find_All_Objects_Of_Type('X_WING')\n");
        for (const eaw::GameObject* o : sim->sim().objectsOfType("X_WING")) {
            sim->sim().addUnitToForce(forceId, o->id);
        }
    }
};

void testCreateAndQuery() {
    TfFixture fx;
    check(fx.sim->sim().taskForce(fx.forceId) != nullptr, "taskforce created");
    fx.addFightersToForce();
    fx.sim->scripts().runScript(
        "force = Find_First_Object('X_WING') and nil\n"); // placeholder
    // The force wrapper is created engine-side; scripts get it via a hook.
    // Test via C++: force holds 2 units, threat = sum of hulls.
    const eaw::TaskForce* f = fx.sim->sim().taskForce(fx.forceId);
    check(f->units.size() == 2, "force holds both fighters");
    check(fx.sim->sim().forceThreat(fx.forceId) == 2.0, "threat sums unit hulls");
    check(f->name == "AttackPlan", "goal type name stored");
}

void testWrapperMethods() {
    TfFixture fx;
    fx.addFightersToForce();
    // Give the script a force wrapper via a global (engine hook simulation).
    fx.sim->scripts().runScript("force = nil\n");
    // Push a force wrapper from C++ into the global.
    eaw::SimState& state = fx.sim->sim();
    {
        lua_State* s = fx.sim->scripts().state();
        eaw::pushWrapper(s, &state, eaw::WrapperKind::TaskForce, fx.forceId);
        lua_setglobal(s, "force");
    }
    fx.sim->scripts().runScript(
        "count = force:Get_Force_Count()\n"
        "units = force:Get_Unit_Table()\n"
        "nunits = #units\n"
        "name = force:Get_Goal_Type_Name()\n"
        "threat = force:Get_Self_Threat_Sum()\n"
        "stage = force:Get_Stage()\n"
        "all = force:Are_All_Units_On_Free_Store()\n");
    lua_getglobal(fx.sim->scripts().state(), "count");
    check(lua_tointeger(fx.sim->scripts().state(), -1) == 2, "Get_Force_Count");
    lua_pop(fx.sim->scripts().state(), 1);
    lua_getglobal(fx.sim->scripts().state(), "nunits");
    check(lua_tointeger(fx.sim->scripts().state(), -1) == 2, "Get_Unit_Table size");
    lua_pop(fx.sim->scripts().state(), 1);
    lua_getglobal(fx.sim->scripts().state(), "name");
    check(std::string(lua_tostring(fx.sim->scripts().state(), -1)) == "AttackPlan",
          "Get_Goal_Type_Name");
    lua_pop(fx.sim->scripts().state(), 1);
    lua_getglobal(fx.sim->scripts().state(), "threat");
    check(lua_tonumber(fx.sim->scripts().state(), -1) == 2.0, "Get_Self_Threat_Sum");
    lua_pop(fx.sim->scripts().state(), 1);
    lua_getglobal(fx.sim->scripts().state(), "stage");
    check(lua_tointeger(fx.sim->scripts().state(), -1) == 0, "Get_Stage default");
    lua_pop(fx.sim->scripts().state(), 1);
    lua_getglobal(fx.sim->scripts().state(), "all");
    check(lua_toboolean(fx.sim->scripts().state(), -1) == 1, "all units on force");
    lua_pop(fx.sim->scripts().state(), 1);
}

void testMutators() {
    TfFixture fx;
    fx.addFightersToForce();
    eaw::SimState& state = fx.sim->sim();
    {
        lua_State* s = fx.sim->scripts().state();
        eaw::pushWrapper(s, &state, eaw::WrapperKind::TaskForce, fx.forceId);
        lua_setglobal(s, "force");
    }
    fx.sim->scripts().runScript(
        "force:Set_Stage(3)\n"
        "force:Set_Plan_Result(true)\n"
        "force:Set_As_Goal_System_Removable(false)\n"
        "stage = force:Get_Stage()\n");
    lua_getglobal(fx.sim->scripts().state(), "stage");
    check(lua_tointeger(fx.sim->scripts().state(), -1) == 3, "Set_Stage works");
    lua_pop(fx.sim->scripts().state(), 1);
    const eaw::TaskForce* f = state.taskForce(fx.forceId);
    check(f->planResult == true, "Set_Plan_Result works");
    check(f->goalSystemRemovable == false, "Set_As_Goal_System_Removable works");
}

void testCollectiveOrders() {
    TfFixture fx;
    fx.addFightersToForce();
    eaw::SimState& state = fx.sim->sim();
    {
        lua_State* s = fx.sim->scripts().state();
        eaw::pushWrapper(s, &state, eaw::WrapperKind::TaskForce, fx.forceId);
        lua_setglobal(s, "force");
    }
    // Order the whole force to attack + move to the ISD.
    fx.sim->scripts().runScript(
        "target = Find_First_Object('ISD')\n"
        "cmd = force:Attack_Target(target)\n"
        "fin = cmd:IsFinished()\n"
        "force:Move_To(target)\n");
    lua_getglobal(fx.sim->scripts().state(), "fin");
    check(lua_toboolean(fx.sim->scripts().state(), -1) == 1, "force attack returns command");
    lua_pop(fx.sim->scripts().state(), 1);
    // Both fighters must have the attack target and move target set.
    int withTarget = 0, withMove = 0;
    for (const eaw::GameObject* o : state.objectsOfType("X_WING")) {
        if (o->attackTargetId != 0) ++withTarget;
        if (o->hasMoveTarget) ++withMove;
    }
    check(withTarget == 2, "attack order fanned out to both units");
    check(withMove == 2, "move order fanned out to both units");
    // Run the sim: fighters should close on the ISD.
    for (int i = 0; i < 120; ++i) fx.sim->tick(1.0 / 30.0);
    const eaw::GameObject* isd = state.objectsOfType("ISD")[0];
    const eaw::GameObject* f0 = state.objectsOfType("X_WING")[0];
    check(f0->position.distanceTo(isd->position) < 400.0,
          "force moved toward the target");
}

void testReleaseAndPrune() {
    TfFixture fx;
    fx.addFightersToForce();
    eaw::SimState& state = fx.sim->sim();
    {
        lua_State* s = fx.sim->scripts().state();
        eaw::pushWrapper(s, &state, eaw::WrapperKind::TaskForce, fx.forceId);
        lua_setglobal(s, "force");
    }
    // Kill one fighter via script, release the other.
    fx.sim->scripts().runScript(
        "x = Find_First_Object('X_WING')\n"
        "force:Release_Unit(x)\n");
    const eaw::TaskForce* f = state.taskForce(fx.forceId);
    check(f->units.size() == 1, "Release_Unit removes the unit");
    // Kill the remaining one: prune on the next tick empties the force.
    int remaining = f->units[0];
    state.object(remaining)->alive = false;
    fx.sim->tick(1.0 / 30.0);
    const eaw::TaskForce* f2 = state.taskForce(fx.forceId);
    check(f2->units.empty(), "dead units pruned from force");
    check(fx.sim->sim().forceThreat(fx.forceId) == 0.0, "empty force has no threat");
}

void testGarrisonOrder() {
    TfFixture fx;
    fx.addFightersToForce();
    eaw::SimState& state = fx.sim->sim();
    {
        lua_State* s = fx.sim->scripts().state();
        eaw::pushWrapper(s, &state, eaw::WrapperKind::TaskForce, fx.forceId);
        lua_setglobal(s, "force");
    }
    // ISD is a valid garrison target; order the force to garrison.
    fx.sim->scripts().runScript(
        "target = Find_First_Object('ISD')\n"
        "ok = force:Garrison(target)\n"
        "g = target:Get_Garrisoned_Units()\n"
        "n = #g\n");
    lua_getglobal(fx.sim->scripts().state(), "ok");
    check(lua_toboolean(fx.sim->scripts().state(), -1) == 1, "force garrison succeeds");
    lua_pop(fx.sim->scripts().state(), 1);
    lua_getglobal(fx.sim->scripts().state(), "n");
    check(lua_tointeger(fx.sim->scripts().state(), -1) == 2, "both units garrisoned");
    lua_pop(fx.sim->scripts().state(), 1);
    fx.sim->scripts().runScript("force:Leave_Garrison()\n");
    const eaw::GameObject* isd = state.objectsOfType("ISD")[0];
    check(isd->garrisonedUnits.empty(), "Leave_Garrison empties the garrison");
}

void testGalacticMode() {
    TfFixture fx;
    fx.addFightersToForce();
    eaw::SimState& state = fx.sim->sim();
    // Two planets 500 units apart -> ~500s travel at speed 1.
    int tatooine = state.addPlanet("Tatooine", "EMPIRE", {0, 0, 0});
    int endor = state.addPlanet("Endor", "REBEL", {500, 0, 0});
    check(state.planet(tatooine) != nullptr && state.planet(endor) != nullptr,
          "planets created");
    check(state.findPlanet("Tatooine") != nullptr, "findPlanet by name");
    check(state.findPlanet("Nope") == nullptr, "findPlanet unknown returns null");
    check(state.forcePlanet(fx.forceId) == -1, "force starts at no planet");
    // Assign the force to its origin planet (as force assembly would).
    state.taskForce(fx.forceId)->planetId = tatooine;
    check(state.forcePlanet(fx.forceId) == tatooine, "force assigned to origin");

    {
        lua_State* s = fx.sim->scripts().state();
        eaw::pushWrapper(s, &state, eaw::WrapperKind::TaskForce, fx.forceId);
        lua_setglobal(s, "force");
    }
    fx.sim->scripts().runScript(
        "p = FindPlanet('Endor')\n"
        "name = p:Get_Name()\n"
        "owner = p:Get_Owner()\n"
        "owner_name = owner:Get_Faction_Name()\n"
        "force:Move_To(p)\n");
    lua_getglobal(fx.sim->scripts().state(), "name");
    check(std::string(lua_tostring(fx.sim->scripts().state(), -1)) == "Endor",
          "FindPlanet + Get_Name");
    lua_pop(fx.sim->scripts().state(), 1);
    lua_getglobal(fx.sim->scripts().state(), "owner_name");
    check(std::string(lua_tostring(fx.sim->scripts().state(), -1)) == "REBEL",
          "planet owner faction");
    lua_pop(fx.sim->scripts().state(), 1);
    // Force is now IN TRANSIT, not arrived.
    check(state.forceInTransit(fx.forceId), "force is in hyperspace");
    check(state.forceTransitTarget(fx.forceId) == endor, "transit destination");
    check(state.forcePlanet(fx.forceId) == tatooine, "force still at origin");
    // Units hidden during transit.
    for (const eaw::GameObject* o : state.objectsOfType("X_WING")) {
        check(o->hidden, "units hidden in hyperspace");
    }
    // Progress grows over time.
    fx.sim->tick(100.0);
    double p1 = state.forceTransitProgress(fx.forceId);
    check(p1 > 0.0 && p1 < 1.0, "transit in progress");
    // Tick past the arrival.
    fx.sim->tick(500.0);
    check(!state.forceInTransit(fx.forceId), "force arrived");
    check(state.forcePlanet(fx.forceId) == endor, "force now at destination");
    check(fx.sim->transitArrivals() == 1, "arrival counted");
    for (const eaw::GameObject* o : state.objectsOfType("X_WING")) {
        check(!o->hidden, "units visible after arrival");
        check(o->position.x == 500.0, "units at destination planet");
    }
}

void testFindPlanetNil() {
    TfFixture fx;
    fx.sim->scripts().runScript("p = FindPlanet('Missing')\n");
    lua_getglobal(fx.sim->scripts().state(), "p");
    check(lua_isnil(fx.sim->scripts().state(), -1), "FindPlanet missing returns nil");
    lua_pop(fx.sim->scripts().state(), 1);
}

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    testCreateAndQuery();
    testWrapperMethods();
    testMutators();
    testCollectiveOrders();
    testReleaseAndPrune();
    testGarrisonOrder();
    testGalacticMode();
    testFindPlanetNil();
    if (failures == 0) { std::printf("ALL TESTS PASSED\n"); return 0; }
    std::printf("%d FAILURES\n", failures);
    return 1;
}
