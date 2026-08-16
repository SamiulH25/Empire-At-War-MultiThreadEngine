// Typed loader for GameConstants.xml — the game's global tuning knobs.
//
// We model the performance-relevant settings (pathfinding budgets, collision
// check spacing) plus a generic pass-through for everything else, since mods
// may add arbitrary tags that the engine must tolerate.
#pragma once

#include "core/xml.h"

#include <map>
#include <optional>
#include <string>

namespace eaw {

struct GameConstants {
    // Pathfinding / movement (confirmed present in the 64-bit remaster)
    int spacePathfindMaxExpansions = 0;
    int spacePathfindFrameDelayDelta = 0;
    double spacePathFailureMaxExpansionsCoefficient = 0.0;
    int framesPerCollisionCheck = 0;

    // Generic: every other tag, keyed by name (first occurrence wins; mods may
    // duplicate tags, so we keep all values for the repeated ones we care about).
    std::map<std::string, std::string> values;

    // Parses GameConstants.xml (root <GameConstants>). Throws XmlError.
    static GameConstants Parse(const std::string& xmlText);
};

} // namespace eaw
