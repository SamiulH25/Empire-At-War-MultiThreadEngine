#include "core/event_system.h"

#include "core/lua_host.h"
#include "core/lua_wrappers.h"

extern "C" {
#include "lua.h"
}

namespace eaw {

EventSystem::EventSystem(SimState& sim) : sim_(sim) {}

EventSystem::~EventSystem() {
    if (!L_) return;
    for (const Timer& t : timers_) {
        luaL_unref(L_, LUA_REGISTRYINDEX, t.ref);
        if (t.paramRef != LUA_NOREF) luaL_unref(L_, LUA_REGISTRYINDEX, t.paramRef);
    }
    for (const DeathHandler& h : deaths_) luaL_unref(L_, LUA_REGISTRYINDEX, h.ref);
    for (const AttackedHandler& h : attacked_) luaL_unref(L_, LUA_REGISTRYINDEX, h.ref);
    for (const ProxHandler& h : proxes_) luaL_unref(L_, LUA_REGISTRYINDEX, h.ref);
}

void EventSystem::attach(lua_State* L) {
    L_ = L;
}

bool EventSystem::empty() const {
    return timers_.empty() && deaths_.empty() && attacked_.empty() && proxes_.empty();
}

void EventSystem::registerTimer(int funcRef, int paramRef, double now, double timeout) {
    Timer t;
    t.ref = funcRef;
    t.paramRef = paramRef;
    t.fireTime = now + timeout;
    timers_.push_back(t);
}

void EventSystem::registerDeath(int objectId, int funcRef) {
    DeathHandler h;
    h.objectId = objectId;
    h.ref = funcRef;
    deaths_.push_back(h);
}

void EventSystem::registerAttacked(int objectId, int funcRef) {
    AttackedHandler h;
    h.objectId = objectId;
    h.ref = funcRef;
    h.lastHull = 1.0;
    const GameObject* o = sim_.object(objectId);
    if (o) h.lastHull = o->hull;
    attacked_.push_back(h);
}

void EventSystem::cancelAttacked(int objectId) {
    for (size_t i = 0; i < attacked_.size();) {
        if (attacked_[i].objectId == objectId) {
            luaL_unref(L_, LUA_REGISTRYINDEX, attacked_[i].ref);
            attacked_.erase(attacked_.begin() + i);
        } else {
            ++i;
        }
    }
}

void EventSystem::registerProx(int objectId, int funcRef, double range, int playerFilter) {
    ProxHandler h;
    h.objectId = objectId;
    h.ref = funcRef;
    h.range = range;
    h.playerFilter = playerFilter;
    proxes_.push_back(h);
}

void EventSystem::fire(int ref, const std::function<void(lua_State*)>& pushArgs) {
    if (!L_ || ref == LUA_NOREF) return;
    lua_rawgeti(L_, LUA_REGISTRYINDEX, ref); // [fn]
    if (!lua_isfunction(L_, -1)) {
        lua_pop(L_, 1);
        return;
    }
    int nargs = 0;
    if (pushArgs) {
        pushArgs(L_);                        // [fn, args...]
        nargs = lua_gettop(L_) - 1;
    }
    if (lua_pcall(L_, nargs, 0, 0) != 0) {
        std::string msg = lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "event callback error";
        lua_pop(L_, 1);
        throw LuaError("event: " + msg);
    }
}

void EventSystem::processTimers(double now) {
    if (timers_.empty()) return;
    std::vector<Timer> ready;
    for (size_t i = 0; i < timers_.size();) {
        if (timers_[i].fireTime <= now) {
            ready.push_back(timers_[i]);
            timers_.erase(timers_.begin() + i);
        } else {
            ++i;
        }
    }
    for (const Timer& t : ready) {
        fire(t.ref, [this, &t](lua_State* L) {
            if (t.paramRef != LUA_NOREF) {
                lua_rawgeti(L, LUA_REGISTRYINDEX, t.paramRef); // [fn, param]
            }
        });
        luaL_unref(L_, LUA_REGISTRYINDEX, t.ref);
        if (t.paramRef != LUA_NOREF) luaL_unref(L_, LUA_REGISTRYINDEX, t.paramRef);
    }
}

void EventSystem::processDeaths() {
    if (deaths_.empty()) return;
    std::vector<DeathHandler> fired;
    for (size_t i = 0; i < deaths_.size();) {
        DeathHandler& h = deaths_[i];
        const GameObject* o = sim_.object(h.objectId);
        if (!o || !o->alive) {
            fired.push_back(h);
            deaths_.erase(deaths_.begin() + i);
        } else {
            ++i;
        }
    }
    for (const DeathHandler& h : fired) {
        // Fire BEFORE releasing: callback gets the object handle (which
        // resolves to nil via pushObject since it's dead — but the game
        // passes the dying object; we push the raw handle).
        fire(h.ref, [this, &h](lua_State* L) {
            pushObjectHandle(L, &sim_, h.objectId);
        });
        luaL_unref(L_, LUA_REGISTRYINDEX, h.ref);
    }
}

void EventSystem::processAttacked() {
    if (attacked_.empty()) return;
    std::vector<AttackedHandler> fired;
    for (size_t i = 0; i < attacked_.size();) {
        AttackedHandler& h = attacked_[i];
        const GameObject* o = sim_.object(h.objectId);
        if (o && o->alive && o->hull < h.lastHull) {
            fired.push_back(h);
            h.lastHull = o->hull;
            ++i;
        } else if (o && o->alive) {
            h.lastHull = o->hull; // track upward changes (heals)
            ++i;
        } else {
            // object gone — drop the handler
            luaL_unref(L_, LUA_REGISTRYINDEX, h.ref);
            attacked_.erase(attacked_.begin() + i);
        }
    }
    for (const AttackedHandler& h : fired) {
        fire(h.ref, [this, &h](lua_State* L) {
            pushObjectHandle(L, &sim_, h.objectId);
        });
        luaL_unref(L_, LUA_REGISTRYINDEX, h.ref);
    }
}

void EventSystem::processProximities() {
    if (proxes_.empty()) return;
    std::vector<ProxHandler> fired;
    for (size_t i = 0; i < proxes_.size();) {
        ProxHandler& h = proxes_[i];
        const GameObject* self = sim_.object(h.objectId);
        if (!self || !self->alive) {
            luaL_unref(L_, LUA_REGISTRYINDEX, h.ref);
            proxes_.erase(proxes_.begin() + i);
            continue;
        }
        // Find any object within range (owned by the filter, if set).
        const GameObject* other = nullptr;
        for (const GameObject* o : sim_.allObjects()) {
            if (o->id == h.objectId || !o->alive) continue;
            if (h.playerFilter != 0 && o->playerId != h.playerFilter) continue;
            if (self->position.distanceTo(o->position) <= h.range) {
                other = o;
                break;
            }
        }
        if (other) {
            fired.push_back(h);
            // A prox fires once per entering the range; to keep it simple and
            // deterministic, drop the handler after firing (one-shot).
            proxes_.erase(proxes_.begin() + i);
            continue;
        }
        ++i;
    }
    for (const ProxHandler& h : fired) {
        // Find the other object again for the callback arg.
        const GameObject* self = sim_.object(h.objectId);
        const GameObject* other = nullptr;
        if (self) {
            for (const GameObject* o : sim_.allObjects()) {
                if (o->id == h.objectId || !o->alive) continue;
                if (h.playerFilter != 0 && o->playerId != h.playerFilter) continue;
                if (self->position.distanceTo(o->position) <= h.range) {
                    other = o;
                    break;
                }
            }
        }
        fire(h.ref, [this, &h, other](lua_State* L) {
            pushObjectHandle(L, &sim_, h.objectId);
            if (other) pushObjectHandle(L, &sim_, other->id);
            else lua_pushnil(L);
        });
        luaL_unref(L_, LUA_REGISTRYINDEX, h.ref);
    }
}

void EventSystem::pump(double now) {
    processTimers(now);
    processDeaths();
    processAttacked();
    processProximities();
}

} // namespace eaw
