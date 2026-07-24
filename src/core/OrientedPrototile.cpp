#include "core/OrientedPrototile.h"

#include "core/geometry/Rotation.h"

#include <algorithm>
#include <optional>
#include <utility>

namespace tiles {

namespace {

// Map an admitted orientation onto its quarter turn, or nothing if the angle is
// not one of the four supported lattice orientations.
std::optional<QuarterTurn> quarter_turn_of(Orientation p_orientation) {
    if (p_orientation == Orientation::reference()) {
        return QuarterTurn::zero;
    }
    if (p_orientation == Orientation::quarter()) {
        return QuarterTurn::quarter;
    }
    if (p_orientation == Orientation::half()) {
        return QuarterTurn::half;
    }
    if (p_orientation == Orientation::three_quarter()) {
        return QuarterTurn::three_quarter;
    }
    return std::nullopt;
}

// One requested orientation compiled directly from the reference polygon.
struct Compiled {
    Orientation orientation;
    Polygon polygon;
};

LatticeOrientationError error_for(
    LatticeOrientationErrorCode p_code, Orientation p_orientation) {
    return LatticeOrientationError { p_code, p_orientation, std::nullopt };
}

} // namespace

OrientedPrototile::OrientedPrototile(
    Prototile p_prototile,
    std::vector<Orientation> p_equivalent_orientations,
    Polygon p_canonical_polygon) :
    prototile_(std::move(p_prototile)),
    equivalent_orientations_(std::move(p_equivalent_orientations)),
    canonical_polygon_(std::move(p_canonical_polygon)) {}

Result<std::vector<OrientedPrototile>, LatticeOrientationError>
compile_lattice_orientations(
    const Prototile &p_prototile, std::vector<Orientation> p_requested) {
    using ResultType =
        Result<std::vector<OrientedPrototile>, LatticeOrientationError>;

    if (p_requested.empty()) {
        return ResultType::failure(LatticeOrientationError {
            LatticeOrientationErrorCode::empty_orientation_set,
            std::nullopt, std::nullopt });
    }

    // Interpret the input as a set: sort into angular order and drop duplicate
    // angles. This also makes the first entry of each later boundary group the
    // least admitted angle in that group.
    std::sort(p_requested.begin(), p_requested.end());
    p_requested.erase(
        std::unique(p_requested.begin(), p_requested.end()), p_requested.end());

    // Reject any non-quarter turn before compiling anything.
    for (const Orientation orientation : p_requested) {
        if (!quarter_turn_of(orientation).has_value()) {
            return ResultType::failure(error_for(
                LatticeOrientationErrorCode::unsupported_orientation, orientation));
        }
    }

    // Compile every requested orientation directly from the reference polygon.
    std::vector<Compiled> compiled;
    compiled.reserve(p_requested.size());
    for (const Orientation orientation : p_requested) {
        const QuarterTurn turn = *quarter_turn_of(orientation);

        Polygon::Vertices rotated;
        rotated.reserve(p_prototile.polygon().vertices().size());
        for (const Point &vertex : p_prototile.polygon().vertices()) {
            auto turned = rotate_point(vertex, turn);
            if (!turned.has_value()) {
                return ResultType::failure(error_for(
                    LatticeOrientationErrorCode::coordinate_overflow, orientation));
            }
            rotated.push_back(turned.value());
        }

        auto built = Polygon::make(std::move(rotated));
        if (!built.has_value()) {
            return ResultType::failure(LatticeOrientationError {
                LatticeOrientationErrorCode::polygon_construction_failed,
                orientation, built.error() });
        }

        auto normalized = translation_normalize(built.value());
        if (!normalized.has_value()) {
            return ResultType::failure(error_for(
                LatticeOrientationErrorCode::normalization_overflow, orientation));
        }

        compiled.push_back(Compiled { orientation, std::move(normalized).value() });
    }

    // Group compiled results with equal canonical boundaries. Because the
    // requested angles were sorted, scanning ascending makes the first unused
    // entry of each group its least angle, and the groups themselves appear in
    // representative angular order.
    std::vector<OrientedPrototile> groups;
    std::vector<bool> used(compiled.size(), false);
    for (std::size_t i = 0; i < compiled.size(); ++i) {
        if (used[i]) {
            continue;
        }
        std::vector<Orientation> equivalents;
        equivalents.push_back(compiled[i].orientation);
        used[i] = true;
        for (std::size_t j = i + 1; j < compiled.size(); ++j) {
            if (used[j]) {
                continue;
            }
            if (same_boundary(compiled[i].polygon, compiled[j].polygon)) {
                equivalents.push_back(compiled[j].orientation);
                used[j] = true;
            }
        }
        groups.push_back(OrientedPrototile(
            p_prototile, std::move(equivalents), compiled[i].polygon));
    }

    return ResultType::success(std::move(groups));
}

} // namespace tiles
