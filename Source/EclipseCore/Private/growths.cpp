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

void growVeinTubes(Context& c, const VeinNetwork& veins, Mesh& out, GrowthStats& stats) {
    for (const auto& path : veins.paths) {
        if (path.points.size() < 2) continue;
        float widest = *std::max_element(path.radius.begin(), path.radius.end());
        if (widest < c.f.veinTubeMinRadius) continue;
        std::vector<Vec3> pts;
        std::vector<float> radii;
        std::vector<CorruptionAttributes> attrs;
        for (size_t i = 0; i < path.points.size(); ++i) {
            float r = path.radius[i] * 0.75f;
            // Half sunk into the tissue, as if grown from beneath it.
            pts.push_back(path.points[i] + path.normals[i] * (r * 0.25f));
            radii.push_back(r);
            attrs.push_back(c.attr(path.vertices[i], 0.35f + 0.65f * path.strength[i]));
        }
        appendTube(out, pts, radii, attrs, 6);
        ++stats.veinTubes;
    }
}

void growSpikes(Context& c, Rng& rng, Mesh& out, GrowthStats& stats) {
    const auto& S = c.surface;
    auto plated = [&](uint32_t v) { return c.infection(v) > 0.8f && c.plates.plate[v] >= 0; };
    std::vector<float> platedCurvature;
    for (uint32_t v = 0; v < S.vertexCount(); ++v)
        if (plated(v)) platedCurvature.push_back(c.curvature[v]);
    if (platedCurvature.empty()) return;
    // Spikes sprout from the sharpest ridges and the crests of plates.
    const float ridge = quantile(platedCurvature, 1.0f - c.f.spikeRidgeShare);
    const float crest = c.params.seamWidth * 2.0f;
    auto sites = poissonThin(S.positions, c.candidates([&](uint32_t v) {
        return plated(v) && c.curvature[v] >= ridge && c.plates.seamDistance[v] > crest;
    }), c.f.spikeSpacing * c.f.scale);

    for (uint32_t v : sites) {
        float m = c.maturity(v);
        if (rng.uniform() > 0.35f + 0.65f * m) continue;
        const Vec3& n = S.normals[v];
        float len = c.f.spikeLength * c.f.scale * rng.range(0.6f, 1.4f) * (0.6f + 0.4f * m);
        float r0 = len * 0.2f;
        Vec3 dir = normalize(n + c.grain[v] * c.f.spikeSweep);
        Vec3 base = S.positions[v] - n * (r0 * 0.6f);
        auto pts = sampleQuadratic(base, S.positions[v] + n * (len * 0.45f), S.positions[v] + dir * len, 7);
        std::vector<float> radii;
        std::vector<CorruptionAttributes> attrs;
        for (size_t i = 0; i < pts.size(); ++i) {
            float t = static_cast<float>(i) / static_cast<float>(pts.size() - 1);
            radii.push_back(i + 1 == pts.size() ? 0.0f : r0 * std::pow(1.0f - t, 1.3f));
            attrs.push_back(c.attr(v, 0.0f));
        }
        appendTube(out, pts, radii, attrs, 7);
        ++stats.spikes;
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
            attrs.push_back(c.attr(v, glowing ? 0.55f * (0.3f + 0.7f * t) : 0.04f));
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
        sheet.attributes.push_back(c.attr(a, 0.12f + 0.55f * rim));
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

    appendStrandTube(c, a2, edgeB, c.f.strandRadius * c.f.scale, 0.1f, out);
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
        float glow = rng.uniform() < 0.3f ? 0.5f : 0.06f;
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
    if (features.veinTubes) growVeinTubes(c, g.veins, g.growths, g.stats);
    growSpikes(c, spikeRng, g.growths, g.stats);
    growNodules(c, noduleRng, g.growths, g.stats);
    growStrands(c, strandRng, g.growths, g.stats);
    growBridges(c, bridgeRng, g.growths, g.stats);
    return g;
}

}  // namespace eclipse
