// Self-contained test runner: no framework, so the suite builds anywhere the
// core does. Run with a substring to select tests: eclipse_tests spread

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <stdexcept>
#include <system_error>
#include <string>
#include <vector>

#include "eclipse/corruption.h"
#include "eclipse/export.h"
#include "eclipse/detail.h"
#include "eclipse/geometry.h"
#include "eclipse/growths.h"
#include "eclipse/hosts.h"
#include "eclipse/preview.h"
#include "eclipse/random.h"
#include "eclipse/sdf.h"
#include "eclipse/skin.h"
#include "eclipse/spatial.h"

using namespace eclipse;

namespace {

struct TestCase {
    const char* name;
    std::function<void()> body;
};
std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}
struct Register {
    Register(const char* name, std::function<void()> body) { registry().push_back({name, std::move(body)}); }
};
int failures = 0;

#define TEST(name)                                     \
    static void name();                                \
    static Register register_##name(#name, name);      \
    static void name()

#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (!(cond)) {                                                                \
            std::printf("    FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
            ++failures;                                                               \
        }                                                                             \
    } while (0)

std::vector<char> readAll(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<char>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// Never throws: a leftover temp file must not abort the whole run.
void removeFile(const std::string& path) {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

// Shared hosts are expensive enough to build once.
const BuiltinHost& mannequin() {
    static BuiltinHost h = makeBuiltinHost("mannequin", 0.012f);
    return h;
}

Mesh sphere(float r, float cell) {
    return polygonize([r](const Vec3& p) { return length(p) - r; }, Vec3{-r - 2 * cell, -r - 2 * cell, -r - 2 * cell},
                      Vec3{r + 2 * cell, r + 2 * cell, r + 2 * cell}, cell);
}

// In a closed, consistently wound mesh every edge is used by exactly two
// triangles, once in each direction. Returns the directed edges that are not.
size_t brokenEdges(const Mesh& m) {
    std::map<std::pair<uint32_t, uint32_t>, int> directed;
    for (const auto& t : m.triangles)
        for (int i = 0; i < 3; ++i) directed[{t[i], t[(i + 1) % 3]}]++;
    size_t broken = 0;
    for (const auto& [e, count] : directed) {
        auto rev = directed.find({e.second, e.first});
        if (count != 1 || rev == directed.end() || rev->second != 1) ++broken;
    }
    return broken;
}

}  // namespace

TEST(rng_is_deterministic_and_uniform) {
    Rng a(42), b(42), c(43);
    bool same = true, differs = false;
    double sum = 0;
    for (int i = 0; i < 10000; ++i) {
        uint32_t x = a.next();
        same = same && x == b.next();
        differs = differs || x != c.next();
        sum += Rng(i).uniform();
    }
    CHECK(same);
    CHECK(differs);
    CHECK(std::fabs(sum / 10000 - 0.5) < 0.02);
}

TEST(noise_is_bounded_and_seeded) {
    Noise n1(1), n2(1), n3(2);
    float lo = 1e9f, hi = -1e9f;
    bool differs = false;
    for (int i = 0; i < 5000; ++i) {
        Vec3 p{i * 0.137f, i * 0.071f, i * 0.293f};
        float v = n1.fbm(p, 4);
        lo = std::min(lo, v);
        hi = std::max(hi, v);
        CHECK(v == n2.fbm(p, 4));
        differs = differs || v != n3.fbm(p, 4);
    }
    CHECK(lo > -1.5f && hi < 1.5f);
    CHECK(hi - lo > 0.5f);
    CHECK(differs);
}

TEST(round_cone_distance_matches_sphere_ends) {
    Vec3 a{0, 0, 0}, b{0, 1, 0};
    CHECK(std::fabs(sdRoundCone({0, -0.5f, 0}, a, b, 0.2f, 0.1f) - 0.3f) < 1e-4f);
    CHECK(std::fabs(sdRoundCone({0, 1.5f, 0}, a, b, 0.2f, 0.1f) - 0.4f) < 1e-4f);
    CHECK(sdRoundCone({0, 0.5f, 0}, a, b, 0.2f, 0.1f) < 0.0f);
}

TEST(polygonized_sphere_is_closed_and_round) {
    Mesh m = sphere(0.5f, 0.05f);
    CHECK(m.vertexCount() > 300);
    CHECK(brokenEdges(m) == 0);
    float worst = 0;
    for (size_t i = 0; i < m.vertexCount(); ++i) {
        worst = std::max(worst, std::fabs(length(m.positions[i]) - 0.5f));
        // Normals point outward and agree with the winding.
        CHECK(dot(m.normals[i], m.positions[i]) > 0);
    }
    CHECK(worst < 0.05f * 0.5f);
    Mesh wound = m;
    wound.computeNormals();
    int agree = 0;
    for (size_t i = 0; i < m.vertexCount(); ++i) agree += dot(wound.normals[i], m.normals[i]) > 0.5f;
    CHECK(agree == static_cast<int>(m.vertexCount()));
}

TEST(mannequin_is_one_closed_body) {
    const Mesh& m = mannequin().mesh;
    // Surface nets can pinch where a feature is thinner than a cell (finger
    // tips); allow a handful of such edges, never a systematic tear.
    CHECK(brokenEdges(m) <= m.triangleCount() * 3 / 2000);
    Vec3 lo, hi;
    m.bounds(lo, hi);
    CHECK(hi.y - lo.y > 1.7f && hi.y - lo.y < 1.9f);  // height
    CHECK(hi.x - lo.x > 1.7f && hi.x - lo.x < 2.0f);  // arm span
    // Connected: a plain flood fill from vertex 0 reaches everything.
    VertexGraph g = VertexGraph::build(m);
    std::vector<uint8_t> seen(m.vertexCount(), 0);
    std::vector<uint32_t> stack = {0};
    seen[0] = 1;
    size_t reached = 0;
    while (!stack.empty()) {
        uint32_t v = stack.back();
        stack.pop_back();
        ++reached;
        for (uint32_t i = g.offsets[v]; i < g.offsets[v + 1]; ++i)
            if (!seen[g.neighbours[i]]) {
                seen[g.neighbours[i]] = 1;
                stack.push_back(g.neighbours[i]);
            }
    }
    CHECK(reached == m.vertexCount());
}

TEST(curvature_is_positive_on_convex_shapes) {
    Mesh m = sphere(0.3f, 0.02f);
    VertexGraph g = VertexGraph::build(m);
    auto c = vertexCurvature(m, g);
    double mean = 0;
    for (float v : c) mean += v;
    mean /= c.size();
    // Mean curvature of a sphere is 2/r.
    CHECK(mean > 0.5 * 2 / 0.3 && mean < 2.0 * 2 / 0.3);
}

TEST(spread_matches_requested_coverage) {
    const Mesh& m = mannequin().mesh;
    VertexGraph g = VertexGraph::build(m);
    auto area = vertexAreas(m);
    double total = 0;
    for (float a : area) total += a;
    for (float coverage : {0.1f, 0.4f, 0.8f}) {
        GrowthParams p;
        p.coverage = coverage;
        SpreadField f = computeSpread(m, g, mannequin().defaultSeeds, p);
        double touched = 0;
        for (size_t v = 0; v < m.vertexCount(); ++v)
            if (f.distance[v] <= f.threshold) touched += area[v];
        CHECK(std::fabs(touched / total - coverage) < 0.02);
        for (uint32_t s : f.seedVertices) CHECK(f.infection[s] == 1.0f);
    }
}

TEST(spread_is_nested_across_coverage) {
    // A higher coverage from the same seed contains the lower one, so stages
    // can be played back as a growth animation.
    const Mesh& m = mannequin().mesh;
    VertexGraph g = VertexGraph::build(m);
    GrowthParams lowP, highP;
    lowP.coverage = 0.25f;
    highP.coverage = 0.6f;
    SpreadField low = computeSpread(m, g, mannequin().defaultSeeds, lowP);
    SpreadField high = computeSpread(m, g, mannequin().defaultSeeds, highP);
    bool nested = true;
    for (size_t v = 0; v < m.vertexCount(); ++v) nested = nested && high.infection[v] >= low.infection[v];
    CHECK(nested);
}

TEST(spread_tree_leads_back_to_a_seed) {
    const Mesh& m = mannequin().mesh;
    VertexGraph g = VertexGraph::build(m);
    GrowthParams p;
    SpreadField f = computeSpread(m, g, mannequin().defaultSeeds, p);
    bool ok = true;
    for (size_t v = 0; v < m.vertexCount() && ok; v += 37) {
        int32_t cur = static_cast<int32_t>(v);
        size_t steps = 0;
        while (f.parent[cur] >= 0 && steps <= m.vertexCount()) {
            ok = ok && f.distance[f.parent[cur]] <= f.distance[cur];
            cur = f.parent[cur];
            ++steps;
        }
        bool isSeed = false;
        for (uint32_t s : f.seedVertices) isSeed = isSeed || static_cast<uint32_t>(cur) == s;
        ok = ok && isSeed;
    }
    CHECK(ok);
}

TEST(gravity_biases_the_spread) {
    const Mesh& m = mannequin().mesh;
    VertexGraph g = VertexGraph::build(m);
    auto meanHeight = [&](float gravity) {
        GrowthParams p;
        p.coverage = 0.3f;
        p.gravity = gravity;
        SpreadField f = computeSpread(m, g, {{0, 1.1f, 0.13f}}, p);
        double sum = 0, w = 0;
        for (size_t v = 0; v < m.vertexCount(); ++v) {
            sum += f.infection[v] * m.positions[v].y;
            w += f.infection[v];
        }
        return sum / w;
    };
    CHECK(meanHeight(0.7f) > meanHeight(-0.7f) + 0.05);
}

TEST(veins_branch_from_seeds_and_thicken_toward_them) {
    const Mesh& m = mannequin().mesh;
    VertexGraph g = VertexGraph::build(m);
    GrowthParams p;
    SpreadField f = computeSpread(m, g, mannequin().defaultSeeds, p);
    VeinNetwork veins = computeVeins(m, f, p);
    CHECK(veins.veinVertexCount > 50);
    // Flow is conserved downstream: a parent drains at least what a child does.
    bool monotone = true;
    for (size_t v = 0; v < m.vertexCount(); ++v)
        if (f.parent[v] >= 0 && veins.flow[v] > 0) monotone = monotone && veins.flow[f.parent[v]] >= veins.flow[v];
    CHECK(monotone);
    // Veins reach a little past the plated region but not across the body.
    size_t ahead = 0, farAway = 0;
    for (size_t v = 0; v < m.vertexCount(); ++v) {
        if (veins.mask[v] <= 0.5f) continue;
        if (f.infection[v] == 0.0f) ++ahead;
        if (f.distance[v] > f.threshold * (1.0f + p.veinReach) * 1.2f) ++farAway;
    }
    CHECK(ahead > 0);
    CHECK(farAway == 0);
    for (float m2 : veins.mask) CHECK(m2 >= 0.0f && m2 <= 1.0f);
}

TEST(plates_tile_the_established_region) {
    const Mesh& m = mannequin().mesh;
    VertexGraph g = VertexGraph::build(m);
    GrowthParams p;
    p.coverage = 0.5f;
    SpreadField f = computeSpread(m, g, mannequin().defaultSeeds, p);
    PlateLayout plates = computePlates(m, g, f, p);
    CHECK(plates.plateCount > 30);
    size_t unassigned = 0, seams = 0;
    for (size_t v = 0; v < m.vertexCount(); ++v) {
        if (f.infection[v] > 0.9f && plates.plate[v] < 0) ++unassigned;
        if (f.infection[v] == 0.0f) CHECK(plates.plate[v] < 0);
        if (plates.plate[v] >= 0 && plates.seamDistance[v] == 0.0f) ++seams;
    }
    CHECK(unassigned == 0);
    CHECK(seams > 100);
}

TEST(corrupt_is_deterministic_per_seed) {
    const Mesh& m = mannequin().mesh;
    GrowthParams p;
    uint64_t a = meshHash(corrupt(m, mannequin().defaultSeeds, p));
    uint64_t b = meshHash(corrupt(m, mannequin().defaultSeeds, p));
    p.seed = 2;
    uint64_t c = meshHash(corrupt(m, mannequin().defaultSeeds, p));
    CHECK(a == b);
    CHECK(a != c);
}

TEST(corrupt_only_moves_infected_tissue_outward) {
    const Mesh& m = mannequin().mesh;
    GrowthParams p;
    Mesh out = corrupt(m, mannequin().defaultSeeds, p);
    CHECK(out.vertexCount() == m.vertexCount());
    CHECK(out.triangles == m.triangles);
    CHECK(out.attributes.size() == m.vertexCount());
    bool cleanStill = true, bounded = true;
    float maxLift = 0;
    for (size_t v = 0; v < m.vertexCount(); ++v) {
        float lift = dot(out.positions[v] - m.positions[v], m.normals[v]);
        if (out.attributes[v].infection == 0.0f && out.attributes[v].glow == 0.0f)
            cleanStill = cleanStill && length(out.positions[v] - m.positions[v]) < 1e-6f;
        bounded = bounded && lift > -p.roughness * 2 && lift < p.swell + p.roughness * 2 + p.plateThickness + p.veinHeight;
        maxLift = std::max(maxLift, lift);
    }
    CHECK(cleanStill);
    CHECK(bounded);
    CHECK(maxLift > p.plateThickness * 0.5f);
}

TEST(zero_coverage_leaves_host_untouched) {
    const Mesh& m = mannequin().mesh;
    GrowthParams p;
    p.coverage = 0.0f;
    Mesh out = corrupt(m, mannequin().defaultSeeds, p);
    bool same = true;
    for (size_t v = 0; v < m.vertexCount(); ++v) {
        same = same && out.positions[v].x == m.positions[v].x && out.positions[v].y == m.positions[v].y &&
               out.positions[v].z == m.positions[v].z;
        same = same && out.attributes[v].infection == 0.0f && out.attributes[v].glow == 0.0f;
    }
    CHECK(same);
}

TEST(glb_is_well_formed) {
    const Mesh& m = mannequin().mesh;
    Mesh out = corrupt(m, mannequin().defaultSeeds, GrowthParams{});
    auto path = (std::filesystem::temp_directory_path() / "eclipse_test.glb").string();
    CHECK(writeGlb(path, out));
    std::vector<char> bytes = readAll(path);
    auto u32 = [&](size_t o) {
        uint32_t v;
        std::memcpy(&v, bytes.data() + o, 4);
        return v;
    };
    CHECK(bytes.size() > 28);
    CHECK(u32(0) == 0x46546C67u);
    CHECK(u32(4) == 2);
    CHECK(u32(8) == bytes.size());
    uint32_t jsonLen = u32(12);
    CHECK(jsonLen % 4 == 0);
    CHECK(u32(16) == 0x4E4F534Au);
    std::string json(bytes.data() + 20, jsonLen);
    CHECK(json.find("\"COLOR_0\"") != std::string::npos);
    CHECK(json.find("\"TEXCOORD_0\"") != std::string::npos);
    CHECK(json.find("\"TEXCOORD_1\"") != std::string::npos);
    uint32_t binLen = u32(20 + jsonLen);
    CHECK(u32(24 + jsonLen) == 0x004E4942u);
    CHECK(28 + jsonLen + binLen == bytes.size());
    size_t expected = out.vertexCount() * (12 + 12 + 16 + 8 + 8) + out.triangleCount() * 12;
    CHECK(binLen >= expected && binLen < expected + 32);
    removeFile(path);
}

TEST(obj_round_trips) {
    Mesh m = sphere(0.2f, 0.04f);
    auto path = (std::filesystem::temp_directory_path() / "eclipse_test.obj").string();
    CHECK(writeObj(path, m));
    Mesh back;
    CHECK(readObj(path, back));
    CHECK(back.vertexCount() == m.vertexCount());
    CHECK(back.triangles == m.triangles);
    removeFile(path);
}

TEST(preview_renders_the_glow) {
    const Mesh& m = mannequin().mesh;
    Mesh out = corrupt(m, mannequin().defaultSeeds, GrowthParams{});
    PreviewOptions o;
    o.width = 160;
    o.height = 180;
    o.supersample = 1;
    o.yaws = {0.0f};
    Image im = renderPreview(out, o);
    CHECK(im.width == 160 && im.height == 180);
    size_t violet = 0, dark = 0;
    for (size_t i = 0; i < im.rgb.size(); i += 3) {
        int r = im.rgb[i], g = im.rgb[i + 1], b = im.rgb[i + 2];
        if (b > 150 && b > g + 60 && r > g) ++violet;
        if (std::max({r, g, b}) < 80) ++dark;  // carapace, after tone mapping
    }
    CHECK(violet > 20);
    CHECK(dark > 200);
    auto path = (std::filesystem::temp_directory_path() / "eclipse_test.png").string();
    CHECK(writePng(path, im));
    CHECK(std::filesystem::file_size(path) > static_cast<uintmax_t>(160 * 180 * 3));
    removeFile(path);
}

// Signed volume; positive when a closed mesh's faces point outward.
double signedVolume(const Mesh& m) {
    double v = 0;
    for (const auto& t : m.triangles)
        v += dot(m.positions[t[0]], cross(m.positions[t[1]], m.positions[t[2]])) / 6.0;
    return v;
}

TEST(point_grid_matches_brute_force) {
    Rng rng(7);
    std::vector<Vec3> pts;
    for (int i = 0; i < 2000; ++i) pts.push_back({rng.range(-1, 1), rng.range(-1, 1), rng.range(-1, 1)});
    PointGrid grid(pts, 0.1f);
    for (int q = 0; q < 50; ++q) {
        Vec3 p{rng.range(-1, 1), rng.range(-1, 1), rng.range(-1, 1)};
        std::vector<uint32_t> brute;
        int32_t nearest = -1;
        float best = 0.3f * 0.3f;
        for (uint32_t i = 0; i < pts.size(); ++i) {
            float d = dot(pts[i] - p, pts[i] - p);
            if (d <= 0.25f * 0.25f) brute.push_back(i);
            if (d < best) best = d, nearest = static_cast<int32_t>(i);
        }
        CHECK(grid.within(p, 0.25f) == brute);
        CHECK(grid.nearest(p, 0.3f) == nearest);
    }
}

TEST(poisson_thinning_keeps_its_spacing) {
    Rng rng(9);
    std::vector<Vec3> pts;
    std::vector<uint32_t> order;
    for (uint32_t i = 0; i < 3000; ++i) {
        pts.push_back({rng.range(0, 1), rng.range(0, 1), 0});
        order.push_back(i);
    }
    auto kept = poissonThin(pts, order, 0.05f);
    CHECK(kept.size() > 150 && kept.size() < 500);
    bool spaced = true;
    for (size_t i = 0; i < kept.size(); ++i)
        for (size_t j = i + 1; j < kept.size(); ++j) spaced = spaced && length(pts[kept[i]] - pts[kept[j]]) >= 0.05f;
    CHECK(spaced);
}

TEST(tubes_are_closed_and_face_outward) {
    for (bool tip : {false, true}) {
        Mesh m;
        std::vector<Vec3> pts;
        std::vector<float> radii;
        for (int i = 0; i < 8; ++i) {
            pts.push_back({0.1f * std::sin(i * 0.4f), 0.05f * i, 0});
            radii.push_back(tip && i == 7 ? 0.0f : 0.02f);
        }
        appendTube(m, pts, radii, std::vector<CorruptionAttributes>(pts.size()), 7);
        CHECK(brokenEdges(m) == 0);
        CHECK(signedVolume(m) > 0);
        CHECK(m.attributes.size() == m.vertexCount() && m.normals.size() == m.vertexCount());
    }
}

TEST(styled_tubes_stay_closed_and_vary_their_radius) {
    // Knots and growth rings reshape a tube (resampling it finely enough to
    // show them) without opening it, flipping it or blunting a sharp tip.
    std::vector<Vec3> pts;
    std::vector<float> radii;
    for (int i = 0; i < 6; ++i) {
        pts.push_back({0.05f * std::sin(i * 0.5f), 0.06f * i, 0});
        radii.push_back(i == 5 ? 0.0f : 0.02f * (1.0f - 0.18f * i));
    }
    Mesh plain, styled;
    appendTube(plain, pts, radii, std::vector<CorruptionAttributes>(pts.size()), 10);
    TubeStyle style;
    style.lumps = 0.3f;
    style.lumpSize = 0.02f;
    style.rings = 0.1f;
    style.ringSpacing = 0.015f;
    appendTube(styled, pts, radii, std::vector<CorruptionAttributes>(pts.size()), 10, true, style);
    CHECK(styled.vertexCount() > 3 * plain.vertexCount());  // resampled along its length
    CHECK(brokenEdges(styled) == 0);
    CHECK(signedVolume(styled) > 0);
    // The tip is still a point, at the same place.
    float tipGap = 1e9f;
    for (const Vec3& q : styled.positions) tipGap = std::min(tipGap, length(q - pts.back()));
    CHECK(tipGap < 1e-5f);
    // The surface is no longer a constant distance from its axis.
    float lo = 1e9f, hi = 0.0f;
    for (const Vec3& q : styled.positions)
        if (std::fabs(q.y - 0.06f) < 0.004f) {  // a slice near the base, where the radius is ~2 cm
            float r = std::sqrt((q.x - 0.05f * std::sin(0.5f)) * (q.x - 0.05f * std::sin(0.5f)) + q.z * q.z);
            lo = std::min(lo, r);
            hi = std::max(hi, r);
        }
    CHECK(hi - lo > 0.002f);
    bool unit = true;
    for (const Vec3& n : styled.normals) unit = unit && std::fabs(length(n) - 1.0f) < 1e-3f;
    CHECK(unit);
}

TEST(ellipsoids_are_closed_with_the_right_volume) {
    Mesh m;
    appendEllipsoid(m, {1, 2, 3}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0.3f, 0.2f, 0.1f}, 24, 48,
                    [](const Vec3&) { return CorruptionAttributes{}; });
    CHECK(brokenEdges(m) == 0);
    // Volume is translation-invariant for a closed mesh.
    double expected = 4.0 / 3.0 * 3.14159265 * 0.3 * 0.2 * 0.1;
    CHECK(std::fabs(signedVolume(m) - expected) < expected * 0.05);
}

