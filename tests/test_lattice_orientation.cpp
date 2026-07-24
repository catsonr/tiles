#include "TestHarness.h"

#include "core/Orientation.h"
#include "core/OrientedPrototile.h"
#include "core/Prototile.h"
#include "core/geometry/Polygon.h"

#include <cstdint>
#include <utility>
#include <vector>

using namespace tiles;
using tiles_test::raw_pt;

namespace {

Point raw(std::int64_t p_x, std::int64_t p_y) {
    return raw_pt(p_x, p_y);
}

Prototile proto(std::uint64_t p_id, std::vector<Point> p_vertices) {
    auto polygon = Polygon::make(std::move(p_vertices));
    auto p = Prototile::make(PrototileId(p_id), std::move(polygon).value());
    return std::move(p).value();
}

// A scalene triangle carrying a negative source coordinate, so its four quarter
// turns are all geometrically distinct and both coordinate signs are exercised.
Prototile scalene() {
    return proto(1, { raw(0, 0), raw(4, -1), raw(1, 3) });
}

Orientation orient(std::uint32_t p_step, std::uint32_t p_order) {
    return Orientation::make(p_step, p_order).value();
}

// Independently apply a named quarter turn to a prototile's reference vertices
// and translation-normalize, without using the production rotation kernel.
Polygon expected_canonical(const Prototile &p_proto, Orientation p_orientation) {
    std::vector<Point> rotated;
    for (const Point &v : p_proto.polygon().vertices()) {
        const std::int64_t x = v.x.raw();
        const std::int64_t y = v.y.raw();
        std::int64_t nx = x;
        std::int64_t ny = y;
        if (p_orientation == Orientation::quarter()) {
            nx = -y;
            ny = x;
        } else if (p_orientation == Orientation::half()) {
            nx = -x;
            ny = -y;
        } else if (p_orientation == Orientation::three_quarter()) {
            nx = y;
            ny = -x;
        }
        rotated.push_back(raw(nx, ny));
    }
    auto polygon = Polygon::make(std::move(rotated));
    auto normalized = translation_normalize(polygon.value());
    return std::move(normalized).value();
}

} // namespace

TEST_CASE("lattice compilation rejects an empty orientation set") {
    auto compiled = compile_lattice_orientations(scalene(), {});
    CHECK(compiled.has_value() == false);
    CHECK(compiled.error().code == LatticeOrientationErrorCode::empty_orientation_set);
    CHECK(compiled.error().orientation.has_value() == false);
}

TEST_CASE("lattice compilation removes exact duplicate angles") {
    // The same angle written four different ways is one requested orientation.
    auto compiled = compile_lattice_orientations(scalene(), {
        orient(1, 4), orient(2, 8), orient(25, 100), orient(1, 4)
    });
    CHECK(compiled.has_value());
    CHECK(compiled.value().size() == 1);
    const OrientedPrototile &only = compiled.value().front();
    CHECK(only.orientation() == Orientation::quarter());
    CHECK(only.equivalent_orientations().size() == 1);
}

TEST_CASE("lattice compilation rejects non-quarter turns") {
    // 1/8, 1/3, and 1/6 are not lattice orientations.
    for (const Orientation bad : { orient(1, 8), orient(1, 3), orient(1, 6), orient(5, 12) }) {
        auto compiled = compile_lattice_orientations(scalene(), {
            Orientation::reference(), bad
        });
        CHECK(compiled.has_value() == false);
        CHECK(compiled.error().code == LatticeOrientationErrorCode::unsupported_orientation);
        CHECK(compiled.error().orientation.has_value());
        CHECK(compiled.error().orientation.value() == bad);
    }
}

TEST_CASE("each quarter turn of an asymmetric polygon is a distinct valid variant") {
    auto compiled = compile_lattice_orientations(scalene(), {
        Orientation::reference(), Orientation::quarter(),
        Orientation::half(), Orientation::three_quarter()
    });
    CHECK(compiled.has_value());
    CHECK(compiled.value().size() == 4);

    const Orientation expected[4] = {
        Orientation::reference(), Orientation::quarter(),
        Orientation::half(), Orientation::three_quarter()
    };
    for (std::size_t i = 0; i < 4; ++i) {
        const OrientedPrototile &variant = compiled.value()[i];
        // Deterministic angular output order, one admitted angle each.
        CHECK(variant.orientation() == expected[i]);
        CHECK(variant.equivalent_orientations().size() == 1);

        // Direct-from-reference: matches an independent transform of the
        // reference vertices, and carries a valid reconstructed triangulation.
        CHECK(same_boundary(variant.canonical_polygon(), expected_canonical(scalene(), expected[i])));
        const Polygon &polygon = variant.canonical_polygon();
        CHECK(polygon.triangulation().size() == polygon.vertices().size() - 2);

        // Every canonical variant is translation-normalized to minimum x zero.
        CHECK(polygon.vertices().front().x == Coordinate::from_raw(0));
    }
}

