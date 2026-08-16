// Tests for the ability system (cooldowns, targeting, bindings).
#include "core/simulation.h"
#include "core/unit_data_loader.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace {

int failures = 0;
void check(bool c, const char* w) {
    std::printf("%s: %s\n", c ? "ok" : "FAIL", w);
    if (!c) ++failures;
}

struct AbilityFixture {
    std::unique_ptr<eaw::Simulation> sim;
    int rebelId = 0;
    int empireId = 0;
    int xwingId = 0;
    int isdId = 0;

    AbilityFixture() {
        sim = std::make_unique<eaw::Simulation>(4);
        eaw::Player& rebel = sim->sim().addPlayer("Rebel Alliance", "REBEL");
        eaw::Player& empire = sim->sim().addPlayer("Galactic Empire", "EMPIRE");
        rebelId = rebel.id;
        empireId = empire.id;
        eaw::ObjectType xwing;
        xwing.name = "X-Wing";
        xwing.properties = {"Unit"};
        xwing.damage = 0.01;
        xwing.attackRate = 1.0;
        xwing.maxRange = 300;
        eaw::ObjectType::Ability torpedo;
        torpedo.name = "Torpedo_Attack";
        torpedo.cooldown = 5.0;
        torpedo.range = 400.0;
        torpedo.damage = 0.5;
        torpedo.requiresTarget = true;
        xwing.abilities.push_back(torpedo);
        sim->sim().addType(std::move(xwing));
        eaw::ObjectType isd;
        isd.name = "ISD";
        isd.properties = {"Unit"};
        isd.damage = 0.0;
        isd.maxRange = 500;
        sim->sim().addType(std::move(isd));
        xwingId = sim->sim().spawnUnit("X-Wing", rebelId, {0, 0, 0});
        isdId = sim->sim().spawnUnit("ISD", empireId, {100, 0, 0});
    }
};

void testHasAndReady() {
    AbilityFixture fx;
    check(fx.sim->sim().hasAbility(fx.xwingId, "Torpedo_Attack"), "has ability");
    check(!fx.sim->sim().hasAbility(fx.xwingId, "Nope"), "unknown ability absent");
    check(fx.sim->sim().isAbilityReady(fx.xwingId, "Torpedo_Attack"),
          "ability ready initially");
}

void testActivateDealsDamage() {
    AbilityFixture fx;
    const eaw::GameObject* isd = fx.sim->sim().object(fx.isdId);
    double before = isd->hull;
    bool ok = fx.sim->sim().activateAbility(fx.xwingId, "Torpedo_Attack", fx.isdId);
    check(ok, "ability activates on enemy in range");
    check(fx.sim->sim().object(fx.isdId)->hull < before, "ability dealt damage");
    // Now on cooldown.
    check(!fx.sim->sim().isAbilityReady(fx.xwingId, "Torpedo_Attack"),
          "ability on cooldown after use");
    bool again = fx.sim->sim().activateAbility(fx.xwingId, "Torpedo_Attack", fx.isdId);
    check(!again, "ability blocked on cooldown");
}

void testCooldownRecovers() {
    AbilityFixture fx;
    fx.sim->sim().activateAbility(fx.xwingId, "Torpedo_Attack", fx.isdId);
    check(!fx.sim->sim().isAbilityReady(fx.xwingId, "Torpedo_Attack"), "not ready");
    // 6s of ticks > 5s cooldown.
    for (int i = 0; i < 180; ++i) fx.sim->tick(1.0 / 30.0);
    check(fx.sim->sim().isAbilityReady(fx.xwingId, "Torpedo_Attack"),
          "cooldown recovers after ticks");
}

void testRangeAndFriendly() {
    AbilityFixture fx;
    // Out of range.
    fx.sim->sim().object(fx.isdId)->position = {10000, 0, 0};
    bool far = fx.sim->sim().activateAbility(fx.xwingId, "Torpedo_Attack", fx.isdId);
    check(!far, "ability blocked out of range");
    // Friendly target blocked.
    fx.sim->sim().object(fx.isdId)->position = {100, 0, 0};
    fx.sim->sim().object(fx.isdId)->playerId = fx.rebelId;
    bool friendly = fx.sim->sim().activateAbility(fx.xwingId, "Torpedo_Attack", fx.isdId);
    check(!friendly, "ability blocked on friendly target");
}

