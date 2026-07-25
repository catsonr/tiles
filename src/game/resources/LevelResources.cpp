#include "game/resources/LevelResources.h"

#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>

namespace tiles::game {

namespace {

// The inspector hint string for a typed array of resources, spelled from the
// pinned variant and hint enumerators rather than from hardcoded numbers.
godot::String resource_array_hint(const char *p_class_name) {
    return godot::vformat(
        "%d/%d:%s",
        static_cast<std::int64_t>(godot::Variant::OBJECT),
        static_cast<std::int64_t>(godot::PROPERTY_HINT_RESOURCE_TYPE),
        godot::String(p_class_name));
}

} // namespace

// --- palette entry ---

void PaletteEntryResource::_bind_methods() {
    godot::ClassDB::bind_method(
        godot::D_METHOD("set_prototile_id", "prototile_id"),
        &PaletteEntryResource::set_prototile_id);
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_prototile_id"), &PaletteEntryResource::get_prototile_id);
    godot::ClassDB::bind_method(
        godot::D_METHOD("set_supply", "supply"), &PaletteEntryResource::set_supply);
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_supply"), &PaletteEntryResource::get_supply);
    godot::ClassDB::bind_method(
        godot::D_METHOD("set_color", "color"), &PaletteEntryResource::set_color);
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_color"), &PaletteEntryResource::get_color);

    ADD_PROPERTY(
        godot::PropertyInfo(godot::Variant::INT, "prototile_id"),
        "set_prototile_id",
        "get_prototile_id");
    ADD_PROPERTY(
        godot::PropertyInfo(godot::Variant::INT, "supply"), "set_supply", "get_supply");
    // Opaque presentation only: the editor chooses opaque colors and the schema
    // has no alpha policy.
    ADD_PROPERTY(
        godot::PropertyInfo(
            godot::Variant::COLOR, "color", godot::PROPERTY_HINT_COLOR_NO_ALPHA),
        "set_color",
        "get_color");
}

void PaletteEntryResource::set_prototile_id(std::int64_t p_id) {
    if (prototile_id_ == p_id) {
        return;
    }
    prototile_id_ = p_id;
    emit_changed();
}

std::int64_t PaletteEntryResource::get_prototile_id() const {
    return prototile_id_;
}

void PaletteEntryResource::set_supply(std::int64_t p_supply) {
    if (supply_ == p_supply) {
        return;
    }
    supply_ = p_supply;
    emit_changed();
}

std::int64_t PaletteEntryResource::get_supply() const {
    return supply_;
}

void PaletteEntryResource::set_color(const godot::Color &p_color) {
    if (color_ == p_color) {
        return;
    }
    color_ = p_color;
    emit_changed();
}

godot::Color PaletteEntryResource::get_color() const {
    return color_;
}

// --- palette ---

void PaletteResource::_bind_methods() {
    godot::ClassDB::bind_method(
        godot::D_METHOD("set_entries", "entries"), &PaletteResource::set_entries);
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_entries"), &PaletteResource::get_entries);
    ADD_PROPERTY(
        godot::PropertyInfo(
            godot::Variant::ARRAY,
            "entries",
            godot::PROPERTY_HINT_ARRAY_TYPE,
            resource_array_hint("PaletteEntryResource")),
        "set_entries",
        "get_entries");
}

void PaletteResource::set_entries(
    const godot::TypedArray<PaletteEntryResource> &p_entries) {
    if (entries_ == p_entries) {
        return;
    }
    entries_ = p_entries;
    emit_changed();
}

godot::TypedArray<PaletteEntryResource> PaletteResource::get_entries() const {
    return entries_;
}

// --- blueprint placement ---

void BlueprintPlacementResource::_bind_methods() {
    godot::ClassDB::bind_method(
        godot::D_METHOD("set_prototile_id", "prototile_id"),
        &BlueprintPlacementResource::set_prototile_id);
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_prototile_id"),
        &BlueprintPlacementResource::get_prototile_id);
    godot::ClassDB::bind_method(
        godot::D_METHOD("set_orientation_step", "orientation_step"),
        &BlueprintPlacementResource::set_orientation_step);
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_orientation_step"),
        &BlueprintPlacementResource::get_orientation_step);
    godot::ClassDB::bind_method(
        godot::D_METHOD("set_orientation_order", "orientation_order"),
        &BlueprintPlacementResource::set_orientation_order);
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_orientation_order"),
        &BlueprintPlacementResource::get_orientation_order);
    godot::ClassDB::bind_method(
        godot::D_METHOD("set_translation_x_raw", "translation_x_raw"),
        &BlueprintPlacementResource::set_translation_x_raw);
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_translation_x_raw"),
        &BlueprintPlacementResource::get_translation_x_raw);
    godot::ClassDB::bind_method(
        godot::D_METHOD("set_translation_y_raw", "translation_y_raw"),
        &BlueprintPlacementResource::set_translation_y_raw);
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_translation_y_raw"),
        &BlueprintPlacementResource::get_translation_y_raw);

    ADD_PROPERTY(
        godot::PropertyInfo(godot::Variant::INT, "prototile_id"),
        "set_prototile_id",
        "get_prototile_id");
    ADD_PROPERTY(
        godot::PropertyInfo(godot::Variant::INT, "orientation_step"),
        "set_orientation_step",
        "get_orientation_step");
    ADD_PROPERTY(
        godot::PropertyInfo(godot::Variant::INT, "orientation_order"),
        "set_orientation_order",
        "get_orientation_order");
    ADD_PROPERTY(
        godot::PropertyInfo(godot::Variant::INT, "translation_x_raw"),
        "set_translation_x_raw",
        "get_translation_x_raw");
    ADD_PROPERTY(
        godot::PropertyInfo(godot::Variant::INT, "translation_y_raw"),
        "set_translation_y_raw",
        "get_translation_y_raw");
}

