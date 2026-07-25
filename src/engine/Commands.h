#pragma once

#include "core/Arrangement.h"
#include "core/Placement.h"
#include "core/geometry/Alignment.h"
#include "core/geometry/Point.h"

#include <cstddef>
#include <variant>

namespace tiles::engine {

// A strong index into Palette::entries(). It is stable for the lifetime of a
// State, which never mutates or reorders its palette. Deliberately not
// interchangeable with a bare size_t or with a palette orientation index.
class PaletteEntryIndex final {
public:
    using Value = std::size_t;

    explicit constexpr PaletteEntryIndex(Value p_value) :
        value_(p_value) {}

    constexpr Value value() const {
        return value_;
    }

    friend constexpr bool operator==(PaletteEntryIndex p_lhs, PaletteEntryIndex p_rhs) {
        return p_lhs.value_ == p_rhs.value_;
    }

    friend constexpr bool operator!=(PaletteEntryIndex p_lhs, PaletteEntryIndex p_rhs) {
        return !(p_lhs == p_rhs);
    }

private:
    Value value_;
};

// A strong index into one PaletteEntry::orientations(): it selects a distinct
// compiled oriented prototile. It never indexes or cycles through an oriented
// value's equivalent_orientations() labels, which record which requested angles
// collapsed onto one boundary rather than offering separate geometry.
class PaletteOrientationIndex final {
public:
    using Value = std::size_t;

    explicit constexpr PaletteOrientationIndex(Value p_value) :
        value_(p_value) {}

    constexpr Value value() const {
        return value_;
    }

    friend constexpr bool operator==(
        PaletteOrientationIndex p_lhs, PaletteOrientationIndex p_rhs) {
        return p_lhs.value_ == p_rhs.value_;
    }

    friend constexpr bool operator!=(
        PaletteOrientationIndex p_lhs, PaletteOrientationIndex p_rhs) {
        return !(p_lhs == p_rhs);
    }

private:
    Value value_;
};

// Place one palette-authored oriented candidate at an exact q16.48 translation.
//
// The translation is authoritative and is passed to the core unchanged: neither
// coordinate need be a multiple of Coordinate::SCALE, and nothing here rounds,
// snaps, clamps, or infers a cell size. This is also the complete exact-contact
// operation — it admits legal partial-edge contact that shares no polygon
// vertex, which neither mating command can derive from selected features.
struct PlaceCommand final {
    PaletteEntryIndex palette_entry;
    PaletteOrientationIndex orientation;
    Point translation;
};

// Derive one candidate translation by mating a complete anchor footprint edge
// with a complete oriented candidate edge, then insert the result.
//
// "mate" rather than "join": the command derives a translation from selected
// boundary features and creates no adjacency relation inside the arrangement.
struct MateFullEdgesCommand final {
    PlacementId anchor;
    EdgeIndex anchor_edge;
    PaletteEntryIndex candidate_entry;
    PaletteOrientationIndex candidate_orientation;
    EdgeIndex candidate_edge;
};

// Derive one candidate translation by mating a selected anchor footprint vertex
// with a selected oriented candidate vertex, then insert the result. The
// resulting footprints may additionally share edges, partial edges, other
// vertices, or several disconnected contacts; the only independent condition
// remains absence of positive-area interior overlap.
struct MateVerticesCommand final {
    PlacementId anchor;
    VertexIndex anchor_vertex;
    PaletteEntryIndex candidate_entry;
    PaletteOrientationIndex candidate_orientation;
    VertexIndex candidate_vertex;
};

// Failures owned by the engine rather than the exact core: naming a candidate
// that the palette does not offer, or one whose configured supply is already
// used up. Candidate resolution has one precedence for every command — entry,
// then orientation, then supply — and inspects nothing beyond the first failure.
enum class CandidateError {
    palette_entry_out_of_range,
    orientation_out_of_range,
    supply_exhausted,
};

// The level's region rejecting an otherwise well-formed candidate footprint.
// It is a distinct alternative rather than another CandidateError because the
// candidate itself is legitimate: the palette offers it, its supply remains, and
// its footprint was constructed exactly. Only its position leaves the region.
enum class RegionPlacementError {
    outside_region,
};

// Command failures keep engine and core failures distinct alternatives; core
// errors are preserved exactly, including ArrangementError::conflicting_placement
// and JoinError::{code, conflicting_placement}. Nothing is flattened to a string,
// logged instead of returned, or replaced with a presentation-facing value.
using PlaceCommandError = std::variant<
    CandidateError,
    PlacementError,
    RegionPlacementError,
    ArrangementError>;

using MateCommandError = std::variant<CandidateError, JoinError, RegionPlacementError>;

} // namespace tiles::engine
