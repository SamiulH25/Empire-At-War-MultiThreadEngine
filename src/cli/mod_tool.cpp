// mod_tool — mod-compatibility tooling.
//
// Loads a mod's Data folder the way the game does (loose files override
// megas — doc 05), and reports/proves compatibility:
//
//   mod_tool scan <mod_dir> [--game <base game dir>]
//       Walks the mod's Data folder, loads any base-game megas as fallback,
//       reports the file surface (loose files by category, meg files loaded).
//
//   mod_tool battle <mod_dir> [--game <base game dir>]
//       [--workers N] [--ticks N] [--fighters N] [--capitals N] [--land]
//       Loads the mod's unit XML (loose files override base megas), picks
//       combat units per side, and runs a deterministic battle through the
//       parallel sim.
//
//   mod_tool bindings <mod_dir>
//       Scans the mod's Lua scripts for engine binding calls (the PG*
//       surface), lists which registered bindings they use and which
//       documented bindings are still missing from the engine.
//
//   mod_tool run <mod_dir> <script-path> [--game <base game dir>]
//       [--ticks N] [--battle] [--galaxy]
//       Executes one mod script (loose file or meg entry, e.g.
//       DATA\SCRIPTS\LIBRARY\PGTASKFORCE.LUA) against a live engine: the
//       script is loaded through ScriptManager::loadScript and pumped for
//       --ticks frames. Runtime errors are reported with the script name;
//       exit code 0 = the script ran, 1 = load/pump error, 2 = script
//       requested a documented-but-missing binding (helpful gap report).
#include "core/lua_wrappers.h"
#include "core/meg_file.h"
#include "core/meg_manager.h"
#include "core/mod_loader.h"
#include "core/simulation.h"
#include "core/unit_data_loader.h"
#include "core/xml.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return std::string((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
}

