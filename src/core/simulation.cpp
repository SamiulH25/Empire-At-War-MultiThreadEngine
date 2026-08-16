#include "core/simulation.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace eaw {

namespace {

// Per-object movement integration. Reads only the object's own state and
// writes only its own position — safe to run on any worker for disjoint
// object sets (design doc 06: read-shared, write-partitioned).
void integrateObject(GameObject& o, double dt) {
    if (!o.alive || !o.hasMoveTarget) return;
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
// position/player (read-only); writes only decisions[i] (the shooter's own
// slot) and the shooter's own cooldown.
void decideShot(const SimState& sim, const std::vector<int>& ids, int64_t i,
                const std::vector<GameObject*>& objs, double dt,
                ShotDecision& out) {
    GameObject* o = objs[i];
    if (!o || !o->alive) { out = ShotDecision{}; return; }
    const ObjectType* t = sim.type(o->typeName);
    o->attackCooldown = std::max(0.0, o->attackCooldown - dt);
    if (!t || t->damage <= 0 || o->attackCooldown > 0.0) return;
    // Find the attack target (may be set by script or auto-acquired).
    const GameObject* target = o->attackTargetId ? sim.object(o->attackTargetId) : nullptr;
    if (!target || !target->alive || target->playerId == o->playerId) return;
    double dist = o->position.distanceTo(target->position);
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
    : scripts_(files_), jobs_(workerThreads) {
}

void Simulation::tick(double dt) {
    time_ += dt;
    scripts_.pump(dt);
    updateObjects(dt);
    runCombat(dt);
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
            if (o) integrateObject(*o, dt);
        }
    });
    ++updateTicks_;
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
            decideShot(sim(), ids, i, objs, dt, decisions[i]);
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
