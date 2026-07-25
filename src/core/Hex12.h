#pragma once

#include "core/Orientation.h"
#include "core/OrientedPrototile.h"
#include "core/Prototile.h"
#include "core/Result.h"
#include "core/geometry/Polygon.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace tiles {

// The regular polygons the unit-side hexagonal twelve-direction module admits.
// Each has a side length of exactly one game unit and a side count dividing
// twelve, so every edge lies along one of the module's twelve directions. The
// enumeration is closed: it is not a side count, and there is no arbitrary
// regular n-gon.
//
// The underlying type is fixed so that casting any integer to this enumeration
// is well defined. A value outside the four enumerators is then an ordinary
// rejected input rather than an unspecified one, and the compiler answers
// unsupported_polygon without constructing geometry.
enum class Hex12RegularPolygon : std::uint8_t {
    triangle,
    square,
    hexagon,
    dodecagon,
};

enum class Hex12CompilationErrorCode {
    empty_orientation_set,
    unsupported_polygon,
    unsupported_orientation,
    coordinate_overflow,
    boundary_did_not_close,
    polygon_construction_failed,
    prototile_construction_failed,
    normalization_overflow,
};

struct Hex12CompilationError final {
    Hex12CompilationErrorCode code;
    // Populated for every failure tied to one requested or constructed phase.
    // Reference construction is attributed to Orientation::reference().
    std::optional<Orientation> orientation;
    // Populated only for polygon_construction_failed.
    std::optional<PolygonError> polygon_error;
    // Populated only for prototile_construction_failed.
    std::optional<PrototileError> prototile_error;
};

// Compile a finite requested set of twelfth-turn orientations of one unit-side
// regular hex-12 polygon into distinct oriented prototiles. The input is
// interpreted as a mathematical set: order and duplicate spellings carry no
// meaning.
//
// The compiler owns its source geometry. A caller names a regular polygon and an
// identity; it cannot submit an arbitrary q16.48 boundary and claim it is a
// regular hex-12 tile. Every boundary is constructed directly from the module's
// twelve fixed direction vectors for its own phase — never by rotating the
// reference polygon and never by transforming another compiled phase — so the
// module's single fixed projection is the only approximation anywhere in the
// result.
//
// The compiler rejects an empty set; rejects a polygon value outside the
// enumeration; rejects any angle other than the twelve twelfth turns k/12
// (equivalent spellings such as 1/6, 1/4, 1/3, and 1/2 are canonical twelfth
// turns and are admitted); groups results whose canonical boundaries are equal;
// assigns each group its least requested orientation as representative; and
// returns groups in representative angular order. Any single failure returns only
// the error, with no partially compiled sequence.
//
// The product is the same runtime value tier 1 publishes: validated,
// translation-normalized canonical q16.48 geometry. Placement, joining,
// intersection, containment, and arrangement consume it without knowing that
// hex-12 exists.
Result<std::vector<OrientedPrototile>, Hex12CompilationError>
compile_hex12_orientations(
    PrototileId p_id,
    Hex12RegularPolygon p_polygon,
    std::vector<Orientation> p_requested);

} // namespace tiles
