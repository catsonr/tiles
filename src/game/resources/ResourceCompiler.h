#pragma once

#include "content/CanonicalOrientationCompiler.h"
#include "content/GeometryDomain.h"
#include "content/PrototileCatalog.h"
#include "core/OrientedPrototile.h"
#include "core/Prototile.h"
#include "core/Region.h"
#include "core/Result.h"
#include "core/geometry/Coordinate.h"
#include "core/geometry/Polygon.h"
#include "engine/Level.h"
#include "engine/Palette.h"
#include "game/resources/LevelResources.h"

#include <godot_cpp/classes/ref.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>

namespace tiles::game {

// The one-way compiler from authored Godot transport into the exact runtime
// model.
//
// Every operation is pure with respect to its resource and its catalog: it
// mutates nothing, canonicalizes nothing back into a resource, emits no change
// notification, caches no exact value inside a resource, treats no resource
// path or pointer as domain identity, logs no expected invalid input, accepts
// no fallback prototile for an unknown id, and never enters an arrangement or
// play session.
//
// Polygon, palette, region, and level compilation are independently reusable so
// a draft's parts can be validated separately without reimplementing a subset
// of the level compiler.

enum class CoordinateAxis {
    x,
    y,
};

enum class PolygonResourceErrorCode {
    missing_resource,
    coordinate_quantization_failed,
    polygon_construction_failed,
};

// missing_resource populates no payload; coordinate_quantization_failed
// populates vertex, axis, and quantization_error; polygon_construction_failed
// populates only polygon_error.
struct PolygonResourceError final {
    PolygonResourceErrorCode code;
    std::optional<std::size_t> vertex;
    std::optional<CoordinateAxis> axis;
    std::optional<QuantizationError> quantization_error;
    std::optional<PolygonError> polygon_error;
};

enum class PaletteResourceErrorCode {
    missing_resource,
    unsupported_geometry_domain,
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
// Invalid supply populates its signed encoding. Orientation, entry, and palette
// failures preserve the complete native errors, each populated only by the code
// it belongs to.
struct PaletteResourceError final {
    PaletteResourceErrorCode code;
    std::optional<std::size_t> entry;
    std::optional<std::int64_t> encoded_prototile_id;
    std::optional<std::int64_t> encoded_supply;
    std::optional<PrototileId> prototile_id;
    std::optional<content::CanonicalOrientationCompilationError> orientation_error;
    std::optional<engine::PaletteEntryCompilationError> palette_entry_error;
    std::optional<engine::PaletteError> palette_error;
};

enum class RegionResourceErrorCode {
    missing_resource,
    outer_boundary_invalid,
    inner_boundary_invalid,
    region_construction_failed,
};

// `hole` is populated exactly for inner_boundary_invalid. An absent outer
// boundary is reported as outer_boundary_invalid carrying
// PolygonResourceError::missing_resource, so the nesting is uniform.
struct RegionResourceError final {
    RegionResourceErrorCode code;
    std::optional<std::size_t> hole;
    std::optional<PolygonResourceError> polygon_error;
    std::optional<RegionError> region_error;
};

enum class LevelResourceErrorCode {
    missing_resource,
    palette_invalid,
    region_invalid,
};

// Exactly one nested error is populated, matching the code.
struct LevelResourceError final {
    LevelResourceErrorCode code;
    std::optional<PaletteResourceError> palette_error;
    std::optional<RegionResourceError> region_error;
};

// Quantize each authored vertex onto the exact lattice in authored order, x
// before y, through the existing deterministic q16.48 quantizer, then build the
// polygon once through the ordinary core factory. Nothing here multiplies by
// Coordinate::SCALE, casts to coordinate storage, rounds through Godot, clamps,
// chooses an epsilon, requires whole coordinates, or pre-canonicalizes order.
Result<Polygon, PolygonResourceError> compile_polygon_resource(
    const godot::Ref<PolygonResource> &p_resource);

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
Result<engine::Palette, PaletteResourceError> compile_palette_resource(
    content::GeometryDomain p_domain,
    const godot::Ref<PaletteResource> &p_resource,
    const content::PrototileCatalog &p_catalog);

// Compile the outer boundary, then every hole in authored order, then prove the
// region relationship through the ordinary core factory.
Result<Region, RegionResourceError> compile_region_resource(
    const godot::Ref<RegionResource> &p_resource);

// Compile the palette, then the region, then pair them. Construction of the
// level itself cannot fail: both members are already proof-bearing exact values.
//
// The level resource serializes no geometry domain, so it remains lattice-only
// and compiles its palette in GeometryDomain::lattice.
Result<engine::Level, LevelResourceError> compile_level_resource(
    const godot::Ref<LevelResource> &p_resource,
    const content::PrototileCatalog &p_catalog);

} // namespace tiles::game
