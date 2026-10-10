#include "eclipse/preview.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>

namespace eclipse {

namespace {

struct Camera {
    Vec3 eye, right, up, forward;
    float focal;  // in pixels
    float cx, cy;
};

struct Light {
    Vec3 key, fill;
};

Vec3 shade(const Vec3& normal, const Vec3& toEye, const CorruptionAttributes& a, const Light& light) {
    Vec3 n = normalize(normal);
    if (dot(n, toEye) < 0.0f) n = -n;  // two-sided, for open environment shells
    const float s = smoothstep(0.0f, 1.0f, a.infection);
    const float inflamed = 4.0f * a.infection * (1.0f - a.infection);

    Vec3 skin{0.50f, 0.43f, 0.41f};
    Vec3 carapace = Vec3{0.018f, 0.015f, 0.022f} + Vec3{0.02f, 0.008f, 0.03f} * a.plateTint;
    Vec3 base = lerp(skin, carapace, s);
    base = lerp(base, Vec3{0.12f, 0.05f, 0.10f}, 0.45f * inflamed * (1.0f - s));

    float ambient = lerpf(0.20f, 0.36f, n.y * 0.5f + 0.5f);
    float diffuse = std::max(0.0f, dot(n, light.key)) * 0.9f + std::max(0.0f, dot(n, light.fill)) * 0.25f;
    Vec3 colour = base * (ambient + diffuse);

    float gloss = lerpf(0.03f, 0.85f, s);
    float exponent = lerpf(12.0f, 140.0f, s);
    Vec3 h = normalize(light.key + toEye);
    float spec = gloss * std::pow(std::max(0.0f, dot(n, h)), exponent) * (exponent + 8.0f) / 40.0f;
    Vec3 h2 = normalize(light.fill + toEye);
    spec += 0.4f * gloss * std::pow(std::max(0.0f, dot(n, h2)), exponent) * (exponent + 8.0f) / 40.0f;
    colour += Vec3{1.0f, 0.97f, 1.0f} * spec;

    float fresnel = std::pow(1.0f - std::max(0.0f, dot(n, toEye)), 4.0f);
    colour += Vec3{0.55f, 0.55f, 0.6f} * fresnel * (0.04f + 0.3f * s);

    // Soft tissue: dark and wet, scattering wine-red light through its thin
    // edges and from behind, like the flesh material's subsurface shading.
    const float soft = clamp01(a.flesh) * s;
    if (soft > 0.0f) {
        Vec3 fleshBase{0.10f, 0.025f, 0.06f};
        float wrap = std::max(0.0f, (dot(n, light.key) + 0.5f) / 1.5f) * 0.8f +
                     std::max(0.0f, (dot(n, light.fill) + 0.5f) / 1.5f) * 0.25f;
        float through = std::pow(std::max(0.0f, dot(-n, light.key)), 2.0f) * 0.6f +
                        std::pow(1.0f - std::max(0.0f, dot(n, toEye)), 2.0f) * 0.35f;
        Vec3 wet = fleshBase * (ambient + wrap) + Vec3{0.45f, 0.06f, 0.20f} * (through * 0.5f);
        wet += Vec3{1.0f, 0.95f, 1.0f} * (0.5f * std::pow(std::max(0.0f, dot(n, h)), 90.0f) * 2.4f);
        colour = lerp(colour, wet, soft);
    }

    float g = clamp01(a.glow);
    colour += Vec3{0.55f, 0.14f, 1.0f} * (g * 2.0f);
    colour += Vec3{0.9f, 0.75f, 1.0f} * (std::pow(g, 6.0f) * 0.9f);
    return colour;
}

uint8_t toByte(float linear) {
    float mapped = 1.0f - std::exp(-std::max(0.0f, linear) * 1.5f);
    float srgb = std::pow(clamp01(mapped), 1.0f / 2.2f);
    return static_cast<uint8_t>(std::lround(srgb * 255.0f));
}

void renderView(const Mesh& mesh, const Camera& cam, const Light& light, int W, int H, std::vector<Vec3>& colour) {
    const Vec3 background{0.88f, 0.88f, 0.90f};
    colour.assign(static_cast<size_t>(W) * H, background);
    std::vector<float> depth(static_cast<size_t>(W) * H, std::numeric_limits<float>::infinity());

    const size_t n = mesh.positions.size();
    std::vector<float> sx(n), sy(n), invz(n);
    for (size_t i = 0; i < n; ++i) {
        Vec3 d = mesh.positions[i] - cam.eye;
        float z = dot(d, cam.forward);
        invz[i] = z > 1e-4f ? 1.0f / z : 0.0f;
        sx[i] = cam.cx + dot(d, cam.right) * cam.focal * invz[i];
        sy[i] = cam.cy - dot(d, cam.up) * cam.focal * invz[i];
    }
    const bool hasAttr = mesh.attributes.size() == n;
    const CorruptionAttributes clean{};

    for (const auto& t : mesh.triangles) {
        const uint32_t i0 = t[0], i1 = t[1], i2 = t[2];
        if (invz[i0] == 0.0f || invz[i1] == 0.0f || invz[i2] == 0.0f) continue;
        float area = (sx[i1] - sx[i0]) * (sy[i2] - sy[i0]) - (sx[i2] - sx[i0]) * (sy[i1] - sy[i0]);
        if (std::fabs(area) < 1e-9f) continue;
        int x0 = std::max(0, static_cast<int>(std::floor(std::min({sx[i0], sx[i1], sx[i2]}))));
        int x1 = std::min(W - 1, static_cast<int>(std::ceil(std::max({sx[i0], sx[i1], sx[i2]}))));
        int y0 = std::max(0, static_cast<int>(std::floor(std::min({sy[i0], sy[i1], sy[i2]}))));
        int y1 = std::min(H - 1, static_cast<int>(std::ceil(std::max({sy[i0], sy[i1], sy[i2]}))));
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                float px = x + 0.5f, py = y + 0.5f;
                float w0 = ((sx[i1] - px) * (sy[i2] - py) - (sx[i2] - px) * (sy[i1] - py)) / area;
                float w1 = ((sx[i2] - px) * (sy[i0] - py) - (sx[i0] - px) * (sy[i2] - py)) / area;
                float w2 = 1.0f - w0 - w1;
                if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;
                float iz = w0 * invz[i0] + w1 * invz[i1] + w2 * invz[i2];
                float z = 1.0f / iz;
                size_t pix = static_cast<size_t>(y) * W + x;
                if (z >= depth[pix]) continue;
                depth[pix] = z;
                // Perspective-correct weights.
                float b0 = w0 * invz[i0] * z, b1 = w1 * invz[i1] * z, b2 = w2 * invz[i2] * z;
                Vec3 pos = mesh.positions[i0] * b0 + mesh.positions[i1] * b1 + mesh.positions[i2] * b2;
                Vec3 nrm = mesh.normals[i0] * b0 + mesh.normals[i1] * b1 + mesh.normals[i2] * b2;
                CorruptionAttributes a = clean;
                if (hasAttr) {
                    const auto &a0 = mesh.attributes[i0], &a1 = mesh.attributes[i1], &a2 = mesh.attributes[i2];
                    a.infection = a0.infection * b0 + a1.infection * b1 + a2.infection * b2;
                    a.glow = a0.glow * b0 + a1.glow * b1 + a2.glow * b2;
                    a.crack = a0.crack * b0 + a1.crack * b1 + a2.crack * b2;
                    a.plateTint = a0.plateTint * b0 + a1.plateTint * b1 + a2.plateTint * b2;
                    a.flesh = a0.flesh * b0 + a1.flesh * b1 + a2.flesh * b2;
                }
                colour[pix] = shade(nrm, normalize(cam.eye - pos), a, light);
            }
    }
}

}  // namespace

