#pragma once

#include <cmath>
#include <vector>

#include "eclipse/mesh.h"

namespace eclipse {

// A swept tube along a polyline, using parallel-transport frames so it never
// twists. Each point carries its own radius and corruption attributes. A
// zero radius at the last point closes the tube to a sharp tip; otherwise
// both ends are capped flat. The target mesh must carry attributes.
void appendTube(Mesh& mesh, const std::vector<Vec3>& points, const std::vector<float>& radii,
                const std::vector<CorruptionAttributes>& attributes, int sides);

// An ellipsoid with semi-axes along a right-handed frame (x, y, z). The
// attribute function receives each vertex's unit direction in that frame.
template <class AttributeFn>
void appendEllipsoid(Mesh& mesh, const Vec3& centre, const Vec3& axisX, const Vec3& axisY, const Vec3& axisZ,
                     const Vec3& radii, int rings, int segments, AttributeFn attributeOf);

// Evenly spaced samples of a quadratic Bezier curve.
std::vector<Vec3> sampleQuadratic(const Vec3& a, const Vec3& control, const Vec3& b, int samples);

// ---------------------------------------------------------------------------

template <class AttributeFn>
void appendEllipsoid(Mesh& mesh, const Vec3& centre, const Vec3& ax, const Vec3& ay, const Vec3& az,
                     const Vec3& r, int rings, int segments, AttributeFn attributeOf) {
    const uint32_t base = static_cast<uint32_t>(mesh.positions.size());
    const float pi = 3.14159265358979f;
    auto emit = [&](const Vec3& unit) {
        Vec3 local{unit.x * r.x, unit.y * r.y, unit.z * r.z};
        // Normal of an ellipsoid: gradient of x^2/a^2 + y^2/b^2 + z^2/c^2.
        Vec3 gradient{unit.x / r.x, unit.y / r.y, unit.z / r.z};
        mesh.positions.push_back(centre + ax * local.x + ay * local.y + az * local.z);
        mesh.normals.push_back(normalize(ax * gradient.x + ay * gradient.y + az * gradient.z));
        mesh.attributes.push_back(attributeOf(unit));
    };
    // One vertex per pole, rings 1..rings-1 between them, from +z down to -z.
    emit(Vec3{0, 0, 1});
    for (int i = 1; i < rings; ++i) {
        float theta = pi * static_cast<float>(i) / static_cast<float>(rings);
        for (int j = 0; j < segments; ++j) {
            float phi = 2.0f * pi * static_cast<float>(j) / static_cast<float>(segments);
            emit(Vec3{std::sin(theta) * std::cos(phi), std::sin(theta) * std::sin(phi), std::cos(theta)});
        }
    }
    emit(Vec3{0, 0, -1});
    const uint32_t top = base, bottom = static_cast<uint32_t>(mesh.positions.size() - 1);
    auto at = [&](int ring, int j) {  // ring in 1..rings-1
        return base + 1 + static_cast<uint32_t>((ring - 1) * segments + (j % segments));
    };
    for (int j = 0; j < segments; ++j) {
        mesh.triangles.push_back({top, at(1, j), at(1, j + 1)});
        mesh.triangles.push_back({bottom, at(rings - 1, j + 1), at(rings - 1, j)});
        for (int i = 1; i + 1 < rings; ++i) {
            mesh.triangles.push_back({at(i, j), at(i + 1, j), at(i, j + 1)});
            mesh.triangles.push_back({at(i, j + 1), at(i + 1, j), at(i + 1, j + 1)});
        }
    }
}

}  // namespace eclipse
