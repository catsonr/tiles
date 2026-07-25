#pragma once

#include "content/CanonicalOrientationCompiler.h"
#include "content/GeometryDomain.h"
#include "content/PrototileCatalog.h"
#include "core/Arrangement.h"
#include "core/ArrangementRegion.h"
#include "core/Orientation.h"
#include "core/Prototile.h"
#include "core/Result.h"
#include "engine/Blueprint.h"
#include "engine/Level.h"
#include "engine/Palette.h"
#include "engine/Supply.h"
#include "game/resources/LevelResources.h"

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/typed_array.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace tiles::game {

// The one-way compiler from authored Godot transport into the exact runtime
// model, and the one encoder back out of exact values into fresh transport.
//
// Every compilation is pure with respect to its resource and its catalog: it
// mutates nothing, canonicalizes nothing back into a resource, emits no change
// notification, caches no exact value inside a resource, treats no resource
// path or pointer as domain identity, logs no expected invalid input, accepts
// no fallback prototile for an unknown id, reads no color into exact geometry,
// and never enters a play session.
//
// Palette, blueprint, and level compilation are independently reusable so a
// draft's parts can be validated separately without reimplementing a subset of
// the level compiler.

// The largest blueprint an externally supplied resource may carry. It is a
// transport boundary, not a mathematical one: engine::compile_blueprint and
// region_from_arrangement remain uncapped, and this limits the work reachable
// before the quadratic arrangement and boundary compilers run. Authored levels
// are roughly fifty placements or fewer.
constexpr std::size_t MAX_BLUEPRINT_PLACEMENTS = 64;

enum class PaletteResourceErrorCode {
    missing_resource,
    unsupported_geometry_domain,
    too_many_entries,
    missing_entry,
    negative_prototile_id,
    unknown_prototile_id,
    prototile_unavailable_in_domain,
    invalid_supply,
    orientation_compilation_failed,
    palette_entry_construction_failed,
    palette_construction_failed,
};

// Every failure tied to one authored entry populates `entry`. Id failures
// populate the signed encoded value; once a nonnegative id has been converted,
// unknown-id and later entry failures also populate the strong PrototileId.
// Invalid supply populates its signed encoding. too_many_entries populates the
// two counts and nothing else. Orientation, entry, and palette failures
// preserve the complete native errors, each populated only by the code it
// belongs to.
struct PaletteResourceError final {
    PaletteResourceErrorCode code;
    std::optional<std::size_t> entry;
    std::optional<std::int64_t> encoded_prototile_id;
    std::optional<std::int64_t> encoded_supply;
    std::optional<PrototileId> prototile_id;
    // too_many_entries: how many entries were authored, and how many distinct
    // canonical identities the selected domain admits at all.
    std::optional<std::size_t> entry_count;
    std::optional<std::size_t> maximum_entry_count;
    std::optional<content::CanonicalOrientationCompilationError> orientation_error;
    std::optional<engine::PaletteEntryCompilationError> palette_entry_error;
    std::optional<engine::PaletteError> palette_error;
};

enum class BlueprintResourceErrorCode {
    too_many_placements,
    missing_placement,
    negative_prototile_id,
    orientation_step_out_of_range,
    orientation_order_out_of_range,
    invalid_orientation,
};

// too_many_placements is a property of the whole array and populates only the
// two counts. Every other failure names its record index and populates only the
// signed encoded value, or the complete OrientationError, which its own code
// concerns.
struct BlueprintResourceError final {
    BlueprintResourceErrorCode code;
    // too_many_placements
    std::optional<std::size_t> placement_count;
    std::optional<std::size_t> maximum_placement_count;
    // every per-record failure
    std::optional<std::size_t> placement;
    std::optional<std::int64_t> encoded_prototile_id;
    std::optional<std::int64_t> encoded_orientation_step;
    std::optional<std::int64_t> encoded_orientation_order;
    std::optional<OrientationError> orientation_error;
};

enum class LevelResourceErrorCode {
    missing_resource,
    unsupported_format_version,
    unsupported_geometry_domain,
    palette_invalid,
    blueprint_resource_invalid,
    blueprint_arrangement_invalid,
    arrangement_region_invalid,
};

// One code per compilation stage, and exactly the payload that stage produces:
// the signed encoded value for the version and domain failures, and otherwise
// the complete nested error of the compiler which refused. Nothing is
// flattened to text, repaired, or accompanied by a partial compiled product.
struct LevelResourceError final {
    LevelResourceErrorCode code;
    std::optional<std::int64_t> encoded_format_version;
    std::optional<std::int64_t> encoded_geometry_domain;
    std::optional<PaletteResourceError> palette_error;
    std::optional<BlueprintResourceError> blueprint_error;
    std::optional<engine::BlueprintCompilationError> arrangement_error;
    std::optional<ArrangementRegionError> region_error;
};

// The complete exact product of one authored level artifact.
//
// It has one authority per meaning: `domain` records which source compiler
// interpreted the transport, `blueprint` is the decoded reconstructable
// witness, `arrangement` is the exact proof compiled from that witness, and
// `level` owns the exact palette and region runtime play uses. Neither palette
// nor region is duplicated beside `level`.
//
// The arrangement is authoring proof, not runtime state. An engine::State built
// from `level` still begins with an empty arrangement and receives no witness
// placement, so the stored witness never becomes player progress and never
// privileges itself over another legal solution.
struct CompiledLevelResource final {
    content::GeometryDomain domain;
    std::vector<engine::BlueprintPlacement> blueprint;
    Arrangement arrangement;
    engine::Level level;
};

// Resolve every authored entry's id through the supplied catalog and build the
// runtime palette in authored order, interpreting every entry in one geometry
// domain.
//
// The domain is the first argument because it governs every later step: which
// canonical identities are admissible at all, and which exact source compiler
// produces their orientations. An id the domain does not admit is rejected
// rather than compiled through the other domain's compiler, and no domain
// reaches the published palette. Authored color is read by no part of this
// operation.
//
// Once the domain is known, an array longer than the number of identities that
// domain admits is refused before any entry is inspected: a valid palette
// cannot exceed that count, because Palette requires distinct ids.
Result<engine::Palette, PaletteResourceError> compile_palette_resource(
    content::GeometryDomain p_domain,
    const godot::Ref<PaletteResource> &p_resource,
    const content::PrototileCatalog &p_catalog);

// Decode a complete blueprint transport array into exact records, in array
// order.
//
// The array size is checked before the record vector is reserved and before any
// record is inspected. Each record is then validated in one fixed precedence —
// null resource, negative id, orientation step range, orientation order range,
// orientation construction — and its translation is read as the two
// authoritative raw q16.48 integers it already is. Zero is representable as an
// unsigned order, so an encoded order of zero reaches Orientation::make and
// preserves OrientationError::zero_order rather than being refused as an
// out-of-range signed value.
//
// Orientation::make canonicalizes the rational turn, and compile_blueprint
// later requires equality with a compiled variant's representative. An
// equivalent but nonrepresentative label therefore decodes successfully here
// and fails later as orientation_not_in_palette.
Result<std::vector<engine::BlueprintPlacement>, BlueprintResourceError>
compile_blueprint_resource(
    const godot::TypedArray<BlueprintPlacementResource> &p_resources);

// Compile one complete authored level artifact into its exact product.
//
// The stages run in exactly one order — resource, format version, geometry
// domain, palette, blueprint records, arrangement, region — and the first
// failure stops compilation and publishes no partial product. The region is
// derived from the arrangement alone; no authored region is accepted, compared
// against, or repaired.
Result<CompiledLevelResource, LevelResourceError> compile_level_resource(
    const godot::Ref<LevelResource> &p_resource,
    const content::PrototileCatalog &p_catalog);

enum class LevelResourceEncodingErrorCode {
    unsupported_geometry_domain,
    color_count_mismatch,
    prototile_id_not_representable,
    supply_not_representable,
    too_many_placements,
    blueprint_prototile_id_not_representable,
};

// Representability failures name the palette entry or blueprint record they
// belong to and carry the exact unsigned value which does not fit the signed
// transport. Count failures carry the actual count and the count that was
// required — the palette's entry count for colors, and the transport maximum
// for the blueprint.
struct LevelResourceEncodingError final {
    LevelResourceEncodingErrorCode code;
    std::optional<std::size_t> entry;
    std::optional<std::size_t> placement;
    std::optional<PrototileId::Value> prototile_id;
    std::optional<engine::Supply::Amount> supply;
    std::optional<std::size_t> actual_count;
    std::optional<std::size_t> expected_count;
};

// Encode exact authoring values as one completely fresh resource graph.
//
// The exact palette, the parallel authored colors in palette order, and the
// exact blueprint are the authoritative input: this accepts no rendered
// geometry, palette-row index, proposal state, or existing LevelResource, and
// it reuses no previously constructed subresource. Every child is new and
// unpathed, so the result is self-contained wherever it is later written.
//
// Encoding does not compile its own output. A caller wanting that independent
// proof calls the public compiler on the returned graph.
Result<godot::Ref<LevelResource>, LevelResourceEncodingError> make_level_resource(
    content::GeometryDomain p_domain,
    const engine::Palette &p_palette,
    const std::vector<godot::Color> &p_colors,
    const std::vector<engine::BlueprintPlacement> &p_blueprint);

} // namespace tiles::game
