#include "TestHarness.h"

#include "content/CellBoundary.h"
#include "content/GeometryDomain.h"
#include "content/PrototileCatalog.h"
#include "core/Hex12.h"
#include "core/Orientation.h"
#include "core/OrientedPrototile.h"
#include "core/Prototile.h"
#include "core/geometry/Coordinate.h"
#include "core/geometry/ExactInteger.h"
#include "core/geometry/Point.h"
#include "core/geometry/Polygon.h"
#include "core/geometry/Predicates.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace tiles;
using tiles::content::CanonicalDefinition;
using tiles::content::CanonicalPrototile;
using tiles::content::Cell;
using tiles::content::CellBoundaryErrorCode;
using tiles::content::GeometryDomain;
using tiles::content::PrototileCatalog;
using tiles::content::PrototileCatalogErrorCode;
using tiles::content::PrototileCatalogStage;

namespace {

// The lattice domain view: the thirty-four identities the lattice admits, in
// their established order.
constexpr std::size_t CATALOG_SIZE = 34;

// Every canonical identity, counting the three hex-12-only ones once each and
// the shared unit square once, not twice.
constexpr std::size_t MASTER_SIZE = 37;

// The hex-12 domain view: triangle, square, hexagon, dodecagon.
constexpr std::size_t HEX12_SIZE = 4;

const std::vector<Orientation> &quarter_turns() {
    static const std::vector<Orientation> requested = {
        Orientation::reference(),
        Orientation::quarter(),
        Orientation::half(),
        Orientation::three_quarter(),
    };
    return requested;
}

Orientation twelfth(std::uint32_t p_step) {
    return Orientation::make(p_step, 12).value();
}

std::vector<Orientation> all_twelfth_turns() {
    std::vector<Orientation> requested;
    for (std::uint32_t k = 0; k < 12; ++k) {
        requested.push_back(twelfth(k));
    }
    return requested;
}

std::optional<PrototileCatalog> canonical() {
    auto built = content::make_canonical_prototile_catalog();
    if (!built) {
        return std::nullopt;
    }
    return std::move(built).value();
}

// The lattice presentation view, which is what every established lattice
// expectation below is about.
std::vector<const CanonicalPrototile *> lattice_view(const PrototileCatalog &p_catalog) {
    return p_catalog.entries_for(GeometryDomain::lattice);
}

// The exact doubled area of a whole-unit shape, expressed as a count of unit
// squares. Doubled area is the project's complete comparison value; it is never
// halved or converted to a scalar.
Int256 doubled_unit_area(std::int64_t p_unit_squares) {
    return Int256::multiply(
        static_cast<__int128>(2 * p_unit_squares) * Coordinate::SCALE, Coordinate::SCALE);
}

std::vector<Polygon> admitted_rotations(const Prototile &p_prototile) {
    std::vector<Polygon> polygons;
    auto compiled = compile_lattice_orientations(p_prototile, quarter_turns());
    if (!compiled) {
        return polygons;
    }
    for (const OrientedPrototile &oriented : compiled.value()) {
        polygons.push_back(oriented.canonical_polygon());
    }
    return polygons;
}

// Whether any admitted rotation of the left prototile has the same canonical
// boundary as the right one's reference geometry. Because canonical polygons
// are translation-normalized, this is exactly congruence under the four
// permitted rotations.
bool congruent_under_rotation(const Prototile &p_lhs, const Prototile &p_rhs) {
    for (const Polygon &rotated : admitted_rotations(p_lhs)) {
        if (same_boundary(rotated, p_rhs.polygon())) {
            return true;
        }
    }
    return false;
}

std::vector<Cell> cells_of(PrototileId::Value p_id) {
    for (const CanonicalDefinition &definition : content::canonical_definitions()) {
        if (definition.id == p_id) {
            return definition.cells;
        }
    }
    return {};
}

// The mirror image of a cell set across the vertical axis, normalized so its
// minimum column is zero. Reflection is applied only to source definitions:
// no prototile, polygon, orientation, or placement is ever reflected.
std::vector<Cell> mirrored_cells(const std::vector<Cell> &p_cells) {
    std::vector<Cell> mirrored;
    mirrored.reserve(p_cells.size());
    std::int64_t max_x = p_cells.empty() ? 0 : p_cells.front().x;
    for (const Cell &cell : p_cells) {
        if (cell.x > max_x) {
            max_x = cell.x;
        }
    }
    for (const Cell &cell : p_cells) {
        mirrored.push_back(Cell { max_x - cell.x, cell.y });
    }
    return mirrored;
}

std::set<std::pair<std::int64_t, std::int64_t>> normalized_cell_set(
    const std::vector<Cell> &p_cells) {
    std::set<std::pair<std::int64_t, std::int64_t>> set;
    if (p_cells.empty()) {
        return set;
    }
    std::int64_t min_x = p_cells.front().x;
    std::int64_t min_y = p_cells.front().y;
    for (const Cell &cell : p_cells) {
        if (cell.x < min_x) {
            min_x = cell.x;
        }
        if (cell.y < min_y) {
            min_y = cell.y;
        }
    }
    for (const Cell &cell : p_cells) {
        set.insert({ cell.x - min_x, cell.y - min_y });
    }
    return set;
}

bool cells_edge_connected(const std::vector<Cell> &p_cells) {
    const std::set<std::pair<std::int64_t, std::int64_t>> set = normalized_cell_set(p_cells);
    if (set.empty()) {
        return false;
    }
    std::set<std::pair<std::int64_t, std::int64_t>> reached;
    std::vector<std::pair<std::int64_t, std::int64_t>> frontier { *set.begin() };
    reached.insert(*set.begin());
    while (!frontier.empty()) {
        const std::pair<std::int64_t, std::int64_t> cell = frontier.back();
        frontier.pop_back();
        const std::pair<std::int64_t, std::int64_t> neighbours[4] = {
            { cell.first - 1, cell.second },
            { cell.first + 1, cell.second },
            { cell.first, cell.second - 1 },
            { cell.first, cell.second + 1 },
        };
        for (const auto &neighbour : neighbours) {
            if (set.count(neighbour) != 0 && reached.insert(neighbour).second) {
                frontier.push_back(neighbour);
            }
        }
    }
    return reached.size() == set.size();
}

// The fixed act-0-3 table, restated here so the test proves the shipped values
// rather than reading them back out of the same source it is checking.
struct ExpectedEntry final {
    PrototileId::Value id;
    const char *display_name;
    std::size_t orientations;
    std::int64_t unit_squares;
};

const ExpectedEntry EXPECTED[CATALOG_SIZE] = {
    { 1, "tetromino o (square 2)", 1, 4 },
    { 2, "tetromino i", 2, 4 },
    { 3, "tetromino t", 4, 4 },
    { 4, "tetromino s", 2, 4 },
    { 5, "tetromino z", 2, 4 },
    { 6, "tetromino j", 4, 4 },
    { 7, "tetromino l", 4, 4 },
    { 8, "pentomino f", 4, 5 },
    { 9, "pentomino f mirrored", 4, 5 },
    { 10, "pentomino i", 2, 5 },
    { 11, "pentomino l", 4, 5 },
    { 12, "pentomino l mirrored", 4, 5 },
    { 13, "pentomino n", 4, 5 },
    { 14, "pentomino n mirrored", 4, 5 },
    { 15, "pentomino p", 4, 5 },
    { 16, "pentomino p mirrored", 4, 5 },
    { 17, "pentomino t", 4, 5 },
    { 18, "pentomino u", 4, 5 },
    { 19, "pentomino v", 4, 5 },
    { 20, "pentomino w", 4, 5 },
    { 21, "pentomino x", 1, 5 },
    { 22, "pentomino y", 4, 5 },
    { 23, "pentomino y mirrored", 4, 5 },
    { 24, "pentomino z", 2, 5 },
    { 25, "pentomino z mirrored", 2, 5 },
    { 26, "domino", 2, 2 },
    { 27, "square 1", 1, 1 },
    { 28, "square 3", 1, 9 },
    { 29, "square 4", 1, 16 },
    { 30, "square 5", 1, 25 },
    { 31, "square 6", 1, 36 },
    { 32, "square 7", 1, 49 },
    { 33, "square 8", 1, 64 },
    { 34, "square 9", 1, 81 },
};

// The six chiral pentomino families, as id pairs.
const std::pair<PrototileId::Value, PrototileId::Value> MIRRORED_PAIRS[6] = {
    { 8, 9 },
    { 11, 12 },
    { 13, 14 },
    { 15, 16 },
    { 22, 23 },
    { 24, 25 },
};

// The six achiral pentominoes, which receive exactly one entry each.
const PrototileId::Value ACHIRAL_PENTOMINOES[6] = { 10, 17, 18, 19, 20, 21 };

// A whole-game-unit point, for comparing catalog geometry against the exact
// rings the tetromino bootstrap authored before this catalog existed.
Point unit(std::int64_t p_gx, std::int64_t p_gy) {
    return tiles_test::raw_pt(p_gx * Coordinate::SCALE, p_gy * Coordinate::SCALE);
}

} // namespace

