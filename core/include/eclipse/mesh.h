#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "eclipse/math.h"

namespace eclipse {

// Per-vertex corruption attributes. These travel with the mesh into the
// engine (glTF COLOR_0 / TEXCOORD_0) and drive the Eclipse master material.
struct CorruptionAttributes {
    float infection = 0;  // 0 = clean host tissue, 1 = fully overtaken
    float glow = 0;       // violet emissive: veins and core light
    float crack = 0;      // seams between carapace plates (also emissive)
    float arrival = 1;    // normalized time the front reached this vertex; >1 never
    float plateTint = 0;  // per-plate random, for colour variation
    float height = 0;     // how far the surface was pushed out, in metres
};

struct Mesh {
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<std::array<uint32_t, 3>> triangles;
    // Empty for an uncorrupted mesh; otherwise one entry per vertex.
    std::vector<CorruptionAttributes> attributes;

    size_t vertexCount() const { return positions.size(); }
    size_t triangleCount() const { return triangles.size(); }

    void computeNormals();
    void bounds(Vec3& lo, Vec3& hi) const;
    // Appends another mesh, offsetting its indices. Attributes are kept only
    // when both meshes carry them (or this mesh is empty).
    void append(const Mesh& other);
};

// Compressed adjacency: neighbours of vertex v are
// neighbours[offsets[v] .. offsets[v+1]).
struct VertexGraph {
    std::vector<uint32_t> offsets;
    std::vector<uint32_t> neighbours;

    static VertexGraph build(const Mesh& mesh);
    // Neighbours and neighbours-of-neighbours. Shortest paths over this graph
    // can turn in 16 directions instead of 4 on a grid-like mesh, which
    // removes the axis-aligned "circuit board" look from spread and veins.
    VertexGraph twoRing() const;
    size_t degree(uint32_t v) const { return offsets[v + 1] - offsets[v]; }
};

// One third of the area of every incident triangle.
std::vector<float> vertexAreas(const Mesh& mesh);

// Signed mean-curvature estimate per vertex: positive on ridges and convex
// bumps, negative in creases and hollows. Units are 1/metre.
std::vector<float> vertexCurvature(const Mesh& mesh, const VertexGraph& graph);

// Wavefront OBJ. Reading triangulates polygons and ignores everything but
// positions and faces; writing emits per-vertex colours as the common
// "v x y z r g b" extension (infection, glow, crack).
bool readObj(const std::string& path, Mesh& out, std::string* error = nullptr);
bool writeObj(const std::string& path, const Mesh& mesh);

}  // namespace eclipse
