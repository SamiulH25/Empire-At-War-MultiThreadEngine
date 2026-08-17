#include "core/pg_object_bindings.h"

#include "core/lua_wrappers.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

namespace eaw {

namespace {

// Forward declaration (defined in the type-methods section below).
const char* typeNameFromId(lua_State* s, Wrapper* w);

// ---- global queries -----------------------------------------------------

int findPlayer(lua_State* s) {
    SimState* sim = simFromUpvalue(s, 1);
    const char* name = luaL_checkstring(s, 1);
    const Player* p = sim->findPlayer(name);
    if (!p) { lua_pushnil(s); return 1; }
    pushWrapper(s, sim, WrapperKind::Player, p->id);
    return 1;
}

int findObjectType(lua_State* s) {
    SimState* sim = simFromUpvalue(s, 1);
    const char* name = luaL_checkstring(s, 1);
    const ObjectType* t = sim->type(name);
    if (!t) { lua_pushnil(s); return 1; }
    // Look up (or lazily assign) the type's wrapper id. The types table
    // stays on the stack throughout so the lazy branch can write to it.
    lua_getfield(s, LUA_REGISTRYINDEX, "__PgTypeIds"); // [name, types]
    lua_pushstring(s, name);
    lua_gettable(s, -2);                   // [name, types, id]
    if (lua_isnil(s, -1)) {
        lua_pop(s, 1);                     // [name, types]
        lua_getfield(s, LUA_REGISTRYINDEX, "__PgTypeCounter");
        int id = static_cast<int>(lua_tointeger(s, -1));
        lua_pop(s, 1);
        lua_pushinteger(s, id + 1);
        lua_setfield(s, LUA_REGISTRYINDEX, "__PgTypeCounter");
        // names[id] = name
        lua_getfield(s, LUA_REGISTRYINDEX, "__PgTypeNames"); // [name, types, names]
        lua_pushinteger(s, id + 1);
        lua_pushstring(s, name);
        lua_settable(s, -3);               // [name, types, names]
        lua_pop(s, 1);                     // [name, types]
        // types[name] = id+1
        lua_pushstring(s, name);           // [name, types, name]
        lua_pushinteger(s, id + 1);        // [name, types, name, id+1]
        lua_settable(s, -3);               // [name, types]
        lua_pushinteger(s, id + 1);        // [name, types, id]
    }
    int id = static_cast<int>(lua_tointeger(s, -1));
    lua_pop(s, 3);                         // [name, types, id] -> []
    pushWrapper(s, sim, WrapperKind::Type, id);
    return 1;
}

// Helper: pushes a list of object wrappers.
void pushObjectList(lua_State* s, SimState* sim, const std::vector<const GameObject*>& objs) {
    lua_createtable(s, static_cast<int>(objs.size()), 0);
    int n = 1;
    for (const GameObject* o : objs) {
        pushObject(s, sim, o);
        lua_rawseti(s, -2, n++);
    }
}

int findAllObjectsOfType(lua_State* s) {
    SimState* sim = simFromUpvalue(s, 1);
    const char* x = luaL_checkstring(s, 1);
    // 1-arg: type name, property flag, or category. Try type name first,
    // then category (pipe-separated categories match any).
    std::vector<const GameObject*> objs = sim->objectsOfType(x);
    if (objs.empty()) objs = sim->objectsOfCategory(x);
    pushObjectList(s, sim, objs);
    return 1;
}

int findFirstObject(lua_State* s) {
    SimState* sim = simFromUpvalue(s, 1);
    const char* typeName = luaL_checkstring(s, 1);
    auto objs = sim->objectsOfType(typeName);
    pushObject(s, sim, objs.empty() ? nullptr : objs.front());
    return 1;
}

int findNearest(lua_State* s) {
    SimState* sim = simFromUpvalue(s, 1);
    Wrapper* from = checkWrapper(s, 1);
    const char* typeName = luaL_checkstring(s, 2);
    const GameObject* origin = wrapperObject(s, from);
    if (!origin) { lua_pushnil(s); return 1; }
    pushObject(s, sim, sim->nearestObject(origin->position, typeName));
    return 1;
}

// ---- wrapper methods (object) -------------------------------------------

int objGetHull(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const GameObject* o = wrapperObject(s, w);
    if (!o) { lua_pushnil(s); return 1; }
    lua_pushnumber(s, o->hull);
    return 1;
}

int objGetShield(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const GameObject* o = wrapperObject(s, w);
    if (!o) { lua_pushnil(s); return 1; }
    lua_pushnumber(s, o->shield);
    return 1;
}

int objGetHealth(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const GameObject* o = wrapperObject(s, w);
    if (!o) { lua_pushnil(s); return 1; }
    lua_pushnumber(s, o->hull);
    return 1;
}

int objGetEnergy(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const GameObject* o = wrapperObject(s, w);
    if (!o) { lua_pushnil(s); return 1; }
    lua_pushnumber(s, o->energy);
    return 1;
}

int objGetOwner(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const GameObject* o = wrapperObject(s, w);
    if (!o) { lua_pushnil(s); return 1; }
    const Player* p = w->sim->player(o->playerId);
    if (!p) { lua_pushnil(s); return 1; }
    pushWrapper(s, w->sim, WrapperKind::Player, p->id);
    return 1;
}

int objGetFaction(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const GameObject* o = wrapperObject(s, w);
    if (!o) { lua_pushnil(s); return 1; }
    const Player* p = w->sim->player(o->playerId);
    if (!p) { lua_pushnil(s); return 1; }
    lua_pushstring(s, p->factionName.c_str());
    return 1;
}

int objGetForce(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const GameObject* o = wrapperObject(s, w);
    if (!o) { lua_pushnil(s); return 1; }
    // Find the taskforce containing this object (galactic mode).
    for (const TaskForce* f : w->sim->forcesOfPlayer(o->playerId)) {
        for (int uid : f->units) {
            if (uid == o->id) {
                pushWrapper(s, w->sim, WrapperKind::TaskForce, f->id);
                return 1;
            }
        }
    }
    lua_pushnil(s);
    return 1;
}

int objSetTargetingPriorities(lua_State* s, bool land) {
    Wrapper* w = checkWrapper(s, 1);
    GameObject* o = w->sim->object(w->id);
    if (!o) return 0;
    std::vector<std::string>& dst =
        land ? o->landTargetingPriorities : o->targetingPriorities;
    dst.clear();
    // Arg 2: a table of priority category names (ordered).
    if (lua_istable(s, 2)) {
        lua_pushnil(s);
        while (lua_next(s, 2) != 0) {
            if (lua_isstring(s, -1)) dst.push_back(lua_tostring(s, -1));
            lua_pop(s, 1);
        }
    } else if (lua_isstring(s, 2)) {
        dst.push_back(lua_tostring(s, 2));
    }
    return 0;
}

int objSetTargetingPrioritiesFn(lua_State* s) {
    return objSetTargetingPriorities(s, false);
}

int objSetLandTargetingPrioritiesFn(lua_State* s) {
    return objSetTargetingPriorities(s, true);
}

int objGetType(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const GameObject* o = wrapperObject(s, w);
    if (!o) { lua_pushnil(s); return 1; }
    // Look up the type wrapper by name.
    lua_getfield(s, LUA_REGISTRYINDEX, "__PgTypeIds");
    lua_pushstring(s, o->typeName.c_str());
    lua_gettable(s, -2);
    int id = static_cast<int>(lua_tointeger(s, -1));
    lua_pop(s, 2);
    if (id == 0) {
        // type not yet wrapped — build it via Find_Object_Type logic
        lua_getglobal(s, "Find_Object_Type");
        lua_pushstring(s, o->typeName.c_str());
        lua_call(s, 1, 1);
        return 1;
    }
    pushWrapper(s, w->sim, WrapperKind::Type, id);
    return 1;
}

int objGetId(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    lua_pushinteger(s, w->id);
    return 1;
}

int objGetPosition(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const GameObject* o = wrapperObject(s, w);
    if (!o) { lua_pushnil(s); return 1; }
    pushWrapper(s, w->sim, WrapperKind::Position, 0);
    Wrapper* pw = static_cast<Wrapper*>(lua_touserdata(s, -1));
    pw->pos = o->position;
    return 1;
}

int objGetDistance(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const GameObject* o = wrapperObject(s, w);
    if (!o) { lua_pushnil(s); return 1; }
    Wrapper* other = checkWrapper(s, 2);
    const GameObject* o2 = wrapperObject(s, other);
    if (!o2) { lua_pushnil(s); return 1; }
    lua_pushnumber(s, o->position.distanceTo(o2->position));
    return 1;
}

int objIsCategory(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const GameObject* o = wrapperObject(s, w);
    const char* cat = luaL_checkstring(s, 2);
    if (!o) { lua_pushboolean(s, 0); return 1; }
    const ObjectType* t = w->sim->type(o->typeName);
    bool found = false;
    if (t) {
        // pipe-separated categories: "Frigate | Capital" matches any
        std::string cats(cat);
        size_t start = 0;
        while (start <= cats.size()) {
            size_t pipe = cats.find('|', start);
            std::string one = cats.substr(start, pipe == std::string::npos ? std::string::npos : pipe - start);
            // trim
            size_t b = one.find_first_not_of(" \t");
            size_t e = one.find_last_not_of(" \t");
            if (b != std::string::npos) one = one.substr(b, e - b + 1);
            for (const auto& c : t->categories) {
                if (c == one) { found = true; break; }
            }
            if (found) break;
            if (pipe == std::string::npos) break;
            start = pipe + 1;
        }
    }
    lua_pushboolean(s, found);
    return 1;
}

int objHasProperty(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const GameObject* o = wrapperObject(s, w);
    const char* prop = luaL_checkstring(s, 2);
    if (!o) { lua_pushboolean(s, 0); return 1; }
    const ObjectType* t = w->sim->type(o->typeName);
    bool found = false;
    if (t) {
        for (const auto& p : t->properties) {
            if (p == prop) { found = true; break; }
        }
    }
    lua_pushboolean(s, found);
    return 1;
}

int objIsValid(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    lua_pushboolean(s, wrapperObjectValid(s, w));
    return 1;
}

int objIsHero(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const GameObject* o = wrapperObject(s, w);
    if (!o) { lua_pushboolean(s, 0); return 1; }
    const ObjectType* t = w->sim->type(o->typeName);
    lua_pushboolean(s, t && t->hero);
    return 1;
}

int objIsSelectable(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const GameObject* o = wrapperObject(s, w);
    lua_pushboolean(s, o ? o->selectable : 0);
    return 1;
}

int objGetName(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const GameObject* o = wrapperObject(s, w);
    if (!o) { lua_pushnil(s); return 1; }
    lua_pushstring(s, o->typeName.c_str());
    return 1;
}

int objGetGarrisonedUnits(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const GameObject* o = wrapperObject(s, w);
    if (!o) { lua_pushnil(s); return 1; }
    lua_createtable(s, static_cast<int>(o->garrisonedUnits.size()), 0);
    int n = 1;
    for (int id : o->garrisonedUnits) {
        pushObject(s, w->sim, w->sim->object(id));
        lua_rawseti(s, -2, n++);
    }
    return 1;
}

int objHasGarrison(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const GameObject* o = wrapperObject(s, w);
    lua_pushboolean(s, o && !o->garrisonedUnits.empty());
    return 1;
}

int objIsInGarrison(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const GameObject* o = wrapperObject(s, w);
    lua_pushboolean(s, o ? o->inGarrison : 0);
    return 1;
}

int objGetTimeTillDead(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const GameObject* o = wrapperObject(s, w);
    if (!o) { lua_pushnil(s); return 1; }
    // Hull is normalized 0..1; no damage model yet — return hull as a proxy.
    lua_pushnumber(s, o->hull);
    return 1;
}

// ---- wrapper methods (player) -------------------------------------------

int playerGetId(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    lua_pushinteger(s, w->id);
    return 1;
}

int playerGetName(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const Player* p = w->sim->player(w->id);
    if (!p) { lua_pushnil(s); return 1; }
    lua_pushstring(s, p->name.c_str());
    return 1;
}

int playerGetFactionName(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const Player* p = w->sim->player(w->id);
    if (!p) { lua_pushnil(s); return 1; }
    lua_pushstring(s, p->factionName.c_str());
    return 1;
}

int playerGetDifficulty(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const Player* p = w->sim->player(w->id);
    if (!p) { lua_pushnil(s); return 1; }
    lua_pushstring(s, p->difficulty.c_str());
    return 1;
}

int playerIsHuman(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const Player* p = w->sim->player(w->id);
    lua_pushboolean(s, p ? p->human : 0);
    return 1;
}

int playerGetTechLevel(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const Player* p = w->sim->player(w->id);
    if (!p) { lua_pushnil(s); return 1; }
    lua_pushinteger(s, p->techLevel);
    return 1;
}

int playerGetCredits(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const Player* p = w->sim->player(w->id);
    if (!p) { lua_pushnil(s); return 1; }
    lua_pushnumber(s, p->credits);
    return 1;
}

// ---- economy (player methods) --------------------------------------------

int playerGiveMoney(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    double amount = luaL_checknumber(s, 2);
    w->sim->giveMoney(w->id, amount);
    return 0;
}

int playerSetTechLevel(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    int level = static_cast<int>(luaL_checkinteger(s, 2));
    w->sim->setTechLevel(w->id, level);
    return 0;
}

int playerUnlockTech(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    // Arg 2 may be a type wrapper or a type name.
    if (lua_isuserdata(s, 2)) {
        Wrapper* t = checkWrapper(s, 2);
        const char* n = typeNameFromId(s, t);
        if (n) {
            w->sim->unlockType(w->id, n);
            lua_pop(s, 2);
        }
    } else {
        w->sim->unlockType(w->id, luaL_checkstring(s, 2));
    }
    return 0;
}

int playerLockTech(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    if (lua_isuserdata(s, 2)) {
        Wrapper* t = checkWrapper(s, 2);
        const char* n = typeNameFromId(s, t);
        if (n) {
            w->sim->lockType(w->id, n);
            lua_pop(s, 2);
        }
    } else {
        w->sim->lockType(w->id, luaL_checkstring(s, 2));
    }
    return 0;
}

// ---- abilities (object methods) ------------------------------------------

int objActivateAbility(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const char* name = luaL_checkstring(s, 2);
    int targetId = 0;
    if (!lua_isnoneornil(s, 3) && lua_isuserdata(s, 3)) {
        Wrapper* t = checkWrapper(s, 3);
        if (t->kind == WrapperKind::Object) targetId = t->id;
    }
    bool ok = w->sim->activateAbility(w->id, name, targetId);
    lua_pushboolean(s, ok);
    return 1;
}

int objTryAbility(lua_State* s) {
    // Try_Ability: activate if ready; never errors on cooldown.
    return objActivateAbility(s);
}

int objUseAbilityIfAble(lua_State* s) {
    return objActivateAbility(s);
}

int objHasAbility(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const char* name = luaL_checkstring(s, 2);
    lua_pushboolean(s, w->sim->hasAbility(w->id, name));
    return 1;
}

int objIsAbilityActive(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const char* name = luaL_checkstring(s, 2);
    lua_pushboolean(s, w->sim->isAbilityActive(w->id, name));
    return 1;
}

int objIsAbilityReady(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const char* name = luaL_checkstring(s, 2);
    lua_pushboolean(s, w->sim->isAbilityReady(w->id, name));
    return 1;
}

int objCancelAbility(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const char* name = luaL_checkstring(s, 2);
    w->sim->cancelAbility(w->id, name);
    return 0;
}

int objForceAbilityRecharge(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const char* name = luaL_checkstring(s, 2);
    w->sim->resetAbilityCooldown(w->id, name);
    return 0;
}

int objResetAbilityCounter(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    GameObject* o = w->sim->object(w->id);
    if (!o) return 0;
    for (const auto& [n, st] : o->abilityStates) {
        w->sim->resetAbilityCooldown(w->id, n);
    }
    return 0;
}

// ---- diplomacy (player methods) ------------------------------------------

int playerMakeAlly(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    Wrapper* other = checkWrapper(s, 2);
    if (other->kind != WrapperKind::Player) {
        return luaL_error(s, "Make_Ally: expected player");
    }
    w->sim->makeAlly(w->id, other->id);
    return 0;
}

int playerMakeEnemy(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    Wrapper* other = checkWrapper(s, 2);
    if (other->kind != WrapperKind::Player) {
        return luaL_error(s, "Make_Enemy: expected player");
    }
    w->sim->makeEnemy(w->id, other->id);
    return 0;
}

int playerIsAlly(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    Wrapper* other = checkWrapper(s, 2);
    lua_pushboolean(s, other->kind == WrapperKind::Player &&
                          w->sim->isAlly(w->id, other->id));
    return 1;
}

int playerIsEnemy(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    Wrapper* other = checkWrapper(s, 2);
    lua_pushboolean(s, other->kind == WrapperKind::Player &&
                          w->sim->isEnemy(w->id, other->id));
    return 1;
}

int playerGetEnemy(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const Player* p = w->sim->player(w->id);
    if (!p) { lua_pushnil(s); return 1; }
    // First player that is an enemy of this one (documented as "an enemy
    // player"; typically the opposing faction).
    for (const auto& other : w->sim->allPlayers()) {
        if (other.id != w->id && w->sim->isEnemy(w->id, other.id)) {
            pushWrapper(s, w->sim, WrapperKind::Player, other.id);
            return 1;
        }
    }
    lua_pushnil(s);
    return 1;
}

// ---- wrapper methods (type) ---------------------------------------------

// Pushes the type name for a Type wrapper's id onto the stack. Returns the
// name (valid until the stack is modified) or nullptr if unknown.
const char* typeNameFromId(lua_State* s, Wrapper* w) {
    lua_getfield(s, LUA_REGISTRYINDEX, "__PgTypeNames");
    lua_pushinteger(s, w->id);
    lua_gettable(s, -2);                   // [names, name]
    const char* n = lua_tostring(s, -1);
    if (!n) {
        lua_pop(s, 2);
        return nullptr;
    }
    return n;
}

int typeGetName(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const char* n = typeNameFromId(s, w);
    if (!n) { lua_pushnil(s); return 1; }
    // stack: [self, names, name] — replace self with name, drop names
    lua_replace(s, 1);                     // [name, names]
    lua_pop(s, 1);                         // [name]
    return 1;
}

const ObjectType* typeFromWrapper(lua_State* s, Wrapper* w) {
    const char* n = typeNameFromId(s, w);
    if (!n) return nullptr;
    const ObjectType* t = w->sim->type(n);
    lua_pop(s, 2);                         // drop names, name
    return t;
}

int typeIsHero(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const ObjectType* t = typeFromWrapper(s, w);
    lua_pushboolean(s, t ? t->hero : 0);
    return 1;
}

int typeGetBuildCost(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const ObjectType* t = typeFromWrapper(s, w);
    if (!t) { lua_pushnil(s); return 1; }
    lua_pushnumber(s, t->buildCost);
    return 1;
}

int typeGetTechLevel(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const ObjectType* t = typeFromWrapper(s, w);
    if (!t) { lua_pushnil(s); return 1; }
    lua_pushinteger(s, t->techLevel);
    return 1;
}

int typeGetMaxRange(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const ObjectType* t = typeFromWrapper(s, w);
    if (!t) { lua_pushnil(s); return 1; }
    lua_pushnumber(s, t->maxRange);
    return 1;
}

int typeGetMinRange(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const ObjectType* t = typeFromWrapper(s, w);
    if (!t) { lua_pushnil(s); return 1; }
    lua_pushnumber(s, t->minRange);
    return 1;
}

int typeIsAffectedByMissileShield(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const ObjectType* t = typeFromWrapper(s, w);
    lua_pushboolean(s, t ? t->affectedByMissileShield : 0);
    return 1;
}

int typeIsAffectedByLaserDefense(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const ObjectType* t = typeFromWrapper(s, w);
    lua_pushboolean(s, t ? t->affectedByLaserDefense : 0);
    return 1;
}

// ---- position methods ----------------------------------------------------

int posGetXYZ(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    lua_pushnumber(s, w->pos.x);
    lua_pushnumber(s, w->pos.y);
    lua_pushnumber(s, w->pos.z);
    return 3;
}

// ---- wrapper methods (object actions) ------------------------------------
// These mutate the sim and return CommandBlock wrappers (or nothing), per the
// documented API. Positions are the target's position for object targets.
// (pushCommandBlock / targetPosition live in lua_wrappers.h — shared with
// the taskforce bindings.)

int cmdIsFinished(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    lua_pushboolean(s, w->finished);
    return 1;
}

int cmdResult(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    if (w->result == 0) { lua_pushnil(s); return 1; }
    lua_pushnumber(s, w->result);
    return 1;
}

int objMoveTo(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const GameObject* o = wrapperObject(s, w);
    if (!o) { lua_pushnil(s); return 1; }
    Vec3 target;
    if (!targetPosition(s, 2, target)) { lua_pushnil(s); return 1; }
    // Set the move target; the sim's parallel update integrates toward it
    // over subsequent ticks (async, like the game's movement).
    GameObject* g = w->sim->object(o->id);
    g->hasMoveTarget = true;
    g->moveTarget = target;
    pushCommandBlock(s, w->sim, 0);
    return 1;
}

int objAttackTarget(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const GameObject* o = wrapperObject(s, w);
    if (!o || !lua_isuserdata(s, 2)) { lua_pushnil(s); return 1; }
    // Record the attack target id on the object.
    Wrapper* t = checkWrapper(s, 2);
    if (t->kind != WrapperKind::Object) { lua_pushnil(s); return 1; }
    GameObject* g = w->sim->object(o->id);
    g->attackTargetId = t->id;
    pushCommandBlock(s, w->sim, 0);
    return 1;
}

int objGetAttackTarget(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const GameObject* o = wrapperObject(s, w);
    if (!o || o->attackTargetId == 0) { lua_pushnil(s); return 1; }
    pushObject(s, w->sim, w->sim->object(o->attackTargetId));
    return 1;
}

int objHasAttackTarget(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const GameObject* o = wrapperObject(s, w);
    lua_pushboolean(s, o && o->attackTargetId != 0);
    return 1;
}

int objReleaseUnit(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    (void)w; // no-op: no unit pools in this sim tier
    return 0;
}

int objLockCurrentOrders(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const GameObject* o = wrapperObject(s, w);
    if (o) w->sim->object(o->id)->ordersLocked = true;
    return 0;
}

int objUnlockCurrentOrders(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const GameObject* o = wrapperObject(s, w);
    if (o) w->sim->object(o->id)->ordersLocked = false;
    return 0;
}

int objTakeDamage(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const GameObject* o = wrapperObject(s, w);
    double dmg = luaL_checknumber(s, 2);
    if (!o) return 0;
    GameObject* g = w->sim->object(o->id);
    if (!g->invulnerable) {
        g->hull = std::max(0.0, g->hull - dmg);
        if (g->hull == 0.0) g->alive = false;
    }
    return 0;
}

int objDespawn(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const GameObject* o = wrapperObject(s, w);
    if (!o) return 0;
    GameObject* g = w->sim->object(o->id);
    g->alive = false;
    return 0;
}

int objMakeInvulnerable(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    bool inv = lua_toboolean(s, 2);
    const GameObject* o = wrapperObject(s, w);
    if (o) w->sim->object(o->id)->invulnerable = inv;
    return 0;
}

int objSetSelectable(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    bool sel = lua_toboolean(s, 2);
    const GameObject* o = wrapperObject(s, w);
    if (o) w->sim->object(o->id)->selectable = sel;
    return 0;
}

int objCanGarrison(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const GameObject* o = wrapperObject(s, w);
    if (!o || !lua_isuserdata(s, 2)) { lua_pushboolean(s, 0); return 1; }
    Wrapper* t = checkWrapper(s, 2);
    const GameObject* container = wrapperObject(s, t);
    // Both must exist; the container must have garrison capacity
    // (modeled: any object can hold units for now).
    lua_pushboolean(s, o->alive && container && container->alive);
    return 1;
}

int objGarrison(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const GameObject* o = wrapperObject(s, w);
    if (!o || !lua_isuserdata(s, 2)) { lua_pushboolean(s, 0); return 1; }
    Wrapper* t = checkWrapper(s, 2);
    bool ok = w->sim->garrisonUnit(o->id, t->id);
    lua_pushboolean(s, ok);
    return 1;
}

int objTryGarrison(lua_State* s) {
    return objGarrison(s);
}

int objLeaveGarrison(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const GameObject* o = wrapperObject(s, w);
    if (o) w->sim->ungarrisonUnit(o->id);
    return 0;
}

int objEjectGarrison(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const GameObject* o = wrapperObject(s, w);
    if (!o) return 0;
    std::vector<int> ids = o->garrisonedUnits;
    for (int id : ids) w->sim->ungarrisonUnit(id);
    return 0;
}

// ---- global spawn ---------------------------------------------------------

int createPosition(lua_State* s) {
    SimState* sim = simFromUpvalue(s, 1);
    double x = luaL_checknumber(s, 1);
    double y = luaL_checknumber(s, 2);
    double z = luaL_checknumber(s, 3);
    pushWrapper(s, sim, WrapperKind::Position, 0);
    Wrapper* pw = static_cast<Wrapper*>(lua_touserdata(s, -1));
    pw->pos = Vec3{x, y, z};
    return 1;
}

int spawnUnit(lua_State* s) {
    SimState* sim = simFromUpvalue(s, 1);
    // arg 1: type wrapper or type name; arg 2: position wrapper or object;
    // arg 3: player wrapper or faction name
    std::string typeName;
    if (lua_isuserdata(s, 1)) {
        Wrapper* t = checkWrapper(s, 1);
        if (t->kind != WrapperKind::Type) { lua_pushnil(s); return 1; }
        const char* n = typeNameFromId(s, t);
        if (!n) { lua_pushnil(s); return 1; }
        typeName = n;
        lua_pop(s, 2);
    } else {
        typeName = luaL_checkstring(s, 1);
    }
    Vec3 pos;
    if (!targetPosition(s, 2, pos)) { lua_pushnil(s); return 1; }
    int playerId = 0;
    if (lua_isuserdata(s, 3)) {
        Wrapper* p = checkWrapper(s, 3);
        playerId = p->id;
    } else {
        const Player* pl = sim->findPlayer(luaL_checkstring(s, 3));
        if (!pl) { lua_pushnil(s); return 1; }
        playerId = pl->id;
    }
    int newId = sim->spawnUnit(typeName, playerId, pos);
    if (newId == 0) { lua_pushnil(s); return 1; }
    // Returns a list whose first entry is the spawned object (documented).
    lua_createtable(s, 1, 0);
    pushObject(s, sim, sim->object(newId));
    lua_rawseti(s, -2, 1);
    return 1;
}

int reinforceUnit(lua_State* s) {
    SimState* sim = simFromUpvalue(s, 1);
    std::string typeName;
    if (lua_isuserdata(s, 1)) {
        Wrapper* t = checkWrapper(s, 1);
        if (t->kind != WrapperKind::Type) { lua_pushnil(s); return 1; }
        const char* n = typeNameFromId(s, t);
        if (!n) { lua_pushnil(s); return 1; }
        typeName = n;
        lua_pop(s, 2);
    } else {
        typeName = luaL_checkstring(s, 1);
    }
    // Y=false adds to the reinforcement pool (no spawn); Y=position spawns.
    if (lua_isnoneornil(s, 2) || (lua_isboolean(s, 2) && !lua_toboolean(s, 2))) {
        pushCommandBlock(s, sim, 0);
        return 1;
    }
    Vec3 pos;
    if (!targetPosition(s, 2, pos)) { pushCommandBlock(s, sim, 0); return 1; }
    int playerId = 0;
    if (lua_isuserdata(s, 3)) {
        Wrapper* p = checkWrapper(s, 3);
        playerId = p->id;
    } else {
        const Player* pl = sim->findPlayer(luaL_checkstring(s, 3));
        if (!pl) { pushCommandBlock(s, sim, 0); return 1; }
        playerId = pl->id;
    }
    sim->spawnUnit(typeName, playerId, pos);
    pushCommandBlock(s, sim, 0);
    return 1;
}

// ---- __index dispatch ----------------------------------------------------
// The shared LuaWrapperMetaTable.__index receives (wrapper, key). We look up
// the method by name in a per-kind method table and return it.

struct MethodEntry {
    const char* name;
    lua_CFunction fn;
};

const MethodEntry kObjectMethods[] = {
    {"Get_Hull", objGetHull},
    {"Get_Health", objGetHealth},
    {"Get_Shield", objGetShield},
    {"Get_Energy", objGetEnergy},
    {"Get_Owner", objGetOwner},
    {"Get_Faction", objGetFaction},
    {"Get_Type", objGetType},
    {"Get_ID", objGetId},
    {"Get_Position", objGetPosition},
    {"Get_Distance", objGetDistance},
    {"Is_Category", objIsCategory},
    {"Has_Property", objHasProperty},
    {"Is_Valid", objIsValid},
    {"Is_Hero", objIsHero},
    {"Is_Selectable", objIsSelectable},
    {"Get_Name", objGetName},
    {"Get_Force", objGetForce},
    {"Set_Targeting_Priorities", objSetTargetingPrioritiesFn},
    {"Set_Land_AI_Targeting_Priorities", objSetLandTargetingPrioritiesFn},
    {"Get_Garrisoned_Units", objGetGarrisonedUnits},
    {"Has_Garrison", objHasGarrison},
    {"Is_In_Garrison", objIsInGarrison},
    {"Get_Time_Till_Dead", objGetTimeTillDead},
    // Actions
    {"Move_To", objMoveTo},
    {"Attack_Target", objAttackTarget},
    {"Get_Attack_Target", objGetAttackTarget},
    {"Has_Attack_Target", objHasAttackTarget},
    {"Release_Unit", objReleaseUnit},
    {"Lock_Current_Orders", objLockCurrentOrders},
    {"Unlock_Current_Orders", objUnlockCurrentOrders},
    {"Take_Damage", objTakeDamage},
    {"Despawn", objDespawn},
    {"Make_Invulnerable", objMakeInvulnerable},
    {"Set_Selectable", objSetSelectable},
    {"Can_Garrison", objCanGarrison},
    {"Garrison", objGarrison},
    {"Try_Garrison", objTryGarrison},
    {"Leave_Garrison", objLeaveGarrison},
    {"Eject_Garrison", objEjectGarrison},
    // Abilities
    {"Activate_Ability", objActivateAbility},
    {"Try_Ability", objTryAbility},
    {"Use_Ability_If_Able", objUseAbilityIfAble},
    {"Has_Ability", objHasAbility},
    {"Is_Ability_Active", objIsAbilityActive},
    {"Is_Ability_Ready", objIsAbilityReady},
    {"Cancel_Ability", objCancelAbility},
    {"Force_Ability_Recharge", objForceAbilityRecharge},
    {"Reset_Ability_Counter", objResetAbilityCounter},
};

const MethodEntry kPlayerMethods[] = {
    {"Get_ID", playerGetId},
    {"Get_Name", playerGetName},
    {"Get_Faction_Name", playerGetFactionName},
    {"Get_Difficulty", playerGetDifficulty},
    {"Is_Human", playerIsHuman},
    {"Get_Tech_Level", playerGetTechLevel},
    {"Get_Credits", playerGetCredits},
    {"Make_Ally", playerMakeAlly},
    {"Make_Enemy", playerMakeEnemy},
    {"Is_Ally", playerIsAlly},
    {"Is_Enemy", playerIsEnemy},
    {"Get_Enemy", playerGetEnemy},
    {"Give_Money", playerGiveMoney},
    {"Set_Tech_Level", playerSetTechLevel},
    {"Unlock_Tech", playerUnlockTech},
    {"Lock_Tech", playerLockTech},
};

const MethodEntry kTypeMethods[] = {
    {"Get_Name", typeGetName},
    {"Is_Hero", typeIsHero},
    {"Get_Build_Cost", typeGetBuildCost},
    {"Get_Tech_Level", typeGetTechLevel},
    {"Get_Max_Range", typeGetMaxRange},
    {"Get_Min_Range", typeGetMinRange},
    {"Is_Affected_By_Missile_Shield", typeIsAffectedByMissileShield},
    {"Is_Affected_By_Laser_Defense", typeIsAffectedByLaserDefense},
};

const MethodEntry kPositionMethods[] = {
    {"Get_XYZ", posGetXYZ},
};

const MethodEntry kCommandMethods[] = {
    {"IsFinished", cmdIsFinished},
    {"Result", cmdResult},
};

const MethodEntry* methodsFor(WrapperKind kind) {
    switch (kind) {
        case WrapperKind::Object: return kObjectMethods;
        case WrapperKind::Player: return kPlayerMethods;
        case WrapperKind::Type: return kTypeMethods;
        case WrapperKind::Position: return kPositionMethods;
        case WrapperKind::Command: return kCommandMethods;
    }
    return nullptr;
}

int methodCountFor(WrapperKind kind) {
    switch (kind) {
        case WrapperKind::Object: return static_cast<int>(sizeof(kObjectMethods) / sizeof(kObjectMethods[0]));
        case WrapperKind::Player: return static_cast<int>(sizeof(kPlayerMethods) / sizeof(kPlayerMethods[0]));
        case WrapperKind::Type: return static_cast<int>(sizeof(kTypeMethods) / sizeof(kTypeMethods[0]));
        case WrapperKind::Position: return static_cast<int>(sizeof(kPositionMethods) / sizeof(kPositionMethods[0]));
        case WrapperKind::Command: return static_cast<int>(sizeof(kCommandMethods) / sizeof(kCommandMethods[0]));
    }
    return 0;
}

int wrapperIndex(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const char* key = luaL_checkstring(s, 2);
    const MethodEntry* methods = methodsFor(w->kind);
    int count = methodCountFor(w->kind);
    for (int i = 0; i < count; ++i) {
        if (std::strcmp(methods[i].name, key) == 0) {
            lua_pushcfunction(s, methods[i].fn);
            return 1;
        }
    }
    // Extended method tables (taskforce bindings register into
    // __PgWrapperMethods[kind][name]).
    lua_getfield(s, LUA_REGISTRYINDEX, "__PgWrapperMethods");
    if (lua_istable(s, -1)) {
        lua_pushinteger(s, static_cast<int>(w->kind));
        lua_gettable(s, -2);               // [methods, kindTable]
        if (lua_istable(s, -1)) {
            lua_pushstring(s, key);
            lua_gettable(s, -2);           // [methods, kindTable, fn]
            if (lua_isfunction(s, -1)) {
                lua_remove(s, -2);
                lua_remove(s, -2);
                return 1;
            }
            lua_pop(s, 1);                 // drop nil fn
        }
        lua_pop(s, 1);                     // drop kindTable
    }
    lua_pop(s, 1);                         // drop methods
    lua_pushnil(s);
    return 1;
}

int wrapperToString(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    switch (w->kind) {
        case WrapperKind::Object:
            lua_pushfstring(s, "GameObjectWrapper(%d)", w->id);
            break;
        case WrapperKind::Player:
            lua_pushfstring(s, "PlayerWrapper(%d)", w->id);
            break;
        case WrapperKind::Type:
            lua_pushfstring(s, "GameObjectTypeWrapper(%d)", w->id);
            break;
        case WrapperKind::Position:
            lua_pushfstring(s, "PositionWrapper");
            break;
        case WrapperKind::Command:
            lua_pushfstring(s, "CommandBlock");
            break;
        case WrapperKind::TaskForce:
            lua_pushfstring(s, "TaskForce(%d)", w->id);
            break;
        case WrapperKind::Planet:
            lua_pushfstring(s, "Planet(%d)", w->id);
            break;
    }
    return 1;
}

// ---- registration --------------------------------------------------------

void reg(lua_State* s, const char* name, lua_CFunction fn) {
    lua_register(s, name, fn);
}

} // namespace

void registerObjectBindings(LuaHost& lua, SimState& sim) {
    lua_State* s = lua.state();

    // Shared wrapper metatable.
    if (luaL_newmetatable(s, kWrapperMeta)) {
        lua_pushcfunction(s, wrapperIndex);
        lua_setfield(s, -2, "__index");
        lua_pushcfunction(s, wrapperToString);
        lua_setfield(s, -2, "__tostring");
    }
    lua_pop(s, 1);

    // Type id tables (name <-> id) and the type counter.
    lua_newtable(s);
    lua_setfield(s, LUA_REGISTRYINDEX, "__PgTypeIds");
    lua_newtable(s);
    lua_setfield(s, LUA_REGISTRYINDEX, "__PgTypeNames");
    lua_pushinteger(s, 0);
    lua_setfield(s, LUA_REGISTRYINDEX, "__PgTypeCounter");

    // Seed the type id tables from the sim's registered types.
    int id = 0;
    for (const std::string& name : sim.typeNames()) {
        ++id;
        lua_getfield(s, LUA_REGISTRYINDEX, "__PgTypeIds");
        lua_pushstring(s, name.c_str());
        lua_pushinteger(s, id);
        lua_settable(s, -3);
        lua_pop(s, 1);
        lua_getfield(s, LUA_REGISTRYINDEX, "__PgTypeNames");
        lua_pushinteger(s, id);
        lua_pushstring(s, name.c_str());
        lua_settable(s, -3);
        lua_pop(s, 1);
    }
    lua_pushinteger(s, id);
    lua_setfield(s, LUA_REGISTRYINDEX, "__PgTypeCounter");

    // Global queries. Each gets the sim as a lightuserdata upvalue.
    lua_pushlightuserdata(s, &sim);
    lua_pushcclosure(s, findPlayer, 1);
    lua_setglobal(s, "Find_Player");
    lua_pushlightuserdata(s, &sim);
    lua_pushcclosure(s, findObjectType, 1);
    lua_setglobal(s, "Find_Object_Type");
    lua_pushlightuserdata(s, &sim);
    lua_pushcclosure(s, findAllObjectsOfType, 1);
    lua_setglobal(s, "Find_All_Objects_Of_Type");
    lua_pushlightuserdata(s, &sim);
    lua_pushcclosure(s, findFirstObject, 1);
    lua_setglobal(s, "Find_First_Object");
    lua_pushlightuserdata(s, &sim);
    lua_pushcclosure(s, findNearest, 1);
    lua_setglobal(s, "Find_Nearest");

    // Spawning.
    lua_pushlightuserdata(s, &sim);
    lua_pushcclosure(s, createPosition, 1);
    lua_setglobal(s, "Create_Position");
    lua_pushlightuserdata(s, &sim);
    lua_pushcclosure(s, spawnUnit, 1);
    lua_setglobal(s, "Spawn_Unit");
    lua_pushlightuserdata(s, &sim);
    lua_pushcclosure(s, reinforceUnit, 1);
    lua_setglobal(s, "Reinforce_Unit");
}

} // namespace eaw
