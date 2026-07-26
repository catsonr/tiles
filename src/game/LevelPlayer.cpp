#include "game/LevelPlayer.h"

#include "core/geometry/Coordinate.h"
#include "core/geometry/Predicates.h"
#include "core/geometry/Triangle.h"
#include "game/PrototilePreview.h"
#include "game/resources/LevelPersistence.h"

#include <godot_cpp/classes/h_box_container.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/classes/v_box_container.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <cstdint>
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

void LevelPlayer::_bind_methods() {
    godot::ClassDB::bind_method(
        godot::D_METHOD("set_level_path", "path"), &LevelPlayer::set_level_path);
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_level_path"), &LevelPlayer::get_level_path);
    ADD_PROPERTY(
        godot::PropertyInfo(
            godot::Variant::STRING,
            "level_path",
            godot::PROPERTY_HINT_FILE,
            "*.tres"),
        "set_level_path",
        "get_level_path");
}

void LevelPlayer::set_level_path(const godot::String &p_path) {
    level_path_ = p_path;
}

godot::String LevelPlayer::get_level_path() const {
    return level_path_;
}

void LevelPlayer::_ready() {
    status_label_ = godot::Object::cast_to<godot::Label>(get_node_or_null("Status"));
    completion_label_ = godot::Object::cast_to<godot::RichTextLabel>(get_node_or_null("Complete"));
    palette_rows_ = godot::Object::cast_to<godot::Control>(get_node_or_null("Palette/Margin/Rows"));
    if (status_label_ == nullptr || completion_label_ == nullptr || palette_rows_ == nullptr) {
        godot::UtilityFunctions::push_error("[tiles] level player scene is incomplete");
        return;
    }
    completion_label_->set_visible(false);
    set_focus_mode(godot::Control::FOCUS_ALL);
    if (!load()) {
        return;
    }
    build_palette_controls();
    update_canvas_rect();
    update_projection();
    refresh_controls();
    grab_focus();
    queue_redraw();
    godot::UtilityFunctions::print("[tiles] level player ready: ", level_path_);
}

bool LevelPlayer::load() {
    auto catalog = content::make_canonical_prototile_catalog();
    if (!catalog) {
        godot::UtilityFunctions::push_error("[tiles] level player: canonical catalog failed");
        return false;
    }
    catalog_ = std::move(catalog).value();
    auto loaded = load_level_resource(level_path_, catalog_.value());
    if (!loaded) {
        godot::UtilityFunctions::push_error(
            "[tiles] level player: level load failed with typed code ",
            static_cast<std::int64_t>(loaded.error().code));
        return false;
    }
    LoadedLevelResource level = std::move(loaded).value();
    const godot::Ref<PaletteResource> palette_resource = level.resource->get_palette();
    const godot::TypedArray<PaletteEntryResource> entries = palette_resource->get_entries();
    colors_.reserve(static_cast<std::size_t>(entries.size()));
    for (std::int64_t i = 0; i < entries.size(); ++i) {
        const godot::Ref<PaletteEntryResource> entry = entries[i];
        colors_.push_back(entry->get_color());
    }
    session_.emplace(engine::State(std::move(level.compiled.level)));
    selection_ = Selection { 0, 0 };
    rebuild_proposals();
    return true;
}

void LevelPlayer::build_palette_controls() {
    for (EntryControl &entry : entry_controls_) {
        if (entry.preview != nullptr) {
            entry.preview->queue_free();
        }
    }
    entry_controls_.clear();
    const std::vector<engine::PaletteEntry> &entries = session_->state().palette().entries();
    for (std::size_t i = 0; i < entries.size(); ++i) {
        auto *row = memnew(godot::HBoxContainer);
        auto *preview = memnew(PrototilePreview);
        preview->set_custom_minimum_size(godot::Vector2(72.0f, 56.0f));
        preview->set_mouse_filter(godot::Control::MOUSE_FILTER_IGNORE);
        preview->set_polygon(entries[i].orientations().front().canonical_polygon());
        preview->set_fill_color(colors_[i]);
        auto *supply = memnew(godot::Label);
        row->add_child(preview);
        row->add_child(supply);
        palette_rows_->add_child(row);
        entry_controls_.push_back(EntryControl { preview, supply });
    }
}

