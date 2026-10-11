#include "eclipse/hosts.h"

#include <stdexcept>

namespace eclipse {

SdfModel mannequinModel() {
    SdfModel m;
    const float body = 0.035f;
    m.ellipsoid({0, 0.98f, 0}, {0.165f, 0.11f, 0.11f}, body);                 // pelvis
    m.roundCone({0, 1.0f, 0}, {0, 1.24f, 0.005f}, 0.125f, 0.135f, body);       // abdomen
    m.ellipsoid({0, 1.33f, 0.012f}, {0.185f, 0.15f, 0.115f}, body);            // ribcage
    m.roundCone({0, 1.44f, -0.01f}, {0, 1.57f, 0.01f}, 0.058f, 0.05f, 0.03f);  // neck
    m.ellipsoid({0, 1.67f, 0.02f}, {0.088f, 0.112f, 0.102f}, 0.03f);           // skull
    m.roundCone({0, 1.645f, 0.045f}, {0, 1.585f, 0.07f}, 0.06f, 0.035f, 0.03f);  // jaw
    for (float s : {-1.0f, 1.0f}) {
        m.roundCone({s * 0.05f, 1.43f, -0.01f}, {s * 0.19f, 1.425f, 0}, 0.07f, 0.06f, body);   // trapezius
        m.roundCone({s * 0.19f, 1.425f, 0}, {s * 0.46f, 1.425f, 0}, 0.055f, 0.043f, 0.03f);    // upper arm
        m.roundCone({s * 0.46f, 1.425f, 0}, {s * 0.72f, 1.425f, 0}, 0.044f, 0.032f, 0.02f);    // forearm
        m.ellipsoid({s * 0.785f, 1.425f, 0}, {0.055f, 0.02f, 0.042f}, 0.02f);                  // palm
        for (int f = 0; f < 4; ++f) {
            float z = -0.027f + 0.018f * f;
            float len = (f == 0 || f == 3) ? 0.065f : 0.08f;
            m.roundCone({s * 0.83f, 1.425f, z}, {s * (0.83f + len), 1.42f, z * 1.15f}, 0.0105f, 0.008f, 0.006f);
        }
        m.roundCone({s * 0.76f, 1.425f, 0.035f}, {s * 0.81f, 1.415f, 0.085f}, 0.012f, 0.009f, 0.01f);  // thumb
        m.roundCone({s * 0.095f, 0.93f, 0}, {s * 0.12f, 0.5f, 0.005f}, 0.085f, 0.058f, body);           // thigh
        m.roundCone({s * 0.12f, 0.5f, 0.005f}, {s * 0.13f, 0.08f, -0.01f}, 0.055f, 0.038f, 0.025f);    // shin
        m.roundCone({s * 0.13f, 0.05f, -0.035f}, {s * 0.14f, 0.03f, 0.13f}, 0.045f, 0.03f, 0.025f);   // foot
    }
    return m;
}

SdfModel corridorModel() {
    SdfModel m;
    m.box({0, -0.05f, 0}, {1.5f, 0.05f, 1.5f}, 0.0f, 0.0f);          // floor
    m.box({0, 1.4f, -1.45f}, {1.5f, 1.4f, 0.05f}, 0.0f, 0.04f);      // back wall
    m.box({-1.45f, 1.4f, 0}, {0.05f, 1.4f, 1.5f}, 0.0f, 0.04f);      // side wall
    m.capsule({-1.5f, 2.2f, -1.3f}, {1.5f, 2.2f, -1.3f}, 0.07f, 0.02f);  // pipe run
    m.capsule({-1.5f, 1.95f, -1.32f}, {1.5f, 1.95f, -1.32f}, 0.045f, 0.02f);
    m.box({-0.85f, 0.35f, -0.85f}, {0.35f, 0.35f, 0.35f}, 0.03f, 0.03f);  // crate
    m.roundCone({0.8f, 0.0f, -0.6f}, {0.8f, 2.8f, -0.6f}, 0.16f, 0.13f, 0.06f);  // column
    return m;
}

std::vector<std::string> builtinHostNames() { return {"mannequin", "corridor", "kit"}; }

namespace {

BuiltinHost makeKit(float cellSize) {
    BuiltinHost host;
    host.name = "kit";
    host.defaultSeeds = {{-0.6f, 0.0f, 0.4f}};
    struct Part {
        SdfModel model;
        Vec3 lo, hi;
    };
    std::vector<Part> parts(5);
    const Vec3 pad{cellSize * 2, cellSize * 2, cellSize * 2};
    parts[0].model.box({0, -0.05f, 0}, {1.5f, 0.05f, 1.5f}, 0.0f, 0.0f);  // floor
    parts[0].lo = {-1.5f, -0.1f - pad.y, -1.5f};
    parts[0].hi = {1.45f, pad.y, 1.45f};
    parts[1].model.box({0, 1.4f, -1.45f}, {1.5f, 1.4f, 0.05f}, 0.0f, 0.04f);  // back wall
    parts[1].lo = {-1.5f, -pad.y, -1.5f};
    parts[1].hi = {1.45f, 2.8f, -1.4f + pad.z};
    parts[2].model.box({-1.45f, 1.4f, 0}, {0.05f, 1.4f, 1.5f}, 0.0f, 0.04f);  // side wall
    parts[2].lo = {-1.5f, -pad.y, -1.5f};
    parts[2].hi = {-1.4f + pad.x, 2.8f, 1.45f};
    parts[3].model.box({-0.85f, 0.35f, -0.85f}, {0.35f, 0.35f, 0.35f}, 0.03f, 0.03f);  // crate
    parts[3].lo = Vec3{-1.2f, 0.0f, -1.2f} - pad;
    parts[3].hi = Vec3{-0.5f, 0.7f, -0.5f} + pad;
    parts[4].model.roundCone({0.8f, 0.0f, -0.6f}, {0.8f, 2.8f, -0.6f}, 0.16f, 0.13f, 0.06f);  // column
    parts[4].lo = Vec3{0.6f, 0.0f, -0.8f} - pad;
    parts[4].hi = Vec3{1.0f, 2.8f, -0.4f} + pad;
    for (Part& part : parts) {
        Mesh piece = polygonize([&](const Vec3& p) { return part.model.evaluate(p); }, part.lo, part.hi, cellSize);
        piece.computeNormals();
        host.mesh.append(piece);
        host.pieces.push_back(std::move(piece));
    }
    return host;
}

}  // namespace

BuiltinHost makeBuiltinHost(const std::string& name, float cellSize) {
    if (name == "kit") return makeKit(cellSize);
    BuiltinHost host;
    host.name = name;
    SdfModel model;
    Vec3 lo, hi;
    if (name == "mannequin") {
        model = mannequinModel();
        host.defaultSeeds = {{0, 1.33f, 0.13f}, {0.9f, 1.425f, 0.0f}};
        model.bounds(lo, hi);
    } else if (name == "corridor") {
        model = corridorModel();
        host.defaultSeeds = {{-1.4f, 0.0f, -1.4f}, {0.8f, 0.0f, -0.4f}};
        // Clip the open sides so the slabs stay single surfaces.
        lo = Vec3{-1.5f, -0.1f, -1.5f};
        hi = Vec3{1.45f, 2.8f, 1.45f};
    } else {
        throw std::invalid_argument("unknown built-in host: " + name);
    }
    const Vec3 pad{cellSize * 2, cellSize * 2, cellSize * 2};
    if (name == "mannequin") {
        lo -= pad;
        hi += pad;
    }
    host.mesh = polygonize([&](const Vec3& p) { return model.evaluate(p); }, lo, hi, cellSize);
    return host;
}

}  // namespace eclipse
