// Fog-of-war Lua bindings — the FogOfWar table surface.
//
// Registers the documented FogOfWar commands (Alamo Engine Tools):
//   FogOfWar.Reveal(player, position, radius)   — permanent reveal
//   FogOfWar.Temporary_Reveal(player, pos, r)   — same (no timed expiry tier)
//   FogOfWar.Reveal_All(player)                 — everything visible
//   FogOfWar.Disable_Rendering(bool)            — rendering hint (no-op here)
#pragma once

#include "core/lua_host.h"
#include "core/object_model.h"

namespace eaw {

void registerFogBindings(LuaHost& lua, SimState& sim);

} // namespace eaw
