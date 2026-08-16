#include "core/pathfinding.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace eaw {

namespace {

constexpr int kCardinalCost = 100;

inline int idxOf(const PathGrid& g, int x, int y) {
    return y * g.width() + x;
}

inline int manhattan(int ax, int ay, int bx, int by) {
    return (std::abs(ax - bx) + std::abs(ay - by)) * kCardinalCost;
}

} // namespace

// --- heap (per-search) ---------------------------------------------------

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
            // Remove the top.
            s.open[0] = s.open.back();
            s.open.pop_back();
            heapSiftDown(s, 0);
            return top.node;
        }
        // Stale entry — discard and re-heapify.
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
    s->goalX = grid_.cellOf(goal.x);
    s->goalY = grid_.cellOf(goal.y);
    s->nodes.assign(static_cast<size_t>(grid_.width()) * grid_.height(), Node{});

    if (s->startX == s->goalX && s->startY == s->goalY) {
        s->path = {Vec3{start.x, start.y, start.z}, Vec3{goal.x, goal.y, goal.z}};
        s->done = true;
    } else if (grid_.blocked(s->startX, s->startY) || grid_.blocked(s->goalX, s->goalY)) {
        s->failed = true;
    } else {
        int si = idxOf(grid_, s->startX, s->startY);
        Node& startNode = s->nodes[si];
        startNode.x = s->startX;
        startNode.y = s->startY;
        startNode.g = 0;
        startNode.f = manhattan(s->startX, s->startY, s->goalX, s->goalY);
        startNode.parent = -1;
        heapPush(*s, si, startNode.f);
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
        if (c.x == s.goalX && c.y == s.goalY) {
            // Reconstruct via parent chain.
            std::vector<Vec3> raw;
            for (int n = cur; n >= 0; n = s.nodes[n].parent) {
                raw.push_back(Vec3{grid_.worldOf(s.nodes[n].x),
                                   grid_.worldOf(s.nodes[n].y), 0.0});
            }
            std::reverse(raw.begin(), raw.end());
            // Line-of-sight shortcut: drop intermediate waypoints reachable
            // in a straight line from the last kept one.
            std::vector<Vec3> out;
            out.push_back(raw.front());
            for (size_t i = 1; i < raw.size(); ++i) {
                if (grid_.lineBlocked(out.back().x, out.back().y,
                                      raw[i].x, raw[i].y)) {
                    out.push_back(raw[i - 1]);
                }
            }
            if (out.back().x != raw.back().x || out.back().y != raw.back().y) {
                out.push_back(raw.back());
            }
            s.path = std::move(out);
            s.done = true;
            return true;
        }
        // Expand 4 neighbors.
        static const int dx[4] = {1, -1, 0, 0};
        static const int dy[4] = {0, 0, 1, -1};
        for (int d = 0; d < 4; ++d) {
            int nx = c.x + dx[d], ny = c.y + dy[d];
            if (grid_.blocked(nx, ny)) continue;
            int ni = idxOf(grid_, nx, ny);
            Node& n = s.nodes[ni];
            if (n.closed) continue;
            int tentative = c.g + kCardinalCost;
            if (tentative < n.g) {
                if (n.g == 0x7fffffff) {
                    n.x = nx;
                    n.y = ny;
                }
                n.g = tentative;
                n.f = tentative + manhattan(nx, ny, s.goalX, s.goalY);
                n.parent = cur;
                heapPush(s, ni, n.f);
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
