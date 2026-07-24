#include "engine/State.h"

#include <utility>

namespace tiles::engine {

State::State(Palette p_palette, Arrangement p_arrangement) :
    palette_(std::move(p_palette)),
    arrangement_(std::move(p_arrangement)) {}

} // namespace tiles::engine
