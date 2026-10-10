#include "eclipse/sdf.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace eclipse {

static float signf(float v) { return v < 0.0f ? -1.0f : (v > 0.0f ? 1.0f : 0.0f); }

float sdRoundCone(const Vec3& p, const Vec3& a, const Vec3& b, float r1, float r2) {
    // Inigo Quilez's exact round-cone distance.
    Vec3 ba = b - a;
    float l2 = dot(ba, ba);
    if (l2 < 1e-12f) return length(p - a) - std::max(r1, r2);
    float rr = r1 - r2;
    float a2 = l2 - rr * rr;
    float il2 = 1.0f / l2;
    Vec3 pa = p - a;
    float y = dot(pa, ba);
    float z = y - l2;
    Vec3 xv = pa * l2 - ba * y;
    float x2 = dot(xv, xv);
    float y2 = y * y * l2;
    float z2 = z * z * l2;
    float k = signf(rr) * rr * rr * x2;
    if (signf(z) * a2 * z2 > k) return std::sqrt(x2 + z2) * il2 - r2;
    if (signf(y) * a2 * y2 < k) return std::sqrt(x2 + y2) * il2 - r1;
    return (std::sqrt(x2 * a2 * il2) + y * rr) * il2 - r1;
}

float sdEllipsoid(const Vec3& p, const Vec3& c, const Vec3& r) {
    Vec3 q = p - c;
    Vec3 q1{q.x / r.x, q.y / r.y, q.z / r.z};
    Vec3 q2{q.x / (r.x * r.x), q.y / (r.y * r.y), q.z / (r.z * r.z)};
    float k0 = length(q1);
    float k1 = length(q2);
    if (k1 < 1e-12f) return -std::min(r.x, std::min(r.y, r.z));
    return k0 * (k0 - 1.0f) / k1;
}

float sdBox(const Vec3& p, const Vec3& c, const Vec3& h, float rounding) {
    Vec3 q = p - c;
    Vec3 d{std::fabs(q.x) - h.x + rounding, std::fabs(q.y) - h.y + rounding, std::fabs(q.z) - h.z + rounding};
    Vec3 outside = vmax(d, Vec3{0, 0, 0});
    float inside = std::min(std::max(d.x, std::max(d.y, d.z)), 0.0f);
    return length(outside) + inside - rounding;
}

float smoothMin(float a, float b, float k) {
    if (k <= 0.0f) return std::min(a, b);
    float h = std::max(k - std::fabs(a - b), 0.0f) / k;
    return std::min(a, b) - h * h * k * 0.25f;
}

void SdfModel::roundCone(const Vec3& a, const Vec3& b, float ra, float rb, float blend) {
    prims_.push_back({Kind::RoundCone, a, b, ra, rb, blend});
}

void SdfModel::ellipsoid(const Vec3& centre, const Vec3& radii, float blend) {
    prims_.push_back({Kind::Ellipsoid, centre, radii, 0, 0, blend});
}

void SdfModel::box(const Vec3& centre, const Vec3& halfExtents, float rounding, float blend) {
    prims_.push_back({Kind::Box, centre, halfExtents, rounding, 0, blend});
}

float SdfModel::evaluate(const Vec3& p) const {
    float d = 1e9f;
    for (const auto& pr : prims_) {
        float e = 0;
        switch (pr.kind) {
            case Kind::RoundCone: e = sdRoundCone(p, pr.a, pr.b, pr.ra, pr.rb); break;
            case Kind::Ellipsoid: e = sdEllipsoid(p, pr.a, pr.b); break;
            case Kind::Box: e = sdBox(p, pr.a, pr.b, pr.ra); break;
        }
        d = smoothMin(d, e, pr.blend);
    }
    return d;
}

void SdfModel::bounds(Vec3& lo, Vec3& hi) const {
    lo = Vec3{1e30f, 1e30f, 1e30f};
    hi = Vec3{-1e30f, -1e30f, -1e30f};
    for (const auto& pr : prims_) {
        Vec3 l, h;
        switch (pr.kind) {
            case Kind::RoundCone: {
                float r = std::max(pr.ra, pr.rb);
                l = vmin(pr.a, pr.b) - Vec3{r, r, r};
                h = vmax(pr.a, pr.b) + Vec3{r, r, r};
                break;
            }
            case Kind::Ellipsoid:
            case Kind::Box:
                l = pr.a - pr.b;
                h = pr.a + pr.b;
                break;
        }
        lo = vmin(lo, l);
        hi = vmax(hi, h);
    }
}

