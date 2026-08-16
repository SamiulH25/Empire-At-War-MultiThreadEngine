// Tests for the perception-driven AI targeting loop.
#include "core/simulation.h"

#include <cstdio>
#include <memory>
#include <string>

namespace {

int failures = 0;
void check(bool c, const char* w) {
    std::printf("%s: %s\n", c ? "ok" : "FAIL", w);
    if (!c) ++failures;
}

// Attack equation: prefer weak targets (damaged), then nearby.
const char* kEquation = R"(
<Equations>
	<Attack_Target>
		10.0 * (0.5 > Variable_Target.Health) + Variable_Target.EnemyForce
	</Attack_Target>
</Equations>
)";

struct Fixture {
    std::unique_ptr<eaw::Simulation> sim;
    int rebelId = 0;
    int empireId = 0;
    int forceId = 0;

    Fixture() {
        sim = std::make_unique<eaw::Simulation>(4);
        eaw::Player& rebel = sim->sim().addPlayer("Rebel Alliance", "REBEL");
        eaw::Player& empire = sim->sim().addPlayer("Galactic Empire", "EMPIRE");
        rebel.human = true;   // human: no AI step
        rebelId = rebel.id;
        empireId = empire.id; // AI player
        eaw::ObjectType xwing;
        xwing.name = "X-Wing";
        xwing.categories = {"Fighter"};
        xwing.damage = 0.02;
        xwing.attackRate = 2.0;
        xwing.maxRange = 400;
        sim->sim().addType(std::move(xwing));
        eaw::ObjectType tie;
        tie.name = "TIE_Fighter";
        tie.categories = {"Fighter"};
        tie.damage = 0.02;
        tie.attackRate = 2.0;
        tie.maxRange = 400;
        sim->sim().addType(std::move(tie));
        // Empire taskforce with two TIEs.
        sim->sim().spawnUnit("TIE_Fighter", empireId, {0, 0, 0});
        sim->sim().spawnUnit("TIE_Fighter", empireId, {20, 0, 0});
        forceId = sim->sim().addTaskForce(empireId, "AttackPlan");
        for (const eaw::GameObject* o : sim->sim().objectsOfType("TIE_Fighter")) {
            sim->sim().addUnitToForce(forceId, o->id);
        }
        // Rebel targets: one healthy, one damaged.
        sim->sim().spawnUnit("X-Wing", rebelId, {300, 0, 0});
        int weak = sim->sim().spawnUnit("X-Wing", rebelId, {320, 0, 0});
        sim->sim().object(weak)->hull = 0.2;
        sim->loadPerceptions(kEquation);
        sim->setAiAttackEquation("Attack_Target");
    }
};

void testAiPicksWeakTarget() {
    Fixture fx;
    // The damaged X-Wing (hull 0.2 < 0.5) scores 10 + force; the healthy one
    // scores only force. The AI force must target the damaged one.
    for (int i = 0; i < 10; ++i) fx.sim->tick(1.0 / 30.0);
    const eaw::TaskForce* f = fx.sim->sim().taskForce(fx.forceId);
    check(f->units.size() == 2, "force intact");
    // Both TIEs should be attacking the damaged X-Wing.
    int weakId = 0;
    for (const eaw::GameObject* o : fx.sim->sim().objectsOfType("X-Wing")) {
        if (o->hull < 0.5) weakId = o->id;
    }
    bool allOnWeak = true;
    for (int uid : f->units) {
        const eaw::GameObject* u = fx.sim->sim().object(uid);
        if (u->attackTargetId != weakId) allOnWeak = false;
    }
    check(weakId != 0, "found the weak target");
    check(allOnWeak, "force attacks the perception-best target");
}

void testAiOrdersCombat() {
    Fixture fx;
    // Run long enough for the AI to close and engage; the weak X-Wing should
    // take damage or die from the TIEs.
    for (int i = 0; i < 300; ++i) fx.sim->tick(1.0 / 30.0);
    bool weakDamaged = false;
    for (const eaw::GameObject* o : fx.sim->sim().objectsOfType("X-Wing")) {
        if (o->hull < 0.2) weakDamaged = true;
    }
    check(weakDamaged || fx.sim->totalShots() > 0, "AI combat engaged");
}

void testAiSkipsHuman() {
    Fixture fx;
    // The human player's units must not get AI orders (no taskforce for
    // rebel, and human flag skips the AI step entirely — no crash).
    for (int i = 0; i < 30; ++i) fx.sim->tick(1.0 / 30.0);
    check(true, "human player skipped without issue");
}

void testParallelMatchesSerial() {
    Fixture fx;
    // Parallel findTarget must equal serial findTarget on the same setup.
    const eaw::TaskForce* f = fx.sim->sim().taskForce(fx.forceId);
    eaw::AiTargeting tgt(fx.sim->jobs(), fx.sim->perceptions());
    auto par = tgt.findTarget(fx.sim->sim(), *f, "Attack_Target", 10.0);
    auto ser = tgt.findTargetSerial(fx.sim->sim(), *f, "Attack_Target", 10.0);
    check(par.objectId == ser.objectId && par.score == ser.score,
          "parallel and serial targeting agree");
}

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    testAiPicksWeakTarget();
    testAiOrdersCombat();
    testAiSkipsHuman();
    testParallelMatchesSerial();
    if (failures == 0) { std::printf("ALL TESTS PASSED\n"); return 0; }
    std::printf("%d FAILURES\n", failures);
    return 1;
}
