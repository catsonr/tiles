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

// --- polygon ---

void PolygonResource::_bind_methods() {
    godot::ClassDB::bind_method(
        godot::D_METHOD("set_vertices", "vertices"), &PolygonResource::set_vertices);
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_vertices"), &PolygonResource::get_vertices);
    ADD_PROPERTY(
        godot::PropertyInfo(godot::Variant::PACKED_VECTOR2_ARRAY, "vertices"),
        "set_vertices",
        "get_vertices");
}

void PolygonResource::set_vertices(const godot::PackedVector2Array &p_vertices) {
    if (vertices_ == p_vertices) {
        return;
    }
    vertices_ = p_vertices;
    emit_changed();
}

godot::PackedVector2Array PolygonResource::get_vertices() const {
    return vertices_;
}

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

// --- region ---

void RegionResource::_bind_methods() {
    godot::ClassDB::bind_method(
        godot::D_METHOD("set_outer_boundary", "outer_boundary"),
        &RegionResource::set_outer_boundary);
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_outer_boundary"), &RegionResource::get_outer_boundary);
    godot::ClassDB::bind_method(
        godot::D_METHOD("set_inner_boundaries", "inner_boundaries"),
        &RegionResource::set_inner_boundaries);
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_inner_boundaries"), &RegionResource::get_inner_boundaries);

    ADD_PROPERTY(
        godot::PropertyInfo(
            godot::Variant::OBJECT,
            "outer_boundary",
            godot::PROPERTY_HINT_RESOURCE_TYPE,
            "PolygonResource"),
        "set_outer_boundary",
        "get_outer_boundary");
    ADD_PROPERTY(
        godot::PropertyInfo(
            godot::Variant::ARRAY,
            "inner_boundaries",
            godot::PROPERTY_HINT_ARRAY_TYPE,
            resource_array_hint("PolygonResource")),
        "set_inner_boundaries",
        "get_inner_boundaries");
}

void RegionResource::set_outer_boundary(const godot::Ref<PolygonResource> &p_boundary) {
    if (outer_boundary_ == p_boundary) {
        return;
    }
    outer_boundary_ = p_boundary;
    emit_changed();
}

godot::Ref<PolygonResource> RegionResource::get_outer_boundary() const {
    return outer_boundary_;
}

void RegionResource::set_inner_boundaries(
    const godot::TypedArray<PolygonResource> &p_boundaries) {
    if (inner_boundaries_ == p_boundaries) {
        return;
    }
    inner_boundaries_ = p_boundaries;
    emit_changed();
}

godot::TypedArray<PolygonResource> RegionResource::get_inner_boundaries() const {
    return inner_boundaries_;
}

// --- level ---

void LevelResource::_bind_methods() {
    godot::ClassDB::bind_method(
        godot::D_METHOD("set_palette", "palette"), &LevelResource::set_palette);
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_palette"), &LevelResource::get_palette);
    godot::ClassDB::bind_method(
        godot::D_METHOD("set_region", "region"), &LevelResource::set_region);
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_region"), &LevelResource::get_region);

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
            godot::Variant::OBJECT,
            "region",
            godot::PROPERTY_HINT_RESOURCE_TYPE,
            "RegionResource"),
        "set_region",
        "get_region");
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

void LevelResource::set_region(const godot::Ref<RegionResource> &p_region) {
    if (region_ == p_region) {
        return;
    }
    region_ = p_region;
    emit_changed();
}

godot::Ref<RegionResource> LevelResource::get_region() const {
    return region_;
}

} // namespace tiles::game
