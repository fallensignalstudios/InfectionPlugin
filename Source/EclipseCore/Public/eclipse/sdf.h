#pragma once

#include "eclipse/api.h"

#include <functional>
#include <vector>

#include "eclipse/math.h"
#include "eclipse/mesh.h"

namespace eclipse {

// A signed distance: negative inside, positive outside.
using DistanceFn = std::function<float(const Vec3&)>;

ECLIPSECORE_API float sdRoundCone(const Vec3& p, const Vec3& a, const Vec3& b, float ra, float rb);
ECLIPSECORE_API float sdEllipsoid(const Vec3& p, const Vec3& centre, const Vec3& radii);
ECLIPSECORE_API float sdBox(const Vec3& p, const Vec3& centre, const Vec3& halfExtents, float rounding = 0.0f);
// Polynomial smooth minimum; k is the blend radius in metres.
ECLIPSECORE_API float smoothMin(float a, float b, float k);

// A union of primitives, each blended into what came before it.
class SdfModel {
public:
    ECLIPSECORE_API void roundCone(const Vec3& a, const Vec3& b, float ra, float rb, float blend);
    void capsule(const Vec3& a, const Vec3& b, float r, float blend) { roundCone(a, b, r, r, blend); }
    ECLIPSECORE_API void ellipsoid(const Vec3& centre, const Vec3& radii, float blend);
    void sphere(const Vec3& centre, float r, float blend) { ellipsoid(centre, {r, r, r}, blend); }
    ECLIPSECORE_API void box(const Vec3& centre, const Vec3& halfExtents, float rounding, float blend);

    ECLIPSECORE_API float evaluate(const Vec3& p) const;
    bool empty() const { return prims_.empty(); }
    // Conservative bounds of the surface.
    ECLIPSECORE_API void bounds(Vec3& lo, Vec3& hi) const;

private:
    enum class Kind { RoundCone, Ellipsoid, Box };
    struct Prim {
        Kind kind;
        Vec3 a, b;   // cone ends, or centre and radii / half extents
        float ra, rb;
        float blend;
    };
    std::vector<Prim> prims_;
};

// Polygonises a distance field with naive surface nets over the box
// [lo, hi]. The result is closed wherever the surface stays inside the box,
// with normals taken from the field gradient.
ECLIPSECORE_API Mesh polygonize(const DistanceFn& field, const Vec3& lo, const Vec3& hi, float cellSize);

}  // namespace eclipse
