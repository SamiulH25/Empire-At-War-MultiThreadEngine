#include "core/script_manager.h"

#include <cctype>
#include <string>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

namespace eaw {

namespace {

// The game's module-loader convention: `require("PGTaskForce")` resolves to
// DATA\SCRIPTS\LIBRARY\PGTASKFORCE.LUA (module name + .LUA under the
// SCRIPTS root). We install this as a package.loaders entry so mod scripts
// can pull in their library dependencies through the mounted file manager
// (loose files override meg entries).
//
// Some requires name the game's NATIVE binding modules (PGAICommands,
// pgcommands, PGBaseDefinitions — hardcoded C++ tables in the real engine,
// not Lua files). Those are stubbed as empty tables so the library scripts
// load and define their functions; calling a stub member still errors, which
// is the honest behavior until the binding is implemented.
int loadModScript(lua_State* s) {
    // package.loaders entries receive the module name.
    const char* mod = luaL_checkstring(s, 1);
    if (!mod || !*mod) { lua_pushstring(s, "empty module name"); return 1; }

    // The manager is a lightuserdata upvalue.
    MegaFileManager* files = static_cast<MegaFileManager*>(
        lua_touserdata(s, lua_upvalueindex(1)));

    // Native binding modules the game registers (no Lua file on disk).
    // require() expects a loader to return a FUNCTION (the module chunk);
    // for native modules we return a function that yields an empty table.
    const std::string upper = [&]() {
        std::string m = mod;
        for (char& c : m) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return m;
    }();
    if (upper == "PGAICOMMANDS" || upper == "PGCOMMANDS" ||
        upper == "PGBASEDEFINITIONS") {
        lua_pushcfunction(s, [](lua_State* ls) -> int {
            lua_newtable(ls);
            return 1;
        });
        return 1;
    }

    // Candidate virtual paths, in the game's search order:
    //   DATA\SCRIPTS\LIBRARY\<mod>.LUA        (the AI library scripts)
    //   DATA\SCRIPTS\<mod>.LUA                (plan/script root)
    const char* prefixes[] = {"DATA/SCRIPTS/LIBRARY/", "DATA/SCRIPTS/"};
    for (const char* p : prefixes) {
        std::string name = std::string(p) + upper + ".LUA";
        if (files->exists(name)) {
            try {
                auto bytes = files->read(name);
                if (luaL_loadbuffer(s, reinterpret_cast<const char*>(bytes.data()),
                                    bytes.size(), name.c_str()) == 0) {
                    return 1; // module chunk on the stack
                }
                lua_pop(s, 1); // load error — fall through to "not found"
            } catch (const std::exception&) {
            }
        }
    }
    lua_pushfstring(s, "no engine script module '%s'", mod);
    return 1;
}

} // namespace

ScriptManager::ScriptManager(MegaFileManager& files)
    : files_(files), events_(sim_) {
    registerPgBindings(host_);
    registerObjectBindings(host_, sim_);
    registerEventBindings(host_, events_, sim_);
    registerTaskForceBindings(host_, sim_);
    registerFogBindings(host_, sim_);
    events_.attach(host_.state());
    setEngineTime(host_.state(), time_);

    // Install the engine script loader into package.loaders (Lua 5.1).
    lua_State* s = host_.state();
    lua_getglobal(s, "package");
    if (lua_istable(s, -1)) {
        lua_getfield(s, -1, "loaders");
        if (lua_istable(s, -1)) {
            lua_pushlightuserdata(s, &files_);
            lua_pushcclosure(s, loadModScript, 1);
            // Append at the end (after the default loaders, before the
            // C-loader fallback is irrelevant — ours covers the mod scripts).
            int n = static_cast<int>(lua_objlen(s, -1));
            lua_rawseti(s, -2, n + 1);
        }
        lua_pop(s, 1);
    }
    lua_pop(s, 1);
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
