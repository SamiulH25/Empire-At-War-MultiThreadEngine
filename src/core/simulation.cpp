#include "core/simulation.h"

namespace eaw {

Simulation::Simulation()
    : scripts_(files_) {
}

void Simulation::tick(double dt) {
    time_ += dt;
    scripts_.pump(dt);
}

} // namespace eaw
