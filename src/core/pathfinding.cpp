#include "core/pathfinding.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace eaw {

namespace {

constexpr int kCardinalCost = 100;
constexpr int kDiagonalCost = 141; // ~sqrt(2)*100, unused with 6-connected

inline int manhattan3d(int ax, int ay, int az, int bx, int by, int bz) {
    return (std::abs(ax - bx) + std::abs(ay - by) + std::abs(az - bz)) *
           kCardinalCost;
}

} // namespace

// --- per-search heap -----------------------------------------------------

uint64_t PathfindingSystem::pack(int x, int y, int z) {
    // Cells are bounded by grid dims (small ints); 21 bits each is ample.
    return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 42) |
           (static_cast<uint64_t>(static_cast<uint32_t>(y)) << 21) |
           static_cast<uint64_t>(static_cast<uint32_t>(z));
}

void PathfindingSystem::heapPush(Search& s, int nodeIdx, int f) {
    s.open.push_back(HeapEntry{nodeIdx, f});
    int pos = static_cast<int>(s.open.size()) - 1;
    while (pos > 0) {
        int parent = (pos - 1) / 2;
        if (s.open[parent].f <= s.open[pos].f) break;
        std::swap(s.open[parent], s.open[pos]);
        pos = parent;
    }
}

void PathfindingSystem::heapSiftDown(Search& s, int pos) {
    int n = static_cast<int>(s.open.size());
    for (;;) {
        int l = 2 * pos + 1, r = 2 * pos + 2;
        int smallest = pos;
        if (l < n && s.open[l].f < s.open[smallest].f) smallest = l;
        if (r < n && s.open[r].f < s.open[smallest].f) smallest = r;
        if (smallest == pos) break;
        std::swap(s.open[pos], s.open[smallest]);
        pos = smallest;
    }
}

int PathfindingSystem::heapPop(Search& s) {
    // Lazy deletion: skip entries whose stored f no longer matches the node's
    // current f (a better path was found after the entry was pushed).
    while (!s.open.empty()) {
        HeapEntry top = s.open[0];
        Node& n = s.nodes[top.node];
        if (top.f == n.f && !n.closed) {
            s.open[0] = s.open.back();
            s.open.pop_back();
            heapSiftDown(s, 0);
            return top.node;
        }
        s.open[0] = s.open.back();
        s.open.pop_back();
        heapSiftDown(s, 0);
    }
    return -1;
}

PathfindingSystem::PathfindingSystem(const PathGrid& grid, JobSystem& jobs, Options opt)
    : grid_(grid), jobs_(jobs), opt_(opt) {}

PathfindingSystem::PathfindingSystem(const PathGrid& grid, JobSystem& jobs)
    : grid_(grid), jobs_(jobs), opt_() {}

int PathfindingSystem::request(const Vec3& start, const Vec3& goal) {
    auto s = std::make_unique<Search>();
    s->id = nextId_++;
    s->startX = grid_.cellOf(start.x);
    s->startY = grid_.cellOf(start.y);
    s->startZ = grid_.cellOf(start.z);
    s->goalX = grid_.cellOf(goal.x);
    s->goalY = grid_.cellOf(goal.y);
    s->goalZ = grid_.cellOf(goal.z);
    s->nodeIndex.reserve(1024);
    s->nodes.reserve(1024);

    if (s->startX == s->goalX && s->startY == s->goalY && s->startZ == s->goalZ) {
        s->path = {Vec3{start.x, start.y, start.z}, Vec3{goal.x, goal.y, goal.z}};
        s->done = true;
    } else if (grid_.blocked(s->startX, s->startY, s->startZ) ||
               grid_.blocked(s->goalX, s->goalY, s->goalZ)) {
        s->failed = true;
    } else {
        Node startNode;
        startNode.x = s->startX;
        startNode.y = s->startY;
        startNode.z = s->startZ;
        startNode.g = 0;
        startNode.f = manhattan3d(s->startX, s->startY, s->startZ,
                                  s->goalX, s->goalY, s->goalZ);
        startNode.parent = -1;
        s->nodeIndex[pack(s->startX, s->startY, s->startZ)] = 0;
        s->nodes.push_back(startNode);
        heapPush(*s, 0, startNode.f);
    }
    int id = s->id;
    searches_.push_back(std::move(s));
    return id;
}

