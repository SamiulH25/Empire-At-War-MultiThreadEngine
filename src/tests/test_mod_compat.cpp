// Tests for the mod-compat path: loose-file override + mod unit loading.
#include "core/meg_file.h"
#include "core/meg_manager.h"
#include "core/simulation.h"
#include "core/unit_data_loader.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int failures = 0;
void check(bool c, const char* w) {
    std::printf("%s: %s\n", c ? "ok" : "FAIL", w);
    if (!c) ++failures;
}

// Minimal synthetic meg with one file (same layout as test_meg_manager).
std::vector<uint8_t> makeMeg(const std::string& name, const std::string& content) {
    std::vector<uint8_t> b;
    auto put16 = [&](uint16_t v) { b.push_back(v & 0xff); b.push_back(v >> 8); };
    auto put32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) { b.push_back(v & 0xff); v >>= 8; } };
    put32(1); put32(1);
    put16(static_cast<uint16_t>(name.size()));
    b.insert(b.end(), name.begin(), name.end());
    size_t dataStart = 8 + 2 + name.size() + 20;
    put32(0xdeadbeef); put32(0); put32(static_cast<uint32_t>(content.size()));
    put32(static_cast<uint32_t>(dataStart)); put32(0);
    b.insert(b.end(), content.begin(), content.end());
    return b;
}

// A unit XML in the game's schema.
const char* kUnitXml = R"(
<FighterUnits>
	<SpaceUnit Name="A-Wing">
		<Tactical_Health>50</Tactical_Health>
		<Shield_Points>30</Shield_Points>
		<Damage>7</Damage>
		<Projectile_Fire_Recharge_Seconds>1.0</Projectile_Fire_Recharge_Seconds>
		<Targeting_Max_Attack_Distance>500.0</Targeting_Max_Attack_Distance>
		<Max_Speed>5.2</Max_Speed>
		<Build_Cost_Credits>60</Build_Cost_Credits>
		<CategoryMask>Fighter</CategoryMask>
	</SpaceUnit>
</FighterUnits>
)";

void testLooseOverridesMeg() {
    // Base meg has "A-Wing" with damage 5; loose file overrides with damage 7.
    auto baseMeg = makeMeg("DATA\\XML\\SPACEUNITSFIGHTERS.XML",
        "<FighterUnits><SpaceUnit Name=\"A-Wing\"><Tactical_Health>50</Tactical_Health>"
        "<Damage>5</Damage><Targeting_Max_Attack_Distance>400</Targeting_Max_Attack_Distance>"
        "</SpaceUnit></FighterUnits>");
    eaw::MegFile mf = eaw::MegFile::Parse(baseMeg);
    eaw::MegaFileManager files;
    files.addArchive("base.meg", baseMeg, mf);
    check(files.exists("DATA\\XML\\SPACEUNITSFIGHTERS.XML"), "base meg file resolves");

    // Loose override (the mod mechanism).
    files.addLooseFile("DATA\\XML\\SPACEUNITSFIGHTERS.XML",
                       std::vector<uint8_t>(kUnitXml, kUnitXml + std::strlen(kUnitXml)));
    auto bytes = files.read("DATA\\XML\\SPACEUNITSFIGHTERS.XML");
    eaw::UnitDataLoader loader;
    auto types = loader.loadXml(std::string(bytes.begin(), bytes.end()));
    check(types.size() == 1 && types[0].name == "A-Wing", "loose override parsed");
    check(types[0].damage > 0.139 && types[0].damage < 0.141,
          "loose override wins (7/50) over meg (5/50)");
    check(types[0].maxRange == 500.0, "loose override range wins");
}

void testModBattleRuns() {
    // Load mod units into a sim and run a short battle (the mod_tool path).
    eaw::MegaFileManager files;
    files.addLooseFile("DATA\\XML\\SPACEUNITSFIGHTERS.XML",
                       std::vector<uint8_t>(kUnitXml, kUnitXml + std::strlen(kUnitXml)));
    auto bytes = files.read("DATA\\XML\\SPACEUNITSFIGHTERS.XML");
    eaw::UnitDataLoader loader;
    auto types = loader.loadXml(std::string(bytes.begin(), bytes.end()));

    eaw::Simulation sim(4);
    eaw::Player& rebel = sim.sim().addPlayer("Rebel Alliance", "REBEL");
    eaw::Player& empire = sim.sim().addPlayer("Galactic Empire", "EMPIRE");
    for (auto& t : types) sim.sim().addType(std::move(t));
    for (int i = 0; i < 4; ++i) {
        sim.sim().spawnUnit("A-Wing", rebel.id, {static_cast<double>(i) * 5.0, 0, 0});
        sim.sim().spawnUnit("A-Wing", empire.id, {300.0 + i * 5.0, 0, 0});
    }
    sim.scripts().runScript(
        "for i, o in ipairs(Find_All_Objects_Of_Type('A-Wing')) do\n"
        "  o:Attack_Target(Find_First_Object('A-Wing'))\n"
        "  o:Move_To(Find_First_Object('A-Wing'))\n"
        "end\n");
    for (int i = 0; i < 300; ++i) sim.tick(1.0 / 30.0);
    check(sim.totalShots() > 0, "mod-unit battle engages");
    bool someoneDied = false;
    for (const eaw::GameObject* o : sim.sim().allObjects()) {
        if (!o->alive) someoneDied = true;
    }
    check(someoneDied, "mod-unit battle resolves");
}

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    testLooseOverridesMeg();
    testModBattleRuns();
    if (failures == 0) { std::printf("ALL TESTS PASSED\n"); return 0; }
    std::printf("%d FAILURES\n", failures);
    return 1;
}
