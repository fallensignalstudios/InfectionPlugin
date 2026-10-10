#pragma once

#include "eclipse/api.h"

#include <cstdint>
#include <vector>

#include "eclipse/mesh.h"

namespace eclipse {

// Every knob of one growth. Distances are in metres. The defaults are tuned
// for a human-scale host at about 6 mm mesh resolution.
struct GrowthParams {
    uint32_t seed = 1;

    // Spread ---------------------------------------------------------------
    float coverage = 0.4f;       // fraction of the reachable surface overtaken, 0..1
    float frontWidth = 0.18f;    // softness of the leading edge, relative to its reach
    float gravity = 0.0f;        // >0 creeps upward more easily, <0 pools downward
    float noiseFrequency = 6.0f; // cycles per metre of the irregular spread front
    float noiseStrength = 1.3f;  // how fingered and uneven the front is
    float cavityBias = 0.6f;     // preference for creases and hollows over ridges

    // Veins ----------------------------------------------------------------
    float veinReach = 0.35f;     // how far veins run ahead of the front, relative
    float veinMinFlow = 0.004f;  // drained area (m^2) before a path becomes a vein
    float veinWidth = 0.0018f;   // radius of the thinnest vein
    float veinMaxWidth = 0.008f;
    float veinHeight = 0.004f;   // how far a vein lifts off clean tissue
    float coreRadius = 0.05f;    // glowing heart around each seed

    // Carapace -------------------------------------------------------------
    float plateSize = 0.085f;    // mean spacing between plate centres
    float plateStretch = 2.5f;   // plates elongate along the spread into sinew strands
    float plateThickness = 0.014f;
    float seamWidth = 0.004f;    // half-width of the crack between plates
    float seamGlow = 0.7f;       // how many seams leak light, 0..1
    float swell = 0.006f;        // general thickening of corrupted tissue
    float roughness = 0.0025f;   // amplitude of the knotted surface detail
};

// The spread over one mesh before any displacement. Kept separate so tools
// and tests can inspect it.
struct SpreadField {
    std::vector<float> distance;    // weighted geodesic distance from the nearest seed
    std::vector<int32_t> parent;    // shortest-path tree; -1 at seeds and unreachable
    std::vector<uint32_t> seedVertices;
    float threshold = 0;            // distance the front has reached
    std::vector<float> infection;   // 0..1
};

// One unbranched run of vein between junctions, leaves or a seed, smoothed
// off the mesh's vertex grid. Ordered from the far end toward the seed.
struct VeinPath {
    std::vector<uint32_t> vertices;  // host vertex each point started from
    std::vector<Vec3> points;
    std::vector<Vec3> normals;      // surface normal under each point
    std::vector<float> radius;
    std::vector<float> strength;    // 1 inside the corruption, dimmer for scouts ahead of it
};

struct VeinNetwork {
    std::vector<float> flow;        // drained area through each vertex, m^2
    std::vector<float> mask;        // 0..1 on and around veins
    std::vector<float> radius;      // radius of the vein covering each vertex
    size_t veinVertexCount = 0;     // vertices on a vein centreline
    std::vector<VeinPath> paths;
};

struct PlateLayout {
    std::vector<int32_t> plate;     // plate id per vertex, -1 outside the carapace
    std::vector<float> seamDistance;
    std::vector<float> siteDistance;
    size_t plateCount = 0;
};

ECLIPSECORE_API uint32_t nearestVertex(const Mesh& mesh, const Vec3& p);

ECLIPSECORE_API SpreadField computeSpread(const Mesh& host, const VertexGraph& graph, const std::vector<Vec3>& seeds,
                          const GrowthParams& params);
ECLIPSECORE_API VeinNetwork computeVeins(const Mesh& host, const SpreadField& spread,
                         const GrowthParams& params);
ECLIPSECORE_API PlateLayout computePlates(const Mesh& host, const VertexGraph& graph, const SpreadField& spread,
                          const GrowthParams& params);

// The corrupted copy of the host (displaced positions, fresh normals and
// per-vertex corruption attributes) with the fields that produced it.
struct SurfaceGrowth {
    Mesh surface;
    SpreadField spread;
    VeinNetwork veins;
    PlateLayout plates;
};

ECLIPSECORE_API SurfaceGrowth growSurface(const Mesh& host, const std::vector<Vec3>& seeds, const GrowthParams& params);

// Just the corrupted surface; see growSurface.
ECLIPSECORE_API Mesh corrupt(const Mesh& host, const std::vector<Vec3>& seeds, const GrowthParams& params);

// The part of a corrupted surface that differs from the clean host: every
// triangle touching infection or glow, lifted `offset` along the normals so
// it sits just above the original. Rendered over the untouched host with a
// masked material, the host keeps its own materials everywhere else. If
// sourceVertex is given it receives the surface vertex each shell vertex
// came from.
ECLIPSECORE_API Mesh extractShell(const Mesh& surface, float offset, std::vector<uint32_t>* sourceVertex = nullptr);

// A sparse record of where the corruption is and when it arrived, small
// enough to keep at runtime for gameplay queries ("how infected is this
// spot once the growth has played this far?").
struct InfectionSample {
    Vec3 position;
    float arrival = 0;    // as CorruptionAttributes::arrival
    float infection = 0;  // the strongest of infection and glow nearby
};

// Poisson-thinned samples of every vertex the corruption touches, at least
// `spacing` apart, each carrying the earliest arrival and strongest
// infection of the vertices it stands for.
ECLIPSECORE_API std::vector<InfectionSample> sampleInfection(const Mesh& surface, float spacing);

// Stable fingerprint of a mesh's geometry and attributes, for regression tests.
ECLIPSECORE_API uint64_t meshHash(const Mesh& mesh);

}  // namespace eclipse