void testBindings() {
    AbilityFixture fx;
    fx.sim->scripts().runScript(
        "x = Find_First_Object('X-Wing')\n"
        "t = Find_First_Object('ISD')\n"
        "has = x:Has_Ability('Torpedo_Attack')\n"
        "ready = x:Is_Ability_Ready('Torpedo_Attack')\n"
        "ok = x:Activate_Ability('Torpedo_Attack', t)\n"
        "active = x:Is_Ability_Active('Torpedo_Attack')\n"
        "ready2 = x:Is_Ability_Ready('Torpedo_Attack')\n"
        "ok2 = x:Try_Ability('Torpedo_Attack', t)\n");
    lua_getglobal(fx.sim->scripts().state(), "has");
    check(lua_toboolean(fx.sim->scripts().state(), -1) == 1, "Has_Ability binding");
    lua_pop(fx.sim->scripts().state(), 1);
    lua_getglobal(fx.sim->scripts().state(), "ready");
    check(lua_toboolean(fx.sim->scripts().state(), -1) == 1, "Is_Ability_Ready initial");
    lua_pop(fx.sim->scripts().state(), 1);
    lua_getglobal(fx.sim->scripts().state(), "ok");
    check(lua_toboolean(fx.sim->scripts().state(), -1) == 1, "Activate_Ability binding");
    lua_pop(fx.sim->scripts().state(), 1);
    lua_getglobal(fx.sim->scripts().state(), "active");
    check(lua_toboolean(fx.sim->scripts().state(), -1) == 1, "Is_Ability_Active after use");
    lua_pop(fx.sim->scripts().state(), 1);
    lua_getglobal(fx.sim->scripts().state(), "ready2");
    check(lua_toboolean(fx.sim->scripts().state(), -1) == 0, "not ready after use");
    lua_pop(fx.sim->scripts().state(), 1);
    lua_getglobal(fx.sim->scripts().state(), "ok2");
    check(lua_toboolean(fx.sim->scripts().state(), -1) == 0, "Try_Ability fails on cooldown");
    lua_pop(fx.sim->scripts().state(), 1);
    // Force_Ability_Recharge + Cancel_Ability.
    fx.sim->scripts().runScript(
        "x:Force_Ability_Recharge('Torpedo_Attack')\n"
        "r = x:Is_Ability_Ready('Torpedo_Attack')\n"
        "x:Cancel_Ability('Torpedo_Attack')\n"
        "a = x:Is_Ability_Active('Torpedo_Attack')\n");
    lua_getglobal(fx.sim->scripts().state(), "r");
    check(lua_toboolean(fx.sim->scripts().state(), -1) == 1, "Force_Ability_Recharge");
    lua_pop(fx.sim->scripts().state(), 1);
    lua_getglobal(fx.sim->scripts().state(), "a");
    check(lua_toboolean(fx.sim->scripts().state(), -1) == 0, "Cancel_Ability");
    lua_pop(fx.sim->scripts().state(), 1);
}

void testLoaderParsesAbilities() {
    // The game's unit XML ability block must load into ObjectType.
    const char* xml = R"(
<FighterUnits>
	<SpaceUnit Name="LURE_Wing">
		<Tactical_Health>50</Tactical_Health>
		<Damage>7</Damage>
		<Targeting_Max_Attack_Distance>400</Targeting_Max_Attack_Distance>
		<Unit_Abilities_Data>
			<Unit_Ability>
				<Type>LURE</Type>
				<Effective_Radius>350</Effective_Radius>
				<Recharge_Seconds>45</Recharge_Seconds>
			</Unit_Ability>
			<Unit_Ability>
				<Type>SPEED_BOOST</Type>
				<Recharge_Seconds>20</Recharge_Seconds>
			</Unit_Ability>
		</Unit_Abilities_Data>
	</SpaceUnit>
</FighterUnits>
)";
    eaw::UnitDataLoader loader;
    auto types = loader.loadXml(xml);
    check(types.size() == 1, "unit loaded");
    check(types[0].abilities.size() == 2, "two abilities parsed");
    if (types[0].abilities.size() == 2) {
        check(types[0].abilities[0].name == "LURE", "ability name");
        check(types[0].abilities[0].cooldown == 45.0, "ability cooldown");
        check(types[0].abilities[0].range == 350.0, "ability range");
        check(types[0].abilities[1].name == "SPEED_BOOST", "second ability");
    }
}

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    testHasAndReady();
    testActivateDealsDamage();
    testCooldownRecovers();
    testRangeAndFriendly();
    testBindings();
    testLoaderParsesAbilities();
    if (failures == 0) { std::printf("ALL TESTS PASSED\n"); return 0; }
    std::printf("%d FAILURES\n", failures);
    return 1;
}
