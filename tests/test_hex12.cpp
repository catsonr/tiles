#include "TestHarness.h"

#include "core/Arrangement.h"
#include "core/Hex12.h"
#include "core/Orientation.h"
#include "core/OrientedPrototile.h"
#include "core/Placement.h"
#include "core/Prototile.h"
#include "core/geometry/Alignment.h"
#include "core/geometry/Intersection.h"
#include "core/geometry/Point.h"
#include "core/geometry/Polygon.h"
#include "core/geometry/Predicates.h"
#include "core/geometry/Triangle.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

using namespace tiles;
using tiles_test::raw_pt;

namespace {

Point raw(std::int64_t p_x, std::int64_t p_y) {
    return raw_pt(p_x, p_y);
}

// ---------------------------------------------------------------------------
// independently authored module constants
// ---------------------------------------------------------------------------
//
// Every expected value below is written out here rather than read from any
// production accessor, so the tests can disagree with the compiler.

// 2^48 and 2^47.
constexpr std::int64_t EXPECTED_Q = 281474976710656;
constexpr std::int64_t EXPECTED_H = 140737488355328;
// The q16.48 projection of sqrt(3) / 2, rounded to nearest.
constexpr std::int64_t EXPECTED_R = 243764480361061;

// The twelve counterclockwise unit directions, thirty degrees apart.
const std::array<Point, 12> &expected_directions() {
    static const std::array<Point, 12> directions = { {
        raw(EXPECTED_Q, 0),
        raw(EXPECTED_R, EXPECTED_H),
        raw(EXPECTED_H, EXPECTED_R),
        raw(0, EXPECTED_Q),
        raw(-EXPECTED_H, EXPECTED_R),
        raw(-EXPECTED_R, EXPECTED_H),
        raw(-EXPECTED_Q, 0),
        raw(-EXPECTED_R, -EXPECTED_H),
        raw(-EXPECTED_H, -EXPECTED_R),
        raw(0, -EXPECTED_Q),
        raw(EXPECTED_H, -EXPECTED_R),
        raw(EXPECTED_R, -EXPECTED_H),
    } };
    return directions;
}

Point expected_direction(std::size_t p_step) {
    return expected_directions()[p_step % 12];
}

// ---------------------------------------------------------------------------
// small fixture helpers
// ---------------------------------------------------------------------------

Orientation twelfth(std::uint32_t p_step) {
    return Orientation::make(p_step, 12).value();
}

std::vector<Orientation> all_twelve() {
    std::vector<Orientation> requested;
    for (std::uint32_t k = 0; k < 12; ++k) {
        requested.push_back(twelfth(k));
    }
    return requested;
}

// The four admitted polygons, keyed by side count, with a fixed test-only id.
Hex12RegularPolygon polygon_of(std::size_t p_sides) {
    switch (p_sides) {
        case 3:
            return Hex12RegularPolygon::triangle;
        case 4:
            return Hex12RegularPolygon::square;
        case 6:
            return Hex12RegularPolygon::hexagon;
        default:
            return Hex12RegularPolygon::dodecagon;
    }
}

PrototileId id_of(std::size_t p_sides) {
    return PrototileId(static_cast<std::uint64_t>(p_sides));
}

std::size_t stride_of(std::size_t p_sides) {
    return 12 / p_sides;
}

// Every distinct compiled variant of one regular polygon.
std::vector<OrientedPrototile> compile_all(std::size_t p_sides) {
    auto compiled = compile_hex12_orientations(
        id_of(p_sides), polygon_of(p_sides), all_twelve());
    return std::move(compiled).value();
}

// Raw componentwise difference. Every module coordinate is far inside the
// lattice, so plain int64 subtraction is exact here.
Point minus(Point p_lhs, Point p_rhs) {
    return raw(p_lhs.x.raw() - p_rhs.x.raw(), p_lhs.y.raw() - p_rhs.y.raw());
}

// The displacement of edge i: v[i] -> v[i + 1].
Point edge_displacement(const Polygon &p_polygon, std::size_t p_edge) {
    const Polygon::Vertices &v = p_polygon.vertices();
    return minus(v[(p_edge + 1) % v.size()], v[p_edge]);
}

// The module step a displacement equals, or 12 when it is not a unit direction.
std::size_t direction_step_of(Point p_displacement) {
    for (std::size_t k = 0; k < 12; ++k) {
        if (p_displacement == expected_direction(k)) {
            return k;
        }
    }
    return 12;
}

// The edge carrying an exact directed displacement, or an out-of-range index so
// a missing edge fails loudly rather than silently selecting another.
EdgeIndex find_edge_with_displacement(const Polygon &p_polygon, Point p_displacement) {
    for (std::size_t i = 0; i < p_polygon.vertices().size(); ++i) {
        if (edge_displacement(p_polygon, i) == p_displacement) {
            return EdgeIndex(i);
        }
    }
    return EdgeIndex(p_polygon.vertices().size());
}

bool has_directed_edge(const Polygon &p_polygon, Point p_from, Point p_to) {
    const Polygon::Vertices &v = p_polygon.vertices();
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (v[i] == p_from && v[(i + 1) % v.size()] == p_to) {
            return true;
        }
    }
    return false;
}

bool has_vertex(const Polygon &p_polygon, Point p_point) {
    for (const Point &v : p_polygon.vertices()) {
        if (v == p_point) {
            return true;
        }
    }
    return false;
}

