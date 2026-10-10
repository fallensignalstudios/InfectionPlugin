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
#include <map>
#include <string>
#include <vector>

#include "eclipse/corruption.h"
#include "eclipse/export.h"
#include "eclipse/hosts.h"
#include "eclipse/preview.h"
#include "eclipse/random.h"
#include "eclipse/sdf.h"

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
    std::ifstream f(path, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
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
    uint32_t binLen = u32(20 + jsonLen);
    CHECK(u32(24 + jsonLen) == 0x004E4942u);
    CHECK(28 + jsonLen + binLen == bytes.size());
    size_t expected = out.vertexCount() * (12 + 12 + 16 + 8) + out.triangleCount() * 12;
    CHECK(binLen >= expected && binLen < expected + 32);
    std::filesystem::remove(path);
}

TEST(obj_round_trips) {
    Mesh m = sphere(0.2f, 0.04f);
    auto path = (std::filesystem::temp_directory_path() / "eclipse_test.obj").string();
    CHECK(writeObj(path, m));
    Mesh back;
    CHECK(readObj(path, back));
    CHECK(back.vertexCount() == m.vertexCount());
    CHECK(back.triangles == m.triangles);
    std::filesystem::remove(path);
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
    std::filesystem::remove(path);
}

int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int run = 0;
    for (const auto& t : registry()) {
        if (filter && !std::strstr(t.name, filter)) continue;
        int before = failures;
        t.body();
        ++run;
        std::printf("%s %s\n", failures == before ? "ok  " : "FAIL", t.name);
    }
    std::printf("%d tests, %d failed checks\n", run, failures);
    return failures == 0 ? 0 : 1;
}
