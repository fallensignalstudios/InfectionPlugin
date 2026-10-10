#include "eclipse/corruption.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <numeric>
#include <queue>
#include <unordered_map>

#include "eclipse/random.h"
#include "eclipse/spatial.h"

namespace eclipse {

namespace {

constexpr float kInf = std::numeric_limits<float>::infinity();

struct Source {
    uint32_t vertex;
    int32_t label;
    float distance;
};

struct Propagation {
    std::vector<float> distance;
    std::vector<int32_t> parent;
    std::vector<int32_t> label;
};

// Multi-source Dijkstra over the vertex graph. cost(from, to, label) returns
// the edge weight, or a negative number to forbid the step. Ties break on the
// vertex index, so results never depend on container ordering.
Propagation propagate(const VertexGraph& graph, size_t vertexCount, const std::vector<Source>& sources,
                      const std::function<float(uint32_t, uint32_t, int32_t)>& cost, float limit = kInf) {
    Propagation r;
    r.distance.assign(vertexCount, kInf);
    r.parent.assign(vertexCount, -1);
    r.label.assign(vertexCount, -1);
    using Item = std::pair<float, uint32_t>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> queue;
    for (const auto& s : sources) {
        if (s.distance < r.distance[s.vertex]) {
            r.distance[s.vertex] = s.distance;
            r.label[s.vertex] = s.label;
            r.parent[s.vertex] = -1;
            queue.push({s.distance, s.vertex});
        }
    }
    while (!queue.empty()) {
        auto [d, v] = queue.top();
        queue.pop();
        if (d > r.distance[v]) continue;
        for (uint32_t i = graph.offsets[v]; i < graph.offsets[v + 1]; ++i) {
            uint32_t w = graph.neighbours[i];
            float c = cost(v, w, r.label[v]);
            if (c < 0.0f) continue;
            float nd = d + c;
            if (nd > limit) continue;
            if (nd < r.distance[w] || (nd == r.distance[w] && static_cast<int32_t>(v) < r.parent[w])) {
                r.distance[w] = nd;
                r.parent[w] = static_cast<int32_t>(v);
                r.label[w] = r.label[v];
                queue.push({nd, w});
            }
        }
    }
    return r;
}

float plateCoverage(float infection) { return smoothstep(0.35f, 0.85f, infection); }

}  // namespace

uint32_t nearestVertex(const Mesh& mesh, const Vec3& p) {
    uint32_t best = 0;
    float bestD = kInf;
    for (uint32_t i = 0; i < mesh.positions.size(); ++i) {
        Vec3 d = mesh.positions[i] - p;
        float dd = dot(d, d);
        if (dd < bestD) {
            bestD = dd;
            best = i;
        }
    }
    return best;
}

SpreadField computeSpread(const Mesh& host, const VertexGraph& graph, const std::vector<Vec3>& seeds,
                          const GrowthParams& params) {
    const size_t n = host.positions.size();
    SpreadField field;
    if (n == 0) return field;

    Noise noise(params.seed * 7919u + 11u);
    std::vector<float> resistance(n);
    std::vector<float> curvature = vertexCurvature(host, graph);
    for (size_t v = 0; v < n; ++v) {
        float rough = params.noiseStrength * noise.fbm(host.positions[v] * params.noiseFrequency, 4);
        float ridge = params.cavityBias * std::tanh(curvature[v] / 30.0f);
        resistance[v] = std::exp(rough + ridge);
    }

    std::vector<Source> sources;
    for (const auto& s : seeds) {
        uint32_t v = nearestVertex(host, s);
        field.seedVertices.push_back(v);
        sources.push_back({v, static_cast<int32_t>(sources.size()), 0.0f});
    }
    auto cost = [&](uint32_t a, uint32_t b, int32_t) {
        Vec3 e = host.positions[b] - host.positions[a];
        float len = length(e);
        if (len <= 0.0f) return 0.0f;
        float climb = std::max(0.25f, 1.0f - params.gravity * (e.y / len));
        return len * climb * 0.5f * (resistance[a] + resistance[b]);
    };
    Propagation p = propagate(graph.twoRing(), n, sources, cost);
    field.distance = std::move(p.distance);
    field.parent = std::move(p.parent);

    // The front stops where the overtaken area equals the requested coverage.
    std::vector<float> area = vertexAreas(host);
    std::vector<uint32_t> order;
    float reachable = 0.0f;
    for (uint32_t v = 0; v < n; ++v)
        if (field.distance[v] < kInf) {
            order.push_back(v);
            reachable += area[v];
        }
    std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
        return field.distance[a] != field.distance[b] ? field.distance[a] < field.distance[b] : a < b;
    });
    float target = clamp01(params.coverage) * reachable, acc = 0.0f;
    field.threshold = 0.0f;
    if (target > 0.0f) {
        for (uint32_t v : order) {
            acc += area[v];
            field.threshold = field.distance[v];
            if (acc >= target) break;
        }
    }

    field.infection.assign(n, 0.0f);
    const float width = std::max(1e-6f, params.frontWidth * field.threshold);
    if (field.threshold > 0.0f)
        for (size_t v = 0; v < n; ++v)
            field.infection[v] = smoothstep(field.threshold, field.threshold - width, field.distance[v]);
    return field;
}