const Growth& grownMannequin() {
    static Growth g = [] {
        GrowthParams p;
        p.coverage = 0.85f;
        return grow(mannequin().mesh, mannequin().defaultSeeds, p, FeatureParams{});
    }();
    return g;
}

TEST(growths_sprout_every_feature) {
    const Growth& g = grownMannequin();
    CHECK(g.stats.veinTubes > 20);
    CHECK(g.stats.spikes > 20);
    CHECK(g.stats.nodules > 5);
    CHECK(g.stats.strands > 20);
    CHECK(g.stats.bridges > 5);
    // Membranes need two facing surfaces and a clear gap between them, so on
    // the slim mannequin they appear for some seeds and not others.
    size_t membranes = g.stats.membranes;
    for (uint32_t seed = 2; seed <= 4 && membranes == 0; ++seed) {
        GrowthParams p;
        p.coverage = 0.85f;
        p.seed = seed;
        membranes += grow(mannequin().mesh, mannequin().defaultSeeds, p, FeatureParams{}).stats.membranes;
    }
    CHECK(membranes > 0);
    CHECK(g.stats.tendrils > 20);
    CHECK(g.stats.rootlets > 20);
    CHECK(g.stats.barbs > 0);
    CHECK(g.stats.hearts == mannequin().defaultSeeds.size());
    CHECK(g.stats.vertebrae > 0);
    CHECK(g.growths.attributes.size() == g.growths.vertexCount());
    CHECK(g.growths.normals.size() == g.growths.vertexCount());
    bool valid = true;
    for (const auto& t : g.growths.triangles)
        for (uint32_t i : t) valid = valid && i < g.growths.vertexCount();
    CHECK(valid);
    bool attrs = true;
    for (const auto& a : g.growths.attributes)
        attrs = attrs && a.infection == 1.0f && a.glow >= 0.0f && a.glow <= 1.0f && a.arrival >= 0.0f;
    CHECK(attrs);
}

