// Simulation — the engine's frame loop core.
//
// Owns the file manager (megs + loose files), the script manager (Lua +
// bindings), and the sim state. Each tick:
//   1. advances game time
//   2. pumps script threads (the game's Pump_Threads)
//   3. runs the fixed-step sim update (script-visible object mutations)
//
// This is the serial spine the design doc's parallel job graph hangs off
// (docs/research/06-threading-design.md): the tick is the serial section;
// per-object subsystems parallelize inside it later.
#pragma once

#include "core/game_constants.h"
#include "core/job_system.h"
#include "core/meg_manager.h"
#include "core/object_model.h"
#include "core/path_grid.h"
#include "core/pathfinding.h"
#include "core/script_manager.h"

#include <unordered_map>

namespace eaw {

class Simulation {
public:
    explicit Simulation(unsigned workerThreads = 0);

    Simulation(const Simulation&) = delete;
    Simulation& operator=(const Simulation&) = delete;

    MegaFileManager& files() { return files_; }
    ScriptManager& scripts() { return scripts_; }
    SimState& sim() { return scripts_.sim(); }
    JobSystem& jobs() { return jobs_; }
    PathGrid& pathGrid() { return grid_; }
    PathfindingSystem& pathfinding() { return pathfinding_; }

    // Applies game tuning constants (from GameConstants.xml). Currently:
    //  - pathfinding expansion budget (SpacePathfindMaxExpansions)
    void configure(const GameConstants& gc);

    // The configured pathfinding options (for telemetry).
    const PathfindingSystem::Options& pathOptions() const { return pathOptions_; }

    // Advances the sim by dt seconds: time += dt, pump scripts, then run the
    // parallel object update (per-object slices on the worker pool), the
    // two-phase parallel combat pass, and the pathfinding step.
    // Throws LuaError if a script thread errors.
    void tick(double dt);

    // Current game time in seconds.
    double time() const { return time_; }

    // How many ticks ran the parallel object update.
    unsigned long long updateTicks() const { return updateTicks_; }

    // Total shots fired by the combat pass (for tests/telemetry).
    unsigned long long totalShots() const { return totalShots_; }

    // Position snapshot for the current tick: object id -> position, taken
    // before the parallel update so cross-object reads (hold-position,
    // targeting) are race-free and deterministic.
    const std::unordered_map<int, Vec3>& positionSnapshot() const { return positions_; }

private:
    void snapshotPositions();
    void updateObjects(double dt);
    void runCombat(double dt);
    void stepPathfinding();

    MegaFileManager files_;
    ScriptManager scripts_;
    JobSystem jobs_;
    // 3D routing grid: 256x256x64 altitude bands at 2-unit cells.
    PathGrid grid_{256, 256, 64, 2.0};
    PathfindingSystem::Options pathOptions_;
    PathfindingSystem pathfinding_;
    double time_ = 0.0;
    unsigned long long updateTicks_ = 0;
    unsigned long long totalShots_ = 0;
    std::unordered_map<int, Vec3> positions_;
};

} // namespace eaw
