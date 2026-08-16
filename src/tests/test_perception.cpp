// Tests for the perception equation system (AI scoring DSL).
#include "core/object_model.h"
#include "core/perception.h"
#include "core/simulation.h"

#include <cstdio>
#include <string>

namespace {

int failures = 0;
void check(bool c, const char* w) {
    std::printf("%s: %s\n", c ? "ok" : "FAIL", w);
    if (!c) ++failures;
}

const char* kEquations = R"(
<?xml version="1.0" ?>
<Equations>
	<Threat_Score>
		Variable_Target.FriendlyForce {Parameter_Category = "Capital"}
		+ 2.0 * Variable_Target.Health
	</Threat_Score>
	<Weak_Target>
		0.3 > Variable_Target.Health
	</Weak_Target>
	<Distance_Bonus>
		(1000 > Variable_Target.DistanceToNearestFriendly {Parameter_Category = "Fighter"})
	</Distance_Bonus>
	<Damage_Threat>
		3.0 * (1.0 - Variable_Target.Health > 0.8)
	</Damage_Threat>
	<Chained>
		Function_Weak_Target.Evaluate * 10
	</Chained>
	<Game_Age_Check>
		Game.Age > 5.0
	</Game_Age_Check>
</Equations>
)";

struct Fixture {
    eaw::Simulation sim;
    eaw::PerceptionSystem per;
    int rebelId = 0;
    int empireId = 0;
    int xwingId = 0;
    int isdId = 0;
    int tieId = 0;

    Fixture() {
        eaw::Player& rebel = sim.sim().addPlayer("Rebel Alliance", "REBEL");
        eaw::Player& empire = sim.sim().addPlayer("Galactic Empire", "EMPIRE");
        rebelId = rebel.id;
        empireId = empire.id;
        eaw::ObjectType xwing;
        xwing.name = "X-Wing";
        xwing.categories = {"Fighter"};
        xwing.damage = 0.1;
        sim.sim().addType(std::move(xwing));
        eaw::ObjectType isd;
        isd.name = "ISD";
        isd.categories = {"Capital"};
        isd.damage = 0.0;
        sim.sim().addType(std::move(isd));
        eaw::ObjectType tie;
        tie.name = "TIE_Fighter";
        tie.categories = {"Fighter"};
        tie.damage = 0.0;
        sim.sim().addType(std::move(tie));
        xwingId = sim.sim().spawnUnit("X-Wing", rebelId, {0, 0, 0});
        isdId = sim.sim().spawnUnit("ISD", rebelId, {500, 0, 0});
        tieId = sim.sim().spawnUnit("TIE_Fighter", empireId, {50, 0, 0});
        per.loadEquations(kEquations);
    }
};

void testLoadsEquations() {
    Fixture fx;
    auto names = fx.per.equationNames();
    check(names.size() == 6, "loads all 6 equations");
    check(names[0] == "Threat_Score", "first equation name");
}

void testArithmeticAndQuery() {
    Fixture fx;
    // Threat_Score = targetFriendlyCapital + 2*targetHealth. Target is the
    // TIE (empire): its friendly capitals are none (the ISD is rebel), so
    // score = 0 + 2*1 = 2.
    eaw::PerceptionContext ctx;
    ctx.sim = &fx.sim.sim();
    ctx.self = fx.sim.sim().object(fx.xwingId);
    ctx.target = fx.sim.sim().object(fx.tieId);
    ctx.gameAge = 10.0;
    double score = fx.per.evaluate("Threat_Score", ctx);
    check(score > 1.99 && score < 2.01, "Threat_Score = friendlyCapital(0) + 2*health(1)");
}

void testComparison() {
    Fixture fx;
    // Weak_Target: target health < 0.3. TIE at full health -> 0.
    eaw::PerceptionContext ctx;
    ctx.sim = &fx.sim.sim();
    ctx.self = fx.sim.sim().object(fx.xwingId);
    ctx.target = fx.sim.sim().object(fx.tieId);
    check(fx.per.evaluate("Weak_Target", ctx) == 0.0, "healthy target not weak");
    // Damage the TIE.
    fx.sim.sim().object(fx.tieId)->hull = 0.2;
    check(fx.per.evaluate("Weak_Target", ctx) == 1.0, "damaged target is weak");
}