TEST(growths_stay_near_the_corruption) {
    // Every growth vertex sits close to infected tissue: nothing sprouts from
    // clean skin or floats away. The reach allows for the longest horns.
    const Growth& g = grownMannequin();
    std::vector<Vec3> infected;
    for (size_t v = 0; v < g.surface.vertexCount(); ++v)
        if (g.surface.attributes[v].infection > 0.0f || g.surface.attributes[v].glow > 0.0f)
            infected.push_back(g.surface.positions[v]);
    PointGrid grid(infected, 0.05f);
    size_t stray = 0;
    for (const Mesh* m : {&g.growths, &g.flesh})
        for (size_t i = 0; i < m->vertexCount(); i += 7)
            if (grid.nearest(m->positions[i], FeatureParams{}.spikeLength * 7.0f) < 0) ++stray;
    CHECK(stray == 0);
}

TEST(detail_maps_tile_seamlessly) {
    for (DetailKind kind : {DetailKind::Chitin, DetailKind::Flesh, DetailKind::Capillaries}) {
        const int n = 128;
        DetailMap m = makeDetailMap(kind, n);
        CHECK(m.size == n);
        CHECK(m.rgba.size() == static_cast<size_t>(n) * n * 4);
        CHECK(m.rgba == makeDetailMap(kind, n).rgba);
        CHECK(m.rgba != makeDetailMap(kind, n, 2).rgba);
        auto at = [&](int x, int y, int c) { return static_cast<float>(m.rgba[(static_cast<size_t>(y) * n + x) * 4 + c]); };
        // Across the wrap the height steps no more than across some column
        // inside. (Not the average: cell edges gather on the columns at whole
        // cell widths, and the wrap is one of them.)
        for (int axis = 0; axis < 2; ++axis) {
            auto h = [&](int along, int across) { return axis == 0 ? at(along, across, 2) : at(across, along, 2); };
            float seam = 0.0f, steepest = 0.0f;
            for (int i = 0; i < n; ++i) seam += std::fabs(h(n - 1, i) - h(0, i));
            for (int k = 0; k + 1 < n; ++k) {
                float step = 0.0f;
                for (int i = 0; i < n; ++i) step += std::fabs(h(k, i) - h(k + 1, i));
                steepest = std::max(steepest, step);
            }
            CHECK(seam <= steepest);
        }
        // Slopes average out to flat, and the map is not flat.
        double slope = 0.0, spread = 0.0;
        for (int i = 0; i < n * n; ++i) {
            slope += m.rgba[static_cast<size_t>(i) * 4];
            spread += std::fabs(m.rgba[static_cast<size_t>(i) * 4 + 2] - 128.0);
        }
        CHECK(std::fabs(slope / (n * n) - 127.5) < 6.0);
        CHECK(spread / (n * n) > 10.0);
    }
}

