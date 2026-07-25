#include "game/testing/LevelPlayerIntegrationRunner.h"

#include "core/geometry/Coordinate.h"
#include "game/LevelPlayer.h"

#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace tiles::game {

namespace {

const char *PLAYER_SCENE = "res://level_player.tscn";
const char *ACT_LEVEL = "res://levels/act0-level.tres";
const char *CANONICAL_LEVEL = "res://tests/fixtures/canonical_level.tres";

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
    const godot::Ref<godot::PackedScene> scene =
        godot::ResourceLoader::get_singleton()->load(PLAYER_SCENE);
    if (!expect(scene.is_valid(), "the real player scene loads")) {
        return nullptr;
    }
    godot::Node *node = scene->instantiate();
    auto *player = godot::Object::cast_to<LevelPlayer>(node);
    if (!expect(player != nullptr, "the player scene root is a LevelPlayer")) {
        if (node != nullptr) {
            memdelete(node);
        }
        return nullptr;
    }
    player->set_level_path(p_path);
    add_child(player);
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
        expect(player->get_level_path() == ACT_LEVEL, "the scene names the baked act level");
        expect(player->initialized(), "the act level loads into a player");
        if (player->initialized()) {
            const auto &state = player->session()->state();
            expect(state.arrangement().entries().empty(), "a player starts with an empty arrangement");
            expect(state.palette().entries().size() == 2, "authored palette order survives loading");
            expect(state.region().inner_boundaries().empty(), "the compiled region exposes zero inner boundaries");
            expect(player->selection().has_value() && player->selection()->entry == 0
                       && player->selection()->orientation == 0,
                "the first entry and orientation are selected");
            expect(player->proposals().size() == 1
                       && player->proposals().front().placement.translation()
                           == Point { Coordinate::from_raw(0), Coordinate::from_raw(0) },
                "the empty state offers exactly the proven origin placement");
            expect(player->accept_active_proposal(), "the ghosted command places through Session");
            expect(player->session()->state().arrangement().entries().size() == 1,
                "placement updates the exact arrangement");
            expect(player->undo(), "undo restores the real player state");
            expect(player->session()->state().arrangement().entries().empty(),
                "undo restores the empty arrangement and its supply");
        }
        player->queue_free();
    }

    LevelPlayer *fixture = make_player(CANONICAL_LEVEL);
    if (fixture != nullptr && fixture->initialized()) {
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
