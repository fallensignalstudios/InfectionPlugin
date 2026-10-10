#include "eclipse/spatial.h"

#include <algorithm>
#include <cmath>

namespace eclipse {

PointGrid::PointGrid(const std::vector<Vec3>& points, float cellSize) : points_(points), cell_(cellSize) {
    for (uint32_t i = 0; i < points.size(); ++i)
        cells_[pack(cellOf(points[i].x), cellOf(points[i].y), cellOf(points[i].z))].push_back(i);
}

uint64_t PointGrid::pack(int64_t x, int64_t y, int64_t z) {
    return (static_cast<uint64_t>(x & 0x1FFFFF) << 42) | (static_cast<uint64_t>(y & 0x1FFFFF) << 21) |
           static_cast<uint64_t>(z & 0x1FFFFF);
}

int64_t PointGrid::cellOf(float v) const { return static_cast<int64_t>(std::floor(v / cell_)); }

std::vector<uint32_t> PointGrid::within(const Vec3& p, float radius) const {
    std::vector<uint32_t> out;
    const float r2 = radius * radius;
    for (int64_t x = cellOf(p.x - radius); x <= cellOf(p.x + radius); ++x)
        for (int64_t y = cellOf(p.y - radius); y <= cellOf(p.y + radius); ++y)
            for (int64_t z = cellOf(p.z - radius); z <= cellOf(p.z + radius); ++z) {
                auto it = cells_.find(pack(x, y, z));
                if (it == cells_.end()) continue;
                for (uint32_t i : it->second) {
                    Vec3 d = points_[i] - p;
                    if (dot(d, d) <= r2) out.push_back(i);
                }
            }
    std::sort(out.begin(), out.end());
    return out;
}

int32_t PointGrid::nearest(const Vec3& p, float maxRadius) const {
    int32_t best = -1;
    float bestD = maxRadius * maxRadius;
    for (int64_t x = cellOf(p.x - maxRadius); x <= cellOf(p.x + maxRadius); ++x)
        for (int64_t y = cellOf(p.y - maxRadius); y <= cellOf(p.y + maxRadius); ++y)
            for (int64_t z = cellOf(p.z - maxRadius); z <= cellOf(p.z + maxRadius); ++z) {
                auto it = cells_.find(pack(x, y, z));
                if (it == cells_.end()) continue;
                for (uint32_t i : it->second) {
                    Vec3 d = points_[i] - p;
                    float dd = dot(d, d);
                    if (dd < bestD || (dd == bestD && static_cast<int32_t>(i) < best)) {
                        bestD = dd;
                        best = static_cast<int32_t>(i);
                    }
                }
            }
    return best;
}

std::vector<uint32_t> poissonThin(const std::vector<Vec3>& points, const std::vector<uint32_t>& candidates,
                                  float spacing) {
    std::vector<uint32_t> kept;
    std::vector<Vec3> keptPoints;
    std::unordered_map<uint64_t, std::vector<uint32_t>> grid;
    auto c = [&](float v) { return static_cast<int64_t>(std::floor(v / spacing)); };
    auto pack = [](int64_t x, int64_t y, int64_t z) {
        return (static_cast<uint64_t>(x & 0x1FFFFF) << 42) | (static_cast<uint64_t>(y & 0x1FFFFF) << 21) |
               static_cast<uint64_t>(z & 0x1FFFFF);
    };
    for (uint32_t v : candidates) {
        const Vec3& p = points[v];
        int64_t cx = c(p.x), cy = c(p.y), cz = c(p.z);
        bool clear = true;
        for (int64_t dx = -1; dx <= 1 && clear; ++dx)
            for (int64_t dy = -1; dy <= 1 && clear; ++dy)
                for (int64_t dz = -1; dz <= 1 && clear; ++dz) {
                    auto it = grid.find(pack(cx + dx, cy + dy, cz + dz));
                    if (it == grid.end()) continue;
                    for (uint32_t k : it->second)
                        if (length(keptPoints[k] - p) < spacing) {
                            clear = false;
                            break;
                        }
                }
        if (!clear) continue;
        grid[pack(cx, cy, cz)].push_back(static_cast<uint32_t>(keptPoints.size()));
        keptPoints.push_back(p);
        kept.push_back(v);
    }
    return kept;
}

}  // namespace eclipse
