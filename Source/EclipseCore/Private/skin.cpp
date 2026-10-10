#include "eclipse/skin.h"

#include <algorithm>

#include "eclipse/spatial.h"

namespace eclipse {

SkinWeights blendSkin(const SkinWeights* const* inputs, const float* factors, size_t count) {
    // Few influences per vertex: a flat list beats a map.
    std::vector<std::pair<uint16_t, float>> total;
    for (size_t i = 0; i < count; ++i) {
        if (!inputs[i] || factors[i] <= 0.0f) continue;
        const SkinWeights& w = *inputs[i];
        for (int k = 0; k < w.count; ++k) {
            auto it = std::find_if(total.begin(), total.end(), [&](const auto& e) { return e.first == w.bone[k]; });
            if (it == total.end())
                total.push_back({w.bone[k], w.weight[k] * factors[i]});
            else
                it->second += w.weight[k] * factors[i];
        }
    }
    // Strongest first; equal weights by bone index, so the result never
    // depends on input order.
    std::sort(total.begin(), total.end(), [](const auto& a, const auto& b) {
        return a.second != b.second ? a.second > b.second : a.first < b.first;
    });
    SkinWeights out;
    float sum = 0.0f;
    for (const auto& [bone, weight] : total) {
        if (out.count == SkinWeights::kMaxInfluences || weight <= 0.0f) break;
        out.bone[out.count] = bone;
        out.weight[out.count] = weight;
        sum += weight;
        ++out.count;
    }
    if (sum > 0.0f)
        for (int k = 0; k < out.count; ++k) out.weight[k] /= sum;
    return out;
}

SkinTable skinAfterWeld(const SkinTable& before, const std::vector<uint32_t>& remap, size_t weldedCount) {
    SkinTable out(weldedCount);
    std::vector<uint8_t> set(weldedCount, 0);
    // Welded vertices share a position, so the first one's weights stand for all.
    for (size_t v = 0; v < remap.size() && v < before.size(); ++v) {
        if (set[remap[v]]) continue;
        out[remap[v]] = before[v];
        set[remap[v]] = 1;
    }
    return out;
}

SkinTable skinAfterSubdivision(SkinTable base, const std::vector<std::array<uint32_t, 2>>& parents) {
    // Parents always precede their child, so one forward pass suffices.
    base.reserve(base.size() + parents.size());
    const float half[2] = {0.5f, 0.5f};
    for (const auto& p : parents) {
        const SkinWeights a = base[p[0]], b = base[p[1]];
        const SkinWeights* both[2] = {&a, &b};
        base.push_back(blendSkin(both, half, 2));
    }
    return base;
}

SkinTable skinFromSources(const SkinTable& skin, const std::vector<uint32_t>& sources) {
    SkinTable out;
    out.reserve(sources.size());
    for (uint32_t s : sources) out.push_back(skin[s]);
    return out;
}

SkinTable bindToNearest(const std::vector<Vec3>& hostPositions, const SkinTable& hostSkin,
                        const std::vector<Vec3>& points, int neighbours) {
    SkinTable out(points.size());
    if (hostPositions.empty()) return out;
    const float start = 0.02f;
    PointGrid grid(hostPositions, start);
    const size_t k = static_cast<size_t>(std::max(1, neighbours));
    std::vector<std::pair<float, uint32_t>> near;
    for (size_t i = 0; i < points.size(); ++i) {
        const Vec3& p = points[i];
        // Widen the search until enough host tissue is in reach.
        std::vector<uint32_t> found;
        for (float r = start; r <= 0.33f; r *= 2.0f) {
            found = grid.within(p, r);
            if (found.size() >= k) break;
        }
        if (found.empty()) {
            // Nothing within a third of a metre: rare, so a plain scan is fine.
            uint32_t best = 0;
            float bestD = dot(hostPositions[0] - p, hostPositions[0] - p);
            for (uint32_t v = 1; v < hostPositions.size(); ++v) {
                float d = dot(hostPositions[v] - p, hostPositions[v] - p);
                if (d < bestD) bestD = d, best = v;
            }
            found.push_back(best);
        }
        near.clear();
        for (uint32_t v : found) {
            Vec3 d = hostPositions[v] - p;
            near.push_back({dot(d, d), v});
        }
        std::sort(near.begin(), near.end());
        if (near.size() > k) near.resize(k);
        std::vector<const SkinWeights*> inputs;
        std::vector<float> factors;
        for (const auto& [d2, v] : near) {
            inputs.push_back(&hostSkin[v]);
            factors.push_back(1.0f / (d2 + 1e-8f));
        }
        out[i] = blendSkin(inputs.data(), factors.data(), inputs.size());
    }
    return out;
}

}  // namespace eclipse
