// sim_tool — headless battle runner for the engine.
//
// Boots a scripted fleet battle (Rebel vs Empire fighters + capitals),
// runs it through the parallel Simulation, and reports the outcome.
//
// Usage:
//   sim_tool battle [--workers N] [--ticks N] [--fighters N] [--escorts N]
//                   [--capitals N] [--game <dir-with-Data\*.XML>]
//   sim_tool battle --compare
//
// --compare runs the same battle with 1 worker and with (hardware-1) workers,
// printing both timings and verifying the outcome is identical — the
// determinism + speedup demo for the multithreading rewrite.
#include "core/simulation.h"
#include "core/unit_data_loader.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

namespace {

struct BattleConfig {
    unsigned workers = 0; // 0 = hardware-1
    int ticks = 3600;     // max ticks (120 s at 30 Hz)
    int fighters = 24;    // per side
    int escorts = 0;      // per side (corvettes)
    int capitals = 4;     // per side
    bool land = false;    // land battle (infantry + vehicles)
    const char* gameRoot = ""; // game Data dir for real unit XML (optional)
};

struct BattleResult {
    int ticks = 0;
    double simTime = 0;
    int rebelAlive = 0, empireAlive = 0;
    int rebelStart = 0, empireStart = 0;
    unsigned long long shots = 0;
    double wallMs = 0;
    double ticksPerSec = 0;
    bool decisive = false;
};

void addType(eaw::SimState& sim, const char* name, double dmg, double rate,
             double range) {
    eaw::ObjectType t;
    t.name = name;
    t.properties = {"Unit"};
    t.damage = dmg;
    t.attackRate = rate;
    t.maxRange = range;
    t.affiliatedFactions = {name[0] == 'R' ? "REBEL" : "EMPIRE"};
    sim.addType(std::move(t));
}

std::string readFile(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return std::string((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
}

// Loads unit stats from the real game XML when available (config.meg
// extracted to disk), falling back to hand-tuned defaults otherwise.
void loadUnitTypes(eaw::SimState& state, const char* gameRoot) {
    bool loaded = false;
    if (gameRoot && *gameRoot) {
        eaw::UnitDataLoader loader;
        std::string base = std::string(gameRoot) + "\\Data\\";
        const char* files[] = {"SPACEUNITSFIGHTERS.XML",
                               "SPACEUNITSCAPITAL.XML",
                               "SPACEUNITSCORVETTES.XML",
                               "SPACEUNITSFRIGATES.XML",
                               "SPACEUNITSSUPERS.XML"};
        std::vector<eaw::ObjectType> all;
        for (const char* file : files) {
            std::string xml = readFile((base + file).c_str());
            if (!xml.empty()) {
                try {
                    auto types = loader.loadXml(xml);
                    all.insert(all.end(), types.begin(), types.end());
                } catch (const std::exception&) {
                    // fall through to defaults
                }
            }
        }
        // Keep only combat-relevant units with real stats.
        for (auto& t : all) {
            if (t.damage > 0.0 && t.maxRange > 0.0) {
                state.addType(std::move(t));
                loaded = true;
            }
        }
    }
    if (!loaded) {
        // Defaults: fast weak fighters + slow strong capitals.
        addType(state, "REBEL_FIGHTER", 0.010, 4.0, 300);
        addType(state, "REBEL_CAPITAL", 0.050, 0.8, 500);
        addType(state, "EMPIRE_FIGHTER", 0.011, 4.0, 300);
        addType(state, "EMPIRE_CAPITAL", 0.055, 0.8, 500);
    }
}

// Spawns one side's fleet in a loose formation around (cx, cy). Only spawns
// types that exist in the sim (real-data load may lack some names). `spacing`
// is the gap between units (12 = space scale, 4 = land scale).
void spawnFleet(eaw::SimState& sim, const std::string& fighterType,
                const std::string& capitalType, int playerId,
                int fighters, int capitals, double cx, double cy,
                double spacing = 12.0) {
    for (int i = 0; i < fighters; ++i) {
        if (!sim.type(fighterType)) break;
        double x = cx + (i % 8) * spacing;
        double y = cy + (i / 8) * spacing;
        double z = (i % 3) * 15.0; // stacked altitudes — 3D battle
        sim.addObject(fighterType, playerId, {x, y, z});
    }
    for (int i = 0; i < capitals; ++i) {
        if (!sim.type(capitalType)) break;
        double x = cx + (i % 2) * spacing * 3.0;
        double y = cy + (i / 2) * spacing * 3.0;
        sim.addObject(capitalType, playerId, {x, y, 10.0});
    }
}

// Orders every unit to attack the first enemy of the matching class and to
// move toward the enemy formation. Works with real unit names: we pick the
// actual fighter/capital types per side at the C++ level and pass the names
// into the script.
void orderAttack(eaw::ScriptManager& scripts, const std::string& rebelFighter,
                 const std::string& rebelCapital, const std::string& empireFighter,
                 const std::string& empireCapital) {
    std::string s =
        "for i, o in ipairs(Find_All_Objects_Of_Type('" + rebelFighter + "')) do\n"
        "  o:Attack_Target(Find_First_Object('" + empireFighter + "'))\n"
        "  o:Move_To(Find_First_Object('" + empireCapital + "'))\n"
        "end\n"
        "for i, o in ipairs(Find_All_Objects_Of_Type('" + rebelCapital + "')) do\n"
        "  o:Attack_Target(Find_First_Object('" + empireCapital + "'))\n"
        "  o:Move_To(Find_First_Object('" + empireFighter + "'))\n"
        "end\n"
        "for i, o in ipairs(Find_All_Objects_Of_Type('" + empireFighter + "')) do\n"
        "  o:Attack_Target(Find_First_Object('" + rebelFighter + "'))\n"
        "  o:Move_To(Find_First_Object('" + rebelCapital + "'))\n"
        "end\n"
        "for i, o in ipairs(Find_All_Objects_Of_Type('" + empireCapital + "')) do\n"
        "  o:Attack_Target(Find_First_Object('" + rebelCapital + "'))\n"
        "  o:Move_To(Find_First_Object('" + rebelFighter + "'))\n"
        "end\n";
    scripts.runScript(s);
}

// Picks the fighter/capital/escort type names per side, preferring real game
// units when the loader found them.
void pickFleetTypes(eaw::SimState& state, bool realData,
                    std::string& rebelFighter, std::string& rebelCapital,
                    std::string& empireFighter, std::string& empireCapital,
                    std::string& rebelEscort, std::string& empireEscort) {
    if (realData) {
        // Prefer iconic units; fall back to whatever loaded.
        const char* rF[] = {"X-Wing", "A-Wing", nullptr};
        const char* eF[] = {"TIE_Fighter", "TIE_Interceptor", nullptr};
        const char* rC[] = {"Calamari_Cruiser", "Home_One", "Nebulon_B", nullptr};
        const char* eC[] = {"Generic_Star_Destroyer", "Star_Destroyer", "Victory_Star_Destroyer", nullptr};
        const char* rE[] = {"Corellian_Corvette", "Tantive_IV", "Corellian_Gunboat", nullptr};
        const char* eE[] = {"Tartan_Patrol_Cruiser", "IPV1_System_Patrol_Craft", "Broadside_Class_Cruiser", nullptr};
        auto pick = [&](const char* const* names) -> std::string {
            for (int i = 0; names[i]; ++i) {
                if (state.type(names[i])) return names[i];
            }
            // First combat unit in the sim.
            for (const std::string& n : state.typeNames()) {
                const eaw::ObjectType* t = state.type(n);
                if (t && t->damage > 0) return n;
            }
            return "";
        };
        rebelFighter = pick(rF);
        empireFighter = pick(eF);
        rebelCapital = pick(rC);
        empireCapital = pick(eC);
        rebelEscort = pick(rE);
        empireEscort = pick(eE);
        return;
    }
    rebelFighter = "REBEL_FIGHTER";
    rebelCapital = "REBEL_CAPITAL";
    empireFighter = "EMPIRE_FIGHTER";
    empireCapital = "EMPIRE_CAPITAL";
    rebelEscort = "";
    empireEscort = "";
}

// Loads land unit stats from the real game XML (GroundVehicle blocks in the
// UNITS_LAND_*/MOBILE_DEFENSE_UNITS/TRANSPORTUNITS files), falling back to
// hand-tuned defaults.
void loadLandTypes(eaw::SimState& state, const char* gameRoot) {
    bool loaded = false;
    if (gameRoot && *gameRoot) {
        eaw::UnitDataLoader loader;
        std::string base = std::string(gameRoot) + "\\Data\\";
        const char* files[] = {"UNITS_LAND_EMPIRE_DARKTROOPERS.XML",
                               "UNITS_LAND_EMPIRE_JUGGERNAUT.XML",
                               "UNITS_LAND_EMPIRE_LANCET.XML",
                               "UNITS_LAND_EMPIRE_NOGHRI.XML",
                               "MOBILE_DEFENSE_UNITS.XML",
                               "TRANSPORTUNITS.XML"};
        std::vector<eaw::ObjectType> all;
        for (const char* file : files) {
            std::string xml = readFile((base + file).c_str());
            if (!xml.empty()) {
                try {
                    auto types = loader.loadXml(xml);
                    all.insert(all.end(), types.begin(), types.end());
                } catch (const std::exception&) {
                    // fall through
                }
            }
        }
        for (auto& t : all) {
            if (t.damage > 0.0 && t.maxRange > 0.0) {
                state.addType(std::move(t));
                loaded = true;
            }
        }
    }
    if (!loaded) {
        addType(state, "REBEL_INFANTRY", 0.020, 1.0, 100);
        addType(state, "REBEL_VEHICLE", 0.030, 0.5, 200);
        addType(state, "EMPIRE_INFANTRY", 0.022, 1.0, 100);
        addType(state, "EMPIRE_VEHICLE", 0.033, 0.5, 200);
    }
}

// Picks infantry/vehicle names for a land battle.
void pickLandTypes(eaw::SimState& state, bool realData,
                   std::string& rebelInf, std::string& rebelVeh,
                   std::string& empireInf, std::string& empireVeh) {
    if (realData) {
        const char* rI[] = {"Dark_Trooper_PhaseI", "Dark_Trooper_PhaseII", nullptr};
        const char* eI[] = {"Dark_Trooper_PhaseI", "Dark_Trooper_PhaseII", nullptr};
        const char* rV[] = {"HAV_Juggernaut", "Lancet", nullptr};
        const char* eV[] = {"HAV_Juggernaut", "Lancet", nullptr};
        auto pick = [&](const char* const* names) -> std::string {
            for (int i = 0; names[i]; ++i) {
                if (state.type(names[i])) return names[i];
            }
            for (const std::string& n : state.typeNames()) {
                const eaw::ObjectType* t = state.type(n);
                if (t && t->damage > 0) return n;
            }
            return "";
        };
        rebelInf = pick(rI);
        empireInf = pick(eI);
        rebelVeh = pick(rV);
        empireVeh = pick(eV);
        return;
    }
    rebelInf = "REBEL_INFANTRY";
    rebelVeh = "REBEL_VEHICLE";
    empireInf = "EMPIRE_INFANTRY";
    empireVeh = "EMPIRE_VEHICLE";
}

// Orders a land battle: everyone attacks the nearest enemy class.
void orderLandAttack(eaw::ScriptManager& scripts, const std::string& rebelInf,
                     const std::string& rebelVeh, const std::string& empireInf,
                     const std::string& empireVeh) {
    std::string s =
        "for i, o in ipairs(Find_All_Objects_Of_Type('" + rebelInf + "')) do\n"
        "  o:Attack_Target(Find_First_Object('" + empireInf + "'))\n"
        "  o:Move_To(Find_First_Object('" + empireVeh + "'))\n"
        "end\n"
        "for i, o in ipairs(Find_All_Objects_Of_Type('" + rebelVeh + "')) do\n"
        "  o:Attack_Target(Find_First_Object('" + empireVeh + "'))\n"
        "  o:Move_To(Find_First_Object('" + empireInf + "'))\n"
        "end\n"
        "for i, o in ipairs(Find_All_Objects_Of_Type('" + empireInf + "')) do\n"
        "  o:Attack_Target(Find_First_Object('" + rebelInf + "'))\n"
        "  o:Move_To(Find_First_Object('" + rebelVeh + "'))\n"
        "end\n"
        "for i, o in ipairs(Find_All_Objects_Of_Type('" + empireVeh + "')) do\n"
        "  o:Attack_Target(Find_First_Object('" + rebelVeh + "'))\n"
        "  o:Move_To(Find_First_Object('" + rebelInf + "'))\n"
        "end\n";
    scripts.runScript(s);
}

BattleResult runBattle(const BattleConfig& cfg) {
    eaw::Simulation sim(cfg.workers);
    eaw::SimState& state = sim.sim();

    eaw::Player& rebel = state.addPlayer("Rebel Alliance", "REBEL");
    eaw::Player& empire = state.addPlayer("Galactic Empire", "EMPIRE");
    rebel.human = true;

    if (cfg.land) {
        loadLandTypes(state, cfg.gameRoot);
        bool realData = state.type("HAV_Juggernaut") ||
                        state.type("Dark_Trooper_PhaseI");
        std::string rebelInf, rebelVeh, empireInf, empireVeh;
        pickLandTypes(state, realData, rebelInf, rebelVeh, empireInf, empireVeh);
        auto ensureType = [&](std::string& name, const char* fallbackName,
                              double dmg, double rate, double range) {
            if (name.empty() || !state.type(name)) {
                addType(state, fallbackName, dmg, rate, range);
                name = fallbackName;
            }
        };
        ensureType(rebelInf, "REBEL_INFANTRY", 0.020, 1.0, 100);
        ensureType(rebelVeh, "REBEL_VEHICLE", 0.030, 0.5, 200);
        ensureType(empireInf, "EMPIRE_INFANTRY", 0.022, 1.0, 100);
        ensureType(empireVeh, "EMPIRE_VEHICLE", 0.033, 0.5, 200);
        // Land maps are smaller: compact formation, forces start closer.
        spawnFleet(state, rebelInf, rebelVeh, rebel.id,
                   cfg.fighters, cfg.capitals, 0, 0, 4.0);
        spawnFleet(state, empireInf, empireVeh, empire.id,
                   cfg.fighters, cfg.capitals, 60, 0, 4.0);
        orderLandAttack(sim.scripts(), rebelInf, rebelVeh, empireInf, empireVeh);
        std::printf("  fleets      : %s x%d + %s x%d vs %s x%d + %s x%d%s\n",
                    rebelInf.c_str(), cfg.fighters, rebelVeh.c_str(),
                    cfg.capitals, empireInf.c_str(), cfg.fighters,
                    empireVeh.c_str(), cfg.capitals,
                    realData ? " [real game data]" : "");
        BattleResult res;
        res.rebelStart = cfg.fighters + cfg.capitals;
        res.empireStart = cfg.fighters + cfg.capitals;
        auto t0 = std::chrono::steady_clock::now();
        const double dt = 1.0 / 30.0;
        for (int i = 0; i < cfg.ticks; ++i) {
            sim.tick(dt);
            int rebelAlive = 0, empireAlive = 0;
            for (const eaw::GameObject* o : state.allObjects()) {
                if (!o->alive) continue;
                const eaw::Player* p = state.player(o->playerId);
                if (p && p->factionName == "REBEL") ++rebelAlive;
                else if (p && p->factionName == "EMPIRE") ++empireAlive;
            }
            if (rebelAlive == 0 || empireAlive == 0) {
                res.decisive = true;
                res.ticks = i + 1;
                res.rebelAlive = rebelAlive;
                res.empireAlive = empireAlive;
                break;
            }
            res.rebelAlive = rebelAlive;
            res.empireAlive = empireAlive;
            res.ticks = i + 1;
        }
        auto t1 = std::chrono::steady_clock::now();
        res.wallMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
        res.simTime = res.ticks * dt;
        res.ticksPerSec = res.ticks / (res.wallMs / 1000.0);
        res.shots = sim.totalShots();
        return res;
    }

    loadUnitTypes(state, cfg.gameRoot);
    bool realData = state.type("X-Wing") || state.type("TIE_Fighter") ||
                    state.type("ISD");

    std::string rebelFighter, rebelCapital, empireFighter, empireCapital;
    std::string rebelEscort, empireEscort;
    pickFleetTypes(state, realData, rebelFighter, rebelCapital,
                   empireFighter, empireCapital, rebelEscort, empireEscort);
    // Fill in any missing side/class with a default so the battle always has
    // both fleets.
    auto ensureType = [&](std::string& name, const char* fallbackName,
                          double dmg, double rate, double range) {
        if (name.empty() || !state.type(name)) {
            addType(state, fallbackName, dmg, rate, range);
            name = fallbackName;
        }
    };
    ensureType(rebelFighter, "REBEL_FIGHTER", 0.010, 4.0, 300);
    ensureType(rebelCapital, "REBEL_CAPITAL", 0.050, 0.8, 500);
    ensureType(empireFighter, "EMPIRE_FIGHTER", 0.011, 4.0, 300);
    ensureType(empireCapital, "EMPIRE_CAPITAL", 0.055, 0.8, 500);

    spawnFleet(state, rebelFighter, rebelCapital, rebel.id,
               cfg.fighters, cfg.capitals, 0, 0);
    spawnFleet(state, empireFighter, empireCapital, empire.id,
               cfg.fighters, cfg.capitals, 800, 0);
    // Escorts (corvettes) if configured and available.
    if (cfg.escorts > 0 && !rebelEscort.empty() && state.type(rebelEscort)) {
        spawnFleet(state, rebelEscort, "", rebel.id, cfg.escorts, 0, -80, 0);
    }
    if (cfg.escorts > 0 && !empireEscort.empty() && state.type(empireEscort)) {
        spawnFleet(state, empireEscort, "", empire.id, cfg.escorts, 0, 880, 0);
    }

    orderAttack(sim.scripts(), rebelFighter, rebelCapital,
                empireFighter, empireCapital);

    // AI production: the empire (non-human) builds its roster over time.
    sim.setAiBuildTypes({empireFighter, empireCapital});
    sim.sim().giveMoney(empire.id, 5000.0);

    std::printf("  fleets      : %s x%d + %s x%d%s vs %s x%d + %s x%d%s%s\n",
                rebelFighter.c_str(), cfg.fighters, rebelCapital.c_str(),
                cfg.capitals,
                (cfg.escorts > 0 && !rebelEscort.empty())
                    ? (" + " + rebelEscort + " x" + std::to_string(cfg.escorts)).c_str()
                    : "",
                empireFighter.c_str(), cfg.fighters,
                empireCapital.c_str(), cfg.capitals,
                (cfg.escorts > 0 && !empireEscort.empty())
                    ? (" + " + empireEscort + " x" + std::to_string(cfg.escorts)).c_str()
                    : "",
                realData ? " [real game data]" : "");

    BattleResult res;
    res.rebelStart = cfg.fighters + cfg.capitals + cfg.escorts;
    res.empireStart = cfg.fighters + cfg.capitals + cfg.escorts;

    auto t0 = std::chrono::steady_clock::now();
    const double dt = 1.0 / 30.0;
    for (int i = 0; i < cfg.ticks; ++i) {
        sim.tick(dt);
        int rebelAlive = 0, empireAlive = 0;
        for (const eaw::GameObject* o : state.allObjects()) {
            if (!o->alive) continue;
            const eaw::Player* p = state.player(o->playerId);
            if (p && p->factionName == "REBEL") ++rebelAlive;
            else if (p && p->factionName == "EMPIRE") ++empireAlive;
        }
        if (rebelAlive == 0 || empireAlive == 0) {
            res.decisive = true;
            res.ticks = i + 1;
            res.rebelAlive = rebelAlive;
            res.empireAlive = empireAlive;
            break;
        }
        res.rebelAlive = rebelAlive;
        res.empireAlive = empireAlive;
        res.ticks = i + 1;
    }
    auto t1 = std::chrono::steady_clock::now();
    res.wallMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    res.simTime = res.ticks * dt;
    res.ticksPerSec = res.ticks / (res.wallMs / 1000.0);
    res.shots = sim.totalShots();
    return res;
}

void printReport(const BattleConfig& cfg, const BattleResult& r, const char* label) {
    std::printf("%s:\n", label);
    std::printf("  workers     : %u\n", cfg.workers ? cfg.workers : std::thread::hardware_concurrency() - 1);
    std::printf("  ticks       : %d (%.1f s sim time)\n", r.ticks, r.simTime);
    std::printf("  wall time   : %.1f ms  (%.0f ticks/s)\n", r.wallMs, r.ticksPerSec);
    std::printf("  shots fired : %llu\n", r.shots);
    std::printf("  rebels      : %d/%d alive\n", r.rebelAlive, r.rebelStart);
    std::printf("  empire      : %d/%d alive\n", r.empireAlive, r.empireStart);
    if (r.decisive) {
        std::printf("  outcome     : %s VICTORY\n",
                    r.rebelAlive > 0 ? "REBEL" : "EMPIRE");
    } else {
        std::printf("  outcome     : undecided (tick cap reached)\n");
    }
}

int cmdBattle(const BattleConfig& cfg) {
    BattleResult r = runBattle(cfg);
    printReport(cfg, r, "battle");
    return 0;
}

int cmdCompare(const BattleConfig& base) {
    BattleConfig single = base;
    single.workers = 1;
    BattleConfig multi = base;
    multi.workers = 0; // hardware-1

    BattleResult r1 = runBattle(single);
    BattleResult rn = runBattle(multi);
    printReport(single, r1, "single-threaded");
    printReport(multi, rn, "multi-threaded");

    bool same = r1.ticks == rn.ticks &&
                r1.rebelAlive == rn.rebelAlive &&
                r1.empireAlive == rn.empireAlive &&
                r1.shots == rn.shots;
    std::printf("\n%s\n", same ? "DETERMINISTIC: identical outcomes" : "MISMATCH: outcomes differ!");
    if (same && rn.wallMs > 0) {
        std::printf("speedup: %.2fx (multi vs single)\n", r1.wallMs / rn.wallMs);
    }
    return same ? 0 : 1;
}

void usage() {
    std::printf("usage:\n"
                "  sim_tool battle [--workers N] [--ticks N] [--fighters N] [--escorts N]\n"
                "                [--capitals N] [--game <dir-with-Data\\*.XML>] [--land]\n"
                "  sim_tool battle --compare\n");
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { usage(); return 1; }
    if (std::strcmp(argv[1], "battle") != 0) { usage(); return 1; }

    BattleConfig cfg;
    bool compare = false;
    for (int i = 2; i < argc; ++i) {
        auto need = [&](const char* name) -> const char* {
            if (i + 1 >= argc) { std::fprintf(stderr, "missing value for %s\n", name); std::exit(1); }
            return argv[++i];
        };
        if (std::strcmp(argv[i], "--workers") == 0) cfg.workers = std::atoi(need("--workers"));
        else if (std::strcmp(argv[i], "--ticks") == 0) cfg.ticks = std::atoi(need("--ticks"));
        else if (std::strcmp(argv[i], "--fighters") == 0) cfg.fighters = std::atoi(need("--fighters"));
        else if (std::strcmp(argv[i], "--escorts") == 0) cfg.escorts = std::atoi(need("--escorts"));
        else if (std::strcmp(argv[i], "--capitals") == 0) cfg.capitals = std::atoi(need("--capitals"));
        else if (std::strcmp(argv[i], "--game") == 0) cfg.gameRoot = need("--game");
        else if (std::strcmp(argv[i], "--land") == 0) cfg.land = true;
        else if (std::strcmp(argv[i], "--compare") == 0) compare = true;
        else { std::fprintf(stderr, "unknown option: %s\n", argv[i]); return 1; }
    }

    if (compare) return cmdCompare(cfg);
    return cmdBattle(cfg);
}
