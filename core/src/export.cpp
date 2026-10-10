#include "eclipse/export.h"

#include <cstdio>
#include <cstring>
#include <sstream>
#include <vector>

namespace eclipse {

namespace {

void appendBytes(std::vector<uint8_t>& out, const void* data, size_t size) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    out.insert(out.end(), p, p + size);
}

void appendU32(std::vector<uint8_t>& out, uint32_t v) {
    uint8_t b[4] = {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v >> 16),
                    static_cast<uint8_t>(v >> 24)};
    appendBytes(out, b, 4);
}

void appendF32(std::vector<uint8_t>& out, float f) {
    uint32_t v;
    std::memcpy(&v, &f, 4);
    appendU32(out, v);
}

void pad4(std::vector<uint8_t>& out, uint8_t fill) {
    while (out.size() % 4) out.push_back(fill);
}

}  // namespace

bool writeGlb(const std::string& path, const Mesh& mesh, std::string* error) {
    const size_t n = mesh.positions.size();
    if (n == 0 || mesh.triangles.empty()) {
        if (error) *error = "mesh is empty";
        return false;
    }
    const bool hasNormals = mesh.normals.size() == n;
    const bool hasAttributes = mesh.attributes.size() == n;

    struct View {
        size_t offset, length;
    };
    std::vector<uint8_t> bin;
    std::vector<View> views;
    auto beginView = [&]() { pad4(bin, 0); return bin.size(); };
    auto endView = [&](size_t start) { views.push_back({start, bin.size() - start}); };

    Vec3 lo, hi;
    mesh.bounds(lo, hi);

    size_t s = beginView();
    for (const auto& p : mesh.positions) {
        appendF32(bin, p.x);
        appendF32(bin, p.y);
        appendF32(bin, p.z);
    }
    endView(s);
    if (hasNormals) {
        s = beginView();
        for (const auto& v : mesh.normals) {
            Vec3 u = normalize(v);
            appendF32(bin, u.x);
            appendF32(bin, u.y);
            appendF32(bin, u.z);
        }
        endView(s);
    }
    if (hasAttributes) {
        s = beginView();
        for (const auto& a : mesh.attributes) {
            appendF32(bin, clamp01(a.infection));
            appendF32(bin, clamp01(a.glow));
            appendF32(bin, clamp01(a.crack));
            appendF32(bin, clamp01(a.plateTint));
        }
        endView(s);
        s = beginView();
        for (const auto& a : mesh.attributes) {
            appendF32(bin, a.arrival);
            appendF32(bin, a.height);
        }
        endView(s);
    }
    s = beginView();
    for (const auto& t : mesh.triangles) {
        appendU32(bin, t[0]);
        appendU32(bin, t[1]);
        appendU32(bin, t[2]);
    }
    endView(s);
    pad4(bin, 0);

    std::ostringstream js;
    js.precision(9);
    js << "{\"asset\":{\"version\":\"2.0\",\"generator\":\"eclipse-biomass\"},";
    js << "\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0,\"name\":\"EclipseCorruption\"}],";
    js << "\"materials\":[{\"name\":\"M_EclipseBiomass\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[0.03,0.025,"
          "0.035,1],\"metallicFactor\":0,\"roughnessFactor\":0.25},\"emissiveFactor\":[0.35,0.1,0.6]}],";

    int view = 0;
    std::ostringstream accessors, attributes;
    int accessor = 0;
    auto addAccessor = [&](int bufferView, int componentType, size_t count, const char* type, const std::string& extra) {
        if (accessor) accessors << ",";
        accessors << "{\"bufferView\":" << bufferView << ",\"componentType\":" << componentType
                  << ",\"count\":" << count << ",\"type\":\"" << type << "\"" << extra << "}";
        return accessor++;
    };
    {
        std::ostringstream mm;
        mm.precision(9);
        mm << ",\"min\":[" << lo.x << "," << lo.y << "," << lo.z << "],\"max\":[" << hi.x << "," << hi.y << ","
           << hi.z << "]";
        attributes << "\"POSITION\":" << addAccessor(view++, 5126, n, "VEC3", mm.str());
    }
    if (hasNormals) attributes << ",\"NORMAL\":" << addAccessor(view++, 5126, n, "VEC3", "");
    if (hasAttributes) {
        attributes << ",\"COLOR_0\":" << addAccessor(view++, 5126, n, "VEC4", "");
        attributes << ",\"TEXCOORD_0\":" << addAccessor(view++, 5126, n, "VEC2", "");
    }
    int indices = addAccessor(view++, 5125, mesh.triangles.size() * 3, "SCALAR", "");

    js << "\"meshes\":[{\"name\":\"EclipseCorruption\",\"primitives\":[{\"attributes\":{" << attributes.str()
       << "},\"indices\":" << indices << ",\"material\":0}]}],";
    js << "\"accessors\":[" << accessors.str() << "],";
    js << "\"bufferViews\":[";
    for (size_t i = 0; i < views.size(); ++i) {
        if (i) js << ",";
        bool isIndices = i + 1 == views.size();
        js << "{\"buffer\":0,\"byteOffset\":" << views[i].offset << ",\"byteLength\":" << views[i].length
           << ",\"target\":" << (isIndices ? 34963 : 34962) << "}";
    }
    js << "],\"buffers\":[{\"byteLength\":" << bin.size() << "}]}";

    std::string json = js.str();
    while (json.size() % 4) json.push_back(' ');

    std::vector<uint8_t> file;
    appendU32(file, 0x46546C67u);  // "glTF"
    appendU32(file, 2);
    appendU32(file, static_cast<uint32_t>(12 + 8 + json.size() + 8 + bin.size()));
    appendU32(file, static_cast<uint32_t>(json.size()));
    appendU32(file, 0x4E4F534Au);  // "JSON"
    appendBytes(file, json.data(), json.size());
    appendU32(file, static_cast<uint32_t>(bin.size()));
    appendU32(file, 0x004E4942u);  // "BIN\0"
    appendBytes(file, bin.data(), bin.size());

    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        if (error) *error = "cannot write " + path;
        return false;
    }
    bool ok = std::fwrite(file.data(), 1, file.size(), f) == file.size();
    ok = (std::fclose(f) == 0) && ok;
    if (!ok && error) *error = "failed writing " + path;
    return ok;
}

}  // namespace eclipse