// ---------------------------------------------------------------------------
// catalog shape
// ---------------------------------------------------------------------------

TEST_CASE("canonical catalog construction succeeds") {
    auto built = content::make_canonical_prototile_catalog();
    CHECK(bool(built));
}

TEST_CASE("the canonical catalog holds exactly thirty-four nonempty lattice entries") {
    auto catalog = canonical();
    CHECK(catalog.has_value());
    if (!catalog) {
        return;
    }
    const std::vector<const CanonicalPrototile *> entries = lattice_view(catalog.value());
    CHECK(entries.empty() == false);
    CHECK(entries.size() == CATALOG_SIZE);
}

TEST_CASE("the canonical catalog holds exactly thirty-seven unique identities") {
    auto catalog = canonical();
    CHECK(catalog.has_value());
    if (!catalog) {
        return;
    }
    CHECK(catalog->entries().size() == MASTER_SIZE);

    std::set<PrototileId::Value> ids;
    for (const CanonicalPrototile &entry : catalog->entries()) {
        ids.insert(entry.prototile().id().value());
    }
    CHECK(ids.size() == MASTER_SIZE);
    for (PrototileId::Value id = 1; id <= MASTER_SIZE; ++id) {
        CHECK(ids.count(id) == 1);
    }
}

TEST_CASE("catalog order, ids, and display names match the fixed table") {
    auto catalog = canonical();
    CHECK(catalog.has_value());
    if (!catalog) {
        return;
    }
    const std::vector<const CanonicalPrototile *> entries = lattice_view(catalog.value());
    CHECK(entries.size() == CATALOG_SIZE);
    for (std::size_t i = 0; i < entries.size() && i < CATALOG_SIZE; ++i) {
        CHECK(entries[i]->prototile().id() == PrototileId(EXPECTED[i].id));
        CHECK(entries[i]->display_name() == std::string(EXPECTED[i].display_name));
        CHECK(entries[i]->display_name().empty() == false);
    }
}

TEST_CASE("catalog ids are exactly one through thirty-four and unique") {
    auto catalog = canonical();
    CHECK(catalog.has_value());
    if (!catalog) {
        return;
    }
    const std::vector<const CanonicalPrototile *> entries = lattice_view(catalog.value());
    std::set<PrototileId::Value> ids;
    for (const CanonicalPrototile *entry : entries) {
        ids.insert(entry->prototile().id().value());
    }
    CHECK(ids.size() == entries.size());
    for (PrototileId::Value id = 1; id <= CATALOG_SIZE; ++id) {
        CHECK(ids.count(id) == 1);
    }
}

TEST_CASE("lookup succeeds for every catalog id and fails outside the table") {
    auto catalog = canonical();
    CHECK(catalog.has_value());
    if (!catalog) {
        return;
    }
    const std::vector<const CanonicalPrototile *> entries = lattice_view(catalog.value());
    for (std::size_t i = 0; i < CATALOG_SIZE && i < entries.size(); ++i) {
        const CanonicalPrototile *found = catalog->find(PrototileId(EXPECTED[i].id));
        CHECK(found != nullptr);
        if (found != nullptr) {
            CHECK(found->prototile().id() == PrototileId(EXPECTED[i].id));
            CHECK(found->display_name() == std::string(EXPECTED[i].display_name));
            // The pointer addresses catalog-owned storage, not a copy, and is
            // the same object the domain view names.
            CHECK(found == entries[i]);
        }
    }

    // Id 0 is a representable encoding, but it is not in this catalog. Nothing
    // is inserted, substituted, or aliased for an unknown id.
    const PrototileId::Value unknown_ids[4] = { 0, 38, 100, 4000000000ULL };
    for (const PrototileId::Value id : unknown_ids) {
        CHECK(catalog->find(PrototileId(id)) == nullptr);
    }
}

TEST_CASE("lookup finds every one of the thirty-seven identities exactly") {
    auto catalog = canonical();
    CHECK(catalog.has_value());
    if (!catalog) {
        return;
    }
    for (PrototileId::Value id = 1; id <= MASTER_SIZE; ++id) {
        const CanonicalPrototile *found = catalog->find(PrototileId(id));
        CHECK(found != nullptr);
        if (found != nullptr) {
            CHECK(found->prototile().id() == PrototileId(id));
            CHECK(found->display_name().empty() == false);
        }
    }
}

TEST_CASE("lookup leaves catalog order and values unchanged") {
    auto catalog = canonical();
    CHECK(catalog.has_value());
    if (!catalog) {
        return;
    }
    std::vector<PrototileId::Value> before;
    for (const CanonicalPrototile &entry : catalog->entries()) {
        before.push_back(entry.prototile().id().value());
    }

    for (PrototileId::Value id = 0; id <= MASTER_SIZE + 5; ++id) {
        catalog->find(PrototileId(id));
    }

    std::vector<PrototileId::Value> after;
    for (const CanonicalPrototile &entry : catalog->entries()) {
        after.push_back(entry.prototile().id().value());
    }
    CHECK(before == after);
    CHECK(catalog->entries().size() == MASTER_SIZE);
    CHECK(lattice_view(catalog.value()).size() == CATALOG_SIZE);
}

