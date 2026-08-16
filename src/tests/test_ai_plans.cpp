// Tests for multi-stage AI plans (assemble -> move -> attack -> done).
#include "core/simulation.h"

#include <cstdio>
#include <memory>

namespace {

int failures = 0;
void check(bool c, const char* w) {
    std::printf("%s: %s\n", c ? "ok" : "FAIL", w);
    if (!c) ++failures;
}

struct PlanFixture {
    std::unique_ptr<eaw::Simulation> sim;
    int rebelId = 0;
    int empireId = 0;
    int forceId = 0;
    int rebelPlanet = 0;
    int empirePlanet = 0;

    PlanFixture() {
        sim = std::make_unique<eaw::Simulation>(4);
        eaw::Player& rebel = sim->sim().addPlayer("Rebel Alliance", "REBEL");
        eaw::Player& empire = sim->sim().addPlayer("Galactic Empire", "EMPIRE");
        rebel.human = true;
        rebelId = rebel.id;
        empireId = empire.id;
        eaw::ObjectType fighter;
        fighter.name = "TIE_Fighter";
        fighter.properties = {"Unit"};
        fighter.damage = 0.01;
        fighter.attackRate = 1.0;
        fighter.maxRange = 300;
        fighter.buildCost = 100.0;
        sim->sim().addType(std::move(fighter));
        // Two planets far apart.
        rebelPlanet = sim->sim().addPlanet("Rebel_Home", "REBEL", {0, 0, 0});
        empirePlanet = sim->sim().addPlanet("Empire_Home", "EMPIRE", {500, 0, 0});
        // Empire (AI) attack force at its home.
        forceId = sim->sim().addTaskForce(empireId, "AttackPlan");
        sim->sim().taskForce(forceId)->planetId = empirePlanet;
        // A rebel defender at the rebel home.
        sim->sim().spawnUnit("TIE_Fighter", rebelId, {10, 0, 0});
        // AI config: build fighters, plan against the rebel home.
        sim->setAiBuildTypes({"TIE_Fighter"});
        sim->setAiPlanTarget("Rebel_Home");
        sim->sim().giveMoney(empireId, 5000.0);
    }
};

void testAssembleThenMove() {
    PlanFixture fx;
    // Stage 0: assemble until the roster hits the target (8 fighters).
    for (int i = 0; i < 300; ++i) {
        fx.sim->tick(1.0 / 30.0);
        if (fx.sim->sim().taskForce(fx.forceId)->stage >= 1) break;
    }
    const eaw::TaskForce* f = fx.sim->sim().taskForce(fx.forceId);
    check(f->stage == 1, "force advanced from assemble to move");
    check(f->units.size() >= 8, "force assembled to target size");
}

void testMoveThenAttackThenDone() {
    PlanFixture fx;
    // Run long enough for the full plan: assemble, transit 500 units
    // (25s at speed 20), arrive, engage the rebel defender, finish.
    for (int i = 0; i < 1800; ++i) {
        fx.sim->tick(1.0 / 30.0);
        if (fx.sim->sim().taskForce(fx.forceId)->planResult) break;
    }
    const eaw::TaskForce* f = fx.sim->sim().taskForce(fx.forceId);
    check(f->planResult, "plan completed");
    check(fx.sim->completedPlans() == 1, "plan completion counted");
    check(f->planetId == fx.rebelPlanet, "force ended at the target planet");
}

void testStageSequence() {
    PlanFixture fx;
    // Track the stages visited.
    int maxStage = 0;
    bool sawMove = false, sawAttack = false;
    for (int i = 0; i < 1800; ++i) {
        fx.sim->tick(1.0 / 30.0);
        const eaw::TaskForce* f = fx.sim->sim().taskForce(fx.forceId);
        if (f->stage > maxStage) maxStage = f->stage;
        if (f->stage == 1) sawMove = true;
        if (f->stage == 2) sawAttack = true;
        if (f->planResult) break;
    }
    check(sawMove, "plan passed through the move stage");
    check(sawAttack, "plan passed through the attack stage");
    check(maxStage >= 3, "plan reached the done stage");
}

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    testAssembleThenMove();
    testMoveThenAttackThenDone();
    testStageSequence();
    if (failures == 0) { std::printf("ALL TESTS PASSED\n"); return 0; }
    std::printf("%d FAILURES\n", failures);
    return 1;
}