Image renderPreview(const Mesh& mesh, const PreviewOptions& options) {
    Image image;
    const int views = static_cast<int>(std::max<size_t>(1, options.yaws.size()));
    image.width = options.width * views;
    image.height = options.height;
    image.rgb.assign(static_cast<size_t>(image.width) * image.height * 3, 0);
    if (mesh.positions.empty()) return image;

    Mesh shaded = mesh;
    if (shaded.normals.size() != shaded.positions.size()) shaded.computeNormals();

    Vec3 lo, hi;
    mesh.bounds(lo, hi);
    const Vec3 centre = (lo + hi) * 0.5f;
    const float radius = length(hi - lo) * 0.5f;
    const int ss = std::max(1, options.supersample);
    const int W = options.width * ss, H = options.height * ss;
    const float fov = 30.0f * 3.14159265f / 180.0f;
    const float distance = radius / std::sin(fov * 0.5f);
    const float pitch = options.pitch * 3.14159265f / 180.0f;

    std::vector<Vec3> buffer;
    for (int v = 0; v < views; ++v) {
        float yaw = (options.yaws.empty() ? 0.0f : options.yaws[v]) * 3.14159265f / 180.0f;
        Camera cam;
        Vec3 back{std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
        cam.eye = centre + back * distance;
        cam.forward = -back;
        cam.right = normalize(cross(cam.forward, Vec3{0, 1, 0}));
        cam.up = cross(cam.right, cam.forward);
        // Frame the projected silhouette with a small margin.
        float xlo = 1e30f, xhi = -1e30f, ylo = 1e30f, yhi = -1e30f;
        for (const auto& p : shaded.positions) {
            Vec3 d = p - cam.eye;
            float z = dot(d, cam.forward);
            if (z <= 1e-4f) continue;
            float x = dot(d, cam.right) / z, y = dot(d, cam.up) / z;
            xlo = std::min(xlo, x);
            xhi = std::max(xhi, x);
            ylo = std::min(ylo, y);
            yhi = std::max(yhi, y);
        }
        cam.focal = std::min(W * 0.92f / std::max(1e-6f, xhi - xlo), H * 0.92f / std::max(1e-6f, yhi - ylo));
        cam.cx = W * 0.5f - cam.focal * 0.5f * (xlo + xhi);
        cam.cy = H * 0.5f + cam.focal * 0.5f * (ylo + yhi);
        Light light;
        light.key = normalize(cam.right * 0.55f + cam.up * 0.75f + back * 0.6f);
        light.fill = normalize(cam.right * -0.7f + cam.up * 0.1f + back * 0.3f);

        renderView(shaded, cam, light, W, H, buffer);
        for (int y = 0; y < options.height; ++y)
            for (int x = 0; x < options.width; ++x) {
                Vec3 sum{0, 0, 0};
                for (int j = 0; j < ss; ++j)
                    for (int i = 0; i < ss; ++i) sum += buffer[static_cast<size_t>(y * ss + j) * W + (x * ss + i)];
                sum = sum / static_cast<float>(ss * ss);
                size_t o = (static_cast<size_t>(y) * image.width + v * options.width + x) * 3;
                image.rgb[o] = toByte(sum.x);
                image.rgb[o + 1] = toByte(sum.y);
                image.rgb[o + 2] = toByte(sum.z);
            }
    }
    return image;
}

// Minimal PNG encoder: stored (uncompressed) deflate blocks. Large files,
// zero dependencies.
bool writePng(const std::string& path, const Image& image) {
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        ready = true;
    }
    auto crc = [&](const std::vector<uint8_t>& d, size_t from) {
        uint32_t c = 0xFFFFFFFFu;
        for (size_t i = from; i < d.size(); ++i) c = table[(c ^ d[i]) & 0xFF] ^ (c >> 8);
        return c ^ 0xFFFFFFFFu;
    };
    auto be32 = [](std::vector<uint8_t>& d, uint32_t v) {
        d.push_back(static_cast<uint8_t>(v >> 24));
        d.push_back(static_cast<uint8_t>(v >> 16));
        d.push_back(static_cast<uint8_t>(v >> 8));
        d.push_back(static_cast<uint8_t>(v));
    };
    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    auto chunk = [&](const char* type, const std::vector<uint8_t>& data) {
        be32(out, static_cast<uint32_t>(data.size()));
        size_t start = out.size();
        out.insert(out.end(), type, type + 4);
        out.insert(out.end(), data.begin(), data.end());
        be32(out, crc(out, start));
    };

    std::vector<uint8_t> ihdr;
    be32(ihdr, static_cast<uint32_t>(image.width));
    be32(ihdr, static_cast<uint32_t>(image.height));
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});
    chunk("IHDR", ihdr);

    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>(image.height) * (image.width * 3 + 1));
    for (int y = 0; y < image.height; ++y) {
        raw.push_back(0);
        auto row = image.rgb.begin() + static_cast<long>(y) * image.width * 3;
        raw.insert(raw.end(), row, row + image.width * 3);
    }
    std::vector<uint8_t> z = {0x78, 0x01};
    uint32_t a = 1, b = 0;
    for (uint8_t byte : raw) {
        a = (a + byte) % 65521u;
        b = (b + a) % 65521u;
    }
    for (size_t pos = 0; pos < raw.size() || pos == 0;) {
        size_t len = std::min<size_t>(65535, raw.size() - pos);
        bool last = pos + len >= raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(static_cast<uint8_t>(len));
        z.push_back(static_cast<uint8_t>(len >> 8));
        z.push_back(static_cast<uint8_t>(~len));
        z.push_back(static_cast<uint8_t>(~len >> 8));
        z.insert(z.end(), raw.begin() + static_cast<long>(pos), raw.begin() + static_cast<long>(pos + len));
        pos += len;
        if (last) break;
    }
    be32(z, (b << 16) | a);
    chunk("IDAT", z);
    chunk("IEND", {});

    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
    f.close();
    return !f.fail();
}

}  // namespace eclipse
