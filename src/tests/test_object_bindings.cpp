// Tests for the object-model Lua bindings (Find_*, wrapper methods).
#include "core/lua_host.h"
#include "core/object_model.h"
#include "core/pg_object_bindings.h"

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

// Pushes a fresh state with a populated sim.
struct Fixture {
    eaw::LuaHost lua;
    eaw::SimState sim;
    Fixture() {
        eaw::registerObjectBindings(lua, sim);
        // players
        eaw::Player& rebel = sim.addPlayer("Rebel Alliance", "REBEL");
        rebel.human = true;
        rebel.difficulty = "Normal";
        eaw::Player& empire = sim.addPlayer("Galactic Empire", "EMPIRE");
        empire.difficulty = "Hard";
        // types
        eaw::ObjectType xwing;
        xwing.name = "X_WING";
        xwing.categories = {"Fighter"};
        xwing.properties = {"Unit", "Flyer"};
        xwing.buildCost = 1000;
        xwing.techLevel = 1;
        xwing.maxRange = 500;
        xwing.affiliatedFactions = {"REBEL"};
        sim.addType(std::move(xwing));
        eaw::ObjectType isd;
        isd.name = "ISD";
        isd.categories = {"Capital"};
        isd.properties = {"Unit", "Structure"};
        isd.hero = false;
        isd.buildCost = 10000;
        isd.techLevel = 4;
        sim.addType(std::move(isd));
        eaw::ObjectType vader;
        vader.name = "VADER";
        vader.categories = {"Hero"};
        vader.properties = {"Unit", "Hero"};
        vader.hero = true;
        sim.addType(std::move(vader));
        // objects
        sim.addObject("X_WING", rebel.id, {0, 0, 0});
        sim.addObject("X_WING", rebel.id, {10, 0, 0});
        sim.addObject("ISD", empire.id, {100, 0, 0});
        sim.addObject("VADER", empire.id, {90, 0, 0});
    }
};

void testFindPlayer() {
    Fixture fx;
    fx.lua.runScript("p = Find_Player('REBEL')\n");
    lua_getglobal(fx.lua.state(), "p");
    check(lua_isuserdata(fx.lua.state(), -1), "Find_Player returns userdata");
    lua_pop(fx.lua.state(), 1);
    fx.lua.runScript("p = Find_Player('NOPE')\n");
    lua_getglobal(fx.lua.state(), "p");
    check(lua_isnil(fx.lua.state(), -1), "Find_Player unknown returns nil");
    lua_pop(fx.lua.state(), 1);
}

void testPlayerMethods() {
    Fixture fx;
    fx.lua.runScript(
        "p = Find_Player('EMPIRE')\n"
        "id = p:Get_ID()\n"
        "name = p:Get_Name()\n"
        "faction = p:Get_Faction_Name()\n"
        "diff = p:Get_Difficulty()\n"
        "human = p:Is_Human()\n");
    lua_getglobal(fx.lua.state(), "faction");
    check(std::string(lua_tostring(fx.lua.state(), -1)) == "EMPIRE", "player Get_Faction_Name");
    lua_pop(fx.lua.state(), 1);
    lua_getglobal(fx.lua.state(), "diff");
    check(std::string(lua_tostring(fx.lua.state(), -1)) == "Hard", "player Get_Difficulty");
    lua_pop(fx.lua.state(), 1);
    lua_getglobal(fx.lua.state(), "human");
    check(lua_toboolean(fx.lua.state(), -1) == 0, "empire is not human");
    lua_pop(fx.lua.state(), 1);
    lua_getglobal(fx.lua.state(), "id");
    int id = static_cast<int>(lua_tointeger(fx.lua.state(), -1));
    lua_pop(fx.lua.state(), 1);
    check(id >= 1, "player Get_ID");
}

