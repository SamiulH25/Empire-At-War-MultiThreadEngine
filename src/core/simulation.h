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

#include "core/meg_manager.h"
#include "core/object_model.h"
#include "core/script_manager.h"

namespace eaw {

class Simulation {
public:
    Simulation();

    Simulation(const Simulation&) = delete;
    Simulation& operator=(const Simulation&) = delete;

    MegaFileManager& files() { return files_; }
    ScriptManager& scripts() { return scripts_; }
    SimState& sim() { return scripts_.sim(); }

    // Advances the sim by dt seconds: time += dt, pump scripts, update sim.
    // Throws LuaError if a script thread errors.
    void tick(double dt);

    // Current game time in seconds.
    double time() const { return time_; }

private:
    MegaFileManager files_;
    ScriptManager scripts_;
    double time_ = 0.0;
};

} // namespace eaw
