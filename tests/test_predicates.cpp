#include "TestHarness.h"

#include "core/geometry/ExactInteger.h"
#include "core/geometry/Predicates.h"

#include <algorithm>
#include <cstdint>
#include <vector>

using namespace tiles;
using tiles_test::raw_pt;

TEST_CASE("orientation of three points near raw extrema") {
    // Counterclockwise, clockwise, collinear with small coordinates.
    CHECK(orientation(raw_pt(0, 0), raw_pt(4, 0), raw_pt(0, 4)) == Turn::counterclockwise);
    CHECK(orientation(raw_pt(0, 0), raw_pt(0, 4), raw_pt(4, 0)) == Turn::clockwise);
    CHECK(orientation(raw_pt(0, 0), raw_pt(2, 2), raw_pt(5, 5)) == Turn::collinear);

    // Large operands: a determinant whose magnitude exceeds signed 128-bit.
    const Point a = raw_pt(INT64_MIN, INT64_MIN);
    const Point b = raw_pt(INT64_MAX, INT64_MIN);
    const Point c = raw_pt(INT64_MIN, INT64_MAX);
    CHECK(orientation(a, b, c) == Turn::counterclockwise);
    CHECK(orientation(a, c, b) == Turn::clockwise);
}

TEST_CASE("point on closed segment") {
    CHECK(on_segment(raw_pt(1, 1), raw_pt(0, 0), raw_pt(2, 2)));
    CHECK(on_segment(raw_pt(0, 0), raw_pt(0, 0), raw_pt(2, 2))); // endpoint
    CHECK(on_segment(raw_pt(2, 2), raw_pt(0, 0), raw_pt(2, 2))); // endpoint
    CHECK(!on_segment(raw_pt(3, 3), raw_pt(0, 0), raw_pt(2, 2))); // collinear, outside
    CHECK(!on_segment(raw_pt(1, 2), raw_pt(0, 0), raw_pt(2, 2))); // off the line
}

TEST_CASE("segment intersection classifications") {
    // Disjoint.
    CHECK(classify_segments(raw_pt(0, 0), raw_pt(1, 0), raw_pt(0, 1), raw_pt(1, 1))
        == SegmentRelation::disjoint);
    CHECK(classify_segments(raw_pt(0, 0), raw_pt(1, 0), raw_pt(2, 0), raw_pt(3, 0))
        == SegmentRelation::disjoint); // collinear but separated

    // Proper crossing at an interior point of both.
    CHECK(classify_segments(raw_pt(0, 0), raw_pt(2, 2), raw_pt(0, 2), raw_pt(2, 0))
        == SegmentRelation::proper_crossing);

    // Endpoint touch: shared endpoint, T-junction, collinear end-to-end.
    CHECK(classify_segments(raw_pt(0, 0), raw_pt(1, 0), raw_pt(1, 0), raw_pt(1, 1))
        == SegmentRelation::endpoint_touch);
    CHECK(classify_segments(raw_pt(0, 0), raw_pt(2, 0), raw_pt(1, 0), raw_pt(1, 2))
        == SegmentRelation::endpoint_touch);
    CHECK(classify_segments(raw_pt(0, 0), raw_pt(1, 0), raw_pt(1, 0), raw_pt(2, 0))
        == SegmentRelation::endpoint_touch);

    // Collinear overlap along a subsegment of positive length.
    CHECK(classify_segments(raw_pt(0, 0), raw_pt(2, 0), raw_pt(1, 0), raw_pt(3, 0))
        == SegmentRelation::collinear_overlap);
    CHECK(classify_segments(raw_pt(0, 0), raw_pt(3, 0), raw_pt(1, 0), raw_pt(2, 0))
        == SegmentRelation::collinear_overlap); // fully contained
}

TEST_CASE("signed doubled area sign, winding, and wide magnitude") {
    // Unit-ish square, counterclockwise: positive area 2 (doubled).
    const std::vector<Point> ccw = { raw_pt(0, 0), raw_pt(2, 0), raw_pt(2, 2), raw_pt(0, 2) };
    CHECK(signed_double_area(ccw).sign() == 1);

    std::vector<Point> cw = ccw;
    std::reverse(cw.begin(), cw.end());
    CHECK(signed_double_area(cw).sign() == -1);

    // Degenerate collinear loop: zero.
    const std::vector<Point> flat = { raw_pt(0, 0), raw_pt(1, 0), raw_pt(2, 0) };
    CHECK(signed_double_area(flat).is_zero());

    // Triangle whose doubled area exceeds signed 128-bit range.
    const std::vector<Point> huge = {
        raw_pt(INT64_MIN, INT64_MIN),
        raw_pt(INT64_MAX, INT64_MIN),
        raw_pt(INT64_MIN, INT64_MAX),
    };
    CHECK(signed_double_area(huge).sign() == 1);
    CHECK(!signed_double_area(huge).is_zero());
}

TEST_CASE("exact integer arithmetic building blocks") {
    const Int256 five = Int256::from_i64(5);
    const Int256 seven = Int256::from_i64(7);
    CHECK((five + seven) == Int256::from_i64(12));
    CHECK((five - seven) == Int256::from_i64(-2));
    CHECK((five - seven).sign() == -1);
    CHECK(Int256::from_i64(0).is_zero());
    CHECK(Int256::compare(five, seven) < 0);
    CHECK(Int256::compare(seven, five) > 0);
    CHECK(Int256::compare(five, five) == 0);

    // Product of two ~2^64 magnitudes, exact and signed.
    const __int128 big = static_cast<__int128>(INT64_MAX) - static_cast<__int128>(INT64_MIN);
    const Int256 sq = Int256::multiply(big, big);
    CHECK(sq.sign() == 1);
    CHECK(Int256::multiply(big, -big).sign() == -1);
    CHECK((Int256::multiply(big, big) - sq).is_zero());
}
