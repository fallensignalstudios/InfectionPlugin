#pragma once

#include "eclipse/api.h"

#include <string>
#include <vector>

#include "eclipse/corruption.h"
#include "eclipse/growths.h"

namespace eclipse {

// Named looks: each sets what grows and how, starting from the defaults.
// The seed, coverage, gravity and feature scale are left as they were, so a
// preset can be tried on a host without losing its framing.
enum class Preset {
    Eclipse,     // the default: carapace, roots, horns, hearts and maws
    Creep,       // early infestation: veins and roots feeling ahead, a long halo, little else
    Thornfield,  // a bed of scythe horns and barbs
    Rootmass,    // thick knotted roots over everything, vertebrae along the trunks
    Hive,        // sacs, pods, eyes, membranes, shelves and slime: a nest
    Carcass,     // ribcages and bone, hanging strands, bruised flesh
    Cathedral,   // set pieces for a room: spires, ribcages, shelves, great maws
};

ECLIPSECORE_API void applyPreset(Preset preset, GrowthParams& params, FeatureParams& features);

ECLIPSECORE_API std::vector<std::string> presetNames();
// Case-insensitive; false if there is no such preset.
ECLIPSECORE_API bool presetFromName(const std::string& name, Preset& out);

}  // namespace eclipse