// The representative angles of a compiled sequence, as twelfth steps.
std::vector<std::size_t> representative_steps(const std::vector<OrientedPrototile> &p_compiled) {
    std::vector<std::size_t> steps;
    for (const OrientedPrototile &variant : p_compiled) {
        for (std::size_t k = 0; k < 12; ++k) {
            if (variant.orientation() == twelfth(static_cast<std::uint32_t>(k))) {
                steps.push_back(k);
            }
        }
    }
    return steps;
}

// The equivalent-angle labels of one group, as twelfth steps.
std::vector<std::size_t> label_steps(const OrientedPrototile &p_variant) {
    std::vector<std::size_t> steps;
    for (const Orientation label : p_variant.equivalent_orientations()) {
        for (std::size_t k = 0; k < 12; ++k) {
            if (label == twelfth(static_cast<std::uint32_t>(k))) {
                steps.push_back(k);
            }
        }
    }
    return steps;
}

} // namespace

// ---------------------------------------------------------------------------
// the direction module
// ---------------------------------------------------------------------------

TEST_CASE("the module constant is the unique nearest projection of sqrt(3)/2") {
    CHECK(EXPECTED_Q == (std::int64_t { 1 } << 48));
    CHECK(EXPECTED_Q == Coordinate::SCALE);
    CHECK(EXPECTED_H * 2 == EXPECTED_Q);

    // r is nearest to sqrt(3) * q / 2 exactly when
    //     (2r - 1)^2 < 3q^2 < (2r + 1)^2,
    // which brackets 2r within a half unit of the irrational value. Every
    // product is about 2.4e29 and stays exact in signed 128-bit.
    const __int128 q = EXPECTED_Q;
    const __int128 three_q_squared = 3 * q * q;
    const __int128 lower =
        static_cast<__int128>(2 * EXPECTED_R - 1) * static_cast<__int128>(2 * EXPECTED_R - 1);
    const __int128 upper =
        static_cast<__int128>(2 * EXPECTED_R + 1) * static_cast<__int128>(2 * EXPECTED_R + 1);

    CHECK(lower < three_q_squared);
    CHECK(three_q_squared < upper);

    // Uniqueness, spelled out: the same two products disqualify both neighbours.
    // For r - 1 the upper bound is (2r - 1)^2, which no longer exceeds 3q^2; for
    // r + 1 the lower bound is (2r + 1)^2, which no longer falls below it.
    CHECK(!(three_q_squared < lower));
    CHECK(!(upper < three_q_squared));

    // The projection is genuinely irrational underneath: 3q^2 is not a perfect
    // square, so no integer is exactly sqrt(3) * q / 2.
    CHECK(three_q_squared != static_cast<__int128>(2 * EXPECTED_R) * static_cast<__int128>(2 * EXPECTED_R));
}

TEST_CASE("opposite module directions are exact negatives") {
    for (std::size_t k = 0; k < 6; ++k) {
        const Point forward = expected_direction(k);
        const Point backward = expected_direction(k + 6);
        CHECK(backward.x.raw() == -forward.x.raw());
        CHECK(backward.y.raw() == -forward.y.raw());
    }

    // Stated once more as the identity the joiner depends on, for all twelve.
    for (std::size_t k = 0; k < 12; ++k) {
        CHECK(minus(expected_direction(k), raw(0, 0))
            == minus(raw(0, 0), expected_direction(k + 6)));
    }
}

TEST_CASE("compiled geometry uses exactly the twelve authored direction vectors") {
    // The dodecagon's twelve edges are the whole module, so its compiled
    // boundary pins down every direction vector at once.
    const std::vector<OrientedPrototile> variants = compile_all(12);
    CHECK(variants.size() == 1);

    const Polygon &polygon = variants.front().canonical_polygon();
    CHECK(polygon.vertices().size() == 12);

    // Each authored direction appears exactly once.
    for (std::size_t k = 0; k < 12; ++k) {
        int occurrences = 0;
        for (std::size_t i = 0; i < 12; ++i) {
            if (edge_displacement(polygon, i) == expected_direction(k)) {
                ++occurrences;
            }
        }
        CHECK(occurrences == 1);
    }

    // And they appear in counterclockwise cyclic order.
    const std::size_t first = direction_step_of(edge_displacement(polygon, 0));
    CHECK(first < 12);
    for (std::size_t i = 0; i < 12; ++i) {
        CHECK(edge_displacement(polygon, i) == expected_direction(first + i));
    }
}

// ---------------------------------------------------------------------------
// boundary construction
// ---------------------------------------------------------------------------

TEST_CASE("every polygon and phase closes exactly on its authored direction cycle") {
    for (const std::size_t sides : { std::size_t { 3 }, std::size_t { 4 },
             std::size_t { 6 }, std::size_t { 12 } }) {
        const std::size_t stride = stride_of(sides);

        for (std::uint32_t phase = 0; phase < 12; ++phase) {
            auto compiled = compile_hex12_orientations(
                id_of(sides), polygon_of(sides), { twelfth(phase) });
            CHECK(compiled.has_value());
            if (!compiled) {
                continue;
            }
            CHECK(compiled.value().size() == 1);

            const Polygon &polygon = compiled.value().front().canonical_polygon();

            // Exact side count.
            CHECK(polygon.vertices().size() == sides);

            // Exact closure: the edge displacements sum to the zero vector, so
            // the implicit closing endpoint is vertex zero and nothing was
            // silently discarded.
            std::int64_t sum_x = 0;
            std::int64_t sum_y = 0;
            for (std::size_t i = 0; i < sides; ++i) {
                const Point step = edge_displacement(polygon, i);
                sum_x += step.x.raw();
                sum_y += step.y.raw();
            }
            CHECK(sum_x == 0);
            CHECK(sum_y == 0);

            // Every edge is a unit module direction, and the whole boundary
            // walks the phase/stride cycle, allowing for canonical cyclic
            // rotation.
            const std::size_t first = direction_step_of(edge_displacement(polygon, 0));
            CHECK(first < 12);
            CHECK(first % stride == phase % stride);
            for (std::size_t i = 0; i < sides; ++i) {
                CHECK(edge_displacement(polygon, i)
                    == expected_direction(first + i * stride));
            }

            // Certified triangulation and canonical local origin.
            CHECK(polygon.triangulation().size() == sides - 2);
            CHECK(polygon.vertices().front() == raw(0, 0));

            // The requested identity survives into the compiled value, and so
            // does the reference prototile's own checked geometry.
            const OrientedPrototile &variant = compiled.value().front();
            CHECK(variant.prototile().id() == id_of(sides));
            CHECK(variant.prototile().polygon().vertices().size() == sides);
        }
    }
}

