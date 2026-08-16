// AiTargeting — perception-driven target selection for taskforces.
//
// Implements the AI's FindTarget loop (the documented PGTASKFORCE/Goalfunction
// surface): given a taskforce, its perception equation, and the enemy
// candidates, score every candidate in parallel (perception evaluation is a
// pure read — design doc 06) and return the best target.
//
// Deterministic: candidate order is fixed (id order) and the max-reduction
// tie-breaks by lower id, so the selected target is identical regardless of
// worker count.
#pragma once

#include "core/job_system.h"
#include "core/object_model.h"
#include "core/perception.h"

#include <vector>

namespace eaw {

struct TargetChoice {
    int objectId = 0;   // 0 = no acceptable target
    double score = 0.0;
    bool found = false;
};

class AiTargeting {
public:
    // `jobs` and `perceptions` must outlive this.
    AiTargeting(JobSystem& jobs, const PerceptionSystem& perceptions)
        : jobs_(jobs), perceptions_(perceptions) {}

    // Scores every living enemy object of `force`'s player using `equation`
    // (self = the force's center object or its first unit; target = the
    // candidate). Returns the best-scoring target. Parallel evaluation.
    TargetChoice findTarget(const SimState& sim, const TaskForce& force,
                            const std::string& equation, double gameAge) const;

    // Serial variant (tests / small candidate sets).
    TargetChoice findTargetSerial(const SimState& sim, const TaskForce& force,
                                  const std::string& equation,
                                  double gameAge) const;

    const PerceptionSystem& perceptions() const { return perceptions_; }

private:
    JobSystem& jobs_;
    const PerceptionSystem& perceptions_;
};

} // namespace eaw
