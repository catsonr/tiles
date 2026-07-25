#pragma once

#include "content/PrototileCatalog.h"
#include "core/Region.h"
#include "engine/Palette.h"
#include "game/LevelEditor.h"
#include "game/resources/LevelResources.h"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/variant/string.hpp>

#include <cstddef>
#include <vector>

namespace tiles::game {

// A headless check of the level editor's authoring surface, its exact
// compilation gates, and its persistence behaviour.
//
// It is instantiated only by its own dedicated test scene: it never enters the
// main application scene, runs during ordinary startup, or opens a window. It
// drives the real main scene — the same `res://main.tscn` the application boots
// — through the editor's own authoring operations, so there is no duplicate
// region builder, compiler, save implementation, or alternate editor state
// anywhere in this file.
class LevelEditorIntegrationRunner : public godot::Node {
    GDCLASS(LevelEditorIntegrationRunner, godot::Node)

protected:
    static void _bind_methods();

public:
    void _ready() override;

    // Records every typed play request the editor publishes, so emission count
    // and pointer identity are observed rather than assumed.
    void on_play_requested(const godot::Ref<LevelResource> &p_level);

private:
    bool expect(bool p_condition, const char *p_description);

    void check_startup();
    void check_palette_rows();
    void check_palette_publication();
    void check_region_authoring();
    void check_holes();
    void check_restart();
    void check_cross_preservation();
    void check_new_level();
    void check_open_drafts(const content::PrototileCatalog &p_catalog);
    void check_save_and_play(const content::PrototileCatalog &p_catalog);
    void check_absent_external_palette_surface();
    void check_temporary_files_removed();

    // Remove exactly one named temporary file this runner owns. Never recursive,
    // never derived from unchecked input.
    void remove_temporary(const godot::String &p_path);

    // Drive one authored boundary through the editor's own operations.
    bool draw_boundary(
        LevelEditor::BoundaryKind p_kind,
        const std::vector<LevelEditor::GridPoint> &p_points);

    // Compare two compiled values field by field, never by pointer identity.
    bool same_palette(const engine::Palette &p_lhs, const engine::Palette &p_rhs);
    bool same_region(const Region &p_lhs, const Region &p_rhs);

    LevelEditor *editor_ = nullptr;

    std::size_t checks_ = 0;
    std::size_t failures_ = 0;

    std::size_t play_emissions_ = 0;
    godot::Ref<LevelResource> last_played_;

    std::vector<godot::String> temporary_paths_;
};

} // namespace tiles::game
