#include "engine/Session.h"

#include <utility>

namespace tiles::engine {

bool Session::undo() {
    if (undo_history_.empty()) {
        return false;
    }

    // The whole prior value moves back into place, so nothing has to be replayed
    // or inverted, and the abandoned state is simply destroyed: there is nowhere
    // for a redo to read it from.
    state_ = std::move(undo_history_.back());
    undo_history_.pop_back();
    return true;
}

} // namespace tiles::engine
