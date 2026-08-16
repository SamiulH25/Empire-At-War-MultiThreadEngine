// Tests for PathGrid + PathfindingSystem (frame-sliced A*).
#include "core/job_system.h"
#include "core/path_grid.h"
#include "core/pathfinding.h"

#include <cstdio>
#include <set>
#include <vector>

namespace {

int failures = 0;
void check(bool c, const char* w) {
    std::printf("%s: %s\n", c ? "ok" : "FAIL", w);
    if (!c) ++failures;
}

void testGridBasics() {
    eaw::PathGrid g(10, 10, 1.0);
    check(g.inBounds(0, 0) && g.inBounds(9, 9), "grid bounds");
    check(!g.inBounds(10, 0) && !g.inBounds(-1, 0), "out of bounds rejected");
    check(!g.blocked(5, 5), "cells clear by default");
    g.setBlocked(5, 5);
    check(g.blocked(5, 5), "setBlocked marks cell");
    check(g.cellOf(2.4) == 2 && g.cellOf(2.6) == 2, "cellOf floors");
    check(g.worldOf(2) == 2.5, "worldOf centers");
}

void testLineBlocked() {
    eaw::PathGrid g(10, 10, 1.0);
    g.setBlocked(5, 2);
    check(g.lineBlocked(0, 2, 9, 2), "line through blocked cell detected");
    check(!g.lineBlocked(0, 0, 9, 9), "clear diagonal line");
}

void testStraightPath() {
    eaw::JobSystem jobs(4);
    eaw::PathGrid g(20, 20, 1.0);
    eaw::PathfindingSystem pf(g, jobs);
    int id = pf.request({0.5, 0.5, 0}, {15.5, 0.5, 0});
    pf.tick([&](int, std::vector<eaw::Vec3>) {});
    check(pf.isDone(id), "straight path completes");
    check(!pf.isFailed(id), "straight path not failed");
    const auto& wp = pf.waypoints(id);
    check(wp.size() >= 2, "path has waypoints");
    if (!wp.empty()) {
        check(wp.front().x < 1.0 && wp.back().x > 14.0, "path spans start to goal");
    }
}

void testObstacleAvoidance() {
    eaw::JobSystem jobs(4);
    eaw::PathGrid g(30, 30, 1.0);
    // Wall from (10,5) to (10,24): must go around.
    for (int y = 5; y <= 24; ++y) g.setBlocked(10, y);
    eaw::PathfindingSystem pf(g, jobs);
    int id = pf.request({0.5, 10.5, 0}, {20.5, 10.5, 0});
    pf.tick([&](int, std::vector<eaw::Vec3>) {});
    check(pf.isDone(id), "path around wall completes");
    // Every waypoint must avoid the wall cells.
    bool clear = true;
    for (const auto& wp : pf.waypoints(id)) {
        if (g.blocked(g.cellOf(wp.x), g.cellOf(wp.y))) { clear = false; break; }
    }
    check(clear, "all waypoints avoid blocked cells");
    // The path must actually change y (go around, not through): wall spans
    // cells y=5..24, so a detour visits a cell y<5 or y>24.
    bool detoured = false;
    for (const auto& wp : pf.waypoints(id)) {
        int cy = g.cellOf(wp.y);
        if (cy < 5 || cy > 24) { detoured = true; break; }
    }
    check(detoured, "path detours around the wall");
}

void testUnreachableGoal() {
    eaw::JobSystem jobs(4);
    eaw::PathGrid g(10, 10, 1.0);
    // Seal the goal in a box.
    g.setBlocked(5, 5); g.setBlocked(6, 5); g.setBlocked(7, 5);
    g.setBlocked(5, 6);                      g.setBlocked(7, 6);
    g.setBlocked(5, 7); g.setBlocked(6, 7); g.setBlocked(7, 7);
    eaw::PathfindingSystem pf(g, jobs, eaw::PathfindingSystem::Options(1000, 20000));
    int id = pf.request({0.5, 0.5, 0}, {6.5, 6.5, 0});
    pf.tick([&](int, std::vector<eaw::Vec3>) {});
    check(pf.isFailed(id), "unreachable goal fails");
    check(!pf.isDone(id), "unreachable goal not done");
}

void testFrameSlicing() {
    // A long search with a tiny per-tick budget must take multiple ticks.
    eaw::JobSystem jobs(4);
    eaw::PathGrid g(64, 64, 1.0);
    eaw::PathfindingSystem pf(g, jobs, eaw::PathfindingSystem::Options(20, 100000));
    int id = pf.request({0.5, 0.5, 0}, {60.5, 60.5, 0});
    int ticks = 0;
    while (!pf.isDone(id) && !pf.isFailed(id) && ticks < 200) {
        pf.tick([&](int, std::vector<eaw::Vec3>) {});
        ++ticks;
    }
    check(pf.isDone(id), "long path completes across ticks");
    check(ticks > 1, "search was frame-sliced (took multiple ticks)");
}

void testParallelDeterminism() {
    // Several searches with different worker counts must produce identical
    // waypoints (per-search state only).
    auto runAll = [](eaw::JobSystem& jobs, bool recordPaths,
                     std::vector<std::vector<eaw::Vec3>>& outPaths) {
        eaw::PathGrid g(50, 50, 1.0);
        for (int x = 15; x <= 35; ++x) { g.setBlocked(x, 20); g.setBlocked(x, 30); }
        eaw::PathfindingSystem pf(g, jobs);
        std::vector<int> ids;
        ids.push_back(pf.request({2.5, 2.5, 0}, {45.5, 2.5, 0}));
        ids.push_back(pf.request({2.5, 25.5, 0}, {45.5, 25.5, 0}));
        ids.push_back(pf.request({25.5, 2.5, 0}, {25.5, 45.5, 0}));
        for (int t = 0; t < 50; ++t) {
            pf.tick([&](int, std::vector<eaw::Vec3>) {});
        }
        if (recordPaths) {
            outPaths.clear();
            for (int id : ids) outPaths.push_back(pf.waypoints(id));
        }
        return pf.isDone(ids[0]) && pf.isDone(ids[1]) && pf.isDone(ids[2]);
    };
    eaw::JobSystem jobsA(4), jobsB(2);
    std::vector<std::vector<eaw::Vec3>> pathsA, pathsB;
    bool doneA = runAll(jobsA, true, pathsA);
    bool doneB = runAll(jobsB, true, pathsB);
    check(doneA && doneB, "all searches complete");
    bool same = pathsA.size() == pathsB.size();
    if (same) {
        for (size_t i = 0; i < pathsA.size() && same; ++i) {
            if (pathsA[i].size() != pathsB[i].size()) { same = false; break; }
            for (size_t k = 0; k < pathsA[i].size() && same; ++k) {
                if (pathsA[i][k].x != pathsB[i][k].x ||
                    pathsA[i][k].y != pathsB[i][k].y) {
                    same = false;
                }
            }
        }
    }
    check(same, "waypoints identical across worker counts");
}

void testSameCellTrivialPath() {
    eaw::JobSystem jobs(4);
    eaw::PathGrid g(10, 10, 1.0);
    eaw::PathfindingSystem pf(g, jobs);
    int id = pf.request({3.5, 3.5, 0}, {3.5, 3.5, 0});
    pf.tick([&](int, std::vector<eaw::Vec3>) {});
    check(pf.isDone(id), "same-cell request completes immediately");
}

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    testGridBasics();
    testLineBlocked();
    testStraightPath();
    testObstacleAvoidance();
    testUnreachableGoal();
    testFrameSlicing();
    testParallelDeterminism();
    testSameCellTrivialPath();
    if (failures == 0) { std::printf("ALL TESTS PASSED\n"); return 0; }
    std::printf("%d FAILURES\n", failures);
    return 1;
}
