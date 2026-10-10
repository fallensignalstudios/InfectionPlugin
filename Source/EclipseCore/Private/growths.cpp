#include "eclipse/growths.h"

#include <algorithm>
#include <cmath>
#include <functional>

#include "eclipse/geometry.h"
#include "eclipse/random.h"

namespace eclipse {

namespace {

const float kPi = 3.14159265358979f;
const Vec3 kDown{0, -1, 0};

struct Context {
    const Mesh& surface;
    const SpreadField& spread;
    const PlateLayout& plates;
    const GrowthParams& params;
    const FeatureParams& f;
    std::vector<float> curvature;
    std::vector<Vec3> grain;          // tangent direction away from the seed
    std::vector<uint32_t> shuffled;   // every vertex, in seeded random order
    PointGrid grid;
    InsideTest inside;

    Context(const Mesh& s, const SpreadField& sp, const PlateLayout& pl, const GrowthParams& p,
            const FeatureParams& fp)
        : surface(s), spread(sp), plates(pl), params(p), f(fp),
          grid(s.positions, std::max(0.01f, fp.bridgeMaxGap * fp.scale * 0.5f)),
          inside(s, std::max(0.02f, fp.scale * 0.03f)) {}

    float infection(uint32_t v) const { return surface.attributes[v].infection; }
    // 1 near the seed (the oldest tissue), 0 at the front.
    float maturity(uint32_t v) const { return clamp01(1.0f - surface.attributes[v].arrival); }

    CorruptionAttributes attr(uint32_t v, float glow, float crack = 0.0f) const {
        CorruptionAttributes a;
        a.infection = 1.0f;
        a.glow = clamp01(glow);
        a.crack = crack;
        a.plateTint = surface.attributes[v].plateTint;
        // Growths emerge just after the tissue under them turns.
        a.arrival = std::min(4.0f, surface.attributes[v].arrival + 0.06f);
        return a;
    }

