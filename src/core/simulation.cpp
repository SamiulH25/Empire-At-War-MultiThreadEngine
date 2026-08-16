#include "core/simulation.h"

#include <cmath>

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

} // namespace

Simulation::Simulation(unsigned workerThreads)
    : scripts_(files_), jobs_(workerThreads) {
}

void Simulation::tick(double dt) {
    time_ += dt;
    scripts_.pump(dt);
    updateObjects(dt);
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

} // namespace eaw
