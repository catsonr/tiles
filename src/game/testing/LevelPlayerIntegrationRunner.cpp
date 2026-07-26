#include "game/testing/LevelPlayerIntegrationRunner.h"

#include "core/geometry/Coordinate.h"
#include "game/LevelPlayer.h"
#include "game/resources/LevelPersistence.h"
#include "game/resources/LevelResources.h"

#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cstddef>
#include <optional>
#include <utility>

namespace tiles::game {

namespace {

const char *CANVAS_SCENE = "res://level_player.tscn";
const char *ACT_LEVEL = "res://levels/act0-level.tres";
const char *CANONICAL_LEVEL = "res://tests/fixtures/canonical_level.tres";

// An authored level which covers no part of the world origin. Absolute
// blueprint position carries no meaning while authoring, so this is ordinary
// content, not a malformed artifact.
const char *OFFSET_LEVEL = "res://levels/dude.tres";

// A deterministic canvas rectangle, so the projection a proposal is ranked
// through does not depend on the headless window.
constexpr float CANVAS_WIDTH = 1000.0f;
constexpr float CANVAS_HEIGHT = 640.0f;

// The remaining supply one palette row presents, read back through the same
// derived status the row itself renders from. Empty would mean an unlimited
// entry.
std::optional<engine::Supply::Amount> remaining(const LevelPlayer &p_player, std::size_t p_entry) {
    const auto status =
        p_player.session()->state().supply_status(engine::PaletteEntryIndex(p_entry));
    return status.has_value() ? status->remaining : std::nullopt;
}

} // namespace

void LevelPlayerIntegrationRunner::_bind_methods() {}

bool LevelPlayerIntegrationRunner::expect(bool p_condition, const char *p_description) {
    ++checks_;
    if (!p_condition) {
        ++failures_;
        godot::UtilityFunctions::push_error("[tiles] level player integration: ", p_description);
    }
    return p_condition;
}

LevelPlayer *LevelPlayerIntegrationRunner::make_player(const godot::String &p_path) {
    if (!catalog_.has_value()) {
        auto catalog = content::make_canonical_prototile_catalog();
        if (!expect(catalog.has_value(), "the canonical catalog is constructed once")) {
            return nullptr;
        }
        catalog_ = std::move(catalog).value();
    }
    auto loaded = load_level_resource(p_path, catalog_.value());
    if (!expect(loaded.has_value(), "the authored level loads through the public consumer")) {
        return nullptr;
    }
    LoadedLevelResource level = std::move(loaded).value();
    const godot::Ref<PaletteResource> palette_resource = level.resource->get_palette();
    const godot::TypedArray<PaletteEntryResource> entries = palette_resource->get_entries();
    std::vector<godot::Color> colors;
    colors.reserve(static_cast<std::size_t>(entries.size()));
    for (std::int64_t i = 0; i < entries.size(); ++i) {
        const godot::Ref<PaletteEntryResource> entry = entries[i];
        colors.push_back(entry->get_color());
    }
    problems_.push_back(std::make_unique<ProblemState>(
        engine::Session(engine::State(std::move(level.compiled.level))), std::move(colors)));

    const godot::Ref<godot::PackedScene> scene =
        godot::ResourceLoader::get_singleton()->load(CANVAS_SCENE);
    if (!expect(scene.is_valid(), "the real problem canvas scene loads")) {
        return nullptr;
    }
    godot::Node *node = scene->instantiate();
    auto *player = godot::Object::cast_to<LevelPlayer>(node);
    if (!expect(player != nullptr, "the canvas scene root is a LevelPlayer")) {
        if (node != nullptr) {
            memdelete(node);
        }
        return nullptr;
    }
    add_child(player);
    player->set_size(godot::Vector2(CANVAS_WIDTH, CANVAS_HEIGHT));
    // The same binding production performs, with no observer: the shell is what
    // supplies one, and a canvas plays identically without it.
    player->bind(*problems_.back(), static_cast<std::int64_t>(problems_.size()), nullptr);
    return player;
}

bool LevelPlayerIntegrationRunner::accept_translation(LevelPlayer &p_player, Point p_translation) {
    for (const LevelPlayer::Proposal &proposal : p_player.proposals()) {
        if (proposal.placement.translation() != p_translation) {
            continue;
        }
        double x = 0.0;
        double y = 0.0;
        const auto &vertices = proposal.placement.footprint().vertices();
        for (const Point &vertex : vertices) {
            const godot::Vector2 projected = p_player.project(vertex);
            x += projected.x;
            y += projected.y;
        }
        p_player.set_pointer(godot::Vector2(x / vertices.size(), y / vertices.size()));
        return p_player.active_proposal().has_value()
            && p_player.accept_active_proposal();
    }
    return false;
}

void LevelPlayerIntegrationRunner::_ready() {
    LevelPlayer *player = make_player(ACT_LEVEL);
    if (player != nullptr) {
        ProblemState *bound = problems_.back().get();
        expect(player->bound(), "the act level binds into a canvas");
        expect(player->state() == bound, "the canvas borrows the persistent problem state");
        if (player->bound()) {
            const auto &state = player->session()->state();
            expect(state.arrangement().entries().empty(), "a bound problem starts with an empty arrangement");
            expect(state.palette().entries().size() == 2, "authored palette order survives loading");
            expect(state.region().inner_boundaries().empty(), "the compiled region exposes zero inner boundaries");
            expect(player->selection().has_value() && player->selection()->entry == 0
                       && player->selection()->orientation == 0,
                "the first entry and orientation are selected");
            expect(player->problem_number() == 1, "the canvas presents the number it was bound with");
            expect(remaining(*player, 0) == 6 && remaining(*player, 1) == 6,
                "both palette entries start at the authored finite supply");
            expect(!player->proposals().empty(),
                "the empty state offers at least one proven opening placement");
            bool distinct_openings = true;
            for (std::size_t i = 0; i < player->proposals().size(); ++i) {
                for (std::size_t j = i + 1; j < player->proposals().size(); ++j) {
                    if (player->proposals()[i].placement.translation()
                        == player->proposals()[j].placement.translation()) {
                        distinct_openings = false;
                    }
                }
            }
            expect(distinct_openings, "opening placements are pairwise distinct");
            expect(player->accept_active_proposal(), "the ghosted command places through Session");
            expect(player->session()->state().arrangement().entries().size() == 1,
                "placement updates the exact arrangement");
            expect(remaining(*player, 0) == 5 && remaining(*player, 1) == 6,
                "placing one tile spends exactly that entry's remaining supply");
            expect(player->selection().has_value() && bound->selection().has_value()
                       && player->selection().value() == bound->selection().value(),
                "the canvas reads and writes the persistent selection");
            player->select_entry(1);
            expect(bound->selection()->entry == 1,
                "selection is written straight through to the persistent problem");
            expect(player->undo(), "undo restores the real player state");
            expect(player->session()->state().arrangement().entries().empty(),
                "undo restores the empty arrangement and its supply");
            expect(remaining(*player, 0) == 6 && remaining(*player, 1) == 6,
                "undo restores both remaining supplies");

            // Destroying the presentation leaves the problem exactly as it was.
            player->select_entry(0);
            const bool arrangement_empty = bound->session().state().arrangement().entries().empty();
            const std::size_t depth = bound->session().undo_depth();
            player->queue_free();
            expect(bound->session().state().arrangement().entries().empty() == arrangement_empty
                       && bound->session().undo_depth() == depth
                       && bound->selection().has_value(),
                "destroying the canvas leaves its persistent problem untouched");
        } else {
            player->queue_free();
        }
    }

    // An opening move exists for every well-formed level, because a convex
    // corner of the region can only be covered by a tile carrying a vertex
    // exactly there. Nothing about that argument mentions the world origin.
    LevelPlayer *offset = make_player(OFFSET_LEVEL);
    if (offset != nullptr && offset->bound()) {
        expect(!offset->proposals().empty(),
            "a level authored away from the origin still offers an opening move");
        expect(offset->accept_active_proposal(),
            "that opening move places through Session");
        expect(offset->session()->state().arrangement().entries().size() == 1,
            "the opening move reaches the exact arrangement");
        offset->queue_free();
    }

    LevelPlayer *fixture = make_player(CANONICAL_LEVEL);
    if (fixture != nullptr && fixture->bound()) {
        const Point origin { Coordinate::from_raw(0), Coordinate::from_raw(0) };
        const Point domino { Coordinate::from_raw(2 * Coordinate::SCALE), Coordinate::from_raw(0) };
        expect(accept_translation(*fixture, origin), "the canonical fixture accepts its first ordinary proposal");
        fixture->select_entry(1);
        fixture->cycle_orientation(true);
        expect(accept_translation(*fixture, domino), "the canonical fixture solves through ordinary proposals");
        expect(fixture->completion_visible(), "completion appears exactly after the final placement");
        if (!fixture->session()->state().arrangement().entries().empty()) {
            const Placement &placed = fixture->session()->state().arrangement().entries().back().placement;
            double x = 0.0;
            double y = 0.0;
            for (const Point &vertex : placed.footprint().vertices()) {
                const godot::Vector2 projected = fixture->project(vertex);
                x += projected.x;
                y += projected.y;
            }
            fixture->remove_at_local(godot::Vector2(
                x / placed.footprint().vertices().size(),
                y / placed.footprint().vertices().size()));
        }
        expect(!fixture->completion_visible(), "removal hides live completion");
        fixture->queue_free();
    }

    if (failures_ == 0) {
        godot::UtilityFunctions::print(
            "[tiles] level player integration: ",
            static_cast<std::int64_t>(checks_),
            " checks passed");
    } else {
        godot::UtilityFunctions::push_error(
            "[tiles] level player integration: ",
            static_cast<std::int64_t>(failures_),
            " of ",
            static_cast<std::int64_t>(checks_),
            " checks failed");
    }
    get_tree()->quit(failures_ == 0 ? 0 : 1);
}

} // namespace tiles::game
