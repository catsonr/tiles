#include "game/resources/LevelPersistence.h"

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/resource_saver.hpp>
#include <godot_cpp/core/object.hpp>

#include <utility>

namespace tiles::game {

namespace {

// The only persistence formats this project uses: Godot's text and binary
// resource containers. Nothing here serializes json or a bespoke text format.
bool is_supported_extension(const godot::String &p_path) {
    const godot::String extension = p_path.get_extension().to_lower();
    return extension == godot::String("tres") || extension == godot::String("res");
}

SaveLevelError save_failure(SaveLevelErrorCode p_code, godot::Error p_error) {
    SaveLevelError error {};
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

Result<godot::String, SaveLevelError> save_level_resource(
    const godot::Ref<LevelResource> &p_resource, const godot::String &p_explicit_path) {
    using Saved = Result<godot::String, SaveLevelError>;

    if (p_resource.is_null()) {
        return Saved::failure(
            save_failure(SaveLevelErrorCode::missing_resource, godot::OK));
    }

    const bool explicit_path = !p_explicit_path.is_empty();
    const godot::String path =
        explicit_path ? p_explicit_path : p_resource->get_path();

    if (path.is_empty()) {
        return Saved::failure(save_failure(SaveLevelErrorCode::path_required, godot::OK));
    }

    if (!is_supported_extension(path)) {
        return Saved::failure(
            save_failure(SaveLevelErrorCode::unsupported_extension, godot::OK));
    }

    // An explicit path takes ownership: this is both first save and save-as. A
    // pathless save writes to the already owned path and leaves identity alone.
    const godot::BitField<godot::ResourceSaver::SaverFlags> flags =
        explicit_path
        ? godot::BitField<godot::ResourceSaver::SaverFlags>(
              godot::ResourceSaver::FLAG_CHANGE_PATH)
        : godot::BitField<godot::ResourceSaver::SaverFlags>(
              godot::ResourceSaver::FLAG_NONE);

    const godot::Error result =
        godot::ResourceSaver::get_singleton()->save(p_resource, path, flags);
    if (result != godot::OK) {
        return Saved::failure(save_failure(SaveLevelErrorCode::saver_failed, result));
    }

    if (explicit_path) {
        // The saver flag governs path resolution only for the duration of the
        // write: the pinned engine restores the resource's previous path
        // afterwards. Ownership is therefore claimed explicitly here, which is
        // what makes an explicit save both first save and save-as, and what
        // lets a later pathless save overwrite the same file.
        p_resource->take_over_path(path);
    }

    return Saved::success(path);
}

Result<LoadedLevel, LoadLevelError> load_level_resource(
    const godot::String &p_path, const content::PrototileCatalog &p_catalog) {
    using Loaded = Result<LoadedLevel, LoadLevelError>;

    if (p_path.is_empty()) {
        return Loaded::failure(load_failure(LoadLevelErrorCode::path_required));
    }

    if (!is_supported_extension(p_path)) {
        return Loaded::failure(load_failure(LoadLevelErrorCode::unsupported_extension));
    }

    // Deep cache bypass: loading a path observes its persisted contents, not an
    // older cached object or the in-memory resource just saved there.
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

    return Loaded::success(LoadedLevel { level_resource, std::move(compiled).value() });
}

} // namespace tiles::game
