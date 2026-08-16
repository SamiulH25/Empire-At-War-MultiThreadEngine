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

void AiDriver::produce(SimState& sim, int playerId,
                       const std::vector<std::string>& buildTypes) {
    if (buildTypes.empty()) return;
    const Player* p = sim.player(playerId);
    if (!p) return;
    // Spend at most ~20% of current credits per step so production paces
    // with income instead of dumping everything at once.
    double budget = p->credits * 0.2;
    for (const TaskForce* f : sim.forcesOfPlayer(playerId)) {
        if (f->planResult) continue; // finished plans don't rebuild
        // Count the force's current roster.
        int fighters = 0, heavies = 0;
        for (int uid : f->units) {
            const GameObject* o = sim.object(uid);
            if (!o || !o->alive) continue;
            const ObjectType* t = sim.type(o->typeName);
            if (!t) continue;
            bool isHeavy = t->buildCost >= 500.0;
            if (isHeavy) ++heavies; else ++fighters;
        }
        if (fighters >= targetFighters() && heavies >= targetHeavies()) continue;
        // Try to build the missing class, preferring the first affordable
        // type in the preference list that fits the gap.
        bool needHeavy = heavies < targetHeavies();
        for (const std::string& tn : buildTypes) {
            const ObjectType* t = sim.type(tn);
            if (!t || !sim.canBuild(playerId, tn)) continue;
            bool isHeavy = t->buildCost >= 500.0;
            if (isHeavy != needHeavy) continue;
            if (t->buildCost > budget) continue;
            // Spawn near the force's first unit.
            Vec3 pos{0, 0, 0};
            for (int uid : f->units) {
                const GameObject* o = sim.object(uid);
                if (o) { pos = o->position; break; }
            }
            int nid = sim.buildUnit(playerId, tn, pos);
            if (nid != 0) {
                sim.addUnitToForce(f->id, nid);
                budget -= t->buildCost;
                break; // one unit per step per force
            }
        }
    }
}

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
