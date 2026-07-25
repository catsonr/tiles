#pragma once

#include "content/CellBoundary.h"
#include "content/GeometryDomain.h"
#include "core/Hex12.h"
#include "core/Prototile.h"
#include "core/Result.h"
#include "core/geometry/Polygon.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace tiles::content {

// The construction stage at which building a catalog failed.
enum class PrototileCatalogStage {
    definition,
    polygon,
    prototile,
    hex12,
    catalog,
};

// A table-level invariant which one catalog failed to prove.
enum class PrototileCatalogErrorCode {
    empty,
    duplicate_prototile_id,
    empty_display_name,
    missing_geometry_source,
    reference_geometry_mismatch,
};

// A typed failure from catalog construction. It preserves the stage, the stable
// intended prototile id when the failure belongs to one entry, and exactly one
// complete underlying error matching that stage. A catalog-table invariant
// failure populates only catalog_error.
struct PrototileCatalogError final {
    PrototileCatalogStage stage;
    std::optional<PrototileId> prototile_id;
    std::optional<CellBoundaryError> definition_error;
    std::optional<PolygonError> polygon_error;
    std::optional<PrototileError> prototile_error;
    // Populated exactly for PrototileCatalogStage::hex12: the complete error the
    // ordinary hex-12 compiler returned, never flattened or re-coded.
    std::optional<Hex12CompilationError> hex12_error;
    std::optional<PrototileCatalogErrorCode> catalog_error;
};

// One shipped-content source definition.
//
// A definition describes one lattice source, one hex-12 source, or both. At
// most one lattice description is populated: `ring` gives an explicit exact
// whole-game-unit boundary, and `cells` gives an occupied unit-cell set traced
// into such a boundary. `hex12_polygon` names the regular polygon the hex-12
// module compiles for this identity.
//
// An omitted hex12_polygon means the identity has no hex-12 source, so every
// definition authored before this member existed remains exactly as valid as it
// was. A definition describing neither source describes no geometry at all.
struct CanonicalDefinition final {
    PrototileId::Value id;
    std::string display_name;
    std::vector<std::pair<std::int64_t, std::int64_t>> ring;
    std::vector<Cell> cells;
    std::optional<Hex12RegularPolygon> hex12_polygon;
};

class PrototileCatalog;

// The application's single canonical catalog: 7 one-sided tetrominoes at ids
// 1..7, 18 one-sided pentominoes at ids 8..25, one domino at id 26, the squares
// of side 1 and 3..9 at ids 27..34, and the unit hex-12 triangle, hexagon, and
// dodecagon at ids 35..37. Id 1 is both the o tetromino and the side-2 square;
// no second congruent identity exists. Id 27, the unit square, is one identity
// belonging to both geometry domains rather than two congruent entries.
//
// Construction is fallible only because every exact core construction path is
// checked. Nothing here is procedural generation: the definitions are a fixed
// shipped table.
Result<PrototileCatalog, PrototileCatalogError> make_canonical_prototile_catalog();

// The fixed shipped definition table. It is the same table production compiles,
// exposed read-only so tests can prove catalog order, ids, names, and source
// geometry are exactly what this layer ships.
const std::vector<CanonicalDefinition> &canonical_definitions();

// Test-only seam. The shipped table is fixed, so its own invariant failures are
// unreachable in ordinary execution; this exposes the same validating
// construction path to a synthesized table so those rejections are observed
// rather than assumed. It validates exactly what production validates and adds
// no mutation and no second construction path.
namespace testing {

Result<PrototileCatalog, PrototileCatalogError> make_catalog(
    const std::vector<CanonicalDefinition> &p_definitions);

} // namespace testing

// One shipped playable prototile: its exact geometry together with the minimal
// metadata needed to present it and to select its source compiler.
//
// A canonical prototile owns its Prototile by value and is observed only
// through const references, so a catalog entry can never be edited or rebound
// to other geometry. It carries no color, material, texture, or other
// presentation policy: those belong to one authored level, not to shipped
// content.
//
// Its domain metadata is closed and immutable: a flag recording whether the
// identity has a lattice source, and the regular polygon its hex-12 source
// compiles, if any. There is no mutable recipe, no compiler callback, and no
// way to relabel one canonical entry with another source.
class CanonicalPrototile final {
public:
    const Prototile &prototile() const {
        return prototile_;
    }

    const std::string &display_name() const {
        return display_name_;
    }

    // Whether this identity is playable in the named domain. An invalid domain
    // is supported by nothing.
    bool supports(GeometryDomain p_domain) const;

    // The hex-12 source this identity compiles, or nothing when it has none.
    // The value is a closed enumerator, not geometry: the module still owns
    // every vertex it constructs.
    std::optional<Hex12RegularPolygon> hex12_polygon() const {
        return hex12_polygon_;
    }

private:
    CanonicalPrototile(
        Prototile p_prototile,
        std::string p_display_name,
        bool p_has_lattice_source,
        std::optional<Hex12RegularPolygon> p_hex12_polygon);

    Prototile prototile_;
    std::string display_name_;
    bool has_lattice_source_;
    std::optional<Hex12RegularPolygon> hex12_polygon_;

    friend class PrototileCatalog;
};

// The application's ordered set of playable prototiles.
//
// The catalog owns every entry by value. Stored order is master storage order:
// one identity appears exactly once regardless of how many domains admit it.
// Each domain's presentation order is that same table filtered, so the shipped
// table is arranged to produce both orders without a second table of playable
// ids and without sorting by id at a call site.
//
// Lookup never reorders, inserts, constructs, logs, falls back, aliases one id
// to another, or invents a sentinel entry. There is no public mutation and no
// public constructor: only the validating build path below can publish one.
class PrototileCatalog final {
public:
    // Every canonical identity, in master storage order.
    const std::vector<CanonicalPrototile> &entries() const {
        return entries_;
    }

    // The identities the named domain admits, in that domain's presentation
    // order. The pointers address catalog-owned storage of this catalog, so a
    // copy or a move of a catalog yields views into itself and nothing can
    // dangle. An invalid domain admits nothing.
    std::vector<const CanonicalPrototile *> entries_for(GeometryDomain p_domain) const;

    // The entry with exactly this stable id, or nullptr when the catalog has
    // none. The returned pointer addresses catalog-owned storage and stays
    // valid for the catalog's lifetime. Lookup spans every identity: a caller
    // wanting a domain-restricted answer combines it with supports().
    const CanonicalPrototile *find(PrototileId p_id) const;

private:
    PrototileCatalog(
        std::vector<CanonicalPrototile> p_entries,
        std::vector<std::size_t> p_lattice_view,
        std::vector<std::size_t> p_hex12_view);

    // The single validating construction path. It proves that the table is
    // nonempty, that every id is unique, that every display name is nonempty,
    // that every definition describes at least one source, and that every
    // polygon, prototile, and hex-12 reference is built by the ordinary core
    // factories, before any catalog exists.
    static Result<PrototileCatalog, PrototileCatalogError> build(
        const std::vector<CanonicalDefinition> &p_definitions);

    std::vector<CanonicalPrototile> entries_;

    // Indices into entries_, in each domain's presentation order. Indices
    // rather than pointers, so copying or moving a catalog cannot leave a view
    // addressing another catalog's storage.
    std::vector<std::size_t> lattice_view_;
    std::vector<std::size_t> hex12_view_;

    friend Result<PrototileCatalog, PrototileCatalogError>
    make_canonical_prototile_catalog();
    friend Result<PrototileCatalog, PrototileCatalogError> testing::make_catalog(
        const std::vector<CanonicalDefinition> &p_definitions);
};

} // namespace tiles::content
