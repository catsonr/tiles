#pragma once

#include "core/Prototile.h"
#include "core/Result.h"
#include "core/geometry/Point.h"
#include "core/geometry/Polygon.h"

namespace tiles {

enum class PlacementError {
    footprint_overflow,
    footprint_construction_failed,
};

// A prototile in its reference orientation together with one exact fixed-point
// translation, plus the derived footprint polygon. Immutable after
// construction and value-owning: it holds its own prototile and footprint, so
// it can never dangle against externally owned geometry.
//
// The stored prototile polygon stays canonical and untranslated. A new
// placement is always built from a prototile and a translation, never by
// transforming another placement's footprint, so no footprint is ever
// translated twice.
class Placement final {
public:
    // Translate every canonical prototile vertex by p_translation with checked
    // arithmetic and rebuild the footprint through the Polygon factory.
    static Result<Placement, PlacementError> make(
        const Prototile &p_prototile, Point p_translation);

    const Prototile &prototile() const {
        return prototile_;
    }

    Point translation() const {
        return translation_;
    }

    const Polygon &footprint() const {
        return footprint_;
    }

private:
    Prototile prototile_;
    Point translation_;
    Polygon footprint_;

    Placement(Prototile p_prototile, Point p_translation, Polygon p_footprint);
};

} // namespace tiles