    std::vector<uint32_t> candidates(const std::function<bool(uint32_t)>& accept) const {
        std::vector<uint32_t> out;
        for (uint32_t v : shuffled)
            if (accept(v)) out.push_back(v);
        return out;
    }
};

// Value of `values` below which `share` of them fall.
float quantile(std::vector<float> values, float share) {
    if (values.empty()) return 0.0f;
    size_t k = static_cast<size_t>(clamp01(share) * static_cast<float>(values.size() - 1));
    std::nth_element(values.begin(), values.begin() + static_cast<long>(k), values.end());
    return values[k];
}

// Lit fissures: a few stretches of an otherwise dark rope glow from within.
float fissure(const Noise& noise, const Vec3& p, float frequency) {
    return smoothstep(0.66f, 0.76f, 0.5f + 0.5f * noise.fbm(p * frequency, 2));
}

void growVeinTubes(Context& c, const VeinNetwork& veins, Mesh& out, GrowthStats& stats) {
    Noise lit(c.params.seed * 389u + 17u);
    for (const auto& path : veins.paths) {
        if (path.points.size() < 2) continue;
        float widest = *std::max_element(path.radius.begin(), path.radius.end());
        if (widest < c.f.veinTubeMinRadius) continue;
        std::vector<Vec3> pts;
        std::vector<float> radii;
        std::vector<CorruptionAttributes> attrs;
        for (size_t i = 0; i < path.points.size(); ++i) {
            float r = path.radius[i] * 1.3f;
            // Raised roots, mostly proud of the tissue they run over.
            pts.push_back(path.points[i] + path.normals[i] * (r * 0.55f));
            radii.push_back(r);
            float glow = 0.6f * path.strength[i] * fissure(lit, path.points[i], 9.0f / c.f.scale);
            attrs.push_back(c.attr(path.vertices[i], glow));
        }
        appendTube(out, pts, radii, attrs, 7);
        ++stats.veinTubes;
    }
}

void growSpikes(Context& c, Rng& rng, Mesh& out, GrowthStats& stats) {
    const auto& S = c.surface;
    auto sites = poissonThin(S.positions, c.candidates([&](uint32_t v) { return c.infection(v) > 0.75f; }),
                             c.f.spikeSpacing * c.f.scale);
    struct Kept {
        Vec3 at;
        float reach;
    };
    std::vector<Kept> kept;
    for (uint32_t v : sites) {
        float m = c.maturity(v);
        // Gathered on the oldest tissue, near the cores.
        if (rng.uniform() > 0.12f + 0.88f * m * m) continue;
        // Mostly modest, a rare few very long.
        float u = rng.uniform();
        float len = c.f.spikeLength * c.f.scale * (0.5f + 6.0f * u * u * u * u) * (0.7f + 0.3f * m);
        // Big horns need room around them.
        const Vec3& p = S.positions[v];
        bool crowded = false;
        for (const auto& k : kept)
            if (length(k.at - p) < 0.45f * (k.reach + len)) {
                crowded = true;
                break;
            }
        if (crowded) continue;
        kept.push_back({p, len});

        const Vec3& n = S.normals[v];
        float r0 = len * 0.11f;
        // A scythe: up out of the tissue, then swept along the growth.
        Vec3 sweep = normalize(n * 0.3f + c.grain[v] * c.f.spikeSweep);
        Vec3 base = p - n * (r0 * 0.6f);
        auto pts = sampleQuadratic(base, p + n * (len * 0.55f), p + n * (len * 0.25f) + sweep * len, 9);
        std::vector<float> radii;
        std::vector<CorruptionAttributes> attrs;
        for (size_t i = 0; i < pts.size(); ++i) {
            float t = static_cast<float>(i) / static_cast<float>(pts.size() - 1);
            radii.push_back(i + 1 == pts.size() ? 0.0f : r0 * std::pow(1.0f - t, 1.1f));
            attrs.push_back(c.attr(v, 0.0f));
        }
        appendTube(out, pts, radii, attrs, 8);
        ++stats.spikes;
    }
}

// Ropes walked over the surface from the corruption's edge in toward its
// core, along the spread's own paths, turned a little to one side so they
// wrap around whatever they climb.
void growTendrils(Context& c, Rng& rng, Mesh& out, GrowthStats& stats) {
    const auto& S = c.surface;
    const float k = c.f.scale;
    const float step = 0.012f * k;
    PointGrid fine(S.positions, step * 2.0f);
    Noise lit(c.params.seed * 571u + 23u);
    auto starts = poissonThin(S.positions, c.candidates([&](uint32_t v) { return c.infection(v) > 0.3f; }),
                              c.f.tendrilSpacing * k);
    for (uint32_t start : starts) {
        if (rng.uniform() > c.f.tendrilDensity) continue;
        const float maxLength = c.f.tendrilLength * k * rng.range(0.4f, 1.4f) * (0.5f + 0.5f * c.maturity(start));
        const float twist = rng.range(-1.0f, 1.0f) * c.f.tendrilTwist;
        const float phase = rng.range(0.0f, 6.2832f);
        const float r0 = c.f.tendrilRadius * k * rng.range(0.6f, 1.4f);

        std::vector<Vec3> ground, normals;
        std::vector<uint32_t> under;
        uint32_t v = start;
        Vec3 p = S.positions[v];
        Vec3 d = -c.grain[v];
        for (float walked = 0.0f; walked < maxLength; walked += step) {
            const Vec3& n = S.normals[v];
            ground.push_back(p);
            normals.push_back(n);
            under.push_back(v);
            // Toward the seed, rotated about the normal by the twist, with a slow wobble.
            Vec3 t = -c.grain[v];
            t = t * std::cos(twist) + cross(n, t) * std::sin(twist);
            Vec3 wobble = cross(n, d) * (0.35f * std::sin(walked / step * 0.45f + phase));
            Vec3 next = lerp(d, t, 0.3f) + wobble;
            next -= n * dot(next, n);
            if (length(next) < 1e-6f) break;
            d = normalize(next);
            Vec3 ahead = p + d * step;
            int32_t w = fine.nearest(ahead, step * 2.5f);
            if (w < 0) break;
            v = static_cast<uint32_t>(w);
            const Vec3& nw = S.normals[v];
            p = ahead - nw * dot(ahead - S.positions[v], nw);  // back onto the surface
            if (c.infection(v) < 0.15f) break;                 // stay within the corruption
            if (c.spread.distance[v] < c.params.coreRadius) break;  // arrived at the heart
        }
        if (ground.size() < 4) continue;
        for (int pass = 0; pass < 3; ++pass)
            for (size_t i = 1; i + 1 < ground.size(); ++i)
                ground[i] = ground[i] * 0.5f + (ground[i - 1] + ground[i + 1]) * 0.25f;

        std::vector<Vec3> pts;
        std::vector<float> radii;
        std::vector<CorruptionAttributes> attrs;
        for (size_t i = 0; i < ground.size(); ++i) {
            float t = static_cast<float>(i) / static_cast<float>(ground.size() - 1);
            float r = r0 * (0.25f + 0.75f * t);  // thin at the tip, thick toward the core
            pts.push_back(ground[i] + normals[i] * (r * 0.65f));
            radii.push_back(r);
            attrs.push_back(c.attr(under[i], 0.5f * fissure(lit, ground[i], 7.0f / k) * t));
        }
        appendTube(out, pts, radii, attrs, 6);
        ++stats.tendrils;
    }
}

// The heart of each outbreak: a glowing orb in a ring of claws. Past
// mawCoverage it becomes a maw, the orb sunk and a second, wider ring
// curling in over it.
void growHearts(Context& c, Rng& rng, Mesh& out, GrowthStats& stats) {
    const auto& S = c.surface;
    std::vector<uint32_t> seeds = c.spread.seedVertices;
    std::sort(seeds.begin(), seeds.end());
    seeds.erase(std::unique(seeds.begin(), seeds.end()), seeds.end());
    const bool maw = c.params.coverage >= c.f.mawCoverage;
    for (uint32_t s : seeds) {
        const Vec3& n = S.normals[s];
        const Vec3& p = S.positions[s];
        float R = c.f.heartRadius * c.f.scale * (0.6f + 0.9f * clamp01(c.params.coverage));
        // No bigger than the body part it grows on: limit to the surface's
        // radius of curvature around the seed.
        float bend = 0.0f, weight = 0.0f;
        for (uint32_t v : c.grid.within(p, 0.06f * c.f.scale)) {
            if (dot(S.normals[v], n) < 0.7f) continue;  // the column beside a floor seed is not under it
            bend += std::max(0.0f, c.curvature[v]);
            weight += 1.0f;
        }
        // Only sharp curvature (limbs, fingers: radius under ~15 cm) limits it.
        if (weight > 0.0f && bend / weight > 12.0f) R = std::min(R, 1.6f / (bend / weight));
        Vec3 ax = anyPerpendicular(n);
        Vec3 ay = cross(n, ax);
        const Vec3 centre = p + n * (R * (maw ? -0.1f : 0.3f));
        appendEllipsoid(out, centre, ax, ay, n, Vec3{R, R, R * (maw ? 0.55f : 0.8f)}, 12, 18, [&](const Vec3& unit) {
            return c.attr(s, 0.3f + 0.5f * smoothstep(-0.3f, 0.85f, unit.z));
        });
        auto ring = [&](int count, float baseRadius, float rise, float reachIn, float thickness) {
            for (int i = 0; i < count; ++i) {
                float a = 6.2831853f * (static_cast<float>(i) + rng.range(-0.2f, 0.2f)) / static_cast<float>(count);
                Vec3 radial = ax * std::cos(a) + ay * std::sin(a);
                Vec3 base = p + radial * (baseRadius * R) - n * (0.15f * R);
                Vec3 control = base + n * (rise * R) + radial * (0.7f * R);
                Vec3 tip = centre + n * (R * (maw ? 0.6f : 1.1f)) + radial * (reachIn * R);
                auto pts = sampleQuadratic(base, control, tip, 10);
                std::vector<float> radii;
                std::vector<CorruptionAttributes> attrs;
                float r0 = thickness * R * rng.range(0.8f, 1.2f);
                for (size_t j = 0; j < pts.size(); ++j) {
                    float t = static_cast<float>(j) / static_cast<float>(pts.size() - 1);
                    radii.push_back(j + 1 == pts.size() ? 0.0f : r0 * std::pow(1.0f - t, 1.1f));
                    attrs.push_back(c.attr(s, 0.0f));
                }
                appendTube(out, pts, radii, attrs, 8);
            }
        };
        ring(c.f.heartClaws, 1.05f, 2.2f, 0.05f, 0.2f);
        if (maw) ring(c.f.heartClaws + 5, 1.9f, 3.4f, 0.35f, 0.25f);
        ++stats.hearts;
    }
}

// Segmented ridges along the thickest trunks, like a spine under the skin.
void growVertebrae(Context& c, const VeinNetwork& veins, Mesh& out, GrowthStats& stats) {
    const float spacing = c.f.vertebraSpacing * c.f.scale;
    // Trunks are the runs carrying the most drained area: the top tenth.
    auto peakFlow = [&](const VeinPath& path) {
        float most = 0.0f;
        for (uint32_t v : path.vertices) most = std::max(most, veins.flow[v]);
        return most;
    };
    std::vector<float> peaks;
    for (const auto& path : veins.paths) peaks.push_back(peakFlow(path));
    if (peaks.empty()) return;
    const float trunk = quantile(peaks, 0.9f);
    for (const auto& path : veins.paths) {
        if (path.points.size() < 3 || peakFlow(path) < trunk) continue;
        float next = 0.0f, walked = 0.0f;
        for (size_t i = 1; i + 1 < path.points.size(); ++i) {
            walked += length(path.points[i] - path.points[i - 1]);
            if (walked < next) continue;
            next = walked + spacing;
            uint32_t v = path.vertices[i];
            if (c.infection(v) < 0.5f) continue;
            const Vec3& n = path.normals[i];
            Vec3 along = path.points[i + 1] - path.points[i - 1];
            along -= n * dot(along, n);
            if (length(along) < 1e-6f) continue;
            along = normalize(along);
            Vec3 side = normalize(cross(n, along));
            float w = std::max(path.radius[i], c.params.veinMaxWidth) * 2.6f * c.f.scale;
            appendEllipsoid(out, path.points[i] + n * (path.radius[i] * 0.9f), side, along, n,
                            Vec3{w, w * 0.32f, w * 0.42f}, 6, 10, [&](const Vec3&) { return c.attr(v, 0.0f); });
            ++stats.vertebrae;
        }
    }
}

void growNodules(Context& c, Rng& rng, Mesh& out, GrowthStats& stats) {
    const auto& S = c.surface;
    std::vector<float> plated;
    for (uint32_t v = 0; v < S.vertexCount(); ++v)
        if (c.infection(v) > 0.7f && c.plates.plate[v] >= 0) plated.push_back(c.curvature[v]);
    if (plated.empty()) return;
    // Eyes and sacs settle into the flatter, hollower parts of plates.
    const float flat = quantile(plated, 0.5f);
    auto sites = poissonThin(S.positions, c.candidates([&](uint32_t v) {
        return c.infection(v) > 0.7f && c.plates.plate[v] >= 0 && c.curvature[v] <= flat &&
               c.plates.seamDistance[v] > c.params.plateSize * 0.2f;
    }), c.f.noduleSpacing * c.f.scale);

    for (uint32_t v : sites) {
        if (rng.uniform() > c.f.noduleChance * (0.5f + 0.5f * c.maturity(v))) continue;
        const Vec3& n = S.normals[v];
        float r = c.f.noduleRadius * c.f.scale * rng.range(0.6f, 1.4f);
        Vec3 ax = c.grain[v];
        Vec3 ay = cross(n, ax);
        bool eye = rng.uniform() < 0.5f;
        Vec3 centre = S.positions[v] + n * (r * 0.6f * 0.15f);
        appendEllipsoid(out, centre, ax, ay, n, Vec3{r, r * 0.85f, r * 0.6f}, 8, 12, [&](const Vec3& unit) {
            if (eye) {
                // A bright pupil ringed by a dark lid.
                float pupil = smoothstep(0.55f, 0.9f, unit.z);
                float lid = smoothstep(0.3f, 0.5f, unit.z) * (1.0f - pupil);
                return c.attr(v, pupil, lid);
            }
            // A glowing sac, translucent toward its crown.
            return c.attr(v, 0.4f * smoothstep(0.1f, 1.0f, unit.z));
        });
        ++stats.nodules;
    }
}

void growStrands(Context& c, Rng& rng, Mesh& out, GrowthStats& stats) {
    const auto& S = c.surface;
    auto sites = poissonThin(S.positions, c.candidates([&](uint32_t v) {
        return c.infection(v) > 0.5f && S.normals[v].y < -0.3f;
    }), c.f.strandSpacing * c.f.scale);

    for (uint32_t v : sites) {
        float m = c.maturity(v);
        if (rng.uniform() > 0.4f + 0.6f * m) continue;
        const Vec3& n = S.normals[v];
        float u = rng.uniform();
        float len = c.f.strandLength * c.f.scale * (0.35f + 0.9f * u * u) * (0.5f + 0.5f * m);
        float r0 = c.f.strandRadius * c.f.scale * rng.range(0.7f, 1.3f);
        Vec3 sway = normalize(cross(n, kDown));
        if (length(cross(n, kDown)) < 1e-3f) sway = anyPerpendicular(kDown);
        float phase = rng.range(0.0f, 6.2832f);
        bool glowing = rng.uniform() < c.f.strandGlowChance;

        std::vector<Vec3> pts;
        std::vector<float> radii;
        std::vector<CorruptionAttributes> attrs;
        const int samples = 10;
        for (int i = 0; i < samples; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(samples - 1);
            Vec3 p = S.positions[v] - n * (r0 * 0.8f) + n * (len * 0.12f * std::sin(t * kPi * 0.5f)) +
                     kDown * (len * std::pow(t, 1.4f)) + sway * (len * 0.06f * std::sin(t * 3.0f + phase) * t);
            // Stop where the strand would sink back into the host.
            if (i > 1 && c.inside.inside(p)) break;
            pts.push_back(p);
            radii.push_back(r0 * (1.0f - 0.6f * t));
            attrs.push_back(c.attr(v, glowing ? 0.55f * (0.3f + 0.7f * t) : 0.0f));
        }
        if (pts.size() < 4) continue;
        appendTube(out, pts, radii, attrs, 5);
        // A drip gathering at the end.
        float rd = radii.back() * 1.5f;
        appendEllipsoid(out, pts.back() + kDown * (rd * 0.8f), sway, cross(kDown, sway), kDown,
                        Vec3{rd, rd, rd * 1.7f}, 5, 8, [&](const Vec3&) { return attrs.back(); });
        ++stats.strands;
    }
}

std::vector<Vec3> catenary(const Vec3& a, const Vec3& b, float sag, int samples) {
    std::vector<Vec3> pts;
    for (int i = 0; i < samples; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(samples - 1);
        pts.push_back(lerp(a, b, t) + kDown * (sag * 4.0f * t * (1.0f - t)));
    }
    return pts;
}

bool clearOfHost(const Context& c, const std::vector<Vec3>& pts) {
    for (size_t i = 1; i + 1 < pts.size(); ++i)
        if (c.inside.inside(pts[i])) return false;
    return true;
}

// The best partner for a bridge from a: a surface across open air that faces back.
int32_t bridgePartner(const Context& c, uint32_t a, const std::vector<uint8_t>& used, float minGap, float maxGap,
                      float facing) {
    const auto& S = c.surface;
    int32_t best = -1;
    float bestScore = -1e9f;
    for (uint32_t b : c.grid.within(S.positions[a], maxGap)) {
        if (used[b] || c.infection(b) < 0.3f) continue;
        Vec3 d = S.positions[b] - S.positions[a];
        float len = length(d);
        if (len < minGap) continue;
        Vec3 dir = d / len;
        float fa = dot(S.normals[a], dir), fb = dot(S.normals[b], -dir);
        if (fa < facing || fb < facing) continue;
        float score = fa + fb + 0.3f * hashToUnit(b * 2654435761u + c.params.seed);
        if (score > bestScore) {
            bestScore = score;
            best = static_cast<int32_t>(b);
        }
    }
    return best;
}

void appendStrandTube(Context& c, uint32_t v, const std::vector<Vec3>& pts, float r, float glow, Mesh& out) {
    std::vector<float> radii;
    std::vector<CorruptionAttributes> attrs;
    for (size_t i = 0; i < pts.size(); ++i) {
        float t = static_cast<float>(i) / static_cast<float>(pts.size() - 1);
        radii.push_back(r * (1.0f - 0.45f * std::sin(kPi * t)));  // thinner in the middle, like stretched goo
        attrs.push_back(c.attr(v, glow));
    }
    appendTube(out, pts, radii, attrs, 5);
}

void growMembrane(Context& c, Rng& rng, uint32_t a, uint32_t b, float sagA, const std::vector<uint8_t>& used,
                  Mesh& out, GrowthStats& stats) {
    const auto& S = c.surface;
    Vec3 span = S.positions[b] - S.positions[a];
    float len = length(span);
    Vec3 side = cross(S.normals[a], span / len);
    if (length(side) < 1e-3f) return;
    side = normalize(side) * (rng.uniform() < 0.5f ? -1.0f : 1.0f);
    float width = len * rng.range(0.3f, 0.7f);
    int32_t a2 = c.grid.nearest(S.positions[a] + side * width, width * 0.6f);
    int32_t b2 = c.grid.nearest(S.positions[b] + side * width, width * 0.6f);
    if (a2 < 0 || b2 < 0 || a2 == static_cast<int32_t>(a) || b2 == static_cast<int32_t>(b)) return;
    if (used[a2] || used[b2] || c.infection(a2) < 0.3f || c.infection(b2) < 0.3f) return;
    Vec3 span2 = S.positions[b2] - S.positions[a2];
    float len2 = length(span2);
    if (len2 < 1e-3f || dot(S.normals[a2], span2 / len2) < 0.3f || dot(S.normals[b2], -span2 / len2) < 0.3f) return;

    const int U = 28;
    auto edgeA = catenary(S.positions[a], S.positions[b], sagA, U);
    auto edgeB = catenary(S.positions[a2], S.positions[b2], len2 * rng.range(0.1f, 0.3f), U);
    if (!clearOfHost(c, edgeB)) return;

    const int V = std::clamp(static_cast<int>(width / (len / static_cast<float>(U - 1))), 6, 20);
    const float belly = len * 0.06f;
    std::vector<Vec3> grid;
    for (int j = 0; j <= V; ++j)
        for (int i = 0; i < U; ++i) {
            float u = static_cast<float>(i) / static_cast<float>(U - 1), w = static_cast<float>(j) / static_cast<float>(V);
            grid.push_back(lerp(edgeA[i], edgeB[i], w) + kDown * (belly * std::sin(kPi * u) * std::sin(kPi * w)));
        }
    // The sheet's open interior must stay in the air; its attached edges
    // (first and last column) run along the skin and are exempt.
    for (int j = 1; j < V; ++j)
        for (int i = 1; i + 1 < U; ++i)
            if (c.inside.inside(grid[j * U + i])) return;

    // Tear holes: drop the interior cells whose noise ranks in the top share.
    Noise tear(c.params.seed * 977u + static_cast<uint32_t>(a));
    const float freq = 1.0f / (0.035f * c.f.scale);
    std::vector<float> noise(grid.size());
    for (size_t k = 0; k < grid.size(); ++k) noise[k] = tear.fbm(grid[k] * freq, 3);
    auto cellNoise = [&](int i, int j) {
        return 0.25f * (noise[j * U + i] + noise[j * U + i + 1] + noise[(j + 1) * U + i] + noise[(j + 1) * U + i + 1]);
    };
    std::vector<float> interior;
    for (int j = 1; j < V - 1; ++j)
        for (int i = 1; i < U - 2; ++i) interior.push_back(cellNoise(i, j));
    const float cut = quantile(interior, 1.0f - c.f.membraneHoles);

    Mesh sheet;
    sheet.positions = grid;
    for (size_t k = 0; k < grid.size(); ++k) {
        float rim = smoothstep(cut - 0.12f, cut, noise[k]);
        sheet.attributes.push_back(c.attr(a, 0.12f * rim));
    }
    for (int j = 0; j < V; ++j)
        for (int i = 0; i + 1 < U; ++i) {
            bool edge = j == 0 || j == V - 1 || i == 0 || i == U - 2;
            if (!edge && cellNoise(i, j) > cut) continue;
            uint32_t p00 = j * U + i, p10 = p00 + 1, p01 = p00 + U, p11 = p01 + 1;
            sheet.triangles.push_back({p00, p10, p11});
            sheet.triangles.push_back({p00, p11, p01});
        }
    sheet.computeNormals();
    // Two-sided: a back copy with flipped winding, a hair behind the front.
    Mesh back = sheet;
    for (size_t k = 0; k < back.positions.size(); ++k) {
        back.normals[k] = -sheet.normals[k];
        back.positions[k] = sheet.positions[k] - sheet.normals[k] * 0.0006f;
    }
    for (auto& t : back.triangles) std::swap(t[1], t[2]);
    out.append(sheet);
    out.append(back);

    appendStrandTube(c, a2, edgeB, c.f.strandRadius * c.f.scale, 0.0f, out);
    ++stats.membranes;
}

void growBridges(Context& c, Rng& rng, Mesh& out, GrowthStats& stats) {
    const auto& S = c.surface;
    const float k = c.f.scale;
    auto anchors = poissonThin(S.positions, c.candidates([&](uint32_t v) { return c.infection(v) > 0.5f; }),
                               c.f.bridgeSpacing * k);
    std::vector<uint8_t> used(S.vertexCount(), 0);
    for (uint32_t a : anchors) {
        if (used[a] || rng.uniform() > c.f.bridgeChance) continue;
        int32_t b = bridgePartner(c, a, used, c.f.bridgeMinGap * k, c.f.bridgeMaxGap * k, 0.55f);
        if (b < 0) continue;
        float len = length(S.positions[b] - S.positions[a]);
        float sag = len * rng.range(0.08f, 0.3f);
        auto pts = catenary(S.positions[a], S.positions[b], sag, 12);
        if (!clearOfHost(c, pts)) continue;
        used[a] = used[b] = 1;
        float glow = rng.uniform() < 0.2f ? 0.45f : 0.0f;
        appendStrandTube(c, a, pts, c.f.strandRadius * k * 1.2f, glow, out);
        ++stats.bridges;
        if (rng.uniform() < c.f.membraneChance) growMembrane(c, rng, a, static_cast<uint32_t>(b), sag, used, out, stats);
    }
}

}  // namespace

InsideTest::InsideTest(const Mesh& surface, float searchRadius)
    : surface_(surface), radius_(searchRadius), grid_(surface.positions, searchRadius) {}

bool InsideTest::inside(const Vec3& p) const {
    int32_t v = grid_.nearest(p, radius_);
    if (v < 0) return false;
    return dot(p - surface_.positions[v], surface_.normals[v]) < 0.0f;
}

Growth grow(const Mesh& host, const std::vector<Vec3>& seeds, const GrowthParams& params,
            const FeatureParams& features) {
    SurfaceGrowth base = growSurface(host, seeds, params);
    Growth g;
    g.surface = std::move(base.surface);
    g.spread = std::move(base.spread);
    g.veins = std::move(base.veins);
    g.plates = std::move(base.plates);
    if (!features.enabled || g.spread.threshold <= 0.0f) return g;

    const Mesh& S = g.surface;
    Context c(S, g.spread, g.plates, params, features);
    VertexGraph graph = VertexGraph::build(S);
    c.curvature = vertexCurvature(S, graph);
    c.grain.resize(S.vertexCount());
    for (uint32_t v = 0; v < S.vertexCount(); ++v) {
        const Vec3& n = S.normals[v];
        int32_t up = g.spread.parent[v];
        Vec3 d = up >= 0 ? S.positions[v] - S.positions[up] : Vec3{0, 0, 0};
        d -= n * dot(d, n);
        c.grain[v] = length(d) > 1e-8f ? normalize(d) : anyPerpendicular(n);
    }
    Rng rng(params.seed, 0x6A09E667ull);
    c.shuffled.resize(S.vertexCount());
    for (uint32_t v = 0; v < S.vertexCount(); ++v) c.shuffled[v] = v;
    for (size_t i = c.shuffled.size(); i > 1; --i)
        std::swap(c.shuffled[i - 1], c.shuffled[rng.below(static_cast<uint32_t>(i))]);

    // Each feature draws from its own stream, so turning one off or retuning
    // it leaves the others exactly where they were.
    Rng spikeRng(params.seed, 11), noduleRng(params.seed, 12), strandRng(params.seed, 13), bridgeRng(params.seed, 14);
    Rng tendrilRng(params.seed, 15), heartRng(params.seed, 16);
    if (features.veinTubes) growVeinTubes(c, g.veins, g.growths, g.stats);
    if (features.tendrils) growTendrils(c, tendrilRng, g.growths, g.stats);
    if (features.vertebrae) growVertebrae(c, g.veins, g.growths, g.stats);
    if (features.hearts) growHearts(c, heartRng, g.growths, g.stats);
    growSpikes(c, spikeRng, g.growths, g.stats);
    growNodules(c, noduleRng, g.growths, g.stats);
    growStrands(c, strandRng, g.growths, g.stats);
    growBridges(c, bridgeRng, g.growths, g.stats);
    return g;
}

}  // namespace eclipse