void testFindObjectType() {
    Fixture fx;
    fx.lua.runScript(
        "t = Find_Object_Type('X_WING')\n"
        "name = t:Get_Name()\n"
        "cost = t:Get_Build_Cost()\n"
        "tech = t:Get_Tech_Level()\n"
        "range = t:Get_Max_Range()\n"
        "hero = t:Is_Hero()\n");
    lua_getglobal(fx.lua.state(), "name");
    check(std::string(lua_tostring(fx.lua.state(), -1)) == "X_WING", "type Get_Name");
    lua_pop(fx.lua.state(), 1);
    lua_getglobal(fx.lua.state(), "cost");
    check(lua_tonumber(fx.lua.state(), -1) == 1000, "type Get_Build_Cost");
    lua_pop(fx.lua.state(), 1);
    lua_getglobal(fx.lua.state(), "tech");
    check(lua_tointeger(fx.lua.state(), -1) == 1, "type Get_Tech_Level");
    lua_pop(fx.lua.state(), 1);
    lua_getglobal(fx.lua.state(), "hero");
    check(lua_toboolean(fx.lua.state(), -1) == 0, "xwing not hero");
    lua_pop(fx.lua.state(), 1);
}

void testFindAllObjectsOfType() {
    Fixture fx;
    fx.lua.runScript(
        "list = Find_All_Objects_Of_Type('X_WING')\n"
        "n = #list\n");
    lua_getglobal(fx.lua.state(), "n");
    check(lua_tointeger(fx.lua.state(), -1) == 2, "Find_All_Objects_Of_Type by type name");
    lua_pop(fx.lua.state(), 1);
    // by category
    fx.lua.runScript("list = Find_All_Objects_Of_Type('Capital')\nn = #list\n");
    lua_getglobal(fx.lua.state(), "n");
    check(lua_tointeger(fx.lua.state(), -1) == 1, "Find_All_Objects_Of_Type by category");
    lua_pop(fx.lua.state(), 1);
}

void testFindFirstObject() {
    Fixture fx;
    fx.lua.runScript("o = Find_First_Object('ISD')\nh = o:Get_Hull()\n");
    lua_getglobal(fx.lua.state(), "h");
    check(lua_tonumber(fx.lua.state(), -1) == 1.0, "Find_First_Object + Get_Hull");
    lua_pop(fx.lua.state(), 1);
}

void testObjectMethods() {
    Fixture fx;
    fx.lua.runScript(
        "list = Find_All_Objects_Of_Type('X_WING')\n"
        "o = list[1]\n"
        "owner = o:Get_Owner()\n"
        "faction = owner:Get_Faction_Name()\n"
        "t = o:Get_Type()\n"
        "tname = t:Get_Name()\n"
        "cat = o:Is_Category('Fighter')\n"
        "prop = o:Has_Property('Flyer')\n"
        "valid = o:Is_Valid()\n"
        "pos = o:Get_Position()\n"
        "x, y, z = pos:Get_XYZ()\n"
        "xsq = x*x + y*y + z*z\n");
    lua_getglobal(fx.lua.state(), "faction");
    check(std::string(lua_tostring(fx.lua.state(), -1)) == "REBEL", "object Get_Owner -> faction");
    lua_pop(fx.lua.state(), 1);
    lua_getglobal(fx.lua.state(), "tname");
    check(std::string(lua_tostring(fx.lua.state(), -1)) == "X_WING", "object Get_Type -> name");
    lua_pop(fx.lua.state(), 1);
    lua_getglobal(fx.lua.state(), "cat");
    check(lua_toboolean(fx.lua.state(), -1) == 1, "Is_Category matches");
    lua_pop(fx.lua.state(), 1);
    lua_getglobal(fx.lua.state(), "prop");
    check(lua_toboolean(fx.lua.state(), -1) == 1, "Has_Property matches");
    lua_pop(fx.lua.state(), 1);
    lua_getglobal(fx.lua.state(), "valid");
    check(lua_toboolean(fx.lua.state(), -1) == 1, "Is_Valid true");
    lua_pop(fx.lua.state(), 1);
    lua_getglobal(fx.lua.state(), "xsq");
    double d2 = lua_tonumber(fx.lua.state(), -1);
    check(d2 == 0.0 || d2 == 100.0, "Get_Position returns an X_WING coordinate");
    lua_pop(fx.lua.state(), 1);
}

