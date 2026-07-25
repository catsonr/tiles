#include "game/resources/ResourceCompiler.h"

#include "content/CanonicalOrientationCompiler.h"
#include "content/GeometryDomain.h"
#include "core/geometry/Coordinate.h"
#include "core/geometry/Point.h"
#include "engine/Supply.h"

#include <limits>
#include <utility>
#include <vector>

namespace tiles::game {

namespace {

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

// Whether a transported domain value names one of the two geometry domains. A
// value cast from an arbitrary integer names neither.
bool is_known_domain(content::GeometryDomain p_domain) {
    switch (p_domain) {
        case content::GeometryDomain::lattice:
        case content::GeometryDomain::hex12:
            return true;
    }
    return false;
}

// --- blueprint ---

BlueprintResourceError blueprint_failure(BlueprintResourceErrorCode p_code) {
    BlueprintResourceError error {};
    error.code = p_code;
    return error;
}

BlueprintResourceError record_failure(
    BlueprintResourceErrorCode p_code, std::size_t p_placement) {
    BlueprintResourceError error = blueprint_failure(p_code);
    error.placement = p_placement;
    return error;
}

// Whether a signed transported component is representable as the unsigned
// component Orientation is built from. Zero is representable and therefore
// reaches Orientation::make, which is what preserves zero_order as an
// orientation failure rather than a transport failure.
bool fits_orientation_component(std::int64_t p_value) {
    return p_value >= 0
        && p_value <= static_cast<std::int64_t>(std::numeric_limits<Orientation::Component>::max());
}

// --- level ---

LevelResourceError level_failure(LevelResourceErrorCode p_code) {
    LevelResourceError error {};
    error.code = p_code;
    return error;
}

// The stable serialized domain encoding, decoded. Every other signed value
// names no domain.
std::optional<content::GeometryDomain> decode_geometry_domain(std::int64_t p_encoded) {
    if (p_encoded == ENCODED_GEOMETRY_DOMAIN_LATTICE) {
        return content::GeometryDomain::lattice;
    }
    if (p_encoded == ENCODED_GEOMETRY_DOMAIN_HEX12) {
        return content::GeometryDomain::hex12;
    }
    return std::nullopt;
}

// --- encoding ---

LevelResourceEncodingError encoding_failure(LevelResourceEncodingErrorCode p_code) {
    LevelResourceEncodingError error {};
    error.code = p_code;
    return error;
}

// Whether an exact unsigned value survives the signed Godot integer transport
// unchanged.
bool fits_signed_transport(std::uint64_t p_value) {
    return p_value <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
}

} // namespace

Result<engine::Palette, PaletteResourceError> compile_palette_resource(
    content::GeometryDomain p_domain,
    const godot::Ref<PaletteResource> &p_resource,
    const content::PrototileCatalog &p_catalog) {
    using Compiled = Result<engine::Palette, PaletteResourceError>;

    if (p_resource.is_null()) {
        return Compiled::failure(palette_failure(PaletteResourceErrorCode::missing_resource));
    }

    // A domain cast from an arbitrary integer is rejected once, before any
    // authored entry is read, rather than silently answering as one domain per
    // entry.
    if (!is_known_domain(p_domain)) {
        return Compiled::failure(
            palette_failure(PaletteResourceErrorCode::unsupported_geometry_domain));
    }

    const godot::TypedArray<PaletteEntryResource> authored = p_resource->get_entries();
    const std::size_t authored_count = static_cast<std::size_t>(authored.size());

    // A palette needs distinct ids, so it can never be longer than the number
    // of identities this domain admits. Refusing that here bounds the work an
    // external transport can ask for before a single entry is inspected or
    // compiled.
    const std::size_t admitted = p_catalog.entries_for(p_domain).size();
    if (authored_count > admitted) {
        PaletteResourceError error =
            palette_failure(PaletteResourceErrorCode::too_many_entries);
        error.entry_count = authored_count;
        error.maximum_entry_count = admitted;
        return Compiled::failure(error);
    }

    std::vector<engine::PaletteEntry> entries;
    entries.reserve(authored_count);

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

        // A known identity the selected domain does not admit is refused here.
        // Nothing substitutes the other domain's compiler, a congruent id, or a
        // fallback shape.
        if (!canonical->supports(p_domain)) {
            PaletteResourceError error = entry_failure(
                PaletteResourceErrorCode::prototile_unavailable_in_domain, index);
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

        // The catalog-owned canonical entry compiled in the selected domain,
        // never a re-quantized or resource-authored copy of its geometry. The
        // domain chooses the source compiler here and disappears immediately
        // afterwards.
        auto compiled_orientations =
            content::compile_canonical_orientations(p_domain, *canonical);
        if (!compiled_orientations) {
            PaletteResourceError error = entry_failure(
                PaletteResourceErrorCode::orientation_compilation_failed, index);
            error.encoded_prototile_id = encoded_id;
            error.prototile_id = id;
            error.orientation_error = compiled_orientations.error();
            return Compiled::failure(error);
        }

        auto entry = engine::PaletteEntry::make_compiled(
            supply.value(), std::move(compiled_orientations).value());
        if (!entry) {
            PaletteResourceError error = entry_failure(
                PaletteResourceErrorCode::palette_entry_construction_failed, index);
            error.encoded_prototile_id = encoded_id;
            error.prototile_id = id;
            error.palette_entry_error = entry.error();
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

Result<std::vector<engine::BlueprintPlacement>, BlueprintResourceError>
compile_blueprint_resource(
    const godot::TypedArray<BlueprintPlacementResource> &p_resources) {
    using Compiled = Result<std::vector<engine::BlueprintPlacement>, BlueprintResourceError>;

    const std::size_t count = static_cast<std::size_t>(p_resources.size());

    // The transport boundary, answered before the record vector is reserved and
    // before any record is read.
    if (count > MAX_BLUEPRINT_PLACEMENTS) {
        BlueprintResourceError error =
            blueprint_failure(BlueprintResourceErrorCode::too_many_placements);
        error.placement_count = count;
        error.maximum_placement_count = MAX_BLUEPRINT_PLACEMENTS;
        return Compiled::failure(error);
    }

    std::vector<engine::BlueprintPlacement> records;
    records.reserve(count);

    for (std::int64_t i = 0; i < p_resources.size(); ++i) {
        const std::size_t index = static_cast<std::size_t>(i);

        const godot::Ref<BlueprintPlacementResource> authored = p_resources[i];
        if (authored.is_null()) {
            return Compiled::failure(
                record_failure(BlueprintResourceErrorCode::missing_placement, index));
        }

        const std::int64_t encoded_id = authored->get_prototile_id();
        if (encoded_id < 0) {
            BlueprintResourceError error =
                record_failure(BlueprintResourceErrorCode::negative_prototile_id, index);
            error.encoded_prototile_id = encoded_id;
            return Compiled::failure(error);
        }
        // Every nonnegative signed id fits the unsigned strong identity, and the
        // sign is settled before conversion.
        const PrototileId id(static_cast<PrototileId::Value>(encoded_id));

        const std::int64_t encoded_step = authored->get_orientation_step();
        if (!fits_orientation_component(encoded_step)) {
            BlueprintResourceError error = record_failure(
                BlueprintResourceErrorCode::orientation_step_out_of_range, index);
            error.encoded_orientation_step = encoded_step;
            return Compiled::failure(error);
        }

        const std::int64_t encoded_order = authored->get_orientation_order();
        if (!fits_orientation_component(encoded_order)) {
            BlueprintResourceError error = record_failure(
                BlueprintResourceErrorCode::orientation_order_out_of_range, index);
            error.encoded_orientation_order = encoded_order;
            return Compiled::failure(error);
        }

        auto orientation = Orientation::make(
            static_cast<Orientation::Component>(encoded_step),
            static_cast<Orientation::Component>(encoded_order));
        if (!orientation) {
            BlueprintResourceError error =
                record_failure(BlueprintResourceErrorCode::invalid_orientation, index);
            error.orientation_error = orientation.error();
            return Compiled::failure(error);
        }

        // The translation is already exact: each component is one authoritative
        // q16.48 bit pattern, and every signed value denotes one lattice
        // coordinate. Nothing here scales, quantizes, or rounds.
        const Point translation {
            Coordinate::from_raw(authored->get_translation_x_raw()),
            Coordinate::from_raw(authored->get_translation_y_raw()),
        };

        records.push_back(
            engine::BlueprintPlacement { id, orientation.value(), translation });
    }

    return Compiled::success(std::move(records));
}

Result<CompiledLevelResource, LevelResourceError> compile_level_resource(
    const godot::Ref<LevelResource> &p_resource,
    const content::PrototileCatalog &p_catalog) {
    using Compiled = Result<CompiledLevelResource, LevelResourceError>;

    if (p_resource.is_null()) {
        return Compiled::failure(level_failure(LevelResourceErrorCode::missing_resource));
    }

    // One schema version is read, exactly. There is no migration, compatibility
    // range, best-effort interpretation, or fallback: a future artifact is
    // refused rather than half-understood.
    const std::int64_t encoded_version = p_resource->get_format_version();
    if (encoded_version != LEVEL_RESOURCE_FORMAT_VERSION) {
        LevelResourceError error =
            level_failure(LevelResourceErrorCode::unsupported_format_version);
        error.encoded_format_version = encoded_version;
        return Compiled::failure(error);
    }

    const std::int64_t encoded_domain = p_resource->get_geometry_domain();
    const std::optional<content::GeometryDomain> domain =
        decode_geometry_domain(encoded_domain);
    if (!domain.has_value()) {
        LevelResourceError error =
            level_failure(LevelResourceErrorCode::unsupported_geometry_domain);
        error.encoded_geometry_domain = encoded_domain;
        return Compiled::failure(error);
    }

    auto palette =
        compile_palette_resource(domain.value(), p_resource->get_palette(), p_catalog);
    if (!palette) {
        LevelResourceError error = level_failure(LevelResourceErrorCode::palette_invalid);
        error.palette_error = palette.error();
        return Compiled::failure(error);
    }

    auto records = compile_blueprint_resource(p_resource->get_blueprint());
    if (!records) {
        LevelResourceError error =
            level_failure(LevelResourceErrorCode::blueprint_resource_invalid);
        error.blueprint_error = records.error();
        return Compiled::failure(error);
    }

    auto arrangement = engine::compile_blueprint(palette.value(), records.value());
    if (!arrangement) {
        LevelResourceError error =
            level_failure(LevelResourceErrorCode::blueprint_arrangement_invalid);
        error.arrangement_error = arrangement.error();
        return Compiled::failure(error);
    }

    // The one region this level has. It is derived from the arrangement's own
    // coverage, so an empty blueprint fails here as an empty arrangement.
    auto region = region_from_arrangement(arrangement.value());
    if (!region) {
        LevelResourceError error =
            level_failure(LevelResourceErrorCode::arrangement_region_invalid);
        error.region_error = region.error();
        return Compiled::failure(error);
    }

    return Compiled::success(CompiledLevelResource {
        domain.value(),
        std::move(records).value(),
        std::move(arrangement).value(),
        engine::Level(std::move(palette).value(), std::move(region).value()),
    });
}

Result<godot::Ref<LevelResource>, LevelResourceEncodingError> make_level_resource(
    content::GeometryDomain p_domain,
    const engine::Palette &p_palette,
    const std::vector<godot::Color> &p_colors,
    const std::vector<engine::BlueprintPlacement> &p_blueprint) {
    using Encoded = Result<godot::Ref<LevelResource>, LevelResourceEncodingError>;

    if (!is_known_domain(p_domain)) {
        return Encoded::failure(
            encoding_failure(LevelResourceEncodingErrorCode::unsupported_geometry_domain));
    }

    if (p_colors.size() != p_palette.entries().size()) {
        LevelResourceEncodingError error =
            encoding_failure(LevelResourceEncodingErrorCode::color_count_mismatch);
        error.actual_count = p_colors.size();
        error.expected_count = p_palette.entries().size();
        return Encoded::failure(error);
    }

    // Representability is proven for the complete input before one child
    // resource exists, so a refusal leaves nothing half-built behind.
    for (std::size_t i = 0; i < p_palette.entries().size(); ++i) {
        const PrototileId::Value id = p_palette.entries()[i].prototile().id().value();
        if (!fits_signed_transport(id)) {
            LevelResourceEncodingError error = encoding_failure(
                LevelResourceEncodingErrorCode::prototile_id_not_representable);
            error.entry = i;
            error.prototile_id = id;
            return Encoded::failure(error);
        }
    }

    for (std::size_t i = 0; i < p_palette.entries().size(); ++i) {
        const std::optional<engine::Supply::Amount> amount =
            p_palette.entries()[i].supply().finite_amount();
        if (amount.has_value() && !fits_signed_transport(amount.value())) {
            LevelResourceEncodingError error =
                encoding_failure(LevelResourceEncodingErrorCode::supply_not_representable);
            error.entry = i;
            error.supply = amount.value();
            return Encoded::failure(error);
        }
    }

    if (p_blueprint.size() > MAX_BLUEPRINT_PLACEMENTS) {
        LevelResourceEncodingError error =
            encoding_failure(LevelResourceEncodingErrorCode::too_many_placements);
        error.actual_count = p_blueprint.size();
        error.expected_count = MAX_BLUEPRINT_PLACEMENTS;
        return Encoded::failure(error);
    }

    for (std::size_t i = 0; i < p_blueprint.size(); ++i) {
        const PrototileId::Value id = p_blueprint[i].prototile_id.value();
        if (!fits_signed_transport(id)) {
            LevelResourceEncodingError error = encoding_failure(
                LevelResourceEncodingErrorCode::blueprint_prototile_id_not_representable);
            error.placement = i;
            error.prototile_id = id;
            return Encoded::failure(error);
        }
    }

    godot::TypedArray<PaletteEntryResource> authored_entries;
    for (std::size_t i = 0; i < p_palette.entries().size(); ++i) {
        const engine::PaletteEntry &entry = p_palette.entries()[i];
        godot::Ref<PaletteEntryResource> authored;
        authored.instantiate();
        authored->set_prototile_id(static_cast<std::int64_t>(entry.prototile().id().value()));
        // Unlimited is the one sentinel; a finite capacity writes its exact
        // positive value.
        authored->set_supply(
            entry.supply().is_unlimited()
                ? std::int64_t(-1)
                : static_cast<std::int64_t>(entry.supply().finite_amount().value()));
        authored->set_color(p_colors[i]);
        authored_entries.push_back(authored);
    }

    godot::Ref<PaletteResource> palette;
    palette.instantiate();
    palette->set_entries(authored_entries);

    godot::TypedArray<BlueprintPlacementResource> authored_blueprint;
    for (const engine::BlueprintPlacement &record : p_blueprint) {
        godot::Ref<BlueprintPlacementResource> authored;
        authored.instantiate();
        authored->set_prototile_id(
            static_cast<std::int64_t>(record.prototile_id.value()));
        // The canonical step/order pair, and the two raw q16.48 integers
        // exactly as the exact value already stores them.
        authored->set_orientation_step(
            static_cast<std::int64_t>(record.orientation.step()));
        authored->set_orientation_order(
            static_cast<std::int64_t>(record.orientation.order()));
        authored->set_translation_x_raw(record.translation.x.raw());
        authored->set_translation_y_raw(record.translation.y.raw());
        authored_blueprint.push_back(authored);
    }

    godot::Ref<LevelResource> level;
    level.instantiate();
    level->set_format_version(LEVEL_RESOURCE_FORMAT_VERSION);
    level->set_geometry_domain(
        p_domain == content::GeometryDomain::hex12 ? ENCODED_GEOMETRY_DOMAIN_HEX12
                                                   : ENCODED_GEOMETRY_DOMAIN_LATTICE);
    level->set_palette(palette);
    level->set_blueprint(authored_blueprint);

    return Encoded::success(level);
}

} // namespace tiles::game
