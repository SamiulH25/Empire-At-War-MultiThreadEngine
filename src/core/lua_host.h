// Lua host — RAII wrapper around a Lua 5.1 state for the engine.
//
// The game embeds Lua 5.x with a coroutine-based script model (LuaCreateThread,
// LuaThreadTable). This host provides the engine-side integration:
//  - owned lua_State (RAII)
//  - script loading from strings (the .meg reader provides the bytes)
//  - function calling with error handling
//  - coroutine creation (the "script thread" model)
//
// Thread safety: a Lua state is NOT thread-safe. One state per script manager
// (matching the game's model); the caller must not share a state across
// threads. The job system must never touch the same state concurrently.
#pragma once

#include <stdexcept>
#include <string>

struct lua_State;

namespace eaw {

class LuaError : public std::runtime_error {
public:
    explicit LuaError(const std::string& msg) : std::runtime_error(msg) {}
};

class LuaHost {
public:
    LuaHost();                              // opens a fresh state with std libs
    ~LuaHost();

    LuaHost(const LuaHost&) = delete;
    LuaHost& operator=(const LuaHost&) = delete;

    lua_State* state() const { return L_; }

    // Loads and runs a script from a string. Throws LuaError on syntax/run error.
    void runScript(const std::string& chunk, const std::string& name = "chunk");

    // Calls a global function by name with no args. Throws LuaError.
    void callGlobal(const std::string& name);

    // Creates a coroutine running `funcName` (the game's LuaCreateThread model).
    // Returns a handle (positive index into the state's registry-ish use).
    int createCoroutine(const std::string& funcName);

    // Resumes the coroutine at the given handle. Returns true if it yielded,
    // false if it finished. Throws LuaError on runtime error.
    bool resumeCoroutine(int handle);

private:
    lua_State* L_ = nullptr;
};

} // namespace eaw
