// The authored-level checker.
//
// It answers, for one decoded level artifact, whether a player can actually
// play it: whether it compiles, whether its authored solution is reachable
// under the rules the player plays by, and whether any legal opening move can
// strand part of it. It also reports the two numbers that describe how the
// level feels rather than whether it works.
//
// IMPORTANT: this reimplements LevelPlayer::rebuild_proposals. The player is
// Godot-coupled and cannot be linked here, so the proposal rule is stated twice
// and the two statements must be kept in agreement. Changing the rule in
// LevelPlayer means changing `offered` below, and vice versa. Every other piece
// of reasoning — palette compilation, blueprint compilation, region derivation,
// legality, supply, containment — is the real shipped code, linked directly.
//
// Input is the flat decoding produced by decode_level.py, on stdin.

#include "content/CanonicalOrientationCompiler.h"
#include "content/GeometryDomain.h"
#include "content/PrototileCatalog.h"
#include "core/Arrangement.h"
#include "core/ArrangementRegion.h"
#include "core/Orientation.h"
#include "core/Placement.h"
#include "core/Prototile.h"
#include "core/geometry/Coordinate.h"
#include "core/geometry/Predicates.h"
#include "engine/Blueprint.h"
#include "engine/Commands.h"
#include "engine/Level.h"
#include "engine/Palette.h"
#include "engine/State.h"
#include "engine/Supply.h"

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <numeric>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

using namespace tiles;

namespace {

// Every random walk uses this fixed seed, so two runs of the checker over
// unchanged content produce byte-identical output.
constexpr unsigned RANDOM_SEED = 20260726u;
constexpr std::size_t ORDER_TRIALS = 40;

struct Record final {
    long long id;
    long long step;
    long long order;
    long long x_raw;
    long long y_raw;
};

void fail(const std::string &p_level, const char *p_stage) {
    std::printf("%-20s FAILED at %s\n", p_level.c_str(), p_stage);
}

// An unlimited supply is never reported as a number: a configured capacity of
// "as many as you like" and a configured capacity of 7 are different authored
// statements, and printing the witness count in place of the former would read
// as though the level said something it does not.
std::string supply_text(engine::Supply p_supply) {
    const std::optional<engine::Supply::Amount> amount = p_supply.finite_amount();
    return amount.has_value() ? std::to_string(amount.value()) : "unlimited";
}

// LevelPlayer's contact rule: a shared length of boundary, not a shared point.
bool shares_edge_contact(const Polygon &p_candidate, const Arrangement &p_arrangement) {
    const Polygon::Vertices &candidate = p_candidate.vertices();
    for (const Entry &anchor : p_arrangement.entries()) {
        const Polygon::Vertices &placed = anchor.placement.footprint().vertices();
        for (std::size_t i = 0; i < candidate.size(); ++i) {
            for (std::size_t j = 0; j < placed.size(); ++j) {
                if (classify_segments(
                        candidate[i], candidate[(i + 1) % candidate.size()], placed[j],
                        placed[(j + 1) % placed.size()])
                    == SegmentRelation::collinear_overlap) {
                    return true;
                }
            }
        }
    }
    return false;
}

} // namespace

