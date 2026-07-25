#include "game/testing/ResourceIntegrationRunner.h"

#include "content/PrototileCatalog.h"
#include "core/Orientation.h"
#include "core/OrientedPrototile.h"
#include "core/geometry/Coordinate.h"
#include "core/geometry/Point.h"
#include "core/geometry/Polygon.h"
#include "engine/Supply.h"
#include "game/resources/LevelPersistence.h"
#include "game/resources/ResourceCompiler.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/resource_saver.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <utility>
#include <vector>

namespace tiles::game {

namespace {

// The one narrow generated directory this runner owns. Cleanup only ever names
// exact files beneath it, then removes the directory itself if it is empty.
// Nothing recursive and nothing derived from unchecked input is ever removed.
const char *TEMPORARY_DIRECTORY = "res://.godot/tiles_resource_integration";

const char *FIXTURE_PATH = "res://tests/fixtures/canonical_level.tres";

godot::String temporary_path(const char *p_file) {
    return godot::String(TEMPORARY_DIRECTORY).path_join(godot::String(p_file));
}

godot::Ref<PolygonResource> make_polygon(
    std::initializer_list<godot::Vector2> p_vertices) {
    godot::Ref<PolygonResource> polygon;
    polygon.instantiate();
    godot::PackedVector2Array vertices;
    for (const godot::Vector2 &vertex : p_vertices) {
        vertices.push_back(vertex);
    }
    polygon->set_vertices(vertices);
    return polygon;
}

godot::Ref<PaletteEntryResource> make_entry(
    std::int64_t p_id, std::int64_t p_supply, const godot::Color &p_color) {
    godot::Ref<PaletteEntryResource> entry;
    entry.instantiate();
    entry->set_prototile_id(p_id);
    entry->set_supply(p_supply);
    entry->set_color(p_color);
    return entry;
}

godot::Ref<PaletteResource> make_palette(
    const godot::TypedArray<PaletteEntryResource> &p_entries) {
    godot::Ref<PaletteResource> palette;
    palette.instantiate();
    palette->set_entries(p_entries);
    return palette;
}

godot::Ref<RegionResource> make_region(
    const godot::Ref<PolygonResource> &p_outer,
    const godot::TypedArray<PolygonResource> &p_holes) {
    godot::Ref<RegionResource> region;
    region.instantiate();
    region->set_outer_boundary(p_outer);
    region->set_inner_boundaries(p_holes);
    return region;
}

godot::Ref<LevelResource> make_level(
    const godot::Ref<PaletteResource> &p_palette,
    const godot::Ref<RegionResource> &p_region) {
    godot::Ref<LevelResource> level;
    level.instantiate();
    level->set_palette(p_palette);
    level->set_region(p_region);
    return level;
}

// A valid two-hole region on exactly the fixture's geometry, built
// programmatically so compiled equivalence can be compared against the
// text-authored one.
godot::Ref<RegionResource> make_fixture_region() {
    godot::TypedArray<PolygonResource> holes;
    holes.push_back(make_polygon({
        godot::Vector2(2.0f, 2.0f),
        godot::Vector2(4.0f, 2.0f),
        godot::Vector2(4.0f, 4.0f),
        godot::Vector2(2.0f, 4.0f),
    }));
    holes.push_back(make_polygon({
        godot::Vector2(6.0f, 1.5f),
        godot::Vector2(8.0f, 1.5f),
        godot::Vector2(8.0f, 3.5f),
        godot::Vector2(6.0f, 3.5f),
    }));
    return make_region(
        make_polygon({
            godot::Vector2(0.0f, 0.0f),
            godot::Vector2(10.0f, 0.0f),
            godot::Vector2(10.0f, 6.5f),
            godot::Vector2(0.0f, 6.5f),
        }),
        holes);
}

// A whole-game-unit coordinate, for comparing compiled vertices exactly.
Coordinate units(std::int64_t p_units) {
    return Coordinate::from_raw(p_units * Coordinate::SCALE);
}

// A half-game-unit coordinate, so the fixture's fractional vertices are
// compared against exact expected lattice values rather than a tolerance.
Coordinate half_units(std::int64_t p_halves) {
    return Coordinate::from_raw(p_halves * (Coordinate::SCALE / 2));
}

// The property entry Godot reports for one name, or an empty dictionary.
godot::Dictionary property_info(godot::Object *p_object, const char *p_name) {
    const godot::TypedArray<godot::Dictionary> properties = p_object->get_property_list();
    for (std::int64_t i = 0; i < properties.size(); ++i) {
        const godot::Dictionary entry = properties[i];
        if (godot::String(entry["name"]) == godot::String(p_name)) {
            return entry;
        }
    }
    return godot::Dictionary();
}

bool property_has_type(
    godot::Object *p_object, const char *p_name, godot::Variant::Type p_type) {
    const godot::Dictionary info = property_info(p_object, p_name);
    if (info.is_empty()) {
        return false;
    }
    return static_cast<godot::Variant::Type>(static_cast<std::int64_t>(info["type"]))
        == p_type;
}

bool property_has_hint(
    godot::Object *p_object, const char *p_name, godot::PropertyHint p_hint) {
    const godot::Dictionary info = property_info(p_object, p_name);
    if (info.is_empty()) {
        return false;
    }
    return static_cast<godot::PropertyHint>(static_cast<std::int64_t>(info["hint"]))
        == p_hint;
}

bool typed_as(const godot::Array &p_array, const char *p_class_name) {
    return p_array.is_typed()
        && p_array.get_typed_builtin() == static_cast<std::int64_t>(godot::Variant::OBJECT)
        && p_array.get_typed_class_name() == godot::StringName(p_class_name);
}

// Exact vertex-by-vertex comparison of two authored rings.
bool same_authored_vertices(
    const godot::PackedVector2Array &p_lhs, const godot::PackedVector2Array &p_rhs) {
    if (p_lhs.size() != p_rhs.size()) {
        return false;
    }
    for (std::int64_t i = 0; i < p_lhs.size(); ++i) {
        if (p_lhs[i] != p_rhs[i]) {
            return false;
        }
    }
    return true;
}

} // namespace

void ResourceIntegrationRunner::_bind_methods() {
    godot::ClassDB::bind_method(
        godot::D_METHOD("on_resource_changed"),
        &ResourceIntegrationRunner::on_resource_changed);
}

void ResourceIntegrationRunner::on_resource_changed() {
    ++changed_notifications_;
}

bool ResourceIntegrationRunner::expect(bool p_condition, const char *p_description) {
    ++checks_;
    if (!p_condition) {
        ++failures_;
        godot::UtilityFunctions::push_error(
            "[tiles] resource integration failed: ", p_description);
    }
    return p_condition;
}

// --- resource graph ---

void ResourceIntegrationRunner::check_resource_defaults() {
    godot::Ref<PolygonResource> polygon;
    polygon.instantiate();
    expect(polygon.is_valid(), "PolygonResource instantiates");
    expect(polygon->get_vertices().is_empty(), "PolygonResource vertices default empty");

    godot::Ref<PaletteEntryResource> entry;
    entry.instantiate();
    expect(entry.is_valid(), "PaletteEntryResource instantiates");
    expect(entry->get_prototile_id() == 0, "PaletteEntryResource prototile_id defaults to 0");
    expect(entry->get_supply() == -1, "PaletteEntryResource supply defaults to -1");
    expect(
        entry->get_color() == godot::Color(1.0f, 1.0f, 1.0f, 1.0f),
        "PaletteEntryResource color defaults to opaque white");

    godot::Ref<PaletteResource> palette;
    palette.instantiate();
    expect(palette.is_valid(), "PaletteResource instantiates");
    expect(palette->get_entries().is_empty(), "PaletteResource entries default empty");
    expect(
        typed_as(palette->get_entries(), "PaletteEntryResource"),
        "default PaletteResource entries stay typed");

    godot::Ref<RegionResource> region;
    region.instantiate();
    expect(region.is_valid(), "RegionResource instantiates");
    expect(region->get_outer_boundary().is_null(), "RegionResource outer_boundary defaults null");
    expect(
        region->get_inner_boundaries().is_empty(),
        "RegionResource inner_boundaries default empty");
    expect(
        typed_as(region->get_inner_boundaries(), "PolygonResource"),
        "default RegionResource inner_boundaries stay typed");

    godot::Ref<LevelResource> level;
    level.instantiate();
    expect(level.is_valid(), "LevelResource instantiates");
    expect(level->get_palette().is_null(), "LevelResource palette defaults null");
    expect(level->get_region().is_null(), "LevelResource region defaults null");

    // Setters and getters preserve values, and typed arrays survive assignment.
    const godot::PackedVector2Array vertices = make_polygon({
        godot::Vector2(0.0f, 0.0f),
        godot::Vector2(1.0f, 0.0f),
        godot::Vector2(1.0f, 1.0f),
    })->get_vertices();
    polygon->set_vertices(vertices);
    expect(
        same_authored_vertices(polygon->get_vertices(), vertices),
        "PolygonResource preserves assigned vertices");

    entry->set_prototile_id(7);
    entry->set_supply(4);
    entry->set_color(godot::Color(0.25f, 0.5f, 0.75f, 1.0f));
    expect(entry->get_prototile_id() == 7, "PaletteEntryResource preserves prototile_id");
    expect(entry->get_supply() == 4, "PaletteEntryResource preserves supply");
    expect(
        entry->get_color() == godot::Color(0.25f, 0.5f, 0.75f, 1.0f),
        "PaletteEntryResource preserves color");

    godot::TypedArray<PaletteEntryResource> entries;
    entries.push_back(entry);
    palette->set_entries(entries);
    expect(palette->get_entries().size() == 1, "PaletteResource preserves entry count");
    expect(
        typed_as(palette->get_entries(), "PaletteEntryResource"),
        "assigned PaletteResource entries stay typed");

    godot::TypedArray<PolygonResource> holes;
    holes.push_back(polygon);
    region->set_outer_boundary(polygon);
    region->set_inner_boundaries(holes);
    expect(
        region->get_outer_boundary() == polygon, "RegionResource preserves outer_boundary");
    expect(region->get_inner_boundaries().size() == 1, "RegionResource preserves hole count");
    expect(
        typed_as(region->get_inner_boundaries(), "PolygonResource"),
        "assigned RegionResource inner_boundaries stay typed");

    level->set_palette(palette);
    level->set_region(region);
    expect(level->get_palette() == palette, "LevelResource preserves palette");
    expect(level->get_region() == region, "LevelResource preserves region");
}

void ResourceIntegrationRunner::check_property_metadata() {
    godot::Ref<PolygonResource> polygon;
    polygon.instantiate();
    expect(
        property_has_type(
            polygon.ptr(), "vertices", godot::Variant::PACKED_VECTOR2_ARRAY),
        "PolygonResource.vertices is a PackedVector2Array property");

    godot::Ref<PaletteEntryResource> entry;
    entry.instantiate();
    expect(
        property_has_type(entry.ptr(), "prototile_id", godot::Variant::INT),
        "PaletteEntryResource.prototile_id is an int property");
    expect(
        property_has_type(entry.ptr(), "supply", godot::Variant::INT),
        "PaletteEntryResource.supply is an int property");
    expect(
        property_has_type(entry.ptr(), "color", godot::Variant::COLOR),
        "PaletteEntryResource.color is a Color property");
    expect(
        property_has_hint(entry.ptr(), "color", godot::PROPERTY_HINT_COLOR_NO_ALPHA),
        "PaletteEntryResource.color uses the no-alpha color hint");

    godot::Ref<PaletteResource> palette;
    palette.instantiate();
    expect(
        property_has_type(palette.ptr(), "entries", godot::Variant::ARRAY),
        "PaletteResource.entries is an array property");
    expect(
        property_has_hint(palette.ptr(), "entries", godot::PROPERTY_HINT_ARRAY_TYPE),
        "PaletteResource.entries uses the typed-array hint");

    godot::Ref<RegionResource> region;
    region.instantiate();
    expect(
        property_has_type(region.ptr(), "outer_boundary", godot::Variant::OBJECT),
        "RegionResource.outer_boundary is an object property");
    expect(
        property_has_hint(
            region.ptr(), "outer_boundary", godot::PROPERTY_HINT_RESOURCE_TYPE),
        "RegionResource.outer_boundary uses the resource-type hint");
    expect(
        property_has_hint(
            region.ptr(), "inner_boundaries", godot::PROPERTY_HINT_ARRAY_TYPE),
        "RegionResource.inner_boundaries uses the typed-array hint");

    godot::Ref<LevelResource> level;
    level.instantiate();
    expect(
        property_has_hint(level.ptr(), "palette", godot::PROPERTY_HINT_RESOURCE_TYPE),
        "LevelResource.palette uses the resource-type hint");
    expect(
        property_has_hint(level.ptr(), "region", godot::PROPERTY_HINT_RESOURCE_TYPE),
        "LevelResource.region uses the resource-type hint");
}

void ResourceIntegrationRunner::check_setter_notifications() {
    godot::Ref<PaletteEntryResource> entry;
    entry.instantiate();
    entry->connect(
        godot::StringName("changed"),
        godot::Callable(this, godot::StringName("on_resource_changed")));

    // emit_changed() is synchronous, so the count is read immediately and the
    // check makes no timing assumption.
    changed_notifications_ = 0;
    entry->set_prototile_id(5);
    expect(changed_notifications_ == 1, "an actual prototile_id change emits changed once");
    entry->set_prototile_id(5);
    expect(changed_notifications_ == 1, "assigning the same prototile_id emits nothing");

    entry->set_supply(2);
    expect(changed_notifications_ == 2, "an actual supply change emits changed once");
    entry->set_supply(2);
    expect(changed_notifications_ == 2, "assigning the same supply emits nothing");

    entry->set_color(godot::Color(0.5f, 0.5f, 0.5f, 1.0f));
    expect(changed_notifications_ == 3, "an actual color change emits changed once");
    entry->set_color(godot::Color(0.5f, 0.5f, 0.5f, 1.0f));
    expect(changed_notifications_ == 3, "assigning the same color emits nothing");

    entry->disconnect(
        godot::StringName("changed"),
        godot::Callable(this, godot::StringName("on_resource_changed")));
}

// --- compilation ---

void ResourceIntegrationRunner::check_polygon_compilation() {
    {
        auto compiled = compile_polygon_resource(godot::Ref<PolygonResource>());
        if (expect(!compiled, "a null polygon resource fails to compile")) {
            expect(
                compiled.error().code == PolygonResourceErrorCode::missing_resource,
                "a null polygon resource reports missing_resource");
            expect(
                !compiled.error().vertex.has_value()
                    && !compiled.error().axis.has_value()
                    && !compiled.error().quantization_error.has_value()
                    && !compiled.error().polygon_error.has_value(),
                "missing_resource carries no payload");
        }
    }

    {
        // Safely quantizable fractional coordinates compile exactly: resource
        // geometry is not restricted to the editor's later integer-grid policy.
        auto compiled = compile_polygon_resource(make_polygon({
            godot::Vector2(0.0f, 0.0f),
            godot::Vector2(1.5f, 0.0f),
            godot::Vector2(1.5f, 2.5f),
            godot::Vector2(0.0f, 2.5f),
        }));
        if (expect(bool(compiled), "fractional polygon coordinates compile")) {
            const Polygon::Vertices &vertices = compiled.value().vertices();
            expect(vertices.size() == 4, "fractional polygon keeps four vertices");
            bool exact = vertices.size() == 4;
            if (exact) {
                exact = vertices[0] == Point { units(0), units(0) }
                    && vertices[1] == Point { half_units(3), units(0) }
                    && vertices[2] == Point { half_units(3), half_units(5) }
                    && vertices[3] == Point { units(0), half_units(5) };
            }
            expect(exact, "fractional polygon quantizes onto exact half-unit lattice values");
        }
    }

    {
        const float non_finite = std::numeric_limits<float>::infinity();
        auto compiled = compile_polygon_resource(make_polygon({
            godot::Vector2(0.0f, 0.0f),
            godot::Vector2(non_finite, 0.0f),
            godot::Vector2(1.0f, 1.0f),
        }));
        if (expect(!compiled, "a non-finite vertex component fails to compile")) {
            const PolygonResourceError &error = compiled.error();
            expect(
                error.code == PolygonResourceErrorCode::coordinate_quantization_failed,
                "a non-finite component reports coordinate_quantization_failed");
            expect(
                error.vertex.has_value() && error.vertex.value() == 1,
                "quantization failure names the authored vertex index");
            expect(
                error.axis.has_value() && error.axis.value() == CoordinateAxis::x,
                "quantization failure names the x axis");
            expect(
                error.quantization_error.has_value()
                    && error.quantization_error.value() == QuantizationError::non_finite,
                "quantization failure preserves QuantizationError::non_finite");
        }
    }

    {
        // x is quantized before y, so a vertex whose components both fail
        // reports the x axis.
        const float non_finite = std::numeric_limits<float>::infinity();
        auto compiled = compile_polygon_resource(make_polygon({
            godot::Vector2(non_finite, non_finite),
            godot::Vector2(1.0f, 0.0f),
            godot::Vector2(1.0f, 1.0f),
        }));
        if (expect(!compiled, "a doubly non-finite vertex fails to compile")) {
            expect(
                compiled.error().axis.has_value()
                    && compiled.error().axis.value() == CoordinateAxis::x,
                "x is quantized before y");
        }
    }

    {
        auto compiled = compile_polygon_resource(make_polygon({
            godot::Vector2(0.0f, 0.0f),
            godot::Vector2(0.0f, 1.0e30f),
            godot::Vector2(1.0f, 1.0f),
        }));
        if (expect(!compiled, "an out-of-range vertex component fails to compile")) {
            const PolygonResourceError &error = compiled.error();
            expect(
                error.axis.has_value() && error.axis.value() == CoordinateAxis::y,
                "an out-of-range y component names the y axis");
            expect(
                error.quantization_error.has_value()
                    && error.quantization_error.value() == QuantizationError::out_of_range,
                "quantization failure preserves QuantizationError::out_of_range");
        }
    }

    {
        auto compiled = compile_polygon_resource(make_polygon({
            godot::Vector2(0.0f, 0.0f),
            godot::Vector2(1.0f, 0.0f),
        }));
        if (expect(!compiled, "a two-vertex ring fails to compile")) {
            const PolygonResourceError &error = compiled.error();
            expect(
                error.code == PolygonResourceErrorCode::polygon_construction_failed,
                "an invalid ring reports polygon_construction_failed");
            expect(
                error.polygon_error.has_value()
                    && error.polygon_error.value() == PolygonError::too_few_vertices,
                "polygon construction failure preserves the complete PolygonError");
            expect(
                !error.vertex.has_value() && !error.quantization_error.has_value(),
                "polygon_construction_failed carries only its polygon error");
        }
    }
}

void ResourceIntegrationRunner::check_palette_compilation(
    const content::PrototileCatalog &p_catalog) {
    const godot::Color white(1.0f, 1.0f, 1.0f, 1.0f);

    {
        auto compiled = compile_palette_resource(godot::Ref<PaletteResource>(), p_catalog);
        if (expect(!compiled, "a null palette resource fails to compile")) {
            expect(
                compiled.error().code == PaletteResourceErrorCode::missing_resource,
                "a null palette resource reports missing_resource");
        }
    }

    {
        godot::TypedArray<PaletteEntryResource> entries;
        entries.push_back(make_entry(1, -1, white));
        entries.push_back(godot::Ref<PaletteEntryResource>());
        auto compiled = compile_palette_resource(make_palette(entries), p_catalog);
        if (expect(!compiled, "a null palette entry fails to compile")) {
            expect(
                compiled.error().code == PaletteResourceErrorCode::missing_entry,
                "a null palette entry reports missing_entry");
            expect(
                compiled.error().entry.has_value() && compiled.error().entry.value() == 1,
                "a null palette entry reports its authored index");
        }
    }

    {
        godot::TypedArray<PaletteEntryResource> entries;
        entries.push_back(make_entry(1, -1, white));
        entries.push_back(make_entry(-3, -1, white));
        auto compiled = compile_palette_resource(make_palette(entries), p_catalog);
        if (expect(!compiled, "a negative prototile id fails to compile")) {
            const PaletteResourceError &error = compiled.error();
            expect(
                error.code == PaletteResourceErrorCode::negative_prototile_id,
                "a negative id reports negative_prototile_id");
            expect(
                error.entry.has_value() && error.entry.value() == 1,
                "a negative id reports its authored entry index");
            expect(
                error.encoded_prototile_id.has_value()
                    && error.encoded_prototile_id.value() == -3,
                "a negative id reports its signed encoded value");
            expect(
                !error.prototile_id.has_value(),
                "a negative id is rejected before unsigned conversion");
        }
    }

    {
        // Id 0 is a valid encoding: it reaches canonical lookup and fails there,
        // not as a negative-id error.
        godot::TypedArray<PaletteEntryResource> entries;
        entries.push_back(make_entry(0, -1, white));
        auto compiled = compile_palette_resource(make_palette(entries), p_catalog);
        if (expect(!compiled, "prototile id 0 fails against the shipped catalog")) {
            const PaletteResourceError &error = compiled.error();
            expect(
                error.code == PaletteResourceErrorCode::unknown_prototile_id,
                "prototile id 0 reports unknown_prototile_id");
            expect(
                error.encoded_prototile_id.has_value()
                    && error.encoded_prototile_id.value() == 0,
                "prototile id 0 reports its encoded value");
            expect(
                error.prototile_id.has_value()
                    && error.prototile_id.value() == PrototileId(0),
                "prototile id 0 reaches lookup as a strong id");
        }
    }

    {
        godot::TypedArray<PaletteEntryResource> entries;
        entries.push_back(make_entry(1, -1, white));
        entries.push_back(make_entry(999, -1, white));
        auto compiled = compile_palette_resource(make_palette(entries), p_catalog);
        if (expect(!compiled, "an unknown positive prototile id fails to compile")) {
            const PaletteResourceError &error = compiled.error();
            expect(
                error.code == PaletteResourceErrorCode::unknown_prototile_id,
                "an unknown positive id reports unknown_prototile_id");
            expect(
                error.entry.has_value() && error.entry.value() == 1,
                "an unknown id reports its authored entry index");
            expect(
                error.prototile_id.has_value()
                    && error.prototile_id.value() == PrototileId(999),
                "an unknown id reports its strong id");
        }
    }

    {
        const std::int64_t invalid_supplies[2] = { 0, -2 };
        for (const std::int64_t supply : invalid_supplies) {
            godot::TypedArray<PaletteEntryResource> entries;
            entries.push_back(make_entry(1, supply, white));
            auto compiled = compile_palette_resource(make_palette(entries), p_catalog);
            if (expect(!compiled, "an invalid supply encoding fails to compile")) {
                const PaletteResourceError &error = compiled.error();
                expect(
                    error.code == PaletteResourceErrorCode::invalid_supply,
                    "an invalid supply reports invalid_supply");
                expect(
                    error.encoded_supply.has_value()
                        && error.encoded_supply.value() == supply,
                    "an invalid supply reports its signed encoding");
                expect(
                    error.prototile_id.has_value()
                        && error.prototile_id.value() == PrototileId(1),
                    "an invalid supply reports the resolved strong id");
            }
        }
    }

    {
        godot::TypedArray<PaletteEntryResource> entries;
        auto compiled = compile_palette_resource(make_palette(entries), p_catalog);
        if (expect(!compiled, "an empty palette fails to compile")) {
            expect(
                compiled.error().code
                    == PaletteResourceErrorCode::palette_construction_failed,
                "an empty palette reports palette_construction_failed");
            expect(
                compiled.error().palette_error.has_value()
                    && compiled.error().palette_error.value() == engine::PaletteError::empty,
                "an empty palette preserves PaletteError::empty");
        }
    }

    {
        godot::TypedArray<PaletteEntryResource> entries;
        entries.push_back(make_entry(1, -1, white));
        entries.push_back(make_entry(1, 2, white));
        auto compiled = compile_palette_resource(make_palette(entries), p_catalog);
        if (expect(!compiled, "a duplicate prototile id fails to compile")) {
            expect(
                compiled.error().palette_error.has_value()
                    && compiled.error().palette_error.value()
                        == engine::PaletteError::duplicate_prototile_id,
                "a duplicate id preserves PaletteError::duplicate_prototile_id");
        }
    }

    {
        godot::TypedArray<PaletteEntryResource> entries;
        entries.push_back(make_entry(1, 3, white));
        entries.push_back(make_entry(2, -1, white));
        auto compiled = compile_palette_resource(make_palette(entries), p_catalog);
        if (expect(bool(compiled), "a valid palette compiles")) {
            const engine::Palette &palette = compiled.value();
            expect(palette.order() == 2, "authored palette order is preserved");
            if (palette.entries().size() == 2) {
                expect(
                    palette.entries()[0].prototile().id() == PrototileId(1)
                        && palette.entries()[1].prototile().id() == PrototileId(2),
                    "authored entry order is preserved");
                expect(
                    palette.entries()[0].supply().finite_amount().has_value()
                        && palette.entries()[0].supply().finite_amount().value() == 3,
                    "a positive supply compiles to that finite amount");
                expect(
                    palette.entries()[1].supply().is_unlimited(),
                    "supply -1 compiles to unlimited");
                expect(
                    palette.entries()[0].orientations().size() == 1,
                    "the o tetromino exposes one distinct orientation");
                expect(
                    palette.entries()[1].orientations().size() == 2,
                    "the i tetromino exposes two distinct orientations");
                const content::CanonicalPrototile *canonical =
                    p_catalog.find(PrototileId(1));
                expect(
                    canonical != nullptr
                        && same_boundary(
                            palette.entries()[0].prototile().polygon(),
                            canonical->prototile().polygon()),
                    "compiled palette geometry is the catalog-owned exact prototile");
            }
        }
    }

    {
        // Color is presentation only: two palettes differing solely in color
        // compile to equivalent exact palettes.
        godot::TypedArray<PaletteEntryResource> plain;
        plain.push_back(make_entry(3, 2, white));
        godot::TypedArray<PaletteEntryResource> colored;
        colored.push_back(make_entry(3, 2, godot::Color(0.25f, 0.75f, 0.5f, 1.0f)));

        auto first = compile_palette_resource(make_palette(plain), p_catalog);
        auto second = compile_palette_resource(make_palette(colored), p_catalog);
        if (expect(
                bool(first) && bool(second),
                "palettes differing only in color both compile")) {
            expect(
                same_palette(first.value(), second.value()),
                "palette compilation ignores authored color");
        }
    }
}

void ResourceIntegrationRunner::check_region_compilation() {
    {
        auto compiled = compile_region_resource(godot::Ref<RegionResource>());
        if (expect(!compiled, "a null region resource fails to compile")) {
            expect(
                compiled.error().code == RegionResourceErrorCode::missing_resource,
                "a null region resource reports missing_resource");
        }
    }

    {
        auto compiled = compile_region_resource(make_region(
            godot::Ref<PolygonResource>(), godot::TypedArray<PolygonResource>()));
        if (expect(!compiled, "an absent outer boundary fails to compile")) {
            const RegionResourceError &error = compiled.error();
            expect(
                error.code == RegionResourceErrorCode::outer_boundary_invalid,
                "an absent outer boundary reports outer_boundary_invalid");
            expect(
                error.polygon_error.has_value()
                    && error.polygon_error.value().code
                        == PolygonResourceErrorCode::missing_resource,
                "an absent outer boundary nests PolygonResourceError::missing_resource");
            expect(!error.hole.has_value(), "an outer-boundary failure names no hole");
        }
    }

    {
        // The outer boundary is compiled before any hole.
        godot::TypedArray<PolygonResource> holes;
        holes.push_back(godot::Ref<PolygonResource>());
        auto compiled = compile_region_resource(
            make_region(godot::Ref<PolygonResource>(), holes));
        if (expect(!compiled, "a region with neither valid boundary fails")) {
            expect(
                compiled.error().code == RegionResourceErrorCode::outer_boundary_invalid,
                "the outer boundary is compiled before holes");
        }
    }

    {
        godot::TypedArray<PolygonResource> holes;
        holes.push_back(make_polygon({
            godot::Vector2(2.0f, 2.0f),
            godot::Vector2(3.0f, 2.0f),
            godot::Vector2(3.0f, 3.0f),
            godot::Vector2(2.0f, 3.0f),
        }));
        holes.push_back(godot::Ref<PolygonResource>());
        auto compiled = compile_region_resource(make_region(
            make_polygon({
                godot::Vector2(0.0f, 0.0f),
                godot::Vector2(8.0f, 0.0f),
                godot::Vector2(8.0f, 8.0f),
                godot::Vector2(0.0f, 8.0f),
            }),
            holes));
        if (expect(!compiled, "a null hole fails to compile")) {
            const RegionResourceError &error = compiled.error();
            expect(
                error.code == RegionResourceErrorCode::inner_boundary_invalid,
                "a null hole reports inner_boundary_invalid");
            expect(
                error.hole.has_value() && error.hole.value() == 1,
                "a null hole reports its authored index");
            expect(
                error.polygon_error.has_value()
                    && error.polygon_error.value().code
                        == PolygonResourceErrorCode::missing_resource,
                "a null hole nests the complete polygon error");
        }
    }

    {
        // A hole outside the outer boundary is a relationship failure among
        // otherwise valid polygons.
        godot::TypedArray<PolygonResource> holes;
        holes.push_back(make_polygon({
            godot::Vector2(20.0f, 20.0f),
            godot::Vector2(21.0f, 20.0f),
            godot::Vector2(21.0f, 21.0f),
            godot::Vector2(20.0f, 21.0f),
        }));
        auto compiled = compile_region_resource(make_region(
            make_polygon({
                godot::Vector2(0.0f, 0.0f),
                godot::Vector2(8.0f, 0.0f),
                godot::Vector2(8.0f, 8.0f),
                godot::Vector2(0.0f, 8.0f),
            }),
            holes));
        if (expect(!compiled, "a hole outside the outer boundary fails to compile")) {
            const RegionResourceError &error = compiled.error();
            expect(
                error.code == RegionResourceErrorCode::region_construction_failed,
                "a relationship failure reports region_construction_failed");
            expect(
                error.region_error.has_value()
                    && error.region_error.value().code
                        == RegionErrorCode::hole_not_strictly_inside_outer,
                "region construction failure preserves the complete RegionError");
            expect(
                error.region_error.has_value() && error.region_error.value().first_hole == 0,
                "region construction failure keeps its deterministic hole index");
        }
    }

    {
        auto compiled = compile_region_resource(make_fixture_region());
        if (expect(bool(compiled), "a valid two-hole region compiles")) {
            const Region &region = compiled.value();
            expect(
                region.inner_boundaries().size() == 2, "both holes compile in order");
            // Exact doubled area: 2 * (10 * 6.5 - 2 * 2 - 2 * 2) = 114 square
            // game units, compared as an exact q16.48 doubled area.
            expect(
                region.doubled_area()
                    == Int256::multiply(
                        static_cast<__int128>(114) * Coordinate::SCALE, Coordinate::SCALE),
                "region doubled area is exact");
            if (region.inner_boundaries().size() == 2) {
                expect(
                    region.inner_boundaries()[0].vertices().front()
                            == Point { units(2), units(2) }
                        && region.inner_boundaries()[1].vertices().front()
                            == Point { units(6), half_units(3) },
                    "authored hole order is preserved exactly");
            }
        }
    }
}

void ResourceIntegrationRunner::check_level_compilation(
    const content::PrototileCatalog &p_catalog) {
    const godot::Color white(1.0f, 1.0f, 1.0f, 1.0f);

    {
        auto compiled = compile_level_resource(godot::Ref<LevelResource>(), p_catalog);
        if (expect(!compiled, "a null level resource fails to compile")) {
            expect(
                compiled.error().code == LevelResourceErrorCode::missing_resource,
                "a null level resource reports missing_resource");
        }
    }

    {
        // Palette before region: with both absent, only the palette error is
        // reported.
        auto compiled = compile_level_resource(
            make_level(godot::Ref<PaletteResource>(), godot::Ref<RegionResource>()),
            p_catalog);
        if (expect(!compiled, "a level with neither member fails to compile")) {
            expect(
                compiled.error().code == LevelResourceErrorCode::palette_invalid,
                "the palette is compiled before the region");
            expect(
                compiled.error().palette_error.has_value()
                    && !compiled.error().region_error.has_value(),
                "a palette failure populates only the palette error");
        }
    }

    {
        godot::TypedArray<PaletteEntryResource> entries;
        entries.push_back(make_entry(1, -1, white));
        auto compiled = compile_level_resource(
            make_level(make_palette(entries), godot::Ref<RegionResource>()), p_catalog);
        if (expect(!compiled, "a level with no region fails to compile")) {
            expect(
                compiled.error().code == LevelResourceErrorCode::region_invalid,
                "an absent region reports region_invalid");
            expect(
                compiled.error().region_error.has_value()
                    && !compiled.error().palette_error.has_value(),
                "a region failure populates only the region error");
        }
    }

    {
        godot::TypedArray<PaletteEntryResource> entries;
        entries.push_back(make_entry(1, 3, white));
        entries.push_back(make_entry(2, -1, white));
        auto compiled = compile_level_resource(
            make_level(make_palette(entries), make_fixture_region()), p_catalog);
        if (expect(bool(compiled), "a complete valid level compiles")) {
            expect(
                compiled.value().palette().order() == 2,
                "the compiled level keeps its palette order");
            expect(
                compiled.value().region().inner_boundaries().size() == 2,
                "the compiled level keeps its region holes");
        }
    }
}

// --- authored fixture ---

void ResourceIntegrationRunner::check_authored_fixture(
    const content::PrototileCatalog &p_catalog) {
    auto loaded = load_level_resource(godot::String(FIXTURE_PATH), p_catalog);
    if (!expect(bool(loaded), "the authored fixture loads and compiles")) {
        return;
    }

    const LoadedLevel &level = loaded.value();

    // The authored graph is restored as the correct custom types.
    if (expect(level.resource->get_palette().is_valid(), "the fixture has a palette resource")
        && expect(
            level.resource->get_region().is_valid(), "the fixture has a region resource")) {
        const godot::TypedArray<PaletteEntryResource> entries =
            level.resource->get_palette()->get_entries();
        if (expect(entries.size() == 2, "the fixture palette has two authored entries")) {
            const godot::Ref<PaletteEntryResource> first = entries[0];
            const godot::Ref<PaletteEntryResource> second = entries[1];
            if (expect(
                    first.is_valid() && second.is_valid(),
                    "the fixture palette entries reload as PaletteEntryResource")) {
                expect(
                    first->get_prototile_id() == 1 && first->get_supply() == 3,
                    "the fixture's first entry is canonical id 1 with finite supply 3");
                expect(
                    second->get_prototile_id() == 2 && second->get_supply() == -1,
                    "the fixture's second entry is canonical id 2 with unlimited supply");
                expect(
                    first->get_color() == godot::Color(0.75f, 0.5f, 0.25f, 1.0f)
                        && second->get_color() == godot::Color(0.25f, 0.75f, 0.5f, 1.0f),
                    "the fixture's authored per-entry colors survive loading");
                expect(
                    !(first->get_color() == second->get_color()),
                    "the fixture's entries carry different colors");
            }
        }

        expect(
            level.resource->get_region()->get_inner_boundaries().size() == 2,
            "the fixture region reloads with both holes");
    }

    // The compiled exact level.
    const engine::Palette &palette = level.level.palette();
    if (expect(palette.order() == 2, "the fixture compiles to a two-entry palette")) {
        const content::CanonicalPrototile *o = p_catalog.find(PrototileId(1));
        const content::CanonicalPrototile *i = p_catalog.find(PrototileId(2));
        if (expect(
                o != nullptr && i != nullptr,
                "the fixture's ids resolve in the canonical catalog")) {
            expect(
                same_boundary(palette.entries()[0].prototile().polygon(), o->prototile().polygon())
                    && same_boundary(
                        palette.entries()[1].prototile().polygon(), i->prototile().polygon()),
                "the fixture's palette geometry is the canonical exact geometry");
            // Display names come from the catalog by runtime palette id; color
            // comes from the authored resource. Neither is in the exact level.
            expect(
                !o->display_name().empty() && !i->display_name().empty(),
                "the fixture's presentation names are obtainable from the catalog");
        }
        expect(
            palette.entries()[0].supply().finite_amount().has_value()
                && palette.entries()[0].supply().finite_amount().value() == 3,
            "the fixture's finite supply compiles exactly");
        expect(
            palette.entries()[1].supply().is_unlimited(),
            "the fixture's unlimited supply compiles exactly");
    }

    const Region &region = level.level.region();
    expect(
        region.outer_boundary().vertices().size() == 4,
        "the fixture's outer boundary compiles to four vertices");
    expect(
        region.outer_boundary().vertices().front() == Point { units(0), units(0) },
        "the fixture's outer boundary begins at its exact canonical vertex");
    if (expect(region.inner_boundaries().size() == 2, "the fixture has two ordered holes")) {
        expect(
            region.inner_boundaries()[0].vertices().front() == Point { units(2), units(2) }
                && region.inner_boundaries()[1].vertices().front()
                    == Point { units(6), half_units(3) },
            "the fixture's holes compile in authored order with exact vertices");
    }
    expect(
        region.doubled_area()
            == Int256::multiply(
                static_cast<__int128>(114) * Coordinate::SCALE, Coordinate::SCALE),
        "the fixture's exact doubled region area is 114 square game units");

    // The same geometry composed programmatically compiles to the same exact
    // region, so the text fixture introduces no second representation.
    auto programmatic = compile_region_resource(make_fixture_region());
    if (expect(bool(programmatic), "the equivalent programmatic region compiles")) {
        expect(
            same_region(region, programmatic.value()),
            "the authored fixture and the programmatic region are exactly equal");
    }
}

// --- persistence ---

void ResourceIntegrationRunner::check_persistence(
    const content::PrototileCatalog &p_catalog) {
    const godot::String tres_path = temporary_path("level.tres");
    const godot::String res_path = temporary_path("level.res");
    const godot::String saved_as_path = temporary_path("saved_as.tres");
    const godot::String invalid_path = temporary_path("invalid.tres");
    const godot::String wrong_type_path = temporary_path("wrong_type.tres");
    const godot::String missing_path = temporary_path("never_written.tres");

    temporary_paths_ = { tres_path, res_path, saved_as_path, invalid_path, wrong_type_path };

    // An interrupted previous run may have left one of these exact files. Only
    // those exact names are removed, and only before this run begins.
    for (const godot::String &path : temporary_paths_) {
        remove_temporary(path);
    }

    const godot::Error made = godot::DirAccess::make_dir_recursive_absolute(
        godot::String(TEMPORARY_DIRECTORY));
    if (!expect(
            made == godot::OK || made == godot::ERR_ALREADY_EXISTS,
            "the narrow temporary directory is available")) {
        return;
    }

    const godot::Color first_color(0.75f, 0.5f, 0.25f, 1.0f);
    const godot::Color second_color(0.25f, 0.75f, 0.5f, 1.0f);
    godot::TypedArray<PaletteEntryResource> entries;
    entries.push_back(make_entry(1, 3, first_color));
    entries.push_back(make_entry(2, -1, second_color));
    godot::Ref<LevelResource> level = make_level(make_palette(entries), make_fixture_region());

    {
        auto saved = save_level_resource(level);
        if (expect(!saved, "saving an unpathed resource without a path fails")) {
            expect(
                saved.error().code == SaveLevelErrorCode::path_required,
                "an unpathed pathless save reports path_required");
            expect(
                saved.error().godot_error == godot::OK,
                "a non-saver failure invents no engine error code");
        }
    }

    {
        auto saved = save_level_resource(godot::Ref<LevelResource>(), tres_path);
        if (expect(!saved, "saving a null resource fails")) {
            expect(
                saved.error().code == SaveLevelErrorCode::missing_resource,
                "saving a null resource reports missing_resource");
        }
    }

    {
        auto saved = save_level_resource(level, temporary_path("level.json"));
        if (expect(!saved, "saving to an unsupported extension fails")) {
            expect(
                saved.error().code == SaveLevelErrorCode::unsupported_extension,
                "an unsupported extension is rejected before the saver");
        }
    }

    // An explicit path is both first save and save-as: it takes ownership.
    if (!expect(
            bool(save_level_resource(level, tres_path)),
            "an explicit .tres save succeeds")) {
        return;
    }
    expect(level->get_path() == tres_path, "an explicit save gives the resource its path");

    // A later pathless save overwrites that owned path without changing it.
    {
        auto saved = save_level_resource(level);
        if (expect(bool(saved), "a later pathless save succeeds")) {
            expect(
                saved.value() == tres_path && level->get_path() == tres_path,
                "a pathless save overwrites the owned path without changing identity");
        }
    }

    // Save-as moves ownership to the new path.
    {
        auto saved = save_level_resource(level, saved_as_path);
        if (expect(bool(saved), "save-as succeeds")) {
            expect(
                saved.value() == saved_as_path && level->get_path() == saved_as_path,
                "save-as changes the resource's owned path");
        }
    }

    expect(bool(save_level_resource(level, res_path)), "a binary .res save succeeds");

    // Uncached reload: authored values return field by field.
    for (const godot::String &path : { tres_path, res_path }) {
        auto loaded = load_level_resource(path, p_catalog);
        if (!expect(bool(loaded), "a persisted level reloads and compiles")) {
            continue;
        }
        const LoadedLevel &reloaded = loaded.value();
        if (!expect(
                reloaded.resource.is_valid() && reloaded.resource->get_palette().is_valid()
                    && reloaded.resource->get_region().is_valid(),
                "the persisted nested resources reload as the correct custom types")) {
            continue;
        }
        expect(
            reloaded.resource->get_path() == path,
            "the loaded resource owns the path it was loaded from");

        const godot::TypedArray<PaletteEntryResource> reloaded_entries =
            reloaded.resource->get_palette()->get_entries();
        if (expect(reloaded_entries.size() == 2, "the reloaded palette keeps two entries")) {
            const godot::Ref<PaletteEntryResource> first = reloaded_entries[0];
            const godot::Ref<PaletteEntryResource> second = reloaded_entries[1];
            if (expect(
                    first.is_valid() && second.is_valid(),
                    "the reloaded palette entries are PaletteEntryResource")) {
                expect(
                    first->get_prototile_id() == 1 && first->get_supply() == 3
                        && first->get_color() == first_color,
                    "the first entry's id, supply, and color all survive the round trip");
                expect(
                    second->get_prototile_id() == 2 && second->get_supply() == -1
                        && second->get_color() == second_color,
                    "the second entry's id, supply, and color all survive the round trip");
            }
        }

        const godot::Ref<RegionResource> reloaded_region = reloaded.resource->get_region();
        const godot::Ref<PolygonResource> reloaded_outer =
            reloaded_region->get_outer_boundary();
        if (expect(reloaded_outer.is_valid(), "the reloaded outer boundary is a PolygonResource")) {
            expect(
                same_authored_vertices(
                    reloaded_outer->get_vertices(),
                    make_fixture_region()->get_outer_boundary()->get_vertices()),
                "authored outer vertices round-trip in order and value");
        }
        const godot::TypedArray<PolygonResource> reloaded_holes =
            reloaded_region->get_inner_boundaries();
        if (expect(reloaded_holes.size() == 2, "the reloaded region keeps two holes")) {
            expect(
                typed_as(reloaded_holes, "PolygonResource"),
                "the reloaded hole array is still typed");
            const godot::TypedArray<PolygonResource> expected_holes =
                make_fixture_region()->get_inner_boundaries();
            bool holes_match = true;
            for (std::int64_t i = 0; i < reloaded_holes.size(); ++i) {
                const godot::Ref<PolygonResource> actual = reloaded_holes[i];
                const godot::Ref<PolygonResource> wanted = expected_holes[i];
                if (actual.is_null() || wanted.is_null()
                    || !same_authored_vertices(actual->get_vertices(), wanted->get_vertices())) {
                    holes_match = false;
                    break;
                }
            }
            expect(holes_match, "authored hole vertices round-trip in order and value");
        }

        // Compiled exact equivalence is a separate claim from authored
        // equivalence, and both must hold.
        auto before = compile_level_resource(level, p_catalog);
        if (expect(bool(before), "the pre-save resource still compiles")) {
            expect(
                same_palette(before.value().palette(), reloaded.level.palette()),
                "pre-save and post-load palettes are exactly equivalent");
            expect(
                same_region(before.value().region(), reloaded.level.region()),
                "pre-save and post-load regions are exactly equivalent");
        }
    }

    {
        auto loaded = load_level_resource(godot::String(), p_catalog);
        if (expect(!loaded, "loading an empty path fails")) {
            expect(
                loaded.error().code == LoadLevelErrorCode::path_required,
                "an empty load path reports path_required");
        }
    }

    {
        auto loaded = load_level_resource(temporary_path("level.json"), p_catalog);
        if (expect(!loaded, "loading an unsupported extension fails")) {
            expect(
                loaded.error().code == LoadLevelErrorCode::unsupported_extension,
                "an unsupported load extension is rejected before the loader");
        }
    }

    {
        // A path with an accepted extension which was never written. The engine
        // reports its own "does not exist" diagnostic; the typed result is what
        // this check observes.
        auto loaded = load_level_resource(missing_path, p_catalog);
        if (expect(!loaded, "loading a path that was never written fails")) {
            expect(
                loaded.error().code == LoadLevelErrorCode::loader_failed,
                "an unwritten path reports loader_failed");
        }
    }

    {
        // A well-formed resource of the wrong type is distinct from a loader
        // failure.
        godot::TypedArray<PaletteEntryResource> lone;
        lone.push_back(make_entry(1, -1, first_color));
        godot::Ref<PaletteResource> palette_only = make_palette(lone);
        const godot::Error saved = godot::ResourceSaver::get_singleton()->save(
            palette_only,
            wrong_type_path,
            godot::BitField<godot::ResourceSaver::SaverFlags>(
                godot::ResourceSaver::FLAG_CHANGE_PATH));
        if (expect(saved == godot::OK, "a palette-only resource can be persisted")) {
            auto loaded = load_level_resource(wrong_type_path, p_catalog);
            if (expect(!loaded, "loading a non-level resource as a level fails")) {
                expect(
                    loaded.error().code == LoadLevelErrorCode::wrong_resource_type,
                    "a wrong resource type is distinct from a loader failure");
            }
        }
    }

    {
        // Save performs no hidden validation: a structurally invalid level is
        // persisted successfully and then fails at the typed compilation stage.
        godot::Ref<LevelResource> invalid = make_level(
            make_palette(godot::TypedArray<PaletteEntryResource>()), make_fixture_region());
        if (expect(
                bool(save_level_resource(invalid, invalid_path)),
                "an invalid level saves without validation")) {
            auto loaded = load_level_resource(invalid_path, p_catalog);
            if (expect(!loaded, "an invalid persisted level fails to load")) {
                expect(
                    loaded.error().code == LoadLevelErrorCode::compilation_failed,
                    "an invalid persisted level fails at the compilation stage");
                expect(
                    loaded.error().compilation_error.has_value()
                        && loaded.error().compilation_error.value().code
                            == LevelResourceErrorCode::palette_invalid,
                    "the load failure preserves the complete typed compiler error");
            }
        }
    }

    for (const godot::String &path : temporary_paths_) {
        remove_temporary(path);
    }
}

void ResourceIntegrationRunner::check_temporary_files_removed() {
    bool all_removed = true;
    for (const godot::String &path : temporary_paths_) {
        if (godot::FileAccess::file_exists(path)) {
            all_removed = false;
        }
    }
    expect(all_removed, "every temporary file this run created was removed");

    // Remove the narrow generated directory itself, but only when it is empty.
    const godot::String directory(TEMPORARY_DIRECTORY);
    if (godot::DirAccess::dir_exists_absolute(directory)
        && godot::DirAccess::get_files_at(directory).is_empty()
        && godot::DirAccess::get_directories_at(directory).is_empty()) {
        godot::DirAccess::remove_absolute(directory);
    }
}

void ResourceIntegrationRunner::remove_temporary(const godot::String &p_path) {
    if (godot::FileAccess::file_exists(p_path)) {
        godot::DirAccess::remove_absolute(p_path);
    }
}

// --- exact comparison ---

bool ResourceIntegrationRunner::same_palette(
    const engine::Palette &p_lhs, const engine::Palette &p_rhs) {
    if (p_lhs.entries().size() != p_rhs.entries().size()) {
        return false;
    }
    for (std::size_t i = 0; i < p_lhs.entries().size(); ++i) {
        const engine::PaletteEntry &lhs = p_lhs.entries()[i];
        const engine::PaletteEntry &rhs = p_rhs.entries()[i];
        if (lhs.prototile().id() != rhs.prototile().id()) {
            return false;
        }
        if (lhs.supply() != rhs.supply()) {
            return false;
        }
        if (!same_boundary(lhs.prototile().polygon(), rhs.prototile().polygon())) {
            return false;
        }
        if (lhs.orientations().size() != rhs.orientations().size()) {
            return false;
        }
        for (std::size_t j = 0; j < lhs.orientations().size(); ++j) {
            if (lhs.orientations()[j].equivalent_orientations()
                != rhs.orientations()[j].equivalent_orientations()) {
                return false;
            }
            if (!same_boundary(
                    lhs.orientations()[j].canonical_polygon(),
                    rhs.orientations()[j].canonical_polygon())) {
                return false;
            }
        }
    }
    return true;
}

bool ResourceIntegrationRunner::same_region(const Region &p_lhs, const Region &p_rhs) {
    if (!same_boundary(p_lhs.outer_boundary(), p_rhs.outer_boundary())) {
        return false;
    }
    if (p_lhs.inner_boundaries().size() != p_rhs.inner_boundaries().size()) {
        return false;
    }
    for (std::size_t i = 0; i < p_lhs.inner_boundaries().size(); ++i) {
        if (!same_boundary(p_lhs.inner_boundaries()[i], p_rhs.inner_boundaries()[i])) {
            return false;
        }
    }
    return p_lhs.doubled_area() == p_rhs.doubled_area();
}

// --- entry point ---

void ResourceIntegrationRunner::_ready() {
    auto catalog = content::make_canonical_prototile_catalog();
    if (!expect(bool(catalog), "the canonical catalog constructs")) {
        godot::UtilityFunctions::push_error(
            "[tiles] resource integration aborted: no canonical catalog");
        if (get_tree() != nullptr) {
            get_tree()->quit(1);
        }
        return;
    }

    check_resource_defaults();
    check_property_metadata();
    check_setter_notifications();
    check_polygon_compilation();
    check_palette_compilation(catalog.value());
    check_region_compilation();
    check_level_compilation(catalog.value());
    check_authored_fixture(catalog.value());
    check_persistence(catalog.value());
    check_temporary_files_removed();

    if (failures_ == 0) {
        godot::UtilityFunctions::print(
            "[tiles] resource integration: ", static_cast<std::int64_t>(checks_),
            " checks passed");
    } else {
        godot::UtilityFunctions::push_error(
            "[tiles] resource integration: ", static_cast<std::int64_t>(failures_),
            " of ", static_cast<std::int64_t>(checks_), " checks failed");
    }

    if (get_tree() != nullptr) {
        get_tree()->quit(failures_ == 0 ? 0 : 1);
    }
}

} // namespace tiles::game
