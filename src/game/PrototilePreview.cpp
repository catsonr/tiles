#include "game/PrototilePreview.h"

#include "core/geometry/Coordinate.h"
#include "core/geometry/Point.h"
#include "core/geometry/Triangle.h"

#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include <algorithm>

namespace tiles::game {

namespace {

const godot::Color PREVIEW_OUTLINE(0.72f, 0.75f, 0.80f, 1.0f);

constexpr float PREVIEW_OUTLINE_WIDTH = 1.0f;
constexpr double PREVIEW_MARGIN = 3.0;

double to_real(Coordinate p_coordinate) {
    return static_cast<double>(p_coordinate.raw()) / static_cast<double>(Coordinate::SCALE);
}

} // namespace

void PrototilePreview::_bind_methods() {}

void PrototilePreview::set_polygon(const Polygon &p_polygon) {
    polygon_ = p_polygon;
    queue_redraw();
}

void PrototilePreview::clear_polygon() {
    polygon_.reset();
    queue_redraw();
}

void PrototilePreview::set_fill_color(godot::Color p_color) {
    fill_color_ = p_color;
    queue_redraw();
}

const Polygon *PrototilePreview::polygon() const {
    return polygon_.has_value() ? &polygon_.value() : nullptr;
}

godot::Color PrototilePreview::fill_color() const {
    return fill_color_;
}

void PrototilePreview::_draw() {
    if (!polygon_.has_value()) {
        return;
    }

    // Measured across every vertex: a canonical polygon begins at its
    // lexicographically smallest vertex, which is not promised to be a corner of
    // its bounding box.
    const Polygon::Vertices &vertices = polygon_->vertices();
    if (vertices.empty()) {
        return;
    }
    double min_x = to_real(vertices.front().x);
    double max_x = min_x;
    double min_y = to_real(vertices.front().y);
    double max_y = min_y;
    for (const Point &vertex : vertices) {
        const double x = to_real(vertex.x);
        const double y = to_real(vertex.y);
        min_x = std::min(min_x, x);
        max_x = std::max(max_x, x);
        min_y = std::min(min_y, y);
        max_y = std::max(max_y, y);
    }

    const double width = max_x - min_x;
    const double height = max_y - min_y;
    if (width <= 0.0 || height <= 0.0) {
        return;
    }

    const godot::Vector2 size = get_size();
    const double inner_width = static_cast<double>(size.x) - 2.0 * PREVIEW_MARGIN;
    const double inner_height = static_cast<double>(size.y) - 2.0 * PREVIEW_MARGIN;
    if (inner_width <= 0.0 || inner_height <= 0.0) {
        return;
    }

    const double scale = std::min(inner_width / width, inner_height / height);
    const double center_x = 0.5 * (min_x + max_x);
    const double center_y = 0.5 * (min_y + max_y);
    const double screen_center_x = 0.5 * static_cast<double>(size.x);
    const double screen_center_y = 0.5 * static_cast<double>(size.y);

    const auto project = [&](const Point &p_point) {
        const double x = screen_center_x + scale * (to_real(p_point.x) - center_x);
        const double y = screen_center_y - scale * (to_real(p_point.y) - center_y);
        return godot::Vector2(static_cast<real_t>(x), static_cast<real_t>(y));
    };

    // The exact certified triangulation is drawn directly, so a concave piece
    // fills correctly without asking Godot to re-triangulate a boundary the core
    // has already decomposed.
    for (const Triangle &triangle : polygon_->triangulation()) {
        godot::PackedVector2Array points;
        for (const Point &vertex : triangle.vertices) {
            points.push_back(project(vertex));
        }
        draw_colored_polygon(points, fill_color_);
    }

    godot::PackedVector2Array boundary;
    for (const Point &vertex : vertices) {
        boundary.push_back(project(vertex));
    }
    // draw_polyline leaves a polygon open, so the closing vertex is repeated
    // here and nowhere else.
    boundary.push_back(boundary[0]);
    draw_polyline(boundary, PREVIEW_OUTLINE, PREVIEW_OUTLINE_WIDTH);
}

} // namespace tiles::game
