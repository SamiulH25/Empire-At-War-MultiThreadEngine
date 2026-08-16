// PathfindingSystem — frame-sliced A* for the sim.
//
// Design (docs/research/06-threading-design.md): pathfinding is the headline
// parallel slice — per-unit searches are independent work with a data-driven
// budget. Each Search owns its A* state (node pool + lazy-deletion heap), so
// the system steps all active searches in parallel via the job system. A
// search runs a bounded number of expansions per tick (the
// SpacePathfindMaxExpansions analog) and fails after a total cap.
//
// Deterministic: a search's outcome depends only on the grid and its own
// start/goal; parallel stepping order across searches cannot affect it.
#pragma once

#include "core/job_system.h"
#include "core/path_grid.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace eaw {

class PathfindingSystem {
public:
    struct Options {
        int expansionsPerTick = 400;    // per-search budget per tick
        int maxTotalExpansions = 40000; // hard cap before giving up

        Options() = default;
        Options(int perTick, int maxTotal)
            : expansionsPerTick(perTick), maxTotalExpansions(maxTotal) {}
    };

    // `jobs` must outlive this system.
    PathfindingSystem(const PathGrid& grid, JobSystem& jobs, Options opt);
    PathfindingSystem(const PathGrid& grid, JobSystem& jobs);

    // Begins a search from start to goal (world coords). Returns a search id
    // (>0). Stepped by tick() until done/failed.
    int request(const Vec3& start, const Vec3& goal);

    // Advances all active searches by their per-tick expansion budget, in
    // parallel. Calls `onPath(searchId, waypoints)` for each search that
    // completed this tick (waypoints in world coords, start->goal order).
    void tick(const std::function<void(int, std::vector<Vec3>)>& onPath);

    bool isDone(int searchId) const;
    bool isFailed(int searchId) const;
    // Completed waypoints (world coords); empty if not done.
    const std::vector<Vec3>& waypoints(int searchId) const;

    int activeCount() const { return static_cast<int>(searches_.size()); }

private:
    struct Node {
        int x = 0, y = 0;
        int g = 0x7fffffff; // cost from start; sentinel = unvisited
        int f = 0x7fffffff;
        int parent = -1;
        bool closed = false;
    };
    // Heap entry: node index + the f value at push time (lazy deletion).
    struct HeapEntry {
        int node = -1;
        int f = 0;
    };

    struct Search {
        int id = 0;
        int startX = 0, startY = 0;
        int goalX = 0, goalY = 0;
        int expansions = 0;
        bool done = false;
        bool failed = false;
        std::vector<Vec3> path;
        std::vector<Node> nodes;     // pooled by y*width+x
        std::vector<HeapEntry> open; // binary heap
    };

    static void heapPush(Search& s, int nodeIdx, int f);
    static int heapPop(Search& s);
    static void heapSiftDown(Search& s, int pos);

    // Runs up to `budget` expansions; returns true when finished (done or
    // failed).
    bool stepSearch(Search& s, int budget);

    const PathGrid& grid_;
    JobSystem& jobs_;
    Options opt_;
    std::vector<std::unique_ptr<Search>> searches_;
    int nextId_ = 1;
};

} // namespace eaw
