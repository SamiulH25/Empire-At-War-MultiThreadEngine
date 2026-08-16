// ScriptManager — per-frame Lua script pump for the engine.
//
// Matches the game's documented script model (docs/research/03-lua-surface.md):
// one Lua state per manager, script "threads" are coroutines registered in a
// thread table, and the engine's per-frame pump resumes every live thread once
// ("Pump_Threads"). Scripts are loaded from the MegaFileManager with loose-file
// override (the mod mechanism, doc 05).
//
// Thread safety: a Lua state is NOT thread-safe. The manager owns one state;
// the caller must pump it from a single thread (the sim thread).
#pragma once

#include "core/lua_host.h"
#include "core/meg_manager.h"
#include "core/object_model.h"
#include "core/pg_bindings.h"
#include "core/pg_object_bindings.h"

#include <string>

namespace eaw {

class ScriptManager {
public:
    explicit ScriptManager(MegaFileManager& files);

    ScriptManager(const ScriptManager&) = delete;
    ScriptManager& operator=(const ScriptManager&) = delete;

    lua_State* state() const { return host_.state(); }

    // The sim this manager's scripts query and mutate (object bindings).
    SimState& sim() { return sim_; }

    // Loads and runs a script chunk from the file manager (loose override
    // applies). Scripts typically define functions; they may run top-level
    // code. Throws LuaError on syntax/run error.
    void loadScript(const std::string& name);

    // Loads a script from a raw chunk string (tests / generated scripts).
    void runScript(const std::string& chunk, const std::string& name = "chunk");

    // Advances engine time by dt and resumes every live script thread once.
    // Finished threads are removed. Throws LuaError if a thread errors.
    void pump(double dt);

    // Current engine time (what GetCurrentTime() returns to scripts).
    double time() const { return time_; }

    // Number of live script threads.
    int threadCount() const;

private:
    MegaFileManager& files_;
    LuaHost host_;
    SimState sim_;
    double time_ = 0.0;
};

} // namespace eaw
