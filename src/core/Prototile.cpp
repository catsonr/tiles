#include "core/Prototile.h"

#include "core/geometry/Point.h"

#include <cassert>

namespace tiles {

Prototile::Prototile(PrototileId p_id, Polygon p_polygon) :
    id_(p_id),
    polygon_(std::move(p_polygon)) {}

Result<Prototile, PrototileError> Prototile::make(PrototileId p_id, const Polygon &p_polygon) {
    const Polygon::Vertices &source = p_polygon.vertices();
    const Point origin = source.front();

    // Translate the canonical first vertex to the local origin with checked
    // arithmetic. Any component that leaves the lattice is a normalization
    // overflow.
    Polygon::Vertices translated;
    translated.reserve(source.size());
    for (const Point &vertex : source) {
        auto shifted = checked_subtract(vertex, origin);
        if (!shifted.has_value()) {
            return Result<Prototile, PrototileError>::failure(
                PrototileError::normalization_overflow);
        }
        translated.push_back(shifted.value());
    }

    // Reconstruct through the validated factory rather than granting privileged
    // access to an incomplete polygon. Translation preserves simplicity, the
    // lexicographic minimum, winding, and triangulation topology, so this always
    // succeeds for an already-valid polygon.
    auto rebuilt = Polygon::make(std::move(translated));
    assert(rebuilt.has_value());

    return Result<Prototile, PrototileError>::success(
        Prototile(p_id, std::move(rebuilt).value()));
}

} // namespace tiles