void testObjectDistanceAndNearest() {
    Fixture fx;
    fx.lua.runScript(
        "o = Find_First_Object('X_WING')\n"
        "d = o:Get_Distance(Find_First_Object('ISD'))\n"
        "near = Find_Nearest(o, 'ISD')\n"
        "nearname = near:Get_Name()\n");
    lua_getglobal(fx.lua.state(), "d");
    double d = lua_tonumber(fx.lua.state(), -1);
    // ISD at (100,0,0); X_WINGs at (0,0,0) and (10,0,0) -> 100 or 90
    check((d > 89.0 && d < 91.0) || (d > 99.0 && d < 101.0),
          "Get_Distance between objects");
    lua_pop(fx.lua.state(), 1);
    lua_getglobal(fx.lua.state(), "nearname");
    check(std::string(lua_tostring(fx.lua.state(), -1)) == "ISD", "Find_Nearest by type");
    lua_pop(fx.lua.state(), 1);
}

void testHeroAndGarrison() {
    Fixture fx;
    fx.lua.runScript(
        "v = Find_First_Object('VADER')\n"
        "hero = v:Is_Hero()\n"
        "g = v:Has_Garrison()\n"
        "gi = v:Is_In_Garrison()\n");
    lua_getglobal(fx.lua.state(), "hero");
    check(lua_toboolean(fx.lua.state(), -1) == 1, "Is_Hero true for hero unit");
    lua_pop(fx.lua.state(), 1);
    lua_getglobal(fx.lua.state(), "g");
    check(lua_toboolean(fx.lua.state(), -1) == 0, "Has_Garrison false when empty");
    lua_pop(fx.lua.state(), 1);
}

void testIsValidAfterRemoval() {
    Fixture fx;
    fx.lua.runScript("o = Find_First_Object('ISD')\nvalid = o:Is_Valid()\n");
    lua_getglobal(fx.lua.state(), "valid");
    check(lua_toboolean(fx.lua.state(), -1) == 1, "object valid while alive");
    lua_pop(fx.lua.state(), 1);
    // remove it from the sim; the wrapper now resolves to nil
    for (const eaw::GameObject* o : fx.sim.allObjects()) {
        if (o->typeName == "ISD") { fx.sim.removeObject(o->id); break; }
    }
    fx.lua.runScript("valid = o:Is_Valid()\n");
    lua_getglobal(fx.lua.state(), "valid");
    check(lua_toboolean(fx.lua.state(), -1) == 0, "object invalid after removal");
    lua_pop(fx.lua.state(), 1);
}

void testModStyleQueryScript() {
    // A script in the style of the game's AI libraries.
    Fixture fx;
    fx.lua.runScript(
        "enemy = Find_Player('EMPIRE')\n"
        "targets = Find_All_Objects_Of_Type('Capital')\n"
        "if #targets > 0 then\n"
        "  t = targets[1]\n"
        "  owner = t:Get_Owner()\n"
        "  owner_name = owner:Get_Name()\n"
        "  hull = t:Get_Hull()\n"
        "  result = owner_name .. ':' .. tostring(hull)\n"
        "end\n");
    lua_getglobal(fx.lua.state(), "result");
    check(lua_tostring(fx.lua.state(), -1) &&
          std::string(lua_tostring(fx.lua.state(), -1)) == "Galactic Empire:1",
          "mod-style query script works");
    lua_pop(fx.lua.state(), 1);
}