TEST_CASE("each triangulation covers its polygon with nondegenerate triangles") {
    for (const std::size_t sides : { std::size_t { 3 }, std::size_t { 4 },
             std::size_t { 6 }, std::size_t { 12 } }) {
        for (const OrientedPrototile &variant : compile_all(sides)) {
            const Polygon &polygon = variant.canonical_polygon();
            CHECK(polygon.triangulation().size() == sides - 2);
            for (const Triangle &triangle : polygon.triangulation()) {
                // Nondegenerate, and drawn from the polygon's own vertices.
                CHECK(orientation(
                          triangle.vertices[0], triangle.vertices[1], triangle.vertices[2])
                    == Turn::counterclockwise);
                for (const Point &corner : triangle.vertices) {
                    CHECK(has_vertex(polygon, corner));
                }
            }
        }
    }
}

TEST_CASE("reference orientation geometry is the stored reference prototile boundary") {
    for (const std::size_t sides : { std::size_t { 3 }, std::size_t { 4 },
             std::size_t { 6 }, std::size_t { 12 } }) {
        auto compiled = compile_hex12_orientations(
            id_of(sides), polygon_of(sides), { Orientation::reference() });
        CHECK(compiled.has_value());
        if (!compiled) {
            continue;
        }

        const OrientedPrototile &variant = compiled.value().front();
        CHECK(variant.orientation() == Orientation::reference());
        CHECK(same_boundary(variant.canonical_polygon(), variant.prototile().polygon()));

        // 0/12 is the same canonical angle as 0/1 and reaches the same geometry.
        auto spelled = compile_hex12_orientations(
            id_of(sides), polygon_of(sides), { twelfth(0) });
        CHECK(spelled.has_value());
        if (spelled) {
            CHECK(spelled.value().front().orientation() == Orientation::reference());
            CHECK(same_boundary(
                spelled.value().front().canonical_polygon(), variant.canonical_polygon()));
        }
    }
}

// ---------------------------------------------------------------------------
// requests and errors
// ---------------------------------------------------------------------------

TEST_CASE("hex-12 compilation rejects an empty orientation set") {
    auto compiled = compile_hex12_orientations(
        PrototileId(1), Hex12RegularPolygon::hexagon, {});
    CHECK(compiled.has_value() == false);
    CHECK(compiled.error().code == Hex12CompilationErrorCode::empty_orientation_set);
    CHECK(compiled.error().orientation.has_value() == false);
    CHECK(compiled.error().polygon_error.has_value() == false);
    CHECK(compiled.error().prototile_error.has_value() == false);
}

TEST_CASE("hex-12 compilation rejects a polygon outside the enumeration") {
    for (const int invalid : { 4, 5, 200 }) {
        auto compiled = compile_hex12_orientations(
            PrototileId(1),
            static_cast<Hex12RegularPolygon>(invalid),
            { Orientation::reference() });
        CHECK(compiled.has_value() == false);
        CHECK(compiled.error().code == Hex12CompilationErrorCode::unsupported_polygon);
        CHECK(compiled.error().orientation.has_value() == false);
        CHECK(compiled.error().polygon_error.has_value() == false);
        CHECK(compiled.error().prototile_error.has_value() == false);
    }

    // Precedence: an empty set answers before the polygon is inspected...
    auto empty_first = compile_hex12_orientations(
        PrototileId(1), static_cast<Hex12RegularPolygon>(200), {});
    CHECK(empty_first.has_value() == false);
    CHECK(empty_first.error().code == Hex12CompilationErrorCode::empty_orientation_set);

    // ...and the polygon answers before any orientation is validated.
    auto polygon_first = compile_hex12_orientations(
        PrototileId(1),
        static_cast<Hex12RegularPolygon>(200),
        { Orientation::make(1, 5).value() });
    CHECK(polygon_first.has_value() == false);
    CHECK(polygon_first.error().code == Hex12CompilationErrorCode::unsupported_polygon);
}

TEST_CASE("hex-12 compilation accepts every equivalent twelfth-turn spelling") {
    // Each canonical angle on the left is the twelfth turn on the right.
    const std::pair<Orientation, std::size_t> spellings[] = {
        { Orientation::reference(), 0 },
        { Orientation::make(1, 12).value(), 1 },
        { Orientation::make(1, 6).value(), 2 },
        { Orientation::quarter(), 3 },
        { Orientation::make(1, 3).value(), 4 },
        { Orientation::make(5, 12).value(), 5 },
        { Orientation::half(), 6 },
        { Orientation::make(2, 3).value(), 8 },
        { Orientation::three_quarter(), 9 },
        { Orientation::make(5, 6).value(), 10 },
    };

    for (const auto &spelling : spellings) {
        CHECK(spelling.first == twelfth(static_cast<std::uint32_t>(spelling.second)));

        auto compiled = compile_hex12_orientations(
            id_of(12), Hex12RegularPolygon::dodecagon, { spelling.first });
        CHECK(compiled.has_value());
        if (!compiled) {
            continue;
        }
        CHECK(compiled.value().size() == 1);
        // The stored angle stays canonical, not re-spelled as k/12.
        CHECK(compiled.value().front().orientation() == spelling.first);
    }
}

