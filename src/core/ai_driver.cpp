#include "core/ai_driver.h"

#include <cmath>

namespace eaw {

namespace {

// The default attack equation when the caller doesn't supply one: prefer
// weak targets that are close and threatening to us. Matches the shape of
// the game's offensive equations.
const char* kDefaultAttackEquation = R"(
<Equations>
	<Attack_Target>
		10.0 * (0.3 > Variable_Target.Health)
		+ 5.0 * (1000 > Variable_Target.DistanceToNearestFriendly)
		+ Variable_Target.EnemyForce
	</Attack_Target>
</Equations>
)";

} // namespace

void AiDriver::runStep(SimState& sim, int playerId, double gameAge,
                       const std::string& equation) {
    // Lazily parse the default equation (cheap, one-time).
    static const PerceptionSystem* defaultPer = [] {
        static PerceptionSystem per;
        per.loadEquations(kDefaultAttackEquation);
        return &per;
    }();
    const PerceptionSystem& per = equation.empty()
                                      ? *defaultPer
                                      : targeting_.perceptions();

    for (const TaskForce* f : sim.forcesOfPlayer(playerId)) {
        if (f->planResult) continue; // plan finished — stop acting
        if (f->units.empty()) continue;
        TargetChoice t = targeting_.findTarget(sim, *f, equation.empty()
                                                            ? "Attack_Target"
                                                            : equation,
                                               gameAge);
        if (!t.found) continue;
        // Order the force: attack the chosen target. Only issue the order
        // if it changed (avoids re-issuing every tick).
        for (int uid : f->units) {
            GameObject* o = sim.object(uid);
            if (o && o->alive) o->attackTargetId = t.objectId;
        }
    }
}

} // namespace eaw
