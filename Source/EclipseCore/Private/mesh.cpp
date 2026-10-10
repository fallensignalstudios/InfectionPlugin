#include "eclipse/mesh.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <queue>
#include <sstream>
#include <unordered_map>

namespace eclipse {

void Mesh::computeNormals() {
    normals.assign(positions.size(), Vec3{0, 0, 0});
    for (const auto& t : triangles) {
        const Vec3& a = positions[t[0]];
        const Vec3& b = positions[t[1]];
        const Vec3& c = positions[t[2]];
        Vec3 n = cross(b - a, c - a);  // area-weighted
        normals[t[0]] += n;
        normals[t[1]] += n;
        normals[t[2]] += n;
    }
    for (auto& n : normals) n = normalize(n);
}

void Mesh::bounds(Vec3& lo, Vec3& hi) const {
    lo = Vec3{1e30f, 1e30f, 1e30f};
    hi = Vec3{-1e30f, -1e30f, -1e30f};
    for (const auto& p : positions) {
        lo = vmin(lo, p);
        hi = vmax(hi, p);
    }
}

void Mesh::append(const Mesh& other) {
    bool keepAttributes = (positions.empty() || attributes.size() == positions.size()) &&
                          other.attributes.size() == other.positions.size();
    uint32_t base = static_cast<uint32_t>(positions.size());
    positions.insert(positions.end(), other.positions.begin(), other.positions.end());
    if (normals.size() + other.normals.size() == positions.size()) {
        normals.insert(normals.end(), other.normals.begin(), other.normals.end());
    } else {
        normals.clear();
    }
    if (keepAttributes) {
        attributes.insert(attributes.end(), other.attributes.begin(), other.attributes.end());
    } else {
        attributes.clear();
    }
    for (const auto& t : other.triangles) triangles.push_back({t[0] + base, t[1] + base, t[2] + base});
}

VertexGraph VertexGraph::build(const Mesh& mesh) {
    const size_t n = mesh.positions.size();
    std::vector<std::vector<uint32_t>> lists(n);
    for (const auto& t : mesh.triangles) {
        for (int i = 0; i < 3; ++i) {
            uint32_t a = t[i], b = t[(i + 1) % 3];
            lists[a].push_back(b);
            lists[b].push_back(a);
        }
    }
    VertexGraph g;
    g.offsets.resize(n + 1, 0);
    for (size_t v = 0; v < n; ++v) {
        auto& l = lists[v];
        std::sort(l.begin(), l.end());
        l.erase(std::unique(l.begin(), l.end()), l.end());
        g.offsets[v + 1] = g.offsets[v] + static_cast<uint32_t>(l.size());
    }
    g.neighbours.reserve(g.offsets[n]);
    for (auto& l : lists) g.neighbours.insert(g.neighbours.end(), l.begin(), l.end());
    return g;
}

VertexGraph VertexGraph::twoRing() const {
    const size_t n = offsets.empty() ? 0 : offsets.size() - 1;
    VertexGraph g;
    g.offsets.resize(n + 1, 0);
    std::vector<uint32_t> ring;
    for (uint32_t v = 0; v < n; ++v) {
        ring.clear();
        for (uint32_t i = offsets[v]; i < offsets[v + 1]; ++i) {
            uint32_t a = neighbours[i];
            ring.push_back(a);
            for (uint32_t j = offsets[a]; j < offsets[a + 1]; ++j)
                if (neighbours[j] != v) ring.push_back(neighbours[j]);
        }
        std::sort(ring.begin(), ring.end());
        ring.erase(std::unique(ring.begin(), ring.end()), ring.end());
        g.neighbours.insert(g.neighbours.end(), ring.begin(), ring.end());
        g.offsets[v + 1] = static_cast<uint32_t>(g.neighbours.size());
    }
    return g;
}

void weldVertices(Mesh& mesh, float tolerance) {
    const size_t n = mesh.positions.size();
    const float cell = std::max(tolerance, 1e-9f);
    auto key = [](int64_t x, int64_t y, int64_t z) {
        return (static_cast<uint64_t>(x & 0x1FFFFF) << 42) | (static_cast<uint64_t>(y & 0x1FFFFF) << 21) |
               static_cast<uint64_t>(z & 0x1FFFFF);
    };
    std::unordered_map<uint64_t, std::vector<uint32_t>> grid;
    std::vector<uint32_t> remap(n);
    std::vector<Vec3> kept;
    for (uint32_t v = 0; v < n; ++v) {
        const Vec3& p = mesh.positions[v];
        int64_t cx = static_cast<int64_t>(std::floor(p.x / cell)), cy = static_cast<int64_t>(std::floor(p.y / cell)),
                cz = static_cast<int64_t>(std::floor(p.z / cell));
        int32_t match = -1;
        for (int64_t dx = -1; dx <= 1 && match < 0; ++dx)
            for (int64_t dy = -1; dy <= 1 && match < 0; ++dy)
                for (int64_t dz = -1; dz <= 1 && match < 0; ++dz) {
                    auto it = grid.find(key(cx + dx, cy + dy, cz + dz));
                    if (it == grid.end()) continue;
                    for (uint32_t k : it->second)
                        if (length(kept[k] - p) <= tolerance) {
                            match = static_cast<int32_t>(k);
                            break;
                        }
                }
        if (match < 0) {
            match = static_cast<int32_t>(kept.size());
            kept.push_back(p);
            grid[key(cx, cy, cz)].push_back(static_cast<uint32_t>(match));
        }
        remap[v] = static_cast<uint32_t>(match);
    }
    std::vector<std::array<uint32_t, 3>> triangles;
    for (const auto& t : mesh.triangles) {
        std::array<uint32_t, 3> r{remap[t[0]], remap[t[1]], remap[t[2]]};
        if (r[0] != r[1] && r[1] != r[2] && r[0] != r[2]) triangles.push_back(r);
    }
    mesh.positions = std::move(kept);
    mesh.triangles = std::move(triangles);
    mesh.normals.clear();
    mesh.attributes.clear();
}

bool subdivideToEdgeLength(Mesh& mesh, float maxEdge, size_t maxVertices) {
    if (maxEdge <= 0.0f) return false;
    auto edgeKey = [](uint32_t a, uint32_t b) {
        return a < b ? (static_cast<uint64_t>(a) << 32) | b : (static_cast<uint64_t>(b) << 32) | a;
    };
    // Estimate first, so an absurd request fails fast instead of eating memory:
    // each long edge of length L becomes about L/maxEdge pieces, and a
    // triangle's vertex count grows with the square of that.
    double estimate = static_cast<double>(mesh.positions.size());
    for (const auto& t : mesh.triangles) {
        float l = std::max({length(mesh.positions[t[1]] - mesh.positions[t[0]]),
                            length(mesh.positions[t[2]] - mesh.positions[t[1]]),
                            length(mesh.positions[t[0]] - mesh.positions[t[2]])});
        double k = std::ceil(l / maxEdge);
        estimate += 0.5 * k * k;
    }
    if (estimate > static_cast<double>(maxVertices)) return false;

    std::vector<Vec3> positions = mesh.positions;
    std::vector<std::array<uint32_t, 3>> triangles = mesh.triangles;
    std::unordered_map<uint64_t, std::vector<uint32_t>> edgeTriangles;
    auto addEdge = [&](uint32_t a, uint32_t b, uint32_t t) { edgeTriangles[edgeKey(a, b)].push_back(t); };
    auto removeEdgeTriangle = [&](uint32_t a, uint32_t b, uint32_t t) {
        auto it = edgeTriangles.find(edgeKey(a, b));
        if (it == edgeTriangles.end()) return;
        auto& list = it->second;
        list.erase(std::remove(list.begin(), list.end(), t), list.end());
        if (list.empty()) edgeTriangles.erase(it);
    };
    for (uint32_t t = 0; t < triangles.size(); ++t)
        for (int i = 0; i < 3; ++i) addEdge(triangles[t][i], triangles[t][(i + 1) % 3], t);

    // Longest edge first keeps the triangles well shaped.
    using Item = std::pair<float, uint64_t>;
    std::priority_queue<Item> queue;
    auto consider = [&](uint32_t a, uint32_t b) {
        float l = length(positions[a] - positions[b]);
        if (l > maxEdge) queue.push({l, edgeKey(a, b)});
    };
    for (const auto& [k, list] : edgeTriangles) consider(static_cast<uint32_t>(k >> 32), static_cast<uint32_t>(k));

    while (!queue.empty()) {
        uint64_t k = queue.top().second;
        queue.pop();
        auto it = edgeTriangles.find(k);
        if (it == edgeTriangles.end()) continue;  // already split
        const uint32_t a = static_cast<uint32_t>(k >> 32), b = static_cast<uint32_t>(k);
        if (positions.size() >= maxVertices) return false;
        const uint32_t m = static_cast<uint32_t>(positions.size());
        positions.push_back((positions[a] + positions[b]) * 0.5f);
        std::vector<uint32_t> touching = it->second;
        edgeTriangles.erase(it);
        for (uint32_t t : touching) {
            // Rotate so the split edge is (p, q) in the triangle's own winding.
            auto tri = triangles[t];
            while (!((tri[0] == a && tri[1] == b) || (tri[0] == b && tri[1] == a)))
                tri = {tri[1], tri[2], tri[0]};
            const uint32_t p = tri[0], q = tri[1], r = tri[2];
            const uint32_t added = static_cast<uint32_t>(triangles.size());
            triangles[t] = {p, m, r};
            triangles.push_back({m, q, r});
            removeEdgeTriangle(q, r, t);
            addEdge(q, r, added);
            addEdge(p, m, t);
            addEdge(m, q, added);
            addEdge(m, r, t);
            addEdge(m, r, added);
            consider(m, r);
        }
        consider(a, m);
        consider(m, b);
    }
    mesh.positions = std::move(positions);
    mesh.triangles = std::move(triangles);
    mesh.attributes.clear();
    mesh.computeNormals();
    return true;
}

std::vector<float> vertexAreas(const Mesh& mesh) {
    std::vector<float> area(mesh.positions.size(), 0.0f);
    for (const auto& t : mesh.triangles) {
        float a = 0.5f * length(cross(mesh.positions[t[1]] - mesh.positions[t[0]],
                                      mesh.positions[t[2]] - mesh.positions[t[0]]));
        for (int i = 0; i < 3; ++i) area[t[i]] += a / 3.0f;
    }
    return area;
}

std::vector<float> vertexCurvature(const Mesh& mesh, const VertexGraph& graph) {
    // Umbrella Laplacian projected on the normal, scaled by the mean edge
    // length squared: a cheap but stable mean-curvature proxy.
    const size_t n = mesh.positions.size();
    std::vector<float> curvature(n, 0.0f);
    for (uint32_t v = 0; v < n; ++v) {
        size_t deg = graph.degree(v);
        if (deg == 0) continue;
        Vec3 centroid{0, 0, 0};
        float meanEdge = 0.0f;
        for (uint32_t i = graph.offsets[v]; i < graph.offsets[v + 1]; ++i) {
            const Vec3& q = mesh.positions[graph.neighbours[i]];
            centroid += q;
            meanEdge += length(q - mesh.positions[v]);
        }
        centroid = centroid / static_cast<float>(deg);
        meanEdge /= static_cast<float>(deg);
        if (meanEdge <= 0) continue;
        Vec3 lap = mesh.positions[v] - centroid;  // points outward on a convex bump
        curvature[v] = 4.0f * dot(lap, mesh.normals[v]) / (meanEdge * meanEdge);
    }
    return curvature;
}

bool readObj(const std::string& path, Mesh& out, std::string* error) {
    std::ifstream in(path);
    if (!in) {
        if (error) *error = "cannot open " + path;
        return false;
    }
    out = Mesh{};
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ss(line);
        std::string tag;
        ss >> tag;
        if (tag == "v") {
            Vec3 p;
            ss >> p.x >> p.y >> p.z;
            out.positions.push_back(p);
        } else if (tag == "f") {
            std::vector<uint32_t> poly;
            std::string tok;
            while (ss >> tok) {
                long idx = std::strtol(tok.c_str(), nullptr, 10);  // stops at '/'
                if (idx < 0) idx = static_cast<long>(out.positions.size()) + idx + 1;
                if (idx <= 0 || static_cast<size_t>(idx) > out.positions.size()) {
                    if (error) *error = "face index out of range in " + path;
                    return false;
                }
                poly.push_back(static_cast<uint32_t>(idx - 1));
            }
            for (size_t i = 2; i < poly.size(); ++i) out.triangles.push_back({poly[0], poly[i - 1], poly[i]});
        }
    }
    if (out.positions.empty() || out.triangles.empty()) {
        if (error) *error = "no geometry in " + path;
        return false;
    }
    out.computeNormals();
    return true;
}

