#pragma once

#include "content/PrototileCatalog.h"
#include "core/Result.h"
#include "engine/Level.h"
#include "game/resources/LevelResources.h"
#include "game/resources/ResourceCompiler.h"

#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/variant/string.hpp>

#include <optional>

namespace tiles::game {

enum class SaveLevelErrorCode {
    missing_resource,
    path_required,
    unsupported_extension,
    saver_failed,
};

// The complete engine error is preserved for saver_failed. Every other failure
// is decided before the saver is reached and reports godot::OK in that field
// rather than inventing a fake engine code.
struct SaveLevelError final {
    SaveLevelErrorCode code;
    godot::Error godot_error;
};

// Persist one level resource and return the path it was written to.
//
// A nonempty explicit path wins; otherwise the resource's own path is used, and
// an empty effective path is rejected. Only `.tres` and `.res` are accepted,
// case-insensitively. An explicit path also becomes the resource's owned path,
// which is both initial-save and save-as behaviour; a later pathless save then
// overwrites that owned path without changing identity.
//
// Saving deliberately performs no compilation or validation. Validity is proven
// by the explicit compiler and at load and play time; duplicating that gate here
// would add no protection this project needs.
Result<godot::String, SaveLevelError> save_level_resource(
    const godot::Ref<LevelResource> &p_resource,
    const godot::String &p_explicit_path = godot::String());

enum class LoadLevelErrorCode {
    path_required,
    unsupported_extension,
    loader_failed,
    wrong_resource_type,
    compilation_failed,
};

// compilation_error is populated exactly for compilation_failed. The pinned
// loader does not report a complete godot::Error, so none is fabricated:
// loader failure and wrong type are distinguished as separate codes instead.
struct LoadLevelError final {
    LoadLevelErrorCode code;
    std::optional<LevelResourceError> compilation_error;
};

// The authored resource graph together with the exact level compiled from it.
// Both are returned because the player needs authored presentation — per-entry
// color in authored order — beside the presentation-free exact level.
struct LoadedLevel final {
    godot::Ref<LevelResource> resource;
    engine::Level level;
};

// Load one persisted level resource and compile it against the supplied catalog.
//
// Loading bypasses the resource cache, so asking for a path observes what is
// persisted there rather than an older cached object or the in-memory resource
// which was just saved to it. There is no fallback to a current resource, no
// catalog substitution, no repair, and no partial publication: an invalid
// persisted level fails at the typed compilation stage.
Result<LoadedLevel, LoadLevelError> load_level_resource(
    const godot::String &p_path, const content::PrototileCatalog &p_catalog);

} // namespace tiles::game