TEST(colour_varies_with_bruises_and_bone_tips) {
    const Growth& g = grownMannequin();
    size_t bruised = 0, infected = 0;
    bool clean = true;
    for (const auto& a : g.surface.attributes) {
        if (a.infection > 0.9f) ++infected;
        if (a.tone < -0.05f) ++bruised;
        clean = clean && (a.infection > 0.0f || a.tone == 0.0f);  // clean tissue is never bruised
    }
    CHECK(clean);
    CHECK(bruised > infected / 20 && bruised < infected * 3 / 4);  // patches, not everywhere
    // Horns and claws pale toward their tips; nothing in the hard growths is bruised.
    size_t pale = 0;
    bool range = true;
    for (const auto& a : g.growths.attributes) {
        if (a.tone > 0.5f) ++pale;
        range = range && a.tone >= 0.0f && a.tone <= 1.0f;
    }
    CHECK(pale > 100);
    CHECK(range);
}

TEST(shell_carries_the_halo_past_the_front) {
    GrowthParams p;
    p.coverage = 0.4f;
    FeatureParams none;
    none.enabled = false;
    Growth g = grow(mannequin().mesh, mannequin().defaultSeeds, p, none);
    Mesh tight = extractShell(g.surface, 0.0015f);
    std::vector<uint32_t> sources;
    Mesh haloed = extractShell(g.surface, 0.0015f, &sources, p.haloReach);
    CHECK(haloed.vertexCount() > tight.vertexCount());
    CHECK(sources.size() == haloed.vertexCount());
    // Every triangle touches the corruption or tissue within the halo's reach.
    bool within = true;
    for (const auto& t : haloed.triangles) {
        bool touches = false;
        for (uint32_t i : t) {
            const auto& a = haloed.attributes[i];
            touches = touches || a.infection > 0.0f || a.glow > 0.0f || a.arrival < 1.0f + p.haloReach;
        }
        within = within && touches;
    }
    CHECK(within);
}