TEST_CASE("a copied and a moved catalog retain exact lookup and order") {
    auto catalog = canonical();
    CHECK(catalog.has_value());
    if (!catalog) {
        return;
    }

    PrototileCatalog copied = catalog.value();
    CHECK(copied.entries().size() == MASTER_SIZE);
    std::vector<const CanonicalPrototile *> copied_view = lattice_view(copied);
    CHECK(copied_view.size() == CATALOG_SIZE);
    for (std::size_t i = 0; i < copied_view.size() && i < CATALOG_SIZE; ++i) {
        CHECK(copied_view[i]->prototile().id() == PrototileId(EXPECTED[i].id));
        const CanonicalPrototile *found = copied.find(PrototileId(EXPECTED[i].id));
        CHECK(found == copied_view[i]);
        // Every view pointer addresses the copy's own storage, never the
        // original's.
        CHECK(found >= copied.entries().data());
        CHECK(found < copied.entries().data() + copied.entries().size());
        CHECK(
            found != nullptr
            && same_boundary(
                found->prototile().polygon(),
                lattice_view(catalog.value())[i]->prototile().polygon()));
    }

    PrototileCatalog moved = std::move(copied);
    CHECK(moved.entries().size() == MASTER_SIZE);
    std::vector<const CanonicalPrototile *> moved_view = lattice_view(moved);
    CHECK(moved_view.size() == CATALOG_SIZE);
    for (std::size_t i = 0; i < moved_view.size() && i < CATALOG_SIZE; ++i) {
        CHECK(moved_view[i]->prototile().id() == PrototileId(EXPECTED[i].id));
        CHECK(moved_view[i]->display_name() == std::string(EXPECTED[i].display_name));
        CHECK(moved.find(PrototileId(EXPECTED[i].id)) == moved_view[i]);
        CHECK(moved_view[i] >= moved.entries().data());
        CHECK(moved_view[i] < moved.entries().data() + moved.entries().size());
    }
    CHECK(moved.entries_for(GeometryDomain::hex12).size() == HEX12_SIZE);
}

// ---------------------------------------------------------------------------
// exact geometry
// ---------------------------------------------------------------------------

TEST_CASE("every canonical prototile is valid translation-normalized geometry") {
    auto catalog = canonical();
    CHECK(catalog.has_value());
    if (!catalog) {
        return;
    }
    for (const CanonicalPrototile &entry : catalog->entries()) {
        const Polygon &polygon = entry.prototile().polygon();
        CHECK(polygon.vertices().size() >= 3);
        CHECK(polygon.triangulation().size() == polygon.vertices().size() - 2);
        // Translation-normalized: the canonical first vertex sits at the local
        // origin.
        CHECK(polygon.vertices().front() == unit(0, 0));
        // Every public polygon is counterclockwise, so its doubled area is
        // positive.
        CHECK(signed_double_area(polygon.vertices()).is_negative() == false);
        CHECK(signed_double_area(polygon.vertices()).is_zero() == false);
    }
}

TEST_CASE("every canonical area matches the fixed table") {
    auto catalog = canonical();
    CHECK(catalog.has_value());
    if (!catalog) {
        return;
    }
    const std::vector<const CanonicalPrototile *> entries = lattice_view(catalog.value());
    CHECK(entries.size() == CATALOG_SIZE);
    for (std::size_t i = 0; i < entries.size() && i < CATALOG_SIZE; ++i) {
        CHECK(
            signed_double_area(entries[i]->prototile().polygon().vertices())
            == doubled_unit_area(EXPECTED[i].unit_squares));
    }
}

TEST_CASE("catalog ids one through seven retain the authored tetromino geometry") {
    auto catalog = canonical();
    CHECK(catalog.has_value());
    if (!catalog) {
        return;
    }

    // The exact rings the tetromino bootstrap owned before shipped content
    // existed. Prototile construction translation-normalizes them, so the
    // comparison rebuilds each expected prototile the same way.
    const std::vector<std::vector<Point>> rings = {
        { unit(0, 0), unit(2, 0), unit(2, 2), unit(0, 2) },
        { unit(0, 0), unit(4, 0), unit(4, 1), unit(0, 1) },
        { unit(0, 0), unit(3, 0), unit(3, 1), unit(2, 1),
            unit(2, 2), unit(1, 2), unit(1, 1), unit(0, 1) },
        { unit(0, 0), unit(2, 0), unit(2, 1), unit(3, 1),
            unit(3, 2), unit(1, 2), unit(1, 1), unit(0, 1) },
        { unit(1, 0), unit(3, 0), unit(3, 1), unit(2, 1),
            unit(2, 2), unit(0, 2), unit(0, 1), unit(1, 1) },
        { unit(0, 0), unit(2, 0), unit(2, 3), unit(1, 3), unit(1, 1), unit(0, 1) },
        { unit(0, 0), unit(2, 0), unit(2, 1), unit(1, 1), unit(1, 3), unit(0, 3) },
    };

    for (std::size_t i = 0; i < rings.size(); ++i) {
        const PrototileId id(static_cast<PrototileId::Value>(i + 1));
        auto polygon = Polygon::make(rings[i]);
        CHECK(bool(polygon));
        if (!polygon) {
            continue;
        }
        auto expected = Prototile::make(id, std::move(polygon).value());
        CHECK(bool(expected));
        const CanonicalPrototile *actual = catalog->find(id);
        CHECK(actual != nullptr);
        if (!expected || actual == nullptr) {
            continue;
        }
        CHECK(same_boundary(actual->prototile().polygon(), expected.value().polygon()));
    }
}

TEST_CASE("id one is exactly the side-two square and nothing else is congruent to it") {
    auto catalog = canonical();
    CHECK(catalog.has_value());
    if (!catalog) {
        return;
    }
    const CanonicalPrototile *square_two = catalog->find(PrototileId(1));
    CHECK(square_two != nullptr);
    if (square_two == nullptr) {
        return;
    }

    auto expected_polygon =
        Polygon::make({ unit(0, 0), unit(2, 0), unit(2, 2), unit(0, 2) });
    CHECK(bool(expected_polygon));
    if (expected_polygon) {
        CHECK(same_boundary(square_two->prototile().polygon(), expected_polygon.value()));
    }

    // No second congruent side-2 identity exists anywhere in the lattice domain.
    for (const CanonicalPrototile *entry : lattice_view(catalog.value())) {
        if (entry->prototile().id() == PrototileId(1)) {
            continue;
        }
        CHECK(congruent_under_rotation(entry->prototile(), square_two->prototile()) == false);
    }
}

