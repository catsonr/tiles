#include "core/Prototile.h"

namespace tiles {

Prototile::Prototile(PrototileId p_id, Polygon p_polygon) :
    id_(p_id),
    polygon_(std::move(p_polygon)) {}

Result<Prototile, PrototileError> Prototile::make(PrototileId p_id, const Polygon &p_polygon) {
    // Translate the canonical first vertex to the local origin through the shared
    // checked normalization helper. Any component that leaves the lattice is a
    // normalization overflow.
    auto normalized = translation_normalize(p_polygon);
    if (!normalized.has_value()) {
        return Result<Prototile, PrototileError>::failure(
            PrototileError::normalization_overflow);
    }

    return Result<Prototile, PrototileError>::success(
        Prototile(p_id, std::move(normalized).value()));
}

} // namespace tiles
