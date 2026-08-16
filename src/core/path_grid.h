// PathGrid — 2D static grid of blocked/clear cells for pathfinding.
//
// World coordinates map to cells via a cell size; the grid holds static
// geometry (walls, map features). Dynamic unit occupancy is not modeled in
// this tier. Used by PathfindingSystem.
#pragma once

#include "core/object_model.h"

#include <cstdint>
#include <vector>

namespace eaw {

class PathGrid {
public:
    PathGrid(int width = 256, int height = 256, double cellSize = 1.0);

    int width() const { return w_; }
    int height() const { return h_; }
    double cellSize() const { return cellSize_; }

    bool inBounds(int x, int y) const;
    bool blocked(int x, int y) const;
    void setBlocked(int x, int y, bool b = true);
    void clearAll();

    // World <-> cell mapping.
    int cellOf(double world) const;   // floor(world / cellSize)
    double worldOf(int cell) const;   // (cell + 0.5) * cellSize

    // True if the straight segment between two world points crosses any
    // blocked or out-of-bounds cell (used for line-of-sight shortcuts).
    bool lineBlocked(double x0, double y0, double x1, double y1) const;

private:
    int w_, h_;
    double cellSize_;
    std::vector<uint8_t> blocked_; // row-major: [y * w_ + x]
};

} // namespace eaw
