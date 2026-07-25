#include "engine/Blueprint.h"

#include "core/OrientedPrototile.h"
#include "engine/Supply.h"

#include <utility>

namespace tiles::engine {

namespace {

using Compilation = Result<Arrangement, BlueprintCompilationError>;

// Authored palette order is the search order. Palette construction already
// proved ids unique, so the first match is the only match.
const PaletteEntry *find_entry(const Palette &p_palette, PrototileId p_id) {
    for (const PaletteEntry &entry : p_palette.entries()) {
        if (entry.prototile().id() == p_id) {
            return &entry;
        }
    }
    return nullptr;
}

// Representative orientations only. equivalent_orientations() records which
// requested angles collapsed onto one boundary and is deliberately not searched:
// a record naming a nonrepresentative label names no distinct variant.
const OrientedPrototile *find_orientation(
    const PaletteEntry &p_entry, Orientation p_orientation) {
    for (const OrientedPrototile &oriented : p_entry.orientations()) {
        if (oriented.orientation() == p_orientation) {
            return &oriented;
        }
    }
    return nullptr;
}

// How many prior records name one identity. Compilation stops at the first
// failure, so every prior record was compiled successfully and this count is
// exactly the number of pieces already spent.
std::size_t consumed_before(
    const std::vector<BlueprintPlacement> &p_placements,
    std::size_t p_index,
    PrototileId p_id) {
    std::size_t consumed = 0;
    for (std::size_t prior = 0; prior < p_index; ++prior) {
        if (p_placements[prior].prototile_id == p_id) {
            ++consumed;
        }
    }
    return consumed;
}

} // namespace

Result<Arrangement, BlueprintCompilationError> compile_blueprint(
    const Palette &p_palette,
    const std::vector<BlueprintPlacement> &p_placements) {
    Arrangement arrangement;

    for (std::size_t index = 0; index < p_placements.size(); ++index) {
        const BlueprintPlacement &record = p_placements[index];

        const PaletteEntry *entry = find_entry(p_palette, record.prototile_id);
        if (entry == nullptr) {
            return Compilation::failure(BlueprintCompilationError {
                BlueprintCompilationErrorCode::prototile_not_in_palette,
                index,
                record.prototile_id,
                std::nullopt,
                std::nullopt,
                std::nullopt,
            });
        }

        const OrientedPrototile *oriented = find_orientation(*entry, record.orientation);
        if (oriented == nullptr) {
            return Compilation::failure(BlueprintCompilationError {
                BlueprintCompilationErrorCode::orientation_not_in_palette,
                index,
                record.prototile_id,
                record.orientation,
                std::nullopt,
                std::nullopt,
            });
        }

        const std::optional<Supply::Amount> amount = entry->supply().finite_amount();
        if (amount.has_value()) {
            const std::size_t consumed =
                consumed_before(p_placements, index, record.prototile_id);
            if (static_cast<Supply::Amount>(consumed) >= amount.value()) {
                return Compilation::failure(BlueprintCompilationError {
                    BlueprintCompilationErrorCode::supply_exhausted,
                    index,
                    record.prototile_id,
                    record.orientation,
                    std::nullopt,
                    std::nullopt,
                });
            }
        }

        auto placement = Placement::make(*oriented, record.translation);
        if (!placement) {
            return Compilation::failure(BlueprintCompilationError {
                BlueprintCompilationErrorCode::placement_construction_failed,
                index,
                record.prototile_id,
                record.orientation,
                placement.error(),
                std::nullopt,
            });
        }

        auto inserted = arrangement.try_insert(std::move(placement).value());
        if (!inserted) {
            return Compilation::failure(BlueprintCompilationError {
                BlueprintCompilationErrorCode::arrangement_insertion_failed,
                index,
                record.prototile_id,
                record.orientation,
                std::nullopt,
                inserted.error(),
            });
        }
    }

    return Compilation::success(std::move(arrangement));
}

} // namespace tiles::engine