void LevelPlayer::select_entry(std::size_t p_entry) {
    if (!session_.has_value() || p_entry >= session_->state().palette().entries().size()) {
        return;
    }
    selection_ = Selection { p_entry, 0 };
    rebuild_proposals();
    refresh_controls();
    queue_redraw();
}

void LevelPlayer::cycle_entry(bool p_forward) {
    if (!selection_.has_value()) {
        return;
    }
    const std::size_t count = session_->state().palette().entries().size();
    select_entry(p_forward ? (selection_->entry + 1) % count
                           : (selection_->entry + count - 1) % count);
}

void LevelPlayer::cycle_orientation(bool p_forward) {
    const engine::PaletteEntry *entry = selected_entry();
    if (entry == nullptr) {
        return;
    }
    const std::size_t count = entry->orientations().size();
    selection_->orientation = p_forward ? (selection_->orientation + 1) % count
                                        : (selection_->orientation + count - 1) % count;
    rebuild_proposals();
    refresh_controls();
    queue_redraw();
}

const engine::PaletteEntry *LevelPlayer::selected_entry() const {
    if (!session_.has_value() || !selection_.has_value()) {
        return nullptr;
    }
    const auto &entries = session_->state().palette().entries();
    return selection_->entry < entries.size() ? &entries[selection_->entry] : nullptr;
}

const OrientedPrototile *LevelPlayer::selected_variant() const {
    const engine::PaletteEntry *entry = selected_entry();
    if (entry == nullptr || selection_->orientation >= entry->orientations().size()) {
        return nullptr;
    }
    return &entry->orientations()[selection_->orientation];
}

