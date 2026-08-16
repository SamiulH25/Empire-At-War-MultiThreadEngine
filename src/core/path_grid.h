// PathGrid — 3D grid of blocked/clear cells for pathfinding.
//
// World coordinates map to cells via a cell size on every axis. The grid
// holds static geometry (walls, map features, terrain altitude bands).
// Dynamic unit occupancy is not modeled in this tier. Used by
// PathfindingSystem; space maps are planar but ships operate at different
// altitudes, so z is a first-class routing axis.
#pragma once

#include <cstdint>
#include <vector>

namespace eaw {

class PathGrid {
public:
    PathGrid(int width = 256, int height = 256, int depth = 1, double cellSize = 1.0);

    int width() const { return w_; }
    int height() const { return h_; }
    int depth() const { return d_; }
    double cellSize() const { return cellSize_; }

    bool inBounds(int x, int y, int z) const;
    bool blocked(int x, int y, int z) const;
    void setBlocked(int x, int y, int z, bool b = true);
    void clearAll();

    // World <-> cell mapping (per axis).
    int cellOf(double world) const;   // floor(world / cellSize)
    double worldOf(int cell) const;   // (cell + 0.5) * cellSize

    // True if the straight segment between two world points crosses any
    // blocked or out-of-bounds cell (used for line-of-sight shortcuts).
    // Uses 3D voxel traversal (Amanatides-Woo DDA), so no cell the line
    // passes through is skipped.
    bool lineBlocked(double x0, double y0, double z0,
                     double x1, double y1, double z1) const;

private:
    int w_, h_, d_;
    double cellSize_;
    std::vector<uint8_t> blocked_; // ((z * h_) + y) * w_ + x
};

} // namespace eaw
