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

// Where a growth meets the surface: the tissue swells up around it, to
// `height` at the centre and back to nothing at `radius`. A hollow anchor
// rises only outside `hollow` (a share of radius): a socket or a crater rim.
struct Anchor {
    Vec3 at, normal;
    float radius, height;
    float hollow = 0.0f;
};

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
    Mesh* fleshOut = nullptr;         // soft tissue goes here, hard growths to `out`
    const GrowthGuide* guide = nullptr;
    std::vector<Anchor> anchors;
    // Points along horns and claws, out in the air, that slime can hang from.
    std::vector<std::pair<Vec3, float>> perches;
    // Space each heart keeps for itself (centre, radius): other growths stay
    // out, so roots do not run into a maw's throat.
    std::vector<std::pair<Vec3, float>> clearings;

    bool cleared(const Vec3& p) const {
        for (const auto& k : clearings)
            if (length(p - k.first) < k.second) return true;
        return false;
    }

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

    // Along a horn, claw or tooth (t from base to tip): dark at the root,
    // paling toward the tip like old horn or bone.
    CorruptionAttributes hornAttr(uint32_t v, float t, float pale = 0.8f) const {
        CorruptionAttributes a = attr(v, 0.0f);
        a.tone = pale * smoothstep(0.45f, 1.0f, t);
        return a;
    }

    // The same for soft tissue: wet and translucent in the material.
    CorruptionAttributes softAttr(uint32_t v, float glow, float crack = 0.0f) const {
        CorruptionAttributes a = attr(v, glow, crack);
        a.flesh = 1.0f;
        return a;
    }

    // A tube surface scaled by the roughness setting; `id` varies the noise.
    TubeStyle style(float lumps, float lumpSize, float rings, float ringSpacing, uint32_t id) const {
        TubeStyle t;
        t.lumps = lumps * f.roughness;
        t.lumpSize = std::max(1e-4f, lumpSize);
        t.rings = rings * f.roughness;
        t.ringSpacing = std::max(1e-4f, ringSpacing);
        t.seed = hash32(id * 2654435761u + params.seed);
        return t;
    }

    void fuse(const Vec3& at, const Vec3& normal, float radius, float height, float hollow = 0.0f) {
        if (f.fusion > 0.0f && radius > 0.0f) anchors.push_back({at, normal, radius, height * f.fusion, hollow});
    }

    // May anything grow on vertex v? Not where a bare mask lies.
    bool allowed(uint32_t v) const { return !guide || guide->bareAt(v) < 0.5f; }

    std::vector<uint32_t> candidates(const std::function<bool(uint32_t)>& accept) const {
        std::vector<uint32_t> out;
        for (uint32_t v : shuffled)
            if (allowed(v) && accept(v)) out.push_back(v);
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
            if (c.cleared(path.points[i])) break;  // ordered toward the seed: the rest is inside the heart
            if (!c.allowed(path.vertices[i])) break;
            float r = path.radius[i] * 1.3f;
            // Raised roots, mostly proud of the tissue they run over.
            pts.push_back(path.points[i] + path.normals[i] * (r * 0.55f));
            radii.push_back(r);
            float glow = 0.6f * path.strength[i] * fissure(lit, path.points[i], 9.0f / c.f.scale);
            attrs.push_back(c.attr(path.vertices[i], glow));
            if (i % 3 == 0) c.fuse(path.points[i], path.normals[i], r * 2.5f, r * 0.6f);
        }
        if (pts.size() < 2) continue;
        // Knotted, but no finer than the vein's own samples: there are
        // thousands of these in a room.
        appendTube(out, pts, radii, attrs, 7, true,
                   c.style(0.18f, std::max(widest * 6.0f, 0.006f * c.f.scale * 3.0f), 0.0f, 1.0f,
                           static_cast<uint32_t>(stats.veinTubes)));
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
        if (c.cleared(p)) continue;
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
        const Vec3 control = p + n * (len * 0.55f), tip = p + n * (len * 0.25f) + sweep * len;
        const bool big = len > 2.0f * c.f.spikeLength * c.f.scale;
        auto pts = sampleQuadratic(base, control, tip, big ? 28 : 16);
        std::vector<float> radii;
        std::vector<CorruptionAttributes> attrs;
        for (size_t i = 0; i < pts.size(); ++i) {
            float t = static_cast<float>(i) / static_cast<float>(pts.size() - 1);
            // Flared at the root, where it tears out of the tissue.
            float flare = 1.0f + 0.5f * std::pow(1.0f - t, 8.0f);
            radii.push_back(i + 1 == pts.size() ? 0.0f : r0 * flare * std::pow(1.0f - t, 1.1f));
            attrs.push_back(c.hornAttr(v, t));
        }
        // Growth rings near the base, like horn or keratin, and slight knots.
        appendTube(out, pts, radii, attrs, big ? 12 : 8, true, c.style(0.06f, r0 * 1.5f, 0.08f, len * 0.05f, v));
        for (float t : {0.35f, 0.6f}) {
            size_t i = static_cast<size_t>(t * static_cast<float>(pts.size() - 1));
            c.perches.emplace_back(pts[i], radii[i]);
        }
        c.fuse(p, n, r0 * 3.2f, r0 * 0.9f);
        ++stats.spikes;

        // Barbs: small hooked thorns along a big horn's outer curve, raked
        // back toward its base.
        if (!big || c.f.spikeBarbs <= 0) continue;
        const Vec3 bend = normalize((pts.front() + pts.back()) * 0.5f - pts[pts.size() / 2]);  // the outer side
        const int barbs = static_cast<int>(static_cast<float>(c.f.spikeBarbs) * rng.range(0.6f, 1.4f) + 0.5f);
        for (int b = 0; b < barbs; ++b) {
            float t = rng.range(0.25f, 0.8f);
            size_t i = static_cast<size_t>(t * static_cast<float>(pts.size() - 1));
            Vec3 along = normalize(pts[i + 1] - pts[i]);
            Vec3 side = bend - along * dot(bend, along);
            if (length(side) < 1e-4f) continue;
            side = normalize(side);
            float spin = rng.range(-0.8f, 0.8f);
            side = normalize(side * std::cos(spin) + cross(along, side) * std::sin(spin));
            float rb = radii[i] * rng.range(0.22f, 0.35f);
            float lb = radii[i] * rng.range(1.4f, 2.6f);
            Vec3 from = pts[i] + side * (radii[i] * 0.7f);
            Vec3 to = from + normalize(side * 0.8f - along * 0.5f) * lb;
            auto barb = sampleQuadratic(from, from + side * (lb * 0.6f), to, 5);
            std::vector<float> br;
            std::vector<CorruptionAttributes> ba;
            for (size_t j = 0; j < barb.size(); ++j) {
                br.push_back(j + 1 == barb.size() ? 0.0f : rb * (1.0f - static_cast<float>(j) / static_cast<float>(barb.size() - 1)));
                ba.push_back(c.hornAttr(v, static_cast<float>(j) / static_cast<float>(barb.size() - 1), 0.6f));
            }
            appendTube(out, barb, br, ba, 6);
            ++stats.barbs;
        }
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
            if (c.infection(v) < 0.15f || !c.allowed(v)) break;  // stay within the corruption
            if (c.spread.distance[v] < c.params.coreRadius || c.cleared(p)) break;  // arrived at the heart
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
            // Half sunk: the tissue heaves up along both flanks.
            if (i % 2 == 0) c.fuse(ground[i], normals[i], r * 2.8f, r * 0.7f);
        }
        appendTube(out, pts, radii, attrs, 8, true, c.style(0.32f, r0 * 2.2f, 0.0f, 1.0f, start));
        ++stats.tendrils;

        // Rootlets: thinner side roots forking off and feeling across the
        // tissue, so the network branches rather than running in parallel.
        for (size_t i = 3; i + 3 < ground.size(); i += 4) {
            if (rng.uniform() > c.f.tendrilBranching) continue;
            Vec3 dir = ground[i + 1] - ground[i - 1];
            const Vec3& n = normals[i];
            dir -= n * dot(dir, n);
            if (length(dir) < 1e-6f) continue;
            dir = normalize(dir);
            float turn = rng.range(0.6f, 1.2f) * (rng.uniform() < 0.5f ? -1.0f : 1.0f);
            dir = normalize(dir * std::cos(turn) + cross(n, dir) * std::sin(turn));
            const float reach = maxLength * rng.range(0.15f, 0.35f);
            const float curl = rng.range(-0.15f, 0.15f);
            std::vector<Vec3> branchGround{ground[i]}, branchNormals{n};
            std::vector<uint32_t> branchUnder{under[i]};
            Vec3 q = ground[i];
            for (float walked = step; walked < reach; walked += step) {
                const Vec3& nq = branchNormals.back();
                dir = normalize(dir + cross(nq, dir) * curl);
                Vec3 ahead = q + dir * step;
                int32_t w = fine.nearest(ahead, step * 2.5f);
                if (w < 0) break;
                const uint32_t u = static_cast<uint32_t>(w);
                const Vec3& nu = S.normals[u];
                q = ahead - nu * dot(ahead - S.positions[u], nu);
                if (c.infection(u) < 0.15f || c.cleared(q) || !c.allowed(u)) break;
                dir -= nu * dot(dir, nu);
                if (length(dir) < 1e-6f) break;
                dir = normalize(dir);
                branchGround.push_back(q);
                branchNormals.push_back(nu);
                branchUnder.push_back(u);
            }
            if (branchGround.size() < 3) continue;
            const float rb = radii[i] * rng.range(0.35f, 0.55f);
            std::vector<Vec3> bp;
            std::vector<float> brad;
            std::vector<CorruptionAttributes> battr;
            for (size_t j = 0; j < branchGround.size(); ++j) {
                float t = static_cast<float>(j) / static_cast<float>(branchGround.size() - 1);
                float r = rb * (1.0f - 0.75f * t);
                bp.push_back(branchGround[j] + branchNormals[j] * (r * 0.6f));
                brad.push_back(r);
                battr.push_back(c.attr(branchUnder[j], 0.0f));
                if (j % 2 == 1) c.fuse(branchGround[j], branchNormals[j], r * 2.6f, r * 0.6f);
            }
            appendTube(out, bp, brad, battr, 6, true, c.style(0.2f, rb * 2.0f, 0.0f, 1.0f, start * 31u + static_cast<uint32_t>(i)));
            ++stats.rootlets;
        }
    }
}

