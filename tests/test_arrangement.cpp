#include "TestHarness.h"

#include "core/Arrangement.h"
#include "core/Placement.h"
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

Prototile square() {
    auto polygon = Polygon::make({ raw(0, 0), raw(4, 0), raw(4, 4), raw(0, 4) });
    auto proto = Prototile::make(PrototileId(1), std::move(polygon).value());
    return std::move(proto).value();
}

// A 4x4 square placement whose lower-left corner is at (p_x, p_y).
Placement square_at(std::int64_t p_x, std::int64_t p_y) {
    auto placement = Placement::make(square(), raw(p_x, p_y));
    return std::move(placement).value();
}

} // namespace

TEST_CASE("empty arrangement has no entries and allocates id zero next") {
    const Arrangement arrangement;
    CHECK(arrangement.entries().empty());
    CHECK(arrangement.next_id().has_value());
    CHECK(arrangement.next_id().value() == PlacementId(0));
}

TEST_CASE("inserting one placement yields id zero") {
    Arrangement arrangement;
    auto id = arrangement.try_insert(square_at(0, 0));
    CHECK(id.has_value());
    CHECK(id.value() == PlacementId(0));
    CHECK(arrangement.entries().size() == 1);
    CHECK(arrangement.entries().front().id == PlacementId(0));
}

TEST_CASE("several independent placements get monotonically increasing ids") {
    Arrangement arrangement;
    auto a = arrangement.try_insert(square_at(0, 0));
    auto b = arrangement.try_insert(square_at(10, 0));
    auto c = arrangement.try_insert(square_at(0, 10));
    CHECK(a.has_value());
    CHECK(b.has_value());
    CHECK(c.has_value());
    CHECK(a.value() == PlacementId(0));
    CHECK(b.value() == PlacementId(1));
    CHECK(c.value() == PlacementId(2));
    CHECK(arrangement.entries().size() == 3);
    CHECK(arrangement.next_id().value() == PlacementId(3));
}

TEST_CASE("edge contact between placements is legal") {
    Arrangement arrangement;
    auto a = arrangement.try_insert(square_at(0, 0));
    // Shares the full edge x == 4 with the first square.
    auto b = arrangement.try_insert(square_at(4, 0));
    CHECK(a.has_value());
    CHECK(b.has_value());
    CHECK(arrangement.entries().size() == 2);
}

TEST_CASE("point contact between placements is legal") {
    Arrangement arrangement;
    auto a = arrangement.try_insert(square_at(0, 0));
    // Touches the first square only at the corner (4, 4).
    auto b = arrangement.try_insert(square_at(4, 4));
    CHECK(a.has_value());
    CHECK(b.has_value());
    CHECK(arrangement.entries().size() == 2);
}

TEST_CASE("insertion is rejected against each existing placement in turn") {
    Arrangement arrangement;
    const auto id0 = arrangement.try_insert(square_at(0, 0)).value();
    const auto id1 = arrangement.try_insert(square_at(20, 0)).value();
    const auto id2 = arrangement.try_insert(square_at(0, 20)).value();

    // Each candidate overlaps exactly one existing placement.
    auto hit0 = arrangement.try_insert(square_at(1, 1));
    CHECK(hit0.has_value() == false);
    CHECK(hit0.error().code == ArrangementErrorCode::interior_overlap);
    CHECK(hit0.error().conflicting_placement.has_value());
    CHECK(hit0.error().conflicting_placement.value() == id0);

    auto hit1 = arrangement.try_insert(square_at(21, 1));
    CHECK(hit1.has_value() == false);
    CHECK(hit1.error().conflicting_placement.value() == id1);

    auto hit2 = arrangement.try_insert(square_at(1, 21));
    CHECK(hit2.has_value() == false);
    CHECK(hit2.error().conflicting_placement.value() == id2);
}

TEST_CASE("a rejected insertion preserves entries, ordering, and the id allocator") {
    Arrangement arrangement;
    arrangement.try_insert(square_at(0, 0));
    arrangement.try_insert(square_at(20, 0));
    arrangement.try_insert(square_at(0, 20));

    const std::size_t before_size = arrangement.entries().size();
    const PlacementId before_next = arrangement.next_id().value();
    std::vector<PlacementId> before_ids;
    for (const Entry &entry : arrangement.entries()) {
        before_ids.push_back(entry.id);
    }

    auto rejected = arrangement.try_insert(square_at(2, 2));
    CHECK(rejected.has_value() == false);

    CHECK(arrangement.entries().size() == before_size);
    CHECK(arrangement.next_id().value() == before_next);
    bool ids_unchanged = true;
    for (std::size_t i = 0; i < before_ids.size(); ++i) {
        if (arrangement.entries()[i].id != before_ids[i]) {
            ids_unchanged = false;
        }
    }
    CHECK(ids_unchanged);
}

TEST_CASE("identifier exhaustion fails without corrupting the arrangement") {
    // The construction seam only presets the id allocator of an otherwise empty,
    // valid arrangement, so it cannot fabricate an invalid public value.
    Arrangement arrangement = Arrangement::testing_with_next_id(UINT64_MAX);
    CHECK(arrangement.next_id().value() == PlacementId(UINT64_MAX));

    auto last = arrangement.try_insert(square_at(0, 0));
    CHECK(last.has_value());
    CHECK(last.value() == PlacementId(UINT64_MAX));
    // The final id has been consumed; none remain.
    CHECK(arrangement.next_id().has_value() == false);

    auto overflow = arrangement.try_insert(square_at(100, 100));
    CHECK(overflow.has_value() == false);
    CHECK(overflow.error().code == ArrangementErrorCode::identifier_exhausted);
    CHECK(overflow.error().conflicting_placement.has_value() == false);
    // The failed insertion left the single successful entry in place.
    CHECK(arrangement.entries().size() == 1);
}
