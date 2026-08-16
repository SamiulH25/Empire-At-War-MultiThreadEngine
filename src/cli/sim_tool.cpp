// sim_tool — headless battle runner for the engine.
//
// Boots a scripted fleet battle (Rebel vs Empire fighters + capitals),
// runs it through the parallel Simulation, and reports the outcome.
//
// Usage:
//   sim_tool battle [--workers N] [--ticks N] [--fighters N] [--capitals N]
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
    int capitals = 4;     // per side
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
                               "SPACEUNITSCAPITAL.XML"};
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
// types that exist in the sim (real-data load may lack some names).
void spawnFleet(eaw::SimState& sim, const std::string& fighterType,
                const std::string& capitalType, int playerId,
                int fighters, int capitals, double cx, double cy) {
    for (int i = 0; i < fighters; ++i) {
        if (!sim.type(fighterType)) break;
        double x = cx + (i % 8) * 12.0;
        double y = cy + (i / 8) * 12.0;
        double z = (i % 3) * 15.0; // stacked altitudes — 3D battle
        sim.addObject(fighterType, playerId, {x, y, z});
    }
    for (int i = 0; i < capitals; ++i) {
        if (!sim.type(capitalType)) break;
        double x = cx + (i % 2) * 40.0;
        double y = cy + (i / 2) * 40.0;
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

// Picks the fighter/capital type names per side, preferring real game units
// when the loader found them.
void pickFleetTypes(eaw::SimState& state, bool realData,
                    std::string& rebelFighter, std::string& rebelCapital,
                    std::string& empireFighter, std::string& empireCapital) {
    if (realData) {
        // Prefer iconic units; fall back to whatever loaded.
        const char* rF[] = {"X-Wing", "A-Wing", nullptr};
        const char* eF[] = {"TIE_Fighter", "TIE_Interceptor", nullptr};
        const char* rC[] = {"Calamari_Cruiser", "Home_One", "Nebulon_B", nullptr};
        const char* eC[] = {"Generic_Star_Destroyer", "Star_Destroyer", "Victory_Star_Destroyer", nullptr};
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
        return;
    }
    rebelFighter = "REBEL_FIGHTER";
    rebelCapital = "REBEL_CAPITAL";
    empireFighter = "EMPIRE_FIGHTER";
    empireCapital = "EMPIRE_CAPITAL";
}

BattleResult runBattle(const BattleConfig& cfg) {
    eaw::Simulation sim(cfg.workers);
    eaw::SimState& state = sim.sim();

    eaw::Player& rebel = state.addPlayer("Rebel Alliance", "REBEL");
    eaw::Player& empire = state.addPlayer("Galactic Empire", "EMPIRE");
    rebel.human = true;

    loadUnitTypes(state, cfg.gameRoot);
    bool realData = state.type("X-Wing") || state.type("TIE_Fighter") ||
                    state.type("ISD");

    std::string rebelFighter, rebelCapital, empireFighter, empireCapital;
    pickFleetTypes(state, realData, rebelFighter, rebelCapital,
                   empireFighter, empireCapital);
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

    orderAttack(sim.scripts(), rebelFighter, rebelCapital,
                empireFighter, empireCapital);

    std::printf("  fleets      : %s x%d + %s x%d vs %s x%d + %s x%d%s\n",
                rebelFighter.c_str(), cfg.fighters, rebelCapital.c_str(),
                cfg.capitals, empireFighter.c_str(), cfg.fighters,
                empireCapital.c_str(), cfg.capitals,
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
                "  sim_tool battle [--workers N] [--ticks N] [--fighters N] [--capitals N]\n"
                "                [--game <dir-with-Data\\*.XML>]\n"
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
        else if (std::strcmp(argv[i], "--capitals") == 0) cfg.capitals = std::atoi(need("--capitals"));
        else if (std::strcmp(argv[i], "--game") == 0) cfg.gameRoot = need("--game");
        else if (std::strcmp(argv[i], "--compare") == 0) compare = true;
        else { std::fprintf(stderr, "unknown option: %s\n", argv[i]); return 1; }
    }

    if (compare) return cmdCompare(cfg);
    return cmdBattle(cfg);
}