bool hasSuffix(const std::string& s, const char* suf) {
    size_t n = std::strlen(suf);
    if (s.size() < n) return false;
    std::string tail = s.substr(s.size() - n);
    std::transform(tail.begin(), tail.end(), tail.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::string lsuf(suf);
    std::transform(lsuf.begin(), lsuf.end(), lsuf.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return tail == lsuf;
}

std::string toUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

// Walks a directory recursively, collecting files with their relative paths
// (forward slashes, uppercased — matching the game's file-table convention).
// When `base` ends in "Data", paths get the "DATA/" prefix (the game's
// master file table keys on DATA\... names).
void walkDir(const std::string& base, const std::string& dir,
             std::vector<std::string>& out) {
    std::error_code ec;
    // Normalize base (no trailing slash) so relative paths come out clean.
    std::string baseNorm = base;
    while (baseNorm.size() > 1 && (baseNorm.back() == '\\' || baseNorm.back() == '/')) {
        baseNorm.pop_back();
    }
    for (const auto& entry : fs::recursive_directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        std::string rel = entry.path().string().substr(baseNorm.size() + 1);
        std::replace(rel.begin(), rel.end(), '\\', '/');
        std::string upper = toUpper(rel);
        std::string baseUpper = toUpper(baseNorm);
        if (baseUpper.size() >= 4 &&
            baseUpper.substr(baseUpper.size() - 4) == "DATA") {
            upper = "DATA/" + upper;
        }
        out.push_back(upper);
    }
}

// Loads base-game megas from <game>/Data/*.meg (the fallback layer).
// Accepts the game root, root\Data, or root\GameData\Data (the real layout).
void loadBaseMegas(eaw::MegaFileManager& files, const std::string& gameRoot,
                   std::vector<std::vector<uint8_t>>& archiveBytes,
                   std::vector<eaw::MegFile>& megas) {
    if (gameRoot.empty()) return;
    std::string dataDir = gameRoot + "\\Data\\";
    if (!std::filesystem::exists(std::filesystem::path(dataDir + "megafiles.xml"))) {
        // Try the game's GameData\Data layout.
        std::string gd = gameRoot + "\\GameData\\Data\\";
        if (std::filesystem::exists(std::filesystem::path(gd + "megafiles.xml"))) {
            dataDir = gd;
        }
    }
    std::vector<std::string> megNames;
    walkDir(dataDir, dataDir, megNames);
    std::sort(megNames.begin(), megNames.end());
    for (const std::string& name : megNames) {
        if (!hasSuffix(name, ".meg")) continue;
        // name has the DATA/ prefix; strip it for the disk path.
        std::string diskName = name;
        if (diskName.rfind("DATA/", 0) == 0) diskName = diskName.substr(5);
        std::string path = dataDir + diskName;
        std::string bytes = readFile(path);
        if (bytes.empty()) continue;
        archiveBytes.emplace_back(bytes.begin(), bytes.end());
        try {
            megas.emplace_back(eaw::MegFile::Parse(archiveBytes.back()));
            files.addArchive(name, archiveBytes.back(), megas.back());
        } catch (const std::exception&) {
            archiveBytes.pop_back();
        }
    }
}

// Full mod mount: base game megas (fallback) + mod's own megas + loose files
// (override). Uses ModLoader for the mod side so custom megas (like TR's
// UGCCEAIDATA.meg) are included.
eaw::ModReport loadModData(eaw::MegaFileManager& files, const std::string& modRoot,
                           const std::string& gameRoot,
                           std::vector<std::vector<uint8_t>>& archiveBytes,
                           std::vector<eaw::MegFile>& megas) {
    loadBaseMegas(files, gameRoot, archiveBytes, megas);
    // ModLoader loads the mod's megas + loose files on top.
    return eaw::ModLoader::load(files, modRoot, "");
}

// ---- discovery-driven loading -------------------------------------------
// The game discovers data through file-list XMLs (megafiles.xml for megas,
// GameObjectFiles.xml for object/unit files, Factionfiles.xml for factions).
// Mods add custom files by listing them; we must load whatever the lists
// name, resolved through loose-override/meg precedence — NOT hardcoded
// vanilla filenames.

// Parses a file-list XML (<Foo_Files> with <File> children) and returns the
// listed paths (normalized to DATA/... uppercase).
std::vector<std::string> readFileList(eaw::MegaFileManager& files,
                                      const std::string& listPath) {
    std::vector<std::string> out;
    if (!files.exists(listPath)) return out;
    auto bytes = files.read(listPath);
    try {
        eaw::XmlNode root = eaw::ParseXml(std::string(bytes.begin(), bytes.end()));
        for (const eaw::XmlNode& child : root.children) {
            if (child.name != "File") continue;
            std::string f = child.text;
            // Trim whitespace.
            size_t b = f.find_first_not_of(" \t\r\n");
            size_t e = f.find_last_not_of(" \t\r\n");
            if (b == std::string::npos) continue;
            f = f.substr(b, e - b + 1);
            std::replace(f.begin(), f.end(), '\\', '/');
            std::string upper = toUpper(f);
            // Paths may be "Data\XML\Foo.xml" or bare "Foo.xml" (relative to
            // the XML dir). Normalize to DATA/XML/... keys.
            if (upper.rfind("DATA/", 0) == 0) {
                out.push_back(upper);
            } else if (upper.rfind("XML/", 0) == 0) {
                out.push_back("DATA/" + upper);
            } else {
                out.push_back("DATA/XML/" + upper);
            }
        }
    } catch (const std::exception&) {
    }
    return out;
}

// Loads the game's data-file lists: megafiles.xml (megs) + GameObjectFiles.xml
// (object/unit files) + Factionfiles.xml. Returns all named files that resolve.
std::vector<std::string> discoverDataFiles(eaw::MegaFileManager& files) {
    std::vector<std::string> out;
    for (const std::string& list : {"DATA/MEGAFILES.XML", "DATA/XML/GAMEOBJECTFILES.XML",
                                    "DATA/XML/FACTIONFILES.XML"}) {
        auto listed = readFileList(files, list);
        out.insert(out.end(), listed.begin(), listed.end());
    }
    // Dedupe, keep order.
    std::vector<std::string> uniq;
    std::set<std::string> seen;
    for (const std::string& f : out) {
        if (seen.insert(f).second) uniq.push_back(f);
    }
    return uniq;
}

// Loads every combat unit type from the discovered object files (loose wins).
// Returns the type names in file order.
std::vector<std::string> loadDiscoveredUnitTypes(eaw::SimState& state,
                                                 eaw::MegaFileManager& files) {
    eaw::UnitDataLoader loader;
    std::vector<eaw::ObjectType> all;
    auto discovered = discoverDataFiles(files);
    for (const std::string& f : discovered) {
        if (!hasSuffix(f, ".xml")) continue;
        if (!files.exists(f)) continue;
        auto bytes = files.read(f);
        try {
            auto types = loader.loadXml(std::string(bytes.begin(), bytes.end()));
            all.insert(all.end(), types.begin(), types.end());
        } catch (const std::exception&) {
        }
    }
    std::vector<std::string> names;
    for (auto& t : all) {
        if (t.damage > 0.0 && t.maxRange > 0.0) {
            names.push_back(t.name);
            state.addType(std::move(t));
        }
    }
    return names;
}

// Loads every planet from the discovered planet files.
std::vector<eaw::Planet> loadDiscoveredPlanets(eaw::MegaFileManager& files) {
    eaw::UnitDataLoader loader;
    std::vector<eaw::Planet> out;
    // PLANETS.XML is not listed in GameObjectFiles.xml — it's a fixed name.
    for (const std::string& f : {"DATA/XML/PLANETS.XML"}) {
        if (!files.exists(f)) continue;
        auto bytes = files.read(f);
        try {
            auto planets = loader.loadPlanets(std::string(bytes.begin(), bytes.end()));
            out.insert(out.end(), planets.begin(), planets.end());
        } catch (const std::exception&) {
        }
    }
    // Also try whatever the file lists name (loose mods may rename it).
    auto discovered = discoverDataFiles(files);
    for (const std::string& f : discovered) {
        if (!hasSuffix(f, ".xml")) continue;
        if (!files.exists(f)) continue;
        auto bytes = files.read(f);
        try {
            auto planets = loader.loadPlanets(std::string(bytes.begin(), bytes.end()));
            out.insert(out.end(), planets.begin(), planets.end());
        } catch (const std::exception&) {
        }
    }
    return out;
}

// ---- scan ---------------------------------------------------------------

int cmdScan(const std::string& modRoot, const std::string& gameRoot) {
    eaw::MegaFileManager files;
    std::vector<std::vector<uint8_t>> archiveBytes;
    std::vector<eaw::MegFile> megas;
    eaw::ModReport report = loadModData(files, modRoot, gameRoot, archiveBytes, megas);

    std::printf("mod root : %s\n", modRoot.c_str());
    std::printf("base megas: %zu loaded", megas.size());
    if (!gameRoot.empty()) std::printf(" (from %s)", gameRoot.c_str());
    std::printf("\nmod megas: %d\n", report.megasLoaded);
    for (const std::string& n : report.megNames) std::printf("    %s\n", n.c_str());
    std::printf("loose files (mod overrides): %zu\n", files.looseCount());

    // Categorize the loose surface.
    int xml = 0, lua = 0, txt = 0, other = 0;
    std::map<std::string, int> luaDirs;
    for (const auto& [name, bytes] : files.loose()) {
        if (hasSuffix(name, ".xml")) ++xml;
        else if (hasSuffix(name, ".lua")) { ++lua; luaDirs[name.substr(0, name.find_last_of('/'))]++; }
        else if (hasSuffix(name, ".txt")) ++txt;
        else ++other;
    }
    std::printf("  xml: %d  lua: %d  txt: %d  other: %d\n", xml, lua, txt, other);
    if (!luaDirs.empty()) {
        std::printf("  lua by dir:\n");
        for (const auto& [d, n] : luaDirs) std::printf("    %s (%d)\n", d.c_str(), n);
    }
    return 0;
}

// ---- battle -------------------------------------------------------------

struct BattleConfig {
    unsigned workers = 0;
    int ticks = 3600;
    int fighters = 16;
    int capitals = 4;
    bool land = false;
};

// Picks two distinct combat types per side (fallback: by category).
std::string pickType(eaw::SimState& state, const std::vector<std::string>& names,
                     const std::set<std::string>& used) {
    for (const std::string& n : names) {
        if (used.count(n)) continue;
        const eaw::ObjectType* t = state.type(n);
        if (t && t->damage > 0 && t->maxRange > 0) return n;
    }
    return "";
}

int cmdBattle(const std::string& modRoot, const std::string& gameRoot,
              const BattleConfig& cfg) {
    eaw::MegaFileManager files;
    std::vector<std::vector<uint8_t>> archiveBytes;
    std::vector<eaw::MegFile> megas;
    loadModData(files, modRoot, gameRoot, archiveBytes, megas);

    eaw::Simulation sim(cfg.workers);
    eaw::SimState& state = sim.sim();
    eaw::Player& rebel = state.addPlayer("Rebel Alliance", "REBEL");
    eaw::Player& empire = state.addPlayer("Galactic Empire", "EMPIRE");
    rebel.human = true;

    // Discovery-driven: load whatever the mod's file lists name.
    std::vector<std::string> names = loadDiscoveredUnitTypes(state, files);
    std::printf("combat types loaded: %zu\n", names.size());
    if (names.empty()) {
        std::fprintf(stderr, "no combat units found — is the mod data present?\n");
        return 1;
    }

    std::set<std::string> used;
    std::string rebelLight = pickType(state, names, used);
    used.insert(rebelLight);
    std::string rebelHeavy = pickType(state, names, used);
    used.insert(rebelHeavy);
    std::string empireLight = pickType(state, names, used);
    used.insert(empireLight);
    std::string empireHeavy = pickType(state, names, used);
    if (empireHeavy.empty()) empireHeavy = empireLight;
    if (rebelHeavy.empty()) rebelHeavy = rebelLight;

    double spacing = cfg.land ? 4.0 : 12.0;
    double gap = cfg.land ? 60.0 : 600.0;
    for (int i = 0; i < cfg.fighters; ++i) {
        double x = (i % 8) * spacing, y = (i / 8) * spacing;
        state.spawnUnit(rebelLight, rebel.id, {x, y, (i % 3) * 15.0});
        state.spawnUnit(empireLight, empire.id, {gap + x, y, (i % 3) * 15.0});
    }
    for (int i = 0; i < cfg.capitals; ++i) {
        double x = (i % 2) * spacing * 3.0, y = (i / 2) * spacing * 3.0;
        state.spawnUnit(rebelHeavy, rebel.id, {x, y, 10.0});
        state.spawnUnit(empireHeavy, empire.id, {gap + x, y, 10.0});
    }
    std::printf("battle: %s x%d + %s x%d vs %s x%d + %s x%d (mod units)\n",
                rebelLight.c_str(), cfg.fighters, rebelHeavy.c_str(), cfg.capitals,
                empireLight.c_str(), cfg.fighters, empireHeavy.c_str(), cfg.capitals);

    // Everyone attacks the first enemy of the other side (auto-acquire
    // handles re-targeting).
    sim.scripts().runScript(
        "for i, o in ipairs(Find_All_Objects_Of_Type('" + rebelLight + "')) do\n"
        "  o:Attack_Target(Find_First_Object('" + empireLight + "'))\n"
        "  o:Move_To(Find_First_Object('" + empireHeavy + "'))\n"
        "end\n"
        "for i, o in ipairs(Find_All_Objects_Of_Type('" + rebelHeavy + "')) do\n"
        "  o:Attack_Target(Find_First_Object('" + empireHeavy + "'))\n"
        "end\n"
        "for i, o in ipairs(Find_All_Objects_Of_Type('" + empireLight + "')) do\n"
        "  o:Attack_Target(Find_First_Object('" + rebelLight + "'))\n"
        "  o:Move_To(Find_First_Object('" + rebelHeavy + "'))\n"
        "end\n"
        "for i, o in ipairs(Find_All_Objects_Of_Type('" + empireHeavy + "')) do\n"
        "  o:Attack_Target(Find_First_Object('" + rebelHeavy + "'))\n"
        "end\n");

    auto t0 = std::chrono::steady_clock::now();
    const double dt = 1.0 / 30.0;
    int ticks = 0, rebelAlive = 0, empireAlive = 0;
    for (int i = 0; i < cfg.ticks; ++i) {
        sim.tick(dt);
        ticks = i + 1;
        rebelAlive = 0;
        empireAlive = 0;
        for (const eaw::GameObject* o : state.allObjects()) {
            if (!o->alive) continue;
            const eaw::Player* p = state.player(o->playerId);
            if (p && p->factionName == "REBEL") ++rebelAlive;
            else if (p && p->factionName == "EMPIRE") ++empireAlive;
        }
        if (rebelAlive == 0 || empireAlive == 0) break;
    }
    auto t1 = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    std::printf("ticks=%d (%.1fs) wall=%.1fms (%.0f ticks/s) shots=%llu\n",
                ticks, ticks * dt, ms, ticks / (ms / 1000.0), sim.totalShots());
    std::printf("rebels %d/%d alive, empire %d/%d alive\n",
                rebelAlive, cfg.fighters + cfg.capitals,
                empireAlive, cfg.fighters + cfg.capitals);
    if (rebelAlive == 0 || empireAlive == 0) {
        std::printf("outcome: %s VICTORY\n", rebelAlive > 0 ? "REBEL" : "EMPIRE");
    } else {
        std::printf("outcome: undecided\n");
    }
    return 0;
}

// ---- galaxy --------------------------------------------------------------

// Simulates the galactic layer: planets from the mod's PLANETS.XML, faction
// home worlds, AI fleets producing + moving between planets via hyperspace.
int cmdGalaxy(const std::string& modRoot, const std::string& gameRoot) {
    eaw::MegaFileManager files;
    std::vector<std::vector<uint8_t>> archiveBytes;
    std::vector<eaw::MegFile> megas;
    loadModData(files, modRoot, gameRoot, archiveBytes, megas);

    eaw::Simulation sim(4);
    eaw::SimState& state = sim.sim();
    eaw::Player& rebel = state.addPlayer("Rebel Alliance", "REBEL");
    eaw::Player& empire = state.addPlayer("Galactic Empire", "EMPIRE");
    eaw::Player& pirate = state.addPlayer("Pirate Factions", "UNDERWORLD");
    rebel.human = true; // only the empire plays AI here

    // Planets from the mod's discovered planet files (loose wins).
    int planets = 0;
    auto planetList = loadDiscoveredPlanets(files);
    for (auto& p : planetList) {
        state.addPlanet(p.name, "", p.position);
        ++planets;
    }
    std::printf("planets loaded: %d\n", planets);
    if (planets == 0) {
        std::fprintf(stderr, "no PLANETS.XML found\n");
        return 1;
    }

    // Combat units for the fleets (mod's discovered unit files).
    std::vector<std::string> names = loadDiscoveredUnitTypes(state, files);
    std::printf("combat types loaded: %zu\n", names.size());
    std::string light = pickType(state, names, {});
    if (light.empty()) light = "X-Wing";
    std::string heavy = light;
    for (const std::string& n : names) {
        const eaw::ObjectType* t = state.type(n);
        if (t && t->buildCost >= 500.0 && n != light) { heavy = n; break; }
    }

    // Assign home worlds: skip art/placeholder planets, pick the first real
    // planets for each faction.
    auto allPlanets = state.allPlanets();
    std::string rebelHome, empireHome;
    for (const eaw::Planet* p : allPlanets) {
        std::string n = p->name;
        if (n.find("Art_Model") != std::string::npos) continue;
        if (n.find("Core") != std::string::npos) continue;
        if (rebelHome.empty()) rebelHome = n;
        else { empireHome = n; break; }
    }
    if (rebelHome.empty() || empireHome.empty() || rebelHome == empireHome) {
        std::fprintf(stderr, "could not pick home planets\n");
        return 1;
    }
    int rebelHomeId = state.findPlanet(rebelHome)->id;
    int empireHomeId = state.findPlanet(empireHome)->id;
    state.findPlanet(rebelHome)->factionName = "REBEL";
    state.findPlanet(empireHome)->factionName = "EMPIRE";
    std::printf("rebel home: %s, empire home: %s\n", rebelHome.c_str(),
                empireHome.c_str());

    // Income from planets: each owned planet contributes credits/sec.
    rebel.incomePerSecond = 10.0;
    empire.incomePerSecond = 10.0;
    for (const eaw::Planet* p : state.allPlanets()) {
        if (p->factionName == "REBEL") rebel.incomePerSecond += p->garrisonHull * 0.02;
        if (p->factionName == "EMPIRE") empire.incomePerSecond += p->garrisonHull * 0.02;
    }

    // Starting fleets at each home world (AI builds them up via economy).
    int rebelForce = state.addTaskForce(rebel.id, "AttackPlan");
    int empireForce = state.addTaskForce(empire.id, "AttackPlan");
    state.taskForce(rebelForce)->planetId = rebelHomeId;
    state.taskForce(empireForce)->planetId = empireHomeId;
    for (int i = 0; i < 4; ++i) {
        const eaw::Planet* hp = state.planet(rebelHomeId);
        int u = state.spawnUnit(light, rebel.id, hp->position);
        state.addUnitToForce(rebelForce, u);
    }
    for (int i = 0; i < 4; ++i) {
        const eaw::Planet* hp = state.planet(empireHomeId);
        int u = state.spawnUnit(light, empire.id, hp->position);
        state.addUnitToForce(empireForce, u);
    }
    sim.setAiBuildTypes({light, heavy});
    sim.sim().giveMoney(empire.id, 3000.0);
    sim.sim().giveMoney(rebel.id, 3000.0);

    // The AI's goal: move the force to the enemy home world (via hyperspace).
    sim.scripts().runScript(
        "force = Find_First_Object('X-Wing') and nil\n"); // placeholder no-op
    // Order the AI (empire) force toward the rebel home via hyperspace.
    {
        lua_State* s = sim.scripts().state();
        eaw::pushWrapper(s, &state, eaw::WrapperKind::TaskForce, empireForce);
        lua_setglobal(s, "empire_force");
        eaw::pushWrapper(s, &state, eaw::WrapperKind::Planet,
                         state.findPlanet(rebelHome)->id);
        lua_setglobal(s, "rebel_home");
    }
    sim.scripts().runScript(
        "empire_force:Move_To(rebel_home)\n");

    // Run the galactic sim for N ticks; report transits, arrivals, economy.
    const double dt = 1.0 / 30.0;
    int ticks = 3600;
    int arrivals = 0;
    for (int i = 0; i < ticks; ++i) {
        sim.tick(dt);
        if (i % 300 == 0 && i > 0) {
            std::printf("t=%d  rebel credits=%.0f force=%zu%s  empire credits=%.0f force=%zu%s\n",
                        i, sim.sim().player(rebel.id)->credits,
                        sim.sim().taskForce(rebelForce)->units.size(),
                        sim.sim().forceInTransit(rebelForce) ? " [in transit]" : "",
                        sim.sim().player(empire.id)->credits,
                        sim.sim().taskForce(empireForce)->units.size(),
                        sim.sim().forceInTransit(empireForce) ? " [in transit]" : "");
        }
    }
    arrivals = static_cast<int>(sim.transitArrivals());
    std::printf("galactic sim complete: %d ticks, %d hyperspace arrivals\n",
                ticks, arrivals);
    std::printf("rebel force at %s (%zu units), empire force at %s (%zu units)\n",
                sim.sim().planet(sim.sim().forcePlanet(rebelForce))
                    ? sim.sim().planet(sim.sim().forcePlanet(rebelForce))->name.c_str()
                    : "none",
                sim.sim().taskForce(rebelForce)->units.size(),
                sim.sim().planet(sim.sim().forcePlanet(empireForce))
                    ? sim.sim().planet(sim.sim().forcePlanet(empireForce))->name.c_str()
                    : "none",
                sim.sim().taskForce(empireForce)->units.size());
    std::printf("rebel credits: %.0f, empire credits: %.0f\n",
                sim.sim().player(rebel.id)->credits,
                sim.sim().player(empire.id)->credits);
    return 0;
}

// ---- bindings -----------------------------------------------------------

// The engine's registered binding surface — globals (pg_bindings,
// pg_object_bindings, pg_event_bindings, pg_taskforce_bindings) plus all
// wrapper methods (object/player/type/position/command/taskforce/planet).
std::set<std::string> engineBindings() {
    return {
        // pg_bindings globals
        "GlobalValue_Get", "GlobalValue_Set", "Create_Thread", "Thread",
        "Thread_Is_Thread_Active", "Thread_Kill", "GetCurrentTime",
        "GameRandom", "GameRandom_Get_Float", "Get_Game_Mode",
        "_ScriptMessage", "lc",
        // pg_object_bindings globals
        "Find_Player", "Find_Object_Type", "Find_All_Objects_Of_Type",
        "Find_First_Object", "Find_Nearest", "Create_Position",
        "Spawn_Unit", "Reinforce_Unit",
        // pg_event_bindings globals
        "Register_Timer", "Register_Death_Event", "Register_Attacked_Event",
        "Cancel_Attacked_Event", "Register_Prox", "Process_Timers",
        "Process_Death_Events", "Process_Attacked_Events",
        "Process_Proximities", "Pump_Service",
        // pg_taskforce_bindings globals
        "FindPlanet",
        // Object methods
        "Get_Hull", "Get_Health", "Get_Shield", "Get_Energy", "Get_Owner",
        "Get_Faction", "Get_Type", "Get_ID", "Get_Position", "Get_Distance",
        "Is_Category", "Has_Property", "Is_Valid", "Is_Hero", "Is_Selectable",
        "Get_Name", "Get_Garrisoned_Units", "Has_Garrison", "Is_In_Garrison",
        "Get_Time_Till_Dead", "Move_To", "Attack_Target", "Get_Attack_Target",
        "Has_Attack_Target", "Release_Unit", "Lock_Current_Orders",
        "Unlock_Current_Orders", "Take_Damage", "Despawn", "Make_Invulnerable",
        "Set_Selectable", "Can_Garrison", "Garrison", "Try_Garrison",
        "Leave_Garrison", "Eject_Garrison", "Activate_Ability", "Try_Ability",
        "Use_Ability_If_Able", "Has_Ability", "Is_Ability_Active",
        "Is_Ability_Ready", "Cancel_Ability", "Force_Ability_Recharge",
        "Reset_Ability_Counter", "Get_Force", "Set_Targeting_Priorities",
        "Set_Land_AI_Targeting_Priorities",
        // Player methods
        "Get_ID", "Get_Name", "Get_Faction_Name", "Get_Difficulty",
        "Is_Human", "Get_Tech_Level", "Get_Credits", "Make_Ally",
        "Make_Enemy", "Is_Ally", "Is_Enemy", "Get_Enemy", "Give_Money",
        "Set_Tech_Level", "Unlock_Tech", "Lock_Tech",
        // Type methods
        "Get_Name", "Is_Hero", "Get_Build_Cost", "Get_Tech_Level",
        "Get_Max_Range", "Get_Min_Range", "Is_Affected_By_Missile_Shield",
        "Is_Affected_By_Laser_Defense",
        // Position methods
        "Get_XYZ",
        // Command methods
        "IsFinished", "Result",
        // TaskForce methods
        "Get_Goal_Type_Name", "Get_Stage", "Set_Stage", "Set_Plan_Result",
        "Set_As_Goal_System_Removable", "Are_All_Units_On_Free_Store",
        "Get_Self_Threat_Sum", "Add_Force", "Release_Unit", "Move_To",
        "Attack_Target", "Garrison", "Leave_Garrison", "Get_Planet",
        // Planet methods
        "Get_Name", "Get_Owner",
    };
}

// The documented bindings mods may call that we have NOT registered yet.
std::set<std::string> documentedButMissing() {
    return {
        // Targeting priority getters (not yet in the engine tables)
        "Get_Targeting_Priorities",
        // Free store / squad
        "Get_Free_Store", "Get_Units_In_Free_Store",
        // Formations
        "Formation_Attack", "Formation_Move", "Set_Formation",
        // Pathing
        "Find_Path", "Get_Path", "Is_Path_Blocked",
        // Damage / effects
        "Set_Hull", "Set_Shield", "Add_Hull", "Add_Shield",
        "Apply_Damage", "Apply_Force",
        // Hero / unique
        "Set_Hero", "Is_Unique", "Get_Unique_ID",
        // Misc documented
        "Get_Player_Count", "Set_Faction",
        "Get_Current_Planet", "Set_Current_Planet",
        "Get_Planet_Owner", "Set_Planet_Owner",
        "Get_Planet_Faction", "Set_Planet_Faction",
        "Set_Force_Planet", "Get_Force_Player",
        "Set_Force_Player", "Get_Number_Of_Forces",
    };
}

int cmdBindings(const std::string& modRoot) {
    std::string dataDir = modRoot + "\\Data";
    std::vector<std::string> rels;
    walkDir(dataDir, dataDir, rels);
    std::set<std::string> used;
    std::set<std::string> usedInFiles;
    auto scanText = [&](const std::string& text, const std::string& srcName) {
        for (const std::string& b : engineBindings()) {
            if (text.find(b) != std::string::npos) {
                used.insert(b);
                usedInFiles.insert(srcName);
            }
        }
        for (const std::string& b : documentedButMissing()) {
            if (text.find(b) != std::string::npos) {
                used.insert(b);
                usedInFiles.insert(srcName);
            }
        }
    };
    // Loose Lua files.
    for (const std::string& rel : rels) {
        if (!hasSuffix(rel, ".lua")) continue;
        std::string diskName = rel;
        if (diskName.rfind("DATA/", 0) == 0) diskName = diskName.substr(5);
        scanText(readFile(dataDir + "\\" + diskName), rel);
    }
    // Lua inside the mod's megas (e.g. UGCCEAIDATA.meg with DATA\SCRIPTS\...).
    // The manager doesn't expose entry lists, so re-parse each mod meg from
    // disk and read entries directly (loose override would apply via the
    // manager, but no loose Lua exists in TR).
    for (const std::string& megName : {"UGCCEAIDATA.meg", "ZC_MISSIONS_DATA.meg"}) {
        std::string path = dataDir + "\\" + megName;
        std::string bytes = readFile(path);
        if (bytes.empty()) continue;
        try {
            eaw::MegFile meg = eaw::MegFile::Parse(
                std::vector<uint8_t>(bytes.begin(), bytes.end()));
            for (const auto& e : meg.entries()) {
                const std::string& name = meg.nameOf(e);
                if (!hasSuffix(name, ".lua")) continue;
                auto entryBytes = meg.read(e, std::vector<uint8_t>(bytes.begin(), bytes.end()));
                scanText(std::string(entryBytes.begin(), entryBytes.end()),
                         "meg:" + megName + ":" + name);
            }
        } catch (const std::exception&) {
        }
    }
    std::printf("lua scripts scanned: %zu (mod root %s)\n",
                usedInFiles.size(), modRoot.c_str());
    std::printf("\nbindings used by mod scripts:\n");
    std::set<std::string> have, missing;
    for (const std::string& b : used) {
        if (engineBindings().count(b)) have.insert(b);
        else missing.insert(b);
    }
    std::printf("  registered in engine: %zu\n", have.size());
    for (const std::string& b : have) std::printf("    %s\n", b.c_str());
    std::printf("  documented but MISSING from engine: %zu\n", missing.size());
    for (const std::string& b : missing) std::printf("    %s\n", b.c_str());
    return missing.empty() ? 0 : 2;
}

// ---- run ----------------------------------------------------------------

// Builds a real battle/galactic sim state and returns it (like cmdBattle /
// cmdGalaxy) so `mod_tool run` can execute scripts with game state to query.
// The mod data is mounted into the Simulation's OWN file manager — that is
// the manager ScriptManager::loadScript reads from (the sim's scripts_ holds
// a reference to the Simulation's private files_). Returns nullptr on fatal
// setup errors (message already printed).
std::unique_ptr<eaw::Simulation> buildRunSim(const std::string& modRoot,
                                             const std::string& gameRoot) {
    auto sim = std::make_unique<eaw::Simulation>(4);
    eaw::MegaFileManager& files = sim->files();

    // Reuse the full mod mount (base megas + mod megas + loose override).
    std::vector<std::vector<uint8_t>> archiveBytes;
    std::vector<eaw::MegFile> megas;
    try {
        loadModData(files, modRoot, gameRoot, archiveBytes, megas);
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "mount failed (%s): %s\n", modRoot.c_str(), ex.what());
        return nullptr;
    }

    eaw::SimState& state = sim->sim();
    eaw::Player& rebel = state.addPlayer("Rebel Alliance", "REBEL");
    eaw::Player& empire = state.addPlayer("Galactic Empire", "EMPIRE");
    rebel.human = true;

    // Real unit types from the mod's file lists (loose wins).
    std::vector<std::string> names = loadDiscoveredUnitTypes(state, files);
    if (names.empty()) {
        std::fprintf(stderr, "no combat units found — is the mod data present?\n");
        return nullptr;
    }
    std::set<std::string> used;
    std::string rl = pickType(state, names, used); used.insert(rl);
    std::string rh = pickType(state, names, used); used.insert(rh);
    std::string el = pickType(state, names, used); used.insert(el);
    std::string eh = pickType(state, names, used);
    if (rh.empty()) rh = rl;
    if (eh.empty()) eh = el;

    // A small standoff: 8 fighters + 2 capitals per side, spaced like cmdBattle.
    for (int i = 0; i < 8; ++i) {
        state.spawnUnit(rl, rebel.id, {(i % 8) * 12.0, (i / 8) * 12.0, (i % 3) * 15.0});
        state.spawnUnit(el, empire.id, {600.0 + (i % 8) * 12.0, (i / 8) * 12.0, (i % 3) * 15.0});
    }
    for (int i = 0; i < 2; ++i) {
        state.spawnUnit(rh, rebel.id, {(i % 2) * 36.0, (i / 2) * 36.0, 10.0});
        state.spawnUnit(eh, empire.id, {600.0 + (i % 2) * 36.0, (i / 2) * 36.0, 10.0});
    }
    std::printf("sim: %s x8 + %s x2 vs %s x8 + %s x2 (mod units)\n",
                rl.c_str(), rh.c_str(), el.c_str(), eh.c_str());

    // A couple of planets for galactic bindings (FindPlanet / Get_Planet).
    state.addPlanet("Corellia", "REBEL", {100, 0, 0});
    state.addPlanet("Kuat", "EMPIRE", {900, 0, 0});
    return sim;
}

// Scans `text` for any documented-but-missing binding name; returns the first
// match (or "" if none).
std::string firstMissingBinding(const std::string& text) {
    for (const std::string& b : documentedButMissing()) {
        if (text.find(b) != std::string::npos) return b;
    }
    return "";
}

// Loads a script from the mounted file table (loose or meg). `scriptPath` may
// be DATA\... (game convention) or a plain name; the manager is
// case-insensitive. Returns the bytes (empty if not found).
std::vector<uint8_t> readMountedScript(eaw::MegaFileManager& files,
                                       const std::string& scriptPath) {
    std::string norm = scriptPath;
    std::replace(norm.begin(), norm.end(), '\\', '/');
    std::string upper = toUpper(norm);
    if (upper.rfind("DATA/", 0) != 0) upper = "DATA/" + upper;
    try {
        return files.read(upper);
    } catch (const std::exception&) {
        return {};
    }
}

int cmdRun(const std::string& modRoot, const std::string& scriptPath,
           const std::string& gameRoot, int ticks) {
    auto sim = buildRunSim(modRoot, gameRoot);
    if (!sim) return 1;
    eaw::MegaFileManager& files = sim->files();

    // Pre-scan the script text for documented-but-missing bindings so we can
    // distinguish "the engine doesn't have this yet" from a real script error.
    auto scriptBytes = readMountedScript(files, scriptPath);
    if (scriptBytes.empty()) {
        std::fprintf(stderr, "script not found in mod data: %s\n", scriptPath.c_str());
        return 1;
    }
    std::string text(scriptBytes.begin(), scriptBytes.end());
    std::string missing = firstMissingBinding(text);
    if (!missing.empty()) {
        std::fprintf(stderr,
                     "script uses '%s', which the engine does not register yet — "
                     "cannot execute it (see docs/progress/05-whats-missing.md item 6)\n",
                     missing.c_str());
        return 2;
    }

    // Drive the script through the engine's runtime backbone: loadScript +
    // pump (threads, timers, events) — not a one-shot runScript.
    eaw::ScriptManager& scripts = sim->scripts();
    std::string loadKey = toUpper(scriptPath);
    try {
        scripts.loadScript(loadKey);
        std::printf("loaded %s\n", scriptPath.c_str());
    } catch (const eaw::LuaError& ex) {
        std::fprintf(stderr, "load error (%s): %s\n", scriptPath.c_str(), ex.what());
        return 1;
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "could not read script (%s): %s\n",
                     scriptPath.c_str(), ex.what());
        return 1;
    }

    const double dt = 1.0 / 30.0;
    try {
        for (int i = 0; i < ticks; ++i) sim->tick(dt);
    } catch (const eaw::LuaError& ex) {
        std::fprintf(stderr, "runtime error while pumping (%s): %s\n",
                     scriptPath.c_str(), ex.what());
        return 1;
    }
    std::printf("ran %d ticks (%.1fs sim time), %d live script threads\n",
                ticks, ticks * dt, scripts.threadCount());
    return 0;
}