int main(int argc, char **argv) {
    const std::string level = argc > 1 ? argv[1] : "?";

    long long encoded_domain = 0;
    std::cin >> encoded_domain;
    const content::GeometryDomain domain = encoded_domain == 1
        ? content::GeometryDomain::hex12
        : content::GeometryDomain::lattice;

    std::size_t entry_count = 0;
    std::cin >> entry_count;
    std::vector<long long> ids(entry_count);
    std::vector<long long> supplies(entry_count);
    for (std::size_t i = 0; i < entry_count; ++i) {
        std::cin >> ids[i] >> supplies[i];
    }

    std::size_t record_count = 0;
    std::cin >> record_count;
    std::vector<Record> records(record_count);
    for (std::size_t i = 0; i < record_count; ++i) {
        std::cin >> records[i].id >> records[i].step >> records[i].order
            >> records[i].x_raw >> records[i].y_raw;
    }

    // --- stage 1: the level compiles into exact runtime values -------------

    auto catalog_result = content::make_canonical_prototile_catalog();
    if (!catalog_result) {
        fail(level, "canonical catalog");
        return 1;
    }
    const content::PrototileCatalog catalog = std::move(catalog_result).value();

    std::vector<engine::PaletteEntry> palette_entries;
    for (std::size_t i = 0; i < entry_count; ++i) {
        const content::CanonicalPrototile *canonical =
            catalog.find(PrototileId(static_cast<PrototileId::Value>(ids[i])));
        if (canonical == nullptr) {
            fail(level, "palette: unknown prototile id");
            return 1;
        }
        auto oriented = content::compile_canonical_orientations(domain, *canonical);
        if (!oriented) {
            fail(level, "palette: orientation compilation");
            return 1;
        }
        engine::Supply supply = engine::Supply::unlimited();
        if (supplies[i] != -1) {
            auto finite =
                engine::Supply::finite(static_cast<engine::Supply::Amount>(supplies[i]));
            if (!finite) {
                fail(level, "palette: invalid supply");
                return 1;
            }
            supply = std::move(finite).value();
        }
        auto entry = engine::PaletteEntry::make_compiled(supply, std::move(oriented).value());
        if (!entry) {
            fail(level, "palette: entry composition");
            return 1;
        }
        palette_entries.push_back(std::move(entry).value());
    }

    auto palette_result = engine::Palette::make(std::move(palette_entries));
    if (!palette_result) {
        fail(level, "palette construction");
        return 1;
    }
    const engine::Palette palette = std::move(palette_result).value();

    std::vector<engine::BlueprintPlacement> blueprint;
    for (const Record &record : records) {
        auto orientation = Orientation::make(
            static_cast<Orientation::Component>(record.step),
            static_cast<Orientation::Component>(record.order));
        if (!orientation) {
            fail(level, "blueprint: invalid orientation");
            return 1;
        }
        blueprint.push_back(engine::BlueprintPlacement {
            PrototileId(static_cast<PrototileId::Value>(record.id)),
            orientation.value(),
            Point {
                Coordinate::from_raw(record.x_raw),
                Coordinate::from_raw(record.y_raw) } });
    }

    auto witness_result = engine::compile_blueprint(palette, blueprint);
    if (!witness_result) {
        std::printf("%-20s FAILED at blueprint compilation, record %zu\n",
            level.c_str(), witness_result.error().placement);
        return 1;
    }
    const Arrangement witness = std::move(witness_result).value();

    // --- stage 1b: the palette says exactly what the solution uses ----------
    //
    // A palette is a claim about the level's content. Every entry is a tile the
    // solution places, and a finite supply is exactly how many of them it
    // places. An entry the witness never uses is a leftover the player has to
    // rule out by hand; a finite supply above the witness count is slack nobody
    // authored on purpose. Both are checked here rather than trusted, so the
    // convention survives every later edit of a level.
    //
    // An unlimited supply fails for the same reason: authored content states how
    // many pieces a solution takes, and "as many as you like" is not that
    // statement.

    bool normalized = true;
    for (const engine::PaletteEntry &entry : palette.entries()) {
        const PrototileId id = entry.prototile().id();
        std::size_t used = 0;
        for (const Entry &placed : witness.entries()) {
            if (placed.placement.prototile().id() == id) {
                ++used;
            }
        }
        const std::string configured = supply_text(entry.supply());
        if (used == 0) {
            fail(level,
                ("palette: prototile " + std::to_string(id.value())
                    + " is never placed by the witness; supply=" + configured + " witness=0")
                    .c_str());
            normalized = false;
            continue;
        }
        if (entry.supply().is_unlimited() || entry.supply().finite_amount().value() != used) {
            fail(level,
                ("palette: prototile " + std::to_string(id.value()) + " supply=" + configured
                    + " witness=" + std::to_string(used))
                    .c_str());
            normalized = false;
        }
    }
    if (!normalized) {
        return 1;
    }

    auto region_result = region_from_arrangement(witness);
    if (!region_result) {
        const char *why = "region derivation";
        switch (region_result.error().code) {
            case ArrangementRegionErrorCode::empty_arrangement:
                why = "region: the blueprint is empty";
                break;
            case ArrangementRegionErrorCode::nonmanifold_boundary_vertex:
                why = "region: tiles meet at a point (pinch or bowtie)";
                break;
            case ArrangementRegionErrorCode::disconnected_coverage:
                why = "region: coverage is more than one connected piece";
                break;
            case ArrangementRegionErrorCode::internal_invariant_failure:
                why = "region: internal invariant";
                break;
        }
        fail(level, why);
        return 1;
    }
    const Region region = std::move(region_result).value();

    // The anchors that never change: every region boundary vertex.
    std::vector<Point> region_vertices;
    for (const Point &point : region.outer_boundary().vertices()) {
        region_vertices.push_back(point);
    }
    for (const Polygon &hole : region.inner_boundaries()) {
        for (const Point &point : hole.vertices()) {
            region_vertices.push_back(point);
        }
    }

    // --- the player's proposal rule, restated ------------------------------

    const auto locate = [&](const engine::BlueprintPlacement &p_target,
                            const engine::Palette &p_palette) {
        for (std::size_t e = 0; e < p_palette.entries().size(); ++e) {
            if (!(p_palette.entries()[e].prototile().id() == p_target.prototile_id)) {
                continue;
            }
            const std::vector<OrientedPrototile> &orientations =
                p_palette.entries()[e].orientations();
            for (std::size_t o = 0; o < orientations.size(); ++o) {
                if (orientations[o].orientation() == p_target.orientation) {
                    return std::make_pair(e, o);
                }
            }
        }
        return std::make_pair(SIZE_MAX, SIZE_MAX);
    };

    // Every legal position the player is offered for one palette selection.
    const auto proposals_for = [&](const engine::State &p_state, std::size_t e, std::size_t o) {
        std::vector<Placement> offered;
        const auto supply = p_state.supply_status(engine::PaletteEntryIndex(e));
        if (supply.has_value() && supply->remaining.has_value()
            && supply->remaining.value() == 0) {
            return offered;
        }
        std::vector<Point> anchors = region_vertices;
        for (const Entry &placed : p_state.arrangement().entries()) {
            for (const Point &vertex : placed.placement.footprint().vertices()) {
                anchors.push_back(vertex);
            }
        }
        const bool empty = p_state.arrangement().entries().empty();
        const Polygon::Vertices &local =
            p_state.palette().entries()[e].orientations()[o].canonical_polygon().vertices();
        for (const Point &anchor : anchors) {
            for (const Point &vertex : local) {
                auto x = checked_subtract(anchor.x, vertex.x);
                auto y = checked_subtract(anchor.y, vertex.y);
                if (!x || !y) {
                    continue;
                }
                auto preview = p_state.preview(engine::PlaceCommand {
                    engine::PaletteEntryIndex(e), engine::PaletteOrientationIndex(o),
                    Point { x.value(), y.value() } });
                if (!preview) {
                    continue;
                }
                if (!empty
                    && !shares_edge_contact(preview.value().footprint(), p_state.arrangement())) {
                    continue;
                }
                bool duplicate = false;
                for (const Placement &kept : offered) {
                    if (kept.translation() == preview.value().translation()) {
                        duplicate = true;
                        break;
                    }
                }
                if (!duplicate) {
                    offered.push_back(std::move(preview).value());
                }
            }
        }
        return offered;
    };

    // Is this one authored placement currently on offer?
    //
    // Asked directly rather than by building the whole proposal set and
    // searching it: the reachability walks ask this O(tiles^3) times per level,
    // and enumerating every position to answer one question about a known
    // position is what made this tool too slow to run on every save.
    const auto is_offered = [&](const engine::State &p_state,
                                const engine::BlueprintPlacement &p_target) {
        const auto [e, o] = locate(p_target, p_state.palette());
        if (e == SIZE_MAX) {
            return false;
        }
        const auto supply = p_state.supply_status(engine::PaletteEntryIndex(e));
        if (supply.has_value() && supply->remaining.has_value()
            && supply->remaining.value() == 0) {
            return false;
        }

        // Nameable: some candidate vertex, once translated, lands exactly on an
        // anchor vertex.
        const Polygon::Vertices &local =
            p_state.palette().entries()[e].orientations()[o].canonical_polygon().vertices();
        bool anchored = false;
        for (const Point &vertex : local) {
            auto x = checked_add(p_target.translation.x, vertex.x);
            auto y = checked_add(p_target.translation.y, vertex.y);
            if (!x || !y) {
                continue;
            }
            const Point required { x.value(), y.value() };
            for (const Point &anchor : region_vertices) {
                if (anchor == required) {
                    anchored = true;
                    break;
                }
            }
            for (const Entry &placed : p_state.arrangement().entries()) {
                if (anchored) {
                    break;
                }
                for (const Point &placed_vertex : placed.placement.footprint().vertices()) {
                    if (placed_vertex == required) {
                        anchored = true;
                        break;
                    }
                }
            }
            if (anchored) {
                break;
            }
        }
        if (!anchored) {
            return false;
        }

        auto preview = p_state.preview(engine::PlaceCommand {
            engine::PaletteEntryIndex(e), engine::PaletteOrientationIndex(o),
            p_target.translation });
        if (!preview) {
            return false;
        }
        return p_state.arrangement().entries().empty()
            || shares_edge_contact(preview.value().footprint(), p_state.arrangement());
    };

    const auto place = [&](engine::State &p_state, const engine::BlueprintPlacement &p_target) {
        const auto [e, o] = locate(p_target, p_state.palette());
        return e != SIZE_MAX
            && p_state
                   .apply(engine::PlaceCommand {
                       engine::PaletteEntryIndex(e), engine::PaletteOrientationIndex(o),
                       p_target.translation })
                   .has_value();
    };

    // --- stage 2: seed safety ----------------------------------------------
    //
    // Open with each authored placement in turn, then grow greedily. Growth is
    // monotone — placing a tile only ever adds anchors and contacts — so the
    // greedy fixed point is the complete set reachable from that opening. A seed
    // which cannot reach every authored placement is a level the player can lose
    // by choosing badly on move one.

    std::size_t stranding_seeds = 0;
    std::size_t worst_reached = blueprint.size();
    for (std::size_t seed = 0; seed < blueprint.size(); ++seed) {
        engine::State state(engine::Level(palette, region));
        if (!place(state, blueprint[seed])) {
            continue;
        }
        std::vector<bool> placed(blueprint.size(), false);
        placed[seed] = true;
        std::size_t reached = 1;
        bool progress = true;
        while (progress) {
            progress = false;
            for (std::size_t i = 0; i < blueprint.size(); ++i) {
                if (placed[i] || !is_offered(state, blueprint[i])) {
                    continue;
                }
                if (place(state, blueprint[i])) {
                    placed[i] = true;
                    ++reached;
                    progress = true;
                }
            }
        }
        worst_reached = std::min(worst_reached, reached);
        if (reached < blueprint.size()) {
            ++stranding_seeds;
        }
    }

    // --- stage 3: feel ------------------------------------------------------
    //
    // Walk random build orders. A deferral is a moment where the tile the player
    // wanted next was not on offer yet. Deferrals are the level asking the player
    // to think about order; they are not defects, and stage 2 proves none of them
    // is permanent.

    std::mt19937 rng(RANDOM_SEED);
    std::size_t deferrals = 0;
    std::size_t worst_options = 0;
    std::size_t worst_overlap = 0;
    bool solvable = false;

    for (std::size_t trial = 0; trial < ORDER_TRIALS; ++trial) {
        std::vector<std::size_t> order(blueprint.size());
        std::iota(order.begin(), order.end(), 0);
        std::shuffle(order.begin(), order.end(), rng);

        engine::State state(engine::Level(palette, region));
        std::vector<bool> placed(blueprint.size(), false);
        bool progress = true;
        while (progress) {
            progress = false;
            for (const std::size_t index : order) {
                if (placed[index]) {
                    continue;
                }
                if (!is_offered(state, blueprint[index])) {
                    ++deferrals;
                    continue;
                }
                if (place(state, blueprint[index])) {
                    placed[index] = true;
                    progress = true;
                }
            }
        }
        if (state.solved()) {
            solvable = true;
        }

        // Density, sampled at a few checkpoints while building the authored
        // solution: how many candidates claim the same point the player aims at.
        // Enumerating every position is expensive, so this samples rather than
        // measuring every step — the extremes live at the sparse early states and
        // are caught either way.
        if (trial == 0) {
            const std::size_t stride = std::max<std::size_t>(1, blueprint.size() / 4);
            engine::State fresh(engine::Level(palette, region));
            for (std::size_t step = 0; step < blueprint.size(); ++step) {
                if (step % stride != 0) {
                    if (!place(fresh, blueprint[step])) {
                        break;
                    }
                    continue;
                }
                for (std::size_t e = 0; e < fresh.palette().entries().size(); ++e) {
                    const std::size_t count = fresh.palette().entries()[e].orientations().size();
                    for (std::size_t o = 0; o < count; ++o) {
                        const std::vector<Placement> offered = proposals_for(fresh, e, o);
                        worst_options = std::max(worst_options, offered.size());
                        for (const Placement &probe : offered) {
                            double cx = 0.0;
                            double cy = 0.0;
                            for (const Point &v : probe.footprint().vertices()) {
                                cx += static_cast<double>(v.x.raw());
                                cy += static_cast<double>(v.y.raw());
                            }
                            cx /= probe.footprint().vertices().size();
                            cy /= probe.footprint().vertices().size();
                            std::size_t claimants = 0;
                            for (const Placement &other : offered) {
                                const Polygon::Vertices &vs = other.footprint().vertices();
                                bool inside = false;
                                for (std::size_t i = 0, j = vs.size() - 1; i < vs.size();
                                     j = i++) {
                                    const double ax = static_cast<double>(vs[i].x.raw());
                                    const double ay = static_cast<double>(vs[i].y.raw());
                                    const double bx = static_cast<double>(vs[j].x.raw());
                                    const double by = static_cast<double>(vs[j].y.raw());
                                    if ((ay > cy) != (by > cy)
                                        && cx < ax + (cy - ay) * (bx - ax) / (by - ay)) {
                                        inside = !inside;
                                    }
                                }
                                if (inside) {
                                    ++claimants;
                                }
                            }
                            worst_overlap = std::max(worst_overlap, claimants);
                        }
                    }
                }
                if (step >= blueprint.size() || !place(fresh, blueprint[step])) {
                    break;
                }
            }
        }
    }

    const bool ok = solvable && stranding_seeds == 0;
    std::printf("%-20s %-4s tiles=%-3zu solvable=%-3s seed-safe=%-7s options=%-4zu overlap=%-3zu deferrals=%.1f\n",
        level.c_str(),
        ok ? "ok" : "BAD",
        blueprint.size(),
        solvable ? "yes" : "NO",
        stranding_seeds == 0
            ? "yes"
            : (std::to_string(blueprint.size() - stranding_seeds) + "/"
                  + std::to_string(blueprint.size()))
                  .c_str(),
        worst_options,
        worst_overlap,
        static_cast<double>(deferrals) / ORDER_TRIALS);

    if (!solvable) {
        std::printf("    the authored solution is not reachable under the player's rules\n");
    }
    if (stranding_seeds != 0) {
        std::printf("    %zu opening moves strand the level; worst reaches %zu/%zu tiles\n",
            stranding_seeds, worst_reached, blueprint.size());
    }
    return ok ? 0 : 1;
}