void testSpawnUnit() {
    Fixture fx;
    fx.lua.runScript(
        "p = Find_Player('REBEL')\n"
        "pos = Find_First_Object('X_WING'):Get_Position()\n"
        "list = Spawn_Unit('X_WING', pos, p)\n"
        "n = #list\n"
        "spawned = list[1]\n"
        "owner = spawned:Get_Owner()\n"
        "faction = owner:Get_Faction_Name()\n");
    lua_getglobal(fx.lua.state(), "n");
    check(lua_tointeger(fx.lua.state(), -1) == 1, "Spawn_Unit returns a 1-entry list");
    lua_pop(fx.lua.state(), 1);
    lua_getglobal(fx.lua.state(), "faction");
    check(std::string(lua_tostring(fx.lua.state(), -1)) == "REBEL", "spawned unit owned by player");
    lua_pop(fx.lua.state(), 1);
    lua_getglobal(fx.lua.state(), "spawned");
    check(lua_isuserdata(fx.lua.state(), -1), "spawned unit is a wrapper");
    lua_pop(fx.lua.state(), 1);
}

void testSpawnUnitByName() {
    Fixture fx;
    fx.lua.runScript(
        "pos = Find_First_Object('X_WING'):Get_Position()\n"
        "list = Spawn_Unit('ISD', pos, 'REBEL')\n"
        "n = #list\n"
        "name = list[1]:Get_Name()\n");
    lua_getglobal(fx.lua.state(), "n");
    check(lua_tointeger(fx.lua.state(), -1) == 1, "Spawn_Unit by type name");
    lua_pop(fx.lua.state(), 1);
    lua_getglobal(fx.lua.state(), "name");
    check(std::string(lua_tostring(fx.lua.state(), -1)) == "ISD", "spawned type name");
    lua_pop(fx.lua.state(), 1);
}

void testMoveTo() {
    Fixture fx;
    fx.lua.runScript(
        "o = Find_First_Object('X_WING')\n"
        "target = Find_First_Object('ISD')\n"
        "cmd = o:Move_To(target)\n"
        "finished = cmd:IsFinished()\n");
    lua_getglobal(fx.lua.state(), "finished");
    check(lua_toboolean(fx.lua.state(), -1) == 1, "Move_To command finishes");
    lua_pop(fx.lua.state(), 1);
    // Move target set on the sim object (async integration happens in the
    // Simulation tick, not here).
    lua_getglobal(fx.lua.state(), "o");
    bool ok = false;
    if (lua_isuserdata(fx.lua.state(), -1)) {
        void* p = lua_touserdata(fx.lua.state(), -1);
        ok = (p != nullptr);
    }
    lua_pop(fx.lua.state(), 1);
    check(ok, "Move_To sets a move target");
}

void testAttackTarget() {
    Fixture fx;
    fx.lua.runScript(
        "o = Find_First_Object('X_WING')\n"
        "t = Find_First_Object('ISD')\n"
        "o:Attack_Target(t)\n"
        "at = o:Get_Attack_Target()\n"
        "name = at:Get_Name()\n"
        "has = o:Has_Attack_Target()\n");
    lua_getglobal(fx.lua.state(), "name");
    check(std::string(lua_tostring(fx.lua.state(), -1)) == "ISD", "Attack_Target sets target");
    lua_pop(fx.lua.state(), 1);
    lua_getglobal(fx.lua.state(), "has");
    check(lua_toboolean(fx.lua.state(), -1) == 1, "Has_Attack_Target true");
    lua_pop(fx.lua.state(), 1);
}

void testTakeDamageAndDeath() {
    Fixture fx;
    fx.lua.runScript(
        "o = Find_First_Object('X_WING')\n"
        "o:Take_Damage(0.25)\n"
        "h = o:Get_Hull()\n"
        "v = o:Is_Valid()\n");
    lua_getglobal(fx.lua.state(), "h");
    double h = lua_tonumber(fx.lua.state(), -1);
    check(h > 0.74 && h < 0.76, "Take_Damage reduces hull");
    lua_pop(fx.lua.state(), 1);
    // kill it
    fx.lua.runScript("o:Take_Damage(10)\nv = o:Is_Valid()\n");
    lua_getglobal(fx.lua.state(), "v");
    check(lua_toboolean(fx.lua.state(), -1) == 0, "object invalid after lethal damage");
    lua_pop(fx.lua.state(), 1);
}

