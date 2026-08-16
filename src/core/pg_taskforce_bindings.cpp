#include "core/pg_taskforce_bindings.h"

#include "core/lua_wrappers.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

namespace eaw {

namespace {

// Pushes a taskforce wrapper for the given force id (nil if unknown).
void pushTaskForce(lua_State* s, SimState* sim, const TaskForce* f) {
    if (!f) { lua_pushnil(s); return; }
    pushWrapper(s, sim, WrapperKind::TaskForce, f->id);
}

const TaskForce* wrapperForce(lua_State* s, Wrapper* w) {
    if (w->kind != WrapperKind::TaskForce) return nullptr;
    return w->sim->taskForce(w->id);
}

// ---- queries -------------------------------------------------------------

int tfGetUnitTable(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const TaskForce* f = wrapperForce(s, w);
    if (!f) { lua_pushnil(s); return 1; }
    lua_createtable(s, static_cast<int>(f->units.size()), 0);
    int n = 1;
    for (int id : f->units) {
        pushObject(s, w->sim, w->sim->object(id));
        lua_rawseti(s, -2, n++);
    }
    return 1;
}

int tfGetForceCount(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const TaskForce* f = wrapperForce(s, w);
    if (!f) { lua_pushinteger(s, 0); return 1; }
    // Live count: units that are still alive.
    int n = 0;
    for (int id : f->units) {
        const GameObject* o = w->sim->object(id);
        if (o && o->alive) ++n;
    }
    lua_pushinteger(s, n);
    return 1;
}

int tfGetGoalTypeName(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const TaskForce* f = wrapperForce(s, w);
    if (!f) { lua_pushnil(s); return 1; }
    lua_pushstring(s, f->name.c_str());
    return 1;
}

int tfGetStage(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const TaskForce* f = wrapperForce(s, w);
    if (!f) { lua_pushinteger(s, 0); return 1; }
    lua_pushinteger(s, f->stage);
    return 1;
}

int tfSetStage(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    TaskForce* f = w->sim->taskForce(w->id);
    if (f) f->stage = static_cast<int>(luaL_checkinteger(s, 2));
    return 0;
}

int tfSetPlanResult(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    TaskForce* f = w->sim->taskForce(w->id);
    if (f) f->planResult = lua_toboolean(s, 2) != 0;
    return 0;
}

int tfSetAsGoalSystemRemovable(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    TaskForce* f = w->sim->taskForce(w->id);
    if (f) f->goalSystemRemovable = lua_toboolean(s, 2) != 0;
    return 0;
}

int tfAreAllUnitsOnFreeStore(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const TaskForce* f = wrapperForce(s, w);
    if (!f) { lua_pushboolean(s, 0); return 1; }
    // All units must still belong to the force and be alive (the sim has no
    // separate free-store pool; a unit on the "free store" is just unassigned,
    // so every force member counts as assigned).
    bool all = true;
    for (int id : f->units) {
        const GameObject* o = w->sim->object(id);
        if (!o || !o->alive) { all = false; break; }
    }
    lua_pushboolean(s, all);
    return 1;
}

int tfGetSelfThreatSum(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    const TaskForce* f = wrapperForce(s, w);
    if (!f) { lua_pushnumber(s, 0.0); return 1; }
    lua_pushnumber(s, w->sim->forceThreat(f->id));
    return 1;
}

int tfAddForce(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    Wrapper* unit = checkWrapper(s, 2);
    if (unit->kind != WrapperKind::Object) {
        return luaL_error(s, "Add_Force: expected object");
    }
    w->sim->addUnitToForce(w->id, unit->id);
    return 0;
}

int tfReleaseUnit(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    Wrapper* unit = checkWrapper(s, 2);
    if (unit->kind != WrapperKind::Object) return 0;
    w->sim->removeUnitFromForce(w->id, unit->id);
    return 0;
}

// ---- collective orders (fan out to the force's units) --------------------

int tfMoveTo(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    TaskForce* f = w->sim->taskForce(w->id);
    if (!f) { lua_pushnil(s); return 1; }
    Vec3 target;
    if (!targetPosition(s, 2, target)) { lua_pushnil(s); return 1; }
    for (int id : f->units) {
        GameObject* o = w->sim->object(id);
        if (o && o->alive) {
            o->hasMoveTarget = true;
            o->moveTarget = target;
            o->path.clear();
            o->pathIndex = 0;
            o->pathSearchId = 0;
        }
    }
    pushCommandBlock(s, w->sim, 0);
    return 1;
}

int tfAttackTarget(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    TaskForce* f = w->sim->taskForce(w->id);
    if (!f || !lua_isuserdata(s, 2)) { lua_pushnil(s); return 1; }
    Wrapper* t = checkWrapper(s, 2);
    if (t->kind != WrapperKind::Object) { lua_pushnil(s); return 1; }
    for (int id : f->units) {
        GameObject* o = w->sim->object(id);
        if (o && o->alive) o->attackTargetId = t->id;
    }
    pushCommandBlock(s, w->sim, 0);
    return 1;
}

int tfGarrison(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    TaskForce* f = w->sim->taskForce(w->id);
    if (!f || !lua_isuserdata(s, 2)) { lua_pushboolean(s, 0); return 1; }
    Wrapper* t = checkWrapper(s, 2);
    if (t->kind != WrapperKind::Object) { lua_pushboolean(s, 0); return 1; }
    bool all = true;
    for (int id : f->units) {
        GameObject* o = w->sim->object(id);
        if (o && o->alive) {
            if (!w->sim->garrisonUnit(id, t->id)) all = false;
        }
    }
    lua_pushboolean(s, all);
    return 1;
}

int tfLeaveGarrison(lua_State* s) {
    Wrapper* w = checkWrapper(s, 1);
    TaskForce* f = w->sim->taskForce(w->id);
    if (f) {
        for (int id : f->units) w->sim->ungarrisonUnit(id);
    }
    return 0;
}

// ---- method table --------------------------------------------------------

const struct { const char* name; lua_CFunction fn; } kTfMethods[] = {
    {"Get_Unit_Table", tfGetUnitTable},
    {"Get_Force_Count", tfGetForceCount},
    {"Get_Goal_Type_Name", tfGetGoalTypeName},
    {"Get_Stage", tfGetStage},
    {"Set_Stage", tfSetStage},
    {"Set_Plan_Result", tfSetPlanResult},
    {"Set_As_Goal_System_Removable", tfSetAsGoalSystemRemovable},
    {"Are_All_Units_On_Free_Store", tfAreAllUnitsOnFreeStore},
    {"Get_Self_Threat_Sum", tfGetSelfThreatSum},
    {"Add_Force", tfAddForce},
    {"Release_Unit", tfReleaseUnit},
    {"Move_To", tfMoveTo},
    {"Attack_Target", tfAttackTarget},
    {"Garrison", tfGarrison},
    {"Leave_Garrison", tfLeaveGarrison},
};

} // namespace

void registerTaskForceBindings(LuaHost& lua, SimState& sim) {
    lua_State* s = lua.state();
    (void)sim;

    // Add the taskforce methods to the shared wrapper metatable's __index.
    // The metatable already exists (created by registerObjectBindings);
    // we extend the dispatch by registering a per-kind lookup table. The
    // wrapperIndex function is in pg_object_bindings; to keep dispatch in
    // one place, we store the methods in a registry table keyed by kind and
    // let that module's __index check it.
    lua_getfield(s, LUA_REGISTRYINDEX, "__PgWrapperMethods");
    if (lua_isnil(s, -1)) {
        lua_pop(s, 1);
        lua_newtable(s);                 // kind -> { name -> fn }
        lua_pushinteger(s, 6);           // TaskForce kind
        lua_newtable(s);
        for (const auto& m : kTfMethods) {
            lua_pushcfunction(s, m.fn);
            lua_setfield(s, -2, m.name);
        }
        lua_settable(s, -3);
        lua_setfield(s, LUA_REGISTRYINDEX, "__PgWrapperMethods");
    } else {
        lua_pop(s, 1);
    }
}

} // namespace eaw
