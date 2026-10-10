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
#include "eclipse/export.h"
#include "eclipse/growths.h"
#include "eclipse/hosts.h"
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
        "  --overlay              export only the corrupted shell, to lay over the untouched host\n"
        "  --weld METRES          OBJ hosts: merge vertices this close (default 0.0001)\n"
        "  --max-edge METRES      OBJ hosts: subdivide until no edge is longer (try 0.01)\n"
        "  --no-preview           skip the PNG previews\n");
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

}  // namespace

int main(int argc, char** argv) {
    std::string hostArg = "mannequin", outDir = "out";
    float cell = 0.006f;
    GrowthParams params;
    params.coverage = 0.45f;
    std::vector<float> stages;
    std::vector<Vec3> seeds;
    bool preview = true, obj = false, plateSizeGiven = false, scaleGiven = false, overlay = false;
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
        else if (a == "--no-growths") features.enabled = false;
        else if (a == "--overlay") overlay = true;
        else if (a == "--weld") weld = std::strtof(next().c_str(), nullptr);
        else if (a == "--max-edge") maxEdge = std::strtof(next().c_str(), nullptr);
        else if (a == "--feature-scale") {
            features.scale = std::strtof(next().c_str(), nullptr);
            scaleGiven = true;
        }
        else if (a == "--stages") {
            if (!parseFloats(next(), stages)) { usage(); return 2; }
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

    auto t0 = std::chrono::steady_clock::now();
    Mesh host;
    std::string hostName = hostArg;
    bool builtin = false;
    for (const auto& name : builtinHostNames()) builtin = builtin || name == hostArg;
    if (builtin) {
        BuiltinHost b = makeBuiltinHost(hostArg, cell);
        host = std::move(b.mesh);
        if (seeds.empty()) seeds = b.defaultSeeds;
        if (hostArg == "corridor" && !plateSizeGiven) params.plateSize = 0.18f;  // architecture-scale plates
        if (hostArg == "corridor" && !scaleGiven) features.scale = 2.0f;
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

    std::vector<Image> strip;
    for (size_t s = 0; s < stages.size(); ++s) {
        GrowthParams p = params;
        p.coverage = stages[s];
        Growth g = grow(host, seeds, p, features);
        char tag[64];
        std::snprintf(tag, sizeof tag, "%s_s%u_c%03d", hostName.c_str(), p.seed, static_cast<int>(p.coverage * 100 + 0.5f));
        std::string base = (std::filesystem::path(outDir) / tag).string();
        std::string err;
        Mesh shell;
        if (overlay) shell = extractShell(g.surface, 0.0015f);
        const Mesh& exported = overlay ? shell : g.surface;
        if (!writeGlb(base + ".glb", {{overlay ? "EclipseShell" : "EclipseSurface", &exported}, {"EclipseGrowths", &g.growths}},
                      &err)) {
            std::fprintf(stderr, "%s\n", err.c_str());
            return 1;
        }
        Mesh grown = g.surface;
        grown.append(g.growths);
        if (obj) writeObj(base + ".obj", grown);
        std::printf("  coverage %.2f -> %s.glb (hash %016llx)\n", p.coverage, base.c_str(),
                    static_cast<unsigned long long>(meshHash(grown)));
        std::printf("    %zu roots, %zu vein tubes, %zu horns, %zu hearts, %zu vertebrae, %zu nodules, %zu strands, "
                    "%zu bridges, %zu membranes; %zu growth triangles\n",
                    g.stats.tendrils, g.stats.veinTubes, g.stats.spikes, g.stats.hearts, g.stats.vertebrae,
                    g.stats.nodules, g.stats.strands, g.stats.bridges, g.stats.membranes, g.growths.triangleCount());
        if (preview) {
            PreviewOptions opts;
            if (stages.size() > 1) {
                opts.yaws = {builtin && hostArg == "corridor" ? 30.0f : 0.0f};
                opts.width = 640;
                opts.height = 760;
                strip.push_back(renderPreview(grown, opts));
            } else {
                if (builtin && hostArg == "corridor") opts.yaws = {30.0f, -20.0f};
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
