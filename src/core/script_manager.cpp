#include "core/script_manager.h"

extern "C" {
#include "lua.h"
}

namespace eaw {

ScriptManager::ScriptManager(MegaFileManager& files)
    : files_(files), events_(sim_) {
    registerPgBindings(host_);
    registerObjectBindings(host_, sim_);
    registerEventBindings(host_, events_, sim_);
    registerTaskForceBindings(host_, sim_);
    registerFogBindings(host_, sim_);
    events_.attach(host_.state());
    setEngineTime(host_.state(), time_);
}

void ScriptManager::loadScript(const std::string& name) {
    auto bytes = files_.read(name);
    host_.runScript(std::string(bytes.begin(), bytes.end()), name);
}

void ScriptManager::runScript(const std::string& chunk, const std::string& name) {
    host_.runScript(chunk, name);
}

void ScriptManager::pump(double dt) {
    time_ += dt;
    setEngineTime(host_.state(), time_);
    pumpThreads(host_.state());
    events_.pump(time_);
}

int ScriptManager::threadCount() const {
    lua_State* s = host_.state();
    lua_getfield(s, LUA_REGISTRYINDEX, "__PgThreads");
    int n = 0;
    lua_pushnil(s);
    while (lua_next(s, -2) != 0) {
        ++n;
        lua_pop(s, 1);
    }
    lua_pop(s, 1);
    return n;
}

} // namespace eaw
