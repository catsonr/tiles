#pragma once

#include "engine/Session.h"

#include <godot_cpp/variant/color.hpp>

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace tiles::game {

// Which palette entry is selected, and which of that entry's distinct
// orientations is current.
//
// This is persistent play state rather than presentation state: a problem the
// player left with a rotated pentomino selected is a problem they should find
// exactly that way when they come back to it.
struct PaletteSelection final {
    std::size_t entry;
    std::size_t orientation;

    friend bool operator==(PaletteSelection p_lhs, PaletteSelection p_rhs) {
        return p_lhs.entry == p_rhs.entry && p_lhs.orientation == p_rhs.orientation;
    }

    friend bool operator!=(PaletteSelection p_lhs, PaletteSelection p_rhs) {
        return !(p_lhs == p_rhs);
    }
};

// One problem's complete persistent play state.
//
// The Session owns the current exact State and the whole undo history, so
// arrangement, placement identities, next_id(), derived supplies, and solved()
// all live in exactly one place and are never mirrored here. Authored colors are
// presentation, but they are authored presentation: they belong to the problem
// rather than to whichever view happens to be showing it.
//
// A visible presentation borrows one of these; it never owns one, never copies
// one, and never outlives the Exam which does own it. Destroying every node that
// was showing a problem leaves its state untouched.
class ProblemState final {
public:
    ProblemState(engine::Session p_session, std::vector<godot::Color> p_colors) :
        session_(std::move(p_session)),
        colors_(std::move(p_colors)),
        selection_(PaletteSelection { 0, 0 }) {}

    engine::Session &session() {
        return session_;
    }

    const engine::Session &session() const {
        return session_;
    }

    // Authored palette colors in authored palette order, parallel to the exact
    // palette entries.
    const std::vector<godot::Color> &colors() const {
        return colors_;
    }

    const std::optional<PaletteSelection> &selection() const {
        return selection_;
    }

    void set_selection(PaletteSelection p_selection) {
        selection_ = p_selection;
    }

private:
    engine::Session session_;
    std::vector<godot::Color> colors_;
    std::optional<PaletteSelection> selection_;
};

// The fixed shell, as seen by a presentation bound to one of its problems.
//
// A moving canvas owns no palette row and no status line, but its operations are
// exactly what makes those stale. This is the one call back into the shell, and
// it carries nothing: the shell re-derives everything it shows from the problem
// state it is bound to.
//
// It is deliberately a plain C++ seam rather than a signal. The semantic event
// hooks observe results and no part of ordinary play may depend on them.
class ProblemStateObserver {
public:
    virtual void on_problem_presentation_changed() = 0;

protected:
    ~ProblemStateObserver() = default;
};

} // namespace tiles::game