Mesh polygonize(const DistanceFn& field, const Vec3& lo, const Vec3& hi, float cell) {
    const int nx = std::max(2, static_cast<int>(std::ceil((hi.x - lo.x) / cell)) + 1);
    const int ny = std::max(2, static_cast<int>(std::ceil((hi.y - lo.y) / cell)) + 1);
    const int nz = std::max(2, static_cast<int>(std::ceil((hi.z - lo.z) / cell)) + 1);
    auto sampleIndex = [&](int x, int y, int z) { return (static_cast<size_t>(z) * ny + y) * nx + x; };
    auto samplePos = [&](int x, int y, int z) { return lo + Vec3{x * cell, y * cell, z * cell}; };

    std::vector<float> values(static_cast<size_t>(nx) * ny * nz);
    for (int z = 0; z < nz; ++z)
        for (int y = 0; y < ny; ++y)
            for (int x = 0; x < nx; ++x) values[sampleIndex(x, y, z)] = field(samplePos(x, y, z));

    // One vertex per cell the surface passes through, at the mean of the
    // edge crossings.
    const int cx = nx - 1, cy = ny - 1, cz = nz - 1;
    auto cellIndex = [&](int x, int y, int z) { return (static_cast<size_t>(z) * cy + y) * cx + x; };
    std::vector<int32_t> cellVertex(static_cast<size_t>(cx) * cy * cz, -1);
    static const int corner[8][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0},
                                     {0, 0, 1}, {1, 0, 1}, {0, 1, 1}, {1, 1, 1}};
    static const int edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3},
                                     {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    Mesh mesh;
    for (int z = 0; z < cz; ++z)
        for (int y = 0; y < cy; ++y)
            for (int x = 0; x < cx; ++x) {
                float v[8];
                int inside = 0;
                for (int c = 0; c < 8; ++c) {
                    v[c] = values[sampleIndex(x + corner[c][0], y + corner[c][1], z + corner[c][2])];
                    inside += v[c] < 0.0f;
                }
                if (inside == 0 || inside == 8) continue;
                Vec3 sum{0, 0, 0};
                int count = 0;
                for (const auto& e : edges) {
                    float a = v[e[0]], b = v[e[1]];
                    if ((a < 0.0f) == (b < 0.0f)) continue;
                    float t = a / (a - b);
                    Vec3 pa{static_cast<float>(corner[e[0]][0]), static_cast<float>(corner[e[0]][1]),
                            static_cast<float>(corner[e[0]][2])};
                    Vec3 pb{static_cast<float>(corner[e[1]][0]), static_cast<float>(corner[e[1]][1]),
                            static_cast<float>(corner[e[1]][2])};
                    sum += lerp(pa, pb, t);
                    ++count;
                }
                cellVertex[cellIndex(x, y, z)] = static_cast<int32_t>(mesh.positions.size());
                mesh.positions.push_back(samplePos(x, y, z) + (sum / static_cast<float>(count)) * cell);
            }

    // One quad per sign-changing grid edge, joining the four cells around it.
    for (int axis = 0; axis < 3; ++axis) {
        const int u = (axis + 1) % 3, w = (axis + 2) % 3;
        int dims[3] = {nx, ny, nz};
        for (int z = 0; z < nz; ++z)
            for (int y = 0; y < ny; ++y)
                for (int x = 0; x < nx; ++x) {
                    int p[3] = {x, y, z};
                    if (p[axis] + 1 >= dims[axis]) continue;
                    if (p[u] < 1 || p[w] < 1 || p[u] >= dims[u] - 1 || p[w] >= dims[w] - 1) continue;
                    int q[3] = {x, y, z};
                    q[axis] += 1;
                    float a = values[sampleIndex(p[0], p[1], p[2])];
                    float b = values[sampleIndex(q[0], q[1], q[2])];
                    if ((a < 0.0f) == (b < 0.0f)) continue;

                    int32_t quad[4];
                    const int du[4] = {-1, 0, 0, -1};
                    const int dw[4] = {-1, -1, 0, 0};
                    bool ok = true;
                    for (int k = 0; k < 4; ++k) {
                        int c[3] = {x, y, z};
                        c[u] += du[k];
                        c[w] += dw[k];
                        quad[k] = cellVertex[cellIndex(c[0], c[1], c[2])];
                        ok = ok && quad[k] >= 0;
                    }
                    if (!ok) continue;
                    // Inside-to-outside along +axis means the face looks along +axis.
                    bool facesPositive = a < 0.0f;
                    auto emit = [&](int32_t i0, int32_t i1, int32_t i2) {
                        Vec3 n = cross(mesh.positions[i1] - mesh.positions[i0], mesh.positions[i2] - mesh.positions[i0]);
                        float along = n[axis];
                        if ((along >= 0.0f) != facesPositive) std::swap(i1, i2);
                        mesh.triangles.push_back(
                            {static_cast<uint32_t>(i0), static_cast<uint32_t>(i1), static_cast<uint32_t>(i2)});
                    };
                    // Split along the shorter diagonal.
                    float d02 = length(mesh.positions[quad[0]] - mesh.positions[quad[2]]);
                    float d13 = length(mesh.positions[quad[1]] - mesh.positions[quad[3]]);
                    if (d02 <= d13) {
                        emit(quad[0], quad[1], quad[2]);
                        emit(quad[0], quad[2], quad[3]);
                    } else {
                        emit(quad[0], quad[1], quad[3]);
                        emit(quad[1], quad[2], quad[3]);
                    }
                }
    }

    mesh.normals.resize(mesh.positions.size());
    const float h = cell * 0.5f;
    for (size_t i = 0; i < mesh.positions.size(); ++i) {
        const Vec3& p = mesh.positions[i];
        Vec3 g{field(p + Vec3{h, 0, 0}) - field(p - Vec3{h, 0, 0}),
               field(p + Vec3{0, h, 0}) - field(p - Vec3{0, h, 0}),
               field(p + Vec3{0, 0, h}) - field(p - Vec3{0, 0, h})};
        mesh.normals[i] = normalize(g);
    }
    return mesh;
}

}  // namespace eclipse
