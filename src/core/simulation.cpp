#include "core/simulation.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace eaw {

namespace {

// Per-object movement integration. Reads only the object's own state and
// writes only its own position — safe to run on any worker for disjoint
// object sets (design doc 06: read-shared, write-partitioned). Movement is
// full 3D: waypoint paths carry altitude, and the direct-move case
// interpolates all three axes.
void integrateObject(const std::unordered_map<int, Vec3>& positions,
                     eaw::SimState& sim, GameObject& o, double dt) {
    if (!o.alive || !o.hasMoveTarget) return;
    // RTS behavior: if we have a live enemy attack target in weapon range,
    // hold position and fire instead of moving (kiting is a later upgrade).
    // Reads the target's position from the tick snapshot (never the live
    // position another worker may be writing).
    if (o.attackTargetId != 0) {
        auto it = positions.find(o.attackTargetId);
        const GameObject* t = sim.object(o.attackTargetId);
        const ObjectType* tt = t ? sim.type(t->typeName) : nullptr;
        if (t && t->alive && !sim.isAlly(o.playerId, t->playerId) &&
            it != positions.end() &&
            o.position.distanceTo(it->second) <= (tt ? tt->maxRange : 0.0) + 1e-9) {
            return; // in range: hold and shoot
        }
    }
    // If the unit has a computed path, walk it waypoint by waypoint.
    if (o.pathIndex < o.path.size()) {
        Vec3 target = o.path[o.pathIndex];
        Vec3 d{target.x - o.position.x,
               target.y - o.position.y,
               target.z - o.position.z};
        double dist = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        double step = o.moveSpeed * dt;
        if (dist <= step) {
            o.position = target;
            ++o.pathIndex;
            if (o.pathIndex >= o.path.size()) {
                // Path done: arrive at the final move target (all axes).
                o.path.clear();
                o.pathIndex = 0;
                o.position = o.moveTarget;
                o.hasMoveTarget = false;
            }
        } else {
            double k = step / dist;
            o.position.x += d.x * k;
            o.position.y += d.y * k;
            o.position.z += d.z * k;
        }
        return;
    }
    // No path — direct move (full 3D).
    Vec3 d{o.moveTarget.x - o.position.x,
           o.moveTarget.y - o.position.y,
           o.moveTarget.z - o.position.z};
    double dist = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    double step = o.moveSpeed * dt;
    if (dist <= step) {
        o.position = o.moveTarget;
        o.hasMoveTarget = false;
    } else {
        double k = step / dist;
        o.position.x += d.x * k;
        o.position.y += d.y * k;
        o.position.z += d.z * k;
    }
}

// One shooter's firing decision for a tick (phase 1 output, phase 2 input).
struct ShotDecision {
    int targetId = 0;    // 0 = no shot
    double damage = 0;   // damage to apply to the target
};

// Phase 1: decide shots. Reads the shooter's own state + the target's
// position (from the tick snapshot — read-only) and player; writes only
// decisions[i] (the shooter's own slot) and the shooter's own cooldown.
void decideShot(const SimState& sim, const std::unordered_map<int, Vec3>& positions,
                const std::vector<int>& ids, int64_t i,
                const std::vector<GameObject*>& objs, double dt,
                ShotDecision& out) {
    GameObject* o = objs[i];
    if (!o || !o->alive) { out = ShotDecision{}; return; }
    const ObjectType* t = sim.type(o->typeName);
    o->attackCooldown = std::max(0.0, o->attackCooldown - dt);
    if (!t || t->damage <= 0 || o->attackCooldown > 0.0) return;
    // Find the attack target (may be set by script or auto-acquired).
    const GameObject* target = o->attackTargetId ? sim.object(o->attackTargetId) : nullptr;
    if (!target || !target->alive || sim.isAlly(o->playerId, target->playerId)) {
        // Auto-acquire: nearest living enemy by snapshot position.
        target = nullptr;
        double best = std::numeric_limits<double>::max();
        for (const auto& [eid, epos] : positions) {
            if (eid == o->id) continue;
            const GameObject* e = sim.object(eid);
            if (!e || !e->alive) continue;
            if (sim.isAlly(o->playerId, e->playerId)) continue;
            double d = o->position.distanceTo(epos);
            if (d < best) { best = d; target = e; }
        }
        if (target) o->attackTargetId = target->id;
    }
    if (!target || !target->alive) return;
    auto it = positions.find(target->id);
    if (it == positions.end()) return;
    double dist = o->position.distanceTo(it->second);
    if (dist > t->maxRange) return;
    // Fire.
    out.targetId = target->id;
    out.damage = t->damage;
    o->attackCooldown = 1.0 / std::max(0.001, t->attackRate);
}

// Phase 2: apply accumulated damage. Each target reads the decision slots
// (read-only) and writes only its own hull/shield/state.
void applyDamage(const SimState& sim, GameObject* target,
                 const std::vector<ShotDecision>& decisions,
                 const std::vector<int>& shooterIds) {
    if (!target || !target->alive) return;
    double dmg = 0;
    for (size_t k = 0; k < decisions.size(); ++k) {
        if (decisions[k].targetId == target->id) dmg += decisions[k].damage;
    }
    if (dmg <= 0) return;
    if (!target->invulnerable) {
        // Shield absorbs first; excess spills to hull.
        double shieldAbsorb = std::min(target->shield, dmg);
        target->shield -= shieldAbsorb;
        double hullDmg = dmg - shieldAbsorb;
        target->hull = std::max(0.0, target->hull - hullDmg);
        target->wasDamagedThisTick = true;
        if (target->hull == 0.0) target->alive = false;
    }
}

} // namespace

Simulation::Simulation(unsigned workerThreads)
    : scripts_(files_), jobs_(workerThreads),
      pathfinding_(grid_, jobs_, pathOptions_) {
}