void LevelPlayer::rebuild_proposals() {
    proposals_.clear();
    active_proposal_.reset();
    const OrientedPrototile *candidate = selected_variant();
    if (candidate == nullptr) {
        return;
    }
    const engine::State &state = session_->state();
    const engine::PaletteEntryIndex entry(selection_->entry);
    const engine::PaletteOrientationIndex orientation(selection_->orientation);
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

bool LevelPlayer::accept_active_proposal() {
    if (!active_proposal_.has_value()) {
        return false;
    }
    const ProposalCommand &command = proposals_[active_proposal_.value()].command;
    const bool applied = std::visit(
        [this](const auto &p_command) { return session_->apply(p_command).has_value(); }, command);
    if (!applied) {
        status_label_->set_text("placement rejected");
        return false;
    }
    refresh_after_mutation();
    return true;
}

std::optional<std::size_t> LevelPlayer::placement_at_local(godot::Vector2 p_local) const {
    const auto &entries = session_->state().arrangement().entries();
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
    const auto index = placement_at_local(p_local);
    if (!index.has_value()) {
        status_label_->set_text("no placed tile is under the pointer");
        return false;
    }
    const PlacementId id = session_->state().arrangement().entries()[index.value()].id;
    if (!session_->apply(engine::RemoveCommand { id })) {
        status_label_->set_text("removal rejected");
        return false;
    }
    refresh_after_mutation();
    return true;
}

bool LevelPlayer::undo() {
    if (!session_->undo()) {
        return false;
    }
    refresh_after_mutation();
    return true;
}

void LevelPlayer::refresh_after_mutation() {
    pointer_.reset();
    rebuild_proposals();
    refresh_controls();
    queue_redraw();
}

void LevelPlayer::set_pointer(godot::Vector2 p_local) {
    pointer_ = p_local;
    if (update_active_proposal()) {
        queue_redraw();
    }
}

void LevelPlayer::refresh_controls() {
    if (!session_.has_value()) {
        return;
    }
    for (std::size_t i = 0; i < entry_controls_.size(); ++i) {
        const auto status = session_->state().supply_status(engine::PaletteEntryIndex(i));
        const bool finite = status->remaining.has_value();
        entry_controls_[i].supply->set_visible(finite);
        if (finite) {
            entry_controls_[i].supply->set_text(
                godot::String("× ") + godot::String::num_uint64(status->remaining.value()));
        }
        if (entry_controls_[i].preview != nullptr) {
            const engine::PaletteEntry &entry = session_->state().palette().entries()[i];
            const std::size_t orientation = i == selection_->entry ? selection_->orientation : 0;
            entry_controls_[i].preview->set_polygon(entry.orientations()[orientation].canonical_polygon());
            entry_controls_[i].preview->set_fill_color(colors_[i]);
            entry_controls_[i].preview->set_modulate(
                i == selection_->entry ? godot::Color(1, 1, 1, 1) : godot::Color(0.62f, 0.62f, 0.62f, 1));
        }
    }
    completion_label_->set_visible(session_->state().solved());
    status_label_->set_text(session_->state().solved()
                                ? "complete :)"
                                : "tab: tile | r: rotate | click: place | right click: remove | ctrl-z: undo");
}

void LevelPlayer::update_canvas_rect() {
    const godot::Vector2 size = get_size();
    canvas_rect_ = godot::Rect2(
        godot::Vector2(260.0f, 0.0f),
        godot::Vector2(std::max(0.0f, size.x - 260.0f), std::max(0.0f, size.y - 44.0f)));
}

void LevelPlayer::update_projection() {
    if (!session_.has_value()) {
        return;
    }
    const Polygon &outer = session_->state().region().outer_boundary();
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
    const Region &region = session_->state().region();
    draw_polygon(region.outer_boundary(), TARGET_FILL, OUTLINE_WIDTH);
    for (const Polygon &hole : region.inner_boundaries()) {
        draw_polygon(hole, CANVAS_BACKGROUND, OUTLINE_WIDTH);
    }
}

void LevelPlayer::draw_arrangement() {
    for (const Entry &entry : session_->state().arrangement().entries()) {
        godot::Color fill(0.8f, 0.8f, 0.8f, 1.0f);
        for (std::size_t i = 0; i < session_->state().palette().entries().size(); ++i) {
            if (session_->state().palette().entries()[i].prototile().id() == entry.placement.prototile().id()) {
                fill = colors_[i];
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
    godot::Color color = colors_[selection_->entry];
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
    draw_rect(canvas_rect_, CANVAS_BACKGROUND);
    if (!session_.has_value()) {
        return;
    }
    draw_region();
    draw_arrangement();
    draw_ghost();
}

void LevelPlayer::_gui_input(const godot::Ref<godot::InputEvent> &p_event) {
    const godot::Ref<godot::InputEventKey> key = p_event;
    if (key.is_valid() && key->is_pressed() && !key->is_echo()) {
        if (key->is_ctrl_pressed() && key->get_keycode() == godot::KEY_Z) {
            undo();
        } else if (key->get_keycode() == godot::KEY_TAB) {
            cycle_entry(!key->is_shift_pressed());
        } else if (key->get_keycode() == godot::KEY_R) {
            cycle_orientation(!key->is_shift_pressed());
        }
        return;
    }
    const godot::Ref<godot::InputEventMouseMotion> motion = p_event;
    if (motion.is_valid()) {
        if (canvas_rect_.has_point(motion->get_position())) {
            set_pointer(motion->get_position());
        }
        return;
    }
    const godot::Ref<godot::InputEventMouseButton> button = p_event;
    if (!button.is_valid() || !button->is_pressed() || !canvas_rect_.has_point(button->get_position())) {
        return;
    }
    grab_focus();
    if (button->get_button_index() == godot::MOUSE_BUTTON_LEFT) {
        accept_active_proposal();
    } else if (button->get_button_index() == godot::MOUSE_BUTTON_RIGHT) {
        remove_at_local(button->get_position());
    }
}

bool LevelPlayer::initialized() const { return session_.has_value(); }
const engine::Session *LevelPlayer::session() const { return session_.has_value() ? &session_.value() : nullptr; }
std::optional<LevelPlayer::Selection> LevelPlayer::selection() const { return selection_; }
const std::vector<LevelPlayer::Proposal> &LevelPlayer::proposals() const { return proposals_; }
std::optional<std::size_t> LevelPlayer::active_proposal() const { return active_proposal_; }
std::optional<godot::Color> LevelPlayer::entry_color(std::size_t p_entry) const { return p_entry < colors_.size() ? std::optional<godot::Color>(colors_[p_entry]) : std::nullopt; }
const PrototilePreview *LevelPlayer::entry_preview(std::size_t p_entry) const {
    return p_entry < entry_controls_.size() ? entry_controls_[p_entry].preview : nullptr;
}
bool LevelPlayer::entry_supply_visible(std::size_t p_entry) const {
    return p_entry < entry_controls_.size() && entry_controls_[p_entry].supply->is_visible();
}
bool LevelPlayer::completion_visible() const { return session_.has_value() && session_->state().solved(); }
godot::Rect2 LevelPlayer::canvas_rect() const { return canvas_rect_; }

} // namespace tiles::game
