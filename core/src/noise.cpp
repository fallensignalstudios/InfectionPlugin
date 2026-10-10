#include <cmath>

#include "eclipse/random.h"

namespace eclipse {

Vec3 Noise::gradient(int x, int y, int z) const {
    uint32_t h = hash32(static_cast<uint32_t>(x) * 73856093u ^ static_cast<uint32_t>(y) * 19349663u ^
                        static_cast<uint32_t>(z) * 83492791u ^ seed_ * 2654435761u);
    // Uniform direction on the sphere from two hashed numbers.
    float u = static_cast<float>(h & 0xFFFFu) / 65535.0f * 2.0f - 1.0f;
    float theta = static_cast<float>(h >> 16) / 65535.0f * 6.28318530718f;
    float r = std::sqrt(std::max(0.0f, 1.0f - u * u));
    return {r * std::cos(theta), u, r * std::sin(theta)};
}

static float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

float Noise::value(const Vec3& p) const {
    float fx = std::floor(p.x), fy = std::floor(p.y), fz = std::floor(p.z);
    int ix = static_cast<int>(fx), iy = static_cast<int>(fy), iz = static_cast<int>(fz);
    Vec3 f{p.x - fx, p.y - fy, p.z - fz};
    float u = fade(f.x), v = fade(f.y), w = fade(f.z);

    auto corner = [&](int dx, int dy, int dz) {
        Vec3 g = gradient(ix + dx, iy + dy, iz + dz);
        return dot(g, Vec3{f.x - dx, f.y - dy, f.z - dz});
    };
    float x00 = lerpf(corner(0, 0, 0), corner(1, 0, 0), u);
    float x10 = lerpf(corner(0, 1, 0), corner(1, 1, 0), u);
    float x01 = lerpf(corner(0, 0, 1), corner(1, 0, 1), u);
    float x11 = lerpf(corner(0, 1, 1), corner(1, 1, 1), u);
    float y0 = lerpf(x00, x10, v);
    float y1 = lerpf(x01, x11, v);
    return lerpf(y0, y1, w) * 1.7f;
}

float Noise::fbm(const Vec3& p, int octaves, float lacunarity, float gain) const {
    float sum = 0.0f, amp = 1.0f, norm = 0.0f;
    Vec3 q = p;
    for (int i = 0; i < octaves; ++i) {
        sum += amp * value(q + Vec3{17.3f * i, 9.1f * i, 31.7f * i});
        norm += amp;
        amp *= gain;
        q *= lacunarity;
    }
    return norm > 0 ? sum / norm : 0.0f;
}

float Noise::ridged(const Vec3& p, int octaves) const {
    float sum = 0.0f, amp = 1.0f, norm = 0.0f;
    Vec3 q = p;
    for (int i = 0; i < octaves; ++i) {
        float n = 1.0f - std::fabs(value(q + Vec3{5.1f * i, 13.7f * i, 2.3f * i}));
        sum += amp * n * n;
        norm += amp;
        amp *= 0.5f;
        q *= 2.0f;
    }
    return norm > 0 ? sum / norm : 0.0f;
}

}  // namespace eclipse
