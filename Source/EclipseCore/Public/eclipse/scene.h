#pragma once

#include "eclipse/api.h"

#include <array>
#include <cstdint>
#include <vector>

#include "eclipse/corruption.h"
#include "eclipse/growths.h"
#include "eclipse/mesh.h"

namespace eclipse {

// A box or sphere that shapes where the corruption goes, placed by a level
// designer. Its effect is full inside the shape and fades to nothing over
// `falloff` outside it.
struct MaskVolume {
    enum class Mode {
        Block,  // the corruption never enters (a walkway, a door, gameplay space)
        Bare,   // the surface may corrupt but nothing grows on it (no horns in a corridor)
        Dense,  // the corruption is drawn here first and grows thickest
    };
    enum class Shape { Box, Sphere };

    Mode mode = Mode::Block;
    Shape shape = Shape::Box;
    Vec3 centre{0, 0, 0};
    Vec3 axes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};  // unit axes of the box
    Vec3 halfExtents{0.5f, 0.5f, 0.5f};                // the sphere's radius is halfExtents.x
    float falloff = 0.0f;
    float strength = 1.0f;

    // 0..strength at p.
    ECLIPSECORE_API float weightAt(const Vec3& p) const;
};

// Writes the volumes into the guide's per-vertex masks (taking the strongest
// where they overlap).
ECLIPSECORE_API void applyMaskVolumes(const Mesh& mesh, const std::vector<MaskVolume>& volumes, GrowthGuide& guide);

// Links each vertex to the nearest vertex of a different piece (owner)
// within `gap`, so a spread crosses between separate meshes that touch.
ECLIPSECORE_API std::vector<std::array<uint32_t, 2>> contactLinks(const Mesh& mesh, const std::vector<uint32_t>& owner,
                                                                  float gap);

// One growth over several meshes as if they were one surface: a modular
// corridor of floor, wall and prop pieces. The pieces must share a frame and
// carry normals. `owner` names the piece each surface vertex came from, in
// the order given; growths and soft tissue are not split.
struct SceneGrowth {
    Growth growth;
    std::vector<uint32_t> owner;
};

ECLIPSECORE_API SceneGrowth growScene(const std::vector<const Mesh*>& pieces, const std::vector<Vec3>& seeds,
                                      const GrowthParams& params, const FeatureParams& features, float contactGap,
                                      const std::vector<MaskVolume>& masks = {});

// The triangles of `mesh` whose vertices all belong to `which`, compacted.
// sourceVertex, if given, receives each kept vertex's index in `mesh`.
ECLIPSECORE_API Mesh subMesh(const Mesh& mesh, const std::vector<uint32_t>& owner, uint32_t which,
                             std::vector<uint32_t>* sourceVertex = nullptr);

}  // namespace eclipse
