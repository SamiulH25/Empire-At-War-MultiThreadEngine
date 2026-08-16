// AiDriver — the per-tick AI loop for taskforces.
//
// A minimal version of the game's goal system: each tick, every taskforce
// with an attack goal picks its best target via a perception equation
// (evaluated in parallel across the force's candidates) and orders the
// force to attack it. Forces whose plan result is set stop acting.
//
// This is the bridge between the perception system and the combat system:
// the AI actually plays — selects targets, issues collective orders.
#pragma once

#include "core/ai_targeting.h"
#include "core/job_system.h"
#include "core/object_model.h"
#include "core/perception.h"

#include <string>
#include <vector>

namespace eaw {

class AiDriver {
public:
    AiDriver(JobSystem& jobs, const PerceptionSystem& perceptions)
        : targeting_(jobs, perceptions) {}

    // Runs one AI step for every attack-goal taskforce of `playerId`.
    // `equation` is the perception used to score targets (default:
    // a simple threat equation defined internally if empty).
    void runStep(SimState& sim, int playerId, double gameAge,
                 const std::string& equation = "");

    // AI production: each attack-goal taskforce under its target size spends
    // the player's credits to build its roster. `buildTypes` lists the type
    // names the AI builds (in order of preference).
    void produce(SimState& sim, int playerId,
                 const std::vector<std::string>& buildTypes);

    // Per-force target sizes for production (fighters + vehicles/capitals).
    static int targetFighters() { return 8; }
    static int targetHeavies() { return 2; }

private:
    AiTargeting targeting_;
};

} // namespace eaw