TEST_CASE("hex-12 compilation rejects the least unsupported angle") {
    // A fifth of a turn is not a twelfth of one.
    auto compiled = compile_hex12_orientations(
        id_of(6), Hex12RegularPolygon::hexagon,
        { twelfth(3), Orientation::make(1, 5).value(), twelfth(1) });
    CHECK(compiled.has_value() == false);
    CHECK(compiled.error().code == Hex12CompilationErrorCode::unsupported_orientation);
    CHECK(compiled.error().orientation.has_value());
    CHECK(compiled.error().orientation.value() == Orientation::make(1, 5).value());
    CHECK(compiled.error().polygon_error.has_value() == false);
    CHECK(compiled.error().prototile_error.has_value() == false);

    // Least, not first-seen: 1/8 sorts below 1/5, so it is the reported angle.
    auto two_bad = compile_hex12_orientations(
        id_of(6), Hex12RegularPolygon::hexagon,
        { Orientation::make(1, 5).value(), Orientation::make(1, 8).value() });
    CHECK(two_bad.has_value() == false);
    CHECK(two_bad.error().code == Hex12CompilationErrorCode::unsupported_orientation);
    CHECK(two_bad.error().orientation.value() == Orientation::make(1, 8).value());

    for (const Orientation bad : { Orientation::make(1, 5).value(),
             Orientation::make(1, 7).value(), Orientation::make(1, 8).value(),
             Orientation::make(1, 24).value(), Orientation::make(7, 24).value() }) {
        auto rejected = compile_hex12_orientations(
            id_of(3), Hex12RegularPolygon::triangle, { Orientation::reference(), bad });
        CHECK(rejected.has_value() == false);
        CHECK(rejected.error().code == Hex12CompilationErrorCode::unsupported_orientation);
        CHECK(rejected.error().orientation.value() == bad);
    }
}

TEST_CASE("duplicate and equivalent hex-12 spellings collapse before compilation") {
    // One angle written four ways, against a polygon with no rotational
    // symmetry at this angle, so a surviving duplicate could not hide.
    auto compiled = compile_hex12_orientations(
        id_of(3), Hex12RegularPolygon::triangle,
        { twelfth(1), Orientation::make(2, 24).value(),
            Orientation::make(5, 60).value(), twelfth(1) });
    CHECK(compiled.has_value());
    if (!compiled) {
        return;
    }
    CHECK(compiled.value().size() == 1);
    CHECK(compiled.value().front().orientation() == twelfth(1));
    CHECK(compiled.value().front().equivalent_orientations().size() == 1);

    // A sixth turn and 2/12 are the same request.
    auto sixth = compile_hex12_orientations(
        id_of(3), Hex12RegularPolygon::triangle,
        { Orientation::make(1, 6).value(), twelfth(2) });
    CHECK(sixth.has_value());
    if (sixth) {
        CHECK(sixth.value().size() == 1);
        CHECK(sixth.value().front().equivalent_orientations().size() == 1);
    }
}

TEST_CASE("hex-12 output order is independent of input order") {
    const std::vector<Orientation> sorted_request = all_twelve();
    std::vector<Orientation> scrambled_request = {
        twelfth(7), twelfth(2), twelfth(11), twelfth(0), twelfth(5), twelfth(9),
        twelfth(3), twelfth(1), twelfth(10), twelfth(4), twelfth(8), twelfth(6),
        // Duplicate and equivalent spellings mixed back in.
        Orientation::quarter(), Orientation::make(1, 3).value(), twelfth(7),
    };

    for (const std::size_t sides : { std::size_t { 3 }, std::size_t { 4 },
             std::size_t { 6 }, std::size_t { 12 } }) {
        auto sorted = compile_hex12_orientations(
            id_of(sides), polygon_of(sides), sorted_request);
        auto scrambled = compile_hex12_orientations(
            id_of(sides), polygon_of(sides), scrambled_request);
        CHECK(sorted.has_value());
        CHECK(scrambled.has_value());
        if (!sorted || !scrambled) {
            continue;
        }
        CHECK(sorted.value().size() == scrambled.value().size());
        for (std::size_t i = 0; i < sorted.value().size(); ++i) {
            CHECK(sorted.value()[i].orientation() == scrambled.value()[i].orientation());
            CHECK(sorted.value()[i].equivalent_orientations()
                == scrambled.value()[i].equivalent_orientations());
            CHECK(same_boundary(
                sorted.value()[i].canonical_polygon(),
                scrambled.value()[i].canonical_polygon()));
        }
    }
}

// ---------------------------------------------------------------------------
// symmetry grouping
// ---------------------------------------------------------------------------

