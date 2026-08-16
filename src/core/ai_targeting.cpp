#include "core/ai_targeting.h"

#include <algorithm>
#include <limits>

namespace eaw {

namespace {

// The object that stands in for the force in Variable_Self queries: its
// first living unit, else null.
const GameObject* forceSelf(const SimState& sim, const TaskForce& force) {
    for (int id : force.units) {
        const GameObject* o = sim.object(id);
        if (o && o->alive) return o;
    }
    return nullptr;
}

// Enumerates living enemy candidates in stable (object-id) order.
std::vector<const GameObject*> candidates(const SimState& sim,
                                          const TaskForce& force) {
    std::vector<const GameObject*> out;
    for (const GameObject* o : sim.allObjects()) {
        if (!o->alive) continue;
        if (sim.isAlly(force.playerId, o->playerId)) continue;
        out.push_back(o);
    }
    std::sort(out.begin(), out.end(),
              [](const GameObject* a, const GameObject* b) { return a->id < b->id; });
    return out;
}

TargetChoice reduceBest(const SimState& sim, const TaskForce& force,
                        const PerceptionSystem& per, const std::string& eq,
                        double gameAge,
                        const std::vector<const GameObject*>& cands) {
    TargetChoice best;
    best.score = -std::numeric_limits<double>::max();
    const GameObject* self = forceSelf(sim, force);
    for (const GameObject* c : cands) {
        PerceptionContext ctx;
        ctx.sim = &sim;
        ctx.self = self;
        ctx.target = c;
        ctx.gameAge = gameAge;
        double score = per.evaluate(eq, ctx);
        // Strictly greater: ties keep the lower id (candidates are id-sorted).
        if (score > best.score) {
            best.score = score;
            best.objectId = c->id;
            best.found = true;
        }
    }
    return best;
}

} // namespace

TargetChoice AiTargeting::findTarget(const SimState& sim, const TaskForce& force,
                                     const std::string& equation,
                                     double gameAge) const {
    auto cands = candidates(sim, force);
    if (cands.empty()) return {};

    // Parallel evaluation: score each candidate on a worker (pure reads).
    std::vector<double> scores(cands.size());
    const GameObject* self = forceSelf(sim, force);
    jobs_.parallel_for(static_cast<int64_t>(cands.size()), [&](int64_t a, int64_t b) {
        for (int64_t i = a; i < b; ++i) {
            PerceptionContext ctx;
            ctx.sim = &sim;
            ctx.self = self;
            ctx.target = cands[i];
            ctx.gameAge = gameAge;
            scores[i] = perceptions_.evaluate(equation, ctx);
        }
    });

    // Serial max-reduction (deterministic tie-break by id order).
    TargetChoice best;
    best.score = -std::numeric_limits<double>::max();
    for (size_t i = 0; i < cands.size(); ++i) {
        if (scores[i] > best.score) {
            best.score = scores[i];
            best.objectId = cands[i]->id;
            best.found = true;
        }
    }
    return best;
}

TargetChoice AiTargeting::findTargetSerial(const SimState& sim,
                                           const TaskForce& force,
                                           const std::string& equation,
                                           double gameAge) const {
    auto cands = candidates(sim, force);
    return reduceBest(sim, force, perceptions_, equation, gameAge, cands);
}

} // namespace eaw
