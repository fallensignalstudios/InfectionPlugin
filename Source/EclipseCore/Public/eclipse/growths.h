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

    // How much the tissue swells up around the base of each growth, so it
    // grows out of the surface instead of sitting on it. 0 turns it off.
    float fusion = 1.0f;

    // How knotted, ringed and lumpy growths are, 0 (smooth, round) to about
    // 2. Organic growth is never perfectly round.
    float roughness = 1.0f;

    bool veinTubes = true;
    float veinTubeMinRadius = 0.0015f;  // thinner veins stay painted only

    // Horns: most short, a rare few several times longer, gathered on the
    // oldest tissue and swept along the growth like scythes.
    float spikeSpacing = 0.06f;
    float spikeLength = 0.075f;         // a typical horn; giants reach about six times this
    float spikeSweep = 0.85f;           // how far horns lean along the growth direction
    int spikeBarbs = 5;                 // small thorns along a big horn's outer curve

    // Roots: ropes laid over the surface that run in toward the cores,
    // thickening as they go, and twist so they spiral around limbs and columns.
    bool tendrils = true;
    float tendrilSpacing = 0.07f;
    float tendrilLength = 0.3f;
    float tendrilRadius = 0.016f;
    float tendrilTwist = 0.5f;          // radians off the straight path
    float tendrilDensity = 0.7f;        // share of possible roots that grow
    float tendrilBranching = 0.35f;     // chance of a side rootlet every ~5 cm

    // Hearts: a glowing orb at each seed, gripped by a ring of claws; past
    // mawCoverage it opens into a maw: a crater of swollen lips around a
    // throat that glows from deep inside, ringed by teeth and a second ring
    // of claws.
    bool hearts = true;
    float heartRadius = 0.07f;
    int heartClaws = 9;
    float mawCoverage = 0.7f;

    // Vertebrae: segmented ridges along the thickest vein trunks.
    bool vertebrae = true;
    float vertebraSpacing = 0.025f;

    // Sacs: clusters of swollen, translucent pods packed into creases.
    bool sacs = true;
    float sacSpacing = 0.12f;
    float sacRadius = 0.022f;
    float sacChance = 0.5f;
    int sacCluster = 5;                 // pods in a typical cluster

    // Slime: thin beaded strings of mucus sagging from horns and claws to
    // the tissue below, and stretched across tight creases.
    bool slime = true;
    float slimeChance = 0.5f;
    float slimeRadius = 0.0008f;

    float noduleSpacing = 0.2f;
    float noduleRadius = 0.03f;
    float noduleChance = 0.45f;

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

    // Formations: large set pieces, off by default (the presets in
    // eclipse/presets.h turn them on). Sizes are at scale 1; rooms want
    // scale 2 or more.
    // Spires: columns of twisted roots rising from floors and ledges, their
    // tips splaying into claws, a glowing core showing between the strands.
    bool spires = false;
    float spireSpacing = 0.6f;
    float spireHeight = 0.7f;
    // Ribcages: a buried spine with paired ribs arching up over it, pale
    // toward their tips, like the remains of something the growth consumed.
    bool ribcages = false;
    float ribcageSpacing = 0.8f;
    float ribcageLength = 0.5f;
    // Pods: large egg-shaped cocoons, translucent, with a shape glowing
    // faintly inside and roots wound around them.
    bool pods = false;
    float podSpacing = 0.35f;
    float podSize = 0.14f;
    // Shelves: stacked bracket-fungus plates on walls, glowing gills beneath.
    bool shelves = false;
    float shelfSpacing = 0.45f;
    float shelfSize = 0.11f;
};

struct GrowthStats {
    size_t veinTubes = 0, spikes = 0, nodules = 0, strands = 0, bridges = 0, membranes = 0;
    size_t tendrils = 0, hearts = 0, vertebrae = 0;
    size_t maws = 0, sacs = 0, barbs = 0, rootlets = 0, slime = 0;
    size_t spires = 0, ribcages = 0, pods = 0, shelves = 0;
};

// Everything one growth produced. `surface` is the host, displaced and
// carrying corruption attributes; `growths` is the added hard geometry
// (horns, claws, roots, strands) and `flesh` the soft tissue (hearts,
// throats, eyes, sacs, membranes), which wants a translucent material.
struct Growth {
    Mesh surface;
    Mesh growths;
    Mesh flesh;
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
