#pragma once

#include "content/CellBoundary.h"
#include "core/Prototile.h"
#include "core/Result.h"
#include "core/geometry/Polygon.h"

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
    catalog,
};

// A table-level invariant which one catalog failed to prove.
enum class PrototileCatalogErrorCode {
    empty,
    duplicate_prototile_id,
    empty_display_name,
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
    std::optional<PrototileCatalogErrorCode> catalog_error;
};

// One shipped-content source definition. Exactly one geometry description is
// populated: `ring` gives an explicit exact whole-game-unit boundary, and
// `cells` gives an occupied unit-cell set traced into such a boundary.
struct CanonicalDefinition final {
    PrototileId::Value id;
    std::string display_name;
    std::vector<std::pair<std::int64_t, std::int64_t>> ring;
    std::vector<Cell> cells;
};

class PrototileCatalog;

// The application's single canonical catalog: 7 one-sided tetrominoes at ids
// 1..7, 18 one-sided pentominoes at ids 8..25, one domino at id 26, and the
// squares of side 1 and 3..9 at ids 27..34. Id 1 is both the o tetromino and
// the side-2 square; no second congruent identity exists.
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
// metadata needed to present it.
//
// A canonical prototile owns its Prototile by value and is observed only
// through const references, so a catalog entry can never be edited or rebound
// to other geometry. It carries no color, material, texture, or other
// presentation policy: those belong to one authored level, not to shipped
// content.
class CanonicalPrototile final {
public:
    const Prototile &prototile() const {
        return prototile_;
    }

    const std::string &display_name() const {
        return display_name_;
    }

private:
    CanonicalPrototile(Prototile p_prototile, std::string p_display_name);

    Prototile prototile_;
    std::string display_name_;

    friend class PrototileCatalog;
};

// The application's ordered set of playable prototiles.
//
// The catalog owns every entry by value. Stored order is presentation order and
// is stable; lookup never reorders, inserts, constructs, logs, falls back,
// aliases one id to another, or invents a sentinel entry. There is no public
// mutation and no public constructor: only the validating build path below can
// publish one.
class PrototileCatalog final {
public:
    const std::vector<CanonicalPrototile> &entries() const {
        return entries_;
    }

    // The entry with exactly this stable id, or nullptr when the catalog has
    // none. The returned pointer addresses catalog-owned storage and stays
    // valid for the catalog's lifetime.
    const CanonicalPrototile *find(PrototileId p_id) const;

private:
    explicit PrototileCatalog(std::vector<CanonicalPrototile> p_entries);

    // The single validating construction path. It proves that the table is
    // nonempty, that every id is unique, that every display name is nonempty,
    // and that every polygon and prototile is built by the ordinary core
    // factories, before any catalog exists.
    static Result<PrototileCatalog, PrototileCatalogError> build(
        const std::vector<CanonicalDefinition> &p_definitions);

    std::vector<CanonicalPrototile> entries_;

    friend Result<PrototileCatalog, PrototileCatalogError>
    make_canonical_prototile_catalog();
    friend Result<PrototileCatalog, PrototileCatalogError> testing::make_catalog(
        const std::vector<CanonicalDefinition> &p_definitions);
};

} // namespace tiles::content
