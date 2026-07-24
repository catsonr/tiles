#include "TestHarness.h"

#include "core/Orientation.h"
#include "core/OrientedPrototile.h"
#include "core/Prototile.h"
#include "core/geometry/ExactInteger.h"
#include "core/geometry/Polygon.h"
#include "core/geometry/Predicates.h"

#include <cstdint>
#include <utility>
#include <vector>

using namespace tiles;
using tiles_test::raw_pt;

namespace {

// A point on whole game-unit q16.48 coordinates: one game unit is Coordinate::SCALE
// raw units, so a tetromino cell is a SCALE x SCALE square.
Point unit(std::int64_t p_gx, std::int64_t p_gy) {
    return raw_pt(p_gx * Coordinate::SCALE, p_gy * Coordinate::SCALE);
}

std::vector<Point> unit_ring(const std::vector<std::pair<std::int64_t, std::int64_t>> &p_cells) {
    std::vector<Point> ring;
    ring.reserve(p_cells.size());
    for (const auto &cell : p_cells) {
        ring.push_back(unit(cell.first, cell.second));
    }
    return ring;
}

Prototile proto(std::uint64_t p_id, std::vector<Point> p_vertices) {
    auto polygon = Polygon::make(std::move(p_vertices));
    auto p = Prototile::make(PrototileId(p_id), std::move(polygon).value());
    return std::move(p).value();
}

const Orientation R = Orientation::reference();
const Orientation Q = Orientation::quarter();
const Orientation H = Orientation::half();
const Orientation T = Orientation::three_quarter();

// The seven one-sided tetrominoes as game-unit rings, each corner a direction
// change (no redundant collinear cell-boundary points).
std::vector<Point> tetromino_o() {
    return unit_ring({ { 0, 0 }, { 2, 0 }, { 2, 2 }, { 0, 2 } });
}
std::vector<Point> tetromino_i() {
    return unit_ring({ { 0, 0 }, { 4, 0 }, { 4, 1 }, { 0, 1 } });
}
std::vector<Point> tetromino_t() {
    return unit_ring({
        { 0, 0 }, { 3, 0 }, { 3, 1 }, { 2, 1 }, { 2, 2 }, { 1, 2 }, { 1, 1 }, { 0, 1 } });
}
std::vector<Point> tetromino_s() {
    return unit_ring({
        { 0, 0 }, { 2, 0 }, { 2, 1 }, { 3, 1 }, { 3, 2 }, { 1, 2 }, { 1, 1 }, { 0, 1 } });
}
std::vector<Point> tetromino_z() {
    return unit_ring({
        { 1, 0 }, { 3, 0 }, { 3, 1 }, { 2, 1 }, { 2, 2 }, { 0, 2 }, { 0, 1 }, { 1, 1 } });
}
std::vector<Point> tetromino_j() {
    return unit_ring({ { 0, 0 }, { 2, 0 }, { 2, 3 }, { 1, 3 }, { 1, 1 }, { 0, 1 } });
}
std::vector<Point> tetromino_l() {
    return unit_ring({ { 0, 0 }, { 2, 0 }, { 2, 1 }, { 1, 1 }, { 1, 3 }, { 0, 3 } });
}

// Independently apply a named quarter turn to reference vertices and normalize,
// without touching the production rotation kernel.
Polygon expected_canonical(const Prototile &p_proto, Orientation p_orientation) {
    std::vector<Point> rotated;
    for (const Point &v : p_proto.polygon().vertices()) {
        const std::int64_t x = v.x.raw();
        const std::int64_t y = v.y.raw();
        std::int64_t nx = x;
        std::int64_t ny = y;
        if (p_orientation == Q) {
            nx = -y;
            ny = x;
        } else if (p_orientation == H) {
            nx = -x;
            ny = -y;
        } else if (p_orientation == T) {
            nx = y;
            ny = -x;
        }
        rotated.push_back(raw_pt(nx, ny));
    }
    auto polygon = Polygon::make(std::move(rotated));
    auto normalized = translation_normalize(polygon.value());
    return std::move(normalized).value();
}

// A cyclically shifted, winding-reversed copy of a ring. Polygon::make must
// canonicalize it back to the same prototile.
std::vector<Point> shifted_reversed(const std::vector<Point> &p_ring) {
    std::vector<Point> out;
    const std::size_t n = p_ring.size();
    // Reverse the winding, then start three corners along.
    for (std::size_t k = 0; k < n; ++k) {
        out.push_back(p_ring[(3 * n - k) % n]);
    }
    return out;
}

std::vector<OrientedPrototile> compile_all(const std::vector<Point> &p_ring, std::uint64_t p_id) {
    auto compiled = compile_lattice_orientations(proto(p_id, p_ring), { R, Q, H, T });
    return std::move(compiled).value();
}

// One expected distinct variant: its representative and complete admitted set.
struct Group {
    Orientation representative;
    std::vector<Orientation> equivalents;
};

void check_tetromino(
    const char *,
    const std::vector<Point> &p_ring,
    std::uint64_t p_id,
    const std::vector<Group> &p_expected) {
    const std::vector<OrientedPrototile> variants = compile_all(p_ring, p_id);
    CHECK(variants.size() == p_expected.size());
    if (variants.size() != p_expected.size()) {
        return;
    }

    // Every one-sided tetromino covers four unit cells; its exact doubled area is
    // 8 * SCALE^2 in every orientation.
    const Int256 expected_double_area = Int256::multiply(
        static_cast<__int128>(8) * Coordinate::SCALE, Coordinate::SCALE);

    const Prototile reference = proto(p_id, p_ring);
    for (std::size_t i = 0; i < variants.size(); ++i) {
        const OrientedPrototile &variant = variants[i];
        const Group &expected = p_expected[i];

        // Deterministic angular order and the complete admitted-angle set.
        CHECK(variant.orientation() == expected.representative);
        CHECK(variant.equivalent_orientations() == expected.equivalents);

        const Polygon &polygon = variant.canonical_polygon();
        // Valid simple polygon with a valid triangulation certificate.
        CHECK(polygon.vertices().size() >= 3);
        CHECK(polygon.triangulation().size() == polygon.vertices().size() - 2);
        // Exact doubled area preserved by the rotation.
        CHECK(signed_double_area(polygon.vertices()) == expected_double_area);
        // Translation-normalized to minimum x zero.
        CHECK(polygon.vertices().front().x == Coordinate::from_raw(0));
        // Direct-from-reference: the named transform of the reference vertices
        // reproduces the stored canonical boundary exactly.
        CHECK(same_boundary(polygon, expected_canonical(reference, expected.representative)));
    }

    // Compiling a cyclically shifted, reversed input winding yields the same
    // variants in the same order.
    const std::vector<OrientedPrototile> reshuffled =
        compile_all(shifted_reversed(p_ring), p_id + 1000);
    CHECK(reshuffled.size() == variants.size());
    for (std::size_t i = 0; i < variants.size() && i < reshuffled.size(); ++i) {
        CHECK(reshuffled[i].orientation() == variants[i].orientation());
        CHECK(same_boundary(reshuffled[i].canonical_polygon(), variants[i].canonical_polygon()));
    }
}

} // namespace

