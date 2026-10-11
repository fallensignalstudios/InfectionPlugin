#include "eclipse/scene.h"

#include <algorithm>
#include <cmath>

#include "eclipse/spatial.h"

namespace eclipse {

float MaskVolume::weightAt(const Vec3& p) const {
    const Vec3 d = p - centre;
    float outside;  // distance outside the shape, <= 0 inside
    if (shape == Shape::Sphere) {
        outside = length(d) - halfExtents.x;
    } else {
        Vec3 q{std::fabs(dot(d, axes[0])) - halfExtents.x, std::fabs(dot(d, axes[1])) - halfExtents.y,
               std::fabs(dot(d, axes[2])) - halfExtents.z};
        Vec3 out{std::max(q.x, 0.0f), std::max(q.y, 0.0f), std::max(q.z, 0.0f)};
        outside = length(out) + std::min(std::max(q.x, std::max(q.y, q.z)), 0.0f);
    }
    if (outside <= 0.0f) return strength;
    if (falloff <= 0.0f) return 0.0f;
    return strength * (1.0f - smoothstep(0.0f, falloff, outside));
}

void applyMaskVolumes(const Mesh& mesh, const std::vector<MaskVolume>& volumes, GrowthGuide& guide) {
    const size_t n = mesh.positions.size();
    for (const MaskVolume& volume : volumes) {
        std::vector<float>& field = volume.mode == MaskVolume::Mode::Block  ? guide.block
                                  : volume.mode == MaskVolume::Mode::Bare ? guide.bare
                                                                          : guide.dense;
        if (field.size() != n) field.assign(n, 0.0f);
        for (size_t v = 0; v < n; ++v) field[v] = std::max(field[v], clamp01(volume.weightAt(mesh.positions[v])));
    }
}

std::vector<std::array<uint32_t, 2>> contactLinks(const Mesh& mesh, const std::vector<uint32_t>& owner, float gap) {
    std::vector<std::array<uint32_t, 2>> links;
    if (gap <= 0.0f || owner.size() != mesh.positions.size()) return links;
    PointGrid grid(mesh.positions, gap);
    for (uint32_t v = 0; v < mesh.positions.size(); ++v) {
        int32_t best = -1;
        float bestD = gap;
        for (uint32_t w : grid.within(mesh.positions[v], gap)) {
            if (owner[w] == owner[v]) continue;
            float d = length(mesh.positions[w] - mesh.positions[v]);
            if (d <= bestD) {
                bestD = d;
                best = static_cast<int32_t>(w);
            }
        }
        // Each pair once, from the lower index.
        if (best > static_cast<int32_t>(v)) links.push_back({v, static_cast<uint32_t>(best)});
        else if (best >= 0) links.push_back({static_cast<uint32_t>(best), v});
    }
    std::sort(links.begin(), links.end());
    links.erase(std::unique(links.begin(), links.end()), links.end());
    return links;
}

SceneGrowth growScene(const std::vector<const Mesh*>& pieces, const std::vector<Vec3>& seeds, const GrowthParams& params,
                      const FeatureParams& features, float contactGap, const std::vector<MaskVolume>& masks) {
    SceneGrowth scene;
    Mesh combined;
    for (uint32_t i = 0; i < pieces.size(); ++i) {
        Mesh piece = *pieces[i];
        if (piece.normals.size() != piece.positions.size()) piece.computeNormals();
        piece.attributes.clear();
        combined.append(piece);
        scene.owner.resize(combined.positions.size(), i);
    }
    GrowthGuide guide;
    guide.links = contactLinks(combined, scene.owner, contactGap);
    applyMaskVolumes(combined, masks, guide);
    scene.growth = grow(combined, seeds, params, features, &guide);
    return scene;
}

Mesh subMesh(const Mesh& mesh, const std::vector<uint32_t>& owner, uint32_t which, std::vector<uint32_t>* sourceVertex) {
    Mesh out;
    if (sourceVertex) sourceVertex->clear();
    if (owner.size() != mesh.positions.size()) return out;
    const bool normals = mesh.normals.size() == mesh.positions.size();
    const bool attributes = mesh.attributes.size() == mesh.positions.size();
    std::vector<int64_t> remap(mesh.positions.size(), -1);
    for (const auto& t : mesh.triangles) {
        if (owner[t[0]] != which || owner[t[1]] != which || owner[t[2]] != which) continue;
        std::array<uint32_t, 3> r{};
        for (int k = 0; k < 3; ++k) {
            uint32_t i = t[k];
            if (remap[i] < 0) {
                remap[i] = static_cast<int64_t>(out.positions.size());
                out.positions.push_back(mesh.positions[i]);
                if (normals) out.normals.push_back(mesh.normals[i]);
                if (attributes) out.attributes.push_back(mesh.attributes[i]);
                if (sourceVertex) sourceVertex->push_back(i);
            }
            r[k] = static_cast<uint32_t>(remap[i]);
        }
        out.triangles.push_back(r);
    }
    return out;
}

}  // namespace eclipse