std::vector<uint32_t> heartSeeds(const Context& c) {
    std::vector<uint32_t> seeds = c.spread.seedVertices;
    std::sort(seeds.begin(), seeds.end());
    seeds.erase(std::unique(seeds.begin(), seeds.end()), seeds.end());
    return seeds;
}

bool heartIsMaw(const Context& c) { return c.params.coverage >= c.f.mawCoverage; }

float heartRadius(const Context& c, uint32_t s) {
    const auto& S = c.surface;
    const Vec3& n = S.normals[s];
    float R = c.f.heartRadius * c.f.scale * (0.6f + 0.9f * clamp01(c.params.coverage));
    // No bigger than the body part it grows on: limit to the surface's
    // radius of curvature around the seed.
    float bend = 0.0f, weight = 0.0f;
    for (uint32_t v : c.grid.within(S.positions[s], 0.06f * c.f.scale)) {
        if (dot(S.normals[v], n) < 0.7f) continue;  // the column beside a floor seed is not under it
        bend += std::max(0.0f, c.curvature[v]);
        weight += 1.0f;
    }
    // Only sharp curvature (limbs, fingers: radius under ~15 cm) limits it.
    if (weight > 0.0f && bend / weight > 12.0f) R = std::min(R, 1.6f / (bend / weight));
    return R;
}

