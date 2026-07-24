#include "engine/Palette.h"

#include <cstddef>
#include <utility>

namespace tiles::engine {

PaletteEntry::PaletteEntry(
    Prototile p_prototile,
    Supply p_supply,
    std::vector<OrientedPrototile> p_orientations) :
    prototile_(std::move(p_prototile)),
    supply_(p_supply),
    orientations_(std::move(p_orientations)) {}

Result<PaletteEntry, LatticeOrientationError> PaletteEntry::make(
    const Prototile &p_prototile,
    Supply p_supply,
    std::vector<Orientation> p_requested_orientations) {
    auto compiled = compile_lattice_orientations(
        p_prototile, std::move(p_requested_orientations));
    if (!compiled) {
        return Result<PaletteEntry, LatticeOrientationError>::failure(
            compiled.error());
    }
    return Result<PaletteEntry, LatticeOrientationError>::success(
        PaletteEntry(p_prototile, p_supply, std::move(compiled).value()));
}

Palette::Palette(std::vector<PaletteEntry> p_entries) :
    entries_(std::move(p_entries)) {}

Result<Palette, PaletteError> Palette::make(std::vector<PaletteEntry> p_entries) {
    if (p_entries.empty()) {
        return Result<Palette, PaletteError>::failure(PaletteError::empty);
    }

    // Reject any repeated exact PrototileId. The palette is small and handcrafted,
    // so a direct pairwise scan is clearer than an auxiliary set; it is a
    // bookkeeping guard, deliberately not a congruence kernel.
    for (std::size_t i = 0; i < p_entries.size(); ++i) {
        for (std::size_t j = i + 1; j < p_entries.size(); ++j) {
            if (p_entries[i].prototile().id() == p_entries[j].prototile().id()) {
                return Result<Palette, PaletteError>::failure(
                    PaletteError::duplicate_prototile_id);
            }
        }
    }

    return Result<Palette, PaletteError>::success(Palette(std::move(p_entries)));
}

} // namespace tiles::engine
