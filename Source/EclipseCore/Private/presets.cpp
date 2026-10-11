#include "eclipse/presets.h"

#include <algorithm>
#include <cctype>

namespace eclipse {

namespace {

const char* const kNames[] = {"eclipse", "creep", "thornfield", "rootmass", "hive", "carcass", "cathedral"};

}  // namespace

void applyPreset(Preset preset, GrowthParams& params, FeatureParams& features) {
    // Start from the defaults, keeping the framing.
    GrowthParams p;
    p.seed = params.seed;
    p.coverage = params.coverage;
    p.gravity = params.gravity;
    p.plateSize = params.plateSize;  // set by the host's scale (CLI, panel)
    FeatureParams f;
    f.enabled = features.enabled;
    f.scale = features.scale;

    switch (preset) {
        case Preset::Eclipse:
            break;
        case Preset::Creep:
            p.veinReach = 0.6f;
            p.veinMinFlow = 0.004f;
            p.haloReach = 0.6f;
            p.noiseStrength = 1.8f;
            p.plateThickness *= 0.6f;
            p.bruising = 0.3f;
            f.tendrilDensity = 0.8f;
            f.tendrilBranching = 0.45f;
            f.tendrilRadius = 0.01f;
            f.tendrilLength = 0.45f;
            f.spikeSpacing = 0.15f;
            f.spikeLength = 0.04f;
            f.hearts = false;
            f.sacs = false;
            f.noduleChance = 0.15f;
            f.strandSpacing = 0.07f;
            f.bridgeChance = 0.2f;
            f.membraneChance = 0.0f;
            f.slimeChance = 0.2f;
            break;
        case Preset::Thornfield:
            f.spikeSpacing = 0.035f;
            f.spikeLength = 0.1f;
            f.spikeSweep = 0.6f;
            f.spikeBarbs = 9;
            f.tendrilDensity = 0.35f;
            f.sacs = false;
            f.noduleChance = 0.2f;
            f.strandSpacing = 0.06f;
            f.membraneChance = 0.15f;
            f.slimeChance = 0.7f;
            f.heartClaws = 13;
            break;
        case Preset::Rootmass:
            p.plateThickness *= 0.7f;
            f.tendrilSpacing = 0.055f;
            f.tendrilRadius = 0.024f;
            f.tendrilLength = 0.55f;
            f.tendrilDensity = 1.0f;
            f.tendrilBranching = 0.4f;
            f.tendrilTwist = 0.8f;
            f.vertebraSpacing = 0.02f;
            f.spikeSpacing = 0.11f;
            f.fusion = 1.5f;
            f.roughness = 1.4f;
            f.sacChance = 0.3f;
            break;
        case Preset::Hive:
            p.seamGlow = 0.2f;
            f.sacs = true;
            f.sacChance = 1.0f;
            f.sacSpacing = 0.07f;
            f.sacCluster = 8;
            f.noduleChance = 0.85f;
            f.noduleSpacing = 0.12f;
            f.pods = true;
            f.shelves = true;
            f.bridgeChance = 1.0f;
            f.membraneChance = 0.8f;
            f.membraneHoles = 0.25f;
            f.slimeChance = 1.0f;
            f.spikeSpacing = 0.1f;
            f.mawCoverage = 0.5f;
            break;
        case Preset::Carcass:
            p.bruising = 0.9f;
            p.seamGlow = 0.03f;
            f.ribcages = true;
            f.pods = true;
            f.podSpacing = 0.6f;
            f.strandSpacing = 0.025f;
            f.strandLength = 0.2f;
            f.strandGlowChance = 0.1f;
            f.spikeSpacing = 0.1f;
            f.slimeChance = 0.8f;
            f.vertebraSpacing = 0.02f;
            break;
        case Preset::Cathedral:
            f.spires = true;
            f.ribcages = true;
            f.shelves = true;
            f.pods = true;
            f.podSpacing = 0.7f;
            f.mawCoverage = 0.4f;
            f.heartRadius = 0.1f;
            f.heartClaws = 12;
            f.spikeLength = 0.09f;
            f.membraneChance = 0.6f;
            break;
    }
    params = p;
    features = f;
}

std::vector<std::string> presetNames() { return {std::begin(kNames), std::end(kNames)}; }

bool presetFromName(const std::string& name, Preset& out) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    for (size_t i = 0; i < std::size(kNames); ++i)
        if (lower == kNames[i]) {
            out = static_cast<Preset>(i);
            return true;
        }
    return false;
}

}  // namespace eclipse
