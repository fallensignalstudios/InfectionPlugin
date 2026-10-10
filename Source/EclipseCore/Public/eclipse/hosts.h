#pragma once

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
};

// A neutral humanoid in T-pose, about 1.8 m tall.
SdfModel mannequinModel();
// A corner of a corridor: floor, two walls, a pipe run, a crate and a column.
SdfModel corridorModel();

BuiltinHost makeBuiltinHost(const std::string& name, float cellSize);
std::vector<std::string> builtinHostNames();

}  // namespace eclipse
