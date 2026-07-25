#pragma once

#include "content/PrototileCatalog.h"
#include "core/Result.h"
#include "game/resources/LevelResources.h"
#include "game/resources/ResourceCompiler.h"

#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/variant/string.hpp>

#include <optional>

namespace tiles::game {

enum class ExportLevelErrorCode {
    missing_resource,
    path_required,
    unsupported_extension,
    compilation_failed,
    saver_failed,
};

// compilation_error is populated exactly for compilation_failed, and preserves
// the complete typed compiler error. The complete engine error is preserved for
// saver_failed; every other failure is decided before the saver is reached and
// reports godot::OK there rather than inventing a fake engine code.
struct ExportLevelError final {
    ExportLevelErrorCode code;
    std::optional<LevelResourceError> compilation_error;
    godot::Error godot_error;
};

// Export one level artifact to one explicitly named `.tres` destination, and
// return the path it was written to.
//
// This is an export, not document persistence. The destination is always
// explicit: there is no resource-owned path, no save/save-as distinction, no
// path takeover, no `.res` or bespoke format, and no editor document identity.
// The resource's inherited path, every subresource path, every property value,
// every pointer relationship, and every `changed` notification count are the
// same afterwards as before, whether the export succeeded or failed.
//
// The complete candidate is compiled first and a compilation failure never
// reaches the saver, so an artifact which cannot become an exact level is never
// written. Subresources are bundled, so the written file depends on no other
// file. Once the saver has begun, an operating-system or engine I/O failure may
// still leave a partial file behind: that outcome is reported honestly rather
// than promised away.
Result<godot::String, ExportLevelError> export_level_resource(
    const godot::Ref<LevelResource> &p_resource,
    const godot::String &p_explicit_path,
    const content::PrototileCatalog &p_catalog);

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

// The authored resource graph together with the complete exact product compiled
// from it. Both are returned because a consumer needs authored presentation —
// per-entry color in authored order — beside the presentation-free exact
// values.
struct LoadedLevelResource final {
    godot::Ref<LevelResource> resource;
    CompiledLevelResource compiled;
};

// Load one exported level artifact and compile it against the supplied catalog.
//
// This is the eventual level consumer, and the round-trip proof that an
// exported artifact is readable. Loading bypasses the resource cache deeply, so
// asking for a path observes what is persisted there rather than an older
// cached object or the in-memory resource which was just written to it. There
// is no fallback to a cached resource, a current editor document, an alternate
// type, a migration adapter, or a repaired graph: an invalid persisted level
// fails at the typed compilation stage.
//
// The authoring editor never calls this and exposes no load operation.
Result<LoadedLevelResource, LoadLevelError> load_level_resource(
    const godot::String &p_path, const content::PrototileCatalog &p_catalog);

} // namespace tiles::game
