// Tests for the two-phase parallel combat pass.
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

// Two factions, a fighter type with damage and an armored type.
struct CombatFixture {
    std::unique_ptr<eaw::Simulation> sim;
    int rebelId = 0;
    int empireId = 0;
    int xwingId = 0;
    int isdId = 0;

    CombatFixture(double xwingDmg = 0.1, double isdHull = 1.0, double isdShield = 0.0) {
        sim = std::make_unique<eaw::Simulation>(4);
        eaw::Player& rebel = sim->sim().addPlayer("Rebel Alliance", "REBEL");
        eaw::Player& empire = sim->sim().addPlayer("Galactic Empire", "EMPIRE");
        rebelId = rebel.id;
        empireId = empire.id;

        eaw::ObjectType xwing;
        xwing.name = "X_WING";
        xwing.properties = {"Unit"};
        xwing.damage = xwingDmg;
        xwing.attackRate = 2.0;          // 2 shots/sec -> 0.5s cooldown
        xwing.maxRange = 500;
        sim->sim().addType(std::move(xwing));

        eaw::ObjectType isd;
        isd.name = "ISD";
        isd.properties = {"Unit"};
        isd.damage = 0.05;
        isd.attackRate = 1.0;
        isd.maxRange = 500;
        sim->sim().addType(std::move(isd));

        xwingId = sim->sim().spawnUnit("X_WING", rebelId, {0, 0, 0});
        isdId = sim->sim().spawnUnit("ISD", empireId, {50, 0, 0});
        sim->sim().object(isdId)->hull = isdHull;
        sim->sim().object(isdId)->shield = isdShield;
    }
};

void testSingleShooterDamagesTarget() {
    CombatFixture fx(0.1);
    // Order the X_WING to attack the ISD.
    fx.sim->scripts().runScript(
        "x = Find_First_Object('X_WING')\n"
        "t = Find_First_Object('ISD')\n"
        "x:Attack_Target(t)\n");
    fx.sim->tick(1.0 / 30.0); // first tick: cooldown starts at 0 -> fires
    const eaw::GameObject* isd = fx.sim->sim().object(fx.isdId);
    check(isd->hull < 1.0, "target takes damage from attack");
    check(fx.sim->totalShots() == 1, "one shot fired");
    check(isd->wasDamagedThisTick, "target flagged as damaged this tick");
}

void testCooldownLimitsFireRate() {
    CombatFixture fx(0.1);
    fx.sim->scripts().runScript(
        "Find_First_Object('X_WING'):Attack_Target(Find_First_Object('ISD'))\n");
    fx.sim->tick(1.0 / 30.0); // fires (cooldown 0 -> set to 0.5s)
    fx.sim->tick(1.0 / 30.0); // cooldown 0.467 -> no shot
    check(fx.sim->totalShots() == 1, "no shot while on cooldown");
    // After 0.5s of ticks, fires again.
    for (int i = 0; i < 16; ++i) fx.sim->tick(1.0 / 30.0); // 0.533s more
    check(fx.sim->totalShots() == 2, "fires again after cooldown expires");
}

void testOutOfRangeNoShot() {
    CombatFixture fx(0.1);
    // Move the ISD far away.
    fx.sim->sim().object(fx.isdId)->position = {10000, 0, 0};
    fx.sim->scripts().runScript(
        "Find_First_Object('X_WING'):Attack_Target(Find_First_Object('ISD'))\n");
    fx.sim->tick(1.0 / 30.0);
    check(fx.sim->totalShots() == 0, "no shot when target out of range");
}

void testNoFriendlyFire() {
    CombatFixture fx(0.1);
    // Both units rebel-owned: same player -> no shot.
    fx.sim->sim().object(fx.isdId)->playerId = fx.rebelId;
    fx.sim->scripts().runScript(
        "Find_First_Object('X_WING'):Attack_Target(Find_First_Object('ISD'))\n");
    fx.sim->tick(1.0 / 30.0);
    check(fx.sim->totalShots() == 0, "no friendly fire");
}