TEST_CASE("the domino and every square have exact geometry and one or two orientations") {
    auto catalog = canonical();
    CHECK(catalog.has_value());
    if (!catalog) {
        return;
    }

    const CanonicalPrototile *domino = catalog->find(PrototileId(26));
    CHECK(domino != nullptr);
    if (domino != nullptr) {
        auto expected = Polygon::make({ unit(0, 0), unit(2, 0), unit(2, 1), unit(0, 1) });
        CHECK(bool(expected));
        if (expected) {
            CHECK(same_boundary(domino->prototile().polygon(), expected.value()));
        }
        CHECK(admitted_rotations(domino->prototile()).size() == 2);
    }

    const std::pair<PrototileId::Value, std::int64_t> squares[8] = {
        { 27, 1 }, { 28, 3 }, { 29, 4 }, { 30, 5 },
        { 31, 6 }, { 32, 7 }, { 33, 8 }, { 34, 9 },
    };
    for (const auto &square : squares) {
        const CanonicalPrototile *entry = catalog->find(PrototileId(square.first));
        CHECK(entry != nullptr);
        if (entry == nullptr) {
            continue;
        }
        const std::int64_t side = square.second;
        auto expected = Polygon::make(
            { unit(0, 0), unit(side, 0), unit(side, side), unit(0, side) });
        CHECK(bool(expected));
        if (expected) {
            CHECK(same_boundary(entry->prototile().polygon(), expected.value()));
        }
        CHECK(
            signed_double_area(entry->prototile().polygon().vertices())
            == doubled_unit_area(side * side));
        CHECK(admitted_rotations(entry->prototile()).size() == 1);
    }
}

// ---------------------------------------------------------------------------
// orientations
// ---------------------------------------------------------------------------

TEST_CASE("distinct orientation counts match the fixed table") {
    auto catalog = canonical();
    CHECK(catalog.has_value());
    if (!catalog) {
        return;
    }
    const std::vector<const CanonicalPrototile *> entries = lattice_view(catalog.value());
    CHECK(entries.size() == CATALOG_SIZE);
    for (std::size_t i = 0; i < entries.size() && i < CATALOG_SIZE; ++i) {
        auto compiled =
            compile_lattice_orientations(entries[i]->prototile(), quarter_turns());
        CHECK(bool(compiled));
        if (compiled) {
            CHECK(compiled.value().size() == EXPECTED[i].orientations);
        }
    }
}

TEST_CASE("the tetromino orientation counts remain 1,2,4,2,2,4,4") {
    auto catalog = canonical();
    CHECK(catalog.has_value());
    if (!catalog) {
        return;
    }
    const std::size_t expected[7] = { 1, 2, 4, 2, 2, 4, 4 };
    std::size_t total = 0;
    for (PrototileId::Value id = 1; id <= 7; ++id) {
        const CanonicalPrototile *entry = catalog->find(PrototileId(id));
        CHECK(entry != nullptr);
        if (entry == nullptr) {
            continue;
        }
        const std::size_t count = admitted_rotations(entry->prototile()).size();
        CHECK(count == expected[id - 1]);
        total += count;
    }
    CHECK(total == 19);
}

TEST_CASE("the eighteen one-sided pentominoes sum to exactly sixty-three orientations") {
    auto catalog = canonical();
    CHECK(catalog.has_value());
    if (!catalog) {
        return;
    }
    std::size_t pentominoes = 0;
    std::size_t total = 0;
    for (PrototileId::Value id = 8; id <= 25; ++id) {
        const CanonicalPrototile *entry = catalog->find(PrototileId(id));
        CHECK(entry != nullptr);
        if (entry == nullptr) {
            continue;
        }
        ++pentominoes;
        total += admitted_rotations(entry->prototile()).size();
    }
    CHECK(pentominoes == 18);
    CHECK(total == 63);
}

TEST_CASE("equal-area catalog entries are pairwise distinct under admitted rotation") {
    auto catalog = canonical();
    CHECK(catalog.has_value());
    if (!catalog) {
        return;
    }
    const std::vector<const CanonicalPrototile *> entries = lattice_view(catalog.value());
    for (std::size_t i = 0; i < entries.size(); ++i) {
        for (std::size_t j = i + 1; j < entries.size(); ++j) {
            const Int256 left =
                signed_double_area(entries[i]->prototile().polygon().vertices());
            const Int256 right =
                signed_double_area(entries[j]->prototile().polygon().vertices());
            if (left != right) {
                // Different area cannot be congruent, and comparing rotations
                // would only restate that.
                continue;
            }
            CHECK(
                congruent_under_rotation(entries[i]->prototile(), entries[j]->prototile())
                == false);
        }
    }
}

// ---------------------------------------------------------------------------
// pentomino source definitions
// ---------------------------------------------------------------------------

TEST_CASE("every pentomino bitmap has five unique edge-connected cells") {
    for (PrototileId::Value id = 8; id <= 25; ++id) {
        const std::vector<Cell> cells = cells_of(id);
        CHECK(cells.size() == 5);
        CHECK(normalized_cell_set(cells).size() == 5);
        CHECK(cells_edge_connected(cells));
    }
}

TEST_CASE("every pentomino bitmap traces one valid boundary of five square units") {
    auto catalog = canonical();
    CHECK(catalog.has_value());
    if (!catalog) {
        return;
    }
    for (PrototileId::Value id = 8; id <= 25; ++id) {
        auto traced = content::trace_cell_boundary(cells_of(id));
        CHECK(bool(traced));
        if (!traced) {
            continue;
        }
        auto polygon = Polygon::make(traced.value());
        CHECK(bool(polygon));
        if (!polygon) {
            continue;
        }
        CHECK(signed_double_area(polygon.value().vertices()) == doubled_unit_area(5));

        // The catalog entry is exactly the product of that same tracing.
        const CanonicalPrototile *entry = catalog->find(PrototileId(id));
        CHECK(entry != nullptr);
        if (entry != nullptr) {
            auto expected = Prototile::make(PrototileId(id), std::move(polygon).value());
            CHECK(bool(expected));
            if (expected) {
                CHECK(same_boundary(
                    entry->prototile().polygon(), expected.value().polygon()));
            }
        }
    }
}

TEST_CASE("each chiral pentomino pair is an exact reflection at the definition level") {
    for (const auto &pair : MIRRORED_PAIRS) {
        const std::vector<Cell> left = cells_of(pair.first);
        const std::vector<Cell> right = cells_of(pair.second);
        CHECK(left.size() == 5);
        CHECK(right.size() == 5);
        CHECK(normalized_cell_set(mirrored_cells(left)) == normalized_cell_set(right));
        CHECK(normalized_cell_set(mirrored_cells(right)) == normalized_cell_set(left));
    }
}

TEST_CASE("each chiral pentomino pair stays distinct under the four permitted rotations") {
    auto catalog = canonical();
    CHECK(catalog.has_value());
    if (!catalog) {
        return;
    }
    for (const auto &pair : MIRRORED_PAIRS) {
        const CanonicalPrototile *left = catalog->find(PrototileId(pair.first));
        const CanonicalPrototile *right = catalog->find(PrototileId(pair.second));
        CHECK(left != nullptr);
        CHECK(right != nullptr);
        if (left == nullptr || right == nullptr) {
            continue;
        }
        CHECK(congruent_under_rotation(left->prototile(), right->prototile()) == false);
        CHECK(congruent_under_rotation(right->prototile(), left->prototile()) == false);
    }
}

