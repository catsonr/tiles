#pragma once

#include "content/PrototileCatalog.h"
#include "core/Arrangement.h"
#include "core/Region.h"
#include "engine/Blueprint.h"
#include "engine/Palette.h"
#include "game/resources/ResourceCompiler.h"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/string.hpp>

#include <cstddef>
#include <vector>

namespace tiles::game {

// A minimal headless check of the proof-carrying level resource: its graph, the
// exact one-way compiler, the fresh encoder, and typed export and consumer
// loading.
//
// It is instantiated only by its own dedicated test scene: it never enters the
// main application scene, runs during ordinary startup, or opens a window. It
// is deliberately not a general test framework — it is one node which performs
// a fixed sequence of deterministic checks, reports each failure through
// push_error, prints one concise summary, removes exactly the temporary files
// it created, and exits its scene tree with a nonzero code if anything failed.
class ResourceIntegrationRunner : public godot::Node {
    GDCLASS(ResourceIntegrationRunner, godot::Node)

protected:
    static void _bind_methods();

public:
    void _ready() override;

    // Counts `changed` notifications from resources this runner observes, so
    // setter notification behaviour is checked without any timing assumption.
    void on_resource_changed();

private:
    // Record one check. Returns its own verdict so a caller can stop early
    // rather than dereferencing a value it just proved absent.
    bool expect(bool p_condition, const char *p_description);

    void check_registration();
    void check_resource_defaults();
    void check_property_metadata();
    void check_setter_notifications();

    void check_palette_compilation(const content::PrototileCatalog &p_catalog);

    // The domain-aware palette compiler: hex-12 compilation, domain membership
    // refusals, an invalid domain, and the shared unit square.
    void check_domain_palette_compilation(const content::PrototileCatalog &p_catalog);
    void check_palette_size_limit(const content::PrototileCatalog &p_catalog);

    void check_blueprint_decoding();
    void check_level_compilation(const content::PrototileCatalog &p_catalog);
    void check_region_derivation(const content::PrototileCatalog &p_catalog);
    void check_compilation_purity(const content::PrototileCatalog &p_catalog);
    void check_encoding(const content::PrototileCatalog &p_catalog);

    void check_authored_fixture(const content::PrototileCatalog &p_catalog);
    void check_export_and_load(const content::PrototileCatalog &p_catalog);
    void check_temporary_files_removed();

    // Remove exactly one named temporary file this runner owns. Never recursive,
    // never derived from unchecked input.
    void remove_temporary(const godot::String &p_path);

    // Compare two compiled values field by field. None uses pointer identity,
    // and none is implemented as an operator== on a domain type.
    bool same_palette(const engine::Palette &p_lhs, const engine::Palette &p_rhs);
    bool same_region(const Region &p_lhs, const Region &p_rhs);
    bool same_arrangement(const Arrangement &p_lhs, const Arrangement &p_rhs);
    bool same_blueprint(
        const std::vector<engine::BlueprintPlacement> &p_lhs,
        const std::vector<engine::BlueprintPlacement> &p_rhs);

    // Replay one compiled artifact's stored witness into a fresh runtime state
    // and report whether every command succeeded and the result is solved.
    //
    // This is a local test helper, not a production engine or resource
    // operation: no replayed state enters a resource, the compiled product, the
    // editor, or a future player.
    bool replay_known_solution(const CompiledLevelResource &p_compiled);

    std::size_t checks_ = 0;
    std::size_t failures_ = 0;
    std::size_t changed_notifications_ = 0;

    // Every temporary path this run created, so cleanup names exact files.
    std::vector<godot::String> temporary_paths_;
};

} // namespace tiles::game
