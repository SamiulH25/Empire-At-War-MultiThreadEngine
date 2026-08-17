// Tests for the ModLoader: mod Data mounting, meg loading, loose-file
// override, and discovery-driven unit/planet loading.
#include "core/meg_manager.h"
#include "core/mod_loader.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

int failures = 0;
void check(bool c, const char* w) {
    std::printf("%s: %s\n", c ? "ok" : "FAIL", w);
    if (!c) ++failures;
}

std::string scratch = (fs::temp_directory_path() / "eaw_mod_test").string();

void writeFile(const std::string& path, const std::string& content) {
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream f(path, std::ios::binary);
    f << content;
}

// Builds a tiny mod tree:
//   <root>/Data/megafiles.xml          -> lists test.meg
//   <root>/Data/test.meg               -> contains DATA\XML\UNITS.XML
//   <root>/Data/XML/OVERRIDE.XML       -> loose override (wins over meg)
// Appends a file-table record for `name`+`content` to a hand-built meg.
// `start` is patched in AFTER the record is laid out (it must point at the
// content, which follows the record).
void appendMegFile(std::vector<uint8_t>& meg, const std::string& name,
                   const std::string& content) {
    auto put32 = [&](uint32_t v) {
        meg.push_back(v & 0xFF); meg.push_back((v >> 8) & 0xFF);
        meg.push_back((v >> 16) & 0xFF); meg.push_back((v >> 24) & 0xFF);
    };
    auto put16 = [&](uint16_t v) {
        meg.push_back(v & 0xFF); meg.push_back((v >> 8) & 0xFF);
    };
    uint32_t crc = 0xFFFFFFFFu;
    for (unsigned char ch : name) {
        crc ^= ch;
        for (int k = 0; k < 8; ++k) {
            uint32_t mask = -(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    crc = ~crc;
    put32(crc); put32(0);
    put32(static_cast<uint32_t>(content.size()));
    put32(0); put32(0); // start patched below
    uint32_t start = static_cast<uint32_t>(meg.size());
    // start field: 8 (header) + 2 (len) + name.size() + 12 (crc+index+size).
    std::memcpy(meg.data() + 8 + 2 + name.size() + 12, &start, 4);
    meg.insert(meg.end(), content.begin(), content.end());
}

// Builds a valid single-file meg (format #1) containing `name` -> `content`.
std::string buildMeg(const std::string& name, const std::string& content) {
    std::vector<uint8_t> meg;
    auto put32 = [&](uint32_t v) {
        meg.push_back(v & 0xFF); meg.push_back((v >> 8) & 0xFF);
        meg.push_back((v >> 16) & 0xFF); meg.push_back((v >> 24) & 0xFF);
    };
    auto put16 = [&](uint16_t v) {
        meg.push_back(v & 0xFF); meg.push_back((v >> 8) & 0xFF);
    };
    put32(1); // numFilenames
    put32(1); // numFiles
    put16(static_cast<uint16_t>(name.size()));
    meg.insert(meg.end(), name.begin(), name.end());
    appendMegFile(meg, name, content);
    return std::string(meg.begin(), meg.end());
}

void makeMod(const std::string& root) {
    writeFile(root + "\\Data\\megafiles.xml",
              "<Mega_Files>\n<File> Data\\test.meg </File>\n</Mega_Files>\n");
    // Meg holds the CAPITAL units file; the loose override shadows the
    // FIGHTERS file. Both must load.
    std::string name = "DATA\\XML\\SpaceUnitsCapital.xml";
    std::string content =
        "<SpaceUnits>\n<SpaceUnit Name=\"Meg_Fighter\">\n"
        "<Damage>5</Damage>\n<Tactical_Health>50</Tactical_Health>\n"
        "<Targeting_Max_Attack_Distance>300</Targeting_Max_Attack_Distance>\n"
        "</SpaceUnit>\n</SpaceUnits>\n";
    writeFile(root + "\\Data\\test.meg", buildMeg(name, content));
    // Loose override.
    writeFile(root + "\\Data\\XML\\SpaceUnitsFighters.xml",
              "<SpaceUnits>\n<SpaceUnit Name=\"Loose_Fighter\">\n"
              "<Damage>7</Damage>\n<Tactical_Health>70</Tactical_Health>\n"
              "<Targeting_Max_Attack_Distance>350</Targeting_Max_Attack_Distance>\n"
              "</SpaceUnit>\n</SpaceUnits>\n");
}

void testModMount() {
    fs::remove_all(scratch);
    std::string root = scratch + "\\mod";
    makeMod(root);

    eaw::MegaFileManager files;
    eaw::ModReport report = eaw::ModLoader::load(files, root);
    check(report.megasLoaded == 1, "mod meg loaded");
    check(files.looseCount() == 3, "loose files registered (megafiles + meg + override)");
    // The meg's unit XML resolves via the manager (normalized key).
    check(files.exists("DATA/XML/SPACEUNITSCAPITAL.XML"), "meg entry resolvable");
    check(files.exists("DATA/XML/SPACEUNITSFIGHTERS.XML"), "loose override resolvable");
    auto types = eaw::ModLoader::loadUnitTypes(files);
    bool hasMeg = false, hasLoose = false;
    for (const auto& t : types) {
        if (t.name == "Meg_Fighter") hasMeg = true;
        if (t.name == "Loose_Fighter") hasLoose = true;
    }
    check(hasMeg, "meg unit type loaded");
    check(hasLoose, "loose unit type loaded");
}

void testLooseOverride() {
    fs::remove_all(scratch);
    std::string root = scratch + "\\mod2";
    // Same unit name in meg and loose: loose must win.
    writeFile(root + "\\Data\\megafiles.xml",
              "<Mega_Files>\n<File> Data\\test.meg </File>\n</Mega_Files>\n");
    std::string name = "DATA\\XML\\SpaceUnitsFighters.xml";
    std::string content =
        "<SpaceUnits>\n<SpaceUnit Name=\"Shared_Fighter\">\n"
        "<Damage>5</Damage>\n<Tactical_Health>50</Tactical_Health>\n"
        "<Targeting_Max_Attack_Distance>300</Targeting_Max_Attack_Distance>\n"
        "</SpaceUnit>\n</SpaceUnits>\n";
    writeFile(root + "\\Data\\test.meg", buildMeg(name, content));
    // Loose override with the SAME key (lowercase path; normalized).
    writeFile(root + "\\Data\\XML\\SpaceUnitsFighters.xml",
              "<SpaceUnits>\n<SpaceUnit Name=\"Shared_Fighter\">\n"
              "<Damage>99</Damage>\n<Tactical_Health>99</Tactical_Health>\n"
              "<Targeting_Max_Attack_Distance>999</Targeting_Max_Attack_Distance>\n"
              "</SpaceUnit>\n</SpaceUnits>\n");

    eaw::MegaFileManager files;
    eaw::ModLoader::load(files, root);
    auto types = eaw::ModLoader::loadUnitTypes(files);
    bool has = false;
    for (const auto& t : types) {
        if (t.name == "Shared_Fighter" && t.damage > 0.9) has = true;
    }
    check(has, "loose file overrides meg entry");
}

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    testModMount();
    testLooseOverride();
    fs::remove_all(scratch);
    if (failures == 0) { std::printf("ALL TESTS PASSED\n"); return 0; }
    std::printf("%d FAILURES\n", failures);
    return 1;
}
