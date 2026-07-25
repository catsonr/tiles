#include "game/resources/LevelPersistence.h"

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/resource_saver.hpp>
#include <godot_cpp/core/object.hpp>

#include <utility>

namespace tiles::game {

namespace {

// The one persistence format this project writes and reads: Godot's text
// resource container. Nothing here serializes binary `.res`, json, or a bespoke
// text format.
bool is_supported_extension(const godot::String &p_path) {
    return p_path.get_extension().to_lower() == godot::String("tres");
}

ExportLevelError export_failure(ExportLevelErrorCode p_code, godot::Error p_error) {
    ExportLevelError error {};
    error.code = p_code;
    error.godot_error = p_error;
    return error;
}

LoadLevelError load_failure(LoadLevelErrorCode p_code) {
    LoadLevelError error {};
    error.code = p_code;
    return error;
}

} // namespace

Result<godot::String, ExportLevelError> export_level_resource(
    const godot::Ref<LevelResource> &p_resource,
    const godot::String &p_explicit_path,
    const content::PrototileCatalog &p_catalog) {
    using Exported = Result<godot::String, ExportLevelError>;

    if (p_resource.is_null()) {
        return Exported::failure(
            export_failure(ExportLevelErrorCode::missing_resource, godot::OK));
    }

    if (p_explicit_path.is_empty()) {
        return Exported::failure(
            export_failure(ExportLevelErrorCode::path_required, godot::OK));
    }

    if (!is_supported_extension(p_explicit_path)) {
        return Exported::failure(
            export_failure(ExportLevelErrorCode::unsupported_extension, godot::OK));
    }

    // The complete compilation gate. It is the whole reason export exists as an
    // operation rather than as a bare saver call: an artifact which cannot
    // become an exact level never reaches the filesystem.
    auto compiled = compile_level_resource(p_resource, p_catalog);
    if (!compiled) {
        ExportLevelError error =
            export_failure(ExportLevelErrorCode::compilation_failed, godot::OK);
        error.compilation_error = compiled.error();
        return Exported::failure(error);
    }

    // Bundled subresources, and deliberately no FLAG_CHANGE_PATH: the written
    // file is self-contained and the exported resource keeps whatever inherited
    // path it already had. Export claims no file identity.
    const godot::Error result = godot::ResourceSaver::get_singleton()->save(
        p_resource,
        p_explicit_path,
        godot::BitField<godot::ResourceSaver::SaverFlags>(
            godot::ResourceSaver::FLAG_BUNDLE_RESOURCES));
    if (result != godot::OK) {
        return Exported::failure(
            export_failure(ExportLevelErrorCode::saver_failed, result));
    }

    return Exported::success(p_explicit_path);
}

Result<LoadedLevelResource, LoadLevelError> load_level_resource(
    const godot::String &p_path, const content::PrototileCatalog &p_catalog) {
    using Loaded = Result<LoadedLevelResource, LoadLevelError>;

    if (p_path.is_empty()) {
        return Loaded::failure(load_failure(LoadLevelErrorCode::path_required));
    }

    if (!is_supported_extension(p_path)) {
        return Loaded::failure(load_failure(LoadLevelErrorCode::unsupported_extension));
    }

    // Deep cache bypass: loading a path observes its persisted contents, not an
    // older cached object or the in-memory resource just written there.
    const godot::Ref<godot::Resource> loaded =
        godot::ResourceLoader::get_singleton()->load(
            p_path,
            godot::String("LevelResource"),
            godot::ResourceLoader::CACHE_MODE_IGNORE_DEEP);
    if (loaded.is_null()) {
        return Loaded::failure(load_failure(LoadLevelErrorCode::loader_failed));
    }

    const godot::Ref<LevelResource> level_resource =
        godot::Object::cast_to<LevelResource>(loaded.ptr());
    if (level_resource.is_null()) {
        return Loaded::failure(load_failure(LoadLevelErrorCode::wrong_resource_type));
    }

    auto compiled = compile_level_resource(level_resource, p_catalog);
    if (!compiled) {
        LoadLevelError error = load_failure(LoadLevelErrorCode::compilation_failed);
        error.compilation_error = compiled.error();
        return Loaded::failure(error);
    }

    return Loaded::success(
        LoadedLevelResource { level_resource, std::move(compiled).value() });
}

} // namespace tiles::game
