#pragma once

#include "core/Orientation.h"
#include "core/Prototile.h"
#include "core/Result.h"
#include "core/geometry/Polygon.h"

#include <optional>
#include <vector>

namespace tiles {

enum class LatticeOrientationErrorCode {
    empty_orientation_set,
    unsupported_orientation,
    coordinate_overflow,
    polygon_construction_failed,
    normalization_overflow,
};

struct LatticeOrientationError final {
    LatticeOrientationErrorCode code;
    // Populated for every failure tied to one requested orientation.
    std::optional<Orientation> orientation;
    // Populated only for polygon_construction_failed.
    std::optional<PolygonError> polygon_error;
};

// A prototile compiled into one admitted lattice orientation together with the
// resulting canonical local geometry. Value-owning and immutable after
// construction. Its polygon is validated, translation-normalized q16.48 local
// geometry: not a world-space footprint and not arrangement state.
//
// There is no public constructor pairing arbitrary geometry with a claimed
// orientation; only compile_lattice_orientations may publish the value, so a
// stored polygon is always an exact rotation of its prototile's reference
// polygon.
class OrientedPrototile final {
public:
    const Prototile &prototile() const {
        return prototile_;
    }

    // The representative orientation: always the least of the equivalents.
    Orientation orientation() const {
        return equivalent_orientations_.front();
    }

    // The complete sorted nonempty set of requested angles whose compiled
    // geometry is identical to this one. Keeps deduplication honest: it records
    // exactly which admitted angles collapsed onto the same boundary.
    const std::vector<Orientation> &equivalent_orientations() const {
        return equivalent_orientations_;
    }

    const Polygon &canonical_polygon() const {
        return canonical_polygon_;
    }

private:
    OrientedPrototile(
        Prototile p_prototile,
        std::vector<Orientation> p_equivalent_orientations,
        Polygon p_canonical_polygon);

    Prototile prototile_;
    std::vector<Orientation> equivalent_orientations_;
    Polygon canonical_polygon_;

    friend Result<std::vector<OrientedPrototile>, LatticeOrientationError>
    compile_lattice_orientations(
        const Prototile &p_prototile, std::vector<Orientation> p_requested);
};

// Compile a finite requested set of lattice orientations of one prototile into
// distinct oriented prototiles. The input is interpreted as a mathematical set:
// order and duplicate angles carry no meaning.
//
// The compiler rejects an empty set; rejects any orientation other than the four
// quarter turns 0/1, 1/4, 1/2, 3/4; compiles every requested orientation
// directly from the reference polygon (never from another compiled result);
// groups results whose canonical boundaries are equal; assigns each group its
// least requested orientation as representative; and returns groups in
// representative angular order. Any single failure returns only the error, with
// no partially compiled sequence.
Result<std::vector<OrientedPrototile>, LatticeOrientationError>
compile_lattice_orientations(
    const Prototile &p_prototile, std::vector<Orientation> p_requested);

} // namespace tiles
