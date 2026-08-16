// Tests for the event system (timers, death/attacked/proximity events).
#include "core/meg_file.h"
#include "core/simulation.h"

#include <cstdio>
#include <memory>
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

std::unique_ptr<eaw::Simulation> makeSimWithUnits() {
    auto sim = std::make_unique<eaw::Simulation>();
    eaw::Player& rebel = sim->sim().addPlayer("Rebel Alliance", "REBEL");
    eaw::Player& empire = sim->sim().addPlayer("Galactic Empire", "EMPIRE");
    eaw::ObjectType xwing;
    xwing.name = "X_WING";
    xwing.properties = {"Unit"};
    sim->sim().addType(std::move(xwing));
    eaw::ObjectType isd;
    isd.name = "ISD";
    isd.properties = {"Unit"};
    sim->sim().addType(std::move(isd));
    sim->sim().spawnUnit("X_WING", rebel.id, {0, 0, 0});
    sim->sim().spawnUnit("ISD", empire.id, {100, 0, 0});
    return sim;
}

void testTimerFiresWithParam() {
    eaw::Simulation sim;
    sim.scripts().runScript(
        "TimerResult = 'not fired'\n"
        "function OnTimer(p)\n"
        "  TimerResult = p\n"
        "end\n"
        "Register_Timer(OnTimer, 1.0, 'hello')\n");
    sim.tick(0.5);
    lua_getglobal(sim.scripts().state(), "TimerResult");
    check(std::string(lua_tostring(sim.scripts().state(), -1)) == "not fired",
          "timer not fired before timeout");
    lua_pop(sim.scripts().state(), 1);
    sim.tick(0.5); // now = 1.0 -> fires
    lua_getglobal(sim.scripts().state(), "TimerResult");
    check(std::string(lua_tostring(sim.scripts().state(), -1)) == "hello",
          "timer fires with param after timeout");
    lua_pop(sim.scripts().state(), 1);
    // Fired once only.
    sim.tick(1.0);
    lua_getglobal(sim.scripts().state(), "TimerResult");
    check(std::string(lua_tostring(sim.scripts().state(), -1)) == "hello",
          "timer does not re-fire");
    lua_pop(sim.scripts().state(), 1);
}

void testDeathEvent() {
    auto sim = makeSimWithUnits();
    sim->scripts().runScript(
        "DeathCount = 0\n"
        "function OnDeath(o)\n"
        "  DeathCount = DeathCount + 1\n"
        "  Died = o:Get_Name()\n"
        "end\n"
        "Register_Death_Event(Find_First_Object('ISD'), OnDeath)\n");
    sim->tick(1.0 / 30.0);
    lua_getglobal(sim->scripts().state(), "DeathCount");
    check(lua_tointeger(sim->scripts().state(), -1) == 0, "no death before damage");
    lua_pop(sim->scripts().state(), 1);
    // Kill the ISD via script.
    sim->scripts().runScript("Find_First_Object('ISD'):Take_Damage(10)\n");
    sim->tick(1.0 / 30.0);
    lua_getglobal(sim->scripts().state(), "DeathCount");
    check(lua_tointeger(sim->scripts().state(), -1) == 1, "death event fired");
    lua_pop(sim->scripts().state(), 1);
    lua_getglobal(sim->scripts().state(), "Died");
    check(std::string(lua_tostring(sim->scripts().state(), -1)) == "ISD",
          "death callback receives the dying object");
    lua_pop(sim->scripts().state(), 1);
}

void testAttackedEvent() {
    auto sim = makeSimWithUnits();
    sim->scripts().runScript(
        "AttackedCount = 0\n"
        "function OnAttacked(o)\n"
        "  AttackedCount = AttackedCount + 1\n"
        "end\n"
        "Register_Attacked_Event(Find_First_Object('X_WING'), OnAttacked)\n");
    sim->tick(1.0 / 30.0);
    sim->scripts().runScript("Find_First_Object('X_WING'):Take_Damage(0.1)\n");
    sim->tick(1.0 / 30.0);
    lua_getglobal(sim->scripts().state(), "AttackedCount");
    check(lua_tointeger(sim->scripts().state(), -1) == 1, "attacked event fired on damage");
    lua_pop(sim->scripts().state(), 1);
    // No more damage -> no more events.
    sim->tick(1.0 / 30.0);
    lua_getglobal(sim->scripts().state(), "AttackedCount");
    check(lua_tointeger(sim->scripts().state(), -1) == 1, "attacked event not re-fired without damage");
    lua_pop(sim->scripts().state(), 1);
}

