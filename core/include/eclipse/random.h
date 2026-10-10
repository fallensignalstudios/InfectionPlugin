#pragma once

#include <cstdint>

#include "eclipse/math.h"

namespace eclipse {

// Deterministic, platform-independent generator (PCG32). Every procedural
// decision draws from one of these so a seed reproduces a growth exactly.
class Rng {
public:
    explicit Rng(uint64_t seed, uint64_t stream = 0x9E3779B97F4A7C15ull) {
        inc_ = (stream << 1u) | 1u;
        next();
        state_ += seed;
        next();
    }

    uint32_t next() {
        uint64_t old = state_;
        state_ = old * 6364136223846793005ull + inc_;
        uint32_t xorshifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
        uint32_t rot = static_cast<uint32_t>(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((~rot + 1u) & 31u));
    }

    // Uniform in [0, 1).
    float uniform() { return static_cast<float>(next() >> 8) * (1.0f / 16777216.0f); }
    float range(float lo, float hi) { return lo + (hi - lo) * uniform(); }
    uint32_t below(uint32_t n) { return n == 0 ? 0 : next() % n; }

private:
    uint64_t state_ = 0;
    uint64_t inc_ = 0;
};

inline uint32_t hash32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

inline float hashToUnit(uint32_t x) { return static_cast<float>(hash32(x) >> 8) * (1.0f / 16777216.0f); }

// Seeded 3D gradient noise in roughly [-1, 1], and fractal sums of it.
class Noise {
public:
    explicit Noise(uint32_t seed) : seed_(seed) {}

    float value(const Vec3& p) const;
    float fbm(const Vec3& p, int octaves, float lacunarity = 2.0f, float gain = 0.5f) const;
    // Sharp ridges where the noise crosses zero; in [0, 1].
    float ridged(const Vec3& p, int octaves) const;

private:
    Vec3 gradient(int x, int y, int z) const;
    uint32_t seed_;
};

}  // namespace eclipse
