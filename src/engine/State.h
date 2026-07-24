#pragma once

#include "core/Arrangement.h"
#include "engine/Palette.h"

namespace tiles::engine {

// The single state-owning engine aggregate: one palette together with one
// authoritative arrangement. It owns both values, accepts any already-valid
// arrangement (including an empty one), and never reconstructs, normalizes, or
// copies polygons into a second representation.
//
// Its act-3 surface is read-only, exposing no mutable reference or pointer. The
// members are deliberately non-const so a later act can introduce one typed
// mutation API without first removing arbitrary mutation paths; State is not
// permanently immutable and carries no editor-specific name or behavior.
class State final {
public:
    State(Palette p_palette, Arrangement p_arrangement);

    const Palette &palette() const {
        return palette_;
    }

    const Arrangement &arrangement() const {
        return arrangement_;
    }

private:
    Palette palette_;
    Arrangement arrangement_;
};

} // namespace tiles::engine
