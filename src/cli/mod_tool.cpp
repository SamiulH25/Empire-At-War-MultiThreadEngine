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
#include "core/lua_wrappers.h"
#include "core/meg_file.h"
#include "core/meg_manager.h"
#include "core/simulation.h"
#include "core/unit_data_loader.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
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
    for (const auto& entry : fs::recursive_directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        std::string rel = entry.path().string().substr(base.size() + 1);
        std::replace(rel.begin(), rel.end(), '\\', '/');
        std::string upper = toUpper(rel);
        std::string baseUpper = toUpper(base);
        if (baseUpper.size() >= 4 &&
            baseUpper.substr(baseUpper.size() - 4) == "DATA") {
            upper = "DATA/" + upper;
        }
        out.push_back(upper);
    }
}

// Loads base-game megas from <game>/Data/*.meg (the fallback layer).
void loadBaseMegas(eaw::MegaFileManager& files, const std::string& gameRoot,
                   std::vector<std::vector<uint8_t>>& archiveBytes,
                   std::vector<eaw::MegFile>& megas) {
    if (gameRoot.empty()) return;
    std::string dataDir = gameRoot + "\\Data\\";
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

// Loads a mod's Data folder as loose files (the override layer).
void loadModLoose(eaw::MegaFileManager& files, const std::string& modRoot) {
    std::string dataDir = modRoot + "\\Data";
    std::vector<std::string> rels;
    walkDir(dataDir, dataDir, rels);
    for (const std::string& rel : rels) {
        // rel has the DATA/ prefix; strip it for the disk path.
        std::string diskName = rel;
        if (diskName.rfind("DATA/", 0) == 0) diskName = diskName.substr(5);
        std::string path = dataDir + "\\" + diskName;
        std::string bytes = readFile(path);
        if (!bytes.empty()) {
            files.addLooseFile(rel, std::vector<uint8_t>(bytes.begin(), bytes.end()));
        }
    }
}

// ---- scan ---------------------------------------------------------------

int cmdScan(const std::string& modRoot, const std::string& gameRoot) {
    eaw::MegaFileManager files;
    std::vector<std::vector<uint8_t>> archiveBytes;
    std::vector<eaw::MegFile> megas;
    loadBaseMegas(files, gameRoot, archiveBytes, megas);
    loadModLoose(files, modRoot);

    std::printf("mod root : %s\n", modRoot.c_str());
    std::printf("base megas: %zu loaded", megas.size());
    if (!gameRoot.empty()) std::printf(" (from %s)", gameRoot.c_str());
    std::printf("\nloose files (mod overrides): %zu\n", files.looseCount());

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

void addType(eaw::SimState& state, const std::string& name, double dmg,
             double rate, double range, double cost) {
    eaw::ObjectType t;
    t.name = name;
    t.properties = {"Unit"};
    t.damage = dmg;
    t.attackRate = rate;
    t.maxRange = range;
    t.buildCost = cost;
    state.addType(std::move(t));
}

// Loads every combat unit type visible to the mod (loose overrides win).
std::vector<std::string> loadModUnitTypes(eaw::SimState& state,
                                          eaw::MegaFileManager& files,
                                          bool land) {
    eaw::UnitDataLoader loader;
    std::vector<eaw::ObjectType> all;
    std::vector<std::string> unitFiles;
    if (land) {
        unitFiles = {"DATA/XML/UNITS_LAND_EMPIRE_DARKTROOPERS.XML",
                     "DATA/XML/UNITS_LAND_EMPIRE_JUGGERNAUT.XML",
                     "DATA/XML/MOBILE_DEFENSE_UNITS.XML",
                     "DATA/XML/TRANSPORTUNITS.XML"};
    } else {
        unitFiles = {"DATA/XML/SPACEUNITSFIGHTERS.XML",
                     "DATA/XML/SPACEUNITSCAPITAL.XML",
                     "DATA/XML/SPACEUNITSCORVETTES.XML",
                     "DATA/XML/SPACEUNITSFRIGATES.XML",
                     "DATA/XML/SPACEUNITSSUPERS.XML"};
    }
    for (const std::string& f : unitFiles) {
        if (!files.exists(f)) continue;
        std::vector<uint8_t> bytes = files.read(f);
        std::string text(bytes.begin(), bytes.end());
        try {
            auto types = loader.loadXml(text);
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
    loadBaseMegas(files, gameRoot, archiveBytes, megas);
    loadModLoose(files, modRoot);

    eaw::Simulation sim(cfg.workers);
    eaw::SimState& state = sim.sim();
    eaw::Player& rebel = state.addPlayer("Rebel Alliance", "REBEL");
    eaw::Player& empire = state.addPlayer("Galactic Empire", "EMPIRE");
    rebel.human = true;

    std::vector<std::string> names = loadModUnitTypes(state, files, cfg.land);
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
    loadBaseMegas(files, gameRoot, archiveBytes, megas);
    loadModLoose(files, modRoot);

    eaw::Simulation sim(4);
    eaw::SimState& state = sim.sim();
    eaw::Player& rebel = state.addPlayer("Rebel Alliance", "REBEL");
    eaw::Player& empire = state.addPlayer("Galactic Empire", "EMPIRE");
    eaw::Player& pirate = state.addPlayer("Pirate Factions", "UNDERWORLD");
    rebel.human = true; // only the empire plays AI here

    // Planets from the mod (or base game fallback).
    eaw::UnitDataLoader loader;
    int planets = 0;
    if (files.exists("DATA/XML/PLANETS.XML")) {
        auto bytes = files.read("DATA/XML/PLANETS.XML");
        auto planetList = loader.loadPlanets(std::string(bytes.begin(), bytes.end()));
        for (auto& p : planetList) {
            // Credit value drives income: planets contribute to their owner.
            state.addPlanet(p.name, "", p.position);
            ++planets;
        }
    }
    std::printf("planets loaded: %d\n", planets);
    if (planets == 0) {
        std::fprintf(stderr, "no PLANETS.XML found\n");
        return 1;
    }

    // Combat units for the fleets (mod's unit XML).
    std::vector<std::string> names = loadModUnitTypes(state, files, false);
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

// The engine's registered binding names (from pg_bindings + object + event +
// taskforce surfaces).
std::set<std::string> engineBindings() {
    return {
        // threads / time / globals (pg_bindings)
        "Create_Thread", "Thread", "Thread_Kill", "Thread_Is_Thread_Active",
        "GetCurrentTime", "GameRandom", "GameRandom_Get_Float", "Get_Game_Mode",
        "_ScriptMessage", "lc", "GetThreadID",
        // objects (pg_object_bindings)
        "Find_Player", "Find_Object_Type", "Find_All_Objects_Of_Type",
        "Find_First_Object", "Find_Nearest", "Spawn_Unit", "Reinforce_Unit",
        "Create_Position",
        // events (pg_event_bindings)
        "Register_Timer", "Register_Death_Event", "Register_Attacked_Event",
        "Register_Prox", "Cancel_Attacked_Event", "Process_Timers",
        "Process_Death_Events", "Process_Attacked_Events", "Process_Proximities",
        "Pump_Service",
        // galactic (pg_taskforce_bindings)
        "FindPlanet",
    };
}

// The documented bindings mods may call that we have NOT registered yet.
std::set<std::string> documentedButMissing() {
    return {
        "Activate_Ability", "Try_Ability", "Use_Ability_If_Able",
        "Has_Ability", "Is_Ability_Active", "Is_Ability_Ready",
        "Get_Difficulty", "Get_Faction_Name", "Get_Tech_Level",
        "Find_Path", "FindPlanet", "Move_To", "Attack_Move",
        "Attack_Target", "Formation_Attack", "Formation_Move",
        "Release_Unit", "Lock_Current_Orders", "Unlock_Current_Orders",
        "Get_Unit_Table", "Get_Garrisoned_Units", "Get_Force_Count",
        "Set_Targeting_Priorities", "Set_Land_AI_Targeting_Priorities",
        "Get_Owner", "Get_Type", "Get_Name", "Get_ID", "Get_Hull",
        "Get_Shield", "Get_Health", "Get_Energy", "Get_Position",
        "Get_Distance", "Is_Category", "Has_Property", "Is_Valid",
        "Is_Hero", "Has_Attack_Target", "Get_Attack_Target",
        "Can_Garrison", "Garrison", "Leave_Garrison", "Eject_Garrison",
        "Take_Damage", "Despawn", "Make_Invulnerable", "Set_Selectable",
        "Give_Money", "Get_Credits", "Set_Tech_Level", "Unlock_Tech",
        "Lock_Tech", "Make_Ally", "Make_Enemy", "Is_Ally", "Is_Enemy",
        "Get_Enemy", "Spawn_Unit", "Reinforce_Unit",
    };
}

int cmdBindings(const std::string& modRoot) {
    std::string dataDir = modRoot + "\\Data";
    std::vector<std::string> rels;
    walkDir(dataDir, dataDir, rels);
    std::set<std::string> used;
    std::set<std::string> usedInFiles;
    for (const std::string& rel : rels) {
        if (!hasSuffix(rel, ".lua")) continue;
        std::string diskName = rel;
        if (diskName.rfind("DATA/", 0) == 0) diskName = diskName.substr(5);
        std::string text = readFile(dataDir + "\\" + diskName);
        // Extract identifier-ish tokens (uppercased names) and match against
        // the binding set (bytecode string tables are also readable this way).
        for (const std::string& b : engineBindings()) {
            if (text.find(b) != std::string::npos) {
                used.insert(b);
                usedInFiles.insert(rel);
            }
        }
        for (const std::string& b : documentedButMissing()) {
            if (text.find(b) != std::string::npos) {
                used.insert(b);
                usedInFiles.insert(rel);
            }
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

void usage() {
    std::printf(
        "usage:\n"
        "  mod_tool scan <mod_dir> [--game <base game dir>]\n"
        "  mod_tool battle <mod_dir> [--game <base game dir>]\n"
        "                 [--workers N] [--ticks N] [--fighters N] [--capitals N]\n"
        "                 [--land]\n"
        "  mod_tool galaxy <mod_dir> [--game <base game dir>]\n"
        "  mod_tool bindings <mod_dir>\n");
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) { usage(); return 1; }
    std::string cmd = argv[1];
    std::string modRoot = argv[2];
    std::string gameRoot;
    BattleConfig cfg;
    for (int i = 3; i < argc; ++i) {
        if (std::strcmp(argv[i], "--game") == 0 && i + 1 < argc) {
            gameRoot = argv[++i];
        } else if (std::strcmp(argv[i], "--workers") == 0 && i + 1 < argc) {
            cfg.workers = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--ticks") == 0 && i + 1 < argc) {
            cfg.ticks = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--fighters") == 0 && i + 1 < argc) {
            cfg.fighters = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--capitals") == 0 && i + 1 < argc) {
            cfg.capitals = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--land") == 0) {
            cfg.land = true;
        }
    }
    if (cmd == "scan") return cmdScan(modRoot, gameRoot);
    if (cmd == "battle") return cmdBattle(modRoot, gameRoot, cfg);
    if (cmd == "galaxy") return cmdGalaxy(modRoot, gameRoot);
    if (cmd == "bindings") return cmdBindings(modRoot);
    usage();
    return 1;
}
