#pragma once

#include "engine/State.h"

#include <godot_cpp/classes/control.hpp>

#include <optional>

namespace tiles::game {

// The application's only scene: one fullscreen Control which owns a handcrafted
// engine::State, populates it exclusively through typed engine commands, and
// renders the resulting authoritative arrangement.
//
// The dependency direction is one-way. This class sends commands down into the
// engine and reads its const arrangement view back out; no Godot value, pixel
// coordinate, or rendering scalar ever flows the other way. The model-to-screen
// projection is lossy, presentation-only, and deliberately has no inverse.
//
// Bootstrap is fallible at every stage, so the state is optional and every
// result is inspected before its value is touched. A bootstrap failure leaves
// the editor stateless and non-crashing rather than half-populated.
class Editor : public godot::Control {
    GDCLASS(Editor, godot::Control)

protected:
    static void _bind_methods();

public:
    void _ready() override;
    void _draw() override;

private:
    // Place every distinct palette orientation into its own exact four-game-unit
    // debug cell. Returns false after reporting the first unexpected failure.
    bool place_orientation_grid();

    // Place one o below the grid, then verify that a second o shifted exactly one
    // game unit is rejected as an interior overlap without disturbing the
    // arrangement. Returns false if anything but that exact outcome occurs.
    bool place_overlap_fixture();

    std::optional<engine::State> state_;

    // Guards the one-shot draw-path diagnostic so redraws do not spam the log.
    bool reported_first_draw_ = false;
};

} // namespace tiles::game
