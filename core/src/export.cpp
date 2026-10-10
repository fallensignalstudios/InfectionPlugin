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
    return writeGlb(path, {{"EclipseCorruption", &mesh}}, error);
}

bool writeGlb(const std::string& path, const std::vector<NamedMesh>& meshes, std::string* error) {
    struct View {
        size_t offset, length;
        bool indices;
    };
    std::vector<uint8_t> bin;
    std::vector<View> views;
    std::ostringstream accessors, meshJson, nodeJson, sceneNodes;
    int accessor = 0, written = 0;
    auto beginView = [&]() { pad4(bin, 0); return bin.size(); };
    auto endView = [&](size_t start, bool indices) {
        views.push_back({start, bin.size() - start, indices});
        return static_cast<int>(views.size() - 1);
    };
    auto addAccessor = [&](int bufferView, int componentType, size_t count, const char* type, const std::string& extra) {
        if (accessor) accessors << ",";
        accessors << "{\"bufferView\":" << bufferView << ",\"componentType\":" << componentType
                  << ",\"count\":" << count << ",\"type\":\"" << type << "\"" << extra << "}";
        return accessor++;
    };

    for (const auto& named : meshes) {
        const Mesh& mesh = *named.mesh;
        const size_t n = mesh.positions.size();
        if (n == 0 || mesh.triangles.empty()) continue;  // an empty part is simply left out
        const bool hasNormals = mesh.normals.size() == n;
        const bool hasAttributes = mesh.attributes.size() == n;
        std::ostringstream attributes;
        attributes.precision(9);

        Vec3 lo, hi;
        mesh.bounds(lo, hi);
        size_t s = beginView();
        for (const auto& p : mesh.positions) {
            appendF32(bin, p.x);
            appendF32(bin, p.y);
            appendF32(bin, p.z);
        }
        {
            std::ostringstream mm;
            mm.precision(9);
            mm << ",\"min\":[" << lo.x << "," << lo.y << "," << lo.z << "],\"max\":[" << hi.x << "," << hi.y
               << "," << hi.z << "]";
            attributes << "\"POSITION\":" << addAccessor(endView(s, false), 5126, n, "VEC3", mm.str());
        }
        if (hasNormals) {
            s = beginView();
            for (const auto& v : mesh.normals) {
                Vec3 u = normalize(v);
                appendF32(bin, u.x);
                appendF32(bin, u.y);
                appendF32(bin, u.z);
            }
            attributes << ",\"NORMAL\":" << addAccessor(endView(s, false), 5126, n, "VEC3", "");
        }
        if (hasAttributes) {
            s = beginView();
            for (const auto& a : mesh.attributes) {
                appendF32(bin, clamp01(a.infection));
                appendF32(bin, clamp01(a.glow));
                appendF32(bin, clamp01(a.crack));
                appendF32(bin, clamp01(a.plateTint));
            }
            attributes << ",\"COLOR_0\":" << addAccessor(endView(s, false), 5126, n, "VEC4", "");
            s = beginView();
            for (const auto& a : mesh.attributes) {
                appendF32(bin, a.arrival);
                appendF32(bin, a.height);
            }
            attributes << ",\"TEXCOORD_0\":" << addAccessor(endView(s, false), 5126, n, "VEC2", "");
        }
        s = beginView();
        for (const auto& t : mesh.triangles) {
            appendU32(bin, t[0]);
            appendU32(bin, t[1]);
            appendU32(bin, t[2]);
        }
        int indices = addAccessor(endView(s, true), 5125, mesh.triangles.size() * 3, "SCALAR", "");

        if (written) {
            meshJson << ",";
            nodeJson << ",";
            sceneNodes << ",";
        }
        meshJson << "{\"name\":\"" << named.name << "\",\"primitives\":[{\"attributes\":{" << attributes.str()
                 << "},\"indices\":" << indices << ",\"material\":0}]}";
        nodeJson << "{\"mesh\":" << written << ",\"name\":\"" << named.name << "\"}";
        sceneNodes << written;
        ++written;
    }
    if (written == 0) {
        if (error) *error = "nothing to write: every mesh is empty";
        return false;
    }
    pad4(bin, 0);

    std::ostringstream js;
    js << "{\"asset\":{\"version\":\"2.0\",\"generator\":\"eclipse-biomass\"},";
    js << "\"scene\":0,\"scenes\":[{\"nodes\":[" << sceneNodes.str() << "]}],\"nodes\":[" << nodeJson.str() << "],";
    js << "\"materials\":[{\"name\":\"M_EclipseBiomass\",\"doubleSided\":true,\"pbrMetallicRoughness\":{"
          "\"baseColorFactor\":[0.03,0.025,0.035,1],\"metallicFactor\":0,\"roughnessFactor\":0.25},"
          "\"emissiveFactor\":[0.35,0.1,0.6]}],";
    js << "\"meshes\":[" << meshJson.str() << "],";
    js << "\"accessors\":[" << accessors.str() << "],";
    js << "\"bufferViews\":[";
    for (size_t i = 0; i < views.size(); ++i) {
        if (i) js << ",";
        js << "{\"buffer\":0,\"byteOffset\":" << views[i].offset << ",\"byteLength\":" << views[i].length
           << ",\"target\":" << (views[i].indices ? 34963 : 34962) << "}";
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
