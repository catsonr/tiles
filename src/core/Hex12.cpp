#include "core/Hex12.h"

#include "core/geometry/Coordinate.h"
#include "core/geometry/Point.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <utility>

namespace tiles {

// The bridge declared in OrientedPrototile.h. Defined only here, friended solely
// to call the private constructor, and never exposed as a header-visible
// unchecked construction operation.
class Hex12OrientedPrototileAccess final {
public:
    static OrientedPrototile publish(
        Prototile p_prototile,
        std::vector<Orientation> p_equivalent_orientations,
        Polygon p_canonical_polygon) {
        return OrientedPrototile(
            std::move(p_prototile),
            std::move(p_equivalent_orientations),
            std::move(p_canonical_polygon));
    }
};

namespace {

using Storage = Coordinate::Storage;

// ---------------------------------------------------------------------------
// the exact unit hex-12 direction module
// ---------------------------------------------------------------------------

// One game unit, and one half unit. Both are exact on the lattice.
constexpr Storage Q = Coordinate::SCALE;
constexpr Storage H = Q / 2;

// The q16.48 projection of sqrt(3) / 2, rounded to nearest. It is a literal
// integer: never std::sqrt, trigonometry, a floating-point literal, or runtime
// quantization. It is the module's single approximation, and no later step
// rounds, snaps, averages, or repairs anything.
constexpr Storage R = 243764480361061;

constexpr __int128 wide(Storage p_value) {
    return static_cast<__int128>(p_value);
}

// R is the unique integer nearest sqrt(3) * Q / 2, because
//
//     (2R - 1)^2 < 3Q^2 < (2R + 1)^2
//
// which places 2R within a half unit of the irrational 2 * (sqrt(3) * Q / 2).
// Every product here is at most ~2.4e29 and stays exact in signed 128-bit. The
// native test carries this proof independently.
static_assert(
    wide(2 * R - 1) * wide(2 * R - 1) < 3 * wide(Q) * wide(Q),
    "R must exceed the lower half-unit bound of sqrt(3) * Q / 2");
static_assert(
    3 * wide(Q) * wide(Q) < wide(2 * R + 1) * wide(2 * R + 1),
    "R must fall below the upper half-unit bound of sqrt(3) * Q / 2");

constexpr Point direction(Storage p_x, Storage p_y) {
    return Point { Coordinate::from_raw(p_x), Coordinate::from_raw(p_y) };
}

// The twelve counterclockwise unit direction vectors, one every thirty degrees.
// Written out so the table reads exactly as it is defined mathematically.
constexpr std::array<Point, 12> DIRECTIONS = { {
    direction(Q, 0),
    direction(R, H),
    direction(H, R),
    direction(0, Q),
    direction(-H, R),
    direction(-R, H),
    direction(-Q, 0),
    direction(-R, -H),
    direction(-H, -R),
    direction(0, -Q),
    direction(H, -R),
    direction(R, -H),
} };

// d[(k + 6) mod 12] == -d[k] for every k. Opposite directions are exact
// negatives, so two tiles meeting along a shared edge agree bit for bit rather
// than nearly.
constexpr bool opposite_directions_are_exact() {
    for (std::size_t k = 0; k < 6; ++k) {
        if (DIRECTIONS[k + 6].x.raw() != -DIRECTIONS[k].x.raw()) {
            return false;
        }
        if (DIRECTIONS[k + 6].y.raw() != -DIRECTIONS[k].y.raw()) {
            return false;
        }
    }
    return true;
}

static_assert(
    opposite_directions_are_exact(),
    "the latter six module directions must be exact negatives of the first six");

constexpr std::size_t DIRECTION_COUNT = 12;

// ---------------------------------------------------------------------------
// admitted polygons and orientations
// ---------------------------------------------------------------------------

// A regular polygon's side count together with the direction stride its edges
// advance by. Their product is always twelve.
struct PolygonShape final {
    std::size_t sides;
    std::size_t stride;
};

// Nothing for a value outside the enumeration, so an arbitrary integer cast to
// Hex12RegularPolygon is rejected before any geometry is constructed.
std::optional<PolygonShape> shape_of(Hex12RegularPolygon p_polygon) {
    switch (p_polygon) {
        case Hex12RegularPolygon::triangle:
            return PolygonShape { 3, 4 };
        case Hex12RegularPolygon::square:
            return PolygonShape { 4, 3 };
        case Hex12RegularPolygon::hexagon:
            return PolygonShape { 6, 2 };
        case Hex12RegularPolygon::dodecagon:
            return PolygonShape { 12, 1 };
    }
    return std::nullopt;
}

// Map an admitted orientation onto its module phase, or nothing if the angle is
// not one of the twelve twelfth turns. Comparison is against canonical
// Orientation values, so equivalent fractions such as 1/6, 1/4, 1/3, and 1/2 are
// recognized as phases 2, 3, 4, and 6. No angle is ever converted to floating
// point.
std::optional<std::size_t> phase_of(Orientation p_orientation) {
    for (std::size_t k = 0; k < DIRECTION_COUNT; ++k) {
        auto twelfth = Orientation::make(
            static_cast<Orientation::Component>(k),
            static_cast<Orientation::Component>(DIRECTION_COUNT));
        if (twelfth.has_value() && p_orientation == twelfth.value()) {
            return k;
        }
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// error construction
// ---------------------------------------------------------------------------

Hex12CompilationError plain_error(Hex12CompilationErrorCode p_code) {
    return Hex12CompilationError {
        p_code, std::nullopt, std::nullopt, std::nullopt
    };
}

Hex12CompilationError phase_error(
    Hex12CompilationErrorCode p_code, Orientation p_orientation) {
    return Hex12CompilationError {
        p_code, p_orientation, std::nullopt, std::nullopt
    };
}

// ---------------------------------------------------------------------------
// direct boundary construction
// ---------------------------------------------------------------------------

// Walk the module from the local origin, accumulating one unit edge per side.
// Emits vertices 0 through n - 1; the final accumulated endpoint is the implicit
// closing vertex and must land back on vertex zero exactly. Cumulative
// construction uses checked point arithmetic, so an unexpected departure from
// the lattice is a typed error rather than a wrap, a saturation, or undefined
// behavior.
Result<Polygon::Vertices, Hex12CompilationError> build_boundary(
    std::size_t p_phase, const PolygonShape &p_shape, Orientation p_label) {
    using ResultType = Result<Polygon::Vertices, Hex12CompilationError>;

    const Point origin = direction(0, 0);

    Polygon::Vertices ring;
    ring.reserve(p_shape.sides);

    Point cursor = origin;
    for (std::size_t j = 0; j < p_shape.sides; ++j) {
        ring.push_back(cursor);

        const Point edge =
            DIRECTIONS[(p_phase + j * p_shape.stride) % DIRECTION_COUNT];
        auto advanced = checked_add(cursor, edge);
        if (!advanced.has_value()) {
            return ResultType::failure(phase_error(
                Hex12CompilationErrorCode::coordinate_overflow, p_label));
        }
        cursor = advanced.value();
    }

    if (cursor != origin) {
        return ResultType::failure(phase_error(
            Hex12CompilationErrorCode::boundary_did_not_close, p_label));
    }

    return ResultType::success(std::move(ring));
}

// One phase compiled from the module into validated, translation-normalized
// canonical geometry. p_label names the requested angle the failure belongs to;
// it never influences the construction, which depends only on the phase.
Result<Polygon, Hex12CompilationError> compile_phase(
    std::size_t p_phase, const PolygonShape &p_shape, Orientation p_label) {
    using ResultType = Result<Polygon, Hex12CompilationError>;

    auto ring = build_boundary(p_phase, p_shape, p_label);
    if (!ring.has_value()) {
        return ResultType::failure(ring.error());
    }

    auto built = Polygon::make(std::move(ring).value());
    if (!built.has_value()) {
        return ResultType::failure(Hex12CompilationError {
            Hex12CompilationErrorCode::polygon_construction_failed,
            p_label, built.error(), std::nullopt });
    }

    auto normalized = translation_normalize(built.value());
    if (!normalized.has_value()) {
        return ResultType::failure(phase_error(
            Hex12CompilationErrorCode::normalization_overflow, p_label));
    }

    return ResultType::success(std::move(normalized).value());
}

// One requested orientation compiled directly from the module.
struct Compiled final {
    Orientation orientation;
    Polygon polygon;
};

} // namespace

Result<std::vector<OrientedPrototile>, Hex12CompilationError>
compile_hex12_orientations(
    PrototileId p_id,
    Hex12RegularPolygon p_polygon,
    std::vector<Orientation> p_requested) {
    using ResultType =
        Result<std::vector<OrientedPrototile>, Hex12CompilationError>;

    if (p_requested.empty()) {
        return ResultType::failure(
            plain_error(Hex12CompilationErrorCode::empty_orientation_set));
    }

    const auto shape = shape_of(p_polygon);
    if (!shape.has_value()) {
        return ResultType::failure(
            plain_error(Hex12CompilationErrorCode::unsupported_polygon));
    }

    // Interpret the input as a set: sort into angular order and drop duplicate
    // angles. This also makes the first entry of each later boundary group the
    // least admitted angle in that group.
    std::sort(p_requested.begin(), p_requested.end());
    p_requested.erase(
        std::unique(p_requested.begin(), p_requested.end()), p_requested.end());

    // Reject the least unsupported angle before compiling anything.
    for (const Orientation orientation : p_requested) {
        if (!phase_of(orientation).has_value()) {
            return ResultType::failure(phase_error(
                Hex12CompilationErrorCode::unsupported_orientation, orientation));
        }
    }

    // The compiler owns its reference geometry: phase zero of the named regular
    // polygon, checked and normalized exactly like every other phase.
    auto reference_polygon =
        compile_phase(0, shape.value(), Orientation::reference());
    if (!reference_polygon.has_value()) {
        return ResultType::failure(reference_polygon.error());
    }

    auto prototile = Prototile::make(p_id, reference_polygon.value());
    if (!prototile.has_value()) {
        return ResultType::failure(Hex12CompilationError {
            Hex12CompilationErrorCode::prototile_construction_failed,
            Orientation::reference(), std::nullopt, prototile.error() });
    }

    // Compile every retained orientation directly from the module, never from
    // the reference polygon and never from another compiled phase.
    std::vector<Compiled> compiled;
    compiled.reserve(p_requested.size());
    for (const Orientation orientation : p_requested) {
        auto polygon =
            compile_phase(*phase_of(orientation), shape.value(), orientation);
        if (!polygon.has_value()) {
            return ResultType::failure(polygon.error());
        }
        compiled.push_back(Compiled { orientation, std::move(polygon).value() });
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
        groups.push_back(Hex12OrientedPrototileAccess::publish(
            prototile.value(), std::move(equivalents), compiled[i].polygon));
    }

    return ResultType::success(std::move(groups));
}

} // namespace tiles
