#include "engine/Level.h"

#include <utility>

namespace tiles::engine {

Level::Level(Palette p_palette, Region p_region) :
    palette_(std::move(p_palette)),
    region_(std::move(p_region)) {}

} // namespace tiles::engine