void testDistanceQuery() {
    Fixture fx;
    // Distance_Bonus: 1000 > DistanceToNearestFriendly[Fighter]. The TIE's
    // nearest friendly fighter is itself (excluded) — none other, so 0.
    eaw::PerceptionContext ctx;
    ctx.sim = &fx.sim.sim();
    ctx.self = fx.sim.sim().object(fx.xwingId);
    ctx.target = fx.sim.sim().object(fx.tieId);
    // Add a second TIE far away and evaluate from the X-Wing's perspective:
    // nearest friendly fighter to the X-Wing (self=X-Wing) is... itself.
    // Instead evaluate with target = X-Wing: its nearest friendly fighter
    // within 1000 is itself excluded; the other X-Wing is at 0? Only one.
    // So DistanceToNearestFriendly for the X-Wing = none -> 0 (not < 1000).
    ctx.target = fx.sim.sim().object(fx.xwingId);
    check(fx.per.evaluate("Distance_Bonus", ctx) == 0.0, "no friendly fighter nearby");
    // Add a friendly fighter 100 away; the bonus should trigger.
    int x2 = fx.sim.sim().spawnUnit("X-Wing", fx.rebelId, {100, 0, 0});
    (void)x2;
    check(fx.per.evaluate("Distance_Bonus", ctx) == 1.0, "friendly fighter within 1000");
}

void testNestedComparison() {
    Fixture fx;
    // Damage_Threat: 3 * (1 - health > 0.8). TIE at full health:
    // (1-1 > 0.8) = 0 -> 0.
    eaw::PerceptionContext ctx;
    ctx.sim = &fx.sim.sim();
    ctx.self = fx.sim.sim().object(fx.xwingId);
    ctx.target = fx.sim.sim().object(fx.tieId);
    check(fx.per.evaluate("Damage_Threat", ctx) == 0.0, "full health gives no damage threat");
    fx.sim.sim().object(fx.tieId)->hull = 0.1; // 1-0.1=0.9 > 0.8 -> 3
    check(fx.per.evaluate("Damage_Threat", ctx) == 3.0, "low health gives damage threat");
}

void testChainedEquation() {
    Fixture fx;
    eaw::PerceptionContext ctx;
    ctx.sim = &fx.sim.sim();
    ctx.self = fx.sim.sim().object(fx.xwingId);
    ctx.target = fx.sim.sim().object(fx.tieId);
    check(fx.per.evaluate("Chained", ctx) == 0.0, "chain of non-weak target = 0");
    fx.sim.sim().object(fx.tieId)->hull = 0.2;
    check(fx.per.evaluate("Chained", ctx) == 10.0, "chain of weak target = 10");
}

void testGameAge() {
    Fixture fx;
    eaw::PerceptionContext ctx;
    ctx.sim = &fx.sim.sim();
    ctx.self = fx.sim.sim().object(fx.xwingId);
    ctx.target = fx.sim.sim().object(fx.tieId);
    ctx.gameAge = 3.0;
    check(fx.per.evaluate("Game_Age_Check", ctx) == 0.0, "young game fails age check");
    ctx.gameAge = 10.0;
    check(fx.per.evaluate("Game_Age_Check", ctx) == 1.0, "old game passes age check");
}

void testUnknownEquation() {
    Fixture fx;
    eaw::PerceptionContext ctx;
    ctx.sim = &fx.sim.sim();
    check(fx.per.evaluate("Nope", ctx) == 0.0, "unknown equation evaluates to 0");
}

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    testLoadsEquations();
    testArithmeticAndQuery();
    testComparison();
    testDistanceQuery();
    testNestedComparison();
    testChainedEquation();
    testGameAge();
    testUnknownEquation();
    if (failures == 0) { std::printf("ALL TESTS PASSED\n"); return 0; }
    std::printf("%d FAILURES\n", failures);
    return 1;
}
