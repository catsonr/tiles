#pragma once

#include "content/PrototileCatalog.h"
#include "core/geometry/Point.h"
#include "game/ProblemState.h"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/string.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace tiles::game {

class LevelPlayer;

// The headless proof that one problem canvas plays its bound problem: the same
// loading path the exam uses, one persistent problem state, and the real player
// operations applied to it.
//
// It owns the problem states it binds, exactly as the exam does, so a canvas
// here is bound the same way a canvas there is.
class LevelPlayerIntegrationRunner : public godot::Node {
    GDCLASS(LevelPlayerIntegrationRunner, godot::Node)

protected:
    static void _bind_methods();

public:
    void _ready() override;

private:
    bool expect(bool p_condition, const char *p_description);
    bool accept_translation(LevelPlayer &p_player, Point p_translation);
    // Load one authored level and keep its persistent state at a stable address,
    // then mount the real canvas scene on it.
    LevelPlayer *make_player(const godot::String &p_path);

    std::optional<content::PrototileCatalog> catalog_;
    std::vector<std::unique_ptr<ProblemState>> problems_;
    std::size_t checks_ = 0;
    std::size_t failures_ = 0;
};

} // namespace tiles::game