TEST_CASE("each achiral pentomino has no duplicate mirror entry in the catalog") {
    auto catalog = canonical();
    CHECK(catalog.has_value());
    if (!catalog) {
        return;
    }
    for (const PrototileId::Value id : ACHIRAL_PENTOMINOES) {
        const std::vector<Cell> cells = cells_of(id);
        CHECK(cells.size() == 5);

        // Its mirror is congruent to itself under rotation, which is why it
        // needs no second identity.
        auto traced = content::trace_cell_boundary(mirrored_cells(cells));
        CHECK(bool(traced));
        if (!traced) {
            continue;
        }
        auto polygon = Polygon::make(traced.value());
        CHECK(bool(polygon));
        if (!polygon) {
            continue;
        }
        auto mirror = Prototile::make(PrototileId(id), std::move(polygon).value());
        CHECK(bool(mirror));
        const CanonicalPrototile *entry = catalog->find(PrototileId(id));
        CHECK(entry != nullptr);
        if (mirror && entry != nullptr) {
            CHECK(congruent_under_rotation(mirror.value(), entry->prototile()));
        }

        // And no other catalog entry claims to be its mirror.
        std::size_t congruent = 0;
        for (const CanonicalPrototile *other : lattice_view(catalog.value())) {
            if (congruent_under_rotation(other->prototile(), entry->prototile())) {
                ++congruent;
            }
        }
        CHECK(congruent == 1);
    }
}

TEST_CASE("the catalog stores no color metadata") {
    // A compile-time fact expressed as a test: CanonicalPrototile exposes
    // exactly the exact prototile and its display name. If a color accessor or
    // color type were reintroduced, this file would name it.
    auto catalog = canonical();
    CHECK(catalog.has_value());
    if (!catalog) {
        return;
    }
    for (const CanonicalPrototile &entry : catalog->entries()) {
        static_assert(
            std::is_same<decltype(entry.prototile()), const Prototile &>::value,
            "a canonical entry owns exactly one exact prototile");
        static_assert(
            std::is_same<decltype(entry.display_name()), const std::string &>::value,
            "a canonical entry owns exactly one display name");
        CHECK(entry.display_name().empty() == false);
    }
}

// ---------------------------------------------------------------------------
// cell-boundary tracing failures
// ---------------------------------------------------------------------------

TEST_CASE("tracing rejects an empty cell set") {
    auto traced = content::trace_cell_boundary({});
    CHECK(bool(traced) == false);
    if (!traced) {
        CHECK(traced.error().code == CellBoundaryErrorCode::empty);
    }
}

TEST_CASE("tracing rejects a duplicate cell") {
    auto traced = content::trace_cell_boundary(
        { Cell { 0, 0 }, Cell { 1, 0 }, Cell { 0, 0 } });
    CHECK(bool(traced) == false);
    if (!traced) {
        CHECK(traced.error().code == CellBoundaryErrorCode::duplicate_cell);
        CHECK(traced.error().cell.has_value());
        if (traced.error().cell.has_value()) {
            const Cell origin { 0, 0 };
            CHECK(traced.error().cell.value() == origin);
        }
    }
}

TEST_CASE("tracing rejects a cell outside the representable whole-unit range") {
    auto traced = content::trace_cell_boundary({ Cell { 40000, 0 } });
    CHECK(bool(traced) == false);
    if (!traced) {
        CHECK(traced.error().code == CellBoundaryErrorCode::coordinate_out_of_range);
    }
}

TEST_CASE("tracing rejects a diagonally connected cell set") {
    auto traced = content::trace_cell_boundary({ Cell { 0, 0 }, Cell { 1, 1 } });
    CHECK(bool(traced) == false);
    if (!traced) {
        CHECK(traced.error().code == CellBoundaryErrorCode::disconnected);
    }
}

TEST_CASE("tracing rejects a boundary that pinches at one corner") {
    // Edge-connected all the way around, but the boundary passes twice through
    // the corner at (1, 1), where only the diagonally opposite cells (0, 0) and
    // (1, 1) are occupied.
    auto traced = content::trace_cell_boundary({
        Cell { 0, 0 },
        Cell { -1, 0 },
        Cell { -1, 1 },
        Cell { -1, 2 },
        Cell { 0, 2 },
        Cell { 1, 2 },
        Cell { 1, 1 },
    });
    CHECK(bool(traced) == false);
    if (!traced) {
        CHECK(traced.error().code == CellBoundaryErrorCode::branching_vertex);
    }
}

TEST_CASE("tracing rejects a hole-bearing cell set") {
    // A three-by-three ring: every boundary corner is well behaved, but the
    // retained edges form two cycles, so the shape is not a simple polygon.
    std::vector<Cell> ring;
    for (std::int64_t x = 0; x < 3; ++x) {
        for (std::int64_t y = 0; y < 3; ++y) {
            if (x == 1 && y == 1) {
                continue;
            }
            ring.push_back(Cell { x, y });
        }
    }
    auto traced = content::trace_cell_boundary(ring);
    CHECK(bool(traced) == false);
    if (!traced) {
        CHECK(traced.error().code == CellBoundaryErrorCode::multiple_boundary_cycles);
    }
}

TEST_CASE("tracing merges collinear runs into single polygon edges") {
    // A four-by-one bar: sixteen retained unit edges, four corners.
    auto traced = content::trace_cell_boundary(
        { Cell { 0, 0 }, Cell { 1, 0 }, Cell { 2, 0 }, Cell { 3, 0 } });
    CHECK(bool(traced));
    if (!traced) {
        return;
    }
    CHECK(traced.value().size() == 4);
    auto polygon = Polygon::make(traced.value());
    CHECK(bool(polygon));
    if (polygon) {
        CHECK(polygon.value().vertices().size() == 4);
        CHECK(signed_double_area(polygon.value().vertices()) == doubled_unit_area(4));
    }
}

TEST_CASE("tracing produces one counterclockwise ring for a concave cell set") {
    // The plus, whose boundary has four reflex corners.
    auto traced = content::trace_cell_boundary(
        { Cell { 1, 2 }, Cell { 0, 1 }, Cell { 1, 1 }, Cell { 2, 1 }, Cell { 1, 0 } });
    CHECK(bool(traced));
    if (!traced) {
        return;
    }
    CHECK(traced.value().size() == 12);
    CHECK(signed_double_area(traced.value()).is_negative() == false);
}

// ---------------------------------------------------------------------------
// catalog table invariants
// ---------------------------------------------------------------------------

