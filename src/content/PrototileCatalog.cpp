#include "content/PrototileCatalog.h"

#include "content/CellBoundary.h"
#include "core/geometry/Coordinate.h"
#include "core/geometry/Point.h"

#include <cstddef>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace tiles::content {

namespace {

PrototileCatalogError definition_failure(PrototileId p_id, CellBoundaryError p_error) {
    PrototileCatalogError error {};
    error.stage = PrototileCatalogStage::definition;
    error.prototile_id = p_id;
    error.definition_error = p_error;
    return error;
}

PrototileCatalogError polygon_failure(PrototileId p_id, PolygonError p_error) {
    PrototileCatalogError error {};
    error.stage = PrototileCatalogStage::polygon;
    error.prototile_id = p_id;
    error.polygon_error = p_error;
    return error;
}

PrototileCatalogError prototile_failure(PrototileId p_id, PrototileError p_error) {
    PrototileCatalogError error {};
    error.stage = PrototileCatalogStage::prototile;
    error.prototile_id = p_id;
    error.prototile_error = p_error;
    return error;
}

PrototileCatalogError table_failure(PrototileCatalogErrorCode p_code) {
    PrototileCatalogError error {};
    error.stage = PrototileCatalogStage::catalog;
    error.catalog_error = p_code;
    return error;
}

PrototileCatalogError entry_table_failure(
    PrototileId p_id, PrototileCatalogErrorCode p_code) {
    PrototileCatalogError error = table_failure(p_code);
    error.prototile_id = p_id;
    return error;
}

// The occupied cells of an axis-aligned rectangle, used only to describe the
// domino and the squares in the same source vocabulary as every other shipped
// polyomino. It builds a definition, never a runtime value.
std::vector<std::pair<std::int64_t, std::int64_t>> rectangle_ring(
    std::int64_t p_width, std::int64_t p_height) {
    return {
        { 0, 0 },
        { p_width, 0 },
        { p_width, p_height },
        { 0, p_height },
    };
}

std::vector<Cell> cells_from(std::initializer_list<std::pair<std::int64_t, std::int64_t>> p_cells) {
    std::vector<Cell> cells;
    cells.reserve(p_cells.size());
    for (const auto &cell : p_cells) {
        cells.push_back(Cell { cell.first, cell.second });
    }
    return cells;
}

// The fixed shipped table, in catalog presentation order.
//
// Ids 1..7 keep the exact boundary rings the handcrafted tetromino bootstrap
// authored, so their identity and geometry are unchanged by the move into
// shipped content. Ids 8..25 are the eighteen one-sided pentominoes, given as
// occupied unit cells with the bottom printed row at y = 0; each mirrored piece
// is its own explicit definition rather than a reflection of a constructed
// prototile, because reflection is not a placement transform in this game.
// Ids 26..34 are the domino and the squares of side 1 and 3..9; the side-2
// square is id 1 and is never duplicated.
std::vector<CanonicalDefinition> build_canonical_definitions() {
    std::vector<CanonicalDefinition> definitions;
    definitions.reserve(34);

    const auto ring_definition =
        [&definitions](
            PrototileId::Value p_id,
            const char *p_name,
            std::vector<std::pair<std::int64_t, std::int64_t>> p_ring) {
            CanonicalDefinition definition {};
            definition.id = p_id;
            definition.display_name = p_name;
            definition.ring = std::move(p_ring);
            definitions.push_back(std::move(definition));
        };

    const auto cell_definition =
        [&definitions](
            PrototileId::Value p_id, const char *p_name, std::vector<Cell> p_cells) {
            CanonicalDefinition definition {};
            definition.id = p_id;
            definition.display_name = p_name;
            definition.cells = std::move(p_cells);
            definitions.push_back(std::move(definition));
        };

    // --- one-sided tetrominoes, ids 1..7 ---
    //
    // Each ring has only direction-changing corners. s and z, and j and l, are
    // distinct chiral prototiles.
    ring_definition(1, "tetromino o (square 2)", { { 0, 0 }, { 2, 0 }, { 2, 2 }, { 0, 2 } });
    ring_definition(2, "tetromino i", { { 0, 0 }, { 4, 0 }, { 4, 1 }, { 0, 1 } });
    ring_definition(
        3,
        "tetromino t",
        { { 0, 0 }, { 3, 0 }, { 3, 1 }, { 2, 1 }, { 2, 2 }, { 1, 2 }, { 1, 1 }, { 0, 1 } });
    ring_definition(
        4,
        "tetromino s",
        { { 0, 0 }, { 2, 0 }, { 2, 1 }, { 3, 1 }, { 3, 2 }, { 1, 2 }, { 1, 1 }, { 0, 1 } });
    ring_definition(
        5,
        "tetromino z",
        { { 1, 0 }, { 3, 0 }, { 3, 1 }, { 2, 1 }, { 2, 2 }, { 0, 2 }, { 0, 1 }, { 1, 1 } });
    ring_definition(
        6, "tetromino j", { { 0, 0 }, { 2, 0 }, { 2, 3 }, { 1, 3 }, { 1, 1 }, { 0, 1 } });
    ring_definition(
        7, "tetromino l", { { 0, 0 }, { 2, 0 }, { 2, 1 }, { 1, 1 }, { 1, 3 }, { 0, 3 } });

    // --- one-sided pentominoes, ids 8..25 ---
    //
    // .##
    // ##.
    // .#.
    cell_definition(8, "pentomino f", cells_from({ { 1, 2 }, { 2, 2 }, { 0, 1 }, { 1, 1 }, { 1, 0 } }));
    // ##.
    // .##
    // .#.
    cell_definition(9, "pentomino f mirrored", cells_from({ { 0, 2 }, { 1, 2 }, { 1, 1 }, { 2, 1 }, { 1, 0 } }));
    // #
    // #
    // #
    // #
    // #
    cell_definition(10, "pentomino i", cells_from({ { 0, 0 }, { 0, 1 }, { 0, 2 }, { 0, 3 }, { 0, 4 } }));
    // #.
    // #.
    // #.
    // ##
    cell_definition(11, "pentomino l", cells_from({ { 0, 0 }, { 1, 0 }, { 0, 1 }, { 0, 2 }, { 0, 3 } }));
    // .#
    // .#
    // .#
    // ##
    cell_definition(12, "pentomino l mirrored", cells_from({ { 0, 0 }, { 1, 0 }, { 1, 1 }, { 1, 2 }, { 1, 3 } }));
    // ##..
    // .###
    cell_definition(13, "pentomino n", cells_from({ { 0, 1 }, { 1, 1 }, { 1, 0 }, { 2, 0 }, { 3, 0 } }));
    // ..##
    // ###.
    cell_definition(14, "pentomino n mirrored", cells_from({ { 2, 1 }, { 3, 1 }, { 0, 0 }, { 1, 0 }, { 2, 0 } }));
    // ##
    // ##
    // #.
    cell_definition(15, "pentomino p", cells_from({ { 0, 2 }, { 1, 2 }, { 0, 1 }, { 1, 1 }, { 0, 0 } }));
    // ##
    // ##
    // .#
    cell_definition(16, "pentomino p mirrored", cells_from({ { 0, 2 }, { 1, 2 }, { 0, 1 }, { 1, 1 }, { 1, 0 } }));
    // ###
    // .#.
    // .#.
    cell_definition(17, "pentomino t", cells_from({ { 0, 2 }, { 1, 2 }, { 2, 2 }, { 1, 1 }, { 1, 0 } }));
    // #.#
    // ###
    cell_definition(18, "pentomino u", cells_from({ { 0, 1 }, { 2, 1 }, { 0, 0 }, { 1, 0 }, { 2, 0 } }));
    // #..
    // #..
    // ###
    cell_definition(19, "pentomino v", cells_from({ { 0, 2 }, { 0, 1 }, { 0, 0 }, { 1, 0 }, { 2, 0 } }));
    // #..
    // ##.
    // .##
    cell_definition(20, "pentomino w", cells_from({ { 0, 2 }, { 0, 1 }, { 1, 1 }, { 1, 0 }, { 2, 0 } }));
    // .#.
    // ###
    // .#.
    cell_definition(21, "pentomino x", cells_from({ { 1, 2 }, { 0, 1 }, { 1, 1 }, { 2, 1 }, { 1, 0 } }));
    // ..#.
    // ####
    cell_definition(22, "pentomino y", cells_from({ { 2, 1 }, { 0, 0 }, { 1, 0 }, { 2, 0 }, { 3, 0 } }));
    // .#..
    // ####
    cell_definition(23, "pentomino y mirrored", cells_from({ { 1, 1 }, { 0, 0 }, { 1, 0 }, { 2, 0 }, { 3, 0 } }));
    // ##.
    // .#.
    // .##
    cell_definition(24, "pentomino z", cells_from({ { 0, 2 }, { 1, 2 }, { 1, 1 }, { 1, 0 }, { 2, 0 } }));
    // .##
    // .#.
    // ##.
    cell_definition(25, "pentomino z mirrored", cells_from({ { 1, 2 }, { 2, 2 }, { 1, 1 }, { 0, 0 }, { 1, 0 } }));

    // --- domino and squares, ids 26..34 ---
    ring_definition(26, "domino", rectangle_ring(2, 1));

    struct SquareSpec final {
        PrototileId::Value id;
        std::int64_t side;
        const char *name;
    };
    constexpr SquareSpec SQUARES[] = {
        { 27, 1, "square 1" },
        { 28, 3, "square 3" },
        { 29, 4, "square 4" },
        { 30, 5, "square 5" },
        { 31, 6, "square 6" },
        { 32, 7, "square 7" },
        { 33, 8, "square 8" },
        { 34, 9, "square 9" },
    };
    for (const SquareSpec &square : SQUARES) {
        ring_definition(square.id, square.name, rectangle_ring(square.side, square.side));
    }

    return definitions;
}

} // namespace

