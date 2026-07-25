#include "game/LevelPlayer.h"

#include "core/geometry/Coordinate.h"
#include "core/geometry/Predicates.h"
#include "core/geometry/Triangle.h"
#include "game/PrototilePreview.h"
#include "game/resources/LevelPersistence.h"

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/color_rect.hpp>
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

} // namespace

void LevelPlayer::_bind_methods() {
    godot::ClassDB::bind_method(
        godot::D_METHOD("set_level_path", "path"), &LevelPlayer::set_level_path);
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_level_path"), &LevelPlayer::get_level_path);
    godot::ClassDB::bind_method(
        godot::D_METHOD("on_entry_pressed", "entry"), &LevelPlayer::on_entry_pressed);
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
    completion_label_ = godot::Object::cast_to<godot::Label>(get_node_or_null("Complete"));
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
        preview->set_custom_minimum_size(godot::Vector2(42.0f, 34.0f));
        preview->set_polygon(entries[i].orientations().front().canonical_polygon());
        auto *button = memnew(godot::Button);
        button->set_text(godot::String("tile ") + godot::String::num_int64(i + 1));
        button->connect(
            "pressed",
            godot::Callable(this, "on_entry_pressed").bind(static_cast<std::int64_t>(i)));
        auto *supply = memnew(godot::Label);
        auto *swatch = memnew(godot::ColorRect);
        swatch->set_color(colors_[i]);
        swatch->set_custom_minimum_size(godot::Vector2(18.0f, 18.0f));
        row->add_child(preview);
        row->add_child(swatch);
        row->add_child(button);
        row->add_child(supply);
        palette_rows_->add_child(row);
        entry_controls_.push_back(EntryControl { preview, supply });
    }
}

void LevelPlayer::on_entry_pressed(std::int64_t p_entry) {
    if (p_entry >= 0) {
        select_entry(static_cast<std::size_t>(p_entry));
    }
    grab_focus();
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
    const auto keep = [&](ProposalCommand p_command, auto p_preview) {
        if (p_preview) {
            proposals_.push_back(Proposal { std::move(p_command), std::move(p_preview).value() });
        }
    };
    if (state.arrangement().entries().empty()) {
        const Point origin { Coordinate::from_raw(0), Coordinate::from_raw(0) };
        engine::PlaceCommand command { entry, orientation, origin };
        keep(command, state.preview(command));
        update_active_proposal();
        return;
    }
    std::vector<Proposal> discovered;
    const std::size_t candidate_features = candidate->canonical_polygon().vertices().size();
    for (const Entry &anchor : state.arrangement().entries()) {
        const std::size_t features = anchor.placement.footprint().vertices().size();
        for (std::size_t a = 0; a < features; ++a) {
            for (std::size_t b = 0; b < candidate_features; ++b) {
                engine::MateFullEdgesCommand command {
                    anchor.id, EdgeIndex(a), entry, orientation, EdgeIndex(b) };
                auto preview = state.preview(command);
                if (preview) {
                    discovered.push_back(Proposal { command, std::move(preview).value() });
                }
            }
        }
    }
    for (const Entry &anchor : state.arrangement().entries()) {
        const std::size_t features = anchor.placement.footprint().vertices().size();
        for (std::size_t a = 0; a < features; ++a) {
            for (std::size_t b = 0; b < candidate_features; ++b) {
                engine::MateVerticesCommand command {
                    anchor.id, VertexIndex(a), entry, orientation, VertexIndex(b) };
                auto preview = state.preview(command);
                if (preview) {
                    discovered.push_back(Proposal { command, std::move(preview).value() });
                }
            }
        }
    }
    for (Proposal &proposal : discovered) {
        bool duplicate = false;
        for (const Proposal &kept : proposals_) {
            if (kept.placement.prototile().id() == proposal.placement.prototile().id()
                && kept.placement.orientation() == proposal.placement.orientation()
                && kept.placement.translation() == proposal.placement.translation()) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate && shares_edge_contact(proposal.placement.footprint(), state.arrangement())) {
            proposals_.push_back(std::move(proposal));
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
            double best = 0.0;
            for (std::size_t i = 0; i < proposals_.size(); ++i) {
                const auto &vertices = proposals_[i].placement.footprint().vertices();
                double x = 0.0;
                double y = 0.0;
                for (const Point &vertex : vertices) {
                    const godot::Vector2 point = project(vertex);
                    x += point.x;
                    y += point.y;
                }
                const double dx = x / vertices.size() - pointer_->x;
                const double dy = y / vertices.size() - pointer_->y;
                const double distance = dx * dx + dy * dy;
                if (!selected.has_value() || distance < best) {
                    selected = i;
                    best = distance;
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
        entry_controls_[i].supply->set_text(
            status->remaining.has_value()
                ? godot::String::num_uint64(status->remaining.value())
                : godot::String("unlimited"));
        if (entry_controls_[i].preview != nullptr) {
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
bool LevelPlayer::completion_visible() const { return session_.has_value() && session_->state().solved(); }
godot::Rect2 LevelPlayer::canvas_rect() const { return canvas_rect_; }

} // namespace tiles::game