TEST_CASE("the all-twelve symmetry table is exact for every admitted polygon") {
    struct Expectation {
        std::size_t sides;
        std::vector<std::vector<std::size_t>> groups;
    };

    const std::vector<Expectation> expectations = {
        { 3, { { 0, 4, 8 }, { 1, 5, 9 }, { 2, 6, 10 }, { 3, 7, 11 } } },
        { 4, { { 0, 3, 6, 9 }, { 1, 4, 7, 10 }, { 2, 5, 8, 11 } } },
        { 6, { { 0, 2, 4, 6, 8, 10 }, { 1, 3, 5, 7, 9, 11 } } },
        { 12, { { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } } },
    };

    for (const Expectation &expectation : expectations) {
        const std::vector<OrientedPrototile> variants = compile_all(expectation.sides);
        CHECK(variants.size() == expectation.groups.size());
        if (variants.size() != expectation.groups.size()) {
            continue;
        }

        for (std::size_t g = 0; g < variants.size(); ++g) {
            const std::vector<std::size_t> &expected = expectation.groups[g];

            // The representative is the least angle in the group, and the groups
            // appear in representative angular order.
            CHECK(variants[g].orientation()
                == twelfth(static_cast<std::uint32_t>(expected.front())));
            CHECK(label_steps(variants[g]) == expected);
            CHECK(variants[g].equivalent_orientations().size() == expected.size());

            // Every label really does compile to this exact boundary...
            for (const std::size_t step : expected) {
                auto alone = compile_hex12_orientations(
                    id_of(expectation.sides), polygon_of(expectation.sides),
                    { twelfth(static_cast<std::uint32_t>(step)) });
                CHECK(alone.has_value());
                if (alone) {
                    CHECK(same_boundary(
                        alone.value().front().canonical_polygon(),
                        variants[g].canonical_polygon()));
                }
            }

            // ...and no two groups share one.
            for (std::size_t other = 0; other < variants.size(); ++other) {
                if (other == g) {
                    continue;
                }
                CHECK(!same_boundary(
                    variants[g].canonical_polygon(),
                    variants[other].canonical_polygon()));
            }
        }
    }
}

TEST_CASE("a partial hex-12 request retains only its admitted equivalence labels") {
    // Triangle steps 0 and 4 are equivalent; step 1 belongs to another class.
    auto compiled = compile_hex12_orientations(
        id_of(3), Hex12RegularPolygon::triangle, { twelfth(4), twelfth(1), twelfth(0) });
    CHECK(compiled.has_value());
    if (!compiled) {
        return;
    }
    CHECK(compiled.value().size() == 2);
    CHECK((representative_steps(compiled.value()) == std::vector<std::size_t> { 0, 1 }));
    CHECK((label_steps(compiled.value()[0]) == std::vector<std::size_t> { 0, 4 }));
    CHECK((label_steps(compiled.value()[1]) == std::vector<std::size_t> { 1 }));

    // Asking only for step 8 makes it its own representative, with the same
    // boundary the {0,4,8} class carries.
    auto lone = compile_hex12_orientations(
        id_of(3), Hex12RegularPolygon::triangle, { twelfth(8) });
    CHECK(lone.has_value());
    if (lone) {
        CHECK(lone.value().size() == 1);
        CHECK(lone.value().front().orientation() == twelfth(8));
        CHECK(lone.value().front().equivalent_orientations().size() == 1);
        CHECK(same_boundary(
            lone.value().front().canonical_polygon(),
            compiled.value()[0].canonical_polygon()));
    }

    // Hexagon steps 3 and 9 are equivalent; the representative is the lesser.
    auto hexagon = compile_hex12_orientations(
        id_of(6), Hex12RegularPolygon::hexagon, { twelfth(9), twelfth(3) });
    CHECK(hexagon.has_value());
    if (hexagon) {
        CHECK(hexagon.value().size() == 1);
        CHECK(hexagon.value().front().orientation() == twelfth(3));
        CHECK((label_steps(hexagon.value().front()) == std::vector<std::size_t> { 3, 9 }));
    }
}

// ---------------------------------------------------------------------------
// the lattice compiler is unchanged
// ---------------------------------------------------------------------------

TEST_CASE("the lattice compiler still rejects a twelfth turn") {
    auto square = Polygon::make({ raw(0, 0), raw(4, 0), raw(4, 4), raw(0, 4) });
    auto prototile = Prototile::make(PrototileId(77), std::move(square).value());
    const Prototile &tile = prototile.value();

    for (const std::uint32_t step : { 1u, 2u, 5u, 7u, 11u }) {
        auto rejected = compile_lattice_orientations(tile, { twelfth(step) });
        CHECK(rejected.has_value() == false);
        CHECK(rejected.error().code == LatticeOrientationErrorCode::unsupported_orientation);
        CHECK(rejected.error().orientation.value() == twelfth(step));
    }

    // The twelfth turns that are also quarter turns remain accepted, and the
    // four-quarter-turn result is untouched.
    auto accepted = compile_lattice_orientations(tile, {
        twelfth(0), twelfth(3), twelfth(6), twelfth(9)
    });
    CHECK(accepted.has_value());
    if (accepted) {
        CHECK(accepted.value().size() == 1);
        CHECK(accepted.value().front().equivalent_orientations().size() == 4);
    }
}

// ---------------------------------------------------------------------------
// exhaustive direction join
// ---------------------------------------------------------------------------

