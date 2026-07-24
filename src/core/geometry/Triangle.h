#pragma once

#include "core/geometry/Point.h"

#include <array>

namespace tiles {

// Three polygon vertices, stored counterclockwise. A triangulation triangle
// introduces no new coordinate; every vertex is a vertex of its polygon.
struct Triangle final {
    std::array<Point, 3> vertices;
};

} // namespace tiles
