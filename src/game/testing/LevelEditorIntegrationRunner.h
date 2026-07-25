#pragma once

#include "content/GeometryDomain.h"
#include "engine/Blueprint.h"
#include "game/LevelEditor.h"

#include <godot_cpp/classes/node.hpp>

#include <cstddef>
#include <vector>

namespace tiles::game {

// A headless check of the level editor's three authoring phases, its exact
// proposal derivation, and its transactional blueprint publication.
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
    void check_absent_region_and_persistence_surface();

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

    std::size_t checks_ = 0;
    std::size_t failures_ = 0;
};

} // namespace tiles::game