TEST_CASE("tetromino o compiles to one orientation") {
    check_tetromino("o", tetromino_o(), 1, { { R, { R, Q, H, T } } });
}

TEST_CASE("tetromino i compiles to two orientations") {
    check_tetromino("i", tetromino_i(), 2, { { R, { R, H } }, { Q, { Q, T } } });
}

TEST_CASE("tetromino t compiles to four orientations") {
    check_tetromino("t", tetromino_t(), 3, { { R, { R } }, { Q, { Q } }, { H, { H } }, { T, { T } } });
}

TEST_CASE("tetromino s compiles to two orientations") {
    check_tetromino("s", tetromino_s(), 4, { { R, { R, H } }, { Q, { Q, T } } });
}

TEST_CASE("tetromino z compiles to two orientations") {
    check_tetromino("z", tetromino_z(), 5, { { R, { R, H } }, { Q, { Q, T } } });
}

TEST_CASE("tetromino j compiles to four orientations") {
    check_tetromino("j", tetromino_j(), 6, { { R, { R } }, { Q, { Q } }, { H, { H } }, { T, { T } } });
}

TEST_CASE("tetromino l compiles to four orientations") {
    check_tetromino("l", tetromino_l(), 7, { { R, { R } }, { Q, { Q } }, { H, { H } }, { T, { T } } });
}

TEST_CASE("the seven one-sided tetrominoes total nineteen distinct orientations") {
    const std::size_t total =
        compile_all(tetromino_o(), 10).size()
        + compile_all(tetromino_i(), 11).size()
        + compile_all(tetromino_t(), 12).size()
        + compile_all(tetromino_s(), 13).size()
        + compile_all(tetromino_z(), 14).size()
        + compile_all(tetromino_j(), 15).size()
        + compile_all(tetromino_l(), 16).size();
    CHECK(total == 19);
}

TEST_CASE("no chiral tetromino orientation coincides with its mirror partner") {
    // Rotation never turns an l into a j or an s into a z: every l orientation
    // differs from every j orientation, and every s from every z.
    const std::vector<OrientedPrototile> l = compile_all(tetromino_l(), 20);
    const std::vector<OrientedPrototile> j = compile_all(tetromino_j(), 21);
    for (const OrientedPrototile &lv : l) {
        for (const OrientedPrototile &jv : j) {
            CHECK(!same_boundary(lv.canonical_polygon(), jv.canonical_polygon()));
        }
    }

    const std::vector<OrientedPrototile> s = compile_all(tetromino_s(), 22);
    const std::vector<OrientedPrototile> z = compile_all(tetromino_z(), 23);
    for (const OrientedPrototile &sv : s) {
        for (const OrientedPrototile &zv : z) {
            CHECK(!same_boundary(sv.canonical_polygon(), zv.canonical_polygon()));
        }
    }
}