void testShieldAbsorbsFirst() {
    CombatFixture fx(0.03, 1.0, 0.05); // 0.03 dmg/shot < 0.05 shield
    fx.sim->scripts().runScript(
        "Find_First_Object('X_WING'):Attack_Target(Find_First_Object('ISD'))\n");
    fx.sim->tick(1.0 / 30.0);
    const eaw::GameObject* isd = fx.sim->sim().object(fx.isdId);
    check(isd->shield < 0.05, "shield absorbs damage first");
    check(isd->hull == 1.0, "hull untouched while shield holds");
    // Second shot spills to hull.
    for (int i = 0; i < 16; ++i) fx.sim->tick(1.0 / 30.0);
    check(fx.sim->sim().object(fx.isdId)->hull < 1.0, "hull damaged after shield gone");
}

void testCombatKillsTarget() {
    CombatFixture fx(0.5, 1.0); // 0.5 dmg per shot -> 2 shots to kill
    fx.sim->scripts().runScript(
        "Find_First_Object('X_WING'):Attack_Target(Find_First_Object('ISD'))\n");
    for (int i = 0; i < 40; ++i) fx.sim->tick(1.0 / 30.0);
    const eaw::GameObject* isd = fx.sim->sim().object(fx.isdId);
    check(!isd->alive, "target dies from sustained fire");
    check(isd->hull == 0.0, "hull reaches zero");
}

void testCombatTriggersDeathEvent() {
    CombatFixture fx(0.5);
    fx.sim->scripts().runScript(
        "DeathCount = 0\n"
        "function OnDeath(o)\n"
        "  DeathCount = DeathCount + 1\n"
        "end\n"
        "Register_Death_Event(Find_First_Object('ISD'), OnDeath)\n"
        "Find_First_Object('X_WING'):Attack_Target(Find_First_Object('ISD'))\n");
    for (int i = 0; i < 40; ++i) fx.sim->tick(1.0 / 30.0);
    lua_getglobal(fx.sim->scripts().state(), "DeathCount");
    check(lua_tointeger(fx.sim->scripts().state(), -1) == 1,
          "combat kill triggers the death event");
    lua_pop(fx.sim->scripts().state(), 1);
}

void testCombatDeterminism() {
    // Two identical sims with several shooters on one target must produce
    // identical outcomes (per-target slot accumulation, no races).
    auto setup = [](eaw::Simulation& sim, const char* script) {
        eaw::Player& rebel = sim.sim().addPlayer("Rebel Alliance", "REBEL");
        eaw::Player& empire = sim.sim().addPlayer("Galactic Empire", "EMPIRE");
        eaw::ObjectType xwing;
        xwing.name = "X_WING";
        xwing.properties = {"Unit"};
        xwing.damage = 0.08;
        xwing.attackRate = 3.0;
        xwing.maxRange = 500;
        sim.sim().addType(std::move(xwing));
        eaw::ObjectType isd;
        isd.name = "ISD";
        isd.properties = {"Unit"};
        isd.damage = 0.0; // non-combatant
        sim.sim().addType(std::move(isd));
        for (int i = 0; i < 6; ++i) {
            sim.sim().spawnUnit("X_WING", rebel.id, {static_cast<double>(i), 0, 0});
        }
        sim.sim().spawnUnit("ISD", empire.id, {100, 0, 0});
        sim.scripts().runScript(script);
    };
    const char* script =
        "t = Find_First_Object('ISD')\n"
        "for i, o in ipairs(Find_All_Objects_Of_Type('X_WING')) do\n"
        "  o:Attack_Target(t)\n"
        "end\n";
    eaw::Simulation a(4), b(3); // different worker counts
    setup(a, script);
    setup(b, script);
    for (int i = 0; i < 60; ++i) { a.tick(1.0 / 30.0); b.tick(1.0 / 30.0); }
    const eaw::GameObject* isdA = a.sim().object(a.sim().objectsOfType("ISD")[0]->id);
    const eaw::GameObject* isdB = b.sim().object(b.sim().objectsOfType("ISD")[0]->id);
    check(isdA->hull == isdB->hull && isdA->alive == isdB->alive,
          "combat outcome identical across worker counts");
    check(a.totalShots() == b.totalShots(), "shot counts identical across worker counts");
}

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    testSingleShooterDamagesTarget();
    testCooldownLimitsFireRate();
    testOutOfRangeNoShot();
    testNoFriendlyFire();
    testShieldAbsorbsFirst();
    testCombatKillsTarget();
    testCombatTriggersDeathEvent();
    testCombatDeterminism();
    if (failures == 0) { std::printf("ALL TESTS PASSED\n"); return 0; }
    std::printf("%d FAILURES\n", failures);
    return 1;
}
