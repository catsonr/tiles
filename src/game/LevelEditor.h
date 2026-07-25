#pragma once

#include "content/GeometryDomain.h"
#include "content/PrototileCatalog.h"
#include "core/Arrangement.h"
#include "core/OrientedPrototile.h"
#include "core/Placement.h"
#include "engine/Blueprint.h"
#include "engine/Palette.h"
#include "engine/Supply.h"

#include <godot_cpp/classes/accept_dialog.hpp>
#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/color_picker_button.hpp>
#include <godot_cpp/classes/color_rect.hpp>
#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/option_button.hpp>
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

// The application's one authoring surface: choose a geometry domain, lock one
// exact palette, and construct one exact blueprint arrangement out of it.
//
// The document has exactly three phases and every one of them is derived from
// what the document actually owns, so no phase field can disagree with the
// state it describes:
//
//     no document                  choose_domain
//     document without a palette   choose_palette
//     document with a palette      build_blueprint
//
// The dependency direction is one-way. Authored Godot values are compiled into
// exact values by the separate resource compiler and the blueprint compiler, and
// read back only for presentation. No projected pixel, camera value, or rendered
// polygon ever re-enters the model: the pointer only ranks already-exact
// proposals and chooses already-stored record indices.
//
// The record vector is the reconstructable authority and the arrangement is the
// complete proof produced from it. They are published together, by one
// transactional recompilation of the whole candidate, and never drift.
class LevelEditor : public godot::Control {
    GDCLASS(LevelEditor, godot::Control)

protected:
    static void _bind_methods();

public:
    enum class EditorPhase {
        choose_domain,
        choose_palette,
        build_blueprint,
    };

    // The authoring view of one canonical catalog entry. Identity stays in the
    // catalog: a row stores its index into the active domain's view, never a
    // copied id, name, or geometry.
    struct PaletteRow final {
        std::size_t catalog_index = 0;
        bool included = false;
        bool unlimited = true;
        std::int64_t finite_amount = 1;
        godot::Color color;
    };

    // One locked palette entry together with one of that entry's distinct
    // compiled orientations. It has no empty or invalid meaning, so it is only
    // ever held inside an optional; there is no sentinel index.
    struct Selection final {
        std::size_t entry = 0;
        std::size_t orientation = 0;
    };

    // One currently offered addition: the exact record it would append, and the
    // exact placement the arrangement's own preview derived for it. Both are
    // authoritative; neither is reconstructed from a projected coordinate.
    struct Proposal final {
        engine::BlueprintPlacement record;
        Placement placement;
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

    // Install one fresh palette-phase document in the named domain. A domain
    // value outside the enumeration installs nothing at all.
    bool choose_domain(content::GeometryDomain p_domain);

    // Discard the entire document. There is no migration, conversion, retained
    // palette, retained arrangement, or confirmation.
    void return_to_domain_choice();

    void set_row_included(std::size_t p_row, bool p_included);
    void set_row_unlimited(std::size_t p_row, bool p_unlimited);
    void set_row_finite_amount(std::size_t p_row, std::int64_t p_amount);
    void set_row_color(std::size_t p_row, const godot::Color &p_color);

    // Compile the rows into one exact palette and, only if that succeeds, lock
    // it and enter blueprint phase with an empty blueprint. A failure leaves the
    // document in palette phase, completely unchanged.
    bool build_palette();

    void select_entry(std::size_t p_entry);
    void cycle_entry(bool p_forward);
    void cycle_orientation(bool p_forward);

    // Append the active proposal's record and publish the recompiled blueprint.
    // Nothing changes at all when there is no active proposal, when the selected
    // entry's finite supply is spent, or when the candidate fails to compile.
    bool accept_active_proposal();

    // Drop one stored record and publish the recompiled blueprint.
    bool remove_record(std::size_t p_record);

    void clear_blueprint();

    // --- pointer, which is presentation only ---

