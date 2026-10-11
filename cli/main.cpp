// eclipse-cli: grow Eclipse corruption over a host mesh and export it.
//
//   eclipse-cli --host mannequin --coverage 0.45 --out out/
//   eclipse-cli --host level_chunk.obj --seed-at 0,0,0 --stages 0.2,0.5,0.9

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#include "eclipse/corruption.h"
#include "eclipse/detail.h"
#include "eclipse/export.h"
#include "eclipse/growths.h"
#include "eclipse/hosts.h"
#include "eclipse/presets.h"
#include "eclipse/scene.h"
#include "eclipse/preview.h"

using namespace eclipse;

namespace {

void usage() {
    std::printf(
        "usage: eclipse-cli [options]\n"
        "  --host NAME|FILE.obj   built-in host (mannequin, corridor) or an OBJ (default mannequin)\n"
        "  --cell METRES          resolution of built-in hosts (default 0.006)\n"
        "  --seed N               random seed (default 1)\n"
        "  --coverage F           fraction overtaken, 0..1 (default 0.45)\n"
        "  --stages F,F,...       grow several coverages from the same seed instead\n"
        "  --gravity F            >0 climbs, <0 pools downward (default 0)\n"
        "  --plate-size METRES    carapace plate spacing (default 0.085, corridor 0.18)\n"
        "  --seed-at X,Y,Z        where corruption starts; repeatable (default: host's own)\n"
        "  --out DIR              output directory (default out)\n"
        "  --obj                  also write OBJ files\n"
        "  --feature-scale F      size of spikes, nodules, strands and membranes (default 1, corridor 2)\n"
        "  --no-growths           corrupt the surface only\n"
        "  --preset NAME          a look: eclipse, creep, thornfield, rootmass, hive, carcass, cathedral\n"
        "                         (resets the look; options after it still apply)\n"
        "  --spires --ribcages --pods --shelves   add formations (large set pieces)\n"
        "  --fusion F             how much tissue swells around growths (default 1, 0 off)\n"
        "  --roughness F          how knotted and lumpy growths are (default 1, 0 smooth)\n"
        "  --overlay              export only the corrupted shell, to lay over the untouched host\n"
        "  --weld METRES          OBJ hosts: merge vertices this close (default 0.0001)\n"
        "  --max-edge METRES      OBJ hosts: subdivide until no edge is longer (try 0.01)\n"
        "  --no-preview           skip the PNG previews\n"
        "  --focus X,Y,Z,R        preview a close-up of what lies within R of X,Y,Z\n"
        "  --mask MODE:SHAPE:X,Y,Z:HX,HY,HZ[:FALLOFF]\n"
        "                         a mask volume, repeatable. MODE block, bare or dense; SHAPE box or\n"
        "                         sphere (radius HX). e.g. block:box:0,0,0:0.3,1,1.5:0.1\n"
        "  --contact-gap METRES   pieces of the kit host closer than this spread into each other (0.02)\n"
        "  --variations N         preview N seeds side by side, from --seed up, and stop\n"
        "  --detail-maps          write the materials' detail maps, lit, as PNGs and stop\n");
}

bool parseFloats(const std::string& s, std::vector<float>& out) {
    std::stringstream ss(s);
    std::string item;
    out.clear();
    while (std::getline(ss, item, ',')) {
        char* end = nullptr;
        float v = std::strtof(item.c_str(), &end);
        if (end == item.c_str()) return false;
        out.push_back(v);
    }
    return !out.empty();
}

Image hconcat(const std::vector<Image>& images) {
    Image out;
    for (const auto& im : images) {
        out.width += im.width;
        out.height = std::max(out.height, im.height);
    }
    out.rgb.assign(static_cast<size_t>(out.width) * out.height * 3, 255);
    int x0 = 0;
    for (const auto& im : images) {
        for (int y = 0; y < im.height; ++y)
            std::copy(im.rgb.begin() + static_cast<long>(y) * im.width * 3,
                      im.rgb.begin() + static_cast<long>(y + 1) * im.width * 3,
                      out.rgb.begin() + (static_cast<long>(y) * out.width + x0) * 3);
        x0 += im.width;
    }
    return out;
}

// A detail map lit from the upper left, two tiles across to show it repeats.
Image litDetail(const DetailMap& map) {
    Image im;
    im.width = im.height = map.size * 2;
    im.rgb.resize(static_cast<size_t>(im.width) * im.height * 3);
    const Vec3 light = normalize(Vec3{-0.5f, 0.6f, 0.62f});
    for (int y = 0; y < im.height; ++y)
        for (int x = 0; x < im.width; ++x) {
            const uint8_t* t = &map.rgba[(static_cast<size_t>(y % map.size) * map.size + (x % map.size)) * 4];
            float su = t[0] / 127.5f - 1.0f, sv = t[1] / 127.5f - 1.0f;
            Vec3 n = normalize(Vec3{-su * 0.6f, sv * 0.6f, 1.0f});  // image rows run down
            float shade = (0.25f + 0.75f * std::max(0.0f, dot(n, light))) * (0.4f + 0.6f * t[3] / 255.0f);
            for (int c = 0; c < 3; ++c)
                im.rgb[(static_cast<size_t>(y) * im.width + x) * 3 + c] =
                    static_cast<uint8_t>(std::lround(std::min(1.0f, shade) * 255.0f));
        }
    return im;
}

// MODE:SHAPE:X,Y,Z:HX,HY,HZ[:FALLOFF]
bool parseMask(const std::string& spec, std::vector<MaskVolume>& out) {
    std::vector<std::string> parts;
    std::stringstream ss(spec);
    std::string part;
    while (std::getline(ss, part, ':')) parts.push_back(part);
    if (parts.size() < 4 || parts.size() > 5) return false;
    MaskVolume m;
    if (parts[0] == "block") m.mode = MaskVolume::Mode::Block;
    else if (parts[0] == "bare") m.mode = MaskVolume::Mode::Bare;
    else if (parts[0] == "dense") m.mode = MaskVolume::Mode::Dense;
    else return false;
    if (parts[1] == "box") m.shape = MaskVolume::Shape::Box;
    else if (parts[1] == "sphere") m.shape = MaskVolume::Shape::Sphere;
    else return false;
    std::vector<float> c, h;
    if (!parseFloats(parts[2], c) || c.size() != 3 || !parseFloats(parts[3], h) || h.empty()) return false;
    m.centre = {c[0], c[1], c[2]};
    m.halfExtents = {h[0], h.size() > 1 ? h[1] : h[0], h.size() > 2 ? h[2] : h[0]};
    if (parts.size() == 5) m.falloff = std::strtof(parts[4].c_str(), nullptr);
    out.push_back(m);
    return true;
}

// Only the triangles with a corner within radius of centre, for close-ups.
Mesh cropToSphere(const Mesh& mesh, const Vec3& centre, float radius) {
    Mesh out;
    std::vector<int64_t> remap(mesh.vertexCount(), -1);
    const bool attrs = mesh.attributes.size() == mesh.vertexCount();
    for (const auto& t : mesh.triangles) {
        bool near = false;
        for (uint32_t i : t) near = near || length(mesh.positions[i] - centre) < radius;
        if (!near) continue;
        std::array<uint32_t, 3> r{};
        for (int k = 0; k < 3; ++k) {
            uint32_t i = t[k];
            if (remap[i] < 0) {
                remap[i] = static_cast<int64_t>(out.positions.size());
                out.positions.push_back(mesh.positions[i]);
                out.normals.push_back(mesh.normals[i]);
                if (attrs) out.attributes.push_back(mesh.attributes[i]);
            }
            r[k] = static_cast<uint32_t>(remap[i]);
        }
        out.triangles.push_back(r);
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    std::string hostArg = "mannequin", outDir = "out";
    float cell = 0.006f;
    GrowthParams params;
    params.coverage = 0.45f;
    std::vector<float> stages;
    std::vector<Vec3> seeds;
    bool preview = true, obj = false, plateSizeGiven = false, scaleGiven = false, overlay = false;
    std::vector<float> focus;  // x, y, z, radius: preview only what lies within
    std::vector<MaskVolume> masks;
    float contactGap = 0.02f;
    int variations = 0;
    bool detailMaps = false;
    float weld = 1e-4f, maxEdge = 0.0f;
    FeatureParams features;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "missing value for %s\n", a.c_str());
                std::exit(2);
            }
            return argv[++i];
        };
        std::vector<float> v;
        if (a == "--host") hostArg = next();
        else if (a == "--cell") cell = std::strtof(next().c_str(), nullptr);
        else if (a == "--seed") params.seed = static_cast<uint32_t>(std::strtoul(next().c_str(), nullptr, 10));
        else if (a == "--coverage") params.coverage = std::strtof(next().c_str(), nullptr);
        else if (a == "--gravity") params.gravity = std::strtof(next().c_str(), nullptr);
        else if (a == "--plate-size") {
            params.plateSize = std::strtof(next().c_str(), nullptr);
            plateSizeGiven = true;
        }
        else if (a == "--out") outDir = next();
        else if (a == "--obj") obj = true;
        else if (a == "--no-preview") preview = false;
        else if (a == "--detail-maps") detailMaps = true;
        else if (a == "--no-growths") features.enabled = false;
        else if (a == "--preset") {
            Preset preset;
            if (!presetFromName(next(), preset)) { usage(); return 2; }
            applyPreset(preset, params, features);
        }
        else if (a == "--spires") features.spires = true;
        else if (a == "--ribcages") features.ribcages = true;
        else if (a == "--pods") features.pods = true;
        else if (a == "--shelves") features.shelves = true;
        else if (a == "--fusion") features.fusion = std::strtof(next().c_str(), nullptr);
        else if (a == "--roughness") features.roughness = std::strtof(next().c_str(), nullptr);
        else if (a == "--overlay") overlay = true;
        else if (a == "--weld") weld = std::strtof(next().c_str(), nullptr);
        else if (a == "--max-edge") maxEdge = std::strtof(next().c_str(), nullptr);
        else if (a == "--feature-scale") {
            features.scale = std::strtof(next().c_str(), nullptr);
            scaleGiven = true;
        }
        else if (a == "--stages") {
            if (!parseFloats(next(), stages)) { usage(); return 2; }
        } else if (a == "--mask") {
            if (!parseMask(next(), masks)) { usage(); return 2; }
        } else if (a == "--contact-gap") {
            contactGap = std::strtof(next().c_str(), nullptr);
        } else if (a == "--variations") {
            variations = std::atoi(next().c_str());
        } else if (a == "--focus") {
            if (!parseFloats(next(), focus) || focus.size() != 4) { usage(); return 2; }
        } else if (a == "--seed-at") {
            if (!parseFloats(next(), v) || v.size() != 3) { usage(); return 2; }
            seeds.push_back({v[0], v[1], v[2]});
        } else if (a == "--help" || a == "-h") {
            usage();
            return 0;
        } else {
            std::fprintf(stderr, "unknown option %s\n", a.c_str());
            usage();
            return 2;
        }
    }

    if (detailMaps) {
        std::filesystem::create_directories(outDir);
        for (auto [kind, name] : {std::pair{DetailKind::Chitin, "detail_chitin.png"}, std::pair{DetailKind::Flesh, "detail_flesh.png"},
                                  std::pair{DetailKind::Capillaries, "detail_capillaries.png"}}) {
            std::string path = (std::filesystem::path(outDir) / name).string();
            writePng(path, litDetail(makeDetailMap(kind, 512)));
            std::printf("%s\n", path.c_str());
        }
        return 0;
    }

    auto t0 = std::chrono::steady_clock::now();
    Mesh host;
    std::vector<Mesh> pieces;  // a host of separate meshes
    std::string hostName = hostArg;
    bool builtin = false;
    for (const auto& name : builtinHostNames()) builtin = builtin || name == hostArg;
    if (builtin) {
        BuiltinHost b = makeBuiltinHost(hostArg, cell);
        host = std::move(b.mesh);
        pieces = std::move(b.pieces);
        if (seeds.empty()) seeds = b.defaultSeeds;
        const bool room = hostArg == "corridor" || hostArg == "kit";
        if (room && !plateSizeGiven) params.plateSize = 0.18f;  // architecture-scale plates
        if (room && !scaleGiven) features.scale = 2.0f;
    } else {
        std::string err;
        if (!readObj(hostArg, host, &err)) {
            std::fprintf(stderr, "%s\n", err.c_str());
            return 1;
        }
        hostName = std::filesystem::path(hostArg).stem().string();
        weldVertices(host, weld);
        if (maxEdge > 0.0f) {
            if (!subdivideToEdgeLength(host, maxEdge, 4000000)) {
                std::fprintf(stderr, "--max-edge %.4f would need more than 4M vertices; use a larger value\n", maxEdge);
                return 1;
            }
        } else {
            host.computeNormals();
        }
        if (seeds.empty()) {
            Vec3 lo, hi;
            host.bounds(lo, hi);
            seeds.push_back((lo + hi) * 0.5f);
        }
    }
    std::printf("host %s: %zu vertices, %zu triangles\n", hostName.c_str(), host.vertexCount(), host.triangleCount());

    std::filesystem::create_directories(outDir);
    if (stages.empty()) stages.push_back(params.coverage);

    // Separate pieces, or any masks, grow as a scene; otherwise the plain way.
    std::vector<const Mesh*> piecePointers;
    for (const Mesh& piece : pieces) piecePointers.push_back(&piece);
    if (piecePointers.empty()) piecePointers.push_back(&host);
    if (pieces.size() > 1) std::printf("  %zu separate pieces, contact gap %.3f m\n", pieces.size(), contactGap);
    auto growHost = [&](const GrowthParams& p) {
        if (pieces.size() > 1 || !masks.empty()) return growScene(piecePointers, seeds, p, features, contactGap, masks).growth;
        return grow(host, seeds, p, features);
    };

    if (variations > 0) {
        std::vector<Image> sheet;
        for (int i = 0; i < variations; ++i) {
            GrowthParams p = params;
            p.seed = params.seed + static_cast<uint32_t>(i);
            Growth g = growHost(p);
            Mesh grown = g.surface;
            grown.append(g.growths);
            grown.append(g.flesh);
            if (!focus.empty()) grown = cropToSphere(grown, {focus[0], focus[1], focus[2]}, focus[3]);
            PreviewOptions opts;
            opts.yaws = {builtin && hostArg != "mannequin" ? 30.0f : 0.0f};
            opts.width = 640;
            opts.height = 760;
            sheet.push_back(renderPreview(grown, opts));
            std::printf("  seed %u done\n", p.seed);
        }
        char tag[96];
        std::snprintf(tag, sizeof tag, "%s_s%u_variations.png", hostName.c_str(), params.seed);
        writePng((std::filesystem::path(outDir) / tag).string(), hconcat(sheet));
        return 0;
    }

    std::vector<Image> strip;
    for (size_t s = 0; s < stages.size(); ++s) {
        GrowthParams p = params;
        p.coverage = stages[s];
        Growth g = growHost(p);
        char tag[64];
        std::snprintf(tag, sizeof tag, "%s_s%u_c%03d", hostName.c_str(), p.seed, static_cast<int>(p.coverage * 100 + 0.5f));
        std::string base = (std::filesystem::path(outDir) / tag).string();
        std::string err;
        Mesh shell;
        if (overlay) shell = extractShell(g.surface, 0.0015f, nullptr, p.haloReach);
        const Mesh& exported = overlay ? shell : g.surface;
        if (!writeGlb(base + ".glb", {{overlay ? "EclipseShell" : "EclipseSurface", &exported}, {"EclipseGrowths", &g.growths},
                       {"EclipseFlesh", &g.flesh}},
                      &err)) {
            std::fprintf(stderr, "%s\n", err.c_str());
            return 1;
        }
        Mesh grown = g.surface;
        grown.append(g.growths);
        grown.append(g.flesh);
        if (obj) writeObj(base + ".obj", grown);
        std::printf("  coverage %.2f -> %s.glb (hash %016llx)\n", p.coverage, base.c_str(),
                    static_cast<unsigned long long>(meshHash(grown)));
        std::printf("    %zu roots (%zu rootlets), %zu vein tubes, %zu horns (%zu barbs), %zu hearts (%zu maws), %zu vertebrae, %zu nodules, "
                    "%zu sac clusters, %zu slime, %zu strands, %zu bridges, %zu membranes; %zu growth + %zu flesh triangles\n",
                    g.stats.tendrils, g.stats.rootlets, g.stats.veinTubes, g.stats.spikes, g.stats.barbs, g.stats.hearts, g.stats.maws,
                    g.stats.vertebrae, g.stats.nodules, g.stats.sacs, g.stats.slime, g.stats.strands, g.stats.bridges,
                    g.stats.membranes, g.growths.triangleCount(), g.flesh.triangleCount());
        if (g.stats.spires + g.stats.ribcages + g.stats.pods + g.stats.shelves > 0)
            std::printf("    formations: %zu spires, %zu ribcages, %zu pod clusters, %zu shelf clusters\n", g.stats.spires,
                        g.stats.ribcages, g.stats.pods, g.stats.shelves);
        if (preview) {
            if (!focus.empty()) grown = cropToSphere(grown, {focus[0], focus[1], focus[2]}, focus[3]);
            PreviewOptions opts;
            if (stages.size() > 1) {
                opts.yaws = {builtin && hostArg != "mannequin" ? 30.0f : 0.0f};
                opts.width = 640;
                opts.height = 760;
                strip.push_back(renderPreview(grown, opts));
            } else {
                if (builtin && hostArg != "mannequin") opts.yaws = {30.0f, -20.0f};
                writePng(base + ".png", renderPreview(grown, opts));
            }
        }
    }
    if (!strip.empty()) {
        char tag[64];
        std::snprintf(tag, sizeof tag, "%s_s%u_stages.png", hostName.c_str(), params.seed);
        writePng((std::filesystem::path(outDir) / tag).string(), hconcat(strip));
    }
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("done in %.2fs\n", secs);
    return 0;
}
