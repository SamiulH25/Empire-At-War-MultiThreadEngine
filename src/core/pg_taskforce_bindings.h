// Taskforce Lua bindings — the AI unit-group surface (PGTASKFORCE).
//
// Registers the documented TaskForceClass methods (Alamo Engine Tools):
// unit table / force count, adding and releasing units, stage + plan
// result, goal-system removability, threat sum, and collective orders that
// fan out to the force's units (Move_To / Attack_Target / Garrison).
//
// Taskforces are created engine-side (SimState::addTaskForce); scripts
// receive them via plan hooks and query/order them through these methods.
#pragma once

#include "core/lua_host.h"
#include "core/object_model.h"

namespace eaw {

// Registers the taskforce binding surface into a Lua state. `sim` must
// outlive the state.
void registerTaskForceBindings(LuaHost& lua, SimState& sim);

} // namespace eaw