VeinNetwork computeVeins(const Mesh& host, const SpreadField& spread,
                         const GrowthParams& params) {
    const size_t n = host.positions.size();
    VeinNetwork veins;
    veins.flow.assign(n, 0.0f);
    veins.mask.assign(n, 0.0f);
    veins.radius.assign(n, 0.0f);
    if (spread.threshold <= 0.0f) return veins;

    // Every vertex in reach drains its area toward the seed along the spread
    // tree; trunks form where many paths merge, like rivers or leaf veins.
    const float reach = spread.threshold * (1.0f + params.veinReach);
    std::vector<float> area = vertexAreas(host);
    std::vector<uint32_t> order;
    for (uint32_t v = 0; v < n; ++v)
        if (spread.distance[v] <= reach) order.push_back(v);
    std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
        return spread.distance[a] != spread.distance[b] ? spread.distance[a] > spread.distance[b] : a > b;
    });
    for (uint32_t v : order) {
        veins.flow[v] += area[v];
        if (spread.parent[v] >= 0) veins.flow[spread.parent[v]] += veins.flow[v];
    }

    // A band narrower than the vertex spacing breaks into dots, so the
    // thinnest vein is never finer than the mesh can show.
    double edgeSum = 0.0;
    for (const auto& t : host.triangles) edgeSum += length(host.positions[t[1]] - host.positions[t[0]]);
    const float minRadius = host.triangles.empty() ? 0.0f : 0.8f * static_cast<float>(edgeSum / host.triangles.size());

    std::vector<float> centreRadius(n, 0.0f), strength(n, 0.0f);
    std::vector<uint8_t> isVein(n, 0);
    for (uint32_t v : order) {
        if (veins.flow[v] < params.veinMinFlow) continue;
        float inside = smoothstep(reach, spread.threshold, spread.distance[v]);
        // Veins that run ahead of the front are thinner, dimmer scouts.
        centreRadius[v] = std::min(params.veinMaxWidth, params.veinWidth * std::sqrt(veins.flow[v] / params.veinMinFlow)) *
                          (0.45f + 0.55f * inside);
        centreRadius[v] = std::max(centreRadius[v], minRadius);
        strength[v] = 0.5f + 0.5f * inside;
        isVein[v] = 1;
        ++veins.veinVertexCount;
    }
    if (veins.veinVertexCount == 0) return veins;

    // Cut the vein forest into unbranched runs. A run starts at every vein
    // vertex that is not simply a link in a chain (a tip or a junction) and
    // climbs toward the seed until the next such vertex.
    std::vector<uint8_t> children(n, 0);
    for (uint32_t v = 0; v < n; ++v)
        if (isVein[v] && spread.parent[v] >= 0 && isVein[spread.parent[v]])
            children[spread.parent[v]] = static_cast<uint8_t>(std::min(255, children[spread.parent[v]] + 1));
    for (uint32_t v : order) {
        if (!isVein[v] || children[v] == 1) continue;
        int32_t up = spread.parent[v];
        if (up < 0 || !isVein[up]) continue;
        VeinPath path;
        auto push = [&](uint32_t u) {
            path.vertices.push_back(u);
            path.points.push_back(host.positions[u]);
            path.normals.push_back(host.normals[u]);
            path.radius.push_back(centreRadius[u]);
            path.strength.push_back(strength[u]);
        };
        push(v);
        while (up >= 0 && isVein[up]) {
            push(static_cast<uint32_t>(up));
            if (children[up] != 1) break;
            up = spread.parent[up];
        }
        // Relax the interior so the run follows a curve rather than mesh
        // edges; the ends stay put so junctions remain joined.
        for (int pass = 0; pass < 8 && path.points.size() > 2; ++pass) {
            std::vector<Vec3> next = path.points;
            for (size_t i = 1; i + 1 < path.points.size(); ++i)
                next[i] = path.points[i] * 0.5f + (path.points[i - 1] + path.points[i + 1]) * 0.25f;
            path.points.swap(next);
        }
        veins.paths.push_back(std::move(path));
    }

    // Paint bands around the curves by true distance to each segment.
    const float cell = std::max(params.veinMaxWidth * 2.0f, 0.01f);
    auto cellOf = [&](const Vec3& p) {
        return std::array<int64_t, 3>{static_cast<int64_t>(std::floor(p.x / cell)),
                                      static_cast<int64_t>(std::floor(p.y / cell)),
                                      static_cast<int64_t>(std::floor(p.z / cell))};
    };
    auto pack = [](int64_t x, int64_t y, int64_t z) {
        return (static_cast<uint64_t>(x & 0x1FFFFF) << 42) | (static_cast<uint64_t>(y & 0x1FFFFF) << 21) |
               static_cast<uint64_t>(z & 0x1FFFFF);
    };
    std::unordered_map<uint64_t, std::vector<std::pair<uint32_t, uint32_t>>> grid;  // (path, segment)
    for (uint32_t pi = 0; pi < veins.paths.size(); ++pi) {
        const auto& path = veins.paths[pi];
        for (uint32_t si = 0; si + 1 < path.points.size(); ++si) {
            float r = std::max(path.radius[si], path.radius[si + 1]);
            Vec3 pad{r, r, r};
            auto lo = cellOf(vmin(path.points[si], path.points[si + 1]) - pad);
            auto hi = cellOf(vmax(path.points[si], path.points[si + 1]) + pad);
            for (int64_t x = lo[0]; x <= hi[0]; ++x)
                for (int64_t y = lo[1]; y <= hi[1]; ++y)
                    for (int64_t z = lo[2]; z <= hi[2]; ++z) grid[pack(x, y, z)].push_back({pi, si});
        }
    }
    for (uint32_t v = 0; v < n; ++v) {
        auto c = cellOf(host.positions[v]);
        auto it = grid.find(pack(c[0], c[1], c[2]));
        if (it == grid.end()) continue;
        const Vec3& p = host.positions[v];
        for (auto [pi, si] : it->second) {
            const auto& path = veins.paths[pi];
            // Ignore veins on the far side of a thin wall.
            if (dot(path.normals[si], host.normals[v]) < 0.2f) continue;
            Vec3 a = path.points[si], ab = path.points[si + 1] - a;
            float t = clamp01(dot(p - a, ab) / std::max(1e-12f, dot(ab, ab)));
            float d = length(p - (a + ab * t));
            float r = lerpf(path.radius[si], path.radius[si + 1], t);
            float m = (1.0f - smoothstep(0.35f, 1.0f, d / r)) * lerpf(path.strength[si], path.strength[si + 1], t);
            if (m > veins.mask[v]) {
                veins.mask[v] = m;
                veins.radius[v] = r;
            }
        }
    }
    return veins;
}

