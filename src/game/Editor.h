#pragma once

#include "content/PrototileCatalog.h"
#include "core/OrientedPrototile.h"
#include "core/Placement.h"
#include "engine/Commands.h"
#include "engine/State.h"

#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include <cstddef>
#include <optional>
#include <variant>
#include <vector>

namespace tiles::game {

// The application's only scene: one fullscreen Control which owns the canonical
// prototile catalog and a temporary engine::State built from it, populates that
// state exclusively through typed engine commands, renders the resulting
// authoritative arrangement, and drives the first player-facing construction
// loop over it.
//
// The dependency direction is one-way. This class sends commands down into the
// engine and reads its const arrangement view back out; no Godot value, pixel
// coordinate, or rendering scalar ever flows the other way. The model-to-screen
// projection is lossy, presentation-only, and deliberately has no inverse. The
// mouse never constructs an authoritative Point: it only chooses among exact
// placements the engine has already proven, each derived from selected polygon
// features.
//
// Bootstrap is fallible at every stage, so the state is optional and every
// result is inspected before its value is touched. A bootstrap failure leaves
// the editor stateless, input-inactive, and non-crashing rather than
// half-populated.
class Editor : public godot::Control {
    GDCLASS(Editor, godot::Control)

protected:
    static void _bind_methods();

public:
    void _ready() override;
    void _draw() override;
    void _gui_input(const godot::Ref<godot::InputEvent> &p_event) override;

private:
    // One valid palette entry together with one of that entry's distinct
    // compiled orientations. It has no empty or invalid meaning, so it is only
    // ever held inside an optional; there is no sentinel index.
    struct Selection final {
        engine::PaletteEntryIndex entry;
        engine::PaletteOrientationIndex orientation;
    };

    // The typed mating command that proves one proposal. Both alternatives are
    // ordinary engine commands, so applying a proposal is a plain visit and the
    // matching State::apply overload.
    using JoinCommand = std::variant<
        engine::MateFullEdgesCommand,
        engine::MateVerticesCommand>;

    // One currently successful join option: the command that proves it, and the
    // exact placement the engine's const preview derived from it.
    struct JoinProposal final {
        JoinCommand command;
        Placement placement;
    };

    // Aggregate counts for one proposal rebuild. Thousands of feature pairs are
    // attempted, so only these totals are ever reported: an ordinary rejected
    // pair is not a diagnostic event.
    struct RebuildCounts final {
        std::size_t full_edge_attempted = 0;
        std::size_t full_edge_succeeded = 0;
        std::size_t vertex_attempted = 0;
        std::size_t vertex_succeeded = 0;
        std::size_t distinct = 0;
        std::size_t point_contact_only = 0;
        std::size_t offered = 0;
    };

    // The catalog display name for one prototile identity, or a conspicuous
    // placeholder when the catalog has no such entry. Diagnostics only: no
    // lookup here constructs, substitutes, or caches geometry.
    const char *label_for(PrototileId p_id) const;

    // Place every distinct palette orientation into its own exact four-game-unit
    // debug cell. Returns false after reporting the first unexpected failure.
    bool place_orientation_grid();

    // Place one o below the grid, then verify that a second o shifted exactly one
    // game unit is rejected as an interior overlap without disturbing the
    // arrangement. Returns false if anything but that exact outcome occurs.
    bool place_overlap_fixture();

    // The palette-owned oriented candidate the current selection names, or
    // nullptr when state or selection is absent. The returned pointer addresses
    // the state-owned palette, which no command mutates or reorders, and is used
    // transiently: it is never stored across a command.
    const OrientedPrototile *selected_candidate() const;

    // Cycle the selection through the palette's authored entry order, or through
    // the selected entry's distinct orientations, wrapping in both directions.
    // Changing entries resets the orientation to zero.
    void cycle_entry(bool p_forward);
    void cycle_orientation(bool p_forward);

    // Report the new selection, rebuild the proposal cache against it, choose an
    // active proposal from the last mouse position, and request one redraw.
    void refresh_for_selection();

    // Submit every anchor-feature x candidate-feature mating command to the
    // engine's const preview, keep each success, collapse proofs of the same
    // physical placement, and replace the cache with the result.
    RebuildCounts rebuild_proposals();

    void report_selection() const;
    void report_rebuild(const char *p_reason, const RebuildCounts &p_counts) const;

    // Choose the cached proposal whose projected handle is nearest the last
    // known mouse position. Returns true when the active proposal changed.
    bool update_active_proposal();

    // Apply the active proposal's stored typed command, then rebuild against the
    // resulting state whether it succeeded or failed.
    void apply_active_proposal();

    // Presentation-only draw helpers. Neither mutates model state; they are
    // non-const only because CanvasItem's draw verbs are.
    void draw_selection_preview();
    void draw_active_ghost();

    // The one successfully constructed canonical catalog. It is the sole source
    // of playable prototile geometry and display names for this scene; the
    // engine state selects from it and never receives a second copy.
    std::optional<content::PrototileCatalog> catalog_;

    std::optional<engine::State> state_;
    std::optional<Selection> selection_;

    // Derived input/presentation state: the finite set of currently successful
    // join options, and which one the cursor selects. Authoritative geometry
    // stays in engine::State and in each proposal's exact Placement; nothing
    // here is a second coordinate representation.
    std::vector<JoinProposal> proposals_;
    std::optional<std::size_t> active_proposal_;

    // The last local mouse position, in control pixels. It ranks projected
    // handles and nothing else.
    std::optional<godot::Vector2> mouse_position_;

    // Guards the one-shot draw-path diagnostic so redraws do not spam the log.
    bool reported_first_draw_ = false;
};

} // namespace tiles::game
