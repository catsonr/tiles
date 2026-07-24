#include "game/Editor.h"

#include "core/Arrangement.h"
#include "core/OrientedPrototile.h"
#include "core/Placement.h"
#include "core/Prototile.h"
#include "core/geometry/Coordinate.h"
#include "core/geometry/Point.h"
#include "core/geometry/Polygon.h"
#include "core/geometry/Predicates.h"
#include "engine/Commands.h"
#include "engine/TetrominoState.h"

#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/rect2.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

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

// The whole ordered boundary of a placement, projected once.
godot::PackedVector2Array project_footprint(const Polygon &p_polygon) {
    godot::PackedVector2Array points;
    for (const Point &vertex : p_polygon.vertices()) {
        points.push_back(to_screen(vertex));
    }
    return points;
}

// --- proposal presentation ---

// A translucent ghost reads as a proposal rather than a placed tile.
constexpr float GHOST_ALPHA = 0.45f;

// The temporary selected-tile cell, in screen coordinates. It sits before the
// arrangement's x origin at 60, so it cannot cover the debug grid.
constexpr double PREVIEW_CELL_MIN = 8.0;
constexpr double PREVIEW_CELL_MAX = 52.0;
constexpr double PREVIEW_CELL_MARGIN = 4.0;

// One presentation-only handle per proposal: the arithmetic mean of its
// projected footprint vertices. Deliberately not the area centroid — for one
// fixed selected orientation the local mean offset is constant, so distinct
// exact translations produce correspondingly translated handles, which is all
// cursor ranking needs. A validated polygon has at least three vertices, so the
// division is always defined. Nothing computed here re-enters the model.
godot::Vector2 proposal_handle(const Placement &p_placement) {
    const Polygon::Vertices &vertices = p_placement.footprint().vertices();
    double sum_x = 0.0;
    double sum_y = 0.0;
    for (const Point &vertex : vertices) {
        const godot::Vector2 projected = to_screen(vertex);
        sum_x += static_cast<double>(projected.x);
        sum_y += static_cast<double>(projected.y);
    }
    const double count = static_cast<double>(vertices.size());
    return godot::Vector2(
        static_cast<real_t>(sum_x / count), static_cast<real_t>(sum_y / count));
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

// Whether two closed bounding boxes meet. Non-strict, because touching extents
// are exactly the interesting case here.
bool bounds_touch(const Bounds &p_lhs, const Bounds &p_rhs) {
    return p_lhs.min_x <= p_rhs.max_x && p_rhs.min_x <= p_lhs.max_x
        && p_lhs.min_y <= p_rhs.max_y && p_rhs.min_y <= p_lhs.max_y;
}

// --- contact policy ---

// Whether a proposed footprint meets any placed footprint along a shared
// segment of positive length rather than only at isolated points.
//
// Isolated point contact is perfectly legal geometry, and the core and engine
// still derive and admit it; it is simply not useful for building a tiling, so
// the editor does not offer it as a placement option. This is presentation
// policy over already-proven placements, and it is decided exactly by the core
// segment predicate — never by a projected distance, a pixel, or a tolerance.
bool shares_edge_contact(const Polygon &p_footprint, const Arrangement &p_arrangement) {
    const Polygon::Vertices &candidate = p_footprint.vertices();
    const Bounds candidate_bounds = bounds_of(p_footprint);

    for (const Entry &entry : p_arrangement.entries()) {
        // Closed bounding boxes that do not meet cannot share a single point,
        // let alone a segment. This is an exact conservative skip on the same
        // lattice values, not an approximation: it only ever avoids work.
        const Bounds placed_bounds = bounds_of(entry.placement.footprint());
        if (!bounds_touch(candidate_bounds, placed_bounds)) {
            continue;
        }

        const Polygon::Vertices &placed = entry.placement.footprint().vertices();
        for (std::size_t i = 0; i < candidate.size(); ++i) {
            const Point &a = candidate[i];
            const Point &b = candidate[(i + 1) % candidate.size()];
            for (std::size_t j = 0; j < placed.size(); ++j) {
                const Point &c = placed[j];
                const Point &d = placed[(j + 1) % placed.size()];
                if (classify_segments(a, b, c, d) == SegmentRelation::collinear_overlap) {
                    return true;
                }
            }
        }
    }
    return false;
}

// --- debug colors ---

// Color is selected by exact prototile identity, so every orientation and every
// placement of one mino shares one color. Channel values are debug presentation,
// not domain state.
// Outline width and the shared boundary style. Every drawn tile is outlined in
// a darkened form of its own fill, so edges read individually where identical
// colors meet without introducing a second palette to maintain.
constexpr float OUTLINE_WIDTH = 2.0f;
constexpr float OUTLINE_SHADE = 0.5f;

godot::Color outline_for(const godot::Color &p_fill) {
    return godot::Color(
        p_fill.r * OUTLINE_SHADE,
        p_fill.g * OUTLINE_SHADE,
        p_fill.b * OUTLINE_SHADE,
        p_fill.a);
}

// Godot closes a filled polygon implicitly but draws a polyline open, so the
// outline repeats the first vertex and nothing else does.
godot::PackedVector2Array closed_boundary(const godot::PackedVector2Array &p_points) {
    godot::PackedVector2Array closed = p_points;
    if (!p_points.is_empty()) {
        closed.push_back(p_points[0]);
    }
    return closed;
}

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

const char *describe(JoinErrorCode p_code) {
    switch (p_code) {
        case JoinErrorCode::anchor_not_found:
            return "JoinErrorCode::anchor_not_found";
        case JoinErrorCode::anchor_edge_out_of_range:
            return "JoinErrorCode::anchor_edge_out_of_range";
        case JoinErrorCode::candidate_edge_out_of_range:
            return "JoinErrorCode::candidate_edge_out_of_range";
        case JoinErrorCode::anchor_vertex_out_of_range:
            return "JoinErrorCode::anchor_vertex_out_of_range";
        case JoinErrorCode::candidate_vertex_out_of_range:
            return "JoinErrorCode::candidate_vertex_out_of_range";
        case JoinErrorCode::incompatible_edges:
            return "JoinErrorCode::incompatible_edges";
        case JoinErrorCode::translation_overflow:
            return "JoinErrorCode::translation_overflow";
        case JoinErrorCode::footprint_overflow:
            return "JoinErrorCode::footprint_overflow";
        case JoinErrorCode::footprint_construction_failed:
            return "JoinErrorCode::footprint_construction_failed";
        case JoinErrorCode::interior_overlap:
            return "JoinErrorCode::interior_overlap";
        case JoinErrorCode::identifier_exhausted:
            return "JoinErrorCode::identifier_exhausted";
    }
    return "JoinErrorCode::<unknown>";
}

// A mating failure reaching this point is unexpected: every proposal in the
// cache was proven by an authoritative preview, and nothing mutates the state
// between that proof and the click. It is reported in full rather than asserted
// away, and never crashes.
void report_mate_failure(
    const char *p_operation, const engine::MateCommandError &p_error) {
    if (const auto *candidate = std::get_if<engine::CandidateError>(&p_error)) {
        godot::UtilityFunctions::push_error(
            "[tiles] ", p_operation, " failed: ", describe(*candidate));
        return;
    }
    if (const auto *join = std::get_if<JoinError>(&p_error)) {
        if (join->conflicting_placement.has_value()) {
            godot::UtilityFunctions::push_error(
                "[tiles] ", p_operation, " failed: ", describe(join->code),
                ", conflicting placement ",
                static_cast<std::int64_t>(join->conflicting_placement.value().value()));
            return;
        }
        godot::UtilityFunctions::push_error(
            "[tiles] ", p_operation, " failed: ", describe(join->code));
        return;
    }
    godot::UtilityFunctions::push_error(
        "[tiles] ", p_operation, " failed: MateCommandError::<unknown alternative>");
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

    // Interaction comes up only once the complete bootstrap has succeeded. Every
    // palette is nonempty and every entry's distinct-orientation vector is
    // nonempty by construction, so entry zero, orientation zero is always valid.
    selection_ = Selection {
        engine::PaletteEntryIndex(0),
        engine::PaletteOrientationIndex(0),
    };
    report_selection();

    // The root control fills the viewport but does not accept keyboard focus by
    // default, and the selection keys are read through _gui_input.
    set_focus_mode(godot::Control::FOCUS_ALL);
    grab_focus();
    mouse_position_ = get_local_mouse_position();

    const RebuildCounts counts = rebuild_proposals();
    report_rebuild("initial", counts);

    // An integration check, not a promised count: this specific twenty-anchor
    // debug fixture with this specific initial selection must offer somewhere to
    // join, or exhaustive enumeration is not reaching the core at all.
    if (proposals_.empty()) {
        godot::UtilityFunctions::push_error(
            "[tiles] integration check failed: the debug arrangement and the initial "
            "selection produced no join proposal from ",
            static_cast<std::int64_t>(counts.full_edge_attempted),
            " full-edge and ",
            static_cast<std::int64_t>(counts.vertex_attempted),
            " vertex commands");
        queue_redraw();
        return;
    }

    update_active_proposal();
    if (active_proposal_.has_value()) {
        const godot::Vector2 handle =
            proposal_handle(proposals_[active_proposal_.value()].placement);
        godot::UtilityFunctions::print(
            "[tiles] active proposal: index ",
            static_cast<std::int64_t>(active_proposal_.value()), " of ",
            static_cast<std::int64_t>(proposals_.size()), ", handle (",
            static_cast<double>(handle.x), ", ", static_cast<double>(handle.y),
            "), cursor (", static_cast<double>(mouse_position_.value().x), ", ",
            static_cast<double>(mouse_position_.value().y), ")");
    }

    queue_redraw();
}

const OrientedPrototile *Editor::selected_candidate() const {
    if (!state_.has_value() || !selection_.has_value()) {
        return nullptr;
    }

    // The selection is guarded, not trusted: every index is range-checked before
    // it indexes anything, exactly as the engine's own resolution does.
    const std::vector<engine::PaletteEntry> &entries = state_->palette().entries();
    if (selection_->entry.value() >= entries.size()) {
        return nullptr;
    }
    const std::vector<OrientedPrototile> &orientations =
        entries[selection_->entry.value()].orientations();
    if (selection_->orientation.value() >= orientations.size()) {
        return nullptr;
    }
    return &orientations[selection_->orientation.value()];
}

void Editor::report_selection() const {
    const OrientedPrototile *candidate = selected_candidate();
    if (!selection_.has_value() || !state_.has_value() || candidate == nullptr) {
        return;
    }

    // The truthful denominator is always orientations().size(): the count of
    // distinct compiled geometry, never the number of authored angles.
    const std::size_t orientation_count =
        state_->palette().entries()[selection_->entry.value()].orientations().size();
    godot::UtilityFunctions::print(
        "[tiles] selection: palette entry ",
        static_cast<std::int64_t>(selection_->entry.value()), " (prototile id ",
        static_cast<std::int64_t>(candidate->prototile().id().value()),
        "), orientation ", static_cast<std::int64_t>(selection_->orientation.value()),
        " of ", static_cast<std::int64_t>(orientation_count));
}

void Editor::report_rebuild(const char *p_reason, const RebuildCounts &p_counts) const {
    const std::size_t succeeded = p_counts.full_edge_succeeded + p_counts.vertex_succeeded;
    godot::UtilityFunctions::print(
        "[tiles] proposals (", p_reason, "): full-edge ",
        static_cast<std::int64_t>(p_counts.full_edge_attempted), " attempted / ",
        static_cast<std::int64_t>(p_counts.full_edge_succeeded), " succeeded, vertex ",
        static_cast<std::int64_t>(p_counts.vertex_attempted), " attempted / ",
        static_cast<std::int64_t>(p_counts.vertex_succeeded), " succeeded, ",
        static_cast<std::int64_t>(p_counts.distinct), " distinct after collapsing ",
        static_cast<std::int64_t>(succeeded - p_counts.distinct),
        " duplicate proofs, ", static_cast<std::int64_t>(p_counts.point_contact_only),
        " dropped as point-only contact, ", static_cast<std::int64_t>(p_counts.offered),
        " offered");
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

Editor::RebuildCounts Editor::rebuild_proposals() {
    RebuildCounts counts;
    proposals_.clear();
    active_proposal_.reset();

    const OrientedPrototile *candidate = selected_candidate();
    if (!state_.has_value() || !selection_.has_value() || candidate == nullptr) {
        return counts;
    }

    const engine::State &state = *state_;
    const Selection selection = selection_.value();

    // Every polygon edge is the cyclic edge beginning at one stored vertex, so a
    // polygon has exactly as many edge indices as vertices. Nothing here assumes
    // a tetromino vertex count, an edge length, or an orientation count: the
    // bounds come from the actual candidate and each actual anchor footprint.
    const std::size_t candidate_features =
        candidate->canonical_polygon().vertices().size();

    std::vector<JoinProposal> proven;

    // Full-edge proofs first, then vertex proofs, and within each family the
    // arrangement's storage order — which is monotonic PlacementId order, since
    // entries are only ever appended and ids are allocated monotonically.
    // Enumerating in exactly the specified precedence means the first surviving
    // proof of a physical placement is already its representative.
    for (const Entry &entry : state.arrangement().entries()) {
        const std::size_t anchor_features =
            entry.placement.footprint().vertices().size();
        for (std::size_t anchor_edge = 0; anchor_edge < anchor_features; ++anchor_edge) {
            for (std::size_t edge = 0; edge < candidate_features; ++edge) {
                const engine::MateFullEdgesCommand command {
                    entry.id,
                    EdgeIndex(anchor_edge),
                    selection.entry,
                    selection.orientation,
                    EdgeIndex(edge),
                };
                ++counts.full_edge_attempted;

                // Authoritative preview decides availability. Incompatible
                // edges, overlap, exhausted supply and every other typed failure
                // means this exact feature pair is simply not on offer right
                // now; it is ordinary, expected, and never logged individually.
                auto previewed = state.preview(command);
                if (!previewed) {
                    continue;
                }
                ++counts.full_edge_succeeded;
                proven.push_back(
                    JoinProposal { JoinCommand(command), std::move(previewed).value() });
            }
        }
    }

    for (const Entry &entry : state.arrangement().entries()) {
        const std::size_t anchor_features =
            entry.placement.footprint().vertices().size();
        for (std::size_t anchor_vertex = 0; anchor_vertex < anchor_features; ++anchor_vertex) {
            for (std::size_t vertex = 0; vertex < candidate_features; ++vertex) {
                const engine::MateVerticesCommand command {
                    entry.id,
                    VertexIndex(anchor_vertex),
                    selection.entry,
                    selection.orientation,
                    VertexIndex(vertex),
                };
                ++counts.vertex_attempted;

                auto previewed = state.preview(command);
                if (!previewed) {
                    continue;
                }
                ++counts.vertex_succeeded;
                proven.push_back(
                    JoinProposal { JoinCommand(command), std::move(previewed).value() });
            }
        }
    }

    // One selection fixes prototile identity and distinct orientation for this
    // whole rebuild, so two successes are the same physical proposal exactly
    // when their exact translations are equal — compared on the q16.48 lattice,
    // never on a projected float. This discards duplicate proofs of one
    // placement, never a distinct placement option: point-only contact, vertex
    // derivation, off-screen position, distance from the cursor, and concave
    // footprints are all irrelevant here.
    std::vector<JoinProposal> distinct;
    for (JoinProposal &proposal : proven) {
        bool duplicate = false;
        for (const JoinProposal &kept : distinct) {
            if (kept.placement.translation() == proposal.placement.translation()) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            distinct.push_back(std::move(proposal));
        }
    }
    counts.distinct = distinct.size();

    // Only placements that actually abut something are offered. The contact test
    // depends on the placement alone, so applying it after deduplication rather
    // than before is equivalent and far cheaper: every proof of one translation
    // shares one footprint and would reach the same verdict.
    for (JoinProposal &proposal : distinct) {
        if (shares_edge_contact(proposal.placement.footprint(), state.arrangement())) {
            proposals_.push_back(std::move(proposal));
        } else {
            ++counts.point_contact_only;
        }
    }
    counts.offered = proposals_.size();

    // Exact lexicographic translation order. Representative-command precedence
    // settles which proof survives; this settles the cache order, and with it
    // every presentation-distance tie, without treating a float as identity.
    std::sort(
        proposals_.begin(),
        proposals_.end(),
        [](const JoinProposal &p_lhs, const JoinProposal &p_rhs) {
            const Point lhs = p_lhs.placement.translation();
            const Point rhs = p_rhs.placement.translation();
            if (lhs.x != rhs.x) {
                return lhs.x < rhs.x;
            }
            return lhs.y < rhs.y;
        });

    return counts;
}

bool Editor::update_active_proposal() {
    const std::optional<std::size_t> previous = active_proposal_;

    std::optional<std::size_t> nearest;
    if (mouse_position_.has_value()) {
        const godot::Vector2 mouse = mouse_position_.value();
        double best = 0.0;
        for (std::size_t i = 0; i < proposals_.size(); ++i) {
            const godot::Vector2 handle = proposal_handle(proposals_[i].placement);
            const double dx = static_cast<double>(handle.x) - static_cast<double>(mouse.x);
            const double dy = static_cast<double>(handle.y) - static_cast<double>(mouse.y);
            const double distance = dx * dx + dy * dy;
            // Strictly nearer only, so an exact floating-point tie keeps the
            // lower cache index and is decided by exact translation order.
            if (!nearest.has_value() || distance < best) {
                nearest = i;
                best = distance;
            }
        }
    }

    active_proposal_ = nearest;
    return active_proposal_ != previous;
}

void Editor::refresh_for_selection() {
    report_selection();
    const RebuildCounts counts = rebuild_proposals();
    report_rebuild("selection changed", counts);
    update_active_proposal();
    queue_redraw();
}

void Editor::cycle_entry(bool p_forward) {
    if (!state_.has_value() || !selection_.has_value()) {
        return;
    }

    const std::size_t count = state_->palette().entries().size();
    if (count == 0) {
        return;
    }

    // Wraps in both directions. Changing entries resets the orientation, because
    // an orientation index is only meaningful within one entry's distinct set.
    const std::size_t current =
        selection_->entry.value() < count ? selection_->entry.value() : 0;
    const std::size_t next =
        p_forward ? (current + 1) % count : (current + count - 1) % count;

    selection_ = Selection {
        engine::PaletteEntryIndex(next),
        engine::PaletteOrientationIndex(0),
    };
    refresh_for_selection();
}

void Editor::cycle_orientation(bool p_forward) {
    if (!state_.has_value() || !selection_.has_value()) {
        return;
    }

    const std::vector<engine::PaletteEntry> &entries = state_->palette().entries();
    if (selection_->entry.value() >= entries.size()) {
        return;
    }

    // Only the selected entry's distinct compiled orientations. A one-orientation
    // prototile therefore stays put, and equivalent_orientations() — which
    // records which requested angles collapsed onto one boundary — is never
    // iterated as though its labels were separate geometry.
    const std::size_t count = entries[selection_->entry.value()].orientations().size();
    if (count == 0) {
        return;
    }

    const std::size_t current =
        selection_->orientation.value() < count ? selection_->orientation.value() : 0;
    const std::size_t next =
        p_forward ? (current + 1) % count : (current + count - 1) % count;

    selection_ = Selection { selection_->entry, engine::PaletteOrientationIndex(next) };
    refresh_for_selection();
}

void Editor::apply_active_proposal() {
    if (!state_.has_value() || !selection_.has_value() || !active_proposal_.has_value()) {
        return;
    }
    if (active_proposal_.value() >= proposals_.size()) {
        return;
    }

    // Copied out before anything is applied: insertion may reallocate the
    // proposal cache's contents through the rebuild below, and a command is a
    // small value anyway.
    const JoinCommand command = proposals_[active_proposal_.value()].command;
    engine::State &state = *state_;

    if (const auto *full_edge = std::get_if<engine::MateFullEdgesCommand>(&command)) {
        auto applied = state.apply(*full_edge);
        if (applied) {
            godot::UtilityFunctions::print(
                "[tiles] placed: full-edge mating added placement ",
                static_cast<std::int64_t>(applied.value().value()));
        } else {
            report_mate_failure("full-edge mating", applied.error());
        }
    } else if (const auto *vertices = std::get_if<engine::MateVerticesCommand>(&command)) {
        auto applied = state.apply(*vertices);
        if (applied) {
            godot::UtilityFunctions::print(
                "[tiles] placed: vertex mating added placement ",
                static_cast<std::int64_t>(applied.value().value()));
        } else {
            report_mate_failure("vertex mating", applied.error());
        }
    }

    // The selection is deliberately preserved, so one oriented prototile can be
    // placed repeatedly. Rebuild either way: on success against the mutated
    // arrangement, on failure against the unchanged one.
    const RebuildCounts counts = rebuild_proposals();
    report_rebuild("after placement", counts);
    update_active_proposal();
    queue_redraw();
}

void Editor::_gui_input(const godot::Ref<godot::InputEvent> &p_event) {
    if (p_event.is_null()) {
        return;
    }

    const godot::Ref<godot::InputEventKey> key = p_event;
    if (key.is_valid()) {
        // Presses only: releases and auto-repeat are not selection changes.
        if (!key->is_pressed() || key->is_echo()) {
            return;
        }
        const godot::Key code = key->get_keycode();
        const bool backward = key->is_shift_pressed();
        if (code == godot::KEY_TAB) {
            cycle_entry(!backward);
            accept_event();
            return;
        }
        if (code == godot::KEY_R) {
            cycle_orientation(!backward);
            accept_event();
            return;
        }
        return;
    }

    const godot::Ref<godot::InputEventMouseMotion> motion = p_event;
    if (motion.is_valid()) {
        // Ranking only. Motion never reruns core or engine preview, and the raw
        // cursor position is not itself rendered, so a redraw is requested only
        // when the choice among cached proposals actually changes.
        mouse_position_ = motion->get_position();
        if (update_active_proposal()) {
            queue_redraw();
        }
        return;
    }

    const godot::Ref<godot::InputEventMouseButton> button = p_event;
    if (button.is_valid()) {
        if (!button->is_pressed() || button->get_button_index() != godot::MOUSE_BUTTON_LEFT) {
            return;
        }
        // The proposal already highlighted as the ghost is the one applied, so a
        // click adds exactly what was shown; the click's own position then takes
        // over ranking for the rebuilt cache.
        const godot::Vector2 position = button->get_position();
        apply_active_proposal();
        mouse_position_ = position;
        if (update_active_proposal()) {
            queue_redraw();
        }
        accept_event();
        return;
    }
}

void Editor::draw_selection_preview() {
    const OrientedPrototile *candidate = selected_candidate();
    if (candidate == nullptr) {
        return;
    }

    const double side = PREVIEW_CELL_MAX - PREVIEW_CELL_MIN;
    draw_rect(
        godot::Rect2(
            godot::Vector2(
                static_cast<real_t>(PREVIEW_CELL_MIN),
                static_cast<real_t>(PREVIEW_CELL_MIN)),
            godot::Vector2(static_cast<real_t>(side), static_cast<real_t>(side))),
        godot::Color(0.12f, 0.12f, 0.14f));

    // Measured across every vertex: a canonical polygon begins at its
    // lexicographically smallest vertex, which is not promised to be a corner of
    // its bounding box.
    const Polygon &polygon = candidate->canonical_polygon();
    const Bounds bounds = bounds_of(polygon);
    const double min_x = to_real(bounds.min_x);
    const double max_x = to_real(bounds.max_x);
    const double min_y = to_real(bounds.min_y);
    const double max_y = to_real(bounds.max_y);
    const double width = max_x - min_x;
    const double height = max_y - min_y;
    if (width <= 0.0 || height <= 0.0) {
        return;
    }

    // One uniform scale preserves the oriented shape, and the same cartesian-y
    // to screen-y flip the world projection uses keeps it the right way up. This
    // local fit is not a second world projection: no value it produces is ever
    // compared against, or allowed to become, a model coordinate.
    const double inner = side - 2.0 * PREVIEW_CELL_MARGIN;
    const double scale = std::min(inner / width, inner / height);
    const double center_x = 0.5 * (min_x + max_x);
    const double center_y = 0.5 * (min_y + max_y);
    const double cell_center = 0.5 * (PREVIEW_CELL_MIN + PREVIEW_CELL_MAX);

    godot::PackedVector2Array points;
    for (const Point &vertex : polygon.vertices()) {
        const double screen_x = cell_center + scale * (to_real(vertex.x) - center_x);
        const double screen_y = cell_center - scale * (to_real(vertex.y) - center_y);
        points.push_back(
            godot::Vector2(static_cast<real_t>(screen_x), static_cast<real_t>(screen_y)));
    }

    const godot::Color fill = color_for(candidate->prototile().id());
    draw_colored_polygon(points, fill);
    draw_polyline(closed_boundary(points), outline_for(fill), OUTLINE_WIDTH);
}

void Editor::draw_active_ghost() {
    if (!active_proposal_.has_value() || active_proposal_.value() >= proposals_.size()) {
        return;
    }

    // Authoritative preview geometry, projected exactly like a placed footprint.
    // It is never a cursor-translated polygon: it jumps between exact
    // feature-derived placements as the active proposal changes.
    const Placement &placement = proposals_[active_proposal_.value()].placement;
    const godot::PackedVector2Array points = project_footprint(placement.footprint());
    if (points.is_empty()) {
        return;
    }

    godot::Color fill = color_for(placement.prototile().id());
    fill.a = GHOST_ALPHA;
    draw_colored_polygon(points, fill);

    // The ghost keeps a white outline rather than its own darkened one, so the
    // active proposal stays distinguishable now that every placed tile is
    // outlined too.
    draw_polyline(
        closed_boundary(points), godot::Color(1.0f, 1.0f, 1.0f, 0.85f), OUTLINE_WIDTH);
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
        const godot::PackedVector2Array points =
            project_footprint(entry.placement.footprint());

        const godot::Color fill = color_for(entry.placement.prototile().id());
        draw_colored_polygon(points, fill);
        draw_polyline(closed_boundary(points), outline_for(fill), OUTLINE_WIDTH);
    }

    // At most one ghost, over the arrangement and under the selection cell. The
    // other cached proposals stay available to ranking without being drawn.
    const bool ghost_drawn =
        active_proposal_.has_value() && active_proposal_.value() < proposals_.size();
    draw_active_ghost();

    const bool preview_drawn = selected_candidate() != nullptr;
    draw_selection_preview();

    if (!reported_first_draw_) {
        reported_first_draw_ = true;
        godot::UtilityFunctions::print(
            "[tiles] first draw: ", static_cast<std::int64_t>(arrangement.entries().size()),
            " footprints projected, selection preview ",
            preview_drawn ? "projected" : "absent", ", active ghost ",
            ghost_drawn ? "projected" : "absent");
    }
}

} // namespace tiles::game
