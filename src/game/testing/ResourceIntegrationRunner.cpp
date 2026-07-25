#include "game/testing/ResourceIntegrationRunner.h"

#include "content/CanonicalOrientationCompiler.h"
#include "content/GeometryDomain.h"
#include "content/PrototileCatalog.h"
#include "core/ArrangementRegion.h"
#include "core/Hex12.h"
#include "core/Orientation.h"
#include "core/OrientedPrototile.h"
#include "core/Prototile.h"
#include "core/geometry/Coordinate.h"
#include "core/geometry/ExactInteger.h"
#include "core/geometry/Point.h"
#include "core/geometry/Polygon.h"
#include "engine/Blueprint.h"
#include "engine/Commands.h"
#include "engine/State.h"
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
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cstdint>
#include <initializer_list>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace tiles::game {

namespace {

// The one narrow generated directory this runner owns. Cleanup only ever names
// exact files beneath it, then removes the directory itself if it is empty.
// Nothing recursive and nothing derived from unchecked input is ever removed.
const char *TEMPORARY_DIRECTORY = "res://.godot/tiles_resource_integration";

const char *FIXTURE_PATH = "res://tests/fixtures/canonical_level.tres";

// Canonical identities this runner names, restated here rather than read out of
// the catalog it is checking.
constexpr std::int64_t O_TETROMINO_ID = 1;
constexpr std::int64_t I_TETROMINO_ID = 2;
constexpr std::int64_t UNIT_SQUARE_ID = 27;
constexpr std::int64_t HEX12_TRIANGLE_ID = 35;
constexpr std::int64_t HEX12_HEXAGON_ID = 36;
constexpr std::int64_t HEX12_DODECAGON_ID = 37;
constexpr std::size_t HEX12_VIEW_SIZE = 4;
constexpr std::size_t LATTICE_VIEW_SIZE = 34;

// The two encoded geometry domains, and one signed value which names neither.
constexpr std::int64_t LATTICE = ENCODED_GEOMETRY_DOMAIN_LATTICE;
constexpr std::int64_t HEX12 = ENCODED_GEOMETRY_DOMAIN_HEX12;

// One authored hex-12 entry together with everything its compiled product must
// carry: its supply, its source polygon, and its complete equivalence-label
// table written as twelfth-turn steps.
struct HexEntryExpectation final {
    std::int64_t id;
    bool unlimited;
    engine::Supply::Amount supply;
    Hex12RegularPolygon polygon;
    std::vector<std::vector<std::size_t>> groups;
};

const std::vector<HexEntryExpectation> &hex12_expectations() {
    static const std::vector<HexEntryExpectation> expectations = {
        { HEX12_TRIANGLE_ID, false, 3, Hex12RegularPolygon::triangle,
            { { 0, 4, 8 }, { 1, 5, 9 }, { 2, 6, 10 }, { 3, 7, 11 } } },
        { UNIT_SQUARE_ID, true, 0, Hex12RegularPolygon::square,
            { { 0, 3, 6, 9 }, { 1, 4, 7, 10 }, { 2, 5, 8, 11 } } },
        { HEX12_HEXAGON_ID, false, 2, Hex12RegularPolygon::hexagon,
            { { 0, 2, 4, 6, 8, 10 }, { 1, 3, 5, 7, 9, 11 } } },
        { HEX12_DODECAGON_ID, false, 1, Hex12RegularPolygon::dodecagon,
            { { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } } },
    };
    return expectations;
}

Orientation twelfth_turn(std::size_t p_step) {
    return Orientation::make(static_cast<Orientation::Component>(p_step), 12).value();
}

std::vector<Orientation> all_twelfth_turns() {
    std::vector<Orientation> requested;
    requested.reserve(12);
    for (std::size_t k = 0; k < 12; ++k) {
        requested.push_back(twelfth_turn(k));
    }
    return requested;
}

std::vector<Orientation> all_quarter_turns() {
    return {
        Orientation::reference(),
        Orientation::quarter(),
        Orientation::half(),
        Orientation::three_quarter(),
    };
}

godot::String temporary_path(const char *p_file) {
    return godot::String(TEMPORARY_DIRECTORY).path_join(godot::String(p_file));
}

// A whole-game-unit coordinate, and its raw q16.48 encoding. Both are exact:
// nothing here scales a rendered value or quantizes a decimal.
constexpr Coordinate::Storage raw_units(std::int64_t p_units) {
    return p_units * Coordinate::SCALE;
}

Coordinate units(std::int64_t p_units) {
    return Coordinate::from_raw(raw_units(p_units));
}

// The exact doubled area of a region measuring p_units square game units, in
// the same q16.48-squared scale Region reports.
Int256 doubled_area_of(std::int64_t p_units) {
    return Int256::multiply(
        static_cast<__int128>(2 * p_units) * Coordinate::SCALE, Coordinate::SCALE);
}

godot::Color white() {
    return godot::Color(1.0f, 1.0f, 1.0f, 1.0f);
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

godot::Ref<BlueprintPlacementResource> make_record(
    std::int64_t p_id,
    std::int64_t p_step,
    std::int64_t p_order,
    std::int64_t p_x_raw,
    std::int64_t p_y_raw) {
    godot::Ref<BlueprintPlacementResource> record;
    record.instantiate();
    record->set_prototile_id(p_id);
    record->set_orientation_step(p_step);
    record->set_orientation_order(p_order);
    record->set_translation_x_raw(p_x_raw);
    record->set_translation_y_raw(p_y_raw);
    return record;
}

godot::Ref<LevelResource> make_level(
    std::int64_t p_version,
    std::int64_t p_domain,
    const godot::Ref<PaletteResource> &p_palette,
    const godot::TypedArray<BlueprintPlacementResource> &p_blueprint) {
    godot::Ref<LevelResource> level;
    level.instantiate();
    level->set_format_version(p_version);
    level->set_geometry_domain(p_domain);
    level->set_palette(p_palette);
    level->set_blueprint(p_blueprint);
    return level;
}

// One unlimited-supply unit-square palette, and one blueprint covering exactly
// the named unit cells. Every hole and contact fixture below is one cell list.
godot::Ref<PaletteResource> unit_square_palette() {
    godot::TypedArray<PaletteEntryResource> entries;
    entries.push_back(make_entry(UNIT_SQUARE_ID, -1, white()));
    return make_palette(entries);
}

godot::TypedArray<BlueprintPlacementResource> unit_square_blueprint(
    std::initializer_list<std::pair<std::int64_t, std::int64_t>> p_cells) {
    godot::TypedArray<BlueprintPlacementResource> records;
    for (const auto &cell : p_cells) {
        records.push_back(
            make_record(UNIT_SQUARE_ID, 0, 1, raw_units(cell.first), raw_units(cell.second)));
    }
    return records;
}

godot::Ref<LevelResource> unit_square_level(
    std::initializer_list<std::pair<std::int64_t, std::int64_t>> p_cells) {
    return make_level(
        LEVEL_RESOURCE_FORMAT_VERSION,
        LATTICE,
        unit_square_palette(),
        unit_square_blueprint(p_cells));
}

// The same graph the authored `.tres` fixture carries, composed
// programmatically so the text fixture introduces no second representation.
//
// The o tetromino covers [0,2]^2 at exact origin; the i tetromino, in its
// quarter-turn representative, covers [2,3] x [0,4]. Their coverage is one
// connected six-vertex region with no holes and exact area 8.
godot::Ref<LevelResource> fixture_level() {
    godot::TypedArray<PaletteEntryResource> entries;
    entries.push_back(make_entry(O_TETROMINO_ID, 3, godot::Color(0.75f, 0.5f, 0.25f, 1.0f)));
    entries.push_back(make_entry(I_TETROMINO_ID, -1, godot::Color(0.25f, 0.75f, 0.5f, 1.0f)));

    godot::TypedArray<BlueprintPlacementResource> records;
    records.push_back(make_record(O_TETROMINO_ID, 0, 1, 0, 0));
    records.push_back(make_record(I_TETROMINO_ID, 1, 4, raw_units(2), 0));

    return make_level(
        LEVEL_RESOURCE_FORMAT_VERSION, LATTICE, make_palette(entries), records);
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

// One palette built entirely from ordinary checked core factories, carrying an
// identity or a supply which the signed Godot transport cannot represent. It is
// the only way to reach the encoder's representability refusals: every catalog
// identity and every authored supply is small.
std::optional<engine::Palette> unrepresentable_palette(
    PrototileId::Value p_id, engine::Supply p_supply) {
    std::vector<Point> square = {
        Point { units(0), units(0) },
        Point { units(1), units(0) },
        Point { units(1), units(1) },
        Point { units(0), units(1) },
    };
    auto polygon = Polygon::make(std::move(square));
    if (!polygon) {
        return std::nullopt;
    }
    auto prototile = Prototile::make(PrototileId(p_id), polygon.value());
    if (!prototile) {
        return std::nullopt;
    }
    auto oriented = compile_lattice_orientations(prototile.value(), all_quarter_turns());
    if (!oriented) {
        return std::nullopt;
    }
    auto entry =
        engine::PaletteEntry::make_compiled(p_supply, std::move(oriented).value());
    if (!entry) {
        return std::nullopt;
    }
    std::vector<engine::PaletteEntry> entries;
    entries.push_back(std::move(entry).value());
    auto palette = engine::Palette::make(std::move(entries));
    if (!palette) {
        return std::nullopt;
    }
    return std::move(palette).value();
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

void ResourceIntegrationRunner::check_registration() {
    expect(
        godot::ClassDB::class_exists(godot::StringName("PaletteEntryResource")),
        "PaletteEntryResource is registered");
    expect(
        godot::ClassDB::class_exists(godot::StringName("PaletteResource")),
        "PaletteResource is registered");
    expect(
        godot::ClassDB::class_exists(godot::StringName("BlueprintPlacementResource")),
        "BlueprintPlacementResource is registered");
    expect(
        godot::ClassDB::class_exists(godot::StringName("LevelResource")),
        "LevelResource is registered");

    // The old region-first graph is gone, types and properties alike. There is
    // no compatibility path and no independently authored region left anywhere.
    expect(
        !godot::ClassDB::class_exists(godot::StringName("PolygonResource")),
        "PolygonResource no longer exists");
    expect(
        !godot::ClassDB::class_exists(godot::StringName("RegionResource")),
        "RegionResource no longer exists");

    godot::Ref<LevelResource> level;
    level.instantiate();
    expect(
        property_info(level.ptr(), "region").is_empty(),
        "LevelResource has no region property");
}

void ResourceIntegrationRunner::check_resource_defaults() {
    godot::Ref<PaletteEntryResource> entry;
    entry.instantiate();
    expect(entry.is_valid(), "PaletteEntryResource instantiates");
    expect(entry->get_prototile_id() == 0, "PaletteEntryResource prototile_id defaults to 0");
    expect(entry->get_supply() == -1, "PaletteEntryResource supply defaults to -1");
    expect(
        entry->get_color() == white(),
        "PaletteEntryResource color defaults to opaque white");

    godot::Ref<PaletteResource> palette;
    palette.instantiate();
    expect(palette.is_valid(), "PaletteResource instantiates");
    expect(palette->get_entries().is_empty(), "PaletteResource entries default empty");
    expect(
        typed_as(palette->get_entries(), "PaletteEntryResource"),
        "default PaletteResource entries stay typed");

    godot::Ref<BlueprintPlacementResource> record;
    record.instantiate();
    expect(record.is_valid(), "BlueprintPlacementResource instantiates");
    expect(
        record->get_prototile_id() == 0,
        "BlueprintPlacementResource prototile_id defaults to 0");
    expect(
        record->get_orientation_step() == 0,
        "BlueprintPlacementResource orientation_step defaults to 0");
    expect(
        record->get_orientation_order() == 1,
        "BlueprintPlacementResource orientation_order defaults to 1");
    expect(
        record->get_translation_x_raw() == 0 && record->get_translation_y_raw() == 0,
        "BlueprintPlacementResource raw translation defaults to exact origin");

    godot::Ref<LevelResource> level;
    level.instantiate();
    expect(level.is_valid(), "LevelResource instantiates");
    expect(
        level->get_format_version() == LEVEL_RESOURCE_FORMAT_VERSION
            && LEVEL_RESOURCE_FORMAT_VERSION == 1,
        "LevelResource format_version defaults to the current version 1");
    expect(
        level->get_geometry_domain() == LATTICE,
        "LevelResource geometry_domain defaults to lattice");
    expect(level->get_palette().is_null(), "LevelResource palette defaults null");
    expect(level->get_blueprint().is_empty(), "LevelResource blueprint defaults empty");
    expect(
        typed_as(level->get_blueprint(), "BlueprintPlacementResource"),
        "default LevelResource blueprint stays typed");

    // Every setter is transport-only: it preserves the exact signed value it was
    // given, including values no compiler will ever accept.
    entry->set_prototile_id(7);
    entry->set_supply(4);
    entry->set_color(godot::Color(0.25f, 0.5f, 0.75f, 1.0f));
    expect(entry->get_prototile_id() == 7, "PaletteEntryResource preserves prototile_id");
    expect(entry->get_supply() == 4, "PaletteEntryResource preserves supply");
    expect(
        entry->get_color() == godot::Color(0.25f, 0.5f, 0.75f, 1.0f),
        "PaletteEntryResource preserves color");

    const std::int64_t least = std::numeric_limits<std::int64_t>::min();
    const std::int64_t greatest = std::numeric_limits<std::int64_t>::max();

    record->set_prototile_id(-9);
    record->set_orientation_step(greatest);
    record->set_orientation_order(0);
    record->set_translation_x_raw(least);
    record->set_translation_y_raw(greatest);
    expect(
        record->get_prototile_id() == -9 && record->get_orientation_step() == greatest
            && record->get_orientation_order() == 0
            && record->get_translation_x_raw() == least
            && record->get_translation_y_raw() == greatest,
        "BlueprintPlacementResource preserves every signed value, extrema included");

    level->set_format_version(-5);
    level->set_geometry_domain(77);
    expect(
        level->get_format_version() == -5 && level->get_geometry_domain() == 77,
        "LevelResource preserves unsupported signed version and domain values");

    godot::TypedArray<PaletteEntryResource> entries;
    entries.push_back(entry);
    palette->set_entries(entries);
    expect(palette->get_entries().size() == 1, "PaletteResource preserves entry count");
    expect(
        typed_as(palette->get_entries(), "PaletteEntryResource"),
        "assigned PaletteResource entries stay typed");

    godot::TypedArray<BlueprintPlacementResource> blueprint;
    blueprint.push_back(record);
    level->set_palette(palette);
    level->set_blueprint(blueprint);
    expect(level->get_palette() == palette, "LevelResource preserves palette");
    expect(level->get_blueprint().size() == 1, "LevelResource preserves record count");
    expect(
        typed_as(level->get_blueprint(), "BlueprintPlacementResource"),
        "assigned LevelResource blueprint stays typed");
}

void ResourceIntegrationRunner::check_property_metadata() {
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

    godot::Ref<BlueprintPlacementResource> record;
    record.instantiate();
    for (const char *name : { "prototile_id", "orientation_step", "orientation_order",
             "translation_x_raw", "translation_y_raw" }) {
        expect(
            property_has_type(record.ptr(), name, godot::Variant::INT),
            "every BlueprintPlacementResource property is a signed int property");
    }

    godot::Ref<LevelResource> level;
    level.instantiate();
    expect(
        property_has_type(level.ptr(), "format_version", godot::Variant::INT),
        "LevelResource.format_version is an int property");
    expect(
        property_has_type(level.ptr(), "geometry_domain", godot::Variant::INT),
        "LevelResource.geometry_domain is an int property");
    expect(
        property_has_hint(level.ptr(), "palette", godot::PROPERTY_HINT_RESOURCE_TYPE),
        "LevelResource.palette uses the resource-type hint");
    expect(
        property_has_type(level.ptr(), "blueprint", godot::Variant::ARRAY),
        "LevelResource.blueprint is an array property");
    expect(
        property_has_hint(level.ptr(), "blueprint", godot::PROPERTY_HINT_ARRAY_TYPE),
        "LevelResource.blueprint uses the typed-array hint");
}

void ResourceIntegrationRunner::check_setter_notifications() {
    const godot::Callable observer(this, godot::StringName("on_resource_changed"));

    godot::Ref<PaletteEntryResource> entry;
    entry.instantiate();
    entry->connect(godot::StringName("changed"), observer);

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
    entry->disconnect(godot::StringName("changed"), observer);

    godot::Ref<BlueprintPlacementResource> record;
    record.instantiate();
    record->connect(godot::StringName("changed"), observer);

    changed_notifications_ = 0;
    record->set_prototile_id(3);
    record->set_orientation_step(1);
    record->set_orientation_order(4);
    record->set_translation_x_raw(raw_units(2));
    record->set_translation_y_raw(-raw_units(3));
    expect(
        changed_notifications_ == 5,
        "each actual blueprint record change emits changed exactly once");
    record->set_prototile_id(3);
    record->set_orientation_step(1);
    record->set_orientation_order(4);
    record->set_translation_x_raw(raw_units(2));
    record->set_translation_y_raw(-raw_units(3));
    expect(
        changed_notifications_ == 5,
        "reassigning identical blueprint record values emits nothing");
    record->disconnect(godot::StringName("changed"), observer);

    godot::Ref<LevelResource> level;
    level.instantiate();
    level->connect(godot::StringName("changed"), observer);

    changed_notifications_ = 0;
    level->set_format_version(2);
    expect(changed_notifications_ == 1, "an actual format_version change emits changed once");
    level->set_format_version(2);
    expect(changed_notifications_ == 1, "assigning the same format_version emits nothing");

    level->set_geometry_domain(HEX12);
    expect(changed_notifications_ == 2, "an actual geometry_domain change emits changed once");
    level->set_geometry_domain(HEX12);
    expect(changed_notifications_ == 2, "assigning the same geometry_domain emits nothing");

    const godot::Ref<PaletteResource> palette = unit_square_palette();
    level->set_palette(palette);
    expect(changed_notifications_ == 3, "an actual palette change emits changed once");
    level->set_palette(palette);
    expect(changed_notifications_ == 3, "assigning the same palette emits nothing");

    const godot::TypedArray<BlueprintPlacementResource> blueprint =
        unit_square_blueprint({ { 0, 0 } });
    level->set_blueprint(blueprint);
    expect(changed_notifications_ == 4, "an actual blueprint change emits changed once");
    level->set_blueprint(blueprint);
    expect(changed_notifications_ == 4, "assigning the same blueprint emits nothing");
    level->disconnect(godot::StringName("changed"), observer);
}

// --- palette compilation ---

void ResourceIntegrationRunner::check_palette_compilation(
    const content::PrototileCatalog &p_catalog) {
    {
        auto compiled = compile_palette_resource(
            content::GeometryDomain::lattice, godot::Ref<PaletteResource>(), p_catalog);
        if (expect(!compiled, "a null palette resource fails to compile")) {
            expect(
                compiled.error().code == PaletteResourceErrorCode::missing_resource,
                "a null palette resource reports missing_resource");
        }
    }

    {
        godot::TypedArray<PaletteEntryResource> entries;
        entries.push_back(make_entry(O_TETROMINO_ID, -1, white()));
        entries.push_back(godot::Ref<PaletteEntryResource>());
        auto compiled = compile_palette_resource(
            content::GeometryDomain::lattice, make_palette(entries), p_catalog);
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
        entries.push_back(make_entry(O_TETROMINO_ID, -1, white()));
        entries.push_back(make_entry(-3, -1, white()));
        auto compiled = compile_palette_resource(
            content::GeometryDomain::lattice, make_palette(entries), p_catalog);
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
        entries.push_back(make_entry(0, -1, white()));
        auto compiled = compile_palette_resource(
            content::GeometryDomain::lattice, make_palette(entries), p_catalog);
        if (expect(!compiled, "prototile id 0 fails against the shipped catalog")) {
            const PaletteResourceError &error = compiled.error();
            expect(
                error.code == PaletteResourceErrorCode::unknown_prototile_id,
                "prototile id 0 reports unknown_prototile_id");
            expect(
                error.prototile_id.has_value()
                    && error.prototile_id.value() == PrototileId(0),
                "prototile id 0 reaches lookup as a strong id");
        }
    }

    {
        godot::TypedArray<PaletteEntryResource> entries;
        entries.push_back(make_entry(O_TETROMINO_ID, -1, white()));
        entries.push_back(make_entry(999, -1, white()));
        auto compiled = compile_palette_resource(
            content::GeometryDomain::lattice, make_palette(entries), p_catalog);
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
            entries.push_back(make_entry(O_TETROMINO_ID, supply, white()));
            auto compiled = compile_palette_resource(
                content::GeometryDomain::lattice, make_palette(entries), p_catalog);
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
                        && error.prototile_id.value() == PrototileId(O_TETROMINO_ID),
                    "an invalid supply reports the resolved strong id");
            }
        }
    }

    {
        godot::TypedArray<PaletteEntryResource> entries;
        auto compiled = compile_palette_resource(
            content::GeometryDomain::lattice, make_palette(entries), p_catalog);
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
        entries.push_back(make_entry(O_TETROMINO_ID, -1, white()));
        entries.push_back(make_entry(O_TETROMINO_ID, 2, white()));
        auto compiled = compile_palette_resource(
            content::GeometryDomain::lattice, make_palette(entries), p_catalog);
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
        entries.push_back(make_entry(O_TETROMINO_ID, 3, white()));
        entries.push_back(make_entry(I_TETROMINO_ID, -1, white()));
        auto compiled = compile_palette_resource(
            content::GeometryDomain::lattice, make_palette(entries), p_catalog);
        if (expect(bool(compiled), "a valid lattice palette compiles")) {
            const engine::Palette &palette = compiled.value();
            expect(palette.order() == 2, "authored palette order is preserved");
            if (palette.entries().size() == 2) {
                expect(
                    palette.entries()[0].prototile().id() == PrototileId(O_TETROMINO_ID)
                        && palette.entries()[1].prototile().id()
                            == PrototileId(I_TETROMINO_ID),
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
                    p_catalog.find(PrototileId(O_TETROMINO_ID));
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
        plain.push_back(make_entry(3, 2, white()));
        godot::TypedArray<PaletteEntryResource> colored;
        colored.push_back(make_entry(3, 2, godot::Color(0.25f, 0.75f, 0.5f, 1.0f)));

        auto first = compile_palette_resource(
            content::GeometryDomain::lattice, make_palette(plain), p_catalog);
        auto second = compile_palette_resource(
            content::GeometryDomain::lattice, make_palette(colored), p_catalog);
        if (expect(
                bool(first) && bool(second),
                "palettes differing only in color both compile")) {
            expect(
                same_palette(first.value(), second.value()),
                "palette compilation ignores authored color");
        }
    }
}

void ResourceIntegrationRunner::check_domain_palette_compilation(
    const content::PrototileCatalog &p_catalog) {
    // A domain value cast from an arbitrary integer. It is representable
    // because the enumeration fixes its underlying type, so rejecting it is an
    // ordinary observable behaviour rather than undefined.
    const content::GeometryDomain invalid_domain =
        static_cast<content::GeometryDomain>(200);

    {
        // Domain validity is answered after a missing resource and before any
        // authored entry is read.
        auto missing = compile_palette_resource(
            invalid_domain, godot::Ref<PaletteResource>(), p_catalog);
        if (expect(!missing, "an invalid domain with no resource fails to compile")) {
            expect(
                missing.error().code == PaletteResourceErrorCode::missing_resource,
                "a missing palette resource answers before the geometry domain");
        }

        godot::TypedArray<PaletteEntryResource> entries;
        entries.push_back(godot::Ref<PaletteEntryResource>());
        auto compiled =
            compile_palette_resource(invalid_domain, make_palette(entries), p_catalog);
        if (expect(!compiled, "an invalid geometry domain fails to compile")) {
            const PaletteResourceError &error = compiled.error();
            expect(
                error.code == PaletteResourceErrorCode::unsupported_geometry_domain,
                "an invalid domain reports unsupported_geometry_domain");
            expect(
                !error.entry.has_value(),
                "an invalid domain answers before any authored entry");
            expect(
                !error.orientation_error.has_value()
                    && !error.palette_entry_error.has_value()
                    && !error.palette_error.has_value(),
                "an invalid domain carries no nested compiler error");
        }
    }

    {
        // A lattice-only identity refused by hex-12, and a hex-only identity
        // refused by the lattice, both at the same typed alternative.
        godot::TypedArray<PaletteEntryResource> lattice_only;
        lattice_only.push_back(make_entry(HEX12_TRIANGLE_ID, -1, white()));
        lattice_only.push_back(make_entry(O_TETROMINO_ID, -1, white()));
        auto in_hex12 = compile_palette_resource(
            content::GeometryDomain::hex12, make_palette(lattice_only), p_catalog);
        if (expect(!in_hex12, "a lattice-only id fails to compile in hex12")) {
            const PaletteResourceError &error = in_hex12.error();
            expect(
                error.code == PaletteResourceErrorCode::prototile_unavailable_in_domain,
                "a lattice-only id in hex12 reports prototile_unavailable_in_domain");
            expect(
                error.entry.has_value() && error.entry.value() == 1,
                "an unavailable id reports its authored entry index");
            expect(
                error.prototile_id.has_value()
                    && error.prototile_id.value() == PrototileId(O_TETROMINO_ID),
                "an unavailable id reports its resolved strong id");
            expect(
                !error.orientation_error.has_value(),
                "an unavailable id is refused before orientation compilation");
        }

        godot::TypedArray<PaletteEntryResource> hex_only;
        hex_only.push_back(make_entry(HEX12_HEXAGON_ID, -1, white()));
        auto in_lattice = compile_palette_resource(
            content::GeometryDomain::lattice, make_palette(hex_only), p_catalog);
        if (expect(!in_lattice, "a hex-only id fails to compile in lattice")) {
            const PaletteResourceError &error = in_lattice.error();
            expect(
                error.code == PaletteResourceErrorCode::prototile_unavailable_in_domain,
                "a hex-only id in lattice reports prototile_unavailable_in_domain");
            expect(
                error.entry.has_value() && error.entry.value() == 0,
                "a hex-only id in lattice reports its authored entry index");
        }
    }

    {
        // Nothing is published after a later entry fails, and neither the
        // resource nor the catalog is touched by a failed compilation.
        godot::TypedArray<PaletteEntryResource> entries;
        entries.push_back(make_entry(HEX12_TRIANGLE_ID, 4, white()));
        entries.push_back(make_entry(O_TETROMINO_ID, -1, white()));
        const godot::Ref<PaletteResource> resource = make_palette(entries);

        const std::size_t catalog_size = p_catalog.entries().size();
        auto compiled = compile_palette_resource(
            content::GeometryDomain::hex12, resource, p_catalog);
        if (expect(!compiled, "a later unavailable entry fails the whole palette")) {
            expect(
                compiled.error().entry.has_value() && compiled.error().entry.value() == 1,
                "the failing entry is the later one, so no partial palette exists");
        }
        expect(
            resource->get_entries().size() == 2,
            "a failed compilation leaves the resource entry count unchanged");
        const godot::Ref<PaletteEntryResource> first_entry = resource->get_entries()[0];
        expect(
            first_entry.is_valid() && first_entry->get_prototile_id() == HEX12_TRIANGLE_ID
                && first_entry->get_supply() == 4 && first_entry->get_color() == white(),
            "a failed compilation leaves every authored entry unchanged");
        expect(
            p_catalog.entries().size() == catalog_size
                && p_catalog.entries_for(content::GeometryDomain::hex12).size()
                    == HEX12_VIEW_SIZE,
            "a failed compilation leaves the catalog unchanged");
    }

    {
        // The complete hex-12 palette, authored in presentation order.
        godot::TypedArray<PaletteEntryResource> entries;
        entries.push_back(make_entry(HEX12_TRIANGLE_ID, 3, white()));
        entries.push_back(make_entry(UNIT_SQUARE_ID, -1, white()));
        entries.push_back(make_entry(HEX12_HEXAGON_ID, 2, white()));
        entries.push_back(make_entry(HEX12_DODECAGON_ID, 1, white()));
        const godot::Ref<PaletteResource> resource = make_palette(entries);

        auto compiled = compile_palette_resource(
            content::GeometryDomain::hex12, resource, p_catalog);
        if (expect(bool(compiled), "the four-entry hex-12 palette compiles")) {
            const engine::Palette &palette = compiled.value();
            if (expect(palette.order() == 4, "the hex-12 palette has order four")) {
                for (std::size_t i = 0; i < HEX12_VIEW_SIZE; ++i) {
                    const HexEntryExpectation &expectation = hex12_expectations()[i];
                    const engine::PaletteEntry &entry = palette.entries()[i];

                    expect(
                        entry.prototile().id() == PrototileId(expectation.id),
                        "the hex-12 palette preserves authored presentation order");
                    expect(
                        expectation.unlimited
                            ? entry.supply().is_unlimited()
                            : entry.supply().finite_amount().has_value()
                                && entry.supply().finite_amount().value()
                                    == expectation.supply,
                        "each hex-12 entry keeps its configured supply");
                    expect(
                        entry.orientations().size() == expectation.groups.size(),
                        "each hex-12 entry exposes its distinct orientation count");
                    if (entry.orientations().size() != expectation.groups.size()) {
                        continue;
                    }

                    // The same identity compiled directly by the ordinary hex-12
                    // core compiler, so the expected geometry is not read back
                    // out of the integration path being checked.
                    auto direct = compile_hex12_orientations(
                        PrototileId(expectation.id),
                        expectation.polygon,
                        all_twelfth_turns());
                    if (!expect(
                            bool(direct),
                            "the ordinary hex-12 compiler produces the same identity")) {
                        continue;
                    }

                    expect(
                        same_boundary(
                            entry.prototile().polygon(),
                            direct.value().front().prototile().polygon()),
                        "each hex-12 entry keeps the module's exact reference boundary");

                    bool orientations_match = true;
                    for (std::size_t g = 0; g < entry.orientations().size(); ++g) {
                        const OrientedPrototile &oriented = entry.orientations()[g];
                        const std::vector<std::size_t> &steps = expectation.groups[g];

                        if (oriented.orientation() != twelfth_turn(steps.front())) {
                            orientations_match = false;
                        }
                        if (oriented.equivalent_orientations().size() != steps.size()) {
                            orientations_match = false;
                        } else {
                            for (std::size_t k = 0; k < steps.size(); ++k) {
                                if (oriented.equivalent_orientations()[k]
                                    != twelfth_turn(steps[k])) {
                                    orientations_match = false;
                                }
                            }
                        }
                        if (!same_boundary(
                                oriented.canonical_polygon(),
                                direct.value()[g].canonical_polygon())) {
                            orientations_match = false;
                        }
                    }
                    expect(
                        orientations_match,
                        "each hex-12 entry keeps its exact representatives, equivalence "
                        "labels, and oriented boundaries");
                }
            }
        }
    }

    {
        // The shared unit square is one identity whose compiled orientation set
        // is chosen by the domain: one lattice group against three hex-12 groups.
        godot::TypedArray<PaletteEntryResource> entries;
        entries.push_back(make_entry(UNIT_SQUARE_ID, -1, white()));

        auto lattice = compile_palette_resource(
            content::GeometryDomain::lattice, make_palette(entries), p_catalog);
        auto hex12 = compile_palette_resource(
            content::GeometryDomain::hex12, make_palette(entries), p_catalog);
        if (expect(
                bool(lattice) && bool(hex12),
                "the unit square compiles in both geometry domains")) {
            expect(
                lattice.value().entries()[0].orientations().size() == 1
                    && hex12.value().entries()[0].orientations().size() == 3,
                "the unit square's admitted orientations are domain-selected");
            expect(
                lattice.value().entries()[0].prototile().id()
                        == hex12.value().entries()[0].prototile().id()
                    && same_boundary(
                        lattice.value().entries()[0].prototile().polygon(),
                        hex12.value().entries()[0].prototile().polygon()),
                "the unit square is one identity with one reference boundary");
        }
    }
}

void ResourceIntegrationRunner::check_palette_size_limit(
    const content::PrototileCatalog &p_catalog) {
    expect(
        p_catalog.entries_for(content::GeometryDomain::lattice).size()
                == LATTICE_VIEW_SIZE
            && p_catalog.entries_for(content::GeometryDomain::hex12).size()
                == HEX12_VIEW_SIZE,
        "each domain admits its expected number of canonical identities");

    // Every entry is null, so reporting the count rather than a missing entry is
    // what shows the limit answered before any entry was inspected.
    const auto null_palette = [](std::size_t p_count) {
        godot::TypedArray<PaletteEntryResource> entries;
        for (std::size_t i = 0; i < p_count; ++i) {
            entries.push_back(godot::Ref<PaletteEntryResource>());
        }
        return make_palette(entries);
    };

    {
        auto compiled = compile_palette_resource(
            content::GeometryDomain::lattice,
            null_palette(LATTICE_VIEW_SIZE + 1),
            p_catalog);
        if (expect(!compiled, "an oversized lattice palette fails to compile")) {
            const PaletteResourceError &error = compiled.error();
            expect(
                error.code == PaletteResourceErrorCode::too_many_entries,
                "an oversized lattice palette reports too_many_entries");
            expect(
                error.entry_count.has_value()
                    && error.entry_count.value() == LATTICE_VIEW_SIZE + 1
                    && error.maximum_entry_count.has_value()
                    && error.maximum_entry_count.value() == LATTICE_VIEW_SIZE,
                "the palette size limit reports its actual and maximum counts");
            expect(
                !error.entry.has_value(),
                "the palette size limit answers before any entry is inspected");
        }
    }

    {
        // The same array length is refused in hex-12 and accepted as far as
        // entry inspection in the lattice, so the limit is domain-specific.
        auto in_hex12 = compile_palette_resource(
            content::GeometryDomain::hex12, null_palette(HEX12_VIEW_SIZE + 1), p_catalog);
        if (expect(!in_hex12, "an oversized hex-12 palette fails to compile")) {
            expect(
                in_hex12.error().code == PaletteResourceErrorCode::too_many_entries
                    && in_hex12.error().maximum_entry_count.has_value()
                    && in_hex12.error().maximum_entry_count.value() == HEX12_VIEW_SIZE,
                "the hex-12 limit is the hex-12 identity count");
        }

        auto in_lattice = compile_palette_resource(
            content::GeometryDomain::lattice,
            null_palette(HEX12_VIEW_SIZE + 1),
            p_catalog);
        if (expect(!in_lattice, "the same array still fails in the lattice")) {
            expect(
                in_lattice.error().code == PaletteResourceErrorCode::missing_entry
                    && in_lattice.error().entry.has_value()
                    && in_lattice.error().entry.value() == 0,
                "an array within the lattice limit reaches entry inspection");
        }
    }
}

// --- blueprint decoding ---

void ResourceIntegrationRunner::check_blueprint_decoding() {
    {
        godot::TypedArray<BlueprintPlacementResource> empty;
        auto decoded = compile_blueprint_resource(empty);
        if (expect(bool(decoded), "an empty blueprint array decodes")) {
            expect(decoded.value().empty(), "an empty blueprint array decodes to no records");
        }
    }

    {
        godot::TypedArray<BlueprintPlacementResource> records;
        for (std::size_t i = 0; i < MAX_BLUEPRINT_PLACEMENTS; ++i) {
            records.push_back(make_record(
                UNIT_SQUARE_ID, 0, 1, raw_units(static_cast<std::int64_t>(i)), 0));
        }
        auto decoded = compile_blueprint_resource(records);
        if (expect(bool(decoded), "exactly 64 blueprint records decode")) {
            expect(
                decoded.value().size() == MAX_BLUEPRINT_PLACEMENTS,
                "all 64 decoded records are published");
        }

        // The 65th record is a null one, so reporting the count rather than a
        // missing record is what shows the limit answered before any record.
        records.push_back(godot::Ref<BlueprintPlacementResource>());
        auto refused = compile_blueprint_resource(records);
        if (expect(!refused, "65 blueprint records fail to decode")) {
            const BlueprintResourceError &error = refused.error();
            expect(
                error.code == BlueprintResourceErrorCode::too_many_placements,
                "an oversized blueprint reports too_many_placements");
            expect(
                error.placement_count.has_value()
                    && error.placement_count.value() == MAX_BLUEPRINT_PLACEMENTS + 1
                    && error.maximum_placement_count.has_value()
                    && error.maximum_placement_count.value() == MAX_BLUEPRINT_PLACEMENTS,
                "the blueprint size limit reports its actual and maximum counts");
            expect(
                !error.placement.has_value(),
                "the blueprint size limit answers before any record is inspected");
        }
    }

    {
        godot::TypedArray<BlueprintPlacementResource> records;
        records.push_back(make_record(UNIT_SQUARE_ID, 0, 1, 0, 0));
        records.push_back(godot::Ref<BlueprintPlacementResource>());
        auto decoded = compile_blueprint_resource(records);
        if (expect(!decoded, "a null blueprint record fails to decode")) {
            expect(
                decoded.error().code == BlueprintResourceErrorCode::missing_placement
                    && decoded.error().placement.has_value()
                    && decoded.error().placement.value() == 1,
                "a null record reports missing_placement at its own index");
            expect(
                !decoded.error().encoded_prototile_id.has_value()
                    && !decoded.error().orientation_error.has_value(),
                "a null record carries no other payload");
        }
    }

    {
        godot::TypedArray<BlueprintPlacementResource> records;
        records.push_back(make_record(-4, 0, 1, 0, 0));
        auto decoded = compile_blueprint_resource(records);
        if (expect(!decoded, "a negative record prototile id fails to decode")) {
            expect(
                decoded.error().code == BlueprintResourceErrorCode::negative_prototile_id
                    && decoded.error().encoded_prototile_id.has_value()
                    && decoded.error().encoded_prototile_id.value() == -4,
                "a negative record id reports its signed encoded value");
        }
    }

    {
        const std::int64_t beyond_unsigned =
            static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max()) + 1;
        const std::int64_t out_of_range_steps[2] = { -1, beyond_unsigned };
        for (const std::int64_t step : out_of_range_steps) {
            godot::TypedArray<BlueprintPlacementResource> records;
            records.push_back(make_record(UNIT_SQUARE_ID, step, 4, 0, 0));
            auto decoded = compile_blueprint_resource(records);
            if (expect(!decoded, "an out-of-range orientation step fails to decode")) {
                expect(
                    decoded.error().code
                            == BlueprintResourceErrorCode::orientation_step_out_of_range
                        && decoded.error().encoded_orientation_step.has_value()
                        && decoded.error().encoded_orientation_step.value() == step,
                    "an out-of-range step reports its signed encoded value");
                expect(
                    !decoded.error().encoded_orientation_order.has_value(),
                    "a step failure carries no order value");
            }
        }

        const std::int64_t out_of_range_orders[2] = { -1, beyond_unsigned };
        for (const std::int64_t order : out_of_range_orders) {
            godot::TypedArray<BlueprintPlacementResource> records;
            records.push_back(make_record(UNIT_SQUARE_ID, 1, order, 0, 0));
            auto decoded = compile_blueprint_resource(records);
            if (expect(!decoded, "an out-of-range orientation order fails to decode")) {
                expect(
                    decoded.error().code
                            == BlueprintResourceErrorCode::orientation_order_out_of_range
                        && decoded.error().encoded_orientation_order.has_value()
                        && decoded.error().encoded_orientation_order.value() == order,
                    "an out-of-range order reports its signed encoded value");
            }
        }

        // The step is checked before the order.
        godot::TypedArray<BlueprintPlacementResource> both;
        both.push_back(make_record(UNIT_SQUARE_ID, -1, -1, 0, 0));
        auto decoded = compile_blueprint_resource(both);
        if (expect(!decoded, "a record with both components out of range fails")) {
            expect(
                decoded.error().code
                    == BlueprintResourceErrorCode::orientation_step_out_of_range,
                "the orientation step is validated before the order");
        }
    }

    {
        // Zero is representable as an unsigned order, so it reaches
        // Orientation::make and keeps that factory's own typed refusal.
        godot::TypedArray<BlueprintPlacementResource> records;
        records.push_back(make_record(UNIT_SQUARE_ID, 0, 0, 0, 0));
        auto decoded = compile_blueprint_resource(records);
        if (expect(!decoded, "orientation order zero fails to decode")) {
            expect(
                decoded.error().code == BlueprintResourceErrorCode::invalid_orientation,
                "order zero reports invalid_orientation");
            expect(
                decoded.error().orientation_error.has_value()
                    && decoded.error().orientation_error.value()
                        == OrientationError::zero_order,
                "order zero preserves OrientationError::zero_order");
        }
    }

    {
        // A negative id and an invalid orientation in one record: the id
        // answers first.
        godot::TypedArray<BlueprintPlacementResource> records;
        records.push_back(make_record(-1, 0, 0, 0, 0));
        auto decoded = compile_blueprint_resource(records);
        if (expect(!decoded, "a record with several defects fails to decode")) {
            expect(
                decoded.error().code == BlueprintResourceErrorCode::negative_prototile_id,
                "the record id is validated before its orientation");
        }
    }

    {
        // Orientation::make canonicalizes the rational turn, so an equivalent
        // label decodes to its canonical step/order pair.
        godot::TypedArray<BlueprintPlacementResource> records;
        records.push_back(make_record(UNIT_SQUARE_ID, 2, 8, 0, 0));
        records.push_back(make_record(UNIT_SQUARE_ID, 4, 4, 0, 0));
        auto decoded = compile_blueprint_resource(records);
        if (expect(bool(decoded), "equivalent rational labels decode")) {
            expect(
                decoded.value()[0].orientation == Orientation::quarter()
                    && decoded.value()[1].orientation == Orientation::reference(),
                "decoded orientations are canonical step/order pairs");
        }
    }

    {
        // Raw translations cross unchanged, including the signed extrema, one
        // raw unit, and a negative value.
        const std::int64_t least = std::numeric_limits<std::int64_t>::min();
        const std::int64_t greatest = std::numeric_limits<std::int64_t>::max();
        godot::TypedArray<BlueprintPlacementResource> records;
        records.push_back(make_record(UNIT_SQUARE_ID, 0, 1, least, greatest));
        records.push_back(make_record(UNIT_SQUARE_ID, 0, 1, 1, -1));
        records.push_back(
            make_record(UNIT_SQUARE_ID, 0, 1, -raw_units(3), Coordinate::SCALE / 2));
        auto decoded = compile_blueprint_resource(records);
        if (expect(bool(decoded), "extreme raw translations decode")) {
            expect(
                decoded.value()[0].translation.x.raw() == least
                    && decoded.value()[0].translation.y.raw() == greatest,
                "the signed raw extrema survive decoding exactly");
            expect(
                decoded.value()[1].translation.x.raw() == 1
                    && decoded.value()[1].translation.y.raw() == -1,
                "single raw units survive decoding exactly");
            expect(
                decoded.value()[2].translation.x.raw() == -raw_units(3)
                    && decoded.value()[2].translation.y.raw() == Coordinate::SCALE / 2,
                "negative whole units and half units survive decoding exactly");
        }
    }

    {
        // Records decode in array order, ids and all.
        godot::TypedArray<BlueprintPlacementResource> records;
        records.push_back(make_record(O_TETROMINO_ID, 0, 1, 0, 0));
        records.push_back(make_record(I_TETROMINO_ID, 1, 4, raw_units(2), 0));
        auto decoded = compile_blueprint_resource(records);
        if (expect(bool(decoded), "an ordinary blueprint array decodes")) {
            expect(
                decoded.value().size() == 2
                    && decoded.value()[0].prototile_id == PrototileId(O_TETROMINO_ID)
                    && decoded.value()[1].prototile_id == PrototileId(I_TETROMINO_ID),
                "records decode in array order");
            expect(
                decoded.value()[1].orientation == Orientation::quarter()
                    && decoded.value()[1].translation.x.raw() == raw_units(2)
                    && decoded.value()[1].translation.y.raw() == 0,
                "each record keeps its exact orientation and translation");
        }
    }
}

// --- level compilation ---

void ResourceIntegrationRunner::check_level_compilation(
    const content::PrototileCatalog &p_catalog) {
    {
        auto compiled = compile_level_resource(godot::Ref<LevelResource>(), p_catalog);
        if (expect(!compiled, "a null level resource fails to compile")) {
            expect(
                compiled.error().code == LevelResourceErrorCode::missing_resource,
                "a null level resource reports missing_resource");
        }
    }

    {
        const std::int64_t unsupported[5] = {
            0,
            2,
            -1,
            std::numeric_limits<std::int64_t>::min(),
            std::numeric_limits<std::int64_t>::max(),
        };
        for (const std::int64_t version : unsupported) {
            godot::Ref<LevelResource> level = fixture_level();
            level->set_format_version(version);
            auto compiled = compile_level_resource(level, p_catalog);
            if (expect(!compiled, "an unsupported format version fails to compile")) {
                const LevelResourceError &error = compiled.error();
                expect(
                    error.code == LevelResourceErrorCode::unsupported_format_version,
                    "an unsupported version reports unsupported_format_version");
                expect(
                    error.encoded_format_version.has_value()
                        && error.encoded_format_version.value() == version,
                    "an unsupported version reports its signed encoded value");
                expect(
                    !error.palette_error.has_value() && !error.blueprint_error.has_value()
                        && !error.region_error.has_value(),
                    "a version failure carries no nested compiler error");
            }
        }

        godot::Ref<LevelResource> current = fixture_level();
        expect(
            bool(compile_level_resource(current, p_catalog)),
            "the current format version compiles");
    }

    {
        const std::int64_t unsupported[3] = {
            2, -1, std::numeric_limits<std::int64_t>::max()
        };
        for (const std::int64_t domain : unsupported) {
            godot::Ref<LevelResource> level = fixture_level();
            level->set_geometry_domain(domain);
            auto compiled = compile_level_resource(level, p_catalog);
            if (expect(!compiled, "an unsupported geometry domain fails to compile")) {
                const LevelResourceError &error = compiled.error();
                expect(
                    error.code == LevelResourceErrorCode::unsupported_geometry_domain,
                    "an unsupported domain reports unsupported_geometry_domain");
                expect(
                    error.encoded_geometry_domain.has_value()
                        && error.encoded_geometry_domain.value() == domain,
                    "an unsupported domain reports its signed encoded value");
            }
        }

        // Version precedes domain: a resource with both wrong reports the
        // version.
        godot::Ref<LevelResource> both = fixture_level();
        both->set_format_version(9);
        both->set_geometry_domain(9);
        auto compiled = compile_level_resource(both, p_catalog);
        if (expect(!compiled, "a level with both header fields wrong fails")) {
            expect(
                compiled.error().code == LevelResourceErrorCode::unsupported_format_version,
                "the format version is decided before the geometry domain");
        }
    }

    {
        // Domain precedes palette, and palette precedes blueprint records.
        godot::Ref<LevelResource> domain_and_palette = make_level(
            LEVEL_RESOURCE_FORMAT_VERSION,
            5,
            godot::Ref<PaletteResource>(),
            godot::TypedArray<BlueprintPlacementResource>());
        auto first = compile_level_resource(domain_and_palette, p_catalog);
        if (expect(!first, "an invalid domain with no palette fails")) {
            expect(
                first.error().code == LevelResourceErrorCode::unsupported_geometry_domain,
                "the geometry domain is decided before the palette");
        }

        godot::TypedArray<BlueprintPlacementResource> null_record;
        null_record.push_back(godot::Ref<BlueprintPlacementResource>());
        godot::Ref<LevelResource> palette_and_records = make_level(
            LEVEL_RESOURCE_FORMAT_VERSION, LATTICE, godot::Ref<PaletteResource>(),
            null_record);
        auto second = compile_level_resource(palette_and_records, p_catalog);
        if (expect(!second, "a level with no palette and a null record fails")) {
            expect(
                second.error().code == LevelResourceErrorCode::palette_invalid,
                "the palette is compiled before the blueprint records");
            expect(
                second.error().palette_error.has_value()
                    && second.error().palette_error.value().code
                        == PaletteResourceErrorCode::missing_resource,
                "a palette failure preserves the complete typed palette error");
            expect(
                !second.error().blueprint_error.has_value(),
                "a palette failure populates only the palette error");
        }
    }

    {
        godot::Ref<LevelResource> level = fixture_level();
        godot::TypedArray<BlueprintPlacementResource> records = level->get_blueprint();
        records.push_back(godot::Ref<BlueprintPlacementResource>());
        level->set_blueprint(records);
        auto compiled = compile_level_resource(level, p_catalog);
        if (expect(!compiled, "a null blueprint record fails the whole level")) {
            expect(
                compiled.error().code == LevelResourceErrorCode::blueprint_resource_invalid,
                "a record transport failure reports blueprint_resource_invalid");
            expect(
                compiled.error().blueprint_error.has_value()
                    && compiled.error().blueprint_error.value().placement.has_value()
                    && compiled.error().blueprint_error.value().placement.value() == 2,
                "a later record failure names its own index and publishes no product");
        }
    }

    {
        // An id which decodes but names no palette entry, and an equivalent but
        // nonrepresentative orientation label: both reach the arrangement
        // compiler and fail there.
        godot::Ref<LevelResource> unknown = fixture_level();
        godot::TypedArray<BlueprintPlacementResource> records = unknown->get_blueprint();
        records.push_back(make_record(UNIT_SQUARE_ID, 0, 1, raw_units(4), 0));
        unknown->set_blueprint(records);
        auto compiled = compile_level_resource(unknown, p_catalog);
        if (expect(!compiled, "a record naming an absent palette id fails")) {
            expect(
                compiled.error().code
                    == LevelResourceErrorCode::blueprint_arrangement_invalid,
                "an absent palette id reports blueprint_arrangement_invalid");
            expect(
                compiled.error().arrangement_error.has_value()
                    && compiled.error().arrangement_error.value().code
                        == engine::BlueprintCompilationErrorCode::prototile_not_in_palette
                    && compiled.error().arrangement_error.value().placement == 2,
                "the complete typed blueprint compilation error is preserved");
        }

        // The o tetromino's one compiled group records all four quarter turns as
        // equivalents and accepts only 0/1 as its representative, so 1/4 decodes
        // successfully and is refused by the palette.
        godot::Ref<LevelResource> equivalent = fixture_level();
        godot::TypedArray<BlueprintPlacementResource> labelled;
        labelled.push_back(make_record(O_TETROMINO_ID, 1, 4, 0, 0));
        equivalent->set_blueprint(labelled);
        auto relabelled = compile_level_resource(equivalent, p_catalog);
        if (expect(!relabelled, "a nonrepresentative equivalent orientation fails")) {
            expect(
                relabelled.error().code
                    == LevelResourceErrorCode::blueprint_arrangement_invalid,
                "an equivalent label decodes and is refused by the palette");
            expect(
                relabelled.error().arrangement_error.has_value()
                    && relabelled.error().arrangement_error.value().code
                        == engine::BlueprintCompilationErrorCode::orientation_not_in_palette,
                "an equivalent label preserves orientation_not_in_palette");
        }
    }

    {
        // An empty blueprint compiles to an empty arrangement and fails at
        // region derivation, which is where emptiness becomes an error.
        godot::Ref<LevelResource> empty = make_level(
            LEVEL_RESOURCE_FORMAT_VERSION,
            LATTICE,
            unit_square_palette(),
            godot::TypedArray<BlueprintPlacementResource>());
        auto compiled = compile_level_resource(empty, p_catalog);
        if (expect(!compiled, "an empty blueprint fails to compile")) {
            expect(
                compiled.error().code == LevelResourceErrorCode::arrangement_region_invalid,
                "an empty blueprint reports arrangement_region_invalid");
            expect(
                compiled.error().region_error.has_value()
                    && compiled.error().region_error.value().code
                        == ArrangementRegionErrorCode::empty_arrangement,
                "an empty blueprint preserves ArrangementRegionErrorCode::empty_arrangement");
        }

        // Two separated tiles are a valid arrangement whose coverage is not one
        // region.
        auto disconnected =
            compile_level_resource(unit_square_level({ { 0, 0 }, { 4, 4 } }), p_catalog);
        if (expect(!disconnected, "disconnected coverage fails to compile")) {
            expect(
                disconnected.error().code
                        == LevelResourceErrorCode::arrangement_region_invalid
                    && disconnected.error().region_error.has_value()
                    && disconnected.error().region_error.value().code
                        == ArrangementRegionErrorCode::disconnected_coverage,
                "disconnected coverage preserves the complete alpha error");
            expect(
                disconnected.error().region_error.value().component_points.size() == 2,
                "the disconnected error keeps its component evidence");
        }
    }

    {
        // The complete rich compiled product.
        auto compiled = compile_level_resource(fixture_level(), p_catalog);
        if (expect(bool(compiled), "a complete valid level compiles")) {
            const CompiledLevelResource &product = compiled.value();
            expect(
                product.domain == content::GeometryDomain::lattice,
                "the compiled product records which domain interpreted the transport");
            expect(
                product.blueprint.size() == 2
                    && product.blueprint[0].prototile_id == PrototileId(O_TETROMINO_ID)
                    && product.blueprint[0].orientation == Orientation::reference()
                    && product.blueprint[0].translation
                        == Point { units(0), units(0) }
                    && product.blueprint[1].prototile_id == PrototileId(I_TETROMINO_ID)
                    && product.blueprint[1].orientation == Orientation::quarter()
                    && product.blueprint[1].translation == Point { units(2), units(0) },
                "the compiled product keeps the exact decoded witness in record order");
            expect(
                product.arrangement.entries().size() == 2,
                "the compiled product carries the exact arrangement proof");
            expect(
                product.level.palette().order() == 2,
                "the compiled level keeps its palette order");
            expect(
                product.level.region().inner_boundaries().empty()
                    && product.level.region().outer_boundary().vertices().size() == 6,
                "the derived region is the exact six-vertex coverage boundary");
            expect(
                product.level.region().doubled_area() == doubled_area_of(8),
                "the derived region has the exact area of its two footprints");
            expect(
                replay_known_solution(product),
                "replaying the compiled witness solves the derived level");
        }
    }
}

void ResourceIntegrationRunner::check_region_derivation(
    const content::PrototileCatalog &p_catalog) {
    {
        // A 3 x 3 block of unit squares with its centre omitted: one hole.
        auto compiled = compile_level_resource(
            unit_square_level({ { 0, 0 }, { 1, 0 }, { 2, 0 }, { 0, 1 }, { 2, 1 },
                { 0, 2 }, { 1, 2 }, { 2, 2 } }),
            p_catalog);
        if (expect(bool(compiled), "a ring of unit squares compiles")) {
            const Region &region = compiled.value().level.region();
            expect(
                region.inner_boundaries().size() == 1,
                "an omitted interior cell derives exactly one hole");
            expect(
                region.outer_boundary().vertices().size() == 4
                    && region.outer_boundary().vertices().front()
                        == Point { units(0), units(0) },
                "the ring's outer boundary is the exact simplified square");
            expect(
                region.inner_boundaries()[0].vertices().front()
                    == Point { units(1), units(1) },
                "the hole begins at its exact canonical vertex");
            expect(
                region.doubled_area() == doubled_area_of(8),
                "the ring's exact area excludes its hole");
            expect(
                replay_known_solution(compiled.value()),
                "replaying the ring witness solves the derived level");
        }
    }

    {
        // A 5 x 3 block with two interior cells omitted: two ordered holes.
        auto compiled = compile_level_resource(
            unit_square_level({ { 0, 0 }, { 1, 0 }, { 2, 0 }, { 3, 0 }, { 4, 0 },
                { 0, 1 }, { 2, 1 }, { 4, 1 },
                { 0, 2 }, { 1, 2 }, { 2, 2 }, { 3, 2 }, { 4, 2 } }),
            p_catalog);
        if (expect(bool(compiled), "a two-hole patch compiles")) {
            const Region &region = compiled.value().level.region();
            if (expect(
                    region.inner_boundaries().size() == 2,
                    "two omitted interior cells derive exactly two holes")) {
                expect(
                    region.inner_boundaries()[0].vertices().front()
                            == Point { units(1), units(1) }
                        && region.inner_boundaries()[1].vertices().front()
                            == Point { units(3), units(1) },
                    "the derived holes are in canonical order with exact vertices");
            }
            expect(
                region.doubled_area() == doubled_area_of(13),
                "the two-hole region's exact area excludes both holes");
            expect(
                replay_known_solution(compiled.value()),
                "replaying the two-hole witness solves the derived level");
        }
    }

    {
        // Partial-edge contact: the unit square meets one half of the o
        // tetromino's right edge and shares no polygon vertex with the rest.
        godot::TypedArray<PaletteEntryResource> entries;
        entries.push_back(make_entry(O_TETROMINO_ID, -1, white()));
        entries.push_back(make_entry(UNIT_SQUARE_ID, -1, white()));
        godot::TypedArray<BlueprintPlacementResource> records;
        records.push_back(make_record(O_TETROMINO_ID, 0, 1, 0, 0));
        records.push_back(make_record(UNIT_SQUARE_ID, 0, 1, raw_units(2), 0));

        auto compiled = compile_level_resource(
            make_level(LEVEL_RESOURCE_FORMAT_VERSION, LATTICE, make_palette(entries),
                records),
            p_catalog);
        if (expect(bool(compiled), "partial-edge contact compiles")) {
            const Region &region = compiled.value().level.region();
            expect(
                region.inner_boundaries().empty()
                    && region.outer_boundary().vertices().size() == 6,
                "partial-edge contact derives one connected six-vertex region");
            expect(
                region.doubled_area() == doubled_area_of(5),
                "the partial-contact region has the exact summed footprint area");
            expect(
                replay_known_solution(compiled.value()),
                "replaying the partial-contact witness solves the derived level");
        }
    }

    {
        // A hex-12 level: the domain reaches the palette compiler and nothing
        // downstream knows about it.
        godot::TypedArray<PaletteEntryResource> entries;
        entries.push_back(make_entry(HEX12_DODECAGON_ID, 2, white()));
        godot::TypedArray<BlueprintPlacementResource> records;
        records.push_back(make_record(HEX12_DODECAGON_ID, 0, 1, 0, 0));

        auto compiled = compile_level_resource(
            make_level(LEVEL_RESOURCE_FORMAT_VERSION, HEX12, make_palette(entries),
                records),
            p_catalog);
        if (expect(bool(compiled), "a hex-12 level compiles")) {
            const CompiledLevelResource &product = compiled.value();
            expect(
                product.domain == content::GeometryDomain::hex12,
                "the hex-12 product records the hex-12 domain");
            expect(
                product.level.region().outer_boundary().vertices().size() == 12,
                "the hex-12 dodecagon derives its exact twelve-vertex region");
            expect(
                replay_known_solution(product),
                "replaying the hex-12 witness solves the derived level");
        }

        // The same palette in the lattice domain is refused, so the serialized
        // domain is load-bearing.
        auto in_lattice = compile_level_resource(
            make_level(LEVEL_RESOURCE_FORMAT_VERSION, LATTICE, make_palette(entries),
                records),
            p_catalog);
        if (expect(!in_lattice, "the same hex-12 palette fails in the lattice")) {
            expect(
                in_lattice.error().code == LevelResourceErrorCode::palette_invalid
                    && in_lattice.error().palette_error.has_value()
                    && in_lattice.error().palette_error.value().code
                        == PaletteResourceErrorCode::prototile_unavailable_in_domain,
                "the serialized domain selects which identities are admissible");
        }
    }
}

void ResourceIntegrationRunner::check_compilation_purity(
    const content::PrototileCatalog &p_catalog) {
    const godot::Callable observer(this, godot::StringName("on_resource_changed"));

    godot::Ref<LevelResource> level = fixture_level();
    const godot::Ref<PaletteResource> palette = level->get_palette();
    const godot::Ref<PaletteEntryResource> first_entry = palette->get_entries()[0];
    const godot::Ref<BlueprintPlacementResource> first_record = level->get_blueprint()[1];

    level->connect(godot::StringName("changed"), observer);
    palette->connect(godot::StringName("changed"), observer);
    first_entry->connect(godot::StringName("changed"), observer);
    first_record->connect(godot::StringName("changed"), observer);

    changed_notifications_ = 0;
    auto compiled = compile_level_resource(level, p_catalog);
    expect(bool(compiled), "the observed level compiles");
    expect(
        changed_notifications_ == 0,
        "compilation emits no changed notification anywhere in the graph");
    expect(
        level->get_format_version() == LEVEL_RESOURCE_FORMAT_VERSION
            && level->get_geometry_domain() == LATTICE
            && level->get_palette() == palette && level->get_blueprint().size() == 2,
        "compilation leaves the level's own properties and pointers unchanged");
    expect(
        first_entry->get_prototile_id() == O_TETROMINO_ID
            && first_entry->get_supply() == 3
            && first_entry->get_color() == godot::Color(0.75f, 0.5f, 0.25f, 1.0f),
        "compilation leaves every authored palette value unchanged");
    expect(
        first_record->get_prototile_id() == I_TETROMINO_ID
            && first_record->get_orientation_step() == 1
            && first_record->get_orientation_order() == 4
            && first_record->get_translation_x_raw() == raw_units(2)
            && first_record->get_translation_y_raw() == 0,
        "compilation leaves every authored record value unchanged");
    expect(
        level->get_path().is_empty() && palette->get_path().is_empty()
            && first_entry->get_path().is_empty() && first_record->get_path().is_empty(),
        "compilation gives no resource a path");

    // A failing compilation is equally inert.
    level->set_format_version(3);
    changed_notifications_ = 0;
    auto refused = compile_level_resource(level, p_catalog);
    expect(!refused, "the observed level with a bad version fails to compile");
    expect(
        changed_notifications_ == 0,
        "a failed compilation emits no changed notification either");
    level->set_format_version(LEVEL_RESOURCE_FORMAT_VERSION);

    level->disconnect(godot::StringName("changed"), observer);
    palette->disconnect(godot::StringName("changed"), observer);
    first_entry->disconnect(godot::StringName("changed"), observer);
    first_record->disconnect(godot::StringName("changed"), observer);

    // Authored color never enters exact geometry: two levels differing only in
    // color compile to exactly equal palettes, arrangements, and regions.
    godot::Ref<LevelResource> recolored = fixture_level();
    godot::TypedArray<PaletteEntryResource> colored;
    colored.push_back(make_entry(O_TETROMINO_ID, 3, godot::Color(0.1f, 0.2f, 0.3f, 1.0f)));
    colored.push_back(make_entry(I_TETROMINO_ID, -1, godot::Color(0.9f, 0.8f, 0.7f, 1.0f)));
    recolored->set_palette(make_palette(colored));

    auto original = compile_level_resource(fixture_level(), p_catalog);
    auto other = compile_level_resource(recolored, p_catalog);
    if (expect(
            bool(original) && bool(other),
            "levels differing only in color both compile")) {
        expect(
            same_palette(original.value().level.palette(), other.value().level.palette())
                && same_arrangement(
                    original.value().arrangement, other.value().arrangement)
                && same_region(
                    original.value().level.region(), other.value().level.region()),
            "authored color enters no exact palette, arrangement, or region");
    }
}

// --- encoding ---

void ResourceIntegrationRunner::check_encoding(
    const content::PrototileCatalog &p_catalog) {
    // One exact palette and one exact blueprint, obtained through the ordinary
    // public compiler rather than fabricated.
    auto source = compile_level_resource(fixture_level(), p_catalog);
    if (!expect(bool(source), "the encoder's exact source values compile")) {
        return;
    }
    const engine::Palette &palette = source.value().level.palette();
    const std::vector<engine::BlueprintPlacement> &blueprint = source.value().blueprint;
    const std::vector<godot::Color> colors = {
        godot::Color(0.75f, 0.5f, 0.25f, 1.0f),
        godot::Color(0.25f, 0.75f, 0.5f, 1.0f),
    };

    {
        auto encoded = make_level_resource(
            static_cast<content::GeometryDomain>(200), palette, colors, blueprint);
        if (expect(!encoded, "an invalid domain fails to encode")) {
            expect(
                encoded.error().code
                    == LevelResourceEncodingErrorCode::unsupported_geometry_domain,
                "an invalid domain reports unsupported_geometry_domain");
        }
    }

    {
        const std::vector<godot::Color> one_color = { white() };
        auto encoded = make_level_resource(
            content::GeometryDomain::lattice, palette, one_color, blueprint);
        if (expect(!encoded, "a short color sequence fails to encode")) {
            const LevelResourceEncodingError &error = encoded.error();
            expect(
                error.code == LevelResourceEncodingErrorCode::color_count_mismatch,
                "a color count mismatch reports color_count_mismatch");
            expect(
                error.actual_count.has_value() && error.actual_count.value() == 1
                    && error.expected_count.has_value()
                    && error.expected_count.value() == 2,
                "a color count mismatch reports its actual and expected counts");
        }

        // Domain precedes the color count.
        auto both = make_level_resource(
            static_cast<content::GeometryDomain>(200), palette, one_color, blueprint);
        if (expect(!both, "an invalid domain with a short color sequence fails")) {
            expect(
                both.error().code
                    == LevelResourceEncodingErrorCode::unsupported_geometry_domain,
                "the domain is validated before the color count");
        }
    }

    {
        const PrototileId::Value beyond_signed =
            static_cast<PrototileId::Value>(std::numeric_limits<std::int64_t>::max()) + 1;
        std::optional<engine::Palette> huge_id =
            unrepresentable_palette(beyond_signed, engine::Supply::unlimited());
        if (expect(
                huge_id.has_value(),
                "a palette carrying an unrepresentable id can be constructed")) {
            auto encoded = make_level_resource(
                content::GeometryDomain::lattice, huge_id.value(), { white() }, {});
            if (expect(!encoded, "an unrepresentable prototile id fails to encode")) {
                const LevelResourceEncodingError &error = encoded.error();
                expect(
                    error.code
                        == LevelResourceEncodingErrorCode::prototile_id_not_representable,
                    "an unrepresentable id reports prototile_id_not_representable");
                expect(
                    error.entry.has_value() && error.entry.value() == 0
                        && error.prototile_id.has_value()
                        && error.prototile_id.value() == beyond_signed,
                    "an unrepresentable id reports its entry index and unsigned value");
            }
        }

        const engine::Supply::Amount beyond_supply =
            static_cast<engine::Supply::Amount>(std::numeric_limits<std::int64_t>::max())
            + 1;
        auto huge_supply_value = engine::Supply::finite(beyond_supply);
        if (expect(
                bool(huge_supply_value),
                "an unrepresentable finite supply can be constructed")) {
            std::optional<engine::Palette> huge_supply =
                unrepresentable_palette(UNIT_SQUARE_ID, huge_supply_value.value());
            if (expect(
                    huge_supply.has_value(),
                    "a palette carrying an unrepresentable supply can be constructed")) {
                auto encoded = make_level_resource(
                    content::GeometryDomain::lattice, huge_supply.value(), { white() },
                    {});
                if (expect(!encoded, "an unrepresentable supply fails to encode")) {
                    const LevelResourceEncodingError &error = encoded.error();
                    expect(
                        error.code
                            == LevelResourceEncodingErrorCode::supply_not_representable,
                        "an unrepresentable supply reports supply_not_representable");
                    expect(
                        error.entry.has_value() && error.entry.value() == 0
                            && error.supply.has_value()
                            && error.supply.value() == beyond_supply,
                        "an unrepresentable supply reports its entry index and value");
                }
            }
        }
    }

    {
        std::vector<engine::BlueprintPlacement> oversized;
        for (std::size_t i = 0; i <= MAX_BLUEPRINT_PLACEMENTS; ++i) {
            oversized.push_back(engine::BlueprintPlacement {
                PrototileId(UNIT_SQUARE_ID),
                Orientation::reference(),
                Point { units(static_cast<std::int64_t>(i)), units(0) },
            });
        }
        auto encoded = make_level_resource(
            content::GeometryDomain::lattice, palette, colors, oversized);
        if (expect(!encoded, "an oversized blueprint fails to encode")) {
            const LevelResourceEncodingError &error = encoded.error();
            expect(
                error.code == LevelResourceEncodingErrorCode::too_many_placements,
                "an oversized blueprint reports too_many_placements");
            expect(
                error.actual_count.has_value()
                    && error.actual_count.value() == MAX_BLUEPRINT_PLACEMENTS + 1
                    && error.expected_count.has_value()
                    && error.expected_count.value() == MAX_BLUEPRINT_PLACEMENTS,
                "an oversized blueprint reports its actual and maximum counts");
        }

        // The color count precedes the blueprint count.
        auto both = make_level_resource(
            content::GeometryDomain::lattice, palette, { white() }, oversized);
        if (expect(!both, "a short color sequence with an oversized blueprint fails")) {
            expect(
                both.error().code == LevelResourceEncodingErrorCode::color_count_mismatch,
                "the color count is validated before the blueprint count");
        }
    }

    {
        const PrototileId::Value beyond_signed =
            static_cast<PrototileId::Value>(std::numeric_limits<std::int64_t>::max()) + 1;
        std::vector<engine::BlueprintPlacement> records = blueprint;
        records.push_back(engine::BlueprintPlacement {
            PrototileId(beyond_signed),
            Orientation::reference(),
            Point { units(0), units(0) },
        });
        auto encoded =
            make_level_resource(content::GeometryDomain::lattice, palette, colors, records);
        if (expect(!encoded, "an unrepresentable record id fails to encode")) {
            const LevelResourceEncodingError &error = encoded.error();
            expect(
                error.code
                    == LevelResourceEncodingErrorCode::
                        blueprint_prototile_id_not_representable,
                "an unrepresentable record id reports its own code");
            expect(
                error.placement.has_value() && error.placement.value() == 2
                    && error.prototile_id.has_value()
                    && error.prototile_id.value() == beyond_signed,
                "an unrepresentable record id reports its record index and value");
        }
    }

    {
        auto encoded = make_level_resource(
            content::GeometryDomain::lattice, palette, colors, blueprint);
        if (expect(bool(encoded), "exact values encode into a fresh resource graph")) {
            const godot::Ref<LevelResource> level = encoded.value();
            expect(
                level->get_format_version() == LEVEL_RESOURCE_FORMAT_VERSION
                    && level->get_geometry_domain() == LATTICE,
                "a generated level carries the current version and its domain encoding");

            const godot::Ref<PaletteResource> generated_palette = level->get_palette();
            if (expect(generated_palette.is_valid(), "a generated level has a palette")) {
                const godot::TypedArray<PaletteEntryResource> entries =
                    generated_palette->get_entries();
                if (expect(entries.size() == 2, "the generated palette keeps its order")) {
                    const godot::Ref<PaletteEntryResource> first = entries[0];
                    const godot::Ref<PaletteEntryResource> second = entries[1];
                    expect(
                        first->get_prototile_id() == O_TETROMINO_ID
                            && first->get_supply() == 3 && first->get_color() == colors[0],
                        "a finite supply and its color encode exactly, in palette order");
                    expect(
                        second->get_prototile_id() == I_TETROMINO_ID
                            && second->get_supply() == -1
                            && second->get_color() == colors[1],
                        "an unlimited supply encodes as -1 with its own color");
                }
            }

            const godot::TypedArray<BlueprintPlacementResource> records =
                level->get_blueprint();
            if (expect(records.size() == 2, "the generated blueprint keeps record order")) {
                const godot::Ref<BlueprintPlacementResource> first = records[0];
                const godot::Ref<BlueprintPlacementResource> second = records[1];
                expect(
                    first->get_prototile_id() == O_TETROMINO_ID
                        && first->get_orientation_step() == 0
                        && first->get_orientation_order() == 1
                        && first->get_translation_x_raw() == 0
                        && first->get_translation_y_raw() == 0,
                    "the first record encodes its canonical orientation and raw origin");
                expect(
                    second->get_prototile_id() == I_TETROMINO_ID
                        && second->get_orientation_step() == 1
                        && second->get_orientation_order() == 4
                        && second->get_translation_x_raw() == raw_units(2)
                        && second->get_translation_y_raw() == 0,
                    "the second record encodes its exact rational turn and raw units");
            }

            bool unpathed = level->get_path().is_empty()
                && generated_palette->get_path().is_empty();
            for (std::int64_t i = 0; i < level->get_blueprint().size(); ++i) {
                const godot::Ref<BlueprintPlacementResource> record =
                    level->get_blueprint()[i];
                unpathed = unpathed && record->get_path().is_empty();
            }
            for (std::int64_t i = 0; i < generated_palette->get_entries().size(); ++i) {
                const godot::Ref<PaletteEntryResource> entry =
                    generated_palette->get_entries()[i];
                unpathed = unpathed && entry->get_path().is_empty();
            }
            expect(unpathed, "every generated resource is fresh and unpathed");

            expect(
                generated_palette != fixture_level()->get_palette(),
                "encoding reuses no previously constructed subresource");

            // The generated graph is an independent artifact: compiling it
            // reproduces the exact values it was built from.
            auto recompiled = compile_level_resource(level, p_catalog);
            if (expect(bool(recompiled), "a generated level compiles")) {
                expect(
                    same_palette(
                        recompiled.value().level.palette(), source.value().level.palette())
                        && same_blueprint(
                            recompiled.value().blueprint, source.value().blueprint)
                        && same_region(
                            recompiled.value().level.region(),
                            source.value().level.region()),
                    "a generated level compiles back to its exact source values");
            }
        }
    }

    {
        // Encoding compiles nothing: an empty blueprint, which no level
        // compilation accepts, still encodes.
        auto encoded = make_level_resource(
            content::GeometryDomain::hex12, palette, colors, {});
        if (expect(bool(encoded), "an uncompilable blueprint still encodes")) {
            expect(
                encoded.value()->get_geometry_domain() == HEX12
                    && encoded.value()->get_blueprint().is_empty(),
                "the encoder writes the hex-12 encoding and no records");
            expect(
                !compile_level_resource(encoded.value(), p_catalog),
                "the encoder's output is proven only by the separate compiler");
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

    const LoadedLevelResource &level = loaded.value();

    expect(
        level.resource->get_format_version() == LEVEL_RESOURCE_FORMAT_VERSION
            && level.resource->get_geometry_domain() == LATTICE,
        "the fixture is a version-1 lattice artifact");

    if (expect(
            level.resource->get_palette().is_valid(),
            "the fixture has a palette resource")) {
        const godot::TypedArray<PaletteEntryResource> entries =
            level.resource->get_palette()->get_entries();
        if (expect(entries.size() == 2, "the fixture palette has two authored entries")) {
            const godot::Ref<PaletteEntryResource> first = entries[0];
            const godot::Ref<PaletteEntryResource> second = entries[1];
            if (expect(
                    first.is_valid() && second.is_valid(),
                    "the fixture palette entries reload as PaletteEntryResource")) {
                expect(
                    first->get_prototile_id() == O_TETROMINO_ID
                        && first->get_supply() == 3,
                    "the fixture's first entry is canonical id 1 with finite supply 3");
                expect(
                    second->get_prototile_id() == I_TETROMINO_ID
                        && second->get_supply() == -1,
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
    }

    const godot::TypedArray<BlueprintPlacementResource> records =
        level.resource->get_blueprint();
    if (expect(records.size() == 2, "the fixture blueprint has two authored records")) {
        const godot::Ref<BlueprintPlacementResource> first = records[0];
        const godot::Ref<BlueprintPlacementResource> second = records[1];
        if (expect(
                first.is_valid() && second.is_valid(),
                "the fixture records reload as BlueprintPlacementResource")) {
            expect(
                first->get_orientation_step() == 0 && first->get_orientation_order() == 1
                    && first->get_translation_x_raw() == 0
                    && first->get_translation_y_raw() == 0,
                "the fixture's first record is the reference turn at exact origin");
            expect(
                second->get_orientation_step() == 1
                    && second->get_orientation_order() == 4
                    && second->get_translation_x_raw() == raw_units(2),
                "the fixture's second record keeps its quarter turn and raw translation");
        }
    }

    const CompiledLevelResource &compiled = level.compiled;
    expect(
        compiled.domain == content::GeometryDomain::lattice,
        "the fixture compiles in the lattice domain");
    expect(
        compiled.level.palette().order() == 2
            && compiled.level.palette().entries()[0].supply().finite_amount().has_value()
            && compiled.level.palette().entries()[0].supply().finite_amount().value() == 3
            && compiled.level.palette().entries()[1].supply().is_unlimited(),
        "the fixture's supplies compile exactly");
    expect(
        compiled.arrangement.entries().size() == 2,
        "the fixture's arrangement carries both witness placements");
    expect(
        compiled.level.region().inner_boundaries().empty()
            && compiled.level.region().doubled_area() == doubled_area_of(8),
        "the fixture's derived region is exactly its coverage");
    expect(
        replay_known_solution(compiled),
        "replaying the fixture witness solves the fixture level");

    // The same graph composed programmatically compiles to exactly the same
    // exact values, so the text fixture introduces no second representation.
    auto programmatic = compile_level_resource(fixture_level(), p_catalog);
    if (expect(bool(programmatic), "the equivalent programmatic level compiles")) {
        expect(
            same_palette(compiled.level.palette(), programmatic.value().level.palette())
                && same_blueprint(compiled.blueprint, programmatic.value().blueprint)
                && same_arrangement(compiled.arrangement, programmatic.value().arrangement)
                && same_region(
                    compiled.level.region(), programmatic.value().level.region()),
            "the authored fixture and the programmatic level are exactly equal");
    }
}

// --- export and consumer loading ---

void ResourceIntegrationRunner::check_export_and_load(
    const content::PrototileCatalog &p_catalog) {
    const godot::String level_path = temporary_path("level.tres");
    const godot::String holes_path = temporary_path("holes.tres");
    const godot::String refused_path = temporary_path("refused.tres");
    const godot::String invalid_path = temporary_path("invalid.tres");
    const godot::String wrong_type_path = temporary_path("wrong_type.tres");
    const godot::String binary_path = temporary_path("level.res");
    const godot::String missing_path = temporary_path("never_written.tres");

    temporary_paths_ = {
        level_path, holes_path, refused_path, invalid_path, wrong_type_path, binary_path
    };

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

    {
        auto exported = export_level_resource(
            godot::Ref<LevelResource>(), level_path, p_catalog);
        if (expect(!exported, "exporting a null resource fails")) {
            expect(
                exported.error().code == ExportLevelErrorCode::missing_resource
                    && exported.error().godot_error == godot::OK,
                "a null export reports missing_resource and invents no engine error");
        }
    }

    {
        auto exported =
            export_level_resource(fixture_level(), godot::String(), p_catalog);
        if (expect(!exported, "exporting without a path fails")) {
            expect(
                exported.error().code == ExportLevelErrorCode::path_required,
                "an empty export path reports path_required");
        }
    }

    {
        auto json =
            export_level_resource(fixture_level(), temporary_path("level.json"), p_catalog);
        if (expect(!json, "exporting to an unsupported extension fails")) {
            expect(
                json.error().code == ExportLevelErrorCode::unsupported_extension,
                "an unsupported extension is rejected before compilation");
        }
        auto binary = export_level_resource(fixture_level(), binary_path, p_catalog);
        if (expect(!binary, "exporting to .res fails")) {
            expect(
                binary.error().code == ExportLevelErrorCode::unsupported_extension,
                "the binary container is not a supported export format");
        }
        expect(
            !godot::FileAccess::file_exists(binary_path),
            "an extension refusal writes nothing");
    }

    {
        // A structurally valid but uncompilable artifact never reaches the
        // saver.
        godot::Ref<LevelResource> empty = make_level(
            LEVEL_RESOURCE_FORMAT_VERSION,
            LATTICE,
            unit_square_palette(),
            godot::TypedArray<BlueprintPlacementResource>());
        auto exported = export_level_resource(empty, refused_path, p_catalog);
        if (expect(!exported, "exporting an uncompilable level fails")) {
            expect(
                exported.error().code == ExportLevelErrorCode::compilation_failed,
                "an uncompilable export reports compilation_failed");
            expect(
                exported.error().compilation_error.has_value()
                    && exported.error().compilation_error.value().code
                        == LevelResourceErrorCode::arrangement_region_invalid,
                "the export failure preserves the complete typed compiler error");
        }
        expect(
            !godot::FileAccess::file_exists(refused_path),
            "a compilation refusal writes nothing");
    }

    // The primary round trip: one artifact, exported and then loaded back
    // through the public consumer operation.
    const godot::Callable observer(this, godot::StringName("on_resource_changed"));
    godot::Ref<LevelResource> level = fixture_level();
    const godot::Ref<PaletteResource> palette = level->get_palette();
    const godot::Ref<BlueprintPlacementResource> second_record = level->get_blueprint()[1];
    level->connect(godot::StringName("changed"), observer);
    palette->connect(godot::StringName("changed"), observer);
    second_record->connect(godot::StringName("changed"), observer);

    changed_notifications_ = 0;
    auto exported = export_level_resource(level, level_path, p_catalog);
    if (!expect(bool(exported), "a valid level exports to .tres")) {
        level->disconnect(godot::StringName("changed"), observer);
        palette->disconnect(godot::StringName("changed"), observer);
        second_record->disconnect(godot::StringName("changed"), observer);
        return;
    }
    expect(exported.value() == level_path, "a successful export returns its explicit path");
    expect(
        changed_notifications_ == 0, "export emits no changed notification");
    expect(
        level->get_path().is_empty(),
        "export claims no path for the exported resource");
    expect(
        palette->get_path().is_empty() && second_record->get_path().is_empty(),
        "export claims no path for any subresource");
    expect(
        level->get_palette() == palette && level->get_blueprint().size() == 2,
        "export leaves every pointer relationship unchanged");
    level->disconnect(godot::StringName("changed"), observer);
    palette->disconnect(godot::StringName("changed"), observer);
    second_record->disconnect(godot::StringName("changed"), observer);

    expect(
        !godot::FileAccess::get_file_as_string(level_path).contains(
            godot::String("[ext_resource")),
        "the exported artifact depends on no external resource");

    auto before = compile_level_resource(level, p_catalog);
    if (!expect(bool(before), "the pre-export resource still compiles")) {
        return;
    }

    {
        auto loaded = load_level_resource(level_path, p_catalog);
        if (expect(bool(loaded), "the exported artifact loads and compiles")) {
            const LoadedLevelResource &reloaded = loaded.value();
            expect(
                reloaded.resource->get_format_version() == LEVEL_RESOURCE_FORMAT_VERSION
                    && reloaded.resource->get_geometry_domain() == LATTICE,
                "the exported version and domain survive the round trip");

            const godot::TypedArray<PaletteEntryResource> entries =
                reloaded.resource->get_palette()->get_entries();
            if (expect(entries.size() == 2, "the reloaded palette keeps two entries")) {
                const godot::Ref<PaletteEntryResource> first = entries[0];
                const godot::Ref<PaletteEntryResource> second = entries[1];
                expect(
                    first->get_prototile_id() == O_TETROMINO_ID
                        && first->get_supply() == 3
                        && first->get_color() == godot::Color(0.75f, 0.5f, 0.25f, 1.0f),
                    "the first entry's id, supply, and color survive the round trip");
                expect(
                    second->get_prototile_id() == I_TETROMINO_ID
                        && second->get_supply() == -1
                        && second->get_color() == godot::Color(0.25f, 0.75f, 0.5f, 1.0f),
                    "the second entry's id, supply, and color survive the round trip");
            }

            const godot::TypedArray<BlueprintPlacementResource> records =
                reloaded.resource->get_blueprint();
            if (expect(records.size() == 2, "the reloaded blueprint keeps two records")) {
                expect(
                    typed_as(records, "BlueprintPlacementResource"),
                    "the reloaded blueprint array is still typed");
                const godot::Ref<BlueprintPlacementResource> second = records[1];
                expect(
                    second->get_prototile_id() == I_TETROMINO_ID
                        && second->get_orientation_step() == 1
                        && second->get_orientation_order() == 4
                        && second->get_translation_x_raw() == raw_units(2)
                        && second->get_translation_y_raw() == 0,
                    "record order, ids, rational turns, and raw translations survive");
            }

            expect(
                same_palette(before.value().level.palette(), reloaded.compiled.level.palette()),
                "pre-export and post-load palettes are exactly equivalent");
            expect(
                same_blueprint(before.value().blueprint, reloaded.compiled.blueprint),
                "pre-export and post-load witnesses are exactly equivalent");
            expect(
                same_arrangement(before.value().arrangement, reloaded.compiled.arrangement),
                "pre-export and post-load arrangements are exactly equivalent");
            expect(
                same_region(before.value().level.region(), reloaded.compiled.level.region()),
                "pre-export and post-load regions are exactly equivalent");
            expect(
                reloaded.compiled.level.region().doubled_area() == doubled_area_of(8),
                "the reloaded region keeps its exact area");
            expect(
                replay_known_solution(reloaded.compiled),
                "replaying the reloaded witness solves the reloaded level");
        }
    }

    {
        // Holes survive the round trip too.
        godot::Ref<LevelResource> holes = unit_square_level({ { 0, 0 }, { 1, 0 }, { 2, 0 },
            { 0, 1 }, { 2, 1 }, { 0, 2 }, { 1, 2 }, { 2, 2 } });
        if (expect(
                bool(export_level_resource(holes, holes_path, p_catalog)),
                "a level with a hole exports")) {
            auto loaded = load_level_resource(holes_path, p_catalog);
            if (expect(bool(loaded), "a level with a hole reloads and compiles")) {
                expect(
                    loaded.value().resource->get_blueprint().size() == 8,
                    "all eight records survive the round trip");
                expect(
                    loaded.value().compiled.level.region().inner_boundaries().size() == 1
                        && loaded.value().compiled.level.region().inner_boundaries()[0]
                                .vertices()
                                .front()
                            == Point { units(1), units(1) },
                    "the derived hole survives the round trip exactly");
                expect(
                    replay_known_solution(loaded.value().compiled),
                    "replaying the reloaded ring witness solves it");
            }
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
        const godot::Error saved = godot::ResourceSaver::get_singleton()->save(
            unit_square_palette(),
            wrong_type_path,
            godot::BitField<godot::ResourceSaver::SaverFlags>(
                godot::ResourceSaver::FLAG_BUNDLE_RESOURCES));
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
        // A persisted artifact which the compiler refuses. Export cannot write
        // one, so the saver is used directly to produce it.
        godot::Ref<LevelResource> invalid = fixture_level();
        invalid->set_format_version(2);
        const godot::Error saved = godot::ResourceSaver::get_singleton()->save(
            invalid,
            invalid_path,
            godot::BitField<godot::ResourceSaver::SaverFlags>(
                godot::ResourceSaver::FLAG_BUNDLE_RESOURCES));
        if (expect(saved == godot::OK, "an invalid level can be persisted directly")) {
            auto loaded = load_level_resource(invalid_path, p_catalog);
            if (expect(!loaded, "an invalid persisted level fails to load")) {
                expect(
                    loaded.error().code == LoadLevelErrorCode::compilation_failed,
                    "an invalid persisted level fails at the compilation stage");
                expect(
                    loaded.error().compilation_error.has_value()
                        && loaded.error().compilation_error.value().code
                            == LevelResourceErrorCode::unsupported_format_version,
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

bool ResourceIntegrationRunner::same_arrangement(
    const Arrangement &p_lhs, const Arrangement &p_rhs) {
    if (p_lhs.entries().size() != p_rhs.entries().size()) {
        return false;
    }
    for (std::size_t i = 0; i < p_lhs.entries().size(); ++i) {
        const Placement &lhs = p_lhs.entries()[i].placement;
        const Placement &rhs = p_rhs.entries()[i].placement;
        if (p_lhs.entries()[i].id != p_rhs.entries()[i].id) {
            return false;
        }
        if (lhs.prototile().id() != rhs.prototile().id()) {
            return false;
        }
        if (lhs.orientation() != rhs.orientation()) {
            return false;
        }
        if (lhs.translation() != rhs.translation()) {
            return false;
        }
        if (!same_boundary(lhs.footprint(), rhs.footprint())) {
            return false;
        }
    }
    return true;
}

bool ResourceIntegrationRunner::same_blueprint(
    const std::vector<engine::BlueprintPlacement> &p_lhs,
    const std::vector<engine::BlueprintPlacement> &p_rhs) {
    if (p_lhs.size() != p_rhs.size()) {
        return false;
    }
    for (std::size_t i = 0; i < p_lhs.size(); ++i) {
        if (p_lhs[i].prototile_id != p_rhs[i].prototile_id) {
            return false;
        }
        if (p_lhs[i].orientation != p_rhs[i].orientation) {
            return false;
        }
        if (p_lhs[i].translation != p_rhs[i].translation) {
            return false;
        }
    }
    return true;
}

// --- known-solution replay ---

bool ResourceIntegrationRunner::replay_known_solution(
    const CompiledLevelResource &p_compiled) {
    // A fresh ordinary runtime state: the witness arrangement is authoring
    // proof and is deliberately not installed here.
    engine::State state(p_compiled.level);

    for (const engine::BlueprintPlacement &record : p_compiled.blueprint) {
        const std::vector<engine::PaletteEntry> &entries = state.palette().entries();

        std::optional<std::size_t> entry_index;
        for (std::size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].prototile().id() == record.prototile_id) {
                entry_index = i;
                break;
            }
        }
        if (!entry_index.has_value()) {
            return false;
        }

        std::optional<std::size_t> orientation_index;
        const std::vector<OrientedPrototile> &orientations =
            entries[entry_index.value()].orientations();
        for (std::size_t i = 0; i < orientations.size(); ++i) {
            if (orientations[i].orientation() == record.orientation) {
                orientation_index = i;
                break;
            }
        }
        if (!orientation_index.has_value()) {
            return false;
        }

        const engine::PlaceCommand command {
            engine::PaletteEntryIndex(entry_index.value()),
            engine::PaletteOrientationIndex(orientation_index.value()),
            record.translation,
        };
        if (!state.apply(command)) {
            return false;
        }
    }

    return state.solved();
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

    check_registration();
    check_resource_defaults();
    check_property_metadata();
    check_setter_notifications();
    check_palette_compilation(catalog.value());
    check_domain_palette_compilation(catalog.value());
    check_palette_size_limit(catalog.value());
    check_blueprint_decoding();
    check_level_compilation(catalog.value());
    check_region_derivation(catalog.value());
    check_compilation_purity(catalog.value());
    check_encoding(catalog.value());
    check_authored_fixture(catalog.value());
    check_export_and_load(catalog.value());
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
