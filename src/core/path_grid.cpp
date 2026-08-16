#include "core/path_grid.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace eaw {

PathGrid::PathGrid(int width, int height, int depth, double cellSize)
    : w_(width), h_(height), d_(depth), cellSize_(cellSize),
      blocked_(static_cast<size_t>(width) * static_cast<size_t>(height) *
                   static_cast<size_t>(depth),
               0) {}

bool PathGrid::inBounds(int x, int y, int z) const {
    return x >= 0 && y >= 0 && z >= 0 && x < w_ && y < h_ && z < d_;
}

bool PathGrid::blocked(int x, int y, int z) const {
    if (!inBounds(x, y, z)) return true; // out of bounds counts as blocked
    return blocked_[((static_cast<size_t>(z) * h_) + y) * w_ + x] != 0;
}

void PathGrid::setBlocked(int x, int y, int z, bool b) {
    if (!inBounds(x, y, z)) return;
    blocked_[((static_cast<size_t>(z) * h_) + y) * w_ + x] = b ? 1 : 0;
}

void PathGrid::clearAll() {
    std::fill(blocked_.begin(), blocked_.end(), 0);
}

int PathGrid::cellOf(double world) const {
    return static_cast<int>(std::floor(world / cellSize_));
}

double PathGrid::worldOf(int cell) const {
    return (static_cast<double>(cell) + 0.5) * cellSize_;
}

// Amanatides-Woo 3D voxel traversal: steps through every cell the segment
// passes through, so diagonal lines cannot skip past blocked cells.
bool PathGrid::lineBlocked(double x0, double y0, double z0,
                           double x1, double y1, double z1) const {
    int cx = cellOf(x0), cy = cellOf(y0), cz = cellOf(z0);
    int ex = cellOf(x1), ey = cellOf(y1), ez = cellOf(z1);
    if (blocked(cx, cy, cz)) return true;

    int stepX = cx < ex ? 1 : -1;
    int stepY = cy < ey ? 1 : -1;
    int stepZ = cz < ez ? 1 : -1;

    // Distance (in cell units) to the next cell boundary along each axis,
    // scaled by the dominant axis delta to avoid floating drift.
    double dx = ex - cx, dy = ey - cy, dz = ez - cz;
    double tMaxX, tMaxY, tMaxZ, tDeltaX, tDeltaY, tDeltaZ;
    const double inf = std::numeric_limits<double>::infinity();

    if (dx == 0) { tMaxX = inf; tDeltaX = inf; }
    else {
        double boundary = (stepX > 0) ? (cx + 1) * cellSize_ : cx * cellSize_;
        double t0 = (boundary - x0) / (x1 - x0);
        tMaxX = t0;
        tDeltaX = cellSize_ / std::abs(x1 - x0);
    }
    if (dy == 0) { tMaxY = inf; tDeltaY = inf; }
    else {
        double boundary = (stepY > 0) ? (cy + 1) * cellSize_ : cy * cellSize_;
        double t0 = (boundary - y0) / (y1 - y0);
        tMaxY = t0;
        tDeltaY = cellSize_ / std::abs(y1 - y0);
    }
    if (dz == 0) { tMaxZ = inf; tDeltaZ = inf; }
    else {
        double boundary = (stepZ > 0) ? (cz + 1) * cellSize_ : cz * cellSize_;
        double t0 = (boundary - z0) / (z1 - z0);
        tMaxZ = t0;
        tDeltaZ = cellSize_ / std::abs(z1 - z0);
    }

    while (cx != ex || cy != ey || cz != ez) {
        if (tMaxX < tMaxY && tMaxX < tMaxZ) {
            cx += stepX;
            tMaxX += tDeltaX;
        } else if (tMaxY < tMaxZ) {
            cy += stepY;
            tMaxY += tDeltaY;
        } else {
            cz += stepZ;
            tMaxZ += tDeltaZ;
        }
        if (blocked(cx, cy, cz)) return true;
    }
    return false;
}

} // namespace eaw
