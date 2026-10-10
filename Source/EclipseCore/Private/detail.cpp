#include "eclipse/detail.h"

#include <algorithm>
#include <cmath>

#include "eclipse/math.h"
#include "eclipse/random.h"

namespace eclipse {

namespace {

const float kTau = 6.28318530718f;

int wrap(int i, int n) { return ((i % n) + n) % n; }

// Noise over the unit square that repeats exactly at its edges: gradient
// noise and cellular noise on lattices whose cells wrap around.
class Tileable {
public:
    explicit Tileable(uint32_t seed) : seed_(seed) {}

    // Gradient noise in about [-1, 1], `period` lattice cells across the tile.
    float noise(float u, float v, int period) const {
        float x = u * static_cast<float>(period), y = v * static_cast<float>(period);
        float fx = std::floor(x), fy = std::floor(y);
        int ix = static_cast<int>(fx), iy = static_cast<int>(fy);
        float tx = x - fx, ty = y - fy;
        auto corner = [&](int dx, int dy) {
            uint32_t h = hash32(static_cast<uint32_t>(wrap(ix + dx, period)) * 73856093u ^
                                static_cast<uint32_t>(wrap(iy + dy, period)) * 19349663u ^ seed_ * 2654435761u ^
                                static_cast<uint32_t>(period) * 83492791u);
            float a = static_cast<float>(h & 0xFFFFu) / 65535.0f * kTau;
            return std::cos(a) * (tx - static_cast<float>(dx)) + std::sin(a) * (ty - static_cast<float>(dy));
        };
        auto fade = [](float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); };
        float a = lerpf(corner(0, 0), corner(1, 0), fade(tx));
        float b = lerpf(corner(0, 1), corner(1, 1), fade(tx));
        return 1.4f * lerpf(a, b, fade(ty));
    }

    float fbm(float u, float v, int period, int octaves) const {
        float sum = 0.0f, amp = 0.5f, norm = 0.0f;
        for (int o = 0; o < octaves; ++o) {
            sum += amp * noise(u, v, period << o);
            norm += amp;
            amp *= 0.5f;
        }
        return sum / norm;
    }

    // Distances to the nearest and second-nearest of `count` x `countV`
    // jittered points, in cell widths. Unequal counts stretch the cells.
    void cells(float u, float v, int count, float& f1, float& f2, uint32_t* nearestId = nullptr, int countV = 0) const {
        if (countV <= 0) countV = count;
        float x = u * static_cast<float>(count), y = v * static_cast<float>(countV);
        int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
        f1 = f2 = 1e9f;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                int cx = ix + dx, cy = iy + dy;
                uint32_t id = static_cast<uint32_t>(wrap(cx, count) + wrap(cy, countV) * count);
                uint32_t h = hash32(id * 2654435761u ^ seed_ * 40503u ^ static_cast<uint32_t>(count) * 97u);
                float px = static_cast<float>(cx) + 0.1f + 0.8f * static_cast<float>(h & 0xFFFFu) / 65535.0f;
                float py = static_cast<float>(cy) + 0.1f + 0.8f * static_cast<float>(h >> 16) / 65535.0f;
                float d = std::sqrt((px - x) * (px - x) + (py - y) * (py - y));
                if (d < f1) {
                    f2 = f1;
                    f1 = d;
                    if (nearestId) *nearestId = id;
                } else if (d < f2) {
                    f2 = d;
                }
            }
    }

private:
    uint32_t seed_;
};

// Overlapping scales split by grooves, combed with growth striations and
// pitted here and there.
float chitinHeight(const Tileable& t, float u, float v) {
    float wu = u + 0.035f * t.fbm(u, v, 3, 3), wv = v + 0.035f * t.fbm(u + 0.37f, v + 0.61f, 3, 3);
    float f1, f2;
    uint32_t id = 0;
    t.cells(wu, wv, 5, f1, f2, &id, 9);  // longer than wide, like the sinew plates
    float groove = smoothstep(0.0f, 0.22f, f2 - f1);
    float dome = 1.0f - 0.35f * f1;
    float scales = groove * dome * (0.85f + 0.3f * hashToUnit(id * 7u + 3u));

    float comb = 0.5f + 0.5f * std::sin(kTau * (wv * 23.0f + 1.6f * t.fbm(u, v, 4, 2)));
    float p1, p2;
    uint32_t pitId = 0;
    t.cells(u, v, 29, p1, p2, &pitId);
    float pit = hashToUnit(pitId) < 0.12f ? 1.0f - smoothstep(0.06f, 0.22f, p1) : 0.0f;
    return 0.75f * scales + 0.1f * comb * groove - 0.18f * pit + 0.06f * t.fbm(u, v, 16, 3);
}