    // Record the last local pointer position and re-rank the cached proposals
    // against it. Nothing here constructs or modifies geometry.
    void set_pointer(godot::Vector2 p_local);
    void clear_pointer();

    // The topmost rendered stored placement containing a local pointer
    // position, as a record index. Lossy: the test runs entirely in projected
    // screen space and can only ever choose an index.
    std::optional<std::size_t> record_at_local(godot::Vector2 p_local) const;

    // --- observation, for presentation and for the headless runner ---

    EditorPhase phase() const;
    std::optional<content::GeometryDomain> domain() const;

    const content::PrototileCatalog *catalog() const;

    // The canonical identities the active domain admits, in presentation order.
    // Empty in domain phase.
    std::vector<const content::CanonicalPrototile *> domain_entries() const;

    const std::vector<PaletteRow> &palette_rows() const;
    const engine::Palette *palette() const;

    // The authored color of one locked palette entry, in palette order.
    std::optional<godot::Color> entry_color(std::size_t p_entry) const;

    const std::vector<engine::BlueprintPlacement> &blueprint() const;
    const Arrangement *arrangement() const;

    std::optional<Selection> selection() const;
    const OrientedPrototile *selected_variant() const;

    // The remaining finite pieces of one palette entry, derived from the
    // blueprint records. Empty for an unlimited entry.
    std::optional<engine::Supply::Amount> remaining_supply(std::size_t p_entry) const;

    const std::vector<Proposal> &proposals() const {
        return proposals_;
    }

    std::optional<std::size_t> active_proposal() const {
        return active_proposal_;
    }

    const godot::String &status_text() const {
        return status_;
    }

    // The lossy presentation projection and the camera behind it. Exposed so
    // pointer-driven behaviour can be exercised through the same transform the
    // canvas draws with; nothing produced here re-enters the model.
    godot::Vector2 project(double p_x, double p_y) const;
    double pixels_per_unit() const;
    godot::Vector2 camera_origin() const;
    godot::Rect2 canvas_rect() const;

    // The generated control nodes, so the runner can prove the visible surface
    // really is one row per admitted identity rather than only checking internal
    // state.
    PrototilePreview *row_preview_control(std::size_t p_row) const;
    godot::Label *row_name_control(std::size_t p_row) const;
    godot::OptionButton *row_supply_control(std::size_t p_row) const;
    godot::SpinBox *row_amount_control(std::size_t p_row) const;
    godot::ColorPickerButton *row_color_control(std::size_t p_row) const;

    godot::Button *entry_select_control(std::size_t p_entry) const;
    godot::Label *entry_supply_control(std::size_t p_entry) const;

    godot::Button *action_button(const char *p_name) const;

    // --- bound callbacks ---

    void on_lattice_pressed();
    void on_hex12_pressed();
    void on_build_palette_pressed();
    void on_clear_blueprint_pressed();
    void on_help_pressed();

    void on_row_preview_input(
        const godot::Ref<godot::InputEvent> &p_event, std::int64_t p_row);
    void on_row_supply_selected(std::int64_t p_index, std::int64_t p_row);
    void on_row_amount_changed(double p_amount, std::int64_t p_row);
    void on_row_color_changed(const godot::Color &p_color, std::int64_t p_row);

    void on_entry_pressed(std::int64_t p_entry);

private:
    // One reversible presentation transform. Screen projection is lossy and its
    // inverse is used for nothing but ranking and hit testing.
    struct Camera final {
        godot::Vector2 origin_pixels;
        double pixels_per_unit = 32.0;
    };

    // The controls one palette-phase row owns.
    struct RowControls final {
        PrototilePreview *preview = nullptr;
        godot::Label *name = nullptr;
        godot::OptionButton *supply = nullptr;
        godot::SpinBox *amount = nullptr;
        godot::ColorPickerButton *color = nullptr;
    };

    // The controls one blueprint-phase palette entry owns.
    struct EntryControls final {
        godot::ColorRect *swatch = nullptr;
        PrototilePreview *preview = nullptr;
        godot::Button *select = nullptr;
        godot::Label *supply = nullptr;
    };

