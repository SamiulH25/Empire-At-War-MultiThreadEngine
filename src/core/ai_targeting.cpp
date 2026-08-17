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

// Targeting-priority bonus for a candidate: the index (0-based) of the
// candidate's category in `prio` (the self object's priority table), or
// prio.size() when no category matches. Smaller = higher priority; the
// score tie-break below picks the lowest. Empty table = all equal.
int priorityRank(const std::vector<std::string>& prio,
                 const SimState& sim, const GameObject* cand) {
    if (prio.empty()) return 0;
    const ObjectType* t = sim.type(cand->typeName);
    if (!t) return static_cast<int>(prio.size());
    for (size_t i = 0; i < prio.size(); ++i) {
        for (const std::string& c : t->categories) {
            if (c == prio[i]) return static_cast<int>(i);
        }
    }
    return static_cast<int>(prio.size());
}

// The priority table that applies to this force's self object (space vs
// land, matching Set_Targeting_Priorities vs Set_Land_AI_Targeting_Priorities).
const std::vector<std::string>& forcePriorityTable(const GameObject* self) {
    static const std::vector<std::string> kEmpty;
    if (!self) return kEmpty;
    if (!self->targetingPriorities.empty()) return self->targetingPriorities;
    return self->landTargetingPriorities;
}

TargetChoice reduceBest(const SimState& sim, const TaskForce& force,
                        const PerceptionSystem& per, const std::string& eq,
                        double gameAge,
                        const std::vector<const GameObject*>& cands) {
    TargetChoice best;
    best.score = -std::numeric_limits<double>::max();
    const GameObject* self = forceSelf(sim, force);
    const std::vector<std::string>& prio = forcePriorityTable(self);
    for (const GameObject* c : cands) {
        PerceptionContext ctx;
        ctx.sim = &sim;
        ctx.self = self;
        ctx.target = c;
        ctx.gameAge = gameAge;
        double score = per.evaluate(eq, ctx);
        // Targeting priorities: the highest-priority category wins ties.
        // The rank is small (table size), so it cannot mask real score
        // differences; `prioRank` is constant when no table is set.
        double rank = static_cast<double>(priorityRank(prio, sim, c));
        double adjusted = score - rank / 1e6;
        // Strictly greater: ties keep the lower id (candidates are id-sorted).
        if (adjusted > best.score) {
            best.score = adjusted;
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
    // The priority rank is a per-candidate constant (pure read of the self
    // object's table + the candidate's type), so it is computed here — no
    // shared writes.
    std::vector<double> scores(cands.size());
    std::vector<double> ranks(cands.size());
    const GameObject* self = forceSelf(sim, force);
    const std::vector<std::string>& prio = forcePriorityTable(self);
    for (size_t i = 0; i < cands.size(); ++i) {
        ranks[i] = static_cast<double>(priorityRank(prio, sim, cands[i]));
    }
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

    // Serial max-reduction (deterministic tie-break by id order, then by
    // targeting priority — same adjusted score as reduceBest).
    TargetChoice best;
    best.score = -std::numeric_limits<double>::max();
    for (size_t i = 0; i < cands.size(); ++i) {
        double adjusted = scores[i] - ranks[i] / 1e6;
        if (adjusted > best.score) {
            best.score = adjusted;
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
