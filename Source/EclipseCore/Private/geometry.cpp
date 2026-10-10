#include "eclipse/geometry.h"

#include <algorithm>
#include <cmath>

namespace eclipse {

std::vector<Vec3> sampleQuadratic(const Vec3& a, const Vec3& control, const Vec3& b, int samples) {
    std::vector<Vec3> out;
    for (int i = 0; i < samples; ++i) {
        float t = samples > 1 ? static_cast<float>(i) / static_cast<float>(samples - 1) : 0.0f;
        float u = 1.0f - t;
        out.push_back(a * (u * u) + control * (2.0f * u * t) + b * (t * t));
    }
    return out;
}

void appendTube(Mesh& mesh, const std::vector<Vec3>& points, const std::vector<float>& radii,
                const std::vector<CorruptionAttributes>& attributes, int sides) {
    const size_t n = points.size();
    if (n < 2 || sides < 3) return;
    const float pi = 3.14159265358979f;

    std::vector<Vec3> tangents(n);
    for (size_t i = 0; i < n; ++i) {
        Vec3 d = points[std::min(i + 1, n - 1)] - points[i > 0 ? i - 1 : 0];
        tangents[i] = normalize(d);
    }
    // Parallel transport: rotate the previous frame by the smallest turn.
    std::vector<Vec3> normalsAlong(n);
    normalsAlong[0] = anyPerpendicular(tangents[0]);
    for (size_t i = 1; i < n; ++i) {
        Vec3 nPrev = normalsAlong[i - 1];
        Vec3 projected = nPrev - tangents[i] * dot(nPrev, tangents[i]);
        normalsAlong[i] = length(projected) > 1e-6f ? normalize(projected) : anyPerpendicular(tangents[i]);
    }

    const bool sharpTip = radii[n - 1] <= 0.0f;
    const size_t ringCount = sharpTip ? n - 1 : n;
    const uint32_t base = static_cast<uint32_t>(mesh.positions.size());
    for (size_t i = 0; i < ringCount; ++i) {
        Vec3 u = normalsAlong[i];
        Vec3 v = cross(tangents[i], u);
        for (int s = 0; s < sides; ++s) {
            float a = 2.0f * pi * static_cast<float>(s) / static_cast<float>(sides);
            Vec3 radial = u * std::cos(a) + v * std::sin(a);
            mesh.positions.push_back(points[i] + radial * radii[i]);
            mesh.normals.push_back(radial);
            mesh.attributes.push_back(attributes[i]);
        }
    }
    auto ring = [&](size_t i, int s) { return base + static_cast<uint32_t>(i * sides + (s % sides)); };
    for (size_t i = 0; i + 1 < ringCount; ++i)
        for (int s = 0; s < sides; ++s) {
            mesh.triangles.push_back({ring(i, s), ring(i, s + 1), ring(i + 1, s)});
            mesh.triangles.push_back({ring(i, s + 1), ring(i + 1, s + 1), ring(i + 1, s)});
        }

    auto cap = [&](size_t i, const Vec3& at, const Vec3& normal, const CorruptionAttributes& attr, bool forward) {
        uint32_t centre = static_cast<uint32_t>(mesh.positions.size());
        mesh.positions.push_back(at);
        mesh.normals.push_back(normal);
        mesh.attributes.push_back(attr);
        for (int s = 0; s < sides; ++s) {
            if (forward)
                mesh.triangles.push_back({ring(i, s), ring(i, s + 1), centre});
            else
                mesh.triangles.push_back({ring(i, s + 1), ring(i, s), centre});
        }
    };
    cap(0, points[0], -tangents[0], attributes[0], false);
    if (sharpTip)
        cap(ringCount - 1, points[n - 1], tangents[n - 1], attributes[n - 1], true);
    else
        cap(n - 1, points[n - 1], tangents[n - 1], attributes[n - 1], true);
}

}  // namespace eclipse