void BlueprintPlacementResource::set_prototile_id(std::int64_t p_id) {
    if (prototile_id_ == p_id) {
        return;
    }
    prototile_id_ = p_id;
    emit_changed();
}

std::int64_t BlueprintPlacementResource::get_prototile_id() const {
    return prototile_id_;
}

void BlueprintPlacementResource::set_orientation_step(std::int64_t p_step) {
    if (orientation_step_ == p_step) {
        return;
    }
    orientation_step_ = p_step;
    emit_changed();
}

std::int64_t BlueprintPlacementResource::get_orientation_step() const {
    return orientation_step_;
}

void BlueprintPlacementResource::set_orientation_order(std::int64_t p_order) {
    if (orientation_order_ == p_order) {
        return;
    }
    orientation_order_ = p_order;
    emit_changed();
}

std::int64_t BlueprintPlacementResource::get_orientation_order() const {
    return orientation_order_;
}

void BlueprintPlacementResource::set_translation_x_raw(std::int64_t p_raw) {
    if (translation_x_raw_ == p_raw) {
        return;
    }
    translation_x_raw_ = p_raw;
    emit_changed();
}

std::int64_t BlueprintPlacementResource::get_translation_x_raw() const {
    return translation_x_raw_;
}

void BlueprintPlacementResource::set_translation_y_raw(std::int64_t p_raw) {
    if (translation_y_raw_ == p_raw) {
        return;
    }
    translation_y_raw_ = p_raw;
    emit_changed();
}

std::int64_t BlueprintPlacementResource::get_translation_y_raw() const {
    return translation_y_raw_;
}

// --- level ---

void LevelResource::_bind_methods() {
    godot::ClassDB::bind_method(
        godot::D_METHOD("set_format_version", "format_version"),
        &LevelResource::set_format_version);
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_format_version"), &LevelResource::get_format_version);
    godot::ClassDB::bind_method(
        godot::D_METHOD("set_geometry_domain", "geometry_domain"),
        &LevelResource::set_geometry_domain);
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_geometry_domain"), &LevelResource::get_geometry_domain);
    godot::ClassDB::bind_method(
        godot::D_METHOD("set_palette", "palette"), &LevelResource::set_palette);
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_palette"), &LevelResource::get_palette);
    godot::ClassDB::bind_method(
        godot::D_METHOD("set_blueprint", "blueprint"), &LevelResource::set_blueprint);
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_blueprint"), &LevelResource::get_blueprint);

    ADD_PROPERTY(
        godot::PropertyInfo(godot::Variant::INT, "format_version"),
        "set_format_version",
        "get_format_version");
    ADD_PROPERTY(
        godot::PropertyInfo(godot::Variant::INT, "geometry_domain"),
        "set_geometry_domain",
        "get_geometry_domain");
    ADD_PROPERTY(
        godot::PropertyInfo(
            godot::Variant::OBJECT,
            "palette",
            godot::PROPERTY_HINT_RESOURCE_TYPE,
            "PaletteResource"),
        "set_palette",
        "get_palette");
    ADD_PROPERTY(
        godot::PropertyInfo(
            godot::Variant::ARRAY,
            "blueprint",
            godot::PROPERTY_HINT_ARRAY_TYPE,
            resource_array_hint("BlueprintPlacementResource")),
        "set_blueprint",
        "get_blueprint");
}

void LevelResource::set_format_version(std::int64_t p_version) {
    if (format_version_ == p_version) {
        return;
    }
    format_version_ = p_version;
    emit_changed();
}

std::int64_t LevelResource::get_format_version() const {
    return format_version_;
}

void LevelResource::set_geometry_domain(std::int64_t p_domain) {
    if (geometry_domain_ == p_domain) {
        return;
    }
    geometry_domain_ = p_domain;
    emit_changed();
}

std::int64_t LevelResource::get_geometry_domain() const {
    return geometry_domain_;
}

void LevelResource::set_palette(const godot::Ref<PaletteResource> &p_palette) {
    if (palette_ == p_palette) {
        return;
    }
    palette_ = p_palette;
    emit_changed();
}

godot::Ref<PaletteResource> LevelResource::get_palette() const {
    return palette_;
}

void LevelResource::set_blueprint(
    const godot::TypedArray<BlueprintPlacementResource> &p_blueprint) {
    if (blueprint_ == p_blueprint) {
        return;
    }
    blueprint_ = p_blueprint;
    emit_changed();
}

godot::TypedArray<BlueprintPlacementResource> LevelResource::get_blueprint() const {
    return blueprint_;
}

} // namespace tiles::game