PlateLayout computePlates(const Mesh& host, const VertexGraph& graph, const SpreadField& spread,
                          const GrowthParams& params) {
    const size_t n = host.positions.size();
    PlateLayout layout;
    layout.plate.assign(n, -1);
    layout.seamDistance.assign(n, kInf);
    layout.siteDistance.assign(n, kInf);
    if (spread.threshold <= 0.0f || params.plateSize <= 0.0f) return layout;

    std::vector<uint8_t> armoured(n, 0);
    std::vector<uint32_t> candidates;
    for (uint32_t v = 0; v < n; ++v)
        if (plateCoverage(spread.infection[v]) > 0.0f) {
            armoured[v] = 1;
            candidates.push_back(v);
        }
    if (candidates.empty()) return layout;

    // Dart-throwing in a seeded random order gives an even scatter of sites.
    Rng rng(params.seed, 0xC0FFEEull);
    for (size_t i = candidates.size(); i > 1; --i) std::swap(candidates[i - 1], candidates[rng.below(static_cast<uint32_t>(i))]);
    const float spacing = params.plateSize * 0.8f;
    auto key = [&](const Vec3& p) {
        auto c = [&](float x) { return static_cast<int64_t>(std::floor(x / spacing)); };
        return std::array<int64_t, 3>{c(p.x), c(p.y), c(p.z)};
    };
    auto pack = [](int64_t x, int64_t y, int64_t z) {
        return (static_cast<uint64_t>(x & 0x1FFFFF) << 42) | (static_cast<uint64_t>(y & 0x1FFFFF) << 21) |
               static_cast<uint64_t>(z & 0x1FFFFF);
    };
    std::unordered_map<uint64_t, std::vector<uint32_t>> grid;
    std::vector<Source> sites;
    for (uint32_t v : candidates) {
        const Vec3& p = host.positions[v];
        auto k = key(p);
        bool clear = true;
        for (int dx = -1; dx <= 1 && clear; ++dx)
            for (int dy = -1; dy <= 1 && clear; ++dy)
                for (int dz = -1; dz <= 1 && clear; ++dz) {
                    auto it = grid.find(pack(k[0] + dx, k[1] + dy, k[2] + dz));
                    if (it == grid.end()) continue;
                    for (uint32_t s : it->second)
                        if (length(host.positions[s] - p) < spacing) {
                            clear = false;
                            break;
                        }
                }
        if (!clear) continue;
        grid[pack(k[0], k[1], k[2])].push_back(v);
        sites.push_back({v, static_cast<int32_t>(sites.size()), 0.0f});
    }
    layout.plateCount = sites.size();

    Noise wobble(params.seed * 31u + 5u);
    std::vector<float> jitter(n, 1.0f);
    for (uint32_t v : candidates)
        jitter[v] = 1.0f + 0.4f * wobble.fbm(host.positions[v] * (2.5f / params.plateSize), 2);

    // The direction the corruption travelled, smoothed. Distance is cheaper
    // along it, so plates grow long and narrow like bundled sinew.
    std::vector<Vec3> grain(n, Vec3{0, 0, 0});
    for (uint32_t v : candidates)
        if (spread.parent[v] >= 0) grain[v] = normalize(host.positions[v] - host.positions[spread.parent[v]]);
    for (int pass = 0; pass < 4; ++pass) {
        std::vector<Vec3> next = grain;
        for (uint32_t v : candidates) {
            Vec3 sum = grain[v];
            for (uint32_t i = graph.offsets[v]; i < graph.offsets[v + 1]; ++i) {
                const Vec3& g = grain[graph.neighbours[i]];
                sum += dot(g, grain[v]) < 0.0f ? -g : g;  // grain is unsigned
            }
            const Vec3& nv = host.normals[v];
            sum -= nv * dot(sum, nv);
            next[v] = length(sum) > 1e-8f ? normalize(sum) : Vec3{0, 0, 0};
        }
        grain.swap(next);
    }
    const float stretch = std::max(1.0f, params.plateStretch);
    auto cellCost = [&](uint32_t a, uint32_t b, int32_t) {
        if (!armoured[b]) return -1.0f;
        Vec3 e = host.positions[b] - host.positions[a];
        float len = length(e);
        if (len <= 0.0f) return 0.0f;
        float along = dot(e / len, grain[a]);
        along *= along;
        float metric = std::sqrt(along / (stretch * stretch) + (1.0f - along));
        return len * metric * 0.5f * (jitter[a] + jitter[b]);
    };
    Propagation cells = propagate(graph.twoRing(), n, sites, cellCost);
    layout.plate = cells.label;
    layout.siteDistance = cells.distance;

    std::vector<Source> seams;
    for (uint32_t v = 0; v < n; ++v) {
        if (layout.plate[v] < 0) continue;
        for (uint32_t i = graph.offsets[v]; i < graph.offsets[v + 1]; ++i) {
            // Only the lower-numbered side of a border is seam, so the groove
            // is one vertex wide rather than two.
            int32_t other = layout.plate[graph.neighbours[i]];
            if (other >= 0 && other > layout.plate[v]) {
                seams.push_back({v, 0, 0.0f});
                break;
            }
        }
    }
    auto seamCost = [&](uint32_t a, uint32_t b, int32_t) {
        if (!armoured[b]) return -1.0f;
        return length(host.positions[b] - host.positions[a]);
    };
    if (!seams.empty()) layout.seamDistance = propagate(graph, n, seams, seamCost, params.plateSize).distance;
    return layout;
}

