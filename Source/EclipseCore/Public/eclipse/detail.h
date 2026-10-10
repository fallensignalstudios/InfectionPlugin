#pragma once

#include "eclipse/api.h"

#include <cstdint>
#include <vector>

namespace eclipse {

// Fine surface detail too small to model, for the materials to tile over
// the generated meshes: the scales, ridges and pits of carapace, and the
// folds, pores and veinlets of soft tissue.
enum class DetailKind { Chitin, Flesh };

// A square, seamlessly tiling map, row-major RGBA bytes:
//   R, G  slope of the height along u and v, 128 = flat. Slopes rather than
//         a normal, so the material can apply them in any projection.
//   B     height, 0 lowest
//   A     cavity: 255 on ridges and open surface, darker down in grooves
struct DetailMap {
    int size = 0;
    std::vector<uint8_t> rgba;
};

ECLIPSECORE_API DetailMap makeDetailMap(DetailKind kind, int size, uint32_t seed = 1);

}  // namespace eclipse
