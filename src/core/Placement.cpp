#include "core/Placement.h"

#include <utility>

namespace tiles {

Placement::Placement(Prototile p_prototile, Point p_translation, Polygon p_footprint) :
    prototile_(std::move(p_prototile)),
    translation_(p_translation),
    footprint_(std::move(p_footprint)) {}

Result<Placement, PlacementError> Placement::make(
    const Prototile &p_prototile, Point p_translation) {
    const Polygon::Vertices &canonical = p_prototile.polygon().vertices();

    // Checked-add the translation to every canonical vertex. Any component that
    // leaves the lattice is a footprint overflow: the placement has no
    // representable footprint and must not be published.
    Polygon::Vertices translated;
    translated.reserve(canonical.size());
    for (const Point &vertex : canonical) {
        auto shifted = checked_add(vertex, p_translation);
        if (!shifted.has_value()) {
            return Result<Placement, PlacementError>::failure(
                PlacementError::footprint_overflow);
        }
        translated.push_back(shifted.value());
    }

    // Rebuild through the validated factory. Exact translation preserves
    // simplicity, winding, the lexicographic minimum, and triangulation
    // topology, so a failure here is an invariant violation rather than invalid
    // input; it is surfaced as an error instead of publishing a broken
    // placement.
    auto footprint = Polygon::make(std::move(translated));
    if (!footprint.has_value()) {
        return Result<Placement, PlacementError>::failure(
            PlacementError::footprint_construction_failed);
    }

    return Result<Placement, PlacementError>::success(
        Placement(p_prototile, p_translation, std::move(footprint).value()));
}

} // namespace tiles