SurfaceGrowth growSurface(const Mesh& hostIn, const std::vector<Vec3>& seeds, const GrowthParams& params) {
    SurfaceGrowth result;
    Mesh host = hostIn;
    if (host.normals.size() != host.positions.size()) host.computeNormals();
    Mesh& out = result.surface;
    out = host;
    const size_t n = out.positions.size();
    VertexGraph graph = VertexGraph::build(out);

    result.spread = computeSpread(out, graph, seeds, params);
    result.veins = computeVeins(out, result.spread, params);
    result.plates = computePlates(out, graph, result.spread, params);
    const SpreadField& spread = result.spread;
    const VeinNetwork& veins = result.veins;
    const PlateLayout& plates = result.plates;

    Noise swellNoise(params.seed * 13u + 3u);
    Noise knots(params.seed * 101u + 7u);
    Noise glowNoise(params.seed * 53u + 29u);
    out.attributes.assign(n, CorruptionAttributes{});
    for (size_t v = 0; v < n; ++v) {
        auto& a = out.attributes[v];
        const Vec3& p = host.positions[v];
        const float inf = spread.infection[v];
        const float armour = plateCoverage(inf);

        float height = params.swell * inf * (0.75f + 0.25f * swellNoise.fbm(p * 9.0f, 3));
        height += params.roughness * inf * (knots.ridged(p * 45.0f, 3) - 0.45f) * 2.0f;

        float crack = 0.0f, crackLight = 0.0f;
        if (plates.plate[v] >= 0) {
            float fromSeam = plates.seamDistance[v];
            // A crest along the middle of each plate, falling into the seams.
            float crest = std::pow(clamp01(fromSeam / (params.plateSize * 0.3f)), 0.6f);
            height += params.plateThickness * armour * crest;
            crack = armour * (1.0f - smoothstep(0.0f, params.seamWidth, fromSeam));
            // Only some seams leak light; the rest stay dark grooves.
            float leak = smoothstep(0.55f - params.seamGlow * 0.6f, 0.75f - params.seamGlow * 0.6f,
                                    0.5f + 0.5f * glowNoise.fbm(p * 10.0f, 3));
            crackLight = crack * leak;
            a.plateTint = hashToUnit(static_cast<uint32_t>(plates.plate[v]) * 2654435761u + params.seed);
        }

        float vein = veins.mask[v];
        float veinScale = params.veinMaxWidth > 0 ? std::sqrt(veins.radius[v] / params.veinMaxWidth) : 0.0f;
        height += params.veinHeight * vein * veinScale * (1.0f - armour);

        float core = 0.0f;
        if (spread.threshold > 0.0f && params.coreRadius > 0.0f)
            core = 1.0f - smoothstep(0.0f, params.coreRadius, spread.distance[v]);

        a.infection = std::max(inf, smoothstep(0.0f, 0.6f, vein));
        a.crack = crack;
        // Veins stain the tissue dark and glow only faintly; light belongs to
        // the cores, the eyes and the seams that leak it.
        a.glow = std::max({vein * (1.0f - 0.85f * armour) * 0.05f, crackLight * 0.6f, core});
        a.height = height;
        a.arrival = spread.threshold > 0.0f && spread.distance[v] < kInf
                        ? std::min(4.0f, spread.distance[v] / spread.threshold)
                        : 4.0f;
        out.positions[v] = host.positions[v] + host.normals[v] * height;
    }
    out.computeNormals();
    return result;
}