TEST(slime_hangs_wet_and_clear_of_the_host) {
    const Growth& g = grownMannequin();
    CHECK(g.stats.slime > 10);
    FeatureParams dry;
    dry.slime = false;
    GrowthParams p;
    p.coverage = 0.85f;
    Growth without = grow(mannequin().mesh, mannequin().defaultSeeds, p, dry);
    // Slime lives in the soft-tissue mesh and changes nothing else.
    CHECK(g.flesh.vertexCount() > without.flesh.vertexCount());
    CHECK(meshHash(g.growths) == meshHash(without.growths));
}

TEST(soft_tissue_is_its_own_mesh) {
    const Growth& g = grownMannequin();
    CHECK(g.stats.sacs > 0);
    CHECK(g.flesh.triangleCount() > 0);
    CHECK(g.flesh.attributes.size() == g.flesh.vertexCount());
    CHECK(g.flesh.normals.size() == g.flesh.vertexCount());
    bool soft = true, hard = true, valid = true;
    for (const auto& a : g.flesh.attributes) soft = soft && a.flesh == 1.0f && a.infection == 1.0f;
    for (const auto& a : g.growths.attributes) hard = hard && a.flesh == 0.0f;
    for (const auto& t : g.flesh.triangles)
        for (uint32_t i : t) valid = valid && i < g.flesh.vertexCount();
    CHECK(soft);
    CHECK(hard);
    CHECK(valid);
}

TEST(tissue_swells_around_growths) {
    GrowthParams p;
    p.coverage = 0.6f;
    FeatureParams flat;
    flat.fusion = 0.0f;
    Growth with = grow(mannequin().mesh, mannequin().defaultSeeds, p, FeatureParams{});
    Growth without = grow(mannequin().mesh, mannequin().defaultSeeds, p, flat);
    // The growths themselves are unchanged; only the surface under them rises.
    CHECK(meshHash(with.growths) == meshHash(without.growths));
    size_t raised = 0;
    bool outward = true, bounded = true, infected = true;
    for (size_t v = 0; v < with.surface.vertexCount(); ++v) {
        Vec3 d = with.surface.positions[v] - without.surface.positions[v];
        float lift = dot(d, without.surface.normals[v]);
        if (length(d) < 1e-6f) continue;
        ++raised;
        outward = outward && lift > 0.0f;
        bounded = bounded && length(d) < 0.03f;
        infected = infected && with.surface.attributes[v].infection > 0.0f;
    }
    CHECK(raised > 1000);
    CHECK(outward);
    CHECK(bounded);
    CHECK(infected);
}

TEST(a_maw_keeps_its_throat_open) {
    const Growth& g = grownMannequin();  // coverage past mawCoverage
    CHECK(g.stats.maws == g.stats.hearts);
    // Nothing hard grows down the middle of the throat, and the floor under
    // it is not swollen shut.
    GrowthParams p;
    p.coverage = 0.85f;
    FeatureParams flat;
    flat.fusion = 0.0f;
    Growth without = grow(mannequin().mesh, mannequin().defaultSeeds, p, flat);
    const float R = FeatureParams{}.heartRadius * 0.6f;  // the smallest a heart can be
    bool clear = true, open = true;
    for (uint32_t s : g.spread.seedVertices) {
        const Vec3 at = g.surface.positions[s];
        for (const Vec3& q : g.growths.positions) clear = clear && length(q - at) > 0.3f * R;
        open = open && length(at - without.surface.positions[s]) < 1e-4f;
    }
    CHECK(clear);
    CHECK(open);
}

TEST(growths_are_deterministic_and_seeded) {
    GrowthParams p;
    p.coverage = 0.5f;
    uint64_t a = meshHash(grow(mannequin().mesh, mannequin().defaultSeeds, p, FeatureParams{}).growths);
    uint64_t b = meshHash(grow(mannequin().mesh, mannequin().defaultSeeds, p, FeatureParams{}).growths);
    p.seed = 5;
    uint64_t c = meshHash(grow(mannequin().mesh, mannequin().defaultSeeds, p, FeatureParams{}).growths);
    CHECK(a == b);
    CHECK(a != c);
}

TEST(no_growths_without_corruption_or_when_disabled) {
    GrowthParams p;
    p.coverage = 0.0f;
    CHECK(grow(mannequin().mesh, mannequin().defaultSeeds, p, FeatureParams{}).growths.vertexCount() == 0);
    p.coverage = 0.6f;
    FeatureParams off;
    off.enabled = false;
    Growth g = grow(mannequin().mesh, mannequin().defaultSeeds, p, off);
    CHECK(g.growths.vertexCount() == 0);
    CHECK(meshHash(g.surface) == meshHash(corrupt(mannequin().mesh, mannequin().defaultSeeds, p)));
}

TEST(glb_holds_surface_and_growths_as_separate_meshes) {
    const Growth& g = grownMannequin();
    auto path = (std::filesystem::temp_directory_path() / "eclipse_test_multi.glb").string();
    CHECK(writeGlb(path, {{"EclipseSurface", &g.surface}, {"EclipseGrowths", &g.growths}}));
    std::vector<char> bytes = readAll(path);
    uint32_t jsonLen;
    std::memcpy(&jsonLen, bytes.data() + 12, 4);
    std::string json(bytes.data() + 20, jsonLen);
    CHECK(json.find("\"name\":\"EclipseSurface\",\"primitives\"") != std::string::npos);
    CHECK(json.find("\"name\":\"EclipseGrowths\",\"primitives\"") != std::string::npos);
    CHECK(json.find("\"nodes\":[0,1]") != std::string::npos);
    removeFile(path);
    Mesh empty;
    CHECK(!writeGlb(path, {{"Nothing", &empty}}));
}

