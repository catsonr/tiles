#pragma once

#include "core/Region.h"
#include "engine/Palette.h"

namespace tiles::engine {

// What a level is for this milestone: one palette together with one region.
//
// Both members already carry their own proofs — a palette is nonempty with
// distinct prototile ids, a region has a validated hole configuration and
// positive exact area — so pairing them cannot fail and there is no Level error.
// The level owns both by value and exposes read-only views.
//
// The rotation rule is deliberately not a third member. Each palette entry's
// compiled distinct orientations are the complete runtime rule, so nothing here
// stores, re-derives, or reinterprets which quarter turns were requested.
class Level final {
public:
    Level(Palette p_palette, Region p_region);

    const Palette &palette() const {
        return palette_;
    }

    const Region &region() const {
        return region_;
    }

private:
    Palette palette_;
    Region region_;
};

} // namespace tiles::engine