// The heart of each outbreak: a glowing orb in a ring of claws, sitting on
// swollen tissue. Past mawCoverage it opens into a maw: a crater of lumpy
// lips around a ribbed throat that glows from deep inside, its rim set with
// teeth and a second, wider ring of claws curling in over the opening.
void growHearts(Context& c, Rng& rng, Mesh& out, GrowthStats& stats) {
    const auto& S = c.surface;
    const bool maw = heartIsMaw(c);
    Noise lumps(c.params.seed * 733u + 41u);
    for (uint32_t s : heartSeeds(c)) {
        if (!c.allowed(s)) continue;
        const Vec3& n = S.normals[s];
        const Vec3& p = S.positions[s];
        const float R = heartRadius(c, s);
        Vec3 ax = anyPerpendicular(n);
        Vec3 ay = cross(n, ax);

        // Claws: from `baseRadius` out on the surface, up through `rise`, to a
        // tip at `tipHeight` above the seed and `reachIn` out from its axis
        // (all in heart radii).
        auto ring = [&](int count, float baseRadius, float rise, float tipHeight, float reachIn, float thickness) {
            for (int i = 0; i < count; ++i) {
                float a = 6.2831853f * (static_cast<float>(i) + rng.range(-0.2f, 0.2f)) / static_cast<float>(count);
                Vec3 radial = ax * std::cos(a) + ay * std::sin(a);
                Vec3 base = p + radial * (baseRadius * R) - n * (0.15f * R);
                Vec3 control = base + n * (rise * R) + radial * (0.7f * R);
                Vec3 tip = p + n * (tipHeight * R) + radial * (reachIn * R);
                auto pts = sampleQuadratic(base, control, tip, 20);
                std::vector<float> radii;
                std::vector<CorruptionAttributes> attrs;
                float r0 = thickness * R * rng.range(0.8f, 1.2f);
                const float clawLength = length(tip - base) + length(control - base) * 0.5f;
                for (size_t j = 0; j < pts.size(); ++j) {
                    float t = static_cast<float>(j) / static_cast<float>(pts.size() - 1);
                    float flare = 1.0f + 0.25f * std::pow(1.0f - t, 8.0f);
                    radii.push_back(j + 1 == pts.size() ? 0.0f : r0 * flare * std::pow(1.0f - t, 1.1f));
                    attrs.push_back(c.hornAttr(s, t));
                }
                appendTube(out, pts, radii, attrs, 10, true,
                           c.style(0.05f, r0 * 1.5f, 0.09f, clawLength * 0.05f, s * 131u + static_cast<uint32_t>(i)));
                c.fuse(base + n * (0.15f * R), n, r0 * 3.0f, r0 * 0.8f);
                const size_t mid = pts.size() / 2;
                c.perches.emplace_back(pts[mid], radii[mid]);
            }
        };

        if (!maw) {
            const Vec3 centre = p + n * (R * 0.3f);
            const size_t v0 = c.fleshOut->vertexCount(), t0 = c.fleshOut->triangleCount();
            appendEllipsoid(*c.fleshOut, centre, ax, ay, n, Vec3{R, R, R * 0.8f}, 16, 24, [&](const Vec3& unit) {
                return c.softAttr(s, 0.3f + 0.5f * smoothstep(-0.3f, 0.85f, unit.z));
            });
            roughen(*c.fleshOut, v0, t0, 0.06f * R * c.f.roughness, 0.45f * R, s);
            ring(c.f.heartClaws, 1.05f, 2.2f, 1.4f, 0.05f, 0.2f);
            c.fuse(p, n, R * 2.2f, R * 0.4f);
            ++stats.hearts;
            continue;
        }

        // Lips: a lumpy ring of swollen tissue, its ends overlapping.
        const float lipRadius = 1.25f * R, lipThick = 0.36f * R, lipHeight = 0.6f * R;
        const int around = 40;
        std::vector<Vec3> lip;
        std::vector<float> lipRadii;
        std::vector<CorruptionAttributes> lipAttrs;
        for (int k = 0; k <= around + 1; ++k) {
            float a = 6.2831853f * static_cast<float>(k) / static_cast<float>(around);
            Vec3 radial = ax * std::cos(a) + ay * std::sin(a);
            Vec3 q = p + radial * lipRadius;
            float lump = lumps.fbm(q * (2.5f / R), 3);
            float knot = lumps.ridged(q * (7.0f / R), 2);
            lip.push_back(q + radial * (0.15f * R * lump) + n * (lipHeight * (1.0f + 0.45f * lump)));
            lipRadii.push_back(lipThick * (0.85f + 0.5f * lump + 0.35f * knot));
            lipAttrs.push_back(c.softAttr(s, 0.0f));
        }
        appendTube(*c.fleshOut, lip, lipRadii, lipAttrs, 10, /*capped*/ false);

        // Throat: a ribbed funnel from inside the lips down to a point just
        // above the surface, dark at the mouth and bright in the depths.
        const Vec3 mouth = p + n * (lipHeight + 0.05f * R);
        const Vec3 bottom = p + n * (0.06f * R);
        const int depthSteps = 14;
        std::vector<Vec3> throat;
        std::vector<float> throatRadii;
        std::vector<CorruptionAttributes> throatAttrs;
        for (int k = 0; k < depthSteps; ++k) {
            float t = static_cast<float>(k) / static_cast<float>(depthSteps - 1);
            throat.push_back(lerp(mouth, bottom, t));
            float rib = 1.0f + 0.07f * std::sin(t * 22.0f);
            throatRadii.push_back(k + 1 == depthSteps ? 0.0f : (lipRadius - 0.75f * lipThick) * (1.0f - 0.8f * t * t) * rib);
            throatAttrs.push_back(c.softAttr(s, 0.95f * smoothstep(0.35f, 1.0f, t)));
        }
        appendTube(*c.fleshOut, throat, throatRadii, throatAttrs, 18, /*capped*/ false);
        // The light at the bottom.
        appendEllipsoid(*c.fleshOut, bottom + n * (0.12f * R), ax, ay, n, Vec3{0.32f * R, 0.32f * R, 0.24f * R}, 8, 12,
                        [&](const Vec3&) { return c.softAttr(s, 1.0f); });

        // Teeth along the rim, pointing down into the throat.
        const int teeth = c.f.heartClaws + 9;
        for (int i = 0; i < teeth; ++i) {
            float a = 6.2831853f * (static_cast<float>(i) + rng.range(-0.25f, 0.25f)) / static_cast<float>(teeth);
            Vec3 radial = ax * std::cos(a) + ay * std::sin(a);
            Vec3 base = p + radial * (lipRadius - 0.35f * lipThick) + n * (lipHeight + 0.55f * lipThick);
            Vec3 tip = p + radial * (0.55f * R) + n * (lipHeight * rng.range(0.2f, 0.6f));
            auto pts = sampleQuadratic(base, base - radial * (0.1f * R) + n * (0.12f * R), tip, 7);
            std::vector<float> radii;
            std::vector<CorruptionAttributes> attrs;
            float r0 = 0.075f * R * rng.range(0.7f, 1.3f);
            for (size_t j = 0; j < pts.size(); ++j) {
                float t = static_cast<float>(j) / static_cast<float>(pts.size() - 1);
                radii.push_back(j + 1 == pts.size() ? 0.0f : r0 * std::pow(1.0f - t, 0.9f));
                attrs.push_back(c.hornAttr(s, t, 1.0f));  // teeth pale toward the tip
            }
            appendTube(out, pts, radii, attrs, 6, true, c.style(0.08f, r0 * 1.5f, 0.0f, 1.0f, s * 17u + static_cast<uint32_t>(i)));
        }

        ring(c.f.heartClaws + 3, 2.1f, 3.0f, 1.7f, 0.85f, 0.22f);
        // The crater wall rises outside the lips; its floor stays open.
        c.fuse(p, n, R * 2.7f, R * 0.5f, (lipRadius - 0.3f * R) / (R * 2.7f));
        ++stats.hearts;
        ++stats.maws;
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
            if (c.infection(v) < 0.5f || c.cleared(path.points[i]) || !c.allowed(v)) continue;
            const Vec3& n = path.normals[i];
            Vec3 along = path.points[i + 1] - path.points[i - 1];
            along -= n * dot(along, n);
            if (length(along) < 1e-6f) continue;
            along = normalize(along);
            Vec3 side = normalize(cross(n, along));
            float w = std::max(path.radius[i], c.params.veinMaxWidth) * 2.6f * c.f.scale;
            const size_t v0 = out.vertexCount(), t0 = out.triangleCount();
            appendEllipsoid(out, path.points[i] + n * (path.radius[i] * 0.9f), side, along, n,
                            Vec3{w, w * 0.32f, w * 0.42f}, 8, 12, [&](const Vec3&) { return c.attr(v, 0.0f); });
            roughen(out, v0, t0, 0.06f * w * c.f.roughness, 0.3f * w, v);
            c.fuse(path.points[i], n, w * 1.5f, w * 0.25f);
            ++stats.vertebrae;
        }
    }
}

