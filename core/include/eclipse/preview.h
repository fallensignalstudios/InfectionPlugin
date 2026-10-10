#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "eclipse/mesh.h"

namespace eclipse {

struct Image {
    int width = 0, height = 0;
    std::vector<uint8_t> rgb;  // row-major, top row first
};

struct PreviewOptions {
    int width = 900;       // per view
    int height = 1000;
    int supersample = 2;
    // Camera yaw angles in degrees around +Y; one panel per angle, side by side.
    std::vector<float> yaws = {0.0f, 35.0f};
    float pitch = 6.0f;
};

// Software-rendered look-dev preview approximating the Eclipse material:
// glossy black carapace, pale host tissue, violet emissive veins and cracks.
// Lets growth be judged without opening the engine.
Image renderPreview(const Mesh& mesh, const PreviewOptions& options);

bool writePng(const std::string& path, const Image& image);

}  // namespace eclipse