TEST_CASE("catalog construction rejects an empty table") {
    auto built = content::testing::make_catalog({});
    CHECK(bool(built) == false);
    if (!built) {
        CHECK(built.error().stage == PrototileCatalogStage::catalog);
        CHECK(built.error().catalog_error.has_value());
        if (built.error().catalog_error.has_value()) {
            CHECK(built.error().catalog_error.value() == PrototileCatalogErrorCode::empty);
        }
        CHECK(built.error().prototile_id.has_value() == false);
        CHECK(built.error().polygon_error.has_value() == false);
        CHECK(built.error().prototile_error.has_value() == false);
        CHECK(built.error().definition_error.has_value() == false);
    }
}

TEST_CASE("catalog construction rejects a duplicate prototile id") {
    CanonicalDefinition first {};
    first.id = 5;
    first.display_name = "first";
    first.ring = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
    CanonicalDefinition second = first;
    second.display_name = "second";

    auto built = content::testing::make_catalog({ first, second });
    CHECK(bool(built) == false);
    if (!built) {
        CHECK(built.error().stage == PrototileCatalogStage::catalog);
        CHECK(built.error().catalog_error.has_value());
        if (built.error().catalog_error.has_value()) {
            CHECK(built.error().catalog_error.value()
                == PrototileCatalogErrorCode::duplicate_prototile_id);
        }
        CHECK(built.error().prototile_id.has_value());
        if (built.error().prototile_id.has_value()) {
            CHECK(built.error().prototile_id.value() == PrototileId(5));
        }
    }
}

TEST_CASE("catalog construction rejects an empty display name") {
    CanonicalDefinition definition {};
    definition.id = 9;
    definition.display_name = "";
    definition.ring = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };

    auto built = content::testing::make_catalog({ definition });
    CHECK(bool(built) == false);
    if (!built) {
        CHECK(built.error().stage == PrototileCatalogStage::catalog);
        CHECK(built.error().catalog_error.has_value());
        if (built.error().catalog_error.has_value()) {
            CHECK(built.error().catalog_error.value()
                == PrototileCatalogErrorCode::empty_display_name);
        }
        CHECK(built.error().prototile_id.has_value());
        if (built.error().prototile_id.has_value()) {
            CHECK(built.error().prototile_id.value() == PrototileId(9));
        }
    }
}

TEST_CASE("catalog construction preserves a polygon failure with its intended id") {
    CanonicalDefinition definition {};
    definition.id = 11;
    definition.display_name = "degenerate";
    // Three collinear corners: a valid whole-unit ring description with no area.
    definition.ring = { { 0, 0 }, { 1, 0 }, { 2, 0 } };

    auto built = content::testing::make_catalog({ definition });
    CHECK(bool(built) == false);
    if (!built) {
        CHECK(built.error().stage == PrototileCatalogStage::polygon);
        CHECK(built.error().prototile_id.has_value());
        if (built.error().prototile_id.has_value()) {
            CHECK(built.error().prototile_id.value() == PrototileId(11));
        }
        CHECK(built.error().polygon_error.has_value());
        CHECK(built.error().catalog_error.has_value() == false);
        CHECK(built.error().prototile_error.has_value() == false);
        CHECK(built.error().definition_error.has_value() == false);
    }
}

TEST_CASE("catalog construction preserves a definition failure with its intended id") {
    CanonicalDefinition definition {};
    definition.id = 13;
    definition.display_name = "disconnected";
    definition.cells = { Cell { 0, 0 }, Cell { 5, 5 } };

    auto built = content::testing::make_catalog({ definition });
    CHECK(bool(built) == false);
    if (!built) {
        CHECK(built.error().stage == PrototileCatalogStage::definition);
        CHECK(built.error().prototile_id.has_value());
        if (built.error().prototile_id.has_value()) {
            CHECK(built.error().prototile_id.value() == PrototileId(13));
        }
        CHECK(built.error().definition_error.has_value());
        if (built.error().definition_error.has_value()) {
            CHECK(built.error().definition_error.value().code
                == CellBoundaryErrorCode::disconnected);
        }
        CHECK(built.error().polygon_error.has_value() == false);
        CHECK(built.error().catalog_error.has_value() == false);
    }
}

TEST_CASE("catalog construction rejects a definition with no geometry description") {
    CanonicalDefinition definition {};
    definition.id = 17;
    definition.display_name = "empty";

    auto built = content::testing::make_catalog({ definition });
    CHECK(bool(built) == false);
    if (!built) {
        // Neither a lattice source nor a hex-12 source: the definition names no
        // geometry at all, which is a table invariant rather than a boundary
        // tracing failure.
        CHECK(built.error().stage == PrototileCatalogStage::catalog);
        CHECK(built.error().catalog_error.has_value());
        if (built.error().catalog_error.has_value()) {
            CHECK(built.error().catalog_error.value()
                == PrototileCatalogErrorCode::missing_geometry_source);
        }
        CHECK(built.error().prototile_id.has_value());
        if (built.error().prototile_id.has_value()) {
            CHECK(built.error().prototile_id.value() == PrototileId(17));
        }
        CHECK(built.error().definition_error.has_value() == false);
        CHECK(built.error().polygon_error.has_value() == false);
        CHECK(built.error().hex12_error.has_value() == false);
    }
}

TEST_CASE("catalog construction rejects a definition offering both ring and cells") {
    CanonicalDefinition definition {};
    definition.id = 19;
    definition.display_name = "two lattice sources";
    definition.ring = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
    definition.cells = { Cell { 0, 0 } };

    auto built = content::testing::make_catalog({ definition });
    CHECK(bool(built) == false);
    if (!built) {
        CHECK(built.error().stage == PrototileCatalogStage::definition);
        CHECK(built.error().definition_error.has_value());
        if (built.error().definition_error.has_value()) {
            CHECK(built.error().definition_error.value().code == CellBoundaryErrorCode::empty);
        }
        CHECK(built.error().prototile_id.has_value());
        if (built.error().prototile_id.has_value()) {
            CHECK(built.error().prototile_id.value() == PrototileId(19));
        }
    }

    // Adding a hex-12 source does not excuse the two lattice descriptions.
    definition.hex12_polygon = Hex12RegularPolygon::square;
    auto with_hex = content::testing::make_catalog({ definition });
    CHECK(bool(with_hex) == false);
    if (!with_hex) {
        CHECK(with_hex.error().stage == PrototileCatalogStage::definition);
    }
}

TEST_CASE("catalog construction preserves an unsupported hex-12 polygon unchanged") {
    CanonicalDefinition definition {};
    definition.id = 23;
    definition.display_name = "not a hex-12 polygon";
    definition.hex12_polygon = static_cast<Hex12RegularPolygon>(200);

    auto built = content::testing::make_catalog({ definition });
    CHECK(bool(built) == false);
    if (!built) {
        // The value reaches the ordinary hex-12 compiler and returns its own
        // error, rather than being pre-flattened here.
        CHECK(built.error().stage == PrototileCatalogStage::hex12);
        CHECK(built.error().prototile_id.has_value());
        if (built.error().prototile_id.has_value()) {
            CHECK(built.error().prototile_id.value() == PrototileId(23));
        }
        CHECK(built.error().hex12_error.has_value());
        if (built.error().hex12_error.has_value()) {
            CHECK(built.error().hex12_error.value().code
                == Hex12CompilationErrorCode::unsupported_polygon);
            CHECK(built.error().hex12_error.value().orientation.has_value() == false);
            CHECK(built.error().hex12_error.value().polygon_error.has_value() == false);
        }
        CHECK(built.error().catalog_error.has_value() == false);
        CHECK(built.error().polygon_error.has_value() == false);
    }
}