// Eyes and glowing sacs, set into swollen sockets.
void growNodules(Context& c, Rng& rng, GrowthStats& stats) {
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
        if (c.cleared(S.positions[v])) continue;
        const Vec3& n = S.normals[v];
        float r = c.f.noduleRadius * c.f.scale * rng.range(0.6f, 1.4f);
        Vec3 ax = c.grain[v];
        Vec3 ay = cross(n, ax);
        bool eye = rng.uniform() < 0.5f;
        Vec3 centre = S.positions[v] + n * (r * 0.6f * 0.15f);
        const size_t v0 = c.fleshOut->vertexCount(), t0 = c.fleshOut->triangleCount();
        appendEllipsoid(*c.fleshOut, centre, ax, ay, n, Vec3{r, r * 0.85f, r * 0.6f}, 10, 14, [&](const Vec3& unit) {
            if (eye) {
                // A bright pupil ringed by a dark lid.
                float pupil = smoothstep(0.55f, 0.9f, unit.z);
                float lid = smoothstep(0.3f, 0.5f, unit.z) * (1.0f - pupil);
                return c.softAttr(v, pupil, lid);
            }
            // A sac, translucent and faintly lit toward its crown.
            return c.softAttr(v, 0.15f * smoothstep(0.3f, 1.0f, unit.z));
        });
        if (!eye) roughen(*c.fleshOut, v0, t0, 0.1f * r * c.f.roughness, 0.5f * r, v);  // an eye stays glassy
        c.fuse(S.positions[v], n, r * 1.8f, r * 0.45f, 0.5f);
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
        if (c.cleared(S.positions[v])) continue;
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
        const int samples = 16;
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
        // Beaded like stretched goo.
        appendTube(out, pts, radii, attrs, 6, true, c.style(0.3f, len * 0.2f, 0.0f, 1.0f, v));
        c.fuse(S.positions[v], n, r0 * 4.0f, r0 * 1.2f);
        // A drip gathering at the end.
        float rd = radii.back() * 1.5f;
        appendEllipsoid(*c.fleshOut, pts.back() + kDown * (rd * 0.8f), sway, cross(kDown, sway), kDown,
                        Vec3{rd, rd, rd * 1.7f}, 5, 8, [&](const Vec3&) {
                            CorruptionAttributes a = attrs.back();
                            a.flesh = 1.0f;
                            return a;
                        });
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
        if (used[b] || c.infection(b) < 0.3f || !c.allowed(b)) continue;
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
    appendTube(out, pts, radii, attrs, 6, true, c.style(0.3f, length(pts.back() - pts.front()) * 0.15f, 0.0f, 1.0f, v));
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
    if (!c.allowed(static_cast<uint32_t>(a2)) || !c.allowed(static_cast<uint32_t>(b2))) return;
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
        sheet.attributes.push_back(c.softAttr(a, 0.12f * rim));
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
    c.fleshOut->append(sheet);
    c.fleshOut->append(back);

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
        if (c.cleared(S.positions[a]) || c.cleared(S.positions[b])) continue;
        float len = length(S.positions[b] - S.positions[a]);
        float sag = len * rng.range(0.08f, 0.3f);
        auto pts = catenary(S.positions[a], S.positions[b], sag, 12);
        if (!clearOfHost(c, pts)) continue;
        used[a] = used[b] = 1;
        float glow = rng.uniform() < 0.2f ? 0.45f : 0.0f;
        appendStrandTube(c, a, pts, c.f.strandRadius * k * 1.2f, glow, out);
        float rb = c.f.strandRadius * k * 1.2f;
        c.fuse(S.positions[a], S.normals[a], rb * 4.0f, rb * 1.2f);
        c.fuse(S.positions[b], S.normals[b], rb * 4.0f, rb * 1.2f);
        ++stats.bridges;
        if (rng.uniform() < c.f.membraneChance) growMembrane(c, rng, a, static_cast<uint32_t>(b), sag, used, out, stats);
    }
}


// A string of mucus between two points: sagging, beaded, thinnest in the
// middle, with a drop gathering at its lowest point.
bool appendSlime(Context& c, uint32_t v, const Vec3& a, const Vec3& b, float sag, float radius) {
    float len = length(b - a);
    if (len < 1e-4f) return false;
    auto pts = catenary(a, b, sag, 14);
    if (!clearOfHost(c, pts)) return false;
    std::vector<float> radii;
    std::vector<CorruptionAttributes> attrs;
    size_t lowest = 0;
    for (size_t i = 0; i < pts.size(); ++i) {
        float t = static_cast<float>(i) / static_cast<float>(pts.size() - 1);
        radii.push_back(radius * (1.0f - 0.55f * std::sin(kPi * t)));
        attrs.push_back(c.softAttr(v, 0.0f));
        if (pts[i].y < pts[lowest].y) lowest = i;
    }
    appendTube(*c.fleshOut, pts, radii, attrs, 5, true, c.style(0.45f, len * 0.12f, 0.0f, 1.0f, v * 7u + 3u));
    if (lowest > 0 && lowest + 1 < pts.size()) {
        float rd = radius * 2.2f;
        Vec3 side = anyPerpendicular(kDown);
        appendEllipsoid(*c.fleshOut, pts[lowest] + kDown * (rd * 0.7f), side, cross(kDown, side), kDown,
                        Vec3{rd, rd, rd * 1.6f}, 5, 8, [&](const Vec3&) { return c.softAttr(v, 0.0f); });
    }
    return true;
}

void growSlime(Context& c, Rng& rng, GrowthStats& stats) {
    const auto& S = c.surface;
    const float k = c.f.scale;
    const float r = c.f.slimeRadius * k;
    // From horns and claws down to the tissue beneath them.
    for (const auto& [at, thickness] : c.perches) {
        if (rng.uniform() > c.f.slimeChance) continue;
        const int strings = 1 + static_cast<int>(rng.below(3));
        for (int s = 0; s < strings; ++s) {
            // A spot on the tissue below and around the perch, facing it.
            Vec3 aim = at + kDown * (0.06f * k) +
                       Vec3{rng.range(-1.0f, 1.0f), 0.0f, rng.range(-1.0f, 1.0f)} * (0.04f * k);
            int32_t w = c.grid.nearest(aim, 0.1f * k);
            if (w < 0) continue;
            const uint32_t u = static_cast<uint32_t>(w);
            Vec3 d = at - S.positions[u];
            float len = length(d);
            if (len < thickness * 2.0f || len > 0.15f * k || dot(S.normals[u], d) <= 0.0f) continue;
            if (c.infection(u) < 0.3f || !c.allowed(u)) continue;
            if (appendSlime(c, u, at, S.positions[u], len * rng.range(0.15f, 0.4f), r * rng.range(0.7f, 1.4f)))
                ++stats.slime;
        }
    }
    // Across tight creases: short strings between tissue that faces itself.
    auto sites = poissonThin(S.positions, c.candidates([&](uint32_t v) { return c.infection(v) > 0.6f; }), 0.05f * k);
    std::vector<uint8_t> used(S.vertexCount(), 0);
    for (uint32_t a : sites) {
        if (rng.uniform() > 0.4f * c.f.slimeChance || c.cleared(S.positions[a])) continue;
        int32_t b = bridgePartner(c, a, used, 0.012f * k, 0.07f * k, 0.3f);
        if (b < 0) continue;
        const Vec3 pa = S.positions[a], pb = S.positions[static_cast<uint32_t>(b)];
        const int strings = 1 + static_cast<int>(rng.below(3));
        bool any = false;
        for (int s = 0; s < strings; ++s) {
            Vec3 jitter = Vec3{rng.range(-1.0f, 1.0f), rng.range(-1.0f, 1.0f), rng.range(-1.0f, 1.0f)} * (0.004f * k);
            any = appendSlime(c, a, pa + jitter, pb - jitter, length(pb - pa) * rng.range(0.1f, 0.35f),
                              r * rng.range(0.6f, 1.2f)) || any;
        }
        if (any) {
            used[a] = used[static_cast<uint32_t>(b)] = 1;
            ++stats.slime;
        }
    }
}

