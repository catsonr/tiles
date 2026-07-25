#pragma once

#include "core/Orientation.h"
#include "core/OrientedPrototile.h"
#include "core/Prototile.h"
#include "core/Result.h"
#include "engine/Supply.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace tiles::engine {

// The composition invariants a precompiled oriented sequence must satisfy to
// become one palette entry.
enum class PaletteEntryCompilationErrorCode {
    empty_orientations,
    inconsistent_prototile_id,
    inconsistent_reference_boundary,
    representatives_not_strictly_ordered,
    duplicate_orientation_label,
    duplicate_canonical_boundary,
};

// `orientation` names the offending index and is populated by every failure
// except empty_orientations. A failure about a pair of oriented values also
// populates `conflicting_orientation`; no other combination occurs.
struct PaletteEntryCompilationError final {
    PaletteEntryCompilationErrorCode code;
    std::optional<std::size_t> orientation;
    std::optional<std::size_t> conflicting_orientation;
};

// One prototile slot in a palette: a prototile, its configured supply, and the
// nonempty sequence of distinct admitted orientations compiled from it.
//
// Orientation compilation is delegated entirely to a checked source compiler;
// this entry never re-sorts, re-rejects, re-rotates, canonicalizes, or
// de-duplicates. The prototile and its compiled oriented values are
// value-owned, consistent with the core's non-dangling ownership model.
class PaletteEntry final {
public:
    // Compile the requested orientations of p_prototile and pair them with the
    // configured supply. On compilation failure the exact core
    // LatticeOrientationError is returned unchanged and no entry is produced.
    static Result<PaletteEntry, LatticeOrientationError> make(
        const Prototile &p_prototile,
        Supply p_supply,
        std::vector<Orientation> p_requested_orientations);

    // Publish one entry from a sequence a source compiler already produced.
    //
    // This is composition checking, not geometry: every supplied value already
    // carries its compiler's proof, and this factory neither recompiles one nor
    // rediscovers which compiler produced it. It proves only that the sequence
    // is one coherent entry — nonempty, one identity, one reference boundary,
    // strictly increasing representatives, and no orientation label or
    // canonical boundary shared by two groups.
    //
    // Preventing a mixed-domain sequence is the content compiler's job: it
    // calls one source compiler once per entry, so a mixture cannot arise here.
    static Result<PaletteEntry, PaletteEntryCompilationError> make_compiled(
        Supply p_supply, std::vector<OrientedPrototile> p_orientations);

    const Prototile &prototile() const {
        return prototile_;
    }

    Supply supply() const {
        return supply_;
    }

    const std::vector<OrientedPrototile> &orientations() const {
        return orientations_;
    }

private:
    PaletteEntry(
        Prototile p_prototile,
        Supply p_supply,
        std::vector<OrientedPrototile> p_orientations);

    Prototile prototile_;
    Supply supply_;
    std::vector<OrientedPrototile> orientations_;
};

enum class PaletteError {
    empty,
    duplicate_prototile_id,
};

// A finite, nonempty, authored-order collection of palette entries. The stored
// order is presentation order, not geometric meaning. Order is the number of
// entries, never the total supply or the number of oriented variants.
//
// Duplicate-id rejection is a bookkeeping guard only. It does not search for
// geometric congruence across entries: authored content is responsible for not
// listing two rotationally congruent prototiles as distinct entries.
class Palette final {
public:
    static Result<Palette, PaletteError> make(
        std::vector<PaletteEntry> p_entries);

    const std::vector<PaletteEntry> &entries() const {
        return entries_;
    }

    std::size_t order() const {
        return entries_.size();
    }

private:
    explicit Palette(std::vector<PaletteEntry> p_entries);

    std::vector<PaletteEntry> entries_;
};

} // namespace tiles::engine