void usage() {
    std::printf(
        "usage:\n"
        "  mod_tool scan <mod_dir> [--game <base game dir>]\n"
        "  mod_tool battle <mod_dir> [--game <base game dir>]\n"
        "                 [--workers N] [--ticks N] [--fighters N] [--capitals N]\n"
        "                 [--land]\n"
        "  mod_tool galaxy <mod_dir> [--game <base game dir>]\n"
        "  mod_tool bindings <mod_dir>\n"
        "  mod_tool run <mod_dir> <script-path> [--game <base game dir>]\n"
        "                 [--ticks N]\n");
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) { usage(); return 1; }
    std::string cmd = argv[1];
    std::string modRoot = argv[2];
    std::string gameRoot;
    std::string scriptPath;
    BattleConfig cfg;
    int runTicks = 120;
    for (int i = 3; i < argc; ++i) {
        if (std::strcmp(argv[i], "--game") == 0 && i + 1 < argc) {
            gameRoot = argv[++i];
        } else if (std::strcmp(argv[i], "--workers") == 0 && i + 1 < argc) {
            cfg.workers = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--ticks") == 0 && i + 1 < argc) {
            cfg.ticks = std::atoi(argv[++i]);
            runTicks = cfg.ticks;
        } else if (std::strcmp(argv[i], "--fighters") == 0 && i + 1 < argc) {
            cfg.fighters = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--capitals") == 0 && i + 1 < argc) {
            cfg.capitals = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--land") == 0) {
            cfg.land = true;
        } else if (scriptPath.empty()) {
            scriptPath = argv[i];
        }
    }
    if (cmd == "scan") return cmdScan(modRoot, gameRoot);
    if (cmd == "battle") return cmdBattle(modRoot, gameRoot, cfg);
    if (cmd == "galaxy") return cmdGalaxy(modRoot, gameRoot);
    if (cmd == "bindings") return cmdBindings(modRoot);
    if (cmd == "run") {
        if (scriptPath.empty()) { usage(); return 1; }
        return cmdRun(modRoot, scriptPath, gameRoot, runTicks);
    }
    usage();
    return 1;
}
