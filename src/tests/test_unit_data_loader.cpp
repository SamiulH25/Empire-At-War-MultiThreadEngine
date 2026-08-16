// Tests for the unit XML data loader (game unit stats -> ObjectType).
#include "core/object_model.h"
#include "core/unit_data_loader.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;
void check(bool c, const char* w) {
    std::printf("%s: %s\n", c ? "ok" : "FAIL", w);
    if (!c) ++failures;
}

// A minimal but schema-faithful snippet (tag names match the game's XML).
const char* kFighterXml = R"(
<?xml version="1.0"?>
<FighterUnits>
	<SpaceUnit Name="A-Wing">
		<Max_Speed>5.2</Max_Speed>
		<Shield_Points>30</Shield_Points>
		<Tactical_Health>50</Tactical_Health>
		<Affiliation>Rebel</Affiliation>
		<Build_Cost_Credits>60</Build_Cost_Credits>
		<Ship_Class>fighter</Ship_Class>
		<Damage>7</Damage>
		<Projectile_Fire_Recharge_Seconds>1.0</Projectile_Fire_Recharge_Seconds>
		<Targeting_Max_Attack_Distance>500.0</Targeting_Max_Attack_Distance>
		<CategoryMask> Fighter | AntiBomber </CategoryMask>
		<Property_Flags> SmallShip </Property_Flags>
		<Is_Hero>no</Is_Hero>
	</SpaceUnit>
	<SpaceUnit Name="TIE_Fighter">
		<Max_Speed>5.5</Max_Speed>
		<Shield_Points>0</Shield_Points>
		<Tactical_Health>40</Tactical_Health>
		<Affiliation>Empire</Affiliation>
		<Build_Cost_Credits>55</Build_Cost_Credits>
		<Ship_Class>fighter</Ship_Class>
		<Damage>8</Damage>
		<Projectile_Fire_Recharge_Seconds>0.9</Projectile_Fire_Recharge_Seconds>
		<Targeting_Max_Attack_Distance>480.0</Targeting_Max_Attack_Distance>
		<CategoryMask>Fighter</CategoryMask>
		<Property_Flags>SmallShip</Property_Flags>
	</SpaceUnit>
</FighterUnits>
)";

void testLoadsUnits() {
    eaw::UnitDataLoader loader;
    auto types = loader.loadXml(kFighterXml);
    check(types.size() == 2, "loads both fighters");
    check(types[0].name == "A-Wing", "first unit name");
    check(types[1].name == "TIE_Fighter", "second unit name");
}

void testStats() {
    eaw::UnitDataLoader loader;
    auto types = loader.loadXml(kFighterXml);
    const eaw::ObjectType& a = types[0];
    // Damage normalized: 7 dmg / 50 health = 0.14
    check(a.damage > 0.139 && a.damage < 0.141, "damage normalized to health fraction");
    // Shield: 30 / 50 = 0.6
    check(a.shieldFraction > 0.59 && a.shieldFraction < 0.61, "shield fraction");
    // Fire rate: 1.0s recharge -> 1 shot/s
    check(a.attackRate > 0.99 && a.attackRate < 1.01, "attack rate from recharge");
    // Range
    check(a.maxRange == 500.0, "max range");
    // Cost / tech
    check(a.buildCost == 60.0, "build cost");
    // Categories
    check(a.categories.size() == 3, "categories incl ship class");
    bool hasFighter = false, hasAntiBomber = false, hasShipClass = false;
    for (const auto& c : a.categories) {
        if (c == "Fighter") hasFighter = true;
        if (c == "AntiBomber") hasAntiBomber = true;
        if (c == "fighter") hasShipClass = true;
    }
    check(hasFighter && hasAntiBomber && hasShipClass, "categories parsed");
    // Properties
    check(a.properties.size() == 1 && a.properties[0] == "SmallShip", "properties parsed");
    // Affiliation
    check(a.affiliatedFactions.size() == 1 && a.affiliatedFactions[0] == "Rebel",
          "affiliation parsed");
    // Move speed
    check(a.moveSpeed > 5.19 && a.moveSpeed < 5.21, "move speed parsed");
    // Not a hero
    check(!a.hero, "hero flag false");
}

void testTieStats() {
    eaw::UnitDataLoader loader;
    auto types = loader.loadXml(kFighterXml);
    const eaw::ObjectType& tie = types[1];
    // No shield: 0/40 = 0
    check(tie.shieldFraction == 0.0, "no shield for unshielded unit");
    // 8 dmg / 40 health = 0.2
    check(tie.damage > 0.199 && tie.damage < 0.201, "tie damage fraction");
    // Recharge 0.9 -> ~1.11 shots/s
    check(tie.attackRate > 1.10 && tie.attackRate < 1.12, "tie attack rate");
    check(tie.affiliatedFactions[0] == "Empire", "tie affiliation");
}

void testLoadsIntoSim() {
    eaw::UnitDataLoader loader;
    auto types = loader.loadXml(kFighterXml);
    eaw::SimState sim;
    eaw::Player& rebel = sim.addPlayer("Rebel Alliance", "REBEL");
    for (auto& t : types) sim.addType(std::move(t));
    int id = sim.spawnUnit("A-Wing", rebel.id, {0, 0, 0});
    const eaw::GameObject* o = sim.object(id);
    check(o != nullptr, "unit spawns from loaded type");
    if (o) {
        check(o->shield > 0.59 && o->shield < 0.61, "spawn applies shield fraction");
        check(o->moveSpeed > 5.19 && o->moveSpeed < 5.21, "spawn applies move speed");
    }
}

void testDuplicateReplaced() {
    // Two definitions of the same name: the later one wins.
    const char* dupXml = R"(
<FighterUnits>
	<SpaceUnit Name="X">
		<Tactical_Health>50</Tactical_Health>
		<Damage>5</Damage>
		<Ship_Class>fighter</Ship_Class>
	</SpaceUnit>
	<SpaceUnit Name="X">
		<Tactical_Health>100</Tactical_Health>
		<Damage>20</Damage>
		<Ship_Class>fighter</Ship_Class>
	</SpaceUnit>
</FighterUnits>
)";
    eaw::UnitDataLoader loader;
    auto types = loader.loadXml(dupXml);
    check(types.size() == 1, "duplicate name collapses to one type");
    check(types[0].damage > 0.199 && types[0].damage < 0.201,
          "later definition wins (20/100)");
}

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    testLoadsUnits();
    testStats();
    testTieStats();
    testLoadsIntoSim();
    testDuplicateReplaced();
    if (failures == 0) { std::printf("ALL TESTS PASSED\n"); return 0; }
    std::printf("%d FAILURES\n", failures);
    return 1;
}
