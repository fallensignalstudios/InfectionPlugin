#pragma once

#include "eclipse/api.h"

#include <string>
#include <vector>

#include "eclipse/mesh.h"

namespace eclipse {

// Binary glTF 2.0 (.glb), the format Unreal imports natively.
//
// Vertex data layout, read by the Eclipse master material:
//   COLOR_0     r = infection, g = glow, b = crack, a = plate tint
//   TEXCOORD_0  u = arrival (0 at the seed, 1 at the current front, up to 4),
//               v = surface height added, in metres
// glTF is +Y up in metres; Unreal's importer converts to +Z up centimetres.
ECLIPSECORE_API bool writeGlb(const std::string& path, const Mesh& mesh, std::string* error = nullptr);

// Several meshes in one file, one node each (for example the corrupted
// surface and the growths on it). Empty meshes are skipped.
struct NamedMesh {
    std::string name;
    const Mesh* mesh;
};
ECLIPSECORE_API bool writeGlb(const std::string& path, const std::vector<NamedMesh>& meshes, std::string* error = nullptr);

}  // namespace eclipse
