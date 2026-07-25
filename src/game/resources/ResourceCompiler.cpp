#include "game/resources/ResourceCompiler.h"

#include "core/Orientation.h"
#include "core/geometry/Point.h"
#include "engine/Supply.h"

#include <godot_cpp/variant/vector2.hpp>

#include <utility>
#include <vector>

namespace tiles::game {

namespace {

// --- polygon ---

PolygonResourceError polygon_missing() {
    PolygonResourceError error {};
    error.code = PolygonResourceErrorCode::missing_resource;
    return error;
}

PolygonResourceError quantization_failure(
    std::size_t p_vertex, CoordinateAxis p_axis, QuantizationError p_error) {
    PolygonResourceError error {};
    error.code = PolygonResourceErrorCode::coordinate_quantization_failed;
    error.vertex = p_vertex;
    error.axis = p_axis;
    error.quantization_error = p_error;
    return error;
}

PolygonResourceError polygon_construction_failure(PolygonError p_error) {
    PolygonResourceError error {};
    error.code = PolygonResourceErrorCode::polygon_construction_failed;
    error.polygon_error = p_error;
    return error;
}

// --- palette ---

PaletteResourceError palette_failure(PaletteResourceErrorCode p_code) {
    PaletteResourceError error {};
    error.code = p_code;
    return error;
}

PaletteResourceError entry_failure(PaletteResourceErrorCode p_code, std::size_t p_entry) {
    PaletteResourceError error = palette_failure(p_code);
    error.entry = p_entry;
    return error;
}

// --- region ---

RegionResourceError region_failure(RegionResourceErrorCode p_code) {
    RegionResourceError error {};
    error.code = p_code;
    return error;
}

// --- level ---

LevelResourceError level_failure(LevelResourceErrorCode p_code) {
    LevelResourceError error {};
    error.code = p_code;
    return error;
}

} // namespace

Result<Polygon, PolygonResourceError> compile_polygon_resource(
    const godot::Ref<PolygonResource> &p_resource) {
    using Compiled = Result<Polygon, PolygonResourceError>;

    if (p_resource.is_null()) {
        return Compiled::failure(polygon_missing());
    }

    const godot::PackedVector2Array vertices = p_resource->get_vertices();

    std::vector<Point> points;
    points.reserve(static_cast<std::size_t>(vertices.size()));
    for (std::int64_t i = 0; i < vertices.size(); ++i) {
        const std::size_t index = static_cast<std::size_t>(i);
        const godot::Vector2 vertex = vertices[i];

        // One quantization per component, in authored order, x then y. The
        // Godot value is widened to double and handed to the single existing
        // deterministic quantizer; nothing else converts it.
        auto x = quantize_double(static_cast<double>(vertex.x));
        if (!x) {
            return Compiled::failure(
                quantization_failure(index, CoordinateAxis::x, x.error()));
        }
        auto y = quantize_double(static_cast<double>(vertex.y));
        if (!y) {
            return Compiled::failure(
                quantization_failure(index, CoordinateAxis::y, y.error()));
        }

        points.push_back(Point { x.value(), y.value() });
    }

    auto polygon = Polygon::make(std::move(points));
    if (!polygon) {
        return Compiled::failure(polygon_construction_failure(polygon.error()));
    }
    return Compiled::success(std::move(polygon).value());
}

Result<engine::Palette, PaletteResourceError> compile_palette_resource(
    const godot::Ref<PaletteResource> &p_resource,
    const content::PrototileCatalog &p_catalog) {
    using Compiled = Result<engine::Palette, PaletteResourceError>;

    if (p_resource.is_null()) {
        return Compiled::failure(palette_failure(PaletteResourceErrorCode::missing_resource));
    }

    // The uniform rotation rule for this milestone. The core compiler collapses
    // geometrically identical results, so a square still offers one choice.
    const std::vector<Orientation> requested = {
        Orientation::reference(),
        Orientation::quarter(),
        Orientation::half(),
        Orientation::three_quarter(),
    };

    const godot::TypedArray<PaletteEntryResource> authored = p_resource->get_entries();

    std::vector<engine::PaletteEntry> entries;
    entries.reserve(static_cast<std::size_t>(authored.size()));

    for (std::int64_t i = 0; i < authored.size(); ++i) {
        const std::size_t index = static_cast<std::size_t>(i);

        const godot::Ref<PaletteEntryResource> authored_entry = authored[i];
        if (authored_entry.is_null()) {
            return Compiled::failure(
                entry_failure(PaletteResourceErrorCode::missing_entry, index));
        }

        const std::int64_t encoded_id = authored_entry->get_prototile_id();
        if (encoded_id < 0) {
            PaletteResourceError error =
                entry_failure(PaletteResourceErrorCode::negative_prototile_id, index);
            error.encoded_prototile_id = encoded_id;
            return Compiled::failure(error);
        }

        // The sign is settled before conversion, so no negative value is ever
        // reinterpreted as an enormous unsigned identity.
        const PrototileId id(static_cast<PrototileId::Value>(encoded_id));

        const content::CanonicalPrototile *canonical = p_catalog.find(id);
        if (canonical == nullptr) {
            PaletteResourceError error =
                entry_failure(PaletteResourceErrorCode::unknown_prototile_id, index);
            error.encoded_prototile_id = encoded_id;
            error.prototile_id = id;
            return Compiled::failure(error);
        }

        const std::int64_t encoded_supply = authored_entry->get_supply();
        std::optional<engine::Supply> supply;
        if (encoded_supply == -1) {
            supply = engine::Supply::unlimited();
        } else if (encoded_supply > 0) {
            // Positive and therefore representable, but the core factory's
            // result is still inspected rather than assumed.
            auto finite = engine::Supply::finite(
                static_cast<engine::Supply::Amount>(encoded_supply));
            if (finite) {
                supply = finite.value();
            }
        }
        if (!supply.has_value()) {
            PaletteResourceError error =
                entry_failure(PaletteResourceErrorCode::invalid_supply, index);
            error.encoded_prototile_id = encoded_id;
            error.prototile_id = id;
            error.encoded_supply = encoded_supply;
            return Compiled::failure(error);
        }

        // The catalog-owned exact prototile, never a re-quantized or
        // resource-authored copy of it.
        auto entry = engine::PaletteEntry::make(
            canonical->prototile(), supply.value(), requested);
        if (!entry) {
            PaletteResourceError error = entry_failure(
                PaletteResourceErrorCode::orientation_compilation_failed, index);
            error.encoded_prototile_id = encoded_id;
            error.prototile_id = id;
            error.orientation_error = entry.error();
            return Compiled::failure(error);
        }

        entries.push_back(std::move(entry).value());
    }

    auto palette = engine::Palette::make(std::move(entries));
    if (!palette) {
        PaletteResourceError error =
            palette_failure(PaletteResourceErrorCode::palette_construction_failed);
        error.palette_error = palette.error();
        return Compiled::failure(error);
    }
    return Compiled::success(std::move(palette).value());
}

Result<Region, RegionResourceError> compile_region_resource(
    const godot::Ref<RegionResource> &p_resource) {
    using Compiled = Result<Region, RegionResourceError>;

    if (p_resource.is_null()) {
        return Compiled::failure(region_failure(RegionResourceErrorCode::missing_resource));
    }

    auto outer = compile_polygon_resource(p_resource->get_outer_boundary());
    if (!outer) {
        RegionResourceError error =
            region_failure(RegionResourceErrorCode::outer_boundary_invalid);
        error.polygon_error = outer.error();
        return Compiled::failure(error);
    }

    const godot::TypedArray<PolygonResource> authored_holes =
        p_resource->get_inner_boundaries();

    std::vector<Polygon> holes;
    holes.reserve(static_cast<std::size_t>(authored_holes.size()));
    for (std::int64_t i = 0; i < authored_holes.size(); ++i) {
        const godot::Ref<PolygonResource> authored_hole = authored_holes[i];
        auto hole = compile_polygon_resource(authored_hole);
        if (!hole) {
            RegionResourceError error =
                region_failure(RegionResourceErrorCode::inner_boundary_invalid);
            error.hole = static_cast<std::size_t>(i);
            error.polygon_error = hole.error();
            return Compiled::failure(error);
        }
        holes.push_back(std::move(hole).value());
    }

    auto region = Region::make(std::move(outer).value(), std::move(holes));
    if (!region) {
        RegionResourceError error =
            region_failure(RegionResourceErrorCode::region_construction_failed);
        error.region_error = region.error();
        return Compiled::failure(error);
    }
    return Compiled::success(std::move(region).value());
}

Result<engine::Level, LevelResourceError> compile_level_resource(
    const godot::Ref<LevelResource> &p_resource,
    const content::PrototileCatalog &p_catalog) {
    using Compiled = Result<engine::Level, LevelResourceError>;

    if (p_resource.is_null()) {
        return Compiled::failure(level_failure(LevelResourceErrorCode::missing_resource));
    }

    auto palette = compile_palette_resource(p_resource->get_palette(), p_catalog);
    if (!palette) {
        LevelResourceError error = level_failure(LevelResourceErrorCode::palette_invalid);
        error.palette_error = palette.error();
        return Compiled::failure(error);
    }

    auto region = compile_region_resource(p_resource->get_region());
    if (!region) {
        LevelResourceError error = level_failure(LevelResourceErrorCode::region_invalid);
        error.region_error = region.error();
        return Compiled::failure(error);
    }

    return Compiled::success(
        engine::Level(std::move(palette).value(), std::move(region).value()));
}

} // namespace tiles::game