// A unit cube with every face's vertices split apart, as game exporters
// often deliver them (separate vertices per face for hard normals).
Mesh splitCube() {
    Mesh m;
    const Vec3 c[8] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}};
    const int faces[6][4] = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4}, {2, 3, 7, 6}, {1, 2, 6, 5}, {0, 4, 7, 3}};
    for (const auto& f : faces) {
        uint32_t base = static_cast<uint32_t>(m.positions.size());
        for (int i : f) m.positions.push_back(c[i]);
        m.triangles.push_back({base, base + 1, base + 2});
        m.triangles.push_back({base, base + 2, base + 3});
    }
    return m;
}

double totalArea(const Mesh& m) {
    double a = 0;
    for (float v : vertexAreas(m)) a += v;
    return a;
}

TEST(welding_joins_split_seams) {
    Mesh m = splitCube();
    CHECK(m.vertexCount() == 24);
    CHECK(brokenEdges(m) > 0);
    weldVertices(m, 1e-4f);
    CHECK(m.vertexCount() == 8);
    CHECK(m.triangleCount() == 12);
    CHECK(brokenEdges(m) == 0);
    CHECK(signedVolume(m) > 0.99 && signedVolume(m) < 1.01);
}

TEST(subdivision_refines_without_changing_shape) {
    Mesh m = splitCube();
    weldVertices(m, 1e-4f);
    CHECK(subdivideToEdgeLength(m, 0.1f, 1000000));
    CHECK(m.vertexCount() > 500);
    CHECK(brokenEdges(m) == 0);  // no cracks or T-junctions
    float longest = 0;
    for (const auto& t : m.triangles)
        for (int i = 0; i < 3; ++i) longest = std::max(longest, length(m.positions[t[i]] - m.positions[t[(i + 1) % 3]]));
    CHECK(longest <= 0.1f);
    CHECK(std::fabs(totalArea(m) - 6.0) < 1e-3);
    CHECK(std::fabs(signedVolume(m) - 1.0) < 1e-3);
    bool onSurface = true;
    for (const auto& p : m.positions) {
        float edge = std::min({std::fabs(p.x), std::fabs(p.x - 1), std::fabs(p.y), std::fabs(p.y - 1), std::fabs(p.z),
                               std::fabs(p.z - 1)});
        onSurface = onSurface && edge < 1e-5f;
    }
    CHECK(onSurface);
    CHECK(m.normals.size() == m.vertexCount());
}

TEST(subdivision_refuses_an_oversized_result) {
    Mesh m = splitCube();
    weldVertices(m, 1e-4f);
    Mesh before = m;
    CHECK(!subdivideToEdgeLength(m, 0.001f, 100000));
    CHECK(m.positions.size() == before.positions.size() && m.triangles == before.triangles);
}

TEST(subdivision_accepts_long_thin_surfaces_within_the_actual_cap) {
    // Ornament and wall trim are long, narrow surfaces, not square patches.
    // The old per-triangle longest-edge-squared estimate rejects this strip
    // at 1024 vertices even though adaptive splitting needs far fewer.
    Mesh m = splitCube();
    weldVertices(m, 1e-4f);
    for (auto& p : m.positions) {
        p.x *= 32.0f;
        p.y *= 0.03125f;
        p.z *= 0.03125f;
    }
    const size_t originalVertices = m.vertexCount();
    const double area = totalArea(m), volume = signedVolume(m);
    SkinTable skin;
    for (const auto& p : m.positions) skin.push_back(SkinWeights::single(p.x > 16.0f ? 1 : 0));
    std::vector<std::array<uint32_t, 2>> parents;
    const bool refined = subdivideToEdgeLength(m, 1.001f, 1024, &parents);
    CHECK(refined);
    if (!refined) return;
    CHECK(m.vertexCount() > originalVertices && m.vertexCount() <= 1024);
    CHECK(brokenEdges(m) == 0);  // shared edges split once, with no T-junctions
    CHECK(std::fabs(totalArea(m) - area) < 1e-4);
    CHECK(std::fabs(signedVolume(m) - volume) < 1e-5);
    bool shortEdges = true, onSurface = true, orderedMidpoints = true;
    for (const auto& t : m.triangles)
        for (int i = 0; i < 3; ++i)
            shortEdges = shortEdges && length(m.positions[t[i]] - m.positions[t[(i + 1) % 3]]) <= 1.001f;
    for (const auto& p : m.positions) {
        const float edge = std::min({std::fabs(p.x), std::fabs(p.x - 32.0f), std::fabs(p.y),
                                     std::fabs(p.y - 0.03125f), std::fabs(p.z), std::fabs(p.z - 0.03125f)});
        onSurface = onSurface && edge < 1e-6f;
    }
    CHECK(parents.size() == m.vertexCount() - originalVertices);
    for (size_t i = 0; i < parents.size(); ++i) {
        const uint32_t a = parents[i][0], b = parents[i][1];
        const size_t child = originalVertices + i;
        orderedMidpoints = orderedMidpoints && a < child && b < child;
        if (a >= child || b >= child) continue;
        const Vec3 midpoint = (m.positions[a] + m.positions[b]) * 0.5f;
        orderedMidpoints = orderedMidpoints && length(m.positions[child] - midpoint) < 1e-6f;
    }
    CHECK(shortEdges && onSurface && orderedMidpoints);
    skin = skinAfterSubdivision(skin, parents);
    CHECK(skin.size() == m.vertexCount());
    bool linearWeights = true;
    for (size_t v = 0; v < m.vertexCount(); ++v) {
        float top = 0;
        for (int k = 0; k < skin[v].count; ++k)
            if (skin[v].bone[k] == 1) top = skin[v].weight[k];
        linearWeights = linearWeights && std::fabs(top - m.positions[v].x / 32.0f) < 1e-5f;
    }
    CHECK(linearWeights);
}

TEST(subdivision_counts_shared_edges_once_and_allows_an_already_refined_mesh) {
    Mesh m;
    m.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}};
    m.triangles = {{0, 1, 2}, {1, 3, 2}};
    std::vector<std::array<uint32_t, 2>> parents;
    // Only the shared diagonal is long. Both incident triangles use the same
    // midpoint, so one extra vertex is sufficient at this exact cap.
    CHECK(subdivideToEdgeLength(m, 1.0f, 5, &parents));
    CHECK(m.vertexCount() == 5 && m.triangleCount() == 4);
    CHECK(parents.size() == 1);
    if (!parents.empty()) CHECK((parents[0] == std::array<uint32_t, 2>{1, 2}));
    const auto triangles = m.triangles;
    CHECK(subdivideToEdgeLength(m, 1.0f, m.vertexCount(), &parents));
    CHECK(m.vertexCount() == 5 && m.triangles == triangles && parents.empty());
}

