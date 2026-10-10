#pragma once

#include "eclipse/api.h"

#include <cstdint>
#include <vector>

#include "eclipse/corruption.h"
#include "eclipse/mesh.h"
#include "eclipse/spatial.h"

namespace eclipse {

// Geometry grown on top of the corrupted surface. Lengths are for a
// human-scale host; `scale` multiplies every length and spacing at once
// (about 2 suits architecture).
struct FeatureParams {
    bool enabled = true;                // false: corrupt the surface only
    float scale = 1.0f;

    bool veinTubes = true;
    float veinTubeMinRadius = 0.0015f;  // thinner veins stay painted only

    float spikeSpacing = 0.06f;
    float spikeLength = 0.075f;
    float spikeSweep = 0.6f;            // how far spikes lean along the growth direction
    float spikeRidgeShare = 0.25f;      // only the most convex share of the carapace sprouts spikes

    float noduleSpacing = 0.11f;
    float noduleRadius = 0.022f;
    float noduleChance = 0.7f; 

    float strandSpacing = 0.035f;
    float strandLength = 0.12f;
    float strandRadius = 0.0022f;
    float strandGlowChance = 0.3f;

    float bridgeSpacing = 0.06f;
    float bridgeMinGap = 0.04f;
    float bridgeMaxGap = 0.3f;
    float bridgeChance = 0.7f;
    float membraneChance = 0.45f;       // share of bridges that grow a torn membrane
    float membraneHoles = 0.35f;        // share of membrane torn away
};

struct GrowthStats {
    size_t veinTubes = 0, spikes = 0, nodules = 0, strands = 0, bridges = 0, membranes = 0;
};

// Everything one growth produced. `surface` is the host, displaced and
// carrying corruption attributes; `growths` is the added geometry.
struct Growth {
    Mesh surface;
    Mesh growths;
    SpreadField spread;
    VeinNetwork veins;
    PlateLayout plates;
    GrowthStats stats;
};

ECLIPSECORE_API Growth grow(const Mesh& host, const std::vector<Vec3>& seeds, const GrowthParams& params,
            const FeatureParams& features);

// Is p inside the closed surface, judged from the nearest vertex's normal?
// Cheap and good enough to keep growths from passing through a body.
class InsideTest {
public:
    explicit ECLIPSECORE_API InsideTest(const Mesh& surface, float searchRadius);
    ECLIPSECORE_API bool inside(const Vec3& p) const;

private:
    const Mesh& surface_;
    float radius_;
    PointGrid grid_;
};

}  // namespace eclipse
