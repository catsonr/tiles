#pragma once

#include "core/Arrangement.h"
#include "core/Result.h"
#include "engine/Commands.h"
#include "engine/State.h"

#include <cstddef>
#include <utility>
#include <vector>

namespace tiles::engine {

// The temporal play interaction around successive exact states: one current
// State together with the stack of prior States a successful mutation left
// behind.
//
// The layering is deliberate and one-directional. Arrangement removes an exact
// stored identity; State is one current legal level arrangement; Session makes
// successful State mutations undoable. No undo stack lives inside Arrangement or
// State, so a snapshot is a plain prior State and can never recursively contain
// a history of its own.
//
// History is session state. It is not arrangement geometry, not part of a level,
// and not serialized anywhere. There is no redo: undo discards the abandoned
// state outright, and a later mutation simply continues from the restored one.
//
// The state is observed only through a const reference. Nothing here hands out a
// mutable State, Arrangement, Level, Palette, Region, or view of the history.
class Session final {
public:
    explicit Session(State p_initial) :
        state_(std::move(p_initial)) {}

    const State &state() const {
        return state_;
    }

    bool can_undo() const {
        return undo_depth() != 0;
    }

    // The exact number of snapshots held, never a separately maintained counter.
    std::size_t undo_depth() const {
        return undo_history_.size();
    }

    // Replace the current state with the newest snapshot and drop that snapshot,
    // reporting whether anything was restored. An empty history is ordinary
    // interface state rather than a domain failure, so it is a non-mutating
    // false and not a fabricated error alternative.
    //
    // Restoration is whole-value: the exact Level, palette entry order and
    // supplies, outer and hole polygons, arrangement entry order, every
    // surviving and restored placement and its identity, next_id() including
    // exhaustion, and therefore every derived supply status and solved().
    //
    // Rewinding next_id() is intentional. Undoing a placement restores the
    // pre-placement allocator, so a later placement may legitimately receive
    // that same identity; no cached proposal, pointer, or hit-test result may
    // outlive a mutation or an undo.
    bool undo();

    // Each overload mutates a copy and installs it only once the command has
    // already succeeded, so a failure leaves the current state and the history
    // completely untouched — and would do so even if a future State
    // implementation performed work before reporting failure. The completed
    // State operations remain independently transactional; this copy is an
    // additional ownership guarantee, not permission to weaken them.
    //
    // Exactly one snapshot per successful placement, mating, or deletion. No
    // failed mutation, preview, or read-only query adds any. The error types are
    // the command-specific ones State already publishes: nothing is unified into
    // a common variant merely to share a return type.
    Result<PlacementId, PlaceCommandError> apply(const PlaceCommand &p_command) {
        return apply_reversible(p_command);
    }

    Result<PlacementId, MateCommandError> apply(const MateFullEdgesCommand &p_command) {
        return apply_reversible(p_command);
    }

    Result<PlacementId, MateCommandError> apply(const MateVerticesCommand &p_command) {
        return apply_reversible(p_command);
    }

    Result<PlacementId, RemoveCommandError> apply(const RemoveCommand &p_command) {
        return apply_reversible(p_command);
    }

private:
    // The one mutation algorithm, shared by every overload so no command can
    // acquire its own snapshot policy. The command is applied to the candidate
    // copy and never to the live state, and the installed candidate is exactly
    // the one that produced the returned success — the command is never applied
    // a second time after the history push.
    //
    // The return type is spelled as exactly whatever State publishes for that
    // command, so each overload above forwards its own error type unchanged.
    template <typename CommandT>
    auto apply_reversible(const CommandT &p_command)
        -> decltype(std::declval<State &>().apply(p_command));

    State state_;
    // Oldest first; the newest snapshot is the one undo restores. Unbounded for
    // this milestone: no cap, compression, checkpointing, or persistence.
    std::vector<State> undo_history_;
};

template <typename CommandT>
auto Session::apply_reversible(const CommandT &p_command)
    -> decltype(std::declval<State &>().apply(p_command)) {
    State candidate = state_;
    auto result = candidate.apply(p_command);
    if (!result.has_value()) {
        return result;
    }

    // Only now, with success already known, does the prior state become a
    // snapshot.
    undo_history_.push_back(std::move(state_));
    state_ = std::move(candidate);
    return result;
}

} // namespace tiles::engine
