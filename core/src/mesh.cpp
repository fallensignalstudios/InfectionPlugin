#include "eclipse/mesh.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

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
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fprintf(f, "# eclipse-biomass\n");
    bool colours = mesh.attributes.size() == mesh.positions.size();
    for (size_t i = 0; i < mesh.positions.size(); ++i) {
        const Vec3& p = mesh.positions[i];
        if (colours) {
            const auto& a = mesh.attributes[i];
            std::fprintf(f, "v %.6f %.6f %.6f %.4f %.4f %.4f\n", p.x, p.y, p.z, a.infection, a.glow, a.crack);
        } else {
            std::fprintf(f, "v %.6f %.6f %.6f\n", p.x, p.y, p.z);
        }
    }
    if (mesh.normals.size() == mesh.positions.size()) {
        for (const auto& n : mesh.normals) std::fprintf(f, "vn %.5f %.5f %.5f\n", n.x, n.y, n.z);
        for (const auto& t : mesh.triangles)
            std::fprintf(f, "f %u//%u %u//%u %u//%u\n", t[0] + 1, t[0] + 1, t[1] + 1, t[1] + 1, t[2] + 1, t[2] + 1);
    } else {
        for (const auto& t : mesh.triangles) std::fprintf(f, "f %u %u %u\n", t[0] + 1, t[1] + 1, t[2] + 1);
    }
    return std::fclose(f) == 0;
}

}  // namespace eclipse