// Clusters of swollen pods packed into the creases, translucent and lit
// faintly from within.
void growSacs(Context& c, Rng& rng, GrowthStats& stats) {
    const auto& S = c.surface;
    const float k = c.f.scale;
    auto sites = poissonThin(S.positions, c.candidates([&](uint32_t v) { return c.infection(v) > 0.6f; }),
                             c.f.sacSpacing * k);
    // A true fold or corner: the tissue around rises well above the site's
    // tangent plane. Measured over several centimetres, so the knots and
    // plates of the carapace do not count.
    const float reachOut = 0.08f * k;
    auto hollow = [&](uint32_t v) {
        float rise = 0.0f;
        size_t count = 0;
        for (uint32_t u : c.grid.within(S.positions[v], reachOut)) {
            rise += dot(S.positions[u] - S.positions[v], S.normals[v]);
            ++count;
        }
        return count > 0 ? rise / static_cast<float>(count) : 0.0f;
    };
    // The most enclosed sixth of the sites, if they are folds at all.
    std::vector<float> depth;
    for (uint32_t v : sites) depth.push_back(hollow(v));
    const float fold = std::max(quantile(depth, 0.83f), 0.0005f * k);
    for (size_t i = 0; i < sites.size(); ++i) {
        const uint32_t v = sites[i];
        if (depth[i] < fold) continue;
        if (rng.uniform() > c.f.sacChance * (0.4f + 0.6f * c.maturity(v))) continue;
        const Vec3& n = S.normals[v];
        const Vec3 t = c.grain[v];
        const Vec3 b = cross(n, t);
        const int count = std::max(1, static_cast<int>(static_cast<float>(c.f.sacCluster) * rng.range(0.5f, 1.5f) + 0.5f));
        const float reach = c.f.sacRadius * k * 1.6f;
        size_t grown = 0;
        for (int j = 0; j < count; ++j) {
            float a = rng.range(0.0f, 6.2831853f);
            float d = j == 0 ? 0.0f : reach * std::sqrt(rng.uniform());
            int32_t w = c.grid.nearest(S.positions[v] + (t * std::cos(a) + b * std::sin(a)) * d, reach * 2.0f);
            if (w < 0) continue;
            const uint32_t u = static_cast<uint32_t>(w);
            const Vec3& nu = S.normals[u];
            if (dot(nu, n) < 0.5f || c.infection(u) < 0.4f || c.cleared(S.positions[u]) || !c.allowed(u)) continue;
            float r = c.f.sacRadius * k * rng.range(0.55f, 1.25f) * (j == 0 ? 1.2f : 1.0f);
            Vec3 ax = t - nu * dot(t, nu);
            ax = length(ax) > 1e-6f ? normalize(ax) : anyPerpendicular(nu);
            Vec3 ay = cross(nu, ax);
            float stretch = rng.range(1.0f, 1.5f);
            // Most are dark; a few hold a faint light under the crown.
            float glow = rng.uniform() < 0.3f ? rng.range(0.1f, 0.3f) : 0.0f;
            const size_t v0 = c.fleshOut->vertexCount(), t0 = c.fleshOut->triangleCount();
            appendEllipsoid(*c.fleshOut, S.positions[u] + nu * (r * 0.35f), ax, ay, nu, Vec3{r * stretch, r, r * 0.85f}, 10,
                            16, [&](const Vec3& unit) { return c.softAttr(u, glow * smoothstep(0.3f, 1.0f, unit.z)); });
            roughen(*c.fleshOut, v0, t0, 0.12f * r * c.f.roughness, 0.55f * r, u);
            c.fuse(S.positions[u], nu, r * 1.9f, r * 0.45f);
            ++grown;
        }
        if (grown > 0) ++stats.sacs;
    }
}

// --- Formations -------------------------------------------------------------

// Walks over the surface from vertex `start` in direction `dir` (projected
// onto the surface at every step) for up to `length`, staying within the
// corruption. Returns the ground points, their normals and vertices.
struct SurfaceWalk {
    std::vector<Vec3> points, normals;
    std::vector<uint32_t> vertices;
};

SurfaceWalk walkSurface(const Context& c, uint32_t start, Vec3 dir, float length, float step) {
    const auto& S = c.surface;
    SurfaceWalk w;
    Vec3 p = S.positions[start];
    uint32_t v = start;
    for (float walked = 0.0f; walked <= length; walked += step) {
        const Vec3& n = S.normals[v];
        dir -= n * dot(dir, n);
        if (eclipse::length(dir) < 1e-6f) break;
        dir = normalize(dir);
        w.points.push_back(p);
        w.normals.push_back(n);
        w.vertices.push_back(v);
        Vec3 ahead = p + dir * step;
        int32_t next = c.grid.nearest(ahead, step * 3.0f);
        if (next < 0) break;
        v = static_cast<uint32_t>(next);
        p = ahead - S.normals[v] * dot(ahead - S.positions[v], S.normals[v]);
        if (c.infection(v) < 0.3f || !c.allowed(v)) break;
    }
    return w;
}

