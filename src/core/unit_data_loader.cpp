#include "core/unit_data_loader.h"

#include <cctype>
#include <cstdlib>
#include <sstream>

namespace eaw {

namespace {

double toDouble(const std::string& s) {
    return std::strtod(s.c_str(), nullptr);
}

int toInt(const std::string& s) {
    return std::atoi(s.c_str());
}

// Splits "A | B | C" or "A B C" into trimmed parts.
std::vector<std::string> splitFlags(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    std::istringstream ss(s);
    while (ss >> cur) {
        // strip leading/trailing pipes
        size_t b = cur.find_first_not_of("| \t");
        size_t e = cur.find_last_not_of("| \t");
        if (b == std::string::npos) continue;
        out.push_back(cur.substr(b, e - b + 1));
    }
    return out;
}

bool yesNo(const std::string& s) {
    std::string t;
    for (char c : s) t += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return t == "yes" || t == "true" || t == "1";
}

// Reads the first child named `tag` (or `tag` inside a wrapper) as text.
std::string childText(const XmlNode& node, const std::string& tag) {
    const XmlNode* c = node.firstChild(tag);
    return c ? c->text : std::string();
}

} // namespace

std::vector<ObjectType> UnitDataLoader::loadXml(const std::string& xmlText) {
    std::vector<ObjectType> out;
    XmlNode root = ParseXml(xmlText);
    // The root is the aggregate (e.g. <FighterUnits>, <Juggernaut_Data>);
    // children are units. Land units use <GroundVehicle> (infantry included);
    // space units use <SpaceUnit>; <LandUnit> also accepted.
    for (const XmlNode& unit : root.children) {
        if (unit.name == "SpaceUnit" || unit.name == "LandUnit" ||
            unit.name == "GroundVehicle") {
            loadUnitNode(unit, unit.name == "SpaceUnit", out);
        }
    }
    return out;
}

void UnitDataLoader::loadUnitNode(const XmlNode& unit, bool /*space*/,
                                  std::vector<ObjectType>& out) {
    std::string name = unit.attr("Name");
    if (name.empty()) return;

    ObjectType t;
    t.name = name;
    // The sim uses normalized hull/shield (0..1): damage is stored as a
    // fraction of the shooter's own max health so the 0..1 scale holds
    // (the game's raw damage vs raw health ratio is preserved).
    double health = toDouble(childText(unit, "Tactical_Health"));
    double shield = toDouble(childText(unit, "Shield_Points"));
    t.damage = health > 0.0 ? toDouble(childText(unit, "Damage")) / health : 0.0;
    // Shield fraction is relative to the same health scale (a 30-point
    // shield on 50 health = 0.6 of a hull bar).
    t.shieldFraction = health > 0.0 ? shield / health : 0.0;
    double recharge = toDouble(childText(unit, "Projectile_Fire_Recharge_Seconds"));
    t.attackRate = recharge > 0.0 ? 1.0 / recharge : 1.0;
    t.maxRange = toDouble(childText(unit, "Targeting_Max_Attack_Distance"));
    t.buildCost = toDouble(childText(unit, "Build_Cost_Credits"));
    t.techLevel = toInt(childText(unit, "Required_Star_Base_Level"));
    t.moveSpeed = toDouble(childText(unit, "Max_Speed"));
    // Affiliation: "Rebel" -> REBEL, "Empire" -> EMPIRE, etc.
    std::string aff = childText(unit, "Affiliation");
    if (!aff.empty()) t.affiliatedFactions.push_back(aff);
    // Categories: "Fighter | AntiBomber"
    for (std::string& c : splitFlags(childText(unit, "CategoryMask"))) {
        t.categories.push_back(c);
    }
    // Properties: "SmallShip"
    for (std::string& p : splitFlags(childText(unit, "Property_Flags"))) {
        t.properties.push_back(p);
    }
    std::string shipClass = childText(unit, "Ship_Class");
    if (!shipClass.empty()) t.categories.push_back(shipClass);
    t.hero = yesNo(childText(unit, "Is_Hero"));

    // Replace same-name entries (later defs win).
    for (ObjectType& existing : out) {
        if (existing.name == name) {
            existing = std::move(t);
            return;
        }
    }
    out.push_back(std::move(t));
}

} // namespace eaw
