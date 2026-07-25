#include "TestHarness.h"

#include "core/Arrangement.h"
#include "core/Placement.h"
#include "core/Prototile.h"
#include "core/geometry/Polygon.h"

#include <cstddef>
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

// The complete stored identity sequence, so a test can assert order rather than
// merely size.
std::vector<PlacementId> ids_of(const Arrangement &p_arrangement) {
    std::vector<PlacementId> ids;
    for (const Entry &entry : p_arrangement.entries()) {
        ids.push_back(entry.id);
    }
    return ids;
}

// The complete stored geometry sequence, paired positionally with ids_of.
std::vector<Point> translations_of(const Arrangement &p_arrangement) {
    std::vector<Point> translations;
    for (const Entry &entry : p_arrangement.entries()) {
        translations.push_back(entry.placement.translation());
    }
    return translations;
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

// ---------------------------------------------------------------------------
// exact removal
// ---------------------------------------------------------------------------

TEST_CASE("removing from an empty arrangement reports placement_not_found") {
    Arrangement arrangement;

    auto removed = arrangement.try_remove(PlacementId(0));
    CHECK(removed.has_value() == false);
    if (!removed.has_value()) {
        CHECK(removed.error() == RemovalError::placement_not_found);
    }

    // An id that was never allocated is missing, not an allocator instruction.
    CHECK(arrangement.entries().empty());
    CHECK(arrangement.next_id().value() == PlacementId(0));
}

TEST_CASE("removing an unknown id from a nonempty arrangement changes nothing at all") {
    Arrangement arrangement;
    CHECK(arrangement.try_insert(square_at(0, 0)).has_value());
    CHECK(arrangement.try_insert(square_at(10, 0)).has_value());
    CHECK(arrangement.try_insert(square_at(0, 10)).has_value());

    const std::vector<PlacementId> before_ids = ids_of(arrangement);
    const std::vector<Point> before_translations = translations_of(arrangement);
    const PlacementId before_next = arrangement.next_id().value();

    // One id beyond the allocator, and one far past it.
    auto just_past = arrangement.try_remove(PlacementId(3));
    CHECK(just_past.has_value() == false);
    if (!just_past.has_value()) {
        CHECK(just_past.error() == RemovalError::placement_not_found);
    }
    auto far_past = arrangement.try_remove(PlacementId(9999));
    CHECK(far_past.has_value() == false);
    if (!far_past.has_value()) {
        CHECK(far_past.error() == RemovalError::placement_not_found);
    }

    CHECK(ids_of(arrangement) == before_ids);
    CHECK(translations_of(arrangement) == before_translations);
    CHECK(arrangement.next_id().value() == before_next);
    // Every placement value survives untouched, not merely every identity.
    CHECK(arrangement.entries()[1].placement.footprint().vertices()
        == square_at(10, 0).footprint().vertices());
}

TEST_CASE("a failed removal preserves an exhausted allocator") {
    Arrangement arrangement = Arrangement::testing_with_next_id(UINT64_MAX);
    CHECK(arrangement.try_insert(square_at(0, 0)).has_value());
    CHECK(arrangement.next_id().has_value() == false);

    auto missing = arrangement.try_remove(PlacementId(0));
    CHECK(missing.has_value() == false);
    if (!missing.has_value()) {
        CHECK(missing.error() == RemovalError::placement_not_found);
    }
    CHECK(arrangement.entries().size() == 1);
    CHECK(arrangement.next_id().has_value() == false);
}

TEST_CASE("removing the first, middle, or last stored entry preserves survivor order") {
    // Each case rebuilds the same three entries and deletes one storage
    // position, so the survivors' relative order is the only thing under test.
    const PlacementId expected[3] = { PlacementId(0), PlacementId(1), PlacementId(2) };
    const std::int64_t xs[3] = { 0, 10, 20 };

    for (std::size_t removed_index = 0; removed_index < 3; ++removed_index) {
        Arrangement arrangement;
        for (std::size_t i = 0; i < 3; ++i) {
            CHECK(arrangement.try_insert(square_at(xs[i], 0)).has_value());
        }

        auto removed = arrangement.try_remove(expected[removed_index]);
        CHECK(removed.has_value());
        if (!removed.has_value()) {
            continue;
        }
        // Success returns the exact requested identity, never a neighbour's.
        CHECK(removed.value() == expected[removed_index]);

        std::vector<PlacementId> survivors;
        std::vector<Point> survivor_translations;
        for (std::size_t i = 0; i < 3; ++i) {
            if (i == removed_index) {
                continue;
            }
            survivors.push_back(expected[i]);
            survivor_translations.push_back(raw(xs[i], 0));
        }
        CHECK(arrangement.entries().size() == 2);
        CHECK(ids_of(arrangement) == survivors);
        CHECK(translations_of(arrangement) == survivor_translations);
        // The allocator only ever moves forward: deletion never rewinds it.
        CHECK(arrangement.next_id().value() == PlacementId(3));
    }
}

TEST_CASE("an identity is never reused merely because its entry was deleted") {
    Arrangement arrangement;
    CHECK(arrangement.try_insert(square_at(0, 0)).has_value());
    CHECK(arrangement.try_insert(square_at(10, 0)).has_value());
    CHECK(arrangement.try_insert(square_at(20, 0)).has_value());

    CHECK(arrangement.try_remove(PlacementId(1)).has_value());
    CHECK(arrangement.next_id().value() == PlacementId(3));

    // The freed space is reusable; the freed identity is not.
    auto refilled = arrangement.try_insert(square_at(10, 0));
    CHECK(refilled.has_value());
    if (refilled.has_value()) {
        CHECK(refilled.value() == PlacementId(3));
    }
    const std::vector<PlacementId> expected {
        PlacementId(0), PlacementId(2), PlacementId(3)
    };
    CHECK(ids_of(arrangement) == expected);
    CHECK(arrangement.next_id().value() == PlacementId(4));

    // And the gap stays a gap: id 1 is gone for good.
    auto again = arrangement.try_remove(PlacementId(1));
    CHECK(again.has_value() == false);
}

TEST_CASE("deleting the final entry leaves an empty, allocation-continuing arrangement") {
    Arrangement arrangement;
    CHECK(arrangement.try_insert(square_at(0, 0)).has_value());
    CHECK(arrangement.try_insert(square_at(10, 0)).has_value());

    CHECK(arrangement.try_remove(PlacementId(0)).has_value());
    CHECK(arrangement.try_remove(PlacementId(1)).has_value());

    CHECK(arrangement.entries().empty());
    // Empty, but not new: the allocator is exactly where it was left.
    CHECK(arrangement.next_id().value() == PlacementId(2));

    auto next = arrangement.try_insert(square_at(0, 0));
    CHECK(next.has_value());
    if (next.has_value()) {
        CHECK(next.value() == PlacementId(2));
    }
}

TEST_CASE("deleting from an exhausted arrangement leaves it exhausted") {
    Arrangement arrangement = Arrangement::testing_with_next_id(UINT64_MAX);
    const auto last = arrangement.try_insert(square_at(0, 0));
    CHECK(last.has_value());
    if (!last.has_value()) {
        return;
    }
    CHECK(arrangement.next_id().has_value() == false);

    auto removed = arrangement.try_remove(last.value());
    CHECK(removed.has_value());
    if (removed.has_value()) {
        CHECK(removed.value() == PlacementId(UINT64_MAX));
    }
    CHECK(arrangement.entries().empty());

    // Deletion frees space, never another representable identity.
    CHECK(arrangement.next_id().has_value() == false);
    auto overflow = arrangement.try_insert(square_at(0, 0));
    CHECK(overflow.has_value() == false);
    if (!overflow.has_value()) {
        CHECK(overflow.error().code == ArrangementErrorCode::identifier_exhausted);
    }
}

TEST_CASE("removal preserves the contact and overlap invariants among survivors") {
    // Three squares in a row, each sharing a complete edge with its neighbour.
    Arrangement arrangement;
    const auto left = arrangement.try_insert(square_at(0, 0)).value();
    const auto middle = arrangement.try_insert(square_at(4, 0)).value();
    const auto right = arrangement.try_insert(square_at(8, 0)).value();

    CHECK(arrangement.try_remove(middle).has_value());
    // The survivors are the outer two, now with a gap between them. Disconnected
    // coverage is a legal consequence of deletion, not a violated invariant.
    const std::vector<PlacementId> survivors { left, right };
    CHECK(ids_of(arrangement) == survivors);

    // Each survivor still rejects a candidate overlapping it, and still names
    // itself as the conflict.
    auto onto_left = arrangement.preview_insert(square_at(1, 1));
    CHECK(onto_left.has_value() == false);
    if (!onto_left.has_value()) {
        CHECK(onto_left.error().conflicting_placement.value() == left);
    }
    auto onto_right = arrangement.preview_insert(square_at(9, 1));
    CHECK(onto_right.has_value() == false);
    if (!onto_right.has_value()) {
        CHECK(onto_right.error().conflicting_placement.value() == right);
    }

    // And the vacated space accepts a placement that touches both survivors
    // along complete edges: exactly the contact the removed entry had.
    auto refilled = arrangement.try_insert(square_at(4, 0));
    CHECK(refilled.has_value());
    CHECK(arrangement.entries().size() == 3);
}