// Columns of twisted roots rising from upward-facing tissue.
void growSpires(Context& c, Rng& rng, Mesh& out, GrowthStats& stats) {
    const auto& S = c.surface;
    const float k = c.f.scale;
    const Vec3 up{0, 1, 0};
    auto sites = poissonThin(S.positions, c.candidates([&](uint32_t v) {
        return c.infection(v) > 0.85f && S.normals[v].y > 0.75f && c.maturity(v) > 0.25f;
    }), c.f.spireSpacing * k);
    for (uint32_t v : sites) {
        if (rng.uniform() > 0.6f || c.cleared(S.positions[v])) continue;
        const Vec3 base = S.positions[v];
        float H = c.f.spireHeight * k * rng.range(0.6f, 1.3f) * (0.6f + 0.4f * c.maturity(v));
        // No taller than the open air above it.
        for (float h = 0.1f * H; h <= H; h += 0.05f * H)
            if (c.inside.inside(base + up * h)) {
                H = h * 0.85f;
                break;
            }
        if (H < 0.2f * c.f.spireHeight * k) continue;
        const float Rb = 0.14f * H;
        const Vec3 axis = normalize(up * 0.85f + S.normals[v] * 0.15f);
        const Vec3 ax = anyPerpendicular(axis), ay = cross(axis, ax);
        const Vec3 lean = (ax * rng.range(-1.0f, 1.0f) + ay * rng.range(-1.0f, 1.0f)) * (0.12f * H);
        const int strands = 5 + static_cast<int>(rng.below(3));
        const float turns = rng.range(0.5f, 1.1f) * (rng.uniform() < 0.5f ? -1.0f : 1.0f);
        const int samples = 40;
        for (int s = 0; s < strands; ++s) {
            const float a0 = 6.2831853f * static_cast<float>(s) / static_cast<float>(strands) + rng.range(-0.3f, 0.3f);
            const float rs = Rb * rng.range(0.38f, 0.5f);
            std::vector<Vec3> pts;
            std::vector<float> radii;
            std::vector<CorruptionAttributes> attrs;
            for (int i = 0; i < samples; ++i) {
                float t = static_cast<float>(i) / static_cast<float>(samples - 1);
                // The strands wind together, then splay apart at the crown.
                float spread = Rb * (1.0f - 0.7f * t) + Rb * 2.2f * smoothstep(0.82f, 1.0f, t);
                float a = a0 + 6.2831853f * turns * t;
                Vec3 centre = base + axis * (H * t) + lean * (t * t) - axis * (0.3f * Rb);
                pts.push_back(centre + (ax * std::cos(a) + ay * std::sin(a)) * spread);
                radii.push_back(i + 1 == samples ? 0.0f : rs * (1.0f - 0.8f * t));
                attrs.push_back(c.hornAttr(v, smoothstep(0.7f, 1.0f, t), 0.5f));
            }
            appendTube(out, pts, radii, attrs, 8, true, c.style(0.15f, rs * 2.0f, 0.0f, 1.0f, v * 13u + static_cast<uint32_t>(s)));
            c.perches.emplace_back(pts[samples * 3 / 4], radii[samples * 3 / 4]);
        }
        // The glowing core between the strands.
        std::vector<Vec3> core;
        std::vector<float> coreRadii;
        std::vector<CorruptionAttributes> coreAttrs;
        for (int i = 0; i < 20; ++i) {
            float t = static_cast<float>(i) / 19.0f * 0.8f;
            core.push_back(base + axis * (H * t) + lean * (t * t));
            coreRadii.push_back(i == 19 ? 0.0f : Rb * 0.42f * (1.0f - 0.6f * t));
            coreAttrs.push_back(c.softAttr(v, 0.7f * (0.4f + 0.6f * std::sin(t * 9.0f) * std::sin(t * 9.0f))));
        }
        appendTube(*c.fleshOut, core, coreRadii, coreAttrs, 10, true, c.style(0.2f, Rb, 0.0f, 1.0f, v));
        c.fuse(base, S.normals[v], Rb * 3.2f, Rb * 0.9f);
        c.clearings.emplace_back(base, Rb * 2.0f);
        ++stats.spires;
    }
}

// A buried spine with paired ribs arching up over it.
void growRibcages(Context& c, Rng& rng, Mesh& out, GrowthStats& stats) {
    const auto& S = c.surface;
    const float k = c.f.scale;
    auto sites = poissonThin(S.positions, c.candidates([&](uint32_t v) {
        // Lying on floors and ledges, like remains.
        return c.infection(v) > 0.9f && c.maturity(v) > 0.3f && S.normals[v].y > 0.6f;
    }), c.f.ribcageSpacing * k);
    for (uint32_t v : sites) {
        if (rng.uniform() > 0.7f || c.cleared(S.positions[v])) continue;
        const float L = c.f.ribcageLength * k * rng.range(0.7f, 1.2f);
        // Lay the spine across the grain, so it runs past the cores rather
        // than into them.
        Vec3 dir = cross(S.normals[v], c.grain[v]);
        SurfaceWalk spine = walkSurface(c, v, dir, L, 0.01f * k);
        if (spine.points.size() < 12) continue;
        const size_t n = spine.points.size();
        const float W = L * rng.range(0.22f, 0.32f);  // half-width of the cage
        const float Hc = W * rng.range(1.0f, 1.4f);   // height of the arch
        // The spine: a row of half-buried vertebrae.
        const size_t ribEvery = std::max<size_t>(2, n / 10);
        for (size_t i = 0; i < n; i += ribEvery / 2 + 1) {
            const Vec3& p = spine.points[i];
            const Vec3& nn = spine.normals[i];
            Vec3 along = spine.points[std::min(i + 1, n - 1)] - spine.points[i > 0 ? i - 1 : 0];
            along -= nn * dot(along, nn);
            if (length(along) < 1e-6f) continue;
            along = normalize(along);
            float w = 0.06f * L;
            const size_t v0 = out.vertexCount(), t0 = out.triangleCount();
            appendEllipsoid(out, p + nn * (0.15f * w), cross(nn, along), along, nn, Vec3{w * 0.9f, w * 0.55f, w * 0.6f}, 8,
                            12, [&](const Vec3& u) {
                                CorruptionAttributes a = c.attr(spine.vertices[i], 0.0f);
                                a.tone = 0.35f + 0.3f * smoothstep(0.0f, 1.0f, u.z);
                                return a;
                            });
            roughen(out, v0, t0, 0.08f * w * c.f.roughness, 0.4f * w, spine.vertices[i]);
            c.fuse(p, nn, w * 2.2f, w * 0.5f);
        }
        // Ribs, longest in the middle of the cage.
        for (size_t i = ribEvery; i + ribEvery / 2 < n; i += ribEvery) {
            const Vec3& p = spine.points[i];
            const Vec3& nn = spine.normals[i];
            Vec3 along = spine.points[std::min(i + 1, n - 1)] - spine.points[i - 1];
            along -= nn * dot(along, nn);
            if (length(along) < 1e-6f) continue;
            along = normalize(along);
            const Vec3 side = normalize(cross(nn, along));
            const float x = static_cast<float>(i) / static_cast<float>(n - 1);
            const float size = 0.45f + 0.55f * std::sin(3.14159265f * x);
            for (float sign : {-1.0f, 1.0f}) {
                const Vec3 s = side * sign;
                const float w = W * size * rng.range(0.85f, 1.1f), h = Hc * size * rng.range(0.85f, 1.1f);
                // Up out of the spine, out wide, then curling back in overhead.
                const Vec3 a = p + s * (0.08f * w) - nn * (0.02f * w);
                const Vec3 bend = p + s * (1.25f * w) + nn * (0.75f * h) - along * (0.1f * w);
                const Vec3 d = p + s * (0.4f * w) + nn * (1.1f * h) - along * (0.25f * w);
                auto pts = sampleQuadratic(a, bend, d, 26);
                if (!clearOfHost(c, pts)) continue;
                const float r0 = 0.07f * w + 0.004f * k;
                std::vector<float> radii;
                std::vector<CorruptionAttributes> attrs;
                for (size_t j = 0; j < pts.size(); ++j) {
                    float t = static_cast<float>(j) / static_cast<float>(pts.size() - 1);
                    radii.push_back(j + 1 == pts.size() ? 0.0f : r0 * (1.0f - 0.75f * t) * (1.0f + 0.4f * std::pow(1.0f - t, 6.0f)));
                    CorruptionAttributes at = c.attr(spine.vertices[i], 0.0f);
                    at.tone = 0.15f + 0.6f * t;  // bone, palest at the tips
                    attrs.push_back(at);
                }
                appendTube(out, pts, radii, attrs, 8, true,
                           c.style(0.05f, r0 * 2.0f, 0.05f, w * 0.12f, v * 31u + static_cast<uint32_t>(i) * 2u + (sign > 0 ? 1u : 0u)));
                c.perches.emplace_back(pts[pts.size() * 2 / 3], radii[pts.size() * 2 / 3]);
                c.fuse(a, nn, r0 * 3.0f, r0 * 0.8f);
            }
        }
        ++stats.ribcages;
    }
}