TEST_CASE("lattice output order is independent of input order") {
    auto sorted = compile_lattice_orientations(scalene(), {
        Orientation::reference(), Orientation::quarter(),
        Orientation::half(), Orientation::three_quarter()
    });
    auto scrambled = compile_lattice_orientations(scalene(), {
        Orientation::three_quarter(), Orientation::reference(),
        Orientation::half(), Orientation::quarter(), Orientation::half()
    });
    CHECK(sorted.has_value());
    CHECK(scrambled.has_value());
    CHECK(sorted.value().size() == scrambled.value().size());
    for (std::size_t i = 0; i < sorted.value().size(); ++i) {
        CHECK(sorted.value()[i].orientation() == scrambled.value()[i].orientation());
        CHECK(same_boundary(
            sorted.value()[i].canonical_polygon(),
            scrambled.value()[i].canonical_polygon()));
    }
}

TEST_CASE("geometrically identical orientations group with their full admitted angle set") {
    // A 4x4 square is invariant under every quarter turn: one group carrying all
    // four requested angles in angular order.
    const Prototile square = proto(2, { raw(0, 0), raw(4, 0), raw(4, 4), raw(0, 4) });
    auto compiled = compile_lattice_orientations(square, {
        Orientation::reference(), Orientation::quarter(),
        Orientation::half(), Orientation::three_quarter()
    });
    CHECK(compiled.has_value());
    CHECK(compiled.value().size() == 1);

    const OrientedPrototile &group = compiled.value().front();
    CHECK(group.orientation() == Orientation::reference());
    const std::vector<Orientation> &equivalents = group.equivalent_orientations();
    CHECK(equivalents.size() == 4);
    CHECK(equivalents[0] == Orientation::reference());
    CHECK(equivalents[1] == Orientation::quarter());
    CHECK(equivalents[2] == Orientation::half());
    CHECK(equivalents[3] == Orientation::three_quarter());
}

TEST_CASE("transform overflow is reported at every reachable INT64_MIN negation") {
    // A normalized prototile with a vertex at y == INT64_MIN. The only quarter
    // turns that negate a y coordinate are the quarter turn (-y, x) and the half
    // turn (-x, -y); both therefore hit the INT64_MIN negation and report an
    // exact coordinate overflow.
    const Prototile deep = proto(3, { raw(0, 0), raw(1, INT64_MIN), raw(2, 0) });

    auto quarter = compile_lattice_orientations(deep, { Orientation::quarter() });
    CHECK(quarter.has_value() == false);
    CHECK(quarter.error().code == LatticeOrientationErrorCode::coordinate_overflow);
    CHECK(quarter.error().orientation.value() == Orientation::quarter());

    auto half = compile_lattice_orientations(deep, { Orientation::half() });
    CHECK(half.has_value() == false);
    CHECK(half.error().code == LatticeOrientationErrorCode::coordinate_overflow);
    CHECK(half.error().orientation.value() == Orientation::half());

    // A coordinate overflow from negating x is structurally unreachable: every
    // normalized polygon has x >= 0, so no x coordinate is ever INT64_MIN. The
    // three-quarter turn (y, -x) is the only turn that negates x, and on a plain
    // asymmetric prototile it compiles cleanly with no negation overflow.
    auto three_quarter =
        compile_lattice_orientations(scalene(), { Orientation::three_quarter() });
    CHECK(three_quarter.has_value());

    // The reference orientation performs no negation at all.
    auto reference = compile_lattice_orientations(deep, { Orientation::reference() });
    CHECK(reference.has_value());
}

TEST_CASE("normalization overflow is reported after a representable transform") {
    // The reference y-extent exceeds the lattice width: y spans INT64_MAX down to
    // -1. The quarter turn negates y (INT64_MAX -> -INT64_MAX, never INT64_MIN),
    // so the rotation itself is representable and the polygon reconstructs; but
    // re-normalizing the rotated boundary to minimum x zero then overflows.
    const Prototile wide = proto(4, { raw(0, 0), raw(1, INT64_MAX), raw(2, -1) });

    auto compiled = compile_lattice_orientations(wide, { Orientation::quarter() });
    CHECK(compiled.has_value() == false);
    CHECK(compiled.error().code == LatticeOrientationErrorCode::normalization_overflow);
    CHECK(compiled.error().orientation.value() == Orientation::quarter());

    // The reference orientation of the same prototile needs no re-normalization
    // shift and compiles cleanly.
    auto reference = compile_lattice_orientations(wide, { Orientation::reference() });
    CHECK(reference.has_value());
}
