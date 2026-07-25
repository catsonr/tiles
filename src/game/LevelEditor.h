#pragma once

#include "content/PrototileCatalog.h"
#include "core/Region.h"
#include "engine/Palette.h"
#include "game/resources/LevelResources.h"

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/check_box.hpp>
#include <godot_cpp/classes/color_picker_button.hpp>
#include <godot_cpp/classes/confirmation_dialog.hpp>
#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/file_dialog.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/option_button.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/spin_box.hpp>
#include <godot_cpp/classes/v_box_container.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/rect2.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace tiles::game {

class PrototilePreview;

// The application's authoring surface: one Control which owns a canonical
// catalog, one mutable LevelResource draft, the palette rows which rebuild that
// draft's embedded palette, and the integer-grid region canvas which rebuilds
// its region.
//
// The dependency direction is one-way. Authored Godot values are compiled into
// exact values by the separate resource compiler and read back only for
// presentation; no projected pixel, camera value, or rendered polygon ever
// re-enters the model. The canvas projection is deliberately lossy: pointer
// input becomes authoritative only after it has been snapped to an integer game
// coordinate and stored as that integer.
//
// The defining invariant is that every file this editor reports as saved was
// successfully compiled as a complete LevelResource immediately before the save.
// A proposed boundary which fails to compile never enters the active resource:
// candidate resource graphs are built beside the live one and published only
// after the exact compiler has proven them.
class LevelEditor : public godot::Control {
    GDCLASS(LevelEditor, godot::Control)

protected:
    static void _bind_methods();

public:
    // One authored integer game coordinate. It is stored as an integer, not as
    // a rendered Vector2 and not as a lattice value: the resource compiler
    // remains the one q16.48 quantization boundary.
    struct GridPoint final {
        std::int64_t x = 0;
        std::int64_t y = 0;

        friend bool operator==(GridPoint p_lhs, GridPoint p_rhs) {
            return p_lhs.x == p_rhs.x && p_lhs.y == p_rhs.y;
        }

        friend bool operator!=(GridPoint p_lhs, GridPoint p_rhs) {
            return !(p_lhs == p_rhs);
        }
    };

    enum class BoundaryKind {
        outer_replacement,
        new_hole,
    };

    // One nonempty-or-empty proposed boundary which has not been accepted. It is
    // the only place an invalid proposal may exist: the active resource keeps
    // its previous valid region untouched while a loop is open.
    struct OpenLoop final {
        BoundaryKind kind = BoundaryKind::outer_replacement;
        std::vector<GridPoint> vertices;
        // Set by a failed closure, cleared by any change to the points, because
        // the diagnostic described an older proposal.
        bool failed_closure = false;
    };

    // The authoring view of one canonical catalog entry. Identity stays in the
    // catalog: a row stores its index, never a copied id, name, or geometry.
    struct PaletteRow final {
        std::size_t catalog_index = 0;
        bool included = false;
        bool unlimited = true;
        std::int64_t finite_amount = 1;
        godot::Color color;
    };

    // The pending file action a discard confirmation is guarding.
    enum class PendingAction {
        none,
        new_level,
        open_draft,
    };

    void _ready() override;
    void _draw() override;
    void _gui_input(const godot::Ref<godot::InputEvent> &p_event) override;

    // --- authoring operations ---
    //
    // These are the operations the toolbar, the palette rows, and the canvas
    // perform. They are ordinary C++ methods rather than a second controller:
    // the UI callbacks call exactly these, and so does the headless runner, so
    // there is only one implementation of every rule below.

    // Replace the active document with one new unsaved level owning an empty
    // embedded palette and no region. Confirmation, if needed, happens before
    // this is reached.
    void install_new_document();

    // Load one draft uncached and install it only if every required check
    // succeeds. The current document is preserved exactly on every failure.
    bool open_draft(const godot::String &p_path);

    void set_row_included(std::size_t p_row, bool p_included);
    void set_row_unlimited(std::size_t p_row, bool p_unlimited);
    void set_row_finite_amount(std::size_t p_row, std::int64_t p_amount);
    void set_row_color(std::size_t p_row, const godot::Color &p_color);

    void begin_outer_loop();
    void begin_hole_loop();
    bool append_point(GridPoint p_point);
    void remove_last_point();
    void cancel_loop();
    bool close_loop();

    // Compile the complete resource and, only then, persist it. An empty path
    // means "save to the resource's own path".
    bool save_document(const godot::String &p_explicit_path);

    // Compile the complete resource and, only then, emit play_requested with the
    // exact current Ref. It never saves, clones, or clears dirty state.
    bool request_play();

    // --- observation, for presentation and for the headless runner ---

    bool has_document() const {
        return document_.has_value();
    }

    godot::Ref<LevelResource> level_resource() const;
    const engine::Palette *compiled_palette() const;
    const Region *compiled_region() const;
    const std::vector<PaletteRow> &palette_rows() const;
    const OpenLoop *open_loop() const;
    bool is_dirty() const;
    bool can_save() const;

    PendingAction pending_action() const {
        return pending_action_;
    }

    const godot::String &status_text() const {
        return status_;
    }

    const content::PrototileCatalog *catalog() const;

    // Snap one local pointer position onto the integer grid. Empty when the
    // result is not finite or leaves the representable whole-game-unit range.
    std::optional<GridPoint> snap_local(godot::Vector2 p_local) const;

    // The lossy presentation projection and the camera behind it. Exposed so
    // pointer-driven behaviour can be exercised through the same transform the
    // canvas draws with; nothing produced here re-enters the model.
    godot::Vector2 project(double p_x, double p_y) const;
    double pixels_per_unit() const;
    godot::Vector2 camera_origin() const;
    godot::Rect2 canvas_rect() const;