Mesh corrupt(const Mesh& host, const std::vector<Vec3>& seeds, const GrowthParams& params) {
    return growSurface(host, seeds, params).surface;
}

Mesh extractShell(const Mesh& surface, float offset, std::vector<uint32_t>* sourceVertex) {
    Mesh shell;
    if (sourceVertex) sourceVertex->clear();
    if (surface.attributes.size() != surface.positions.size()) return shell;
    auto touched = [&](uint32_t v) { return surface.attributes[v].infection > 0.0f || surface.attributes[v].glow > 0.0f; };
    std::vector<int32_t> remap(surface.positions.size(), -1);
    for (const auto& t : surface.triangles) {
        if (!touched(t[0]) && !touched(t[1]) && !touched(t[2])) continue;
        std::array<uint32_t, 3> r{};
        for (int i = 0; i < 3; ++i) {
            if (remap[t[i]] < 0) {
                remap[t[i]] = static_cast<int32_t>(shell.positions.size());
                shell.positions.push_back(surface.positions[t[i]] + surface.normals[t[i]] * offset);
                shell.normals.push_back(surface.normals[t[i]]);
                shell.attributes.push_back(surface.attributes[t[i]]);
                if (sourceVertex) sourceVertex->push_back(t[i]);
            }
            r[i] = static_cast<uint32_t>(remap[t[i]]);
        }
        shell.triangles.push_back(r);
    }
    return shell;
}

