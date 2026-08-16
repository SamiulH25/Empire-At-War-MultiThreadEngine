// Event Lua bindings — Register_Timer / Register_Death_Event / etc.
//
// Registers the documented event surface (Alamo Engine Tools PGCommands):
// Register_Timer(func, timeout, param), Register_Death_Event(obj, func),
// Register_Attacked_Event(obj, func), Cancel_Attacked_Event(obj),
// Register_Prox(obj, func, range, playerFilter), and the Process_* steps.
//
// The bindings get the EventSystem and SimState as lightuserdata upvalues.
#pragma once

#include "core/event_system.h"
#include "core/lua_host.h"
#include "core/object_model.h"

namespace eaw {

// Registers the event bindings into a Lua state. `events` and `sim` must
// outlive the state.
void registerEventBindings(LuaHost& lua, EventSystem& events, SimState& sim);

} // namespace eaw
