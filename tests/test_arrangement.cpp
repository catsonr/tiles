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
using tiles_test::reference_orientation;

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
    auto placement = Placement::make(reference_orientation(square()), raw(p_x, p_y));
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

TEST_CASE("direct insertion preview returns the exact placement and mutates nothing") {
    Arrangement arrangement;
    CHECK(arrangement.try_insert(square_at(0, 0)).has_value());

    const Arrangement &observed = arrangement;
    auto previewed = observed.preview_insert(square_at(10, 0));
    CHECK(previewed.has_value());
    if (previewed.has_value()) {
        // The supplied placement, unchanged: same translation, same footprint.
        CHECK(previewed.value().translation() == raw(10, 0));
        CHECK(previewed.value().footprint().vertices()
            == square_at(10, 0).footprint().vertices());
    }

    // Nothing was appended and no identity was reserved or predicted.
    CHECK(arrangement.entries().size() == 1);
    CHECK(arrangement.next_id().value() == PlacementId(1));

    // Repeating it against the unmodified arrangement is identical.
    auto again = observed.preview_insert(square_at(10, 0));
    CHECK(again.has_value());
    if (previewed.has_value() && again.has_value()) {
        CHECK(previewed.value().footprint().vertices()
            == again.value().footprint().vertices());
    }
    CHECK(arrangement.entries().size() == 1);
}

TEST_CASE("insertion preview applies the same rejection precedence as insertion") {
    Arrangement arrangement;
    const auto id0 = arrangement.try_insert(square_at(0, 0)).value();
    const auto id1 = arrangement.try_insert(square_at(20, 0)).value();

    // The first conflicting placement in storage order names the conflict, in
    // both directions.
    auto first = arrangement.preview_insert(square_at(1, 1));
    CHECK(first.has_value() == false);
    if (!first.has_value()) {
        CHECK(first.error().code == ArrangementErrorCode::interior_overlap);
        CHECK(first.error().conflicting_placement.value() == id0);
    }
    auto second = arrangement.preview_insert(square_at(21, 1));
    CHECK(second.has_value() == false);
    if (!second.has_value()) {
        CHECK(second.error().conflicting_placement.value() == id1);
    }

    // A rejected preview leaves the arrangement exactly as it was.
    CHECK(arrangement.entries().size() == 2);
    CHECK(arrangement.next_id().value() == PlacementId(2));

    // Identifier exhaustion is reported by preview too, with no conflict id.
    Arrangement exhausted = Arrangement::testing_with_next_id(UINT64_MAX);
    CHECK(exhausted.try_insert(square_at(0, 0)).has_value());
    auto overflow = exhausted.preview_insert(square_at(100, 100));
    CHECK(overflow.has_value() == false);
    if (!overflow.has_value()) {
        CHECK(overflow.error().code == ArrangementErrorCode::identifier_exhausted);
        CHECK(overflow.error().conflicting_placement.has_value() == false);
    }
}

TEST_CASE("a successful insertion preview and an immediate insertion agree") {
    Arrangement arrangement;
    CHECK(arrangement.try_insert(square_at(0, 0)).has_value());

    auto previewed = arrangement.preview_insert(square_at(4, 0));
    CHECK(previewed.has_value());
    if (!previewed.has_value()) {
        return;
    }
    const Polygon::Vertices expected = previewed.value().footprint().vertices();

    auto inserted = arrangement.try_insert(square_at(4, 0));
    CHECK(inserted.has_value());
    if (!inserted.has_value()) {
        return;
    }
    CHECK(inserted.value() == PlacementId(1));
    CHECK(arrangement.entries().back().id == inserted.value());
    CHECK(arrangement.entries().back().placement.footprint().vertices() == expected);
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
