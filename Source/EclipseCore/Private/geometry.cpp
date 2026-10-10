#include "eclipse/geometry.h"

#include <algorithm>
#include <cmath>

#include "eclipse/random.h"

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

void appendTube(Mesh& mesh, const std::vector<Vec3>& inputPoints, const std::vector<float>& inputRadii,
                const std::vector<CorruptionAttributes>& inputAttributes, int sides, bool capped, const TubeStyle& style) {
    if (inputPoints.size() < 2 || sides < 3 || inputRadii.size() != inputPoints.size() ||
        inputAttributes.size() != inputPoints.size()) return;
    std::vector<Vec3> resampledPoints;
    std::vector<float> resampledRadii;
    std::vector<CorruptionAttributes> resampledAttributes;
    // Rings and knots need several samples each along the tube, or they
    // alias away: resample finer where the caller's points are too sparse.
    float spacing = 0.0f;
    if (style.rings > 0.0f) spacing = style.ringSpacing / 3.0f;
    if (style.lumps > 0.0f) spacing = spacing > 0.0f ? std::min(spacing, style.lumpSize / 2.0f) : style.lumpSize / 2.0f;
    if (spacing > 0.0f && std::isfinite(spacing)) {
        auto segmentLength = [](const Vec3& a, const Vec3& b) {
            const double x = static_cast<double>(a.x) - b.x;
            const double y = static_cast<double>(a.y) - b.y;
            const double z = static_cast<double>(a.z) - b.z;
            return std::sqrt(x * x + y * y + z * z);
        };
        double total = 0.0, widest = 0.0;
        for (size_t i = 1; i < inputPoints.size(); ++i) {
            const double l = segmentLength(inputPoints[i], inputPoints[i - 1]);
            if (!std::isfinite(l)) return;
            total += l;
            widest = std::max(widest, l);
        }
        if (widest > spacing * 1.5f) {
            // Resample at most once: the 400-sample budget can leave edges
            // coarser than the desired style spacing. Recursing with that
            // capped path would keep requesting the same 400 points forever.
            // Clamp the floating ratio before integer conversion as well.
            const double desired = total / spacing;
            const size_t count = desired >= 398.0 ? 400 : static_cast<size_t>(desired) + 2;
            resampledPoints.reserve(count);
            resampledRadii.reserve(count);
            resampledAttributes.reserve(count);
            size_t seg = 0;
            double segStart = 0.0;
            for (size_t k = 0; k < count; ++k) {
                const double at = total * static_cast<double>(k) / static_cast<double>(count - 1);
                while (seg + 2 < inputPoints.size() && segStart + segmentLength(inputPoints[seg + 1], inputPoints[seg]) < at) {
                    segStart += segmentLength(inputPoints[seg + 1], inputPoints[seg]);
                    ++seg;
                }
                const double l = segmentLength(inputPoints[seg + 1], inputPoints[seg]);
                const float t = l > 0.0 ? clamp01(static_cast<float>((at - segStart) / l)) : 0.0f;
                resampledPoints.push_back(lerp(inputPoints[seg], inputPoints[seg + 1], t));
                // A sharp tip stays sharp: only the last sample keeps radius 0.
                resampledRadii.push_back(k + 1 == count ? inputRadii.back() : lerpf(inputRadii[seg], inputRadii[seg + 1], t));
                resampledAttributes.push_back(t < 0.5f ? inputAttributes[seg] : inputAttributes[seg + 1]);
            }
        }
    }
    const auto& points = resampledPoints.empty() ? inputPoints : resampledPoints;
    const auto& radii = resampledRadii.empty() ? inputRadii : resampledRadii;
    const auto& attributes = resampledAttributes.empty() ? inputAttributes : resampledAttributes;
    const size_t n = points.size();
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
    const size_t firstTriangle = mesh.triangles.size();
    const bool styled = style.lumps > 0.0f || style.rings > 0.0f;
    Noise knots(style.seed * 2654435761u + 977u);
    float along = 0.0f;
    for (size_t i = 0; i < ringCount; ++i) {
        if (i > 0) along += length(points[i] - points[i - 1]);
        Vec3 u = normalsAlong[i];
        Vec3 v = cross(tangents[i], u);
        // Growth rings: sharp ridges with soft valleys between, unevenly
        // spaced and of uneven depth, as they are on horn.
        float ring = 1.0f;
        if (style.rings > 0.0f && style.ringSpacing > 0.0f) {
            float x = along / style.ringSpacing;
            float phase = x + 0.6f * knots.value(Vec3{x * 0.25f, 0.5f, 0.0f});
            float r = 1.0f - std::fabs(2.0f * (phase - std::floor(phase)) - 1.0f);
            float depth = 0.5f + 0.5f * knots.value(Vec3{std::floor(phase) * 0.7f, 3.5f, 0.0f}) + 0.5f;
            ring += style.rings * depth * (r * r * r - 0.25f);
        }
        for (int s = 0; s < sides; ++s) {
            float a = 2.0f * pi * static_cast<float>(s) / static_cast<float>(sides);
            Vec3 radial = u * std::cos(a) + v * std::sin(a);
            float r = radii[i] * ring;
            if (style.lumps > 0.0f && style.lumpSize > 0.0f) {
                Vec3 q = points[i] + radial * radii[i];
                r *= 1.0f + style.lumps * knots.fbm(q / style.lumpSize, 3);
            }
            mesh.positions.push_back(points[i] + radial * r);
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
    if (capped) cap(0, points[0], -tangents[0], attributes[0], false);
    if (sharpTip)
        cap(ringCount - 1, points[n - 1], tangents[n - 1], attributes[n - 1], true);
    else if (capped)
        cap(n - 1, points[n - 1], tangents[n - 1], attributes[n - 1], true);
    if (styled) smoothNormalsFrom(mesh, base, firstTriangle);
}

void smoothNormalsFrom(Mesh& mesh, size_t firstVertex, size_t firstTriangle) {
    for (size_t v = firstVertex; v < mesh.positions.size(); ++v) mesh.normals[v] = Vec3{0, 0, 0};
    for (size_t t = firstTriangle; t < mesh.triangles.size(); ++t) {
        const auto& tri = mesh.triangles[t];
        Vec3 face = cross(mesh.positions[tri[1]] - mesh.positions[tri[0]], mesh.positions[tri[2]] - mesh.positions[tri[0]]);
        for (uint32_t i : tri)
            if (i >= firstVertex) mesh.normals[i] += face;  // area-weighted
    }
    for (size_t v = firstVertex; v < mesh.positions.size(); ++v) {
        float l = length(mesh.normals[v]);
        mesh.normals[v] = l > 1e-12f ? mesh.normals[v] / l : Vec3{0, 1, 0};
    }
}

void roughen(Mesh& mesh, size_t firstVertex, size_t firstTriangle, float amplitude, float featureSize, uint32_t seed) {
    if (amplitude <= 0.0f || featureSize <= 0.0f) return;
    Noise noise(seed * 40503u + 7u);
    for (size_t v = firstVertex; v < mesh.positions.size(); ++v)
        mesh.positions[v] += mesh.normals[v] * (amplitude * noise.fbm(mesh.positions[v] / featureSize, 3));
    smoothNormalsFrom(mesh, firstVertex, firstTriangle);
}

}  // namespace eclipse