    // The complete editable document. The palette is absent in palette phase and
    // locked once present; the records and the arrangement are always published
    // together.
    struct Document final {
        content::GeometryDomain domain = content::GeometryDomain::lattice;
        std::vector<PaletteRow> rows;
        std::optional<engine::Palette> palette;
        // Authored colors, in locked palette order.
        std::vector<godot::Color> colors;
        std::vector<engine::BlueprintPlacement> records;
        Arrangement arrangement;
        std::optional<Selection> selection;
    };

    bool bind_scene();

    void build_palette_rows();
    void clear_palette_rows();
    void sync_row_controls(std::size_t p_row);
    void sync_all_row_controls();

    void build_entry_rows();
    void clear_entry_rows();
    void sync_entry_rows();

    // Publish one candidate record sequence only if the complete candidate
    // compiles. The current records and arrangement survive every failure.
    bool publish(std::vector<engine::BlueprintPlacement> p_candidate);

    // Re-derive the offered additions for the current selection and blueprint,
    // then re-rank them against the last pointer position. Pure with respect to
    // the document: no preview reserves an id, consumes supply, or changes the
    // blueprint.
    void rebuild_proposals();
    bool update_active_proposal();

    const engine::PaletteEntry *selected_entry() const;

    void refresh_controls();
    void refresh_instructions();
    void refresh_selection_label();
    void set_status(const godot::String &p_text);

    void center_camera_on_origin();
    godot::Vector2 to_screen(double p_x, double p_y) const;
    godot::Vector2 to_screen(Point p_point) const;
    void update_canvas_rect();
    void zoom_at(godot::Vector2 p_local, double p_factor);

    void draw_axes();
    void draw_arrangement();
    void draw_ghost();

    // The one successfully constructed canonical catalog. It is the sole source
    // of playable geometry, display names, and row identity for this editor.
    std::optional<content::PrototileCatalog> catalog_;

    std::optional<Document> document_;

    // Derived presentation state. Authoritative geometry stays in the document's
    // records and arrangement and in each proposal's exact Placement; nothing
    // here is a second coordinate representation.
    std::vector<Proposal> proposals_;
    std::optional<std::size_t> active_proposal_;

    Camera camera_;
    godot::Rect2 canvas_rect_;

    // The last local pointer position, in control pixels. It ranks projected
    // handles and hit-tests projected footprints, and does nothing else.
    std::optional<godot::Vector2> pointer_;

    std::optional<godot::Vector2> pan_anchor_;

    godot::String status_;

    // Set while row controls are being written from row state, so an engine-
    // emitted change notification is never mistaken for an author's edit.
    bool suppress_row_signals_ = false;

    // Scene children, resolved once and checked before use.
    godot::Control *toolbar_ = nullptr;
    godot::Control *palette_panel_ = nullptr;
    godot::Control *status_bar_ = nullptr;
    godot::Control *palette_scroll_ = nullptr;
    godot::Control *entry_scroll_ = nullptr;
    godot::VBoxContainer *rows_container_ = nullptr;
    godot::VBoxContainer *entries_container_ = nullptr;
    godot::Label *palette_title_ = nullptr;
    godot::Label *entry_title_ = nullptr;
    godot::Label *instruction_label_ = nullptr;
    godot::Label *status_label_ = nullptr;
    godot::Label *selection_label_ = nullptr;
    godot::Label *phase_label_ = nullptr;
    godot::Button *lattice_button_ = nullptr;
    godot::Button *hex12_button_ = nullptr;
    godot::Button *build_palette_button_ = nullptr;
    godot::Button *clear_blueprint_button_ = nullptr;
    godot::Button *help_button_ = nullptr;
    godot::AcceptDialog *help_dialog_ = nullptr;

    std::vector<RowControls> row_controls_;
    std::vector<EntryControls> entry_controls_;
};

} // namespace tiles::game
