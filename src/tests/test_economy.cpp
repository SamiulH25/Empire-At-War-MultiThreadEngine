// Tests for the economy: income, credits, tech gates, AI production.
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

struct EcoFixture {
    std::unique_ptr<eaw::Simulation> sim;
    int rebelId = 0;
    int empireId = 0;

    EcoFixture() {
        sim = std::make_unique<eaw::Simulation>(4);
        eaw::Player& rebel = sim->sim().addPlayer("Rebel Alliance", "REBEL");
        eaw::Player& empire = sim->sim().addPlayer("Galactic Empire", "EMPIRE");
        rebel.human = true;
        rebelId = rebel.id;
        empireId = empire.id;
        eaw::ObjectType xwing;
        xwing.name = "X-Wing";
        xwing.properties = {"Unit"};
        xwing.damage = 0.01;
        xwing.attackRate = 1.0;
        xwing.maxRange = 300;
        xwing.buildCost = 100.0;
        xwing.techLevel = 0;
        sim->sim().addType(std::move(xwing));
        eaw::ObjectType isd;
        isd.name = "ISD";
        isd.properties = {"Unit"};
        isd.damage = 0.05;
        isd.attackRate = 0.5;
        isd.maxRange = 500;
        isd.buildCost = 1000.0;
        isd.techLevel = 3;
        sim->sim().addType(std::move(isd));
    }
};

void testIncomeAccrues() {
    EcoFixture fx;
    fx.sim->sim().player(fx.rebelId)->credits = 0;
    fx.sim->sim().player(fx.rebelId)->incomePerSecond = 100.0;
    fx.sim->tick(1.0);
    fx.sim->tick(1.0);
    const eaw::Player* p = fx.sim->sim().player(fx.rebelId);
    check(p->credits > 199.0 && p->credits < 201.0, "income accrues per second");
}

void testGiveMoneyBinding() {
    EcoFixture fx;
    fx.sim->scripts().runScript(
        "p = Find_Player('REBEL')\n"
        "p:Give_Money(500)\n"
        "c = p:Get_Credits()\n");
    lua_getglobal(fx.sim->scripts().state(), "c");
    double c = lua_tonumber(fx.sim->scripts().state(), -1);
    check(c > 499.0 && c < 501.0, "Give_Money adds credits");
    lua_pop(fx.sim->scripts().state(), 1);
}

void testTechGate() {
    EcoFixture fx;
    // ISD requires tech 3; rebel at tech 0 cannot build it.
    check(!fx.sim->sim().canBuild(fx.rebelId, "ISD"), "tech-gated type blocked");
    check(fx.sim->sim().canBuild(fx.rebelId, "X-Wing"), "tech-0 type buildable");
    fx.sim->sim().setTechLevel(fx.rebelId, 3);
    check(fx.sim->sim().canBuild(fx.rebelId, "ISD"), "tech 3 unlocks ISD");
}

void testTechBinding() {
    EcoFixture fx;
    fx.sim->scripts().runScript(
        "p = Find_Player('REBEL')\n"
        "p:Set_Tech_Level(3)\n"
        "t = p:Get_Tech_Level()\n");
    lua_getglobal(fx.sim->scripts().state(), "t");
    check(lua_tointeger(fx.sim->scripts().state(), -1) == 3, "Set_Tech_Level binding");
    lua_pop(fx.sim->scripts().state(), 1);
}

void testUnlockLock() {
    EcoFixture fx;
    // Everything defaults to unlocked (tech permitting). Locking a type
    // blocks it; unlocking restores it.
    fx.sim->sim().setTechLevel(fx.rebelId, 3);
    check(fx.sim->sim().canBuild(fx.rebelId, "ISD"), "default: ISD buildable");
    fx.sim->sim().lockType(fx.rebelId, "ISD");
    check(!fx.sim->sim().canBuild(fx.rebelId, "ISD"), "locked type blocked");
    check(fx.sim->sim().canBuild(fx.rebelId, "X-Wing"), "other types still buildable");
    fx.sim->sim().unlockType(fx.rebelId, "ISD");
    check(fx.sim->sim().canBuild(fx.rebelId, "ISD"), "unlocked type buildable again");
}

void testBuildSpendsCredits() {
    EcoFixture fx;
    fx.sim->sim().giveMoney(fx.rebelId, 500);
    int id = fx.sim->sim().buildUnit(fx.rebelId, "X-Wing", {0, 0, 0});
    check(id != 0, "unit built with credits");
    check(fx.sim->sim().player(fx.rebelId)->credits > 399.0 &&
              fx.sim->sim().player(fx.rebelId)->credits < 401.0,
          "build cost deducted");
    // Not enough for an ISD.
    int id2 = fx.sim->sim().buildUnit(fx.rebelId, "ISD", {0, 0, 0});
    check(id2 == 0, "unaffordable build rejected");
}

void testAiProduction() {
    EcoFixture fx;
    // Empire (AI) gets an attack taskforce and a fat wallet; the AI must
    // spend credits to grow the force toward its targets.
    int forceId = fx.sim->sim().addTaskForce(fx.empireId, "AttackPlan");
    fx.sim->sim().giveMoney(fx.empireId, 10000);
    fx.sim->sim().setTechLevel(fx.empireId, 3);
    fx.sim->setAiBuildTypes({"X-Wing", "ISD"});
    // Tick a while: income + production.
    for (int i = 0; i < 300; ++i) fx.sim->tick(1.0 / 30.0);
    const eaw::TaskForce* f = fx.sim->sim().taskForce(forceId);
    check(!f->units.empty(), "AI produced units into the force");
    check(fx.sim->sim().player(fx.empireId)->credits < 10000.0,
          "AI spent credits on production");
    // Count heavies (build cost >= 500) — at least one should exist.
    int heavies = 0, fighters = 0;
    for (int uid : f->units) {
        const eaw::GameObject* o = fx.sim->sim().object(uid);
        if (!o) continue;
        const eaw::ObjectType* t = fx.sim->sim().type(o->typeName);
        if (t && t->buildCost >= 500.0) ++heavies; else ++fighters;
    }
    check(fighters > 0, "AI built fighters");
    check(heavies > 0, "AI built a heavy unit");
}

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    testIncomeAccrues();
    testGiveMoneyBinding();
    testTechGate();
    testTechBinding();
    testUnlockLock();
    testBuildSpendsCredits();
    testAiProduction();
    if (failures == 0) { std::printf("ALL TESTS PASSED\n"); return 0; }
    std::printf("%d FAILURES\n", failures);
    return 1;
}