void Simulation::configure(const GameConstants& gc) {
    if (gc.spacePathfindMaxExpansions > 0) {
        pathOptions_.expansionsPerTick = gc.spacePathfindMaxExpansions;
    }
    if (gc.spacePathFailureMaxExpansionsCoefficient > 0) {
        // The game scales the failure cap by a coefficient; we use it as a
        // multiple of the per-tick budget for the total cap.
        pathOptions_.maxTotalExpansions = static_cast<int>(
            gc.spacePathFailureMaxExpansionsCoefficient *
            pathOptions_.expansionsPerTick);
    }
    pathfinding_.setOptions(pathOptions_);
}

void Simulation::loadPerceptions(const std::string& xmlText) {
    perceptions_.loadEquations(xmlText);
}

void Simulation::tick(double dt) {
    time_ += dt;
    scripts_.pump(dt);
    sim().pruneDeadUnits();
    // AI: taskforces pick targets via perception (parallel evaluation).
    if (!ai_) ai_ = std::make_unique<AiDriver>(jobs_, perceptions_);
    for (const auto& p : sim().allPlayers()) {
        if (!p.human) ai_->runStep(sim(), p.id, time_, aiEquation_);
    }
    snapshotPositions();
    stepPathfinding();
    updateObjects(dt);
    runCombat(dt);
}

void Simulation::snapshotPositions() {
    positions_.clear();
    for (const GameObject* o : sim().allObjects()) {
        positions_[o->id] = o->position;
    }
}

void Simulation::updateObjects(double dt) {
    // Work on ids (stable handles) and resolve mutable objects per slice.
    std::vector<int> ids;
    for (const GameObject* o : sim().allObjects()) ids.push_back(o->id);
    int64_t n = static_cast<int64_t>(ids.size());
    if (n == 0) { ++updateTicks_; return; }
    // Partition [0, n) into contiguous ranges; each worker integrates only
    // its slice's objects (deterministic: no cross-object writes).
    jobs_.parallel_for(n, [&](int64_t start, int64_t end) {
        for (int64_t i = start; i < end; ++i) {
            GameObject* o = sim().object(ids[i]);
            if (o) integrateObject(positions_, sim(), *o, dt);
        }
    });
    ++updateTicks_;
}

void Simulation::stepPathfinding() {
    // Units with a move target whose direct line is blocked request a path.
    // (Simple policy: request once per unit; searches run to completion.)
    std::vector<int> toRequest;
    for (const GameObject* o : sim().allObjects()) {
        if (!o->alive || !o->hasMoveTarget) continue;
        if (!o->path.empty()) continue;        // already following a path
        if (o->pathSearchId != 0) continue;    // search already in flight
        // Only route inside the grid; moves that leave it beeline (the grid
        // is bounded but the world is not).
        if (grid_.inBounds(grid_.cellOf(o->position.x), grid_.cellOf(o->position.y),
                           grid_.cellOf(o->position.z)) &&
            grid_.inBounds(grid_.cellOf(o->moveTarget.x), grid_.cellOf(o->moveTarget.y),
                           grid_.cellOf(o->moveTarget.z)) &&
            grid_.lineBlocked(o->position.x, o->position.y, o->position.z,
                              o->moveTarget.x, o->moveTarget.y, o->moveTarget.z)) {
            toRequest.push_back(o->id);
        }
    }
    for (int id : toRequest) {
        GameObject* o = sim().object(id);
        if (!o) continue;
        int sid = pathfinding_.request(o->position, o->moveTarget);
        // Store the search id on the object so completions can be matched.
        o->pathSearchId = sid;
    }
    // Step all searches; on completion, hand the waypoints to the unit.
    pathfinding_.tick([this](int searchId, std::vector<Vec3> wps) {
        for (const GameObject* o : sim().allObjects()) {
            GameObject* g = sim().object(o->id);
            if (g && g->pathSearchId == searchId) {
                if (!wps.empty()) {
                    g->path = wps;
                    g->pathIndex = 1; // skip the start waypoint
                } else {
                    g->hasMoveTarget = false;
                }
                g->pathSearchId = 0;
                break;
            }
        }
    });
    // Failed searches (no path found) release their unit so it doesn't
    // beeline through obstacles forever: cancel the move order.
    for (const GameObject* o : sim().allObjects()) {
        GameObject* g = sim().object(o->id);
        if (!g || g->pathSearchId == 0) continue;
        if (pathfinding_.isFailed(g->pathSearchId)) {
            g->hasMoveTarget = false;
            g->pathSearchId = 0;
        }
    }
}

void Simulation::runCombat(double dt) {
    std::vector<int> ids;
    for (const GameObject* o : sim().allObjects()) ids.push_back(o->id);
    int64_t n = static_cast<int64_t>(ids.size());
    if (n == 0) return;

    // Phase 1: parallel fire decisions (per-shooter slot writes).
    std::vector<ShotDecision> decisions(static_cast<size_t>(n));
    std::vector<GameObject*> objs(static_cast<size_t>(n));
    for (int64_t i = 0; i < n; ++i) objs[i] = sim().object(ids[i]);
    jobs_.parallel_for(n, [&](int64_t start, int64_t end) {
        for (int64_t i = start; i < end; ++i) {
            decideShot(sim(), positions_, ids, i, objs, dt, decisions[i]);
        }
    });
    for (const ShotDecision& d : decisions) {
        if (d.targetId != 0) ++totalShots_;
    }

    // Phase 2: parallel damage application (per-target slot writes).
    jobs_.parallel_for(n, [&](int64_t start, int64_t end) {
        for (int64_t i = start; i < end; ++i) {
            applyDamage(sim(), objs[i], decisions, ids);
        }
    });
}

} // namespace eaw