void testCancelAttackedEvent() {
    auto sim = makeSimWithUnits();
    sim->scripts().runScript(
        "AttackedCount = 0\n"
        "function OnAttacked(o)\n"
        "  AttackedCount = AttackedCount + 1\n"
        "end\n"
        "x = Find_First_Object('X_WING')\n"
        "Register_Attacked_Event(x, OnAttacked)\n"
        "Cancel_Attacked_Event(x)\n");
    sim->scripts().runScript("Find_First_Object('X_WING'):Take_Damage(0.1)\n");
    sim->tick(1.0 / 30.0);
    lua_getglobal(sim->scripts().state(), "AttackedCount");
    check(lua_tointeger(sim->scripts().state(), -1) == 0, "cancelled attacked event does not fire");
    lua_pop(sim->scripts().state(), 1);
}

void testProximityEvent() {
    auto sim = makeSimWithUnits();
    sim->scripts().runScript(
        "ProxCount = 0\n"
        "function OnProx(self, other)\n"
        "  ProxCount = ProxCount + 1\n"
        "  ProxOther = other:Get_Name()\n"
        "end\n"
        "Register_Prox(Find_First_Object('ISD'), OnProx, 50)\n");
    // ISD at (100,0,0), X_WING at (0,0,0): 100 apart, out of range 50.
    sim->tick(1.0 / 30.0);
    lua_getglobal(sim->scripts().state(), "ProxCount");
    check(lua_tointeger(sim->scripts().state(), -1) == 0, "no prox while out of range");
    lua_pop(sim->scripts().state(), 1);
    // Move the X_WING next to the ISD.
    sim->scripts().runScript(
        "x = Find_First_Object('X_WING')\n"
        "x:Move_To(Find_First_Object('ISD'))\n");
    // Move speed 50 u/s; 100 units -> ~2s. Step until in range.
    for (int i = 0; i < 90; ++i) sim->tick(1.0 / 30.0);
    lua_getglobal(sim->scripts().state(), "ProxCount");
    check(lua_tointeger(sim->scripts().state(), -1) == 1, "prox fires when in range");
    lua_pop(sim->scripts().state(), 1);
    lua_getglobal(sim->scripts().state(), "ProxOther");
    check(std::string(lua_tostring(sim->scripts().state(), -1)) == "X_WING",
          "prox callback receives the other object");
    lua_pop(sim->scripts().state(), 1);
}

void testPumpService() {
    eaw::Simulation sim;
    sim.scripts().runScript(
        "TimerResult = 0\n"
        "function OnTimer(p)\n"
        "  TimerResult = p\n"
        "end\n"
        "Register_Timer(OnTimer, 0.5, 42)\n"
        "Pump_Service()\n");
    // Pump_Service right after registration: time is 0, timer not due.
    lua_getglobal(sim.scripts().state(), "TimerResult");
    check(lua_tointeger(sim.scripts().state(), -1) == 0, "Pump_Service before timeout does nothing");
    lua_pop(sim.scripts().state(), 1);
    sim.tick(0.6);
    lua_getglobal(sim.scripts().state(), "TimerResult");
    check(lua_tointeger(sim.scripts().state(), -1) == 42, "timer fired after tick");
    lua_pop(sim.scripts().state(), 1);
}

void testEventFromMegScript() {
    // Full path: a script from a meg registers a timer that spawns a unit.
    auto meg = makeMeg("DATA\\SCRIPTS\\AI\\EVENTPLAN.LUA",
        "function OnTimeout()\n"
        "  p = Find_Player('REBEL')\n"
        "  pos = Create_Position(1, 2, 3)\n"
        "  Spawn_Unit('X_WING', pos, p)\n"
        "end\n"
        "Register_Timer(OnTimeout, 0.25)\n");
    eaw::MegFile mf = eaw::MegFile::Parse(meg);
    eaw::Simulation sim;
    sim.files().addArchive("test.meg", meg, mf);
    eaw::Player& rebel = sim.sim().addPlayer("Rebel Alliance", "REBEL");
    eaw::ObjectType xwing;
    xwing.name = "X_WING";
    xwing.properties = {"Unit"};
    sim.sim().addType(std::move(xwing));
    sim.scripts().loadScript("DATA\\SCRIPTS\\AI\\EVENTPLAN.LUA");
    check(sim.sim().objectsOfType("X_WING").empty(), "no unit before timer");
    sim.tick(0.3);
    auto objs = sim.sim().objectsOfType("X_WING");
    check(objs.size() == 1, "timer spawned a unit");
    if (objs.size() == 1) {
        check(objs[0]->position.x == 1.0, "spawned at timer-script position");
    }
}

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    testTimerFiresWithParam();
    testDeathEvent();
    testAttackedEvent();
    testCancelAttackedEvent();
    testProximityEvent();
    testPumpService();
    testEventFromMegScript();
    if (failures == 0) { std::printf("ALL TESTS PASSED\n"); return 0; }
    std::printf("%d FAILURES\n", failures);
    return 1;
}