TEST_CASE("catalog construction rejects disagreeing lattice and hex-12 references") {
    // A side-two square claiming to be the unit hex-12 square: both sources
    // build successfully, and only their comparison rejects the definition.
    CanonicalDefinition definition {};
    definition.id = 29;
    definition.display_name = "square two claiming to be square one";
    definition.ring = { { 0, 0 }, { 2, 0 }, { 2, 2 }, { 0, 2 } };
    definition.hex12_polygon = Hex12RegularPolygon::square;

    auto built = content::testing::make_catalog({ definition });
    CHECK(bool(built) == false);
    if (!built) {
        CHECK(built.error().stage == PrototileCatalogStage::catalog);
        CHECK(built.error().catalog_error.has_value());
        if (built.error().catalog_error.has_value()) {
            CHECK(built.error().catalog_error.value()
                == PrototileCatalogErrorCode::reference_geometry_mismatch);
        }
        CHECK(built.error().prototile_id.has_value());
        if (built.error().prototile_id.has_value()) {
            CHECK(built.error().prototile_id.value() == PrototileId(29));
        }
        CHECK(built.error().hex12_error.has_value() == false);
    }

    // The same ring at side one agrees exactly, so the rejection above is about
    // the geometry and not about naming two sources.
    definition.ring = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
    auto agreeing = content::testing::make_catalog({ definition });
    CHECK(bool(agreeing));
}

TEST_CASE("a synthesized table keeps duplicate-id and empty-name precedence") {
    // An empty display name is answered before the duplicate id it also carries.
    CanonicalDefinition first {};
    first.id = 3;
    first.display_name = "first";
    first.hex12_polygon = Hex12RegularPolygon::hexagon;
    CanonicalDefinition second = first;
    second.display_name = "";

    auto built = content::testing::make_catalog({ first, second });
    CHECK(bool(built) == false);
    if (!built) {
        CHECK(built.error().stage == PrototileCatalogStage::catalog);
        CHECK(built.error().catalog_error.has_value());
        if (built.error().catalog_error.has_value()) {
            CHECK(built.error().catalog_error.value()
                == PrototileCatalogErrorCode::empty_display_name);
        }
    }

    // Named, the duplicate id is what remains, and it is answered before the
    // second definition's geometry is compiled.
    second.display_name = "second";
    auto duplicated = content::testing::make_catalog({ first, second });
    CHECK(bool(duplicated) == false);
    if (!duplicated) {
        CHECK(duplicated.error().catalog_error.has_value());
        if (duplicated.error().catalog_error.has_value()) {
            CHECK(duplicated.error().catalog_error.value()
                == PrototileCatalogErrorCode::duplicate_prototile_id);
        }
    }
}

TEST_CASE("the shipped definition table agrees with the shipped catalog") {
    auto catalog = canonical();
    CHECK(catalog.has_value());
    if (!catalog) {
        return;
    }
    const std::vector<CanonicalDefinition> &definitions = content::canonical_definitions();
    CHECK(definitions.size() == MASTER_SIZE);
    CHECK(definitions.size() == catalog->entries().size());
    for (std::size_t i = 0; i < definitions.size() && i < catalog->entries().size(); ++i) {
        CHECK(definitions[i].id == catalog->entries()[i].prototile().id().value());
        CHECK(definitions[i].display_name == catalog->entries()[i].display_name());
        // At most one lattice description, and at least one source overall.
        CHECK(!(!definitions[i].ring.empty() && !definitions[i].cells.empty()));
        CHECK(
            !definitions[i].ring.empty() || !definitions[i].cells.empty()
            || definitions[i].hex12_polygon.has_value());
        // The stored membership metadata is exactly what the definition
        // describes.
        const bool has_lattice =
            !definitions[i].ring.empty() || !definitions[i].cells.empty();
        CHECK(catalog->entries()[i].supports(GeometryDomain::lattice) == has_lattice);
        CHECK(
            catalog->entries()[i].supports(GeometryDomain::hex12)
            == definitions[i].hex12_polygon.has_value());
        CHECK(catalog->entries()[i].hex12_polygon() == definitions[i].hex12_polygon);
    }
}

// ---------------------------------------------------------------------------
// geometry domains
// ---------------------------------------------------------------------------

TEST_CASE("the lattice view is exactly ids one through thirty-four in order") {
    auto catalog = canonical();
    CHECK(catalog.has_value());
    if (!catalog) {
        return;
    }
    const std::vector<const CanonicalPrototile *> view = lattice_view(catalog.value());
    CHECK(view.size() == CATALOG_SIZE);
    for (std::size_t i = 0; i < view.size() && i < CATALOG_SIZE; ++i) {
        CHECK(view[i]->prototile().id() == PrototileId(EXPECTED[i].id));
        CHECK(view[i]->display_name() == std::string(EXPECTED[i].display_name));
    }
}

TEST_CASE("the hex-12 view is exactly triangle, square, hexagon, dodecagon") {
    auto catalog = canonical();
    CHECK(catalog.has_value());
    if (!catalog) {
        return;
    }
    const std::vector<const CanonicalPrototile *> view =
        catalog->entries_for(GeometryDomain::hex12);
    CHECK(view.size() == HEX12_SIZE);
    if (view.size() != HEX12_SIZE) {
        return;
    }

    const PrototileId::Value expected_ids[HEX12_SIZE] = { 35, 27, 36, 37 };
    const char *expected_names[HEX12_SIZE] = {
        "triangle 1", "square 1", "hexagon 1", "dodecagon 1",
    };
    const std::size_t expected_sides[HEX12_SIZE] = { 3, 4, 6, 12 };
    for (std::size_t i = 0; i < HEX12_SIZE; ++i) {
        CHECK(view[i]->prototile().id() == PrototileId(expected_ids[i]));
        CHECK(view[i]->display_name() == std::string(expected_names[i]));
        CHECK(view[i]->prototile().polygon().vertices().size() == expected_sides[i]);
    }
}

