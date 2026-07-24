#include "TestHarness.h"

#include "core/Prototile.h"
#include "core/geometry/Polygon.h"

#include <cstdint>
#include <vector>

using namespace tiles;
using tiles_test::raw_pt;

namespace {

Polygon make_or_die(std::vector<Point> p_vertices) {
    auto result = Polygon::make(std::move(p_vertices));
    // Test-only: the inputs here are known-valid.
    return std::move(result).value();
}

bool same_vertices(const Polygon::Vertices &p_a, const Polygon::Vertices &p_b) {
    if (p_a.size() != p_b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < p_a.size(); ++i) {
        if (p_a[i] != p_b[i]) {
            return false;
        }
    }
    return true;
}

} // namespace

TEST_CASE("prototile preserves identity") {
    const PrototileId id(0xABCDEF0123456789ull);
    auto proto = Prototile::make(id, make_or_die({ raw_pt(0, 0), raw_pt(4, 0), raw_pt(2, 3) }));
    CHECK(proto.has_value());
    CHECK(proto.value().id() == id);
}

TEST_CASE("prototile local vertex zero is the origin") {
    auto proto = Prototile::make(
        PrototileId(1),
        make_or_die({ raw_pt(10, 20), raw_pt(14, 20), raw_pt(12, 23) }));
    CHECK(proto.has_value());
    CHECK(proto.value().polygon().vertices().front() == raw_pt(0, 0));
}

TEST_CASE("lattice translations produce identical local geometry") {
    const std::vector<std::pair<std::int64_t, std::int64_t>> shifts = {
        { 0, 0 }, { 10, 10 }, { -7, 3 }, { 100, -50 }, { -1000, -1000 }
    };

    Polygon::Vertices expected;
    for (const auto &shift : shifts) {
        std::vector<Point> vertices = {
            raw_pt(0 + shift.first, 0 + shift.second),
            raw_pt(4 + shift.first, 0 + shift.second),
            raw_pt(2 + shift.first, 3 + shift.second),
        };
        auto proto = Prototile::make(PrototileId(2), make_or_die(std::move(vertices)));
        CHECK(proto.has_value());
        if (expected.empty()) {
            expected = proto.value().polygon().vertices();
        } else {
            CHECK(same_vertices(proto.value().polygon().vertices(), expected));
        }
    }

    // The shared local geometry is the triangle translated to the origin.
    const Polygon::Vertices canonical = { raw_pt(0, 0), raw_pt(4, 0), raw_pt(2, 3) };
    CHECK(same_vertices(expected, canonical));
}

TEST_CASE("prototile normalization survives coordinate limits") {
    // A valid triangle whose vertices sit near the lattice bound but whose
    // pairwise differences remain representable.
    const std::int64_t hi = INT64_MAX;
    auto proto = Prototile::make(
        PrototileId(3),
        make_or_die({ raw_pt(hi - 4, hi - 3), raw_pt(hi, hi - 3), raw_pt(hi - 2, hi) }));
    CHECK(proto.has_value());
    CHECK(proto.value().polygon().vertices().front() == raw_pt(0, 0));
}

TEST_CASE("prototile reports normalization overflow") {
    // Origin at INT64_MIN and a far vertex at INT64_MAX: the translated
    // component is not representable.
    auto proto = Prototile::make(
        PrototileId(4),
        make_or_die({ raw_pt(INT64_MIN, 0), raw_pt(INT64_MAX, 0), raw_pt(0, 5) }));
    CHECK(proto.has_value() == false);
    CHECK(proto.error() == PrototileError::normalization_overflow);
}
