#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "eclipse/math.h"

namespace eclipse {

// Uniform grid over a fixed point set, for radius and nearest queries.
class PointGrid {
public:
    PointGrid(const std::vector<Vec3>& points, float cellSize);

    // Indices of points within radius of p, in ascending index order.
    std::vector<uint32_t> within(const Vec3& p, float radius) const;
    // Nearest point within maxRadius, or -1.
    int32_t nearest(const Vec3& p, float maxRadius) const;

private:
    static uint64_t pack(int64_t x, int64_t y, int64_t z);
    int64_t cellOf(float v) const;

    const std::vector<Vec3>& points_;
    float cell_;
    std::unordered_map<uint64_t, std::vector<uint32_t>> cells_;
};

// Greedy Poisson-disk thinning: walks the candidates in the given order and
// keeps each one at least `spacing` from every point already kept.
std::vector<uint32_t> poissonThin(const std::vector<Vec3>& points, const std::vector<uint32_t>& candidates,
                                  float spacing);

}  // namespace eclipse