TEST_CASE("every module direction admits an exact full-edge join") {
    const std::vector<OrientedPrototile> variants = compile_all(12);
    CHECK(variants.size() == 1);
    const OrientedPrototile &tile = variants.front();

    for (std::size_t k = 0; k < 12; ++k) {
        Arrangement arrangement;
        auto anchor_placement = Placement::make(tile, raw(0, 0));
        CHECK(anchor_placement.has_value());
        if (!anchor_placement) {
            continue;
        }
        auto anchor = arrangement.try_insert(std::move(anchor_placement).value());
        CHECK(anchor.has_value());
        if (!anchor) {
            continue;
        }

        // The anchor edge with exactly this directed displacement, and the
        // candidate edge with exactly its opposite.
        const Polygon &anchor_footprint =
            arrangement.entries().front().placement.footprint();
        const EdgeIndex anchor_edge =
            find_edge_with_displacement(anchor_footprint, expected_direction(k));
        const EdgeIndex candidate_edge = find_edge_with_displacement(
            tile.canonical_polygon(), expected_direction(k + 6));
        CHECK(anchor_edge.value() < 12);
        CHECK(candidate_edge.value() < 12);

        // Copy the shared endpoints out before the join: insertion may move the
        // entry storage the footprint reference points into.
        const Point shared_from = anchor_footprint.vertices()[anchor_edge.value()];
        const Point shared_to = anchor_footprint.vertices()[(anchor_edge.value() + 1) % 12];

        auto joined = arrangement.try_join_full_edges(
            anchor.value(), anchor_edge, tile, candidate_edge);
        CHECK(joined.has_value());
        if (!joined) {
            continue;
        }
        CHECK(arrangement.entries().size() == 2);

        // Both shared endpoints are the identical raw lattice values in both
        // footprints. No epsilon, no reconstruction.
        const Polygon &placed_anchor = arrangement.entries().front().placement.footprint();
        const Polygon &placed_candidate = arrangement.entries().back().placement.footprint();
        CHECK(has_vertex(placed_candidate, shared_from));
        CHECK(has_vertex(placed_candidate, shared_to));
        CHECK(has_vertex(placed_anchor, shared_from));
        CHECK(has_vertex(placed_anchor, shared_to));
        CHECK(has_directed_edge(placed_anchor, shared_from, shared_to));
        CHECK(has_directed_edge(placed_candidate, shared_to, shared_from));

        // The two-placement arrangement is still interior-disjoint.
        CHECK(!interiors_overlap(placed_anchor, placed_candidate));
    }
}

// ---------------------------------------------------------------------------
// uniform vertex-star acceptance
// ---------------------------------------------------------------------------

namespace {

// A regular n-gon's interior angle occupies this many thirty-degree wedges.
std::size_t interior_steps(std::size_t p_sides) {
    return 6 - 12 / p_sides;
}

// Build one complete vertex star around exact world point (0, 0) and prove it
// closes. Consumes only public production results and existing core operations:
// no floating point, no epsilon, no screen coordinates, no new predicate, and no
// hand-authored compatibility table.
void check_vertex_star(const std::vector<std::size_t> &p_signature) {
    // The signature is a complete turn: twelve thirty-degree wedges.
    std::size_t total_steps = 0;
    for (const std::size_t sides : p_signature) {
        total_steps += interior_steps(sides);
    }
    CHECK(total_steps == 12);

    Arrangement arrangement;
    std::vector<std::size_t> start_rays;
    std::vector<std::size_t> end_rays;

    std::size_t ray = 0;
    for (const std::size_t sides : p_signature) {
        const std::size_t next_ray = ray + interior_steps(sides);
        start_rays.push_back(ray);
        end_rays.push_back(next_ray);

        const std::vector<OrientedPrototile> variants = compile_all(sides);

        // Among this polygon's distinct compiled variants, find the unique
        // vertex whose two neighbour displacements are the unit directions of
        // the current and next dividing rays, in either neighbour order.
        std::size_t matches = 0;
        std::size_t chosen_variant = 0;
        std::size_t chosen_vertex = 0;
        for (std::size_t v = 0; v < variants.size(); ++v) {
            const Polygon::Vertices &vertices = variants[v].canonical_polygon().vertices();
            const std::size_t n = vertices.size();
            for (std::size_t i = 0; i < n; ++i) {
                const Point back = minus(vertices[(i + n - 1) % n], vertices[i]);
                const Point forward = minus(vertices[(i + 1) % n], vertices[i]);
                const Point current = expected_direction(ray);
                const Point following = expected_direction(next_ray);
                if ((back == current && forward == following)
                    || (back == following && forward == current)) {
                    ++matches;
                    chosen_variant = v;
                    chosen_vertex = i;
                }
            }
        }
        CHECK(matches == 1);
        if (matches != 1) {
            return;
        }

        // Translate that exact vertex to the origin through checked core
        // arithmetic.
        const Point vertex =
            variants[chosen_variant].canonical_polygon().vertices()[chosen_vertex];
        auto translation = checked_subtract(raw(0, 0), vertex);
        CHECK(translation.has_value());
        if (!translation) {
            return;
        }

        auto placement = Placement::make(variants[chosen_variant], translation.value());
        CHECK(placement.has_value());
        if (!placement) {
            return;
        }
        CHECK(has_vertex(placement.value().footprint(), raw(0, 0)));

        auto inserted = arrangement.try_insert(std::move(placement).value());
        CHECK(inserted.has_value());
        if (!inserted) {
            return;
        }

        ray = next_ray;
    }

    // The star closes: the final dividing ray is step twelve, which is step zero.
    CHECK(ray == 12);
    CHECK(expected_direction(ray) == expected_direction(0));
    CHECK(arrangement.entries().size() == p_signature.size());

    // No pair of stored footprints has positive-area interior overlap. The
    // arrangement's own pairwise scan already proved this on insertion; assert it
    // again directly over the stored entries.
    const std::vector<Entry> &entries = arrangement.entries();
    for (std::size_t i = 0; i < entries.size(); ++i) {
        for (std::size_t j = i + 1; j < entries.size(); ++j) {
            CHECK(!interiors_overlap(
                entries[i].placement.footprint(), entries[j].placement.footprint()));
        }
    }

    // Every dividing unit segment from (0, 0) to its module direction is a
    // complete boundary edge of both adjacent footprints, in opposite senses.
    // The last placement's closing ray is the first placement's opening ray, so
    // the ring of shared edges closes exactly.
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const std::size_t previous = (i + entries.size() - 1) % entries.size();
        const Point divider = expected_direction(start_rays[i]);

        CHECK(end_rays[previous] % 12 == start_rays[i] % 12);
        CHECK(has_directed_edge(entries[i].placement.footprint(), raw(0, 0), divider));
        CHECK(has_directed_edge(entries[previous].placement.footprint(), divider, raw(0, 0)));
    }
}

} // namespace

