#pragma once

#include "eclipse/api.h"

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

    ECLIPSECORE_API void computeNormals();
    ECLIPSECORE_API void bounds(Vec3& lo, Vec3& hi) const;
    // Appends another mesh, offsetting its indices. Attributes are kept only
    // when both meshes carry them (or this mesh is empty).
    ECLIPSECORE_API void append(const Mesh& other);
};

// Compressed adjacency: neighbours of vertex v are
// neighbours[offsets[v] .. offsets[v+1]).
struct VertexGraph {
    std::vector<uint32_t> offsets;
    std::vector<uint32_t> neighbours;

    ECLIPSECORE_API static VertexGraph build(const Mesh& mesh);
    // Neighbours and neighbours-of-neighbours. Shortest paths over this graph
    // can turn in 16 directions instead of 4 on a grid-like mesh, which
    // removes the axis-aligned "circuit board" look from spread and veins.
    ECLIPSECORE_API VertexGraph twoRing() const;
    size_t degree(uint32_t v) const { return offsets[v + 1] - offsets[v]; }
};

// Merges vertices closer than `tolerance` (game meshes often split vertices
// along UV or smoothing seams, which would stop the spread at the seam).
// Drops triangles that collapse. Normals and attributes are discarded.
ECLIPSECORE_API void weldVertices(Mesh& mesh, float tolerance);

// Splits every edge longer than `maxEdge` at its midpoint, repeatedly, until
// none remain; the surface keeps its exact shape. Corruption detail lives on
// vertices, so a sparse game mesh must be refined first. Returns false and
// leaves the mesh untouched if the result would exceed `maxVertices`.
ECLIPSECORE_API bool subdivideToEdgeLength(Mesh& mesh, float maxEdge, size_t maxVertices);

// One third of the area of every incident triangle.
ECLIPSECORE_API std::vector<float> vertexAreas(const Mesh& mesh);

// Signed mean-curvature estimate per vertex: positive on ridges and convex
// bumps, negative in creases and hollows. Units are 1/metre.
ECLIPSECORE_API std::vector<float> vertexCurvature(const Mesh& mesh, const VertexGraph& graph);

// Wavefront OBJ. Reading triangulates polygons and ignores everything but
// positions and faces; writing emits per-vertex colours as the common
// "v x y z r g b" extension (infection, glow, crack).
ECLIPSECORE_API bool readObj(const std::string& path, Mesh& out, std::string* error = nullptr);
ECLIPSECORE_API bool writeObj(const std::string& path, const Mesh& mesh);

}  // namespace eclipse
