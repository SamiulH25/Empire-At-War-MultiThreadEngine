// UnitDataLoader — parses the game's unit XML into ObjectType stats.
//
// The game ships unit definitions as XML (config.meg: SPACEUNITSFIGHTERS.XML,
// SPACEUNITSCAPITAL.XML, UNITS_SPACE_*.XML, and land equivalents). The combat
// surface we model maps onto these tags:
//   <SpaceUnit Name="A-Wing"> / <LandUnit Name="...">   the unit block
//   <Tactical_Health>          raw hull points
//   <Shield_Points>            raw shield points
//   <Damage>                   per-shot damage points
//   <Projectile_Fire_Recharge_Seconds>  seconds between shots
//   <Targeting_Max_Attack_Distance>     weapon range
//   <Max_Speed>                movement speed
//   <Build_Cost_Credits>       cost
//   <Affiliation>              owning faction
//   <CategoryMask>             pipe-separated categories ("Fighter | Capital")
//   <Property_Flags>           pipe/space-separated properties
//   <Ship_Class>               fighter/capital/corvette/...
//   <Is_Hero>                  hero flag
//
// Our sim uses normalized hull/shield (0..1). The loader keeps the raw game
// numbers on ObjectType (hull/shield/damage all raw) — damage and health
// share a scale, so the sim's fractions work out (7 dmg vs 50 health = 14%
// of hull per hit).
#pragma once

#include "core/object_model.h"
#include "core/xml.h"

#include <string>
#include <vector>

namespace eaw {

class UnitDataLoader {
public:
    // Parses every <SpaceUnit>/<LandUnit>/<GroundVehicle> block from the XML
    // text into ObjectTypes. Later definitions of the same name replace
    // earlier ones. Returns the types in document order.
    std::vector<ObjectType> loadXml(const std::string& xmlText);

    // Parses every <Planet Name="..."> block into Planet records.
    // `creditValue` becomes the planet's income contribution; the position
    // is derived from the galaxy layout (spiral-ish ring around the origin)
    // since the game's PLANETS.XML has no coordinates.
    std::vector<Planet> loadPlanets(const std::string& xmlText);

private:
    void loadUnitNode(const XmlNode& unit, bool space,
                      std::vector<ObjectType>& out);
};

} // namespace eaw
