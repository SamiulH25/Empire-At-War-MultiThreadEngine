#include "core/path_grid.h"

#include <algorithm>
#include <cmath>

namespace eaw {

PathGrid::PathGrid(int width, int height, double cellSize)
    : w_(width), h_(height), cellSize_(cellSize),
      blocked_(static_cast<size_t>(width) * static_cast<size_t>(height), 0) {}

bool PathGrid::inBounds(int x, int y) const {
    return x >= 0 && y >= 0 && x < w_ && y < h_;
}

bool PathGrid::blocked(int x, int y) const {
    if (!inBounds(x, y)) return true; // out of bounds counts as blocked
    return blocked_[static_cast<size_t>(y) * w_ + x] != 0;
}

void PathGrid::setBlocked(int x, int y, bool b) {
    if (!inBounds(x, y)) return;
    blocked_[static_cast<size_t>(y) * w_ + x] = b ? 1 : 0;
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

// Bresenham-style line walk: visit every cell the segment touches; blocked
// if any is out of bounds or blocked.
bool PathGrid::lineBlocked(double x0, double y0, double x1, double y1) const {
    int cx = cellOf(x0), cy = cellOf(y0);
    int ex = cellOf(x1), ey = cellOf(y1);
    int dx = std::abs(ex - cx), dy = std::abs(ey - cy);
    int sx = cx < ex ? 1 : -1;
    int sy = cy < ey ? 1 : -1;
    int err = dx - dy;
    for (;;) {
        if (blocked(cx, cy)) return true;
        if (cx == ex && cy == ey) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; cx += sx; }
        if (e2 < dx) { err += dx; cy += sy; }
    }
    return false;
}

} // namespace eaw
