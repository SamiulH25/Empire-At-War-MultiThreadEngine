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

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

struct BattleConfig {
    unsigned workers = 0; // 0 = hardware-1
    int ticks = 3600;     // max ticks (120 s at 30 Hz)
    int fighters = 24;    // per side
    int capitals = 4;     // per side
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

// Spawns one side's fleet in a loose formation around (cx, cy).
void spawnFleet(eaw::SimState& sim, const std::string& fighterType,
                const std::string& capitalType, int playerId,
                int fighters, int capitals, double cx, double cy) {
    for (int i = 0; i < fighters; ++i) {
        double x = cx + (i % 8) * 12.0;
        double y = cy + (i / 8) * 12.0;
        double z = (i % 3) * 15.0; // stacked altitudes — 3D battle
        auto& o = sim.addObject(fighterType, playerId, {x, y, z});
        o.moveSpeed = 90.0;
    }
    for (int i = 0; i < capitals; ++i) {
        double x = cx + (i % 2) * 40.0;
        double y = cy + (i / 2) * 40.0;
        auto& o = sim.addObject(capitalType, playerId, {x, y, 10.0});
        o.moveSpeed = 25.0;
    }
}

// Orders every unit to attack the first enemy of the matching class and to
// move toward the enemy formation (exercises the Lua bindings end to end).
void orderAttack(eaw::ScriptManager& scripts) {
    scripts.runScript(
        "for i, o in ipairs(Find_All_Objects_Of_Type('REBEL_FIGHTER')) do\n"
        "  o:Attack_Target(Find_First_Object('EMPIRE_FIGHTER'))\n"
        "  o:Move_To(Find_First_Object('EMPIRE_CAPITAL'))\n"
        "end\n"
        "for i, o in ipairs(Find_All_Objects_Of_Type('REBEL_CAPITAL')) do\n"
        "  o:Attack_Target(Find_First_Object('EMPIRE_CAPITAL'))\n"
        "  o:Move_To(Find_First_Object('EMPIRE_FIGHTER'))\n"
        "end\n"
        "for i, o in ipairs(Find_All_Objects_Of_Type('EMPIRE_FIGHTER')) do\n"
        "  o:Attack_Target(Find_First_Object('REBEL_FIGHTER'))\n"
        "  o:Move_To(Find_First_Object('REBEL_CAPITAL'))\n"
        "end\n"
        "for i, o in ipairs(Find_All_Objects_Of_Type('EMPIRE_CAPITAL')) do\n"
        "  o:Attack_Target(Find_First_Object('REBEL_CAPITAL'))\n"
        "  o:Move_To(Find_First_Object('REBEL_FIGHTER'))\n"
        "end\n");
}

BattleResult runBattle(const BattleConfig& cfg) {
    eaw::Simulation sim(cfg.workers);
    eaw::SimState& state = sim.sim();

    eaw::Player& rebel = state.addPlayer("Rebel Alliance", "REBEL");
    eaw::Player& empire = state.addPlayer("Galactic Empire", "EMPIRE");
    rebel.human = true;

    // Fighter: fast, weak; Capital: slow, strong (damage per shot, rate/s).
    addType(state, "REBEL_FIGHTER", 0.010, 4.0, 300);
    addType(state, "REBEL_CAPITAL", 0.050, 0.8, 500);
    addType(state, "EMPIRE_FIGHTER", 0.011, 4.0, 300);
    addType(state, "EMPIRE_CAPITAL", 0.055, 0.8, 500);

    spawnFleet(state, "REBEL_FIGHTER", "REBEL_CAPITAL", rebel.id,
               cfg.fighters, cfg.capitals, 0, 0);
    spawnFleet(state, "EMPIRE_FIGHTER", "EMPIRE_CAPITAL", empire.id,
               cfg.fighters, cfg.capitals, 800, 0);

    orderAttack(sim.scripts());

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
        else if (std::strcmp(argv[i], "--compare") == 0) compare = true;
        else { std::fprintf(stderr, "unknown option: %s\n", argv[i]); return 1; }
    }

    if (compare) return cmdCompare(cfg);
    return cmdBattle(cfg);
}