// Wrinkled, folded skin with pores and fine raised veinlets.
float fleshHeight(const Tileable& t, float u, float v) {
    float folds = std::pow(1.0f - std::fabs(t.fbm(u, v, 3, 3)), 4.0f);
    float wu = u + 0.03f * t.fbm(u, v, 4, 2);
    float wrinkles = std::pow(1.0f - std::fabs(t.fbm(wu, v, 9, 2)), 6.0f);
    float veinlets = std::pow(1.0f - std::fabs(t.fbm(u + 0.5f, v + 0.25f, 4, 3)), 18.0f);
    float p1, p2;
    uint32_t poreId = 0;
    t.cells(u, v, 40, p1, p2, &poreId);
    float pore = hashToUnit(poreId) < 0.3f ? 1.0f - smoothstep(0.05f, 0.2f, p1) : 0.0f;
    float swell = 0.5f + 0.5f * t.fbm(u, v, 2, 3);
    return 0.45f * swell + 0.5f * folds + 0.15f * wrinkles + 0.25f * veinlets - 0.06f * pore;
}

}  // namespace

DetailMap makeDetailMap(DetailKind kind, int size, uint32_t seed) {
    DetailMap map;
    if (size < 8) return map;
    map.size = size;
    const size_t n = static_cast<size_t>(size) * static_cast<size_t>(size);
    Tileable t(seed * 2246822519u + (kind == DetailKind::Chitin ? 11u : 23u));
    std::vector<float> h(n);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size);
            float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
            h[static_cast<size_t>(y) * size + x] = kind == DetailKind::Chitin ? chitinHeight(t, u, v) : fleshHeight(t, u, v);
        }
    auto [lo, hi] = std::minmax_element(h.begin(), h.end());
    const float low = *lo, range = std::max(1e-6f, *hi - *lo);
    for (float& value : h) value = (value - low) / range;
    auto at = [&](int x, int y) { return h[static_cast<size_t>(wrap(y, size)) * size + wrap(x, size)]; };

    // Slopes in height per tile width, scaled so that nearly all fit.
    std::vector<float> su(n), sv(n);
    std::vector<float> magnitudes;
    magnitudes.reserve(n);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            size_t i = static_cast<size_t>(y) * size + x;
            su[i] = (at(x + 1, y) - at(x - 1, y)) * 0.5f * static_cast<float>(size);
            sv[i] = (at(x, y + 1) - at(x, y - 1)) * 0.5f * static_cast<float>(size);
            magnitudes.push_back(std::max(std::fabs(su[i]), std::fabs(sv[i])));
        }
    std::nth_element(magnitudes.begin(), magnitudes.begin() + static_cast<long>(n * 98 / 100), magnitudes.end());
    const float slopeScale = 0.9f / std::max(1e-6f, magnitudes[n * 98 / 100]);

    // Cavity: how far below its neighbourhood each texel lies (a box blur,
    // separable and wrapped).
    const int radius = std::max(2, size / 96);
    std::vector<float> rows(n), blur(n);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            float s = 0.0f;
            for (int k = -radius; k <= radius; ++k) s += at(x + k, y);
            rows[static_cast<size_t>(y) * size + x] = s / static_cast<float>(2 * radius + 1);
        }
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            float s = 0.0f;
            for (int k = -radius; k <= radius; ++k) s += rows[static_cast<size_t>(wrap(y + k, size)) * size + x];
            blur[static_cast<size_t>(y) * size + x] = s / static_cast<float>(2 * radius + 1);
        }

    auto byte = [](float value) { return static_cast<uint8_t>(std::lround(clamp01(value) * 255.0f)); };
    map.rgba.resize(n * 4);
    for (size_t i = 0; i < n; ++i) {
        map.rgba[i * 4 + 0] = byte(0.5f + 0.5f * su[i] * slopeScale);
        map.rgba[i * 4 + 1] = byte(0.5f + 0.5f * sv[i] * slopeScale);
        map.rgba[i * 4 + 2] = byte(h[i]);
        map.rgba[i * 4 + 3] = byte(1.0f + 4.0f * (h[i] - blur[i]));
    }
    return map;
}

}  // namespace eclipse