TEST(subdivision_actual_cap_failure_preserves_mesh_and_parent_mapping) {
    Mesh m;
    m.positions = {{0, 0, 0}, {1, 0, 0}, {0.5f, 0.8660254f, 0}};
    m.triangles = {{0, 1, 2}};
    m.computeNormals();
    m.attributes.resize(m.vertexCount());
    m.attributes[0].infection = 0.75f;
    const Mesh before = m;
    const std::vector<std::array<uint32_t, 2>> sentinel = {{23, 42}};
    auto parents = sentinel;
    // Each original edge needs one midpoint (lower bound: six vertices),
    // but the new interior edges also exceed 0.51. The actual cap must still
    // stop refinement transactionally after the preflight has accepted it.
    CHECK(!subdivideToEdgeLength(m, 0.51f, 6, &parents));
    CHECK(m.triangles == before.triangles && parents == sentinel);
    CHECK(m.positions.size() == before.positions.size() && m.normals.size() == before.normals.size());
    CHECK(m.attributes.size() == before.attributes.size() && m.attributes[0].infection == 0.75f);
    CHECK(std::memcmp(m.positions.data(), before.positions.data(), m.positions.size() * sizeof(Vec3)) == 0);
    CHECK(std::memcmp(m.normals.data(), before.normals.data(), m.normals.size() * sizeof(Vec3)) == 0);
}

TEST(subdivision_rejects_absurd_or_invalid_requests_without_mutating_input) {
    Mesh m;
    m.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    m.triangles = {{0, 1, 2}};
    const Mesh before = m;
    const std::vector<std::array<uint32_t, 2>> sentinel = {{23, 42}};
    auto parents = sentinel;
    for (float resolution : {0.0f, -1.0f, std::numeric_limits<float>::infinity(),
                             std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::denorm_min()}) {
        CHECK(!subdivideToEdgeLength(m, resolution, std::numeric_limits<size_t>::max(), &parents));
        CHECK(m.triangles == before.triangles && parents == sentinel);
        CHECK(m.positions.size() == before.positions.size());
        CHECK(std::memcmp(m.positions.data(), before.positions.data(), m.positions.size() * sizeof(Vec3)) == 0);
    }
    CHECK(!subdivideToEdgeLength(m, 2.0f, m.vertexCount() - 1, &parents));
    CHECK(m.triangles == before.triangles && parents == sentinel);
    m.positions[1].x = std::numeric_limits<float>::infinity();
    CHECK(!subdivideToEdgeLength(m, 1.0f, 1000, &parents));
    CHECK(std::isinf(m.positions[1].x) && m.triangles == before.triangles && parents == sentinel);
    m.positions[1].x = std::numeric_limits<float>::quiet_NaN();
    CHECK(!subdivideToEdgeLength(m, 1.0f, 1000, &parents));
    CHECK(std::isnan(m.positions[1].x) && m.triangles == before.triangles && parents == sentinel);
    m.positions = before.positions;
    m.triangles[0][1] = 42;
    CHECK(!subdivideToEdgeLength(m, 1.0f, 1000, &parents));
    CHECK(m.triangles[0][1] == 42 && parents == sentinel);
    m.triangles[0] = {0, 0, 2};
    CHECK(!subdivideToEdgeLength(m, 0.1f, 1000, &parents));
    CHECK((m.triangles[0] == std::array<uint32_t, 3>{0, 0, 2}) && parents == sentinel);
}

TEST(shell_holds_only_the_corruption_lifted_off_the_host) {
    GrowthParams p;
    p.coverage = 0.3f;
    Mesh surface = corrupt(mannequin().mesh, mannequin().defaultSeeds, p);
    Mesh shell = extractShell(surface, 0.002f);
    CHECK(shell.vertexCount() > 1000);
    CHECK(shell.vertexCount() < surface.vertexCount() * 6 / 10);
    CHECK(shell.attributes.size() == shell.vertexCount() && shell.normals.size() == shell.vertexCount());
    size_t touchedTriangles = 0;
    for (const auto& t : surface.triangles) {
        bool touched = false;
        for (uint32_t v : t) touched = touched || surface.attributes[v].infection > 0 || surface.attributes[v].glow > 0;
        touchedTriangles += touched;
    }
    CHECK(shell.triangleCount() == touchedTriangles);
    // Every shell vertex is its surface vertex moved out along the normal.
    PointGrid grid(surface.positions, 0.02f);
    bool lifted = true;
    for (size_t i = 0; i < shell.vertexCount(); i += 13) {
        int32_t v = grid.nearest(shell.positions[i] - shell.normals[i] * 0.002f, 1e-4f);
        lifted = lifted && v >= 0;
    }
    CHECK(lifted);
    Mesh clean = mannequin().mesh;
    CHECK(extractShell(clean, 0.002f).vertexCount() == 0);  // no attributes, nothing to lift
}

TEST(skin_blend_merges_caps_and_normalises) {
    SkinWeights a = SkinWeights::single(3), b;
    b.count = 2;
    b.bone = {3, 7};
    b.weight = {0.5f, 0.5f};
    const SkinWeights* in[2] = {&a, &b};
    const float f[2] = {1.0f, 1.0f};
    SkinWeights m = blendSkin(in, f, 2);
    CHECK(m.count == 2);
    CHECK(m.bone[0] == 3 && std::fabs(m.weight[0] - 0.75f) < 1e-6f);
    CHECK(m.bone[1] == 7 && std::fabs(m.weight[1] - 0.25f) < 1e-6f);
    // Twelve bones in, the strongest eight out, still summing to one.
    std::vector<SkinWeights> many;
    std::vector<float> factors;
    for (uint16_t i = 0; i < 12; ++i) {
        many.push_back(SkinWeights::single(i));
        factors.push_back(static_cast<float>(i + 1));
    }
    std::vector<const SkinWeights*> ptrs;
    for (const auto& w : many) ptrs.push_back(&w);
    SkinWeights capped = blendSkin(ptrs.data(), factors.data(), ptrs.size());
    CHECK(capped.count == SkinWeights::kMaxInfluences);
    CHECK(capped.bone[0] == 11 && capped.bone[7] == 4);
    float sum = 0;
    for (int k = 0; k < capped.count; ++k) sum += capped.weight[k];
    CHECK(std::fabs(sum - 1.0f) < 1e-5f);
}

