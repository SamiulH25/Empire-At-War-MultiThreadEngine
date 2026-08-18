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
#include "core/path_grid.h"
#include "core/perception.h"

namespace eaw {

// Registers the object-model binding surface into a Lua state. `sim` must
// outlive the state.
void registerObjectBindings(LuaHost& lua, SimState& sim);

// Attaches the perception system used by the EvaluatePerception script
// helper. Called by the Simulation after construction (it owns both the
// script manager and the perception system). Null detaches.
void attachScriptPerceptions(const PerceptionSystem* per);

// Attaches the path grid used by the pathing helpers (Find_Path / Get_Path
// / Is_Path_Blocked). Called by the Simulation after construction (it owns
// the grid). Null detaches.
void attachScriptPathGrid(const PathGrid* grid);

} // namespace eaw
