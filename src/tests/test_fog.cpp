// Tests for fog of war: visibility, TimeLastSeen, reveal bindings.
#include "core/simulation.h"

#include <cstdio>
#include <memory>

namespace {

int failures = 0;
void check(bool c, const char* w) {
    std::printf("%s: %s\n", c ? "ok" : "FAIL", w);
    if (!c) ++failures;
}

struct FogFixture {
    std::unique_ptr<eaw::Simulation> sim;
    int rebelId = 0;
    int empireId = 0;
    int scoutId = 0;
    int farId = 0;

    FogFixture() {
        sim = std::make_unique<eaw::Simulation>(4);
        eaw::Player& rebel = sim->sim().addPlayer("Rebel Alliance", "REBEL");
        eaw::Player& empire = sim->sim().addPlayer("Galactic Empire", "EMPIRE");
        rebelId = rebel.id;
        empireId = empire.id;
        eaw::ObjectType scout;
        scout.name = "Scout";
        scout.properties = {"Unit"};
        scout.maxRange = 100;
        sim->sim().addType(std::move(scout));
        // Rebel scout at origin; empire units at 0 (in sight) and 1000 (far).
        scoutId = sim->sim().spawnUnit("Scout", rebelId, {0, 0, 0});
        sim->sim().spawnUnit("Scout", empireId, {0, 0, 0});   // within 300
        farId = sim->sim().spawnUnit("Scout", empireId, {1000, 0, 0});
        // The rebel unit sees the empire unit at 0 but not the one at 1000.
        sim->tick(1.0 / 30.0);
    }
};

void testVisibleAndHidden() {
    FogFixture fx;
    // The empire unit at 0 is within the rebel scout's 300 sight.
    const eaw::Player* rebel = fx.sim->sim().player(fx.rebelId);
    check(rebel->lastSeen.count(fx.farId) == 0 ||
          rebel->lastSeen.at(fx.farId) < 0.0,
          "far unit never seen");
    // The near empire unit: find it by position (within sight range).
    for (const eaw::GameObject* o : fx.sim->sim().allObjects()) {
        if (o->playerId == fx.empireId && o->position.x < 300.0) {
            check(rebel->lastSeen.count(o->id) && rebel->lastSeen.at(o->id) >= 0.0,
                  "near unit seen");
        }
    }
}

void testTimeSinceSeenAges() {
    FogFixture fx;
    double now = fx.sim->time();
    // The far unit was never seen -> huge.
    check(fx.sim->sim().timeSinceSeen(fx.rebelId, fx.farId, now) > 1000.0,
          "never-seen unit has huge TimeLastSeen");
    // Move the far unit into sight, then out: TimeLastSeen grows.
    fx.sim->sim().object(fx.farId)->position = {0, 0, 0};
    fx.sim->tick(1.0 / 30.0);
    fx.sim->sim().object(fx.farId)->position = {1000, 0, 0};
    fx.sim->tick(1.0);
    double t = fx.sim->sim().timeSinceSeen(fx.rebelId, fx.farId, fx.sim->time());
    check(t > 0.9 && t < 1.2, "TimeLastSeen grows after leaving sight");
}

void testRevealArea() {
    FogFixture fx;
    // Reveal the far unit's area for the rebel player; it becomes seen.
    fx.sim->scripts().runScript(
        "p = Find_Player('REBEL')\n"
        "pos = Create_Position(1000, 0, 0)\n"
        "FogOfWar.Reveal(p, pos, 300)\n");
    fx.sim->tick(1.0 / 30.0);
    const eaw::Player* rebel = fx.sim->sim().player(fx.rebelId);
    check(rebel->lastSeen.count(fx.farId) && rebel->lastSeen.at(fx.farId) >= 0.0,
          "revealed area makes far unit visible");
}

void testRevealAll() {
    FogFixture fx;
    fx.sim->scripts().runScript(
        "p = Find_Player('REBEL')\n"
        "FogOfWar.Reveal_All(p)\n");
    fx.sim->tick(1.0 / 30.0);
    const eaw::Player* rebel = fx.sim->sim().player(fx.rebelId);
    check(rebel->revealAll, "Reveal_All flag set");
    check(rebel->lastSeen.count(fx.farId) && rebel->lastSeen.at(fx.farId) >= 0.0,
          "Reveal_All makes everything visible");
}

void testPerceptionUsesFog() {
    FogFixture fx;
    // Equation: reward recently-seen targets (TimeLastSeen < 10).
    const char* eq = R"(
<Equations>
	<Recently_Seen>
		10.0 > Variable_Target.TimeLastSeen
	</Recently_Seen>
</Equations>
)";
    fx.sim->loadPerceptions(eq);
    eaw::PerceptionContext ctx;
    ctx.sim = &fx.sim->sim();
    ctx.self = fx.sim->sim().object(fx.scoutId);
    ctx.gameAge = fx.sim->time();
    // Far unit: never seen -> huge TimeLastSeen -> 0.
    ctx.target = fx.sim->sim().object(fx.farId);
    check(fx.sim->perceptions().evaluate("Recently_Seen", ctx) == 0.0,
          "unseen target scores 0");
    // Reveal all -> far unit seen recently -> 1.
    fx.sim->scripts().runScript(
        "p = Find_Player('REBEL')\n"
        "FogOfWar.Reveal_All(p)\n");
    fx.sim->tick(1.0 / 30.0);
    ctx.gameAge = fx.sim->time();
    check(fx.sim->perceptions().evaluate("Recently_Seen", ctx) == 1.0,
          "revealed target scores 1");
}

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    testVisibleAndHidden();
    testTimeSinceSeenAges();
    testRevealArea();
    testRevealAll();
    testPerceptionUsesFog();
    if (failures == 0) { std::printf("ALL TESTS PASSED\n"); return 0; }
    std::printf("%d FAILURES\n", failures);
    return 1;
}
