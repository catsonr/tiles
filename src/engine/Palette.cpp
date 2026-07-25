#include "engine/Palette.h"

#include "core/geometry/Polygon.h"

#include <cassert>
#include <cstddef>
#include <utility>

namespace tiles::engine {

namespace {

PaletteEntryCompilationError entry_failure(
    PaletteEntryCompilationErrorCode p_code, std::size_t p_orientation) {
    PaletteEntryCompilationError error {};
    error.code = p_code;
    error.orientation = p_orientation;
    return error;
}

PaletteEntryCompilationError pair_failure(
    PaletteEntryCompilationErrorCode p_code,
    std::size_t p_orientation,
    std::size_t p_conflicting) {
    PaletteEntryCompilationError error = entry_failure(p_code, p_orientation);
    error.conflicting_orientation = p_conflicting;
    return error;
}

} // namespace

PaletteEntry::PaletteEntry(
    Prototile p_prototile,
    Supply p_supply,
    std::vector<OrientedPrototile> p_orientations) :
    prototile_(std::move(p_prototile)),
    supply_(p_supply),
    orientations_(std::move(p_orientations)) {}

Result<PaletteEntry, PaletteEntryCompilationError> PaletteEntry::make_compiled(
    Supply p_supply, std::vector<OrientedPrototile> p_orientations) {
    using Compiled = Result<PaletteEntry, PaletteEntryCompilationError>;

    if (p_orientations.empty()) {
        PaletteEntryCompilationError error {};
        error.code = PaletteEntryCompilationErrorCode::empty_orientations;
        return Compiled::failure(error);
    }

    // Index zero fixes the entry's identity and its reference geometry; every
    // later value is checked against it, in order, identity before boundary.
    const OrientedPrototile &first = p_orientations.front();
    for (std::size_t i = 1; i < p_orientations.size(); ++i) {
        if (p_orientations[i].prototile().id() != first.prototile().id()) {
            return Compiled::failure(entry_failure(
                PaletteEntryCompilationErrorCode::inconsistent_prototile_id, i));
        }
        if (!same_boundary(
                p_orientations[i].prototile().polygon(), first.prototile().polygon())) {
            return Compiled::failure(entry_failure(
                PaletteEntryCompilationErrorCode::inconsistent_reference_boundary, i));
        }
    }

    // Representative angular order, strictly increasing: the sequence is one
    // compiler's grouped product, so two groups can neither tie nor descend.
    for (std::size_t i = 0; i + 1 < p_orientations.size(); ++i) {
        if (!(p_orientations[i].orientation() < p_orientations[i + 1].orientation())) {
            return Compiled::failure(pair_failure(
                PaletteEntryCompilationErrorCode::representatives_not_strictly_ordered,
                i,
                i + 1));
        }
    }

    // No requested angle may name two groups: an equivalence label belongs to
    // exactly one distinct boundary.
    for (std::size_t i = 0; i < p_orientations.size(); ++i) {
        for (std::size_t j = i + 1; j < p_orientations.size(); ++j) {
            for (const Orientation label : p_orientations[i].equivalent_orientations()) {
                bool shared = false;
                for (const Orientation other :
                    p_orientations[j].equivalent_orientations()) {
                    if (label == other) {
                        shared = true;
                        break;
                    }
                }
                if (shared) {
                    return Compiled::failure(pair_failure(
                        PaletteEntryCompilationErrorCode::duplicate_orientation_label,
                        i,
                        j));
                }
            }
        }
    }

    // And no two groups may present the same canonical boundary: they would be
    // one variant offered twice.
    for (std::size_t i = 0; i < p_orientations.size(); ++i) {
        for (std::size_t j = i + 1; j < p_orientations.size(); ++j) {
            if (same_boundary(
                    p_orientations[i].canonical_polygon(),
                    p_orientations[j].canonical_polygon())) {
                return Compiled::failure(pair_failure(
                    PaletteEntryCompilationErrorCode::duplicate_canonical_boundary, i, j));
            }
        }
    }

    Prototile prototile = p_orientations.front().prototile();
    return Compiled::success(
        PaletteEntry(std::move(prototile), p_supply, std::move(p_orientations)));
}

Result<PaletteEntry, LatticeOrientationError> PaletteEntry::make(
    const Prototile &p_prototile,
    Supply p_supply,
    std::vector<Orientation> p_requested_orientations) {
    using Made = Result<PaletteEntry, LatticeOrientationError>;

    auto compiled = compile_lattice_orientations(
        p_prototile, std::move(p_requested_orientations));
    if (!compiled) {
        return Made::failure(compiled.error());
    }

    auto entry = make_compiled(p_supply, std::move(compiled).value());
    // Structurally unreachable: one successful call to the lattice compiler
    // already establishes every condition make_compiled proves. It returns a
    // nonempty sequence of one prototile's groups, in strictly increasing
    // representative order, with each requested angle recorded in exactly one
    // group and each group carrying a distinct canonical boundary. The result is
    // still inspected rather than assumed, and the invariant is asserted rather
    // than widening this established lattice error type.
    assert(entry.has_value());
    if (!entry) {
        LatticeOrientationError error {};
        error.code = LatticeOrientationErrorCode::empty_orientation_set;
        return Made::failure(error);
    }
    return Made::success(std::move(entry).value());
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
