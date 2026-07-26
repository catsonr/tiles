#pragma once

#include "content/PrototileCatalog.h"
#include "core/Placement.h"
#include "engine/Commands.h"
#include "engine/Session.h"

#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/rich_text_label.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/rect2.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include <cstddef>
#include <optional>
#include <variant>
#include <vector>

namespace tiles::game {

class PrototilePreview;

// One intentionally small presentation shell around an empty exact Session.
// It loads an authored level once, then only selects among and applies commands
// the engine has already proven legal. Screen coordinates rank and hit-test
// rendered values only; they never create model geometry.
class LevelPlayer : public godot::Control {
    GDCLASS(LevelPlayer, godot::Control)

protected:
    static void _bind_methods();

public:
    struct Selection final {
        std::size_t entry;
        std::size_t orientation;
    };

    using ProposalCommand = std::variant<
        engine::PlaceCommand,
        engine::MateFullEdgesCommand,
        engine::MateVerticesCommand>;

    struct Proposal final {
        ProposalCommand command;
        Placement placement;
    };

    void _ready() override;
    void _draw() override;
    void _gui_input(const godot::Ref<godot::InputEvent> &p_event) override;

    void set_level_path(const godot::String &p_path);
    godot::String get_level_path() const;

    void select_entry(std::size_t p_entry);
    void cycle_entry(bool p_forward);
    void cycle_orientation(bool p_forward);
    bool accept_active_proposal();
    bool remove_at_local(godot::Vector2 p_local);
    bool undo();
    void set_pointer(godot::Vector2 p_local);
    bool initialized() const;
    const engine::Session *session() const;
    std::optional<Selection> selection() const;
    const std::vector<Proposal> &proposals() const;
    std::optional<std::size_t> active_proposal() const;
    std::optional<godot::Color> entry_color(std::size_t p_entry) const;
    const PrototilePreview *entry_preview(std::size_t p_entry) const;
    bool entry_supply_visible(std::size_t p_entry) const;
    bool completion_visible() const;
    godot::Vector2 project(Point p_point) const;
    godot::Rect2 canvas_rect() const;

private:
    struct EntryControl final {
        PrototilePreview *preview = nullptr;
        godot::Label *supply = nullptr;
    };

    bool load();
    void build_palette_controls();
    void refresh_controls();
    void rebuild_proposals();
    bool update_active_proposal();
    void refresh_after_mutation();
    const engine::PaletteEntry *selected_entry() const;
    const OrientedPrototile *selected_variant() const;
    std::optional<std::size_t> placement_at_local(godot::Vector2 p_local) const;
    void update_projection();
    void update_canvas_rect();
    godot::Vector2 project(double p_x, double p_y) const;
    void draw_polygon(const Polygon &p_polygon, godot::Color p_fill, float p_outline);
    void draw_region();
    void draw_arrangement();
    void draw_ghost();

    godot::String level_path_;
    std::optional<content::PrototileCatalog> catalog_;
    std::optional<engine::Session> session_;
    std::vector<godot::Color> colors_;
    std::optional<Selection> selection_;
    std::vector<Proposal> proposals_;
    std::optional<std::size_t> active_proposal_;
    std::optional<godot::Vector2> pointer_;
    godot::Rect2 canvas_rect_;
    godot::Vector2 projection_origin_;
    double pixels_per_unit_ = 1.0;
    godot::Label *status_label_ = nullptr;
    godot::RichTextLabel *completion_label_ = nullptr;
    godot::Control *palette_rows_ = nullptr;
    std::vector<EntryControl> entry_controls_;
};

} // namespace tiles::game