    // The row control nodes, so the runner can prove the visible surface really
    // is one row per catalog entry rather than only checking internal state.
    godot::CheckBox *row_include_control(std::size_t p_row) const;
    PrototilePreview *row_preview_control(std::size_t p_row) const;
    godot::Label *row_name_control(std::size_t p_row) const;
    godot::OptionButton *row_supply_control(std::size_t p_row) const;
    godot::SpinBox *row_amount_control(std::size_t p_row) const;
    godot::ColorPickerButton *row_color_control(std::size_t p_row) const;

    godot::Button *action_button(const char *p_name) const;

    // --- bound callbacks ---

    void on_new_level_pressed();
    void on_open_draft_pressed();
    void on_restart_region_pressed();
    void on_add_hole_pressed();
    void on_save_pressed();
    void on_save_as_pressed();
    void on_play_pressed();

    void on_open_file_selected(const godot::String &p_path);
    void on_save_file_selected(const godot::String &p_path);
    void on_discard_confirmed();
    void on_discard_canceled();

    void on_row_included_toggled(bool p_pressed, std::int64_t p_row);
    void on_row_supply_selected(std::int64_t p_index, std::int64_t p_row);
    void on_row_amount_changed(double p_amount, std::int64_t p_row);
    void on_row_color_changed(const godot::Color &p_color, std::int64_t p_row);

private:
    // One reversible presentation transform. Screen projection is lossy and its
    // inverse is only ever used to produce a snapped integer.
    struct Camera final {
        godot::Vector2 origin_pixels;
        double pixels_per_unit = 32.0;
    };

    // The controls one catalog row owns. They are ordinary Godot children of the
    // rows container; nothing here is a registered class or a second document
    // model.
    struct RowControls final {
        godot::CheckBox *include = nullptr;
        PrototilePreview *preview = nullptr;
        godot::Label *name = nullptr;
        godot::OptionButton *supply = nullptr;
        godot::SpinBox *amount = nullptr;
        godot::ColorPickerButton *color = nullptr;
    };

    // The complete editable document. compiled_palette and compiled_region are
    // derived proof-bearing values held for presentation only; every save and
    // play request compiles the complete resource again rather than trusting
    // them.
    struct Document final {
        godot::Ref<LevelResource> resource;
        std::optional<engine::Palette> compiled_palette;
        std::optional<Region> compiled_region;
        std::vector<PaletteRow> rows;
        std::optional<OpenLoop> open_loop;
        bool dirty = false;
    };

    bool bind_scene();
    void build_palette_rows();
    void sync_row_controls(std::size_t p_row);
    void sync_all_row_controls();

    // Build one candidate palette from the current rows and publish it only if
    // it compiles. p_previous_rows restores the visible controls if it does not.
    void publish_palette(const std::vector<PaletteRow> &p_previous_rows);

    // Build a candidate region resource from the accepted region plus the open
    // loop, compile it, and publish it only on success.
    bool commit_open_loop();

    void refresh_controls();
    void refresh_instructions();
    void refresh_path_label();
    void refresh_coordinate_label();
    void set_status(const godot::String &p_text);

    void center_camera_on_origin();
    void frame_region(const Region &p_region);
    godot::Vector2 to_screen(double p_x, double p_y) const;
    godot::Vector2 to_screen(GridPoint p_point) const;
    void update_canvas_rect();
    void update_snapped_cursor(godot::Vector2 p_local);
    void zoom_at(godot::Vector2 p_local, double p_factor);

    void draw_grid();
    void draw_region_area();
    void draw_open_loop();
    void draw_cursor();

    void popup_open_dialog();
    void popup_save_dialog();

    // The one successfully constructed canonical catalog. It is the sole source
    // of playable geometry, display names, and row identity for this editor.
    std::optional<content::PrototileCatalog> catalog_;

    std::optional<Document> document_;
    PendingAction pending_action_ = PendingAction::none;

    Camera camera_;
    godot::Rect2 canvas_rect_;

    std::optional<godot::Vector2> cursor_pixels_;
    std::optional<GridPoint> snapped_;
    bool snap_out_of_range_ = false;

    std::optional<godot::Vector2> pan_anchor_;

    godot::String status_;

    // Set while row controls are being written from row state, so an engine-
    // emitted change notification is never mistaken for an author's edit.
    bool suppress_row_signals_ = false;

    // Scene children, resolved once and checked before use.
    godot::Control *toolbar_ = nullptr;
    godot::Control *palette_panel_ = nullptr;
    godot::Control *status_bar_ = nullptr;
    godot::VBoxContainer *rows_container_ = nullptr;
    godot::Label *instruction_label_ = nullptr;
    godot::Label *status_label_ = nullptr;
    godot::Label *coordinate_label_ = nullptr;
    godot::Label *path_label_ = nullptr;
    godot::Button *new_level_button_ = nullptr;
    godot::Button *open_draft_button_ = nullptr;
    godot::Button *restart_region_button_ = nullptr;
    godot::Button *add_hole_button_ = nullptr;
    godot::Button *save_button_ = nullptr;
    godot::Button *save_as_button_ = nullptr;
    godot::Button *play_button_ = nullptr;
    godot::FileDialog *open_dialog_ = nullptr;
    godot::FileDialog *save_dialog_ = nullptr;
    godot::ConfirmationDialog *discard_dialog_ = nullptr;

    std::vector<RowControls> row_controls_;
};

} // namespace tiles::game
