#pragma once

#include "content/GeometryDomain.h"
#include "core/ArrangementRegion.h"
#include "core/Placement.h"
#include "core/geometry/Point.h"
#include "engine/Blueprint.h"
#include "game/LevelEditor.h"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include <cstddef>
#include <optional>
#include <vector>

namespace tiles::game {

// A headless check of the level editor's three authoring phases, its exact
// proposal derivation, its transactional blueprint publication, its nonvisual
// coverage proof, and its one export operation.
//
// It is instantiated only by its own dedicated test scene: it never enters the
// main application scene, runs during ordinary startup, or opens a window. It
// drives the real main scene — the same `res://main.tscn` the application boots
// — through the editor's own public authoring operations, the very ones the UI
// callbacks call, so there is no duplicate proposal builder, blueprint
// compiler, or alternate editor state anywhere in this file.
class LevelEditorIntegrationRunner : public godot::Node {
    GDCLASS(LevelEditorIntegrationRunner, godot::Node)

protected:
    static void _bind_methods();

public:
    void _ready() override;

private:
    bool expect(bool p_condition, const char *p_description);

    void check_startup();
    void check_domain_views();
    void check_domain_discards();
    void check_palette_locking();
    void check_first_placement();
    void check_selection_cycling();
    void check_proposal_derivation();
    void check_repeated_lattice_additions();
    void check_supply_exhaustion();
    void check_removal_and_clear();
    void check_pointer_selection();
    void check_drawing_inputs();
    void check_hex12_blueprint();
    void check_region_proof_lifecycle();
    void check_region_hole();
    void check_disconnected_coverage();
    void check_nonmanifold_coverage();
    void check_hex12_region();
    void check_export_controls();
    void check_export_refusals();
    void check_export_round_trip();
    void check_export_encoding_refusal();
    void check_absent_region_and_persistence_surface();
    void check_temporary_files_removed();

    // Accept exactly the offered proposal whose exact translation is named,
    // chosen the way a user chooses one: by moving the pointer to its projected
    // handle. Fails rather than accepting something else.
    bool place_at(Point p_translation);

    // Exactly one of the region and the typed region error exists, and the
    // arrangement holds exactly one placement per record.
    bool proof_is_exclusive() const;

    // The exact placement one published record denotes, resolved through the
    // locked palette. Used to rebuild a coverage independently of the editor.
    std::optional<Placement> placement_of_record(
        const engine::BlueprintPlacement &p_record) const;

    // The complete observable document, captured so an export can be proven to
    // change none of it.
    struct DocumentSnapshot final {
        bool has_document = false;
        LevelEditor::EditorPhase phase = LevelEditor::EditorPhase::choose_domain;
        std::optional<content::GeometryDomain> domain;
        std::size_t palette_order = 0;
        std::vector<PrototileId> palette_ids;
        std::vector<godot::Color> colors;
        std::vector<engine::BlueprintPlacement> records;
        std::vector<Point> arrangement_translations;
        bool has_region = false;
        std::vector<Point> region_vertices;
        std::size_t hole_count = 0;
        std::optional<ArrangementRegionErrorCode> region_error;
        std::optional<LevelEditor::Selection> selection;
        std::optional<std::size_t> active_proposal;
        std::size_t proposal_count = 0;
        double pixels_per_unit = 0.0;
        godot::Vector2 camera_origin;
        std::size_t entry_control_count = 0;
    };

    DocumentSnapshot capture() const;
    bool unchanged_since(const DocumentSnapshot &p_before) const;

    // One named file, by exact path. Nothing recursive and nothing derived from
    // unchecked input is ever removed.
    void remove_temporary(const godot::String &p_path);

    // Bring the editor into blueprint phase in one domain with one palette,
    // built through the editor's own operations. Returns false if any step the
    // later checks depend on failed.
    bool lock_palette(
        content::GeometryDomain p_domain,
        const std::vector<std::size_t> &p_rows,
        const std::vector<std::int64_t> &p_finite_amounts);

    // Independent exact predicates, deliberately not the editor's own helpers.
    bool shares_positive_length_contact(const Placement &p_placement) const;
    bool overlaps_blueprint(const Placement &p_placement) const;

    static bool same_records(
        const std::vector<engine::BlueprintPlacement> &p_lhs,
        const std::vector<engine::BlueprintPlacement> &p_rhs);

    LevelEditor *editor_ = nullptr;

    // Every path this run may write, in the one temporary directory it owns.
    std::vector<godot::String> temporary_paths_;

    std::size_t checks_ = 0;
    std::size_t failures_ = 0;
};

} // namespace tiles::game
