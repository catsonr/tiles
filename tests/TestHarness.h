#pragma once

#include "core/geometry/Point.h"

#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace tiles_test {

// A tiny dependency-free test registry. TEST_CASE registers a function via a
// static registrar; main() in TestHarness.cpp runs them and reports.
struct Registry {
    struct Case {
        std::string name;
        std::function<void()> fn;
    };

    static std::vector<Case> &cases();
    static int &checks();
    static int &failures();
};

struct Registrar {
    Registrar(const char *p_name, std::function<void()> p_fn);
};

// Point built directly from raw lattice units. Predicate and polygon behavior
// depends only on relative geometry, so small raw values are convenient and
// exact.
inline tiles::Point raw_pt(std::int64_t p_x, std::int64_t p_y) {
    return tiles::Point {
        tiles::Coordinate::from_raw(p_x),
        tiles::Coordinate::from_raw(p_y),
    };
}

} // namespace tiles_test

#define TILES_CONCAT_INNER(a, b) a##b
#define TILES_CONCAT(a, b) TILES_CONCAT_INNER(a, b)

#define TEST_CASE(name)                                                              \
    static void TILES_CONCAT(tiles_test_fn_, __LINE__)();                            \
    static ::tiles_test::Registrar TILES_CONCAT(tiles_test_reg_, __LINE__)(          \
        name, &TILES_CONCAT(tiles_test_fn_, __LINE__));                              \
    static void TILES_CONCAT(tiles_test_fn_, __LINE__)()

#define CHECK(cond)                                                                  \
    do {                                                                             \
        ++::tiles_test::Registry::checks();                                          \
        if (!(cond)) {                                                               \
            ++::tiles_test::Registry::failures();                                    \
            std::printf("  FAIL %s:%d  CHECK(%s)\n", __FILE__, __LINE__, #cond);     \
        }                                                                            \
    } while (0)
