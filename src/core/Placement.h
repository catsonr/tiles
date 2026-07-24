#pragma once

#include "core/Orientation.h"
#include "core/OrientedPrototile.h"
#include "core/Prototile.h"
#include "core/Result.h"
#include "core/geometry/Point.h"
#include "core/geometry/Polygon.h"

namespace tiles {

enum class PlacementError {
    footprint_overflow,
    footprint_construction_failed,
};

// An oriented prototile together with one exact fixed-point translation, plus
// the derived footprint polygon. Immutable after construction and value-owning:
// it holds its own oriented prototile and footprint, so it can never dangle
// against externally owned geometry.
//
// The stored oriented canonical polygon stays local and untranslated. A new
// placement is always built from an oriented prototile and a translation, never
// by transforming another placement's footprint, so no footprint is ever
// translated twice and no orientation is ever derived from a placed result.
class Placement final {
public:
    // Translate every oriented canonical vertex by p_translation with checked
    // arithmetic and rebuild the footprint through the Polygon factory.
    static Result<Placement, PlacementError> make(
        const OrientedPrototile &p_oriented, Point p_translation);

    // The underlying prototile, in its reference orientation and local geometry.
    const Prototile &prototile() const {
        return oriented_.prototile();
    }

    // The representative orientation of the placed geometry.
    Orientation orientation() const {
        return oriented_.orientation();
    }

    // The oriented canonical local polygon, before translation.
    const Polygon &oriented_polygon() const {
        return oriented_.canonical_polygon();
    }

    Point translation() const {
        return translation_;
    }

    const Polygon &footprint() const {
        return footprint_;
    }

private:
    OrientedPrototile oriented_;
    Point translation_;
    Polygon footprint_;

    Placement(OrientedPrototile p_oriented, Point p_translation, Polygon p_footprint);
};

} // namespace tiles