TEST_CASE("the ten uniform vertex stars close exactly around one point") {
    const std::vector<std::vector<std::size_t>> signatures = {
        { 3, 3, 3, 3, 3, 3 },
        { 4, 4, 4, 4 },
        { 6, 6, 6 },
        { 3, 12, 12 },
        { 4, 6, 12 },
        { 3, 6, 3, 6 },
        { 3, 4, 6, 4 },
        { 3, 3, 4, 3, 4 },
        { 3, 3, 3, 4, 4 },
        { 3, 3, 3, 3, 6 },
    };
    CHECK(signatures.size() == 10);

    for (const std::vector<std::size_t> &signature : signatures) {
        check_vertex_star(signature);
    }
}

// ---------------------------------------------------------------------------
// extended patch growth
// ---------------------------------------------------------------------------

namespace {

const Placement *placement_with_id(const Arrangement &p_arrangement, PlacementId p_id) {
    for (const Entry &entry : p_arrangement.entries()) {
        if (entry.id == p_id) {
            return &entry.placement;
        }
    }
    return nullptr;
}

// One grown cell: which regular polygon filled it, its arrangement identity, and
// how many joins from the seed it sits.
struct GrownCell final {
    std::size_t sides;
    PlacementId id;
    std::size_t depth;
};

} // namespace