TEST_CASE("no id appears twice in a domain view and the shared square is one identity") {
    auto catalog = canonical();
    CHECK(catalog.has_value());
    if (!catalog) {
        return;
    }
    const GeometryDomain domains[2] = { GeometryDomain::lattice, GeometryDomain::hex12 };
    for (const GeometryDomain domain : domains) {
        std::set<PrototileId::Value> ids;
        for (const CanonicalPrototile *entry : catalog->entries_for(domain)) {
            CHECK(ids.insert(entry->prototile().id().value()).second);
            // Every view entry reports support for the domain that admitted it.
            CHECK(entry->supports(domain));
        }
    }

    // Id 27 in both views is the very same catalog object, not a second
    // congruent entry.
    const CanonicalPrototile *in_lattice = nullptr;
    for (const CanonicalPrototile *entry : lattice_view(catalog.value())) {
        if (entry->prototile().id() == PrototileId(27)) {
            in_lattice = entry;
        }
    }
    const CanonicalPrototile *in_hex12 = nullptr;
    for (const CanonicalPrototile *entry : catalog->entries_for(GeometryDomain::hex12)) {
        if (entry->prototile().id() == PrototileId(27)) {
            in_hex12 = entry;
        }
    }
    CHECK(in_lattice != nullptr);
    CHECK(in_lattice == in_hex12);
    CHECK(catalog->find(PrototileId(27)) == in_lattice);

    // And exactly one master entry carries that id.
    std::size_t occurrences = 0;
    for (const CanonicalPrototile &entry : catalog->entries()) {
        if (entry.prototile().id() == PrototileId(27)) {
            ++occurrences;
        }
    }
    CHECK(occurrences == 1);
}

TEST_CASE("an invalid geometry domain admits nothing and is supported by nothing") {
    auto catalog = canonical();
    CHECK(catalog.has_value());
    if (!catalog) {
        return;
    }
    const GeometryDomain invalid_domains[3] = {
        static_cast<GeometryDomain>(2),
        static_cast<GeometryDomain>(7),
        static_cast<GeometryDomain>(200),
    };
    for (const GeometryDomain domain : invalid_domains) {
        CHECK(catalog->entries_for(domain).empty());
        for (const CanonicalPrototile &entry : catalog->entries()) {
            CHECK(entry.supports(domain) == false);
        }
    }
}

TEST_CASE("domain membership is exclusive except for the shared unit square") {
    auto catalog = canonical();
    CHECK(catalog.has_value());
    if (!catalog) {
        return;
    }
    for (PrototileId::Value id = 1; id <= MASTER_SIZE; ++id) {
        const CanonicalPrototile *entry = catalog->find(PrototileId(id));
        CHECK(entry != nullptr);
        if (entry == nullptr) {
            continue;
        }
        const bool hex_only = id >= 35;
        const bool shared = id == 27;
        CHECK(entry->supports(GeometryDomain::lattice) == (!hex_only));
        CHECK(entry->supports(GeometryDomain::hex12) == (hex_only || shared));
        CHECK(entry->hex12_polygon().has_value() == (hex_only || shared));
    }
}

TEST_CASE("the shared unit square has one reference boundary in both domains") {
    auto catalog = canonical();
    CHECK(catalog.has_value());
    if (!catalog) {
        return;
    }
    const CanonicalPrototile *square = catalog->find(PrototileId(27));
    CHECK(square != nullptr);
    if (square == nullptr) {
        return;
    }

    // The lattice source, rebuilt here from the exact unit ring rather than read
    // back out of the catalog.
    auto lattice_polygon =
        Polygon::make({ unit(0, 0), unit(1, 0), unit(1, 1), unit(0, 1) });
    CHECK(bool(lattice_polygon));

    // The hex-12 source, obtained from the ordinary compiler at phase zero.
    auto hex_reference = compile_hex12_orientations(
        PrototileId(27), Hex12RegularPolygon::square, { Orientation::reference() });
    CHECK(bool(hex_reference));
    if (!lattice_polygon || !hex_reference) {
        return;
    }
    CHECK(hex_reference.value().size() == 1);

    CHECK(same_boundary(square->prototile().polygon(), lattice_polygon.value()));
    CHECK(same_boundary(
        square->prototile().polygon(),
        hex_reference.value().front().prototile().polygon()));
    CHECK(hex_reference.value().front().prototile().id() == PrototileId(27));
}

TEST_CASE("each hex-only reference is exactly its compiler's reference prototile") {
    auto catalog = canonical();
    CHECK(catalog.has_value());
    if (!catalog) {
        return;
    }
    const std::pair<PrototileId::Value, Hex12RegularPolygon> hex_only[3] = {
        { 35, Hex12RegularPolygon::triangle },
        { 36, Hex12RegularPolygon::hexagon },
        { 37, Hex12RegularPolygon::dodecagon },
    };
    for (const auto &expectation : hex_only) {
        const CanonicalPrototile *entry = catalog->find(PrototileId(expectation.first));
        CHECK(entry != nullptr);
        auto compiled = compile_hex12_orientations(
            PrototileId(expectation.first),
            expectation.second,
            { Orientation::reference() });
        CHECK(bool(compiled));
        if (entry == nullptr || !compiled) {
            continue;
        }
        CHECK(compiled.value().size() == 1);
        CHECK(same_boundary(
            entry->prototile().polygon(),
            compiled.value().front().prototile().polygon()));
        CHECK(entry->prototile().id() == PrototileId(expectation.first));
        CHECK(entry->hex12_polygon() == std::optional<Hex12RegularPolygon>(expectation.second));
        // Reference geometry is translation-normalized exactly like every other
        // canonical entry.
        CHECK(entry->prototile().polygon().vertices().front() == unit(0, 0));
    }
}

TEST_CASE("no two ids in one domain view are congruent under that domain's rotations") {
    auto catalog = canonical();
    CHECK(catalog.has_value());
    if (!catalog) {
        return;
    }

    // Lattice congruence is congruence under the four quarter turns.
    const std::vector<const CanonicalPrototile *> lattice = lattice_view(catalog.value());
    for (std::size_t i = 0; i < lattice.size(); ++i) {
        for (std::size_t j = i + 1; j < lattice.size(); ++j) {
            CHECK(
                congruent_under_rotation(
                    lattice[i]->prototile(), lattice[j]->prototile())
                == false);
        }
    }

    // Hex-12 congruence is congruence under the twelve twelfth turns, taken
    // from the ordinary hex-12 compiler rather than from the catalog.
    const std::vector<const CanonicalPrototile *> hex12 =
        catalog->entries_for(GeometryDomain::hex12);
    std::vector<std::vector<Polygon>> hex_rotations;
    for (const CanonicalPrototile *entry : hex12) {
        std::vector<Polygon> polygons;
        auto compiled = compile_hex12_orientations(
            entry->prototile().id(), entry->hex12_polygon().value(), all_twelfth_turns());
        CHECK(bool(compiled));
        if (compiled) {
            for (const OrientedPrototile &oriented : compiled.value()) {
                polygons.push_back(oriented.canonical_polygon());
            }
        }
        hex_rotations.push_back(std::move(polygons));
    }
    for (std::size_t i = 0; i < hex12.size(); ++i) {
        for (std::size_t j = i + 1; j < hex12.size(); ++j) {
            bool congruent = false;
            for (const Polygon &rotated : hex_rotations[i]) {
                if (same_boundary(rotated, hex12[j]->prototile().polygon())) {
                    congruent = true;
                }
            }
            CHECK(congruent == false);
        }
    }
}
