#include "game/Editor.h"

#include "core/Arrangement.h"
#include "core/OrientedPrototile.h"
#include "core/Placement.h"
#include "core/Prototile.h"
#include "core/geometry/Coordinate.h"
#include "core/geometry/Point.h"
#include "core/geometry/Polygon.h"
#include "engine/Commands.h"
#include "engine/TetrominoState.h"

#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include <cstddef>
#include <cstdint>
#include <utility>
#include <variant>

namespace tiles::game {

namespace {

// --- debug layout ---

// Cells are exactly four game units square. Every tier-1 tetromino orientation
// has an axis-aligned bounding box no larger than that, so one orientation fits
// per cell. Neighbouring closed cells share boundaries, which is why adjacent
// placements may legally touch along complete edges, partial edges, or points.
constexpr std::int64_t CELL_UNITS = 4;

// The debug column order i, o, t, s, z, j, l, resolved against the handcrafted
// palette's authored order o, i, t, s, z, j, l. The label exists only to make
// bootstrap diagnostics readable.
struct DebugColumn final {
    std::size_t palette_entry;
    const char *label;
};

constexpr DebugColumn DEBUG_COLUMNS[] = {
    { 1, "i" },
    { 0, "o" },
    { 2, "t" },
    { 3, "s" },
    { 4, "z" },
    { 5, "j" },
    { 6, "l" },
};

constexpr std::size_t DEBUG_COLUMN_COUNT = sizeof(DEBUG_COLUMNS) / sizeof(DEBUG_COLUMNS[0]);

// 2 + 1 + 4 + 2 + 2 + 4 + 4 distinct orientations across the seven tetrominoes.
constexpr std::size_t EXPECTED_GRID_PLACEMENTS = 19;

// The grid plus the single accepted overlap-fixture o.
constexpr std::size_t EXPECTED_TOTAL_PLACEMENTS = 20;

// The overlap fixture sits below the tallest debug column, which reaches y = -16.
constexpr std::int64_t FIXTURE_ORIGIN_Y_UNITS = -22;

// --- model to screen projection ---

// One fixed debug scale and origin. The specified 7 x 4 cell grid spans model
// x in [0, 28] and the fixture reaches y = -22, so at 24 pixels per unit the
// whole surface lands inside a default project window with no dynamic fitting,
// zoom, pan, or camera state.
constexpr double PIXELS_PER_UNIT = 24.0;
constexpr double SCREEN_ORIGIN_X = 60.0;
constexpr double SCREEN_ORIGIN_Y = 40.0;

double to_real(Coordinate p_coordinate) {
    return static_cast<double>(p_coordinate.raw()) / static_cast<double>(Coordinate::SCALE);
}

// The one-way presentation projection. Model y is negated because cartesian y
// points up while Godot canvas y points down. This conversion is lossy by
// design and has no inverse: no value produced here may re-enter the model.
godot::Vector2 to_screen(Point p_point) {
    const double screen_x = SCREEN_ORIGIN_X + PIXELS_PER_UNIT * to_real(p_point.x);
    const double screen_y = SCREEN_ORIGIN_Y - PIXELS_PER_UNIT * to_real(p_point.y);
    return godot::Vector2(static_cast<real_t>(screen_x), static_cast<real_t>(screen_y));
}

// --- exact cell arithmetic ---

// Build a whole-game-unit coordinate exactly from a raw multiple of SCALE.
// Nothing here quantizes a double, rounds, or introduces a grid to the engine.
constexpr Coordinate game_units(std::int64_t p_units) {
    return Coordinate::from_raw(p_units * Coordinate::SCALE);
}

struct Bounds final {
    Coordinate min_x;
    Coordinate min_y;
    Coordinate max_x;
    Coordinate max_y;
};

// A canonical polygon's first vertex is its lexicographically smallest, which is
// not promised to be any particular corner of its bounding box, so the extent is
// measured across every vertex.
Bounds bounds_of(const Polygon &p_polygon) {
    const Polygon::Vertices &vertices = p_polygon.vertices();
    Bounds bounds {
        vertices.front().x,
        vertices.front().y,
        vertices.front().x,
        vertices.front().y,
    };
    for (const Point &vertex : vertices) {
        if (vertex.x < bounds.min_x) {
            bounds.min_x = vertex.x;
        }
        if (bounds.max_x < vertex.x) {
            bounds.max_x = vertex.x;
        }
        if (vertex.y < bounds.min_y) {
            bounds.min_y = vertex.y;
        }
        if (bounds.max_y < vertex.y) {
            bounds.max_y = vertex.y;
        }
    }
    return bounds;
}

// --- debug colors ---

// Color is selected by exact prototile identity, so every orientation and every
// placement of one mino shares one color. Channel values are debug presentation,
// not domain state.
godot::Color color_for(PrototileId p_id) {
    switch (p_id.value()) {
        case 1:
            return godot::Color(0.95f, 0.85f, 0.20f); // o: yellow
        case 2:
            return godot::Color(0.20f, 0.80f, 0.92f); // i: cyan
        case 3:
            return godot::Color(0.66f, 0.35f, 0.86f); // t: purple
        case 4:
            return godot::Color(0.28f, 0.78f, 0.34f); // s: green
        case 5:
            return godot::Color(0.89f, 0.24f, 0.24f); // z: red
        case 6:
            return godot::Color(0.24f, 0.44f, 0.92f); // j: blue
        case 7:
            return godot::Color(0.96f, 0.55f, 0.14f); // l: orange
        default:
            return godot::Color(1.0f, 0.0f, 1.0f); // conspicuous magenta
    }
}

// --- diagnostic rendering of typed errors ---
//
// These translate typed values into text at the presentation boundary only. No
// engine or core interface is flattened: the typed alternatives are inspected
// directly, and nothing produced here flows back down.

const char *describe(engine::CandidateError p_error) {
    switch (p_error) {
        case engine::CandidateError::palette_entry_out_of_range:
            return "CandidateError::palette_entry_out_of_range";
        case engine::CandidateError::orientation_out_of_range:
            return "CandidateError::orientation_out_of_range";
        case engine::CandidateError::supply_exhausted:
            return "CandidateError::supply_exhausted";
    }
    return "CandidateError::<unknown>";
}

const char *describe(PlacementError p_error) {
    switch (p_error) {
        case PlacementError::footprint_overflow:
            return "PlacementError::footprint_overflow";
        case PlacementError::footprint_construction_failed:
            return "PlacementError::footprint_construction_failed";
    }
    return "PlacementError::<unknown>";
}

const char *describe(ArrangementErrorCode p_code) {
    switch (p_code) {
        case ArrangementErrorCode::interior_overlap:
            return "ArrangementErrorCode::interior_overlap";
        case ArrangementErrorCode::identifier_exhausted:
            return "ArrangementErrorCode::identifier_exhausted";
    }
    return "ArrangementErrorCode::<unknown>";
}

const char *describe_place_error(const engine::PlaceCommandError &p_error) {
    if (const auto *candidate = std::get_if<engine::CandidateError>(&p_error)) {
        return describe(*candidate);
    }
    if (const auto *placement = std::get_if<PlacementError>(&p_error)) {
        return describe(*placement);
    }
    if (const auto *arrangement = std::get_if<ArrangementError>(&p_error)) {
        return describe(arrangement->code);
    }
    return "PlaceCommandError::<unknown alternative>";
}

const char *describe(engine::TetrominoStateStage p_stage) {
    switch (p_stage) {
        case engine::TetrominoStateStage::polygon:
            return "TetrominoStateStage::polygon";
        case engine::TetrominoStateStage::prototile:
            return "TetrominoStateStage::prototile";
        case engine::TetrominoStateStage::palette_entry:
            return "TetrominoStateStage::palette_entry";
        case engine::TetrominoStateStage::palette:
            return "TetrominoStateStage::palette";
    }
    return "TetrominoStateStage::<unknown>";
}

void report_place_failure(
    const char *p_operation,
    const char *p_label,
    std::size_t p_entry,
    std::size_t p_orientation,
    const engine::PlaceCommandError &p_error) {
    godot::UtilityFunctions::push_error(
        "[tiles] ", p_operation, " failed: mino ", p_label,
        ", palette entry ", static_cast<std::int64_t>(p_entry),
        ", orientation ", static_cast<std::int64_t>(p_orientation),
        ", error ", describe_place_error(p_error));
}

} // namespace

void Editor::_bind_methods() {}

void Editor::_ready() {
    auto state = engine::make_tetromino_state();
    if (!state) {
        const engine::TetrominoStateError &error = state.error();
        godot::UtilityFunctions::push_error(
            "[tiles] bootstrap failed: make_tetromino_state at ",
            describe(error.stage),
            error.prototile_id.has_value() ? ", prototile id " : ", no prototile id",
            error.prototile_id.has_value()
                ? static_cast<std::int64_t>(error.prototile_id.value().value())
                : static_cast<std::int64_t>(0));
        queue_redraw();
        return;
    }

    state_ = std::move(state).value();
    godot::UtilityFunctions::print(
        "[tiles] handcrafted tetromino state constructed: palette order ",
        static_cast<std::int64_t>(state_->palette().order()));

    if (!place_orientation_grid()) {
        queue_redraw();
        return;
    }

    if (!place_overlap_fixture()) {
        queue_redraw();
        return;
    }

    godot::UtilityFunctions::print(
        "[tiles] bootstrap complete: ",
        static_cast<std::int64_t>(state_->arrangement().entries().size()),
        " authoritative placements available to draw");

    queue_redraw();
}

bool Editor::place_orientation_grid() {
    engine::State &state = *state_;
    std::size_t placed = 0;

    for (std::size_t column = 0; column < DEBUG_COLUMN_COUNT; ++column) {
        const DebugColumn &spec = DEBUG_COLUMNS[column];

        if (spec.palette_entry >= state.palette().entries().size()) {
            godot::UtilityFunctions::push_error(
                "[tiles] debug grid failed: mino ", spec.label,
                " names palette entry ", static_cast<std::int64_t>(spec.palette_entry),
                " but the palette holds only ",
                static_cast<std::int64_t>(state.palette().entries().size()));
            return false;
        }

        // Distinct compiled orientations only. equivalent_orientations() records
        // which requested angles collapsed onto one boundary and is never
        // iterated as though its labels were separate drawable geometry.
        const std::vector<OrientedPrototile> &orientations =
            state.palette().entries()[spec.palette_entry].orientations();

        for (std::size_t row = 0; row < orientations.size(); ++row) {
            const Bounds bounds = bounds_of(orientations[row].canonical_polygon());

            auto width = checked_subtract(bounds.max_x, bounds.min_x);
            auto height = checked_subtract(bounds.max_y, bounds.min_y);
            if (!width || !height) {
                godot::UtilityFunctions::push_error(
                    "[tiles] debug grid failed: mino ", spec.label,
                    ", orientation ", static_cast<std::int64_t>(row),
                    ", extent arithmetic overflowed");
                return false;
            }

            if (width.value().raw() > CELL_UNITS * Coordinate::SCALE
                || height.value().raw() > CELL_UNITS * Coordinate::SCALE) {
                godot::UtilityFunctions::push_error(
                    "[tiles] debug grid failed: mino ", spec.label,
                    ", orientation ", static_cast<std::int64_t>(row),
                    ", exceeds its ", static_cast<std::int64_t>(CELL_UNITS),
                    " game-unit cell");
                return false;
            }

            // Align the candidate's bounding-box top-left corner to the cell's,
            // since its canonical origin is not promised to be any given corner.
            const Coordinate cell_min_x =
                game_units(CELL_UNITS * static_cast<std::int64_t>(column));
            const Coordinate cell_max_y =
                game_units(-CELL_UNITS * static_cast<std::int64_t>(row));

            auto translation_x = checked_subtract(cell_min_x, bounds.min_x);
            auto translation_y = checked_subtract(cell_max_y, bounds.max_y);
            if (!translation_x || !translation_y) {
                godot::UtilityFunctions::push_error(
                    "[tiles] debug grid failed: mino ", spec.label,
                    ", orientation ", static_cast<std::int64_t>(row),
                    ", cell translation overflowed");
                return false;
            }

            const engine::PlaceCommand command {
                engine::PaletteEntryIndex(spec.palette_entry),
                engine::PaletteOrientationIndex(row),
                Point { translation_x.value(), translation_y.value() },
            };

            auto result = state.apply(command);
            if (!result) {
                report_place_failure(
                    "debug grid placement", spec.label, spec.palette_entry, row, result.error());
                return false;
            }

            ++placed;
        }
    }

    if (placed != EXPECTED_GRID_PLACEMENTS) {
        godot::UtilityFunctions::push_error(
            "[tiles] debug grid failed: expected ",
            static_cast<std::int64_t>(EXPECTED_GRID_PLACEMENTS),
            " distinct orientations but placed ", static_cast<std::int64_t>(placed));
        return false;
    }

    godot::UtilityFunctions::print(
        "[tiles] debug grid: ", static_cast<std::int64_t>(placed),
        " distinct palette orientations placed across ",
        static_cast<std::int64_t>(DEBUG_COLUMN_COUNT), " columns");
    return true;
}

bool Editor::place_overlap_fixture() {
    engine::State &state = *state_;

    // Palette entry zero is o; its single distinct orientation is index zero.
    const engine::PaletteEntryIndex fixture_entry(0);
    const engine::PaletteOrientationIndex fixture_orientation(0);

    const Point first_translation { game_units(0), game_units(FIXTURE_ORIGIN_Y_UNITS) };

    auto first = state.apply(
        engine::PlaceCommand { fixture_entry, fixture_orientation, first_translation });
    if (!first) {
        report_place_failure("overlap fixture (first o)", "o", 0, 0, first.error());
        return false;
    }

    const PlacementId first_id = first.value();

    const std::size_t entries_before = state.arrangement().entries().size();
    const std::optional<PlacementId> next_id_before = state.arrangement().next_id();

    // Exactly one game unit to the right, so the two 2 x 2 squares share a
    // 1 x 2 region of positive interior area.
    auto shifted_x = checked_add(first_translation.x, game_units(1));
    if (!shifted_x) {
        godot::UtilityFunctions::push_error(
            "[tiles] overlap fixture failed: shifted translation overflowed");
        return false;
    }
    const Point second_translation { shifted_x.value(), first_translation.y };

    auto second = state.apply(
        engine::PlaceCommand { fixture_entry, fixture_orientation, second_translation });

    if (second) {
        godot::UtilityFunctions::push_error(
            "[tiles] overlap fixture failed: the overlapping o was accepted as placement ",
            static_cast<std::int64_t>(second.value().value()));
        return false;
    }

    const auto *arrangement_error = std::get_if<ArrangementError>(&second.error());
    if (arrangement_error == nullptr) {
        godot::UtilityFunctions::push_error(
            "[tiles] overlap fixture failed: expected an ArrangementError but got ",
            describe_place_error(second.error()));
        return false;
    }

    if (arrangement_error->code != ArrangementErrorCode::interior_overlap) {
        godot::UtilityFunctions::push_error(
            "[tiles] overlap fixture failed: expected interior_overlap but got ",
            describe(arrangement_error->code));
        return false;
    }

    if (!arrangement_error->conflicting_placement.has_value()) {
        godot::UtilityFunctions::push_error(
            "[tiles] overlap fixture failed: interior_overlap carried no conflicting placement");
        return false;
    }

    if (arrangement_error->conflicting_placement.value() != first_id) {
        godot::UtilityFunctions::push_error(
            "[tiles] overlap fixture failed: conflict names placement ",
            static_cast<std::int64_t>(
                arrangement_error->conflicting_placement.value().value()),
            " but the fixture o is placement ",
            static_cast<std::int64_t>(first_id.value()));
        return false;
    }

    // The expected rejection is successful act behavior, so it is reported
    // informationally rather than as an error.
    godot::UtilityFunctions::print(
        "[tiles] overlap fixture: second o rejected with interior_overlap against placement ",
        static_cast<std::int64_t>(first_id.value()), " (expected)");

    if (state.arrangement().entries().size() != entries_before) {
        godot::UtilityFunctions::push_error(
            "[tiles] overlap fixture failed: rejected command changed the entry count from ",
            static_cast<std::int64_t>(entries_before), " to ",
            static_cast<std::int64_t>(state.arrangement().entries().size()));
        return false;
    }

    if (state.arrangement().next_id() != next_id_before) {
        godot::UtilityFunctions::push_error(
            "[tiles] overlap fixture failed: rejected command consumed an identifier");
        return false;
    }

    bool first_present = false;
    bool second_present = false;
    for (const Entry &entry : state.arrangement().entries()) {
        if (entry.id == first_id) {
            first_present = true;
        }
        if (entry.placement.translation() == second_translation) {
            second_present = true;
        }
    }

    if (!first_present) {
        godot::UtilityFunctions::push_error(
            "[tiles] overlap fixture failed: the accepted o is no longer in the arrangement");
        return false;
    }

    if (second_present) {
        godot::UtilityFunctions::push_error(
            "[tiles] overlap fixture failed: the rejected o entered the arrangement");
        return false;
    }

    if (state.arrangement().entries().size() != EXPECTED_TOTAL_PLACEMENTS) {
        godot::UtilityFunctions::push_error(
            "[tiles] overlap fixture failed: expected ",
            static_cast<std::int64_t>(EXPECTED_TOTAL_PLACEMENTS),
            " total placements but found ",
            static_cast<std::int64_t>(state.arrangement().entries().size()));
        return false;
    }

    godot::UtilityFunctions::print(
        "[tiles] overlap fixture: entry count and next_id unchanged by the rejection");
    return true;
}

void Editor::_draw() {
    if (!state_.has_value()) {
        return;
    }

    const Arrangement &arrangement = state_->arrangement();

    for (const Entry &entry : arrangement.entries()) {
        // The authoritative footprint boundary, in cyclic order. Godot closes the
        // polygon implicitly, so the first vertex is not repeated. The general
        // ordered-boundary contract covers convex and concave alike; nothing here
        // branches on shape, tier, or triangulation.
        const Polygon::Vertices &vertices = entry.placement.footprint().vertices();

        godot::PackedVector2Array points;
        for (const Point &vertex : vertices) {
            points.push_back(to_screen(vertex));
        }

        draw_colored_polygon(points, color_for(entry.placement.prototile().id()));
    }

    if (!reported_first_draw_) {
        reported_first_draw_ = true;
        godot::UtilityFunctions::print(
            "[tiles] first draw: ", static_cast<std::int64_t>(arrangement.entries().size()),
            " footprints projected");
    }
}

} // namespace tiles::game