bool writeObj(const std::string& path, const Mesh& mesh) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    char line[160];
    auto put = [&](int written) { f.write(line, std::min<int>(written, static_cast<int>(sizeof line) - 1)); };
    f << "# eclipse-biomass\n";
    bool colours = mesh.attributes.size() == mesh.positions.size();
    for (size_t i = 0; i < mesh.positions.size(); ++i) {
        const Vec3& p = mesh.positions[i];
        if (colours) {
            const auto& a = mesh.attributes[i];
            put(std::snprintf(line, sizeof line, "v %.6f %.6f %.6f %.4f %.4f %.4f\n", p.x, p.y, p.z, a.infection, a.glow,
                              a.crack));
        } else {
            put(std::snprintf(line, sizeof line, "v %.6f %.6f %.6f\n", p.x, p.y, p.z));
        }
    }
    if (mesh.normals.size() == mesh.positions.size()) {
        for (const auto& n : mesh.normals) put(std::snprintf(line, sizeof line, "vn %.5f %.5f %.5f\n", n.x, n.y, n.z));
        for (const auto& t : mesh.triangles)
            put(std::snprintf(line, sizeof line, "f %u//%u %u//%u %u//%u\n", t[0] + 1, t[0] + 1, t[1] + 1, t[1] + 1,
                              t[2] + 1, t[2] + 1));
    } else {
        for (const auto& t : mesh.triangles)
            put(std::snprintf(line, sizeof line, "f %u %u %u\n", t[0] + 1, t[1] + 1, t[2] + 1));
    }
    f.close();
    return !f.fail();
}

}  // namespace eclipse
