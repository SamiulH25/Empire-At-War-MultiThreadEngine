// EventSystem — the game's event/timer/proximity model.
//
// Implements the documented PG* event surface (Alamo Engine Tools):
// Register_Timer / Register_Death_Event / Register_Attacked_Event /
// Register_Prox, each processed by a Process_* step driven from the script
// pump (the game's Pump_Service). Lua callbacks are stored as registry refs
// and invoked with the same arguments the game passes:
//   - timer:    func(param)          after timeout seconds
//   - death:    func(obj)            when obj dies
//   - attacked: func(obj, attacker)  when obj takes damage
//   - prox:     func(obj, other)     when another object comes in range
//
// Thread safety: the event system runs on the sim thread with the Lua state
// it was attached to; it is not shared across threads.
#pragma once

#include "core/object_model.h"

extern "C" {
#include "lauxlib.h"
}

#include <functional>
#include <unordered_map>
#include <vector>

struct lua_State;

namespace eaw {

class EventSystem {
public:
    explicit EventSystem(SimState& sim);
    ~EventSystem();

    EventSystem(const EventSystem&) = delete;
    EventSystem& operator=(const EventSystem&) = delete;

    // Binds the Lua state the callbacks live in. Must be called once before
    // any registration/processing; the state must outlive this object.
    void attach(lua_State* L);

    // --- registration (called from the Lua bindings) ---------------------
    // Each takes ownership of a luaL_ref'd callback (LUA_NOREF = none).

    // Fires `funcRef(paramRef)` after `timeout` seconds. `paramRef` is a
    // registry ref to the param value (or LUA_NOREF for no param).
    void registerTimer(int funcRef, int paramRef, double now, double timeout);

    // Fires `funcRef(objWrapper)` when the object transitions alive -> dead.
    void registerDeath(int objectId, int funcRef);
    // Fires `funcRef(objWrapper)` when the object's hull drops. The previous
    // hull is tracked per object.
    void registerAttacked(int objectId, int funcRef);
    void cancelAttacked(int objectId);

    // Fires `funcRef(objWrapper, otherWrapper)` when an object owned by
    // `playerFilter` (0 = any) comes within `range` of `objectId`.
    void registerProx(int objectId, int funcRef, double range, int playerFilter);

    // --- processing (driven from the script pump) ------------------------

    // Fires timers whose time has come, then checks death/attacked/proximity
    // transitions since the last pump.
    void pump(double now);

    // Individual process steps (the game exposes each as a Lua binding:
    // Process_Timers / Process_Death_Events / Process_Attacked_Events /
    // Process_Proximities). Called by pump(); also callable from the bindings.
    void processTimers(double now);
    void processDeaths();
    void processAttacked();
    void processProximities();

    bool empty() const;

private:
    struct Timer {
        int ref = LUA_NOREF;
        int paramRef = LUA_NOREF;
        double fireTime = 0;
    };
    struct DeathHandler {
        int objectId = 0;
        int ref = LUA_NOREF;
    };
    struct AttackedHandler {
        int objectId = 0;
        int ref = LUA_NOREF;
        double lastHull = 1.0;
    };
    struct ProxHandler {
        int objectId = 0;
        int ref = LUA_NOREF;
        double range = 0;
        int playerFilter = 0;
    };

    void fire(int ref, const std::function<void(lua_State*)>& pushArgs);

    SimState& sim_;
    lua_State* L_ = nullptr;
    std::vector<Timer> timers_;
    std::vector<DeathHandler> deaths_;
    std::vector<AttackedHandler> attacked_;
    std::vector<ProxHandler> proxes_;
};

} // namespace eaw
