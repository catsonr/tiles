#pragma once

#include <godot_cpp/classes/node.hpp>

namespace tiles {

class GDExtensionSmokeTest : public godot::Node {
    GDCLASS(GDExtensionSmokeTest, godot::Node)

protected:
    static void _bind_methods();

public:
    void _ready() override;
};

} // tiles