bool PathfindingSystem::stepSearch(Search& s, int budget) {
    int steps = 0;
    while (steps < budget) {
        int cur = heapPop(s);
        if (cur < 0) {
            s.failed = true; // open exhausted — unreachable
            return true;
        }
        Node& c = s.nodes[cur];
        c.closed = true;
        ++steps;
        ++s.expansions;
        // Snapshot before expanding: pushing new nodes may reallocate
        // s.nodes and invalidate `c`.
        const int cx = c.x, cy = c.y, cz = c.z, cg = c.g;
        if (cx == s.goalX && cy == s.goalY && cz == s.goalZ) {
            // Reconstruct via parent chain (start -> goal).
            std::vector<Vec3> raw;
            for (int n = cur; n >= 0; n = s.nodes[n].parent) {
                raw.push_back(Vec3{grid_.worldOf(s.nodes[n].x),
                                   grid_.worldOf(s.nodes[n].y),
                                   grid_.worldOf(s.nodes[n].z)});
            }
            std::reverse(raw.begin(), raw.end());
            // Line-of-sight shortcut (3D): drop intermediate waypoints the
            // unit can reach in a straight line.
            std::vector<Vec3> out;
            out.push_back(raw.front());
            for (size_t i = 1; i < raw.size(); ++i) {
                if (grid_.lineBlocked(out.back().x, out.back().y, out.back().z,
                                      raw[i].x, raw[i].y, raw[i].z)) {
                    out.push_back(raw[i - 1]);
                }
            }
            if (out.back().x != raw.back().x || out.back().y != raw.back().y ||
                out.back().z != raw.back().z) {
                out.push_back(raw.back());
            }
            s.path = std::move(out);
            s.done = true;
            return true;
        }
        // Expand 6 neighbors (cardinal on each axis).
        static const int dx[6] = {1, -1, 0, 0, 0, 0};
        static const int dy[6] = {0, 0, 1, -1, 0, 0};
        static const int dz[6] = {0, 0, 0, 0, 1, -1};
        for (int d = 0; d < 6; ++d) {
            int nx = cx + dx[d], ny = cy + dy[d], nz = cz + dz[d];
            if (grid_.blocked(nx, ny, nz)) continue;
            // z-moves cost extra so routing prefers staying on a plane.
            int moveCost = (dz[d] != 0) ? opt_.zStepCost : kCardinalCost;
            uint64_t key = pack(nx, ny, nz);
            auto it = s.nodeIndex.find(key);
            int ni;
            if (it == s.nodeIndex.end()) {
                Node n;
                n.x = nx; n.y = ny; n.z = nz;
                n.g = cg + moveCost;
                n.f = n.g + manhattan3d(nx, ny, nz, s.goalX, s.goalY, s.goalZ);
                n.parent = cur;
                ni = static_cast<int>(s.nodes.size());
                s.nodeIndex[key] = ni;
                s.nodes.push_back(n);
                heapPush(s, ni, n.f);
            } else {
                ni = it->second;
                Node& n = s.nodes[ni];
                if (n.closed) continue;
                int tentative = cg + moveCost;
                if (tentative < n.g) {
                    n.g = tentative;
                    n.f = tentative + manhattan3d(nx, ny, nz,
                                                  s.goalX, s.goalY, s.goalZ);
                    n.parent = cur;
                    heapPush(s, ni, n.f);
                }
            }
        }
    }
    if (s.expansions >= opt_.maxTotalExpansions) {
        s.failed = true;
        return true;
    }
    return false;
}

void PathfindingSystem::tick(const std::function<void(int, std::vector<Vec3>)>& onPath) {
    // Collect active searches (stable unique_ptr addresses).
    std::vector<Search*> active;
    for (auto& s : searches_) {
        if (!s->done && !s->failed) active.push_back(s.get());
    }
    int n = static_cast<int>(active.size());
    if (n == 0) return;

    // Step every active search in parallel; each lambda writes only its own
    // search's state. Budgets are per-search, so results are identical to a
    // serial walk (deterministic).
    std::vector<bool> finished(static_cast<size_t>(n), false);
    jobs_.parallel_for(n, [&](int64_t a, int64_t b) {
        for (int64_t i = a; i < b; ++i) {
            finished[i] = stepSearch(*active[i], opt_.expansionsPerTick);
        }
    });

    // Report completions in id order (deterministic callback order).
    for (int i = 0; i < n; ++i) {
        if (finished[i] && active[i]->done && onPath) {
            onPath(active[i]->id, active[i]->path);
        }
    }
}

bool PathfindingSystem::isDone(int searchId) const {
    for (const auto& s : searches_) {
        if (s->id == searchId) return s->done;
    }
    return false;
}

bool PathfindingSystem::isFailed(int searchId) const {
    for (const auto& s : searches_) {
        if (s->id == searchId) return s->failed;
    }
    return false;
}

const std::vector<Vec3>& PathfindingSystem::waypoints(int searchId) const {
    static const std::vector<Vec3> empty;
    for (const auto& s : searches_) {
        if (s->id == searchId) return s->path;
    }
    return empty;
}

} // namespace eaw
