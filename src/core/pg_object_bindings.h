// Object-model Lua bindings — the game-object query surface.
//
// Registers the documented engine bindings (Alamo Engine Tools) that read
// the sim: Find_Player / Find_Object_Type / Find_All_Objects_Of_Type /
// Find_Nearest, plus wrapper userdata methods (Get_Hull, Get_Owner, ...)
// resolved through the shared LuaWrapperMetaTable.
//
// The wrappers are userdata holding a (kind, id) pair into a SimState that
// must outlive the Lua state.
#pragma once

#include "core/lua_host.h"
#include "core/object_model.h"

namespace eaw {

// Registers the object-model binding surface into a Lua state. `sim` must
// outlive the state.
void registerObjectBindings(LuaHost& lua, SimState& sim);

} // namespace eaw