TEST_CASE("an extended 3.6.3.6 patch grows by chained joins with no drift") {
    // In 3.6.3.6 every hexagon edge meets a triangle and every triangle edge
    // meets a hexagon, so the neighbour type is forced and the patch needs no
    // authored layout. Growth is breadth-first from one seed hexagon; every tile
    // after the seed is positioned only by try_join_full_edges against a tile
    // that was itself joined, so quantization error would have every opportunity
    // to accumulate along the chain.
    const std::vector<OrientedPrototile> hexagons = compile_all(6);
    const std::vector<OrientedPrototile> triangles = compile_all(3);
    CHECK(hexagons.size() == 2);
    CHECK(triangles.size() == 4);

    Arrangement arrangement;
    std::vector<GrownCell> cells;

    auto seed = Placement::make(hexagons.front(), raw(0, 0));
    CHECK(seed.has_value());
    if (!seed) {
        return;
    }
    auto seed_id = arrangement.try_insert(std::move(seed).value());
    CHECK(seed_id.has_value());
    if (!seed_id) {
        return;
    }
    cells.push_back(GrownCell { 6, seed_id.value(), 0 });

    const std::size_t max_depth = 3;
    std::size_t joins_accepted = 0;
    std::size_t joins_rejected_as_filled = 0;
    std::size_t rejected_cells_that_were_bit_exact = 0;
    std::size_t joins_rejected_otherwise = 0;
    std::size_t ambiguous_mates = 0;

    for (std::size_t i = 0; i < cells.size(); ++i) {
        if (cells[i].depth >= max_depth) {
            continue;
        }
        const GrownCell current = cells[i];
        const std::size_t neighbour_sides = current.sides == 6 ? 3 : 6;
        const std::vector<OrientedPrototile> &catalog =
            neighbour_sides == 6 ? hexagons : triangles;

        for (std::size_t e = 0; e < current.sides; ++e) {
            // Re-locate the anchor every time: a successful insertion may have
            // reallocated the entry storage since the last iteration.
            const Placement *anchor = placement_with_id(arrangement, current.id);
            CHECK(anchor != nullptr);
            if (anchor == nullptr) {
                return;
            }

            // The candidate edge must carry the exact opposite displacement.
            const Point wanted =
                minus(raw(0, 0), edge_displacement(anchor->footprint(), e));

            std::size_t mate_variant = 0;
            std::size_t mate_edge = 0;
            std::size_t mates = 0;
            for (std::size_t v = 0; v < catalog.size(); ++v) {
                const Polygon &polygon = catalog[v].canonical_polygon();
                for (std::size_t ce = 0; ce < polygon.vertices().size(); ++ce) {
                    if (edge_displacement(polygon, ce) == wanted) {
                        mate_variant = v;
                        mate_edge = ce;
                        ++mates;
                    }
                }
            }
            // Across all variants and edges of the neighbour type, exactly one
            // choice mates this edge. The tiling is forced, not chosen.
            CHECK(mates == 1);
            if (mates != 1) {
                ++ambiguous_mates;
                continue;
            }

            // The placement this join would produce, derived by pure alignment
            // so it is available even when insertion refuses it.
            auto aligned = align_full_edge(
                anchor->footprint(), EdgeIndex(e),
                catalog[mate_variant], EdgeIndex(mate_edge));
            CHECK(aligned.has_value());
            if (!aligned) {
                continue;
            }
            const Polygon would_place = aligned.value().footprint();

            auto joined = arrangement.try_join_full_edges(
                current.id, EdgeIndex(e), catalog[mate_variant], EdgeIndex(mate_edge));

            if (joined.has_value()) {
                ++joins_accepted;
                cells.push_back(
                    GrownCell { neighbour_sides, joined.value(), current.depth + 1 });
                continue;
            }

            // The only legitimate refusal is a cell some earlier path already
            // filled. This is the drift discriminator: the cell was reached by a
            // different chain of joins, so its geometry must come back bit for
            // bit identical, not merely close enough to overlap.
            CHECK(joined.error().code == JoinErrorCode::interior_overlap);
            if (joined.error().code != JoinErrorCode::interior_overlap) {
                ++joins_rejected_otherwise;
                continue;
            }
            ++joins_rejected_as_filled;

            bool bit_exact = false;
            for (const Entry &entry : arrangement.entries()) {
                if (same_boundary(entry.placement.footprint(), would_place)) {
                    bit_exact = true;
                }
            }
            CHECK(bit_exact);
            if (bit_exact) {
                ++rejected_cells_that_were_bit_exact;
            }
        }
    }

    CHECK(ambiguous_mates == 0);
    CHECK(joins_rejected_otherwise == 0);

    // Thirty-one tiles, every one after the seed placed by a chained join.
    CHECK(arrangement.entries().size() == 31);
    CHECK(joins_accepted == 30);

    std::size_t grown_hexagons = 0;
    std::size_t grown_triangles = 0;
    for (const GrownCell &cell : cells) {
        if (cell.sides == 6) {
            ++grown_hexagons;
        } else {
            ++grown_triangles;
        }
    }
    CHECK(grown_hexagons == 7);
    CHECK(grown_triangles == 24);

    // Thirty independent cycles in the join graph, every one closing exactly.
    CHECK(joins_rejected_as_filled == 30);
    CHECK(rejected_cells_that_were_bit_exact == 30);

    // Exact contact census over the finished patch.
    const std::vector<Entry> &entries = arrangement.entries();
    std::size_t complete_edge_pairs = 0;
    std::size_t single_vertex_pairs = 0;
    for (std::size_t a = 0; a < entries.size(); ++a) {
        for (std::size_t b = a + 1; b < entries.size(); ++b) {
            const Polygon &lhs = entries[a].placement.footprint();
            const Polygon &rhs = entries[b].placement.footprint();
            CHECK(!interiors_overlap(lhs, rhs));

            std::size_t shared = 0;
            for (const Point &p : lhs.vertices()) {
                if (has_vertex(rhs, p)) {
                    ++shared;
                }
            }
            if (shared == 2) {
                ++complete_edge_pairs;
            } else if (shared == 1) {
                ++single_vertex_pairs;
            }
            // Two convex tiles can never share three or more corners.
            CHECK(shared <= 2);
        }
    }
    CHECK(complete_edge_pairs == 42);
    CHECK(single_vertex_pairs == 42);

    // Vertex census. A 3.6.3.6 vertex is surrounded by exactly four tiles, so an
    // interior vertex of the patch must be shared by four footprints at exactly
    // the same raw coordinates. Any drift at all would split those coincidences
    // apart and collapse the degree-four count to zero.
    std::vector<Point> distinct;
    for (const Entry &entry : entries) {
        for (const Point &p : entry.placement.footprint().vertices()) {
            bool already = false;
            for (const Point &d : distinct) {
                if (d == p) {
                    already = true;
                }
            }
            if (!already) {
                distinct.push_back(p);
            }
        }
    }

    std::size_t degree_one = 0;
    std::size_t degree_two = 0;
    std::size_t degree_three = 0;
    std::size_t degree_four = 0;
    std::size_t total_incidences = 0;
    for (const Point &p : distinct) {
        std::size_t degree = 0;
        for (const Entry &entry : entries) {
            if (has_vertex(entry.placement.footprint(), p)) {
                ++degree;
            }
        }
        total_incidences += degree;
        if (degree == 1) {
            ++degree_one;
        } else if (degree == 2) {
            ++degree_two;
        } else if (degree == 3) {
            ++degree_three;
        } else if (degree == 4) {
            ++degree_four;
        }
        // Never more than the four tiles a 3.6.3.6 vertex admits.
        CHECK(degree <= 4);
    }

    CHECK(distinct.size() == 42);
    CHECK(degree_one == 12);
    CHECK(degree_two == 0);
    CHECK(degree_three == 18);
    CHECK(degree_four == 12);

    // The census accounts for every corner of every tile: 7 hexagons * 6 plus
    // 24 triangles * 3.
    CHECK(total_incidences == 7 * 6 + 24 * 3);
    CHECK(degree_one + degree_two * 2 + degree_three * 3 + degree_four * 4
        == total_incidences);
}

TEST_CASE("the interior-angle wedge table is exact for every admitted polygon") {
    CHECK(interior_steps(3) == 2);
    CHECK(interior_steps(4) == 3);
    CHECK(interior_steps(6) == 4);
    CHECK(interior_steps(12) == 5);

    // Each wedge count really is the polygon's interior angle: at every vertex
    // of every compiled variant, the two neighbour displacements are module
    // directions exactly interior_steps apart.
    for (const std::size_t sides : { std::size_t { 3 }, std::size_t { 4 },
             std::size_t { 6 }, std::size_t { 12 } }) {
        for (const OrientedPrototile &variant : compile_all(sides)) {
            const Polygon::Vertices &vertices = variant.canonical_polygon().vertices();
            const std::size_t n = vertices.size();
            for (std::size_t i = 0; i < n; ++i) {
                const std::size_t back =
                    direction_step_of(minus(vertices[(i + n - 1) % n], vertices[i]));
                const std::size_t forward =
                    direction_step_of(minus(vertices[(i + 1) % n], vertices[i]));
                CHECK(back < 12);
                CHECK(forward < 12);
                if (back < 12 && forward < 12) {
                    CHECK((back + 12 - forward) % 12 == interior_steps(sides));
                }
            }
        }
    }
}
