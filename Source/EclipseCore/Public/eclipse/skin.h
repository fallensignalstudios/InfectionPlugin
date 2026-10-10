#pragma once

#include "eclipse/api.h"

#include <array>
#include <cstdint>
#include <vector>

#include "eclipse/mesh.h"

namespace eclipse {

// Bone influences of one vertex, strongest first, weights summing to 1.
// Bone indices are the host skeleton's; the core never interprets them.
struct SkinWeights {
    static constexpr int kMaxInfluences = 8;
    std::array<uint16_t, kMaxInfluences> bone{};
    std::array<float, kMaxInfluences> weight{};
    uint8_t count = 0;

    static SkinWeights single(uint16_t boneIndex) {
        SkinWeights w;
        w.bone[0] = boneIndex;
        w.weight[0] = 1.0f;
        w.count = 1;
        return w;
    }
};

using SkinTable = std::vector<SkinWeights>;

// Weighted mix of several vertices' influences: the same bone's weights add
// up, the strongest kMaxInfluences are kept, and the result is renormalised.
ECLIPSECORE_API SkinWeights blendSkin(const SkinWeights* const* inputs, const float* factors, size_t count);

// Carries a host's skinning through the mesh operations that prepare it, so
// every prepared vertex has weights. They take the provenance those
// operations report (weldVertices' remap, subdivideToEdgeLength's parents,
// extractShell's source vertices).
ECLIPSECORE_API SkinTable skinAfterWeld(const SkinTable& before, const std::vector<uint32_t>& remap, size_t weldedCount);
ECLIPSECORE_API SkinTable skinAfterSubdivision(SkinTable base, const std::vector<std::array<uint32_t, 2>>& parents);
ECLIPSECORE_API SkinTable skinFromSources(const SkinTable& skin, const std::vector<uint32_t>& sources);

// Skins free-standing geometry (growths) from the host tissue nearest each
// point: an inverse-square-distance blend of up to `neighbours` host
// vertices. A strand follows the spot it hangs from; a bridge or membrane
// between two limbs blends toward whichever it is nearer, so it stretches
// as they move apart.
ECLIPSECORE_API SkinTable bindToNearest(const std::vector<Vec3>& hostPositions, const SkinTable& hostSkin,
                                        const std::vector<Vec3>& points, int neighbours = 4);

}  // namespace eclipse
