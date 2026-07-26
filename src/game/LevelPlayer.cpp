#include "game/LevelPlayer.h"

#include "core/geometry/Coordinate.h"
#include "core/geometry/Predicates.h"
#include "core/geometry/Triangle.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <utility>

namespace tiles::game {

namespace {

const godot::Color CANVAS_BACKGROUND(0.12f, 0.13f, 0.16f, 1.0f);
const godot::Color TARGET_FILL(0.23f, 0.25f, 0.30f, 1.0f);
const godot::Color TARGET_OUTLINE(0.72f, 0.75f, 0.80f, 1.0f);
const godot::Color GHOST_OUTLINE(1.0f, 1.0f, 1.0f, 0.9f);

constexpr double CANVAS_MARGIN = 28.0;
constexpr float OUTLINE_WIDTH = 2.0f;
constexpr float GHOST_ALPHA = 0.45f;

const char *CONTROL_HINT =
    "tab: tile | r: rotate | click: place | right click: remove | ctrl-z: undo";
const char *COMPLETE_STATUS = "complete! (scroll down for next problem)";

double to_real(Coordinate p_coordinate) {
    return static_cast<double>(p_coordinate.raw()) / static_cast<double>(Coordinate::SCALE);
}

godot::PackedVector2Array closed(godot::PackedVector2Array p_points) {
    if (!p_points.is_empty()) {
        p_points.push_back(p_points[0]);
    }
    return p_points;
}

// Whether a proposed footprint meets any placed footprint along a shared length
// of boundary. Touching at a single point is not contact: it leaves the two
// tiles with nothing to reason from.
bool shares_edge_contact(const Polygon &p_candidate, const Arrangement &p_arrangement) {
    const Polygon::Vertices &candidate = p_candidate.vertices();
    for (const Entry &anchor : p_arrangement.entries()) {
        const Polygon::Vertices &placed = anchor.placement.footprint().vertices();
        for (std::size_t i = 0; i < candidate.size(); ++i) {
            for (std::size_t j = 0; j < placed.size(); ++j) {
                if (classify_segments(
                        candidate[i], candidate[(i + 1) % candidate.size()], placed[j],
                        placed[(j + 1) % placed.size()])
                    == SegmentRelation::collinear_overlap) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool contains_screen_polygon(const godot::PackedVector2Array &p_points, godot::Vector2 p_query) {
    bool inside = false;
    const std::int64_t count = p_points.size();
    for (std::int64_t i = 0, j = count - 1; i < count; j = i++) {
        const godot::Vector2 a = p_points[i];
        const godot::Vector2 b = p_points[j];
        const bool straddles = (a.y > p_query.y) != (b.y > p_query.y);
        if (!straddles) {
            continue;
        }
        const double x = static_cast<double>(a.x)
            + (static_cast<double>(p_query.y) - static_cast<double>(a.y))
                * (static_cast<double>(b.x) - static_cast<double>(a.x))
                / (static_cast<double>(b.y) - static_cast<double>(a.y));
        if (static_cast<double>(p_query.x) < x) {
            inside = !inside;
        }
    }
    return inside;
}

} // namespace

// Semantic results, not raw input. Every one of these is emitted by the
// operation which already knows it succeeded and already knows whether anything
// observable changed. A consumer may ignore all of them and the game plays
// identically.
void LevelPlayer::_bind_methods() {
    ADD_SIGNAL(godot::MethodInfo(
        "palette_selected", godot::PropertyInfo(godot::Variant::INT, "entry")));
    ADD_SIGNAL(godot::MethodInfo(
        "orientation_changed",
        godot::PropertyInfo(godot::Variant::INT, "entry"),
        godot::PropertyInfo(godot::Variant::INT, "orientation")));
    ADD_SIGNAL(godot::MethodInfo(
        "placement_succeeded", godot::PropertyInfo(godot::Variant::INT, "entry")));
    ADD_SIGNAL(godot::MethodInfo("removal_succeeded"));
    ADD_SIGNAL(godot::MethodInfo("undo_succeeded"));
    ADD_SIGNAL(godot::MethodInfo(
        "active_proposal_changed",
        godot::PropertyInfo(godot::Variant::BOOL, "present"),
        godot::PropertyInfo(godot::Variant::INT, "proposal")));
    ADD_SIGNAL(godot::MethodInfo("victory_reached"));
}

godot::Color LevelPlayer::region_fill_color() {
    return TARGET_FILL;
}

void LevelPlayer::_ready() {
    number_label_ = godot::Object::cast_to<godot::Label>(get_node_or_null("Number"));
    completion_label_ = godot::Object::cast_to<godot::RichTextLabel>(get_node_or_null("Complete"));
    if (number_label_ == nullptr || completion_label_ == nullptr) {
        godot::UtilityFunctions::push_error("[tiles] problem canvas scene is incomplete");
        return;
    }
    completion_label_->set_visible(false);
    // The canvas is moved, created, and destroyed around navigation, so it is
    // never an input target. The fixed shell routes play to whichever canvas is
    // current.
    set_mouse_filter(godot::Control::MOUSE_FILTER_IGNORE);
    set_clip_contents(true);
}

void LevelPlayer::_notification(int p_what) {
    if (p_what == godot::Control::NOTIFICATION_RESIZED) {
        update_canvas_rect();
        update_projection();
        queue_redraw();
    }
}

void LevelPlayer::bind(
    ProblemState &p_state, std::int64_t p_problem_number, ProblemStateObserver *p_observer) {
    state_ = &p_state;
    observer_ = p_observer;
    problem_number_ = p_problem_number;
    refusal_ = godot::String();
    pointer_.reset();
    // Completion is read from the bound state rather than assumed absent, so a
    // solved problem returns solved and cannot emit victory a second time.
    solved_ = state_->session().state().solved();
    if (number_label_ != nullptr) {
        number_label_->set_text(
            godot::String("problem ") + godot::String::num_int64(problem_number_));
    }
    update_canvas_rect();
    update_projection();
    rebuild_proposals();
    refresh_completion();
    queue_redraw();
}

bool LevelPlayer::bound() const {
    return state_ != nullptr;
}

std::int64_t LevelPlayer::problem_number() const {
    return problem_number_;
}

const ProblemState *LevelPlayer::state() const {
    return state_;
}

void LevelPlayer::select_entry(std::size_t p_entry) {
    if (state_ == nullptr
        || p_entry >= state_->session().state().palette().entries().size()) {
        return;
    }
    const GhostIdentity before = ghost_identity();
    const bool changed = !state_->selection().has_value()
        || state_->selection()->entry != p_entry;
    state_->set_selection(Selection { p_entry, 0 });
    refusal_ = godot::String();
    rebuild_proposals();
    queue_redraw();
    notify_shell();
    if (changed) {
        emit_signal(
            godot::StringName("palette_selected"), static_cast<std::int64_t>(p_entry));
    }
    emit_ghost_change(before);
}

void LevelPlayer::cycle_entry(bool p_forward) {
    if (state_ == nullptr || !state_->selection().has_value()) {
        return;
    }
    const std::size_t count = state_->session().state().palette().entries().size();
    const std::size_t entry = state_->selection()->entry;
    select_entry(p_forward ? (entry + 1) % count : (entry + count - 1) % count);
}

void LevelPlayer::cycle_orientation(bool p_forward) {
    const engine::PaletteEntry *entry = selected_entry();
    if (entry == nullptr) {
        return;
    }
    const GhostIdentity before = ghost_identity();
    const Selection selection = state_->selection().value();
    const std::size_t count = entry->orientations().size();
    const std::size_t orientation = p_forward ? (selection.orientation + 1) % count
                                              : (selection.orientation + count - 1) % count;
    const bool changed = orientation != selection.orientation;
    state_->set_selection(Selection { selection.entry, orientation });
    refusal_ = godot::String();
    rebuild_proposals();
    queue_redraw();
    notify_shell();
    if (changed) {
        emit_signal(
            godot::StringName("orientation_changed"),
            static_cast<std::int64_t>(selection.entry),
            static_cast<std::int64_t>(orientation));
    }
    emit_ghost_change(before);
}

const engine::PaletteEntry *LevelPlayer::selected_entry() const {
    if (state_ == nullptr || !state_->selection().has_value()) {
        return nullptr;
    }
    const auto &entries = state_->session().state().palette().entries();
    const std::size_t entry = state_->selection()->entry;
    return entry < entries.size() ? &entries[entry] : nullptr;
}

const OrientedPrototile *LevelPlayer::selected_variant() const {
    const engine::PaletteEntry *entry = selected_entry();
    if (entry == nullptr
        || state_->selection()->orientation >= entry->orientations().size()) {
        return nullptr;
    }
    return &entry->orientations()[state_->selection()->orientation];
}

void LevelPlayer::rebuild_proposals() {
    proposals_.clear();
    active_proposal_.reset();
    const OrientedPrototile *candidate = selected_variant();
    if (candidate == nullptr) {
        return;
    }
    const engine::State &state = state_->session().state();
    const engine::PaletteEntryIndex entry(state_->selection()->entry);
    const engine::PaletteOrientationIndex orientation(state_->selection()->orientation);
    const auto supply = state.supply_status(entry);
    if (supply.has_value() && supply->remaining.has_value() && supply->remaining.value() == 0) {
        return;
    }
    // Every candidate position is named the same way, whether the arrangement is
    // empty or not: bring one candidate polygon vertex onto one anchor vertex.
    // The anchors are every placed footprint vertex together with every region
    // boundary vertex.
    //
    // The region is what makes this complete. It guarantees an opening move
    // exists — a convex region corner can only be covered by a tile carrying a
    // vertex exactly there — so nothing here needs a seed special case. It is
    // also the only thing that can pin a placement whose contact with the
    // arrangement is a partial edge sharing no vertex, which is a legal exact
    // contact PlaceCommand admits and neither mating command can derive.
    std::vector<Point> anchors;
    for (const Entry &placed : state.arrangement().entries()) {
        for (const Point &vertex : placed.placement.footprint().vertices()) {
            anchors.push_back(vertex);
        }
    }
    for (const Point &vertex : state.region().outer_boundary().vertices()) {
        anchors.push_back(vertex);
    }
    for (const Polygon &hole : state.region().inner_boundaries()) {
        for (const Point &vertex : hole.vertices()) {
            anchors.push_back(vertex);
        }
    }

    // The opening move is free, and every later move must join what is already
    // down. This is a deliberate rule of play rather than a legality claim: the
    // engine would admit a second disconnected island, and offering one turns a
    // level into independent local fills. Committing to one growing frontier is
    // where a level's difficulty lives.
    //
    // It defers moves, and never loses them. Every authored solution stays fully
    // reachable from every legal opening placement, so no opening choice can
    // strand part of a level — the cost of the rule is that a separated limb
    // waits until the frontier arrives, not that it becomes unplayable.
    const bool arrangement_empty = state.arrangement().entries().empty();
    for (const Point &anchor : anchors) {
        for (const Point &local : candidate->canonical_polygon().vertices()) {
            // The translation is derived by exact subtraction of two already
            // quantized points, so nothing is rounded or reconstructed here.
            auto x = checked_subtract(anchor.x, local.x);
            auto y = checked_subtract(anchor.y, local.y);
            if (!x || !y) {
                continue;
            }
            engine::PlaceCommand command { entry, orientation, Point { x.value(), y.value() } };
            auto preview = state.preview(command);
            if (!preview) {
                continue;
            }
            if (!arrangement_empty
                && !shares_edge_contact(preview.value().footprint(), state.arrangement())) {
                continue;
            }
            // One selection fixes identity and orientation for this whole
            // rebuild, so two proofs describe the same physical proposal exactly
            // when their exact translations agree.
            bool duplicate = false;
            for (const Proposal &kept : proposals_) {
                if (kept.placement.translation() == command.translation) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) {
                proposals_.push_back(Proposal { command, std::move(preview).value() });
            }
        }
    }
    update_active_proposal();
}

bool LevelPlayer::update_active_proposal() {
    std::optional<std::size_t> selected;
    if (!proposals_.empty()) {
        if (!pointer_.has_value()) {
            selected = 0;
        } else {
            // A candidate the pointer is actually inside always outranks one that
            // is merely near it, and centroid distance only breaks ties within a
            // rank. Candidates spread across the region rather than stacking on
            // one spot, so this keeps selection direct however many there are:
            // point at where the tile should go.
            bool best_covers = false;
            double best = 0.0;
            for (std::size_t i = 0; i < proposals_.size(); ++i) {
                const auto &vertices = proposals_[i].placement.footprint().vertices();
                godot::PackedVector2Array points;
                double x = 0.0;
                double y = 0.0;
                for (const Point &vertex : vertices) {
                    const godot::Vector2 point = project(vertex);
                    points.push_back(point);
                    x += point.x;
                    y += point.y;
                }
                const bool covers = contains_screen_polygon(points, pointer_.value());
                const double dx = x / vertices.size() - pointer_->x;
                const double dy = y / vertices.size() - pointer_->y;
                const double distance = dx * dx + dy * dy;
                if (!selected.has_value() || (covers && !best_covers)
                    || (covers == best_covers && distance < best)) {
                    selected = i;
                    best = distance;
                    best_covers = covers;
                }
            }
        }
    }
    const bool changed = selected != active_proposal_;
    active_proposal_ = selected;
    return changed;
}

LevelPlayer::GhostIdentity LevelPlayer::ghost_identity() const {
    if (!active_proposal_.has_value()) {
        return GhostIdentity {};
    }
    GhostIdentity identity;
    identity.present = true;
    identity.translation = proposals_[active_proposal_.value()].placement.translation();
    return identity;
}

void LevelPlayer::emit_ghost_change(const GhostIdentity &p_before) {
    const GhostIdentity after = ghost_identity();
    if (after.present == p_before.present
        && (!after.present || after.translation == p_before.translation)) {
        return;
    }
    emit_signal(
        godot::StringName("active_proposal_changed"),
        after.present,
        after.present ? static_cast<std::int64_t>(active_proposal_.value()) : std::int64_t(-1));
}

// Victory is the unsolved-to-solved transition of the bound exact state, and
// nothing else. Returning to an unsolved state through removal or undo arms it
// again.
void LevelPlayer::emit_victory_if_reached() {
    const bool solved = state_->session().state().solved();
    const bool reached = solved && !solved_;
    solved_ = solved;
    if (reached) {
        emit_signal(godot::StringName("victory_reached"));
    }
}

bool LevelPlayer::accept_active_proposal() {
    if (state_ == nullptr || !active_proposal_.has_value()) {
        return false;
    }
    const GhostIdentity before = ghost_identity();
    const std::size_t entry = state_->selection()->entry;
    const ProposalCommand &command = proposals_[active_proposal_.value()].command;
    const bool applied = std::visit(
        [this](const auto &p_command) { return state_->session().apply(p_command).has_value(); },
        command);
    if (!applied) {
        refusal_ = "placement rejected";
        notify_shell();
        return false;
    }
    refresh_after_mutation();
    emit_signal(godot::StringName("placement_succeeded"), static_cast<std::int64_t>(entry));
    emit_ghost_change(before);
    emit_victory_if_reached();
    return true;
}

std::optional<std::size_t> LevelPlayer::placement_at_local(godot::Vector2 p_local) const {
    const auto &entries = state_->session().state().arrangement().entries();
    for (std::size_t i = entries.size(); i > 0; --i) {
        godot::PackedVector2Array points;
        for (const Point &vertex : entries[i - 1].placement.footprint().vertices()) {
            points.push_back(project(vertex));
        }
        if (contains_screen_polygon(points, p_local)) {
            return i - 1;
        }
    }
    return std::nullopt;
}

bool LevelPlayer::remove_at_local(godot::Vector2 p_local) {
    if (state_ == nullptr) {
        return false;
    }
    const GhostIdentity before = ghost_identity();
    const auto index = placement_at_local(p_local);
    if (!index.has_value()) {
        refusal_ = "no placed tile is under the pointer";
        notify_shell();
        return false;
    }
    const PlacementId id = state_->session().state().arrangement().entries()[index.value()].id;
    if (!state_->session().apply(engine::RemoveCommand { id })) {
        refusal_ = "removal rejected";
        notify_shell();
        return false;
    }
    refresh_after_mutation();
    emit_signal(godot::StringName("removal_succeeded"));
    emit_ghost_change(before);
    emit_victory_if_reached();
    return true;
}

bool LevelPlayer::undo() {
    if (state_ == nullptr) {
        return false;
    }
    const GhostIdentity before = ghost_identity();
    if (!state_->session().undo()) {
        return false;
    }
    refresh_after_mutation();
    emit_signal(godot::StringName("undo_succeeded"));
    emit_ghost_change(before);
    emit_victory_if_reached();
    return true;
}

void LevelPlayer::refresh_after_mutation() {
    refusal_ = godot::String();
    pointer_.reset();
    rebuild_proposals();
    refresh_completion();
    queue_redraw();
    notify_shell();
}

void LevelPlayer::set_pointer(godot::Vector2 p_local) {
    if (state_ == nullptr) {
        return;
    }
    const GhostIdentity before = ghost_identity();
    pointer_ = p_local;
    if (update_active_proposal()) {
        queue_redraw();
    }
    emit_ghost_change(before);
}

void LevelPlayer::refresh_completion() {
    if (completion_label_ != nullptr) {
        completion_label_->set_visible(state_ != nullptr && state_->session().state().solved());
    }
}

void LevelPlayer::notify_shell() {
    if (observer_ != nullptr) {
        observer_->on_problem_presentation_changed();
    }
}

godot::String LevelPlayer::status_text() const {
    if (!refusal_.is_empty()) {
        return refusal_;
    }
    if (state_ != nullptr && state_->session().state().solved()) {
        return COMPLETE_STATUS;
    }
    return CONTROL_HINT;
}

void LevelPlayer::update_canvas_rect() {
    canvas_rect_ = godot::Rect2(godot::Vector2(0.0f, 0.0f), get_size());
}

void LevelPlayer::update_projection() {
    if (state_ == nullptr) {
        return;
    }
    const Polygon &outer = state_->session().state().region().outer_boundary();
    double min_x = to_real(outer.vertices().front().x);
    double max_x = min_x;
    double min_y = to_real(outer.vertices().front().y);
    double max_y = min_y;
    for (const Point &point : outer.vertices()) {
        min_x = std::min(min_x, to_real(point.x));
        max_x = std::max(max_x, to_real(point.x));
        min_y = std::min(min_y, to_real(point.y));
        max_y = std::max(max_y, to_real(point.y));
    }
    const double width = std::max(max_x - min_x, 1.0);
    const double height = std::max(max_y - min_y, 1.0);
    const double available_x = std::max(1.0, static_cast<double>(canvas_rect_.size.x) - 2.0 * CANVAS_MARGIN);
    const double available_y = std::max(1.0, static_cast<double>(canvas_rect_.size.y) - 2.0 * CANVAS_MARGIN);
    pixels_per_unit_ = std::min(available_x / width, available_y / height);
    projection_origin_ = canvas_rect_.get_center() - godot::Vector2(
        static_cast<real_t>(pixels_per_unit_ * 0.5 * (min_x + max_x)),
        static_cast<real_t>(-pixels_per_unit_ * 0.5 * (min_y + max_y)));
}

godot::Vector2 LevelPlayer::project(double p_x, double p_y) const {
    return projection_origin_ + godot::Vector2(
        static_cast<real_t>(pixels_per_unit_ * p_x), static_cast<real_t>(-pixels_per_unit_ * p_y));
}

godot::Vector2 LevelPlayer::project(Point p_point) const {
    return project(to_real(p_point.x), to_real(p_point.y));
}

void LevelPlayer::draw_polygon(const Polygon &p_polygon, godot::Color p_fill, float p_outline) {
    for (const Triangle &triangle : p_polygon.triangulation()) {
        godot::PackedVector2Array points;
        for (const Point &vertex : triangle.vertices) {
            points.push_back(project(vertex));
        }
        draw_colored_polygon(points, p_fill);
    }
    godot::PackedVector2Array boundary;
    for (const Point &vertex : p_polygon.vertices()) {
        boundary.push_back(project(vertex));
    }
    draw_polyline(closed(boundary), TARGET_OUTLINE, p_outline);
}

void LevelPlayer::draw_region() {
    const Region &region = state_->session().state().region();
    draw_polygon(region.outer_boundary(), TARGET_FILL, OUTLINE_WIDTH);
    for (const Polygon &hole : region.inner_boundaries()) {
        draw_polygon(hole, CANVAS_BACKGROUND, OUTLINE_WIDTH);
    }
}

void LevelPlayer::draw_arrangement() {
    const engine::State &state = state_->session().state();
    const std::vector<godot::Color> &colors = state_->colors();
    for (const Entry &entry : state.arrangement().entries()) {
        godot::Color fill(0.8f, 0.8f, 0.8f, 1.0f);
        for (std::size_t i = 0; i < state.palette().entries().size() && i < colors.size(); ++i) {
            if (state.palette().entries()[i].prototile().id() == entry.placement.prototile().id()) {
                fill = colors[i];
                break;
            }
        }
        draw_polygon(entry.placement.footprint(), fill, OUTLINE_WIDTH);
    }
}

void LevelPlayer::draw_ghost() {
    if (!active_proposal_.has_value()) {
        return;
    }
    godot::Color color = state_->colors()[state_->selection()->entry];
    color.a = GHOST_ALPHA;
    const Polygon &polygon = proposals_[active_proposal_.value()].placement.footprint();
    for (const Triangle &triangle : polygon.triangulation()) {
        godot::PackedVector2Array points;
        for (const Point &vertex : triangle.vertices) {
            points.push_back(project(vertex));
        }
        draw_colored_polygon(points, color);
    }
    godot::PackedVector2Array boundary;
    for (const Point &vertex : polygon.vertices()) {
        boundary.push_back(project(vertex));
    }
    draw_polyline(closed(boundary), GHOST_OUTLINE, OUTLINE_WIDTH);
}

void LevelPlayer::_draw() {
    update_canvas_rect();
    update_projection();
    // The canvas is opaque over its whole rect. Two of them are edge-adjacent
    // while a problem transition runs, so nothing behind them is ever visible
    // between them.
    draw_rect(canvas_rect_, CANVAS_BACKGROUND);
    if (state_ == nullptr) {
        return;
    }
    draw_region();
    draw_arrangement();
    draw_ghost();
}

const engine::Session *LevelPlayer::session() const {
    return state_ != nullptr ? &state_->session() : nullptr;
}

std::optional<LevelPlayer::Selection> LevelPlayer::selection() const {
    return state_ != nullptr ? state_->selection() : std::nullopt;
}

const std::vector<LevelPlayer::Proposal> &LevelPlayer::proposals() const {
    return proposals_;
}

std::optional<std::size_t> LevelPlayer::active_proposal() const {
    return active_proposal_;
}

bool LevelPlayer::completion_visible() const {
    return completion_label_ != nullptr && completion_label_->is_visible();
}

const godot::Label *LevelPlayer::number_label() const {
    return number_label_;
}

const godot::RichTextLabel *LevelPlayer::completion_label() const {
    return completion_label_;
}

godot::Rect2 LevelPlayer::canvas_rect() const {
    return canvas_rect_;
}

} // namespace tiles::game