// Large egg cocoons with a shape glowing faintly inside, roots wound round.
void growPods(Context& c, Rng& rng, Mesh& out, GrowthStats& stats) {
    const auto& S = c.surface;
    const float k = c.f.scale;
    const Vec3 up{0, 1, 0};
    auto sites = poissonThin(S.positions, c.candidates([&](uint32_t v) {
        return c.infection(v) > 0.8f && S.normals[v].y > -0.2f && c.maturity(v) > 0.2f;
    }), c.f.podSpacing * k);
    for (uint32_t v : sites) {
        if (rng.uniform() > 0.5f || c.cleared(S.positions[v])) continue;
        const int count = 1 + static_cast<int>(rng.below(3));
        bool any = false;
        for (int i = 0; i < count; ++i) {
            const float L = c.f.podSize * k * rng.range(0.7f, 1.3f);
            const float R = L * rng.range(0.26f, 0.34f);  // egg-shaped, about twice as long as wide
            Vec3 at = S.positions[v];
            if (i > 0) {
                const Vec3 t1 = anyPerpendicular(S.normals[v]), t2 = cross(S.normals[v], t1);
                const float a = rng.range(0.0f, 6.2831853f);
                const Vec3 offset = (t1 * std::cos(a) + t2 * std::sin(a)) * (1.6f * R);
                int32_t w = c.grid.nearest(at + offset, 2.0f * R);
                if (w < 0) continue;
                at = S.positions[static_cast<uint32_t>(w)];
            }
            int32_t near = c.grid.nearest(at, 0.05f * k);
            if (near < 0 || !c.allowed(static_cast<uint32_t>(near))) continue;
            const uint32_t u = static_cast<uint32_t>(near);
            const Vec3& n = S.normals[u];
            // Standing up from floors, leaning out from walls.
            const Vec3 axis = normalize(n * 0.6f + up * 0.4f + anyPerpendicular(n) * rng.range(-0.2f, 0.2f));
            const Vec3 ax = anyPerpendicular(axis), ay = cross(axis, ax);
            const Vec3 centre = at + axis * (L * 0.45f);
            if (c.inside.inside(centre + axis * L * 0.5f)) continue;
            const size_t v0 = c.fleshOut->vertexCount(), t0 = c.fleshOut->triangleCount();
            const float glow = rng.range(0.15f, 0.4f);
            appendEllipsoid(*c.fleshOut, centre, ax, ay, axis, Vec3{R, R * rng.range(0.85f, 1.0f), L * 0.5f}, 18, 24,
                            [&](const Vec3& unit) {
                                // Something curled inside, lit from within at the middle.
                                return c.softAttr(u, glow * (1.0f - smoothstep(0.1f, 0.8f, std::fabs(unit.z))));
                            });
            roughen(*c.fleshOut, v0, t0, 0.06f * R * c.f.roughness, 0.5f * R, u);
            // Roots wound around it from the base most of the way up.
            const int roots = 2 + static_cast<int>(rng.below(3));
            for (int r = 0; r < roots; ++r) {
                float a0 = rng.range(0.0f, 6.2831853f), turns = rng.range(0.4f, 0.9f);
                std::vector<Vec3> pts;
                std::vector<float> radii;
                std::vector<CorruptionAttributes> attrs;
                const float rr = R * rng.range(0.08f, 0.13f);
                for (int j = 0; j < 24; ++j) {
                    float t = static_cast<float>(j) / 23.0f;
                    float z = -0.95f + 1.6f * t;
                    float ring = std::sqrt(std::max(0.0f, 1.0f - z * z));
                    float a = a0 + 6.2831853f * turns * t;
                    pts.push_back(centre + axis * (z * L * 0.5f) + (ax * std::cos(a) + ay * std::sin(a)) * (R * ring + rr * 0.6f));
                    radii.push_back(j == 23 ? 0.0f : rr * (1.0f - 0.6f * t));
                    attrs.push_back(c.attr(u, 0.0f));
                }
                appendTube(out, pts, radii, attrs, 6, true, c.style(0.2f, rr * 3.0f, 0.0f, 1.0f, u * 7u + static_cast<uint32_t>(r)));
            }
            c.fuse(at, n, R * 1.9f, R * 0.5f);
            c.clearings.emplace_back(centre, R * 1.1f);
            any = true;
        }
        if (any) ++stats.pods;
    }
}

