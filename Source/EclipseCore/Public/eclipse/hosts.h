#pragma once

#include "eclipse/api.h"

#include <string>
#include <vector>

#include "eclipse/mesh.h"
#include "eclipse/sdf.h"

namespace eclipse {

// Built-in test hosts, so the generators can be exercised and previewed
// without any art. Units are metres, +Y up, the front faces +Z (glTF axes).
struct BuiltinHost {
    std::string name;
    Mesh mesh;
    std::vector<Vec3> defaultSeeds;  // where corruption starts if none are given
    // For a host built from separate meshes (the "kit"), each piece on its
    // own; `mesh` then holds them all together. Empty otherwise.
    std::vector<Mesh> pieces;
};

// A neutral humanoid in T-pose, about 1.8 m tall.
ECLIPSECORE_API SdfModel mannequinModel();
// A corner of a corridor: floor, two walls, a pipe run, a crate and a column.
ECLIPSECORE_API SdfModel corridorModel();

// "mannequin", "corridor", or "kit": the same corridor as a modular kit,
// each part a separate mesh that only touches its neighbours, as level
// pieces do.
ECLIPSECORE_API BuiltinHost makeBuiltinHost(const std::string& name, float cellSize);
ECLIPSECORE_API std::vector<std::string> builtinHostNames();

}  // namespace eclipse
