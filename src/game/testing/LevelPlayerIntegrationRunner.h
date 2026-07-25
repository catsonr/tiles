#pragma once

#include "core/geometry/Point.h"

#include <godot_cpp/classes/node.hpp>

namespace tiles::game {

class LevelPlayer;

class LevelPlayerIntegrationRunner : public godot::Node {
    GDCLASS(LevelPlayerIntegrationRunner, godot::Node)

protected:
    static void _bind_methods();

public:
    void _ready() override;

private:
    bool expect(bool p_condition, const char *p_description);
    bool accept_translation(LevelPlayer &p_player, Point p_translation);
    LevelPlayer *make_player(const godot::String &p_path);

    std::size_t checks_ = 0;
    std::size_t failures_ = 0;
};

} // namespace tiles::game