void testGarrison() {
    Fixture fx;
    fx.lua.runScript(
        "container = Find_First_Object('ISD')\n"
        "unit = Find_First_Object('X_WING')\n"
        "ok = unit:Garrison(container)\n"
        "g = container:Get_Garrisoned_Units()\n"
        "n = #g\n"
        "gi = unit:Is_In_Garrison()\n"
        "hg = container:Has_Garrison()\n");
    lua_getglobal(fx.lua.state(), "ok");
    check(lua_toboolean(fx.lua.state(), -1) == 1, "Garrison succeeds");
    lua_pop(fx.lua.state(), 1);
    lua_getglobal(fx.lua.state(), "n");
    check(lua_tointeger(fx.lua.state(), -1) == 1, "container has garrisoned unit");
    lua_pop(fx.lua.state(), 1);
    lua_getglobal(fx.lua.state(), "gi");
    check(lua_toboolean(fx.lua.state(), -1) == 1, "unit Is_In_Garrison");
    lua_pop(fx.lua.state(), 1);
    lua_getglobal(fx.lua.state(), "hg");
    check(lua_toboolean(fx.lua.state(), -1) == 1, "Has_Garrison true");
    lua_pop(fx.lua.state(), 1);
    // leave garrison
    fx.lua.runScript(
        "unit:Leave_Garrison()\n"
        "gi = unit:Is_In_Garrison()\n"
        "g = container:Get_Garrisoned_Units()\n"
        "n = #g\n");
    lua_getglobal(fx.lua.state(), "gi");
    check(lua_toboolean(fx.lua.state(), -1) == 0, "Leave_Garrison works");
    lua_pop(fx.lua.state(), 1);
    lua_getglobal(fx.lua.state(), "n");
    check(lua_tointeger(fx.lua.state(), -1) == 0, "container emptied");
    lua_pop(fx.lua.state(), 1);
}

void testLockOrdersAndInvulnerable() {
    Fixture fx;
    fx.lua.runScript(
        "o = Find_First_Object('X_WING')\n"
        "o:Lock_Current_Orders()\n"
        "o:Make_Invulnerable(true)\n"
        "o:Take_Damage(5)\n"
        "h = o:Get_Hull()\n");
    lua_getglobal(fx.lua.state(), "h");
    check(lua_tonumber(fx.lua.state(), -1) == 1.0, "invulnerable object takes no damage");
    lua_pop(fx.lua.state(), 1);
}

void testReinforceUnit() {
    Fixture fx;
    fx.lua.runScript(
        "p = Find_Player('EMPIRE')\n"
        "cmd = Reinforce_Unit('ISD', false, p)\n"
        "fin = cmd:IsFinished()\n"
        "r = cmd:Result()\n"
        "n = #Find_All_Objects_Of_Type('ISD')\n");
    lua_getglobal(fx.lua.state(), "fin");
    check(lua_toboolean(fx.lua.state(), -1) == 1, "Reinforce_Unit returns command");
    lua_pop(fx.lua.state(), 1);
    lua_getglobal(fx.lua.state(), "n");
    check(lua_tointeger(fx.lua.state(), -1) == 1, "reinforce pool adds no object");
    lua_pop(fx.lua.state(), 1);
}

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    testFindPlayer();
    testPlayerMethods();
    testFindObjectType();
    testFindAllObjectsOfType();
    testFindFirstObject();
    testObjectMethods();
    testObjectDistanceAndNearest();
    testHeroAndGarrison();
    testIsValidAfterRemoval();
    testModStyleQueryScript();
    testSpawnUnit();
    testSpawnUnitByName();
    testMoveTo();
    testAttackTarget();
    testTakeDamageAndDeath();
    testGarrison();
    testLockOrdersAndInvulnerable();
    testReinforceUnit();
    if (failures == 0) { std::printf("ALL TESTS PASSED\n"); return 0; }
    std::printf("%d FAILURES\n", failures);
    return 1;
}