// Stacked bracket-fungus plates on walls, with glowing gills beneath.
void growShelves(Context& c, Rng& rng, Mesh& out, GrowthStats& stats) {
    const auto& S = c.surface;
    const float k = c.f.scale;
    const Vec3 up{0, 1, 0};
    auto sites = poissonThin(S.positions, c.candidates([&](uint32_t v) {
        return c.infection(v) > 0.7f && std::fabs(S.normals[v].y) < 0.35f;
    }), c.f.shelfSpacing * k);
    for (uint32_t v : sites) {
        if (rng.uniform() > 0.5f || c.cleared(S.positions[v])) continue;
        const int count = 3 + static_cast<int>(rng.below(4));
        Vec3 at = S.positions[v];
        float size = c.f.shelfSize * k * rng.range(0.8f, 1.3f);
        int grown = 0;
        for (int i = 0; i < count; ++i) {
            int32_t near = c.grid.nearest(at, 0.05f * k);
            if (near < 0 || !c.allowed(static_cast<uint32_t>(near))) break;
            const uint32_t u = static_cast<uint32_t>(near);
            const Vec3 n = S.normals[u];
            if (std::fabs(n.y) > 0.5f) break;
            Vec3 across = cross(up, n);
            if (length(across) < 1e-4f) break;
            across = normalize(across);
            const Vec3 vertical = normalize(cross(across, n));
            // Tilted up a little, half buried in the wall: a half-disc,
            // thick where it grows from the wall, thin at its wavy rim.
            const Vec3 face = normalize(n + vertical * 0.25f);
            const Vec3 centre = S.positions[u] + n * (size * 0.05f);
            const size_t v0 = out.vertexCount(), t0 = out.triangleCount();
            appendEllipsoid(out, centre, across, face, normalize(cross(across, face)), Vec3{size, size * 0.8f, size * 0.2f}, 10,
                            24, [&](const Vec3& unit) {
                                CorruptionAttributes a = c.attr(u, 0.0f);
                                if (unit.z < -0.2f) {
                                    // Gills radiating from the wall, faintly lit.
                                    float angle = std::atan2(unit.y, unit.x);
                                    a.glow = 0.2f * smoothstep(0.6f, 0.95f, 0.5f + 0.5f * std::sin(angle * 28.0f)) *
                                             smoothstep(-0.2f, -0.6f, unit.z);
                                } else {
                                    a.tone = 0.25f * smoothstep(0.5f, 1.0f, std::sqrt(unit.x * unit.x + unit.y * unit.y));
                                }
                                return a;
                            });
            // Thin toward the rim, with a wavy edge; the back pressed flat
            // just inside the wall rather than through it.
            for (size_t i = v0; i < out.vertexCount(); ++i) {
                float behind = dot(out.positions[i] - S.positions[u], n) + 0.02f * size;
                if (behind < 0.0f) out.positions[i] -= n * behind;
                Vec3 local = out.positions[i] - centre;
                float rim = clamp01(dot(local, face) / (size * 0.8f));
                Vec3 thick = normalize(cross(across, face));
                out.positions[i] -= thick * (dot(local, thick) * 0.6f * rim);
            }
            roughen(out, v0, t0, 0.09f * size * c.f.roughness, 0.3f * size, u);
            c.fuse(S.positions[u], n, size * 1.2f, size * 0.25f);
            c.perches.emplace_back(centre + face * (size * 0.6f), size * 0.1f);
            ++grown;
            // The next one above and to the side, smaller.
            // The next one overlapping beside and above it.
            at = S.positions[u] + up * (size * rng.range(0.3f, 0.6f)) + across * (size * rng.range(0.7f, 1.1f) * (rng.uniform() < 0.5f ? -1.0f : 1.0f));
            size *= rng.range(0.75f, 1.05f);
        }
        if (grown > 0) ++stats.shelves;
    }
}

// Swells the surface up around each growth's base, so that growths rise out
// of the tissue instead of sitting on it. Overlapping swellings merge by
// taking the higher, not the sum, so clusters do not tower.
void applyFusion(Mesh& S, const std::vector<Anchor>& anchors, const PointGrid& grid, const GrowthGuide* guide) {
    if (anchors.empty()) return;
    std::vector<float> lift(S.vertexCount(), 0.0f), weight(S.vertexCount(), 0.0f);
    for (const auto& a : anchors) {
        for (uint32_t v : grid.within(a.at, a.radius)) {
            if (dot(S.normals[v], a.normal) < 0.3f) continue;
            float d = length(S.positions[v] - a.at) / a.radius;
            float w = 1.0f - d * d;
            w *= w;
            if (a.hollow > 0.0f) w *= smoothstep(a.hollow * 0.7f, a.hollow, d);
            lift[v] = std::max(lift[v], a.height * w);
            weight[v] = std::max(weight[v], w);
        }
    }
    // A crater's floor stays open whatever swelled around it.
    for (const auto& a : anchors) {
        if (a.hollow <= 0.0f) continue;
        for (uint32_t v : grid.within(a.at, a.radius * a.hollow)) {
            float d = length(S.positions[v] - a.at) / a.radius;
            float keep = smoothstep(a.hollow * 0.7f, a.hollow, d);
            lift[v] *= keep;
            weight[v] *= keep;
        }
    }
    bool moved = false;
    for (uint32_t v = 0; v < S.vertexCount(); ++v) {
        if (guide && guide->blockAt(v) > 0.5f) continue;  // blocked tissue stays clean
        if (lift[v] <= 0.0f) continue;
        moved = true;
        S.positions[v] += S.normals[v] * lift[v];
        auto& at = S.attributes[v];
        at.height += lift[v];
        at.infection = std::max(at.infection, weight[v]);
        // The stretched flanks show the raw tissue under the carapace.
        at.flesh = std::max(at.flesh, 0.15f * 4.0f * weight[v] * (1.0f - weight[v]));
    }
    if (moved) S.computeNormals();
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
            const FeatureParams& features, const GrowthGuide* guide) {
    SurfaceGrowth base = growSurface(host, seeds, params, guide);
    Growth g;
    g.surface = std::move(base.surface);
    g.spread = std::move(base.spread);
    g.veins = std::move(base.veins);
    g.plates = std::move(base.plates);
    if (!features.enabled || g.spread.threshold <= 0.0f) return g;

    const Mesh& S = g.surface;
    Context c(S, g.spread, g.plates, params, features);
    c.fleshOut = &g.flesh;
    c.guide = guide;
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
    Rng tendrilRng(params.seed, 15), heartRng(params.seed, 16), sacRng(params.seed, 17), slimeRng(params.seed, 18);
    Rng spireRng(params.seed, 19), ribRng(params.seed, 20), podRng(params.seed, 21), shelfRng(params.seed, 22);
    if (features.hearts)
        for (uint32_t s : heartSeeds(c))
            c.clearings.emplace_back(S.positions[s], heartRadius(c, s) * (heartIsMaw(c) ? 1.5f : 0.9f));
    // Set pieces first, so the smaller growths keep out of their space.
    if (features.spires) growSpires(c, spireRng, g.growths, g.stats);
    if (features.pods) growPods(c, podRng, g.growths, g.stats);
    if (features.ribcages) growRibcages(c, ribRng, g.growths, g.stats);
    if (features.shelves) growShelves(c, shelfRng, g.growths, g.stats);
    if (features.veinTubes) growVeinTubes(c, g.veins, g.growths, g.stats);
    if (features.tendrils) growTendrils(c, tendrilRng, g.growths, g.stats);
    if (features.vertebrae) growVertebrae(c, g.veins, g.growths, g.stats);
    if (features.hearts) growHearts(c, heartRng, g.growths, g.stats);
    growSpikes(c, spikeRng, g.growths, g.stats);
    growNodules(c, noduleRng, g.stats);
    growStrands(c, strandRng, g.growths, g.stats);
    growBridges(c, bridgeRng, g.growths, g.stats);
    if (features.sacs) growSacs(c, sacRng, g.stats);
    if (features.slime) growSlime(c, slimeRng, g.stats);
    applyFusion(g.surface, c.anchors, c.grid, guide);
    return g;
}

}  // namespace eclipse