CanonicalPrototile::CanonicalPrototile(Prototile p_prototile, std::string p_display_name) :
    prototile_(std::move(p_prototile)),
    display_name_(std::move(p_display_name)) {}

PrototileCatalog::PrototileCatalog(std::vector<CanonicalPrototile> p_entries) :
    entries_(std::move(p_entries)) {}

const CanonicalPrototile *PrototileCatalog::find(PrototileId p_id) const {
    for (const CanonicalPrototile &entry : entries_) {
        if (entry.prototile().id() == p_id) {
            return &entry;
        }
    }
    return nullptr;
}

Result<PrototileCatalog, PrototileCatalogError> PrototileCatalog::build(
    const std::vector<CanonicalDefinition> &p_definitions) {
    using Built = Result<PrototileCatalog, PrototileCatalogError>;

    std::vector<CanonicalPrototile> entries;
    entries.reserve(p_definitions.size());
    std::set<PrototileId::Value> seen_ids;

    for (const CanonicalDefinition &definition : p_definitions) {
        const PrototileId id(definition.id);

        // Table invariants belonging to this definition are proven before its
        // geometry is compiled, so a defective table never produces work whose
        // product could not be published anyway.
        if (definition.display_name.empty()) {
            return Built::failure(
                entry_table_failure(id, PrototileCatalogErrorCode::empty_display_name));
        }
        if (!seen_ids.insert(definition.id).second) {
            return Built::failure(
                entry_table_failure(id, PrototileCatalogErrorCode::duplicate_prototile_id));
        }

        // Exactly one source description. A definition offering both or neither
        // describes no geometry, and is a definition defect rather than a
        // polygon one.
        const bool has_ring = !definition.ring.empty();
        const bool has_cells = !definition.cells.empty();
        if (has_ring == has_cells) {
            CellBoundaryError source {};
            source.code = CellBoundaryErrorCode::empty;
            return Built::failure(definition_failure(id, source));
        }

        std::vector<Point> ring;
        if (has_ring) {
            ring.reserve(definition.ring.size());
            for (const auto &corner : definition.ring) {
                auto x = whole_game_units(corner.first);
                if (!x) {
                    return Built::failure(definition_failure(id, x.error()));
                }
                auto y = whole_game_units(corner.second);
                if (!y) {
                    return Built::failure(definition_failure(id, y.error()));
                }
                ring.push_back(Point { x.value(), y.value() });
            }
        } else {
            auto traced = trace_cell_boundary(definition.cells);
            if (!traced) {
                return Built::failure(definition_failure(id, traced.error()));
            }
            ring = std::move(traced).value();
        }

        auto polygon = Polygon::make(std::move(ring));
        if (!polygon) {
            return Built::failure(polygon_failure(id, polygon.error()));
        }

        auto prototile = Prototile::make(id, std::move(polygon).value());
        if (!prototile) {
            return Built::failure(prototile_failure(id, prototile.error()));
        }

        entries.push_back(
            CanonicalPrototile(std::move(prototile).value(), definition.display_name));
    }

    if (entries.empty()) {
        return Built::failure(table_failure(PrototileCatalogErrorCode::empty));
    }

    return Built::success(PrototileCatalog(std::move(entries)));
}

const std::vector<CanonicalDefinition> &canonical_definitions() {
    static const std::vector<CanonicalDefinition> definitions = build_canonical_definitions();
    return definitions;
}

Result<PrototileCatalog, PrototileCatalogError> make_canonical_prototile_catalog() {
    return PrototileCatalog::build(canonical_definitions());
}

namespace testing {

Result<PrototileCatalog, PrototileCatalogError> make_catalog(
    const std::vector<CanonicalDefinition> &p_definitions) {
    return PrototileCatalog::build(p_definitions);
}

} // namespace testing

} // namespace tiles::content