std::vector<InfectionSample> sampleInfection(const Mesh& surface, float spacing) {
    std::vector<InfectionSample> samples;
    if (surface.attributes.size() != surface.positions.size() || spacing <= 0.0f) return samples;
    std::vector<uint32_t> touched;
    for (uint32_t v = 0; v < surface.positions.size(); ++v) {
        const auto& a = surface.attributes[v];
        if (a.infection > 0.0f || a.glow > 0.0f) touched.push_back(v);
    }
    // Earliest first, so the sample standing for a patch is its oldest tissue.
    std::stable_sort(touched.begin(), touched.end(), [&](uint32_t a, uint32_t b) {
        return surface.attributes[a].arrival < surface.attributes[b].arrival;
    });
    std::vector<uint32_t> sites = poissonThin(surface.positions, touched, spacing);
    for (uint32_t v : sites) samples.push_back({surface.positions[v], surface.attributes[v].arrival, 0.0f});

    // Each touched vertex lends its strength to its nearest sample, and its
    // arrival if earlier.
    std::vector<Vec3> sitePositions;
    for (const auto& s : samples) sitePositions.push_back(s.position);
    PointGrid grid(sitePositions, spacing);
    for (uint32_t v : touched) {
        int32_t nearest = grid.nearest(surface.positions[v], spacing * 1.5f);
        if (nearest < 0) continue;
        const auto& a = surface.attributes[v];
        auto& s = samples[static_cast<size_t>(nearest)];
        s.infection = std::max({s.infection, a.infection, a.glow});
        s.arrival = std::min(s.arrival, a.arrival);
    }
    return samples;
}

uint64_t meshHash(const Mesh& mesh) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](int64_t value) {
        for (int i = 0; i < 8; ++i) {
            h ^= static_cast<uint64_t>(value >> (8 * i)) & 0xFFu;
            h *= 1099511628211ull;
        }
    };
    auto q = [](float f) { return static_cast<int64_t>(std::llround(static_cast<double>(f) * 1e5)); };
    mix(static_cast<int64_t>(mesh.positions.size()));
    for (const auto& p : mesh.positions) {
        mix(q(p.x));
        mix(q(p.y));
        mix(q(p.z));
    }
    for (const auto& t : mesh.triangles) mix((static_cast<int64_t>(t[0]) << 40) ^ (static_cast<int64_t>(t[1]) << 20) ^ t[2]);
    for (const auto& a : mesh.attributes) {
        mix(q(a.infection));
        mix(q(a.glow));
        mix(q(a.crack));
        mix(q(a.arrival));
    }
    return h;
}

}  // namespace eclipse