TEST(skinning_survives_welding_and_subdivision) {
    // Bone 0 owns the bottom of the cube, bone 1 the top.
    Mesh m = splitCube();
    SkinTable skin;
    for (const auto& p : m.positions) skin.push_back(SkinWeights::single(p.y > 0.5f ? 1 : 0));
    std::vector<uint32_t> remap;
    weldVertices(m, 1e-4f, &remap);
    skin = skinAfterWeld(skin, remap, m.vertexCount());
    CHECK(skin.size() == 8);
    std::vector<std::array<uint32_t, 2>> parents;
    CHECK(subdivideToEdgeLength(m, 0.2f, 100000, &parents));
    CHECK(parents.size() == m.vertexCount() - 8);
    skin = skinAfterSubdivision(skin, parents);
    CHECK(skin.size() == m.vertexCount());
    // Along a vertical edge the weight of bone 1 is the height: midpoint
    // blending reproduces linear interpolation exactly.
    bool linear = true;
    for (size_t v = 0; v < m.vertexCount(); ++v) {
        const Vec3& p = m.positions[v];
        bool onVerticalEdge = (p.x < 1e-6f || p.x > 1 - 1e-6f) && (p.z < 1e-6f || p.z > 1 - 1e-6f);
        if (!onVerticalEdge) continue;
        float top = 0;
        for (int k = 0; k < skin[v].count; ++k)
            if (skin[v].bone[k] == 1) top = skin[v].weight[k];
        linear = linear && std::fabs(top - p.y) < 1e-4f;
    }
    CHECK(linear);
}

// Linear blend skinning with bone 0 fixed and bone 1 rotated about a pivot.
struct TwoBoneRig {
    Vec3 pivot;
    float angle;
    Vec3 rotate(const Vec3& p) const {  // about +z through the pivot
        Vec3 d = p - pivot;
        float c = std::cos(angle), s = std::sin(angle);
        return pivot + Vec3{c * d.x - s * d.y, s * d.x + c * d.y, d.z};
    }
    Vec3 deform(const Vec3& p, const SkinWeights& w) const {
        Vec3 out{0, 0, 0};
        for (int k = 0; k < w.count; ++k) out += (w.bone[k] == 1 ? rotate(p) : p) * w.weight[k];
        return out;
    }
};

TEST(corruption_rides_the_rig) {
    // The mannequin's right arm on bone 1, blended across the shoulder.
    const Mesh& host = mannequin().mesh;
    SkinTable skin;
    for (const auto& p : host.positions) {
        float arm = smoothstep(0.15f, 0.25f, p.x) * smoothstep(1.2f, 1.3f, p.y);
        SkinWeights w;
        w.count = 0;
        if (1 - arm > 0) w.bone[w.count] = 0, w.weight[w.count++] = 1 - arm;
        if (arm > 0) w.bone[w.count] = 1, w.weight[w.count++] = arm;
        skin.push_back(w);
    }
    GrowthParams p;
    p.coverage = 0.85f;
    Growth g = grow(host, mannequin().defaultSeeds, p, FeatureParams{});
    std::vector<uint32_t> sources;
    Mesh shell = extractShell(g.surface, 0.0015f, &sources);
    SkinTable shellSkin = skinFromSources(skin, sources);
    SkinTable growthSkin = bindToNearest(host.positions, skin, g.growths.positions);
    CHECK(shellSkin.size() == shell.vertexCount());
    CHECK(growthSkin.size() == g.growths.vertexCount());

    TwoBoneRig rig{{0.19f, 1.425f, 0.0f}, -1.05f};  // swing the arm down 60 degrees
    // Every shell vertex keeps its exact offset from the host vertex it grew from.
    bool shellRigid = true;
    for (size_t i = 0; i < shell.vertexCount(); ++i) {
        const Vec3& h = host.positions[sources[i]];
        float before = length(shell.positions[i] - h);
        float after = length(rig.deform(shell.positions[i], shellSkin[i]) - rig.deform(h, skin[sources[i]]));
        if (host.positions[sources[i]].x > 0.3f) shellRigid = shellRigid && std::fabs(after - before) < 1e-4f;
    }
    CHECK(shellRigid);
    // Growths on the forearm and hand move with it, as one piece.
    PointGrid grid(host.positions, 0.02f);
    size_t checked = 0;
    bool growthsRigid = true;
    for (size_t i = 0; i < g.growths.vertexCount(); i += 5) {
        const Vec3& q = g.growths.positions[i];
        if (q.x < 0.45f) continue;
        int32_t h = grid.nearest(q, 0.3f);
        if (h < 0) continue;
        float before = length(q - host.positions[h]);
        float after = length(rig.deform(q, growthSkin[i]) - rig.deform(host.positions[h], skin[h]));
        growthsRigid = growthsRigid && std::fabs(after - before) < 1e-4f;
        ++checked;
    }
    CHECK(checked > 500);
    CHECK(growthsRigid);
    // Growths on the torso stay put.
    bool torsoStill = true;
    for (size_t i = 0; i < g.growths.vertexCount(); i += 5) {
        const Vec3& q = g.growths.positions[i];
        if (std::fabs(q.x) > 0.1f) continue;
        torsoStill = torsoStill && length(rig.deform(q, growthSkin[i]) - q) < 1e-5f;
    }
    CHECK(torsoStill);
}

TEST(infection_samples_cover_the_corruption_sparsely) {
    GrowthParams p;
    p.coverage = 0.5f;
    Mesh surface = corrupt(mannequin().mesh, mannequin().defaultSeeds, p);
    const float spacing = 0.05f;
    std::vector<InfectionSample> samples = sampleInfection(surface, spacing);
    size_t touched = 0;
    for (const auto& a : surface.attributes) touched += a.infection > 0 || a.glow > 0;
    CHECK(samples.size() > 50);
    CHECK(samples.size() * 20 < touched);  // far smaller than the mesh
    std::vector<Vec3> positions;
    for (const auto& s : samples) positions.push_back(s.position);
    // Spaced apart, and every touched vertex has a sample close by whose
    // record is at least as strong and as early.
    bool spaced = true;
    PointGrid grid(positions, spacing);
    for (size_t i = 0; i < positions.size(); ++i)
        for (uint32_t j : grid.within(positions[i], spacing * 0.999f)) spaced = spaced && j == i;
    CHECK(spaced);
    bool covered = true;
    for (size_t v = 0; v < surface.vertexCount(); ++v) {
        const auto& a = surface.attributes[v];
        if (a.infection <= 0 && a.glow <= 0) continue;
        int32_t s = grid.nearest(surface.positions[v], spacing * 1.5f);
        covered = covered && s >= 0 && samples[s].arrival <= a.arrival && samples[s].infection >= std::max(a.infection, a.glow);
    }
    CHECK(covered);
    // The seed's own tissue arrives first.
    float earliest = 1e9f;
    for (const auto& s : samples) earliest = std::min(earliest, s.arrival);
    CHECK(earliest < 0.05f);
    CHECK(sampleInfection(mannequin().mesh, spacing).empty());  // a clean host has nothing to record
}

int main(int argc, char** argv) {
    // Unbuffered, so a crash still shows which test was running.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int run = 0;
    for (const auto& t : registry()) {
        if (filter && !std::strstr(t.name, filter)) continue;
        int before = failures;
        try {
            t.body();
        } catch (const std::exception& e) {
            std::printf("    EXCEPTION %s\n", e.what());
            ++failures;
        }
        ++run;
        std::printf("%s %s\n", failures == before ? "ok  " : "FAIL", t.name);
    }
    std::printf("%d tests, %d failed checks\n", run, failures);
    return failures == 0 ? 0 : 1;
}
