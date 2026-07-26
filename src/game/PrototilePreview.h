#pragma once

#include "core/geometry/Polygon.h"

#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/variant/color.hpp>

#include <optional>

namespace tiles::game {

// One small presentation-only view of a single exact polygon.
//
// It exists because mirrored pentominoes cannot be chosen reliably from their
// display names alone, so every catalog row needs to show its actual shape. The
// polygon arrives through a C++-only setter and is owned by value; there is no
// Godot-visible geometry property, no id, no catalog lookup, no compilation, no
// input handling, and no mutation of anything it is shown.
//
// The fit is uniform and cartesian-y-upward, exactly like the region canvas
// projection, but it is a purely local presentation transform: nothing it
// produces is compared against, or allowed to become, a model coordinate.
class PrototilePreview : public godot::Control {
    GDCLASS(PrototilePreview, godot::Control)

protected:
    static void _bind_methods();

public:
    void _draw() override;

    void set_polygon(const Polygon &p_polygon);
    void clear_polygon();
    void set_fill_color(godot::Color p_color);

    // The exact polygon this preview was given, or nullptr when it has none.
    // Read-only, and used by the headless runner to prove that two rows really
    // did receive geometrically different shapes.
    const Polygon *polygon() const;
    godot::Color fill_color() const;

private:
    std::optional<Polygon> polygon_;
    godot::Color fill_color_;
};

} // namespace tiles::game
