# place tiles act 4-1: selected-tile join interaction

## implementor context seed

this act turns the visible act-4 integration proof into the first player-driven
construction interaction. it deliberately retains the act-4 debug arrangement
and adds one narrow interaction loop around the already-established full-edge
and vertex mating commands.

seed the implementation session from the repository. this document fixes the
new contracts, but it does not replace the definitions, coordinate rules, or
completed-act contracts already present there.

before editing:

1. run `git status --short` and preserve every pre-existing user change;
2. enumerate the repository with `rg --files`, excluding generated build output,
   `Godot.app/`, and `vendor/`;
3. read this document completely;
4. read `README.md`, `DEFINITIONS.md`, `COORDINATES.md`, and
   `docs/PLACE-TILES-ROADMAP.md` completely;
5. read every previous implementation plan and note file completely:
   - `docs/INITIAL-SCAFFOLD.md`;
   - `docs/INITIAL-SCAFFOLD-IMPLEMENTATION-NOTES.md`;
   - `docs/PLACE-TILES-ACT-0-IMPLEMENTATION.md`;
   - `docs/PLACE-TILES-ACT-0-IMPLEMENTATION-NOTES.md`;
   - `docs/PLACE-TILES-ACT-1-IMPLEMENTATION.md`;
   - `docs/PLACE-TILES-ACT-1-IMPLEMENTATION-NOTES.md`;
   - `docs/PLACE-TILES-ACT-2-IMPLEMENTATION.md`;
   - `docs/PLACE-TILES-ACT-2-IMPLEMENTATION-NOTES.md`;
   - `docs/PLACE-TILES-ACT-3-IMPLEMENTATION.md`;
   - `docs/PLACE-TILES-ACT-3-IMPLEMENTATION-NOTES.md`;
   - `docs/PLACE-TILES-ACT-3-1-IMPLEMENTATION.md`;
   - `docs/PLACE-TILES-ACT-3-1-IMPLEMENTATION-NOTES.md`;
   - `docs/PLACE-TILES-ACT-4-IMPLEMENTATION.md`; and
   - `docs/PLACE-TILES-ACT-4-IMPLEMENTATION-NOTES.md`;
6. read every authored file beneath `src/core/`, `src/engine/`, `src/game/`, and
   `tests/`, paying particular attention to:
   - `src/core/Arrangement.{h,cpp}`;
   - `src/core/Placement.{h,cpp}`;
   - `src/core/OrientedPrototile.{h,cpp}`;
   - `src/core/Result.h`;
   - `src/core/geometry/Alignment.{h,cpp}`;
   - `src/core/geometry/Intersection.{h,cpp}`;
   - `src/engine/Commands.h`;
   - `src/engine/Palette.{h,cpp}`;
   - `src/engine/State.{h,cpp}`;
   - `src/engine/TetrominoState.{h,cpp}`;
   - `src/game/Editor.{h,cpp}`;
   - `tests/test_arrangement.cpp`;
   - `tests/test_commands.cpp`;
   - `tests/test_join.cpp`;
   - `tests/test_vertex_join.cpp`;
   - `tests/test_tetromino_state.cpp`; and
   - `tests/TestHarness.{h,cpp}`;
7. inspect `SConstruct`, `src/register_types.{h,cpp}`,
   `godot-project/main.tscn`, `godot-project/project.godot`, and
   `godot-project/tiles.gdextension`;
8. inspect only the narrow generated godot-cpp headers needed to confirm the
   pinned Godot 4.7 signatures and constants for:
   - `Control::_gui_input(const Ref<InputEvent> &)`;
   - `Control::accept_event`, `set_focus_mode`, and `grab_focus`;
   - `InputEventKey`;
   - `InputEventMouseMotion`;
   - `InputEventMouseButton`;
   - `KEY_TAB`, `KEY_R`, and `MOUSE_BUTTON_LEFT`; and
   - drawing the temporary selected-tile preview and translucent ghost; and
9. establish the completed act-4 baseline by running:

   ```text
   scons tests
   ./build/tests/tiles_tests
   scons
   ./Godot.app/Contents/MacOS/Godot --headless --path godot-project --quit-after 2
   ```

if the act-4 notes or source are absent, the native tests do not pass, the
GDExtension does not load, or the worktree otherwise indicates that act 4 is
still being implemented, stop and ask carson rather than guessing at a moving
interface.

after implementation, record verified api decisions, exact test totals, build
results, complete headless runtime output, proposal-enumeration diagnostics,
and every visual fact left for carson in:

```text
docs/PLACE-TILES-ACT-4-1-IMPLEMENTATION-NOTES.md
```

do not commit. do not mention this implementation document in code comments or
runtime-facing documentation.

the implementor must not perform the visual verification. do not launch a
visible Godot window, capture a screenshot, or judge the controls, ghost,
selection preview, fill, color, or layout by eye. carson owns the visual and
interaction pass. the implementor owns native tests, compilation, headless
execution, exact invariants, deterministic diagnostics, and a precise visual
handoff.

## goal

add the first player-driven placement loop:

```haskell
data Selection = Selection
  { entry       :: PaletteEntryIndex
  , orientation :: PaletteOrientationIndex
  }

data JoinCommand
  = FullEdge MateFullEdgesCommand
  | Vertex   MateVerticesCommand

data JoinProposal = JoinProposal
  { command   :: JoinCommand
  , placement :: Placement
  }
```

`tiles::game::Editor` owns one valid selected palette entry and one of that
entry's distinct compiled orientations. from the authoritative arrangement it
exhaustively constructs every currently successful full-edge and vertex mating
proposal for the selection, deduplicates physically identical placements,
caches the resulting finite set, and uses the mouse only to choose which exact
proposal is active.

the interaction is:

```text
palette-owned selected oriented candidate
                    │
        ┌───────────┴───────────┐
        ▼                       ▼
every anchor edge pair   every anchor vertex pair
        │                       │
        └────── typed commands ─┘
                    │
                    ▼
       const engine::State::preview
                    │
          ┌─────────┴─────────┐
          ▼                   ▼
     typed failure       exact Placement
       discarded               │
                               ▼
                 exact-translation deduplication
                               │
                               ▼
                    cached finite proposals
                               │
             projected centers + mouse position
                               │
                               ▼
                    one highlighted ghost
                               │
                         left mouse click
                               │
                               ▼
                  typed engine::State::apply
                               │
                               ▼
                 authoritative Arrangement
```

the mouse never produces, rounds, snaps, or otherwise constructs an
authoritative `Point`. it selects one command whose exact translation has
already been derived from core features.

act 4-1 adds:

- const, commit-ready join preview to core and engine;
- game-owned palette and orientation selection;
- exhaustive full-edge and vertex proposal enumeration;
- exact physical-placement deduplication;
- cached proposal geometry;
- keyboard selection controls;
- mouse selection and click-to-apply;
- one translucent authoritative ghost; and
- one temporary rendering of the selected oriented prototile in the top-left
  corner.

it does not introduce free placement, targets, deletion, or a general editor
framework.

## preserved architecture

retain the one-way authored dependency direction:

```text
tiles             exact geometry, preview placements, proof-bearing values
  ▲
  │
tiles::engine     palette resolution, supply, typed preview and mutation
  ▲
  │
tiles::game       selection, enumeration policy, mouse choice, presentation
```

preserve every completed contract, including:

- q16.48 `Coordinate` and exact `Point` values;
- cartesian model coordinates with positive y upward;
- validated canonical polygon boundaries and certified triangulations;
- canonical oriented polygons compiled once by the core;
- exact translations derived from selected features;
- pairwise interior-disjoint arrangements which admit boundary contact;
- monotonic placement identity;
- palette-authored distinct orientation vectors;
- configured supply enforced by the engine;
- complete typed core and engine errors;
- transactional `State::apply` commands;
- read-only state observation;
- value ownership without dangling palette, orientation, or placement pointers;
- one-way lossy rendering projection;
- `-fno-exceptions` compatibility; and
- the Godot-independent native core-and-engine test executable.

no file beneath `src/core/` or `src/engine/` may include a Godot header, use a
Godot value, inspect a pixel coordinate, or know about selection, cursor state,
ghosts, or proposal ranking.

do not register any core value, engine value, selection, proposal, or command
with Godot. `Editor` continues to own an ordinary C++ `engine::State` and invoke
it directly.

## tier-1 boundary for this act

this act remains in the existing tier-1 quarter-turn system. its interaction
must be neutral to prototile identity and must operate from the actual polygon
vertices and actual palette-authored distinct orientations. do not hardcode
tetromino vertex counts, edge lengths, or orientation counts into enumeration.

the intended tier-1 content family includes arbitrary orthogonal polygons whose
vertices and axis-aligned edges lie on the q16.48 lattice, not merely
polyominoes. unequal rectangles, concave orthogonal shapes, and squared-square
pieces are representative future content.

the current compiler accepts validated q16.48 polygons and applies exact
quarter turns without enforcing orthogonality. act 4-1 must not silently change
that compiler or add an orthogonality validator. resolving whether tier 1 is
exactly the orthogonal family or a broader lattice family is a later geometry
specification decision and does not affect feature-pair enumeration here.

## exact meaning of exhaustive joining

"every join option" in this act has one precise, finite meaning.

for the selected oriented candidate, enumerate:

```haskell
fullEdgeCommands =
  [ MateFullEdgesCommand anchor anchorEdge selected candidateEdge
  | anchor        <- arrangementEntries
  , anchorEdge    <- indices anchor.footprint.edges
  , candidateEdge <- indices selected.canonicalPolygon.edges
  ]

vertexCommands =
  [ MateVerticesCommand anchor anchorVertex selected candidateVertex
  | anchor          <- arrangementEntries
  , anchorVertex    <- indices anchor.footprint.vertices
  , candidateVertex <- indices selected.canonicalPolygon.vertices
  ]
```

every polygon edge is the cyclic edge beginning at one stored vertex, so each
polygon has exactly `vertices().size()` edge indices. use the strong
`EdgeIndex` and `VertexIndex` types; do not pass or store untyped feature
indices at command boundaries.

submit every generated command to the corresponding const
`engine::State::preview` overload. a successful preview is a proposal. every
typed failure means that exact feature pair is not a currently available
proposal and is silently omitted from the cache. incompatible edges, overlap,
exhausted supply, and all other expected preview failures must not emit one log
line per attempted command.

both command families are included:

- compatible complete-edge mating;
- vertex mating which yields complete-edge contact;
- vertex mating which yields partial-edge contact;
- vertex mating which yields several contacts; and
- legal isolated point contact.

do not classify or suppress isolated point contact. do not require a positive
length of shared boundary. this act intentionally exposes the entire behavior
of the existing full-edge and vertex mating core so carson can evaluate it.

this is not a search over every q16.48 translation along an edge. neither mating
operation slides a candidate along the interior of an edge. a legal partial-edge
contact with no coincident polygon vertices is not generated by these commands
and remains expressible only by the existing `PlaceCommand`, which this act does
not expose to player input.

## core join preview

add const core queries equivalent to:

```cpp
Result<Placement, JoinError> Arrangement::preview_join_full_edges(
    PlacementId p_anchor,
    EdgeIndex p_anchor_edge,
    const OrientedPrototile &p_candidate,
    EdgeIndex p_candidate_edge) const;

Result<Placement, JoinError> Arrangement::preview_join_vertices(
    PlacementId p_anchor,
    VertexIndex p_anchor_vertex,
    const OrientedPrototile &p_candidate,
    VertexIndex p_candidate_vertex) const;
```

a successful core preview returns the exact `Placement` that an immediate
corresponding `try_join_*` call would attempt to insert. the returned value:

- carries the supplied oriented prototile unchanged;
- carries the exact translation derived by the existing alignment operation;
- owns its exact authoritative footprint;
- has no `PlacementId`; and
- does not reserve, consume, or predict a specific identifier.

preview is commit-ready against the current arrangement. it must perform, in
the same precedence already established by `try_join_*`:

1. anchor lookup;
2. anchor and candidate feature bounds checking;
3. exact feature compatibility and translation derivation;
4. checked footprint construction;
5. whole-footprint overlap checking against every arrangement entry; and
6. identifier-availability checking.

overlap retains the first conflicting `PlacementId` according to the existing
arrangement behavior. overlap is checked before identifier exhaustion, exactly
as in `try_insert`.

preview is observationally pure. success and every failure leave:

- entries and their order;
- every existing placement and id;
- `next_id()`;
- identifier exhaustion state; and
- all supplied candidate values

unchanged.

refactor the core so `try_join_full_edges` and `try_join_vertices` use their
corresponding preview operations rather than maintaining a second alignment and
validation implementation. after successful preview, mutation still flows
through the existing invariant-preserving insertion path. it is acceptable for
that insertion to repeat the cheap overlap and identifier checks; do not add an
unsafe "insert already validated" public seam merely to avoid a second scan.

retain the existing `JoinError` vocabulary and exact error information. do not
add a presentation-facing preview error, a boolean `can_join`, exceptions,
assertions, logging, or a callback.

do not add a general contact enumerator to core. the core previews one explicitly
named feature pair; the game owns exhaustive enumeration policy.

## engine join preview

add const engine queries equivalent to:

```cpp
Result<Placement, MateCommandError> State::preview(
    const MateFullEdgesCommand &p_command) const;

Result<Placement, MateCommandError> State::preview(
    const MateVerticesCommand &p_command) const;
```

each overload:

1. resolves the selected palette entry and distinct orientation through the
   existing state-owned palette;
2. enforces configured supply from the current authoritative arrangement using
   the existing precedence `entry -> orientation -> supply`;
3. delegates the exact proposal to the corresponding const `Arrangement`
   preview; and
4. returns the existing `MateCommandError` alternatives without flattening or
   logging them.

do not copy candidate-resolution logic into `Editor`. do not make
`resolve_candidate` public. do not expose mutable palette or arrangement state.

preview does not consume supply. supply remains derived from arrangement
contents, so repeated successful previews are identical and do not change
availability. a subsequent successful `apply` consumes supply only by inserting
the placement into the arrangement.

for a state which is not modified between calls:

```haskell
preview command == success placement
```

must imply that immediate:

```haskell
apply command == success placementId
```

and the stored entry named by `placementId` has the same prototile identity,
orientation, translation, and footprint vertices as the preview. preview makes
no durable promise if any command mutates the state between preview and apply.

retain the existing apply signatures and typed errors. do not add
`PlaceCommand` preview in this act.

## native preview verification

extend the existing native test files rather than creating a Godot dependency.
retain every existing test and behavior.

core tests must establish for both full-edge and vertex preview:

- a successful preview returns the expected exact oriented placement;
- success leaves entries, ids, ordering, and `next_id()` unchanged;
- repeated preview returns the same exact translation and footprint;
- immediate `try_join_*` stores geometry equal to the preview;
- missing anchors and out-of-range features retain existing precedence;
- incompatible full edges retain `incompatible_edges`;
- translation and footprint overflow remain typed;
- overlap names the same conflicting placement as mutation would;
- identifier exhaustion is reported without mutation; and
- preview never alters the candidate oriented prototile.

engine tests must establish for both command families:

- palette entry, orientation, and supply validation retain their established
  precedence;
- a successful preview does not consume finite supply or a placement id;
- repeated successful preview remains successful while state is unchanged;
- the exact `Placement` uses the palette-owned selected identity and
  orientation;
- immediate apply succeeds and stores geometry equal to the preview;
- preview preserves complete core `JoinError` information;
- failed preview changes no observable state; and
- `preview` is callable through a const `State` reference.

do not weaken existing apply tests because apply now shares a preview path.

## game-owned interaction state

keep this act legible inside `src/game/Editor.{h,cpp}`. small private nested
types or namespace-private helpers are appropriate; do not introduce a
controller, input manager, selection model, proposal service, renderer class,
or separately registered Godot type.

represent the new state directly, equivalent to:

```cpp
struct Selection final {
    engine::PaletteEntryIndex entry;
    engine::PaletteOrientationIndex orientation;
};

using JoinCommand = std::variant<
    engine::MateFullEdgesCommand,
    engine::MateVerticesCommand>;

struct JoinProposal final {
    JoinCommand command;
    Placement placement;
};
```

because `Selection` has no empty or invalid meaning and bootstrap can fail,
store it in `std::optional<Selection>` or an equally direct guarded
representation. do not invent sentinel indices.

`Editor` also owns:

- a finite `std::vector<JoinProposal>` cache;
- an optional active proposal index;
- the last local mouse position needed to choose an active proposal; and
- only the small diagnostic guards already justified by act 4.

the proposal cache is derived presentation/input state. authoritative geometry
remains in `engine::State` and each preview `Placement`. do not cache Godot
polygon arrays, transformed model polygons, or a second coordinate
representation.

never cache pointers or references into the arrangement or palette across a
successful command. insertion may reallocate arrangement storage. commands,
strong indices, `PlacementId`s, exact placement values, and vector indices into
the game-owned proposal cache are safe values.

## retained act-4 bootstrap

preserve the act-4 debug bootstrap:

- construct `engine::make_tetromino_state()` fallibly;
- place all nineteen distinct palette orientations in the deterministic debug
  grid through `PlaceCommand`;
- place the first lower `o`;
- verify rejection and transactionality of the deliberately overlapping second
  `o`;
- retain the resulting twenty authoritative placements; and
- render the arrangement through the existing projection and color mapping.

do not replace the debug grid with one starting tile yet. every one of the
twenty placements is an eligible join anchor. this deliberately gives the
proposal enumerator a noisy integration fixture.

after the complete bootstrap succeeds:

1. set the initial selection to palette entry zero and its distinct orientation
   zero;
2. configure the fullscreen root `Control` to receive keyboard focus with the
   pinned Godot 4.7 focus api;
3. initialize the local mouse position from the control if available;
4. build the initial proposal cache;
5. require that this specific debug fixture and initial selection produce at
   least one successful distinct proposal, reporting a useful integration error
   if they do not; and
6. choose the nearest active proposal and request one redraw.

bootstrap failure at any earlier stage remains non-crashing and leaves input
inactive. never dereference absent state, selection, or active proposal values.

## selection controls

override the pinned Godot 4.7 virtual:

```cpp
void _gui_input(const godot::Ref<godot::InputEvent> &p_event) override;
```

the root control fills the viewport and must explicitly accept keyboard focus.
handle only pressed, non-echo key events and pressed left mouse-button events.
ignore releases and unrelated events.

controls are fixed for this temporary interaction:

```text
tab          select the next palette entry
shift+tab    select the previous palette entry
r            select the next distinct orientation
shift+r      select the previous distinct orientation
mouse move   choose the nearest cached join proposal
left click   apply the active proposal
```

entry cycling:

- follows `Palette::entries()` authored order;
- wraps in both directions;
- resets orientation to `PaletteOrientationIndex(0)`; and
- is safe because every palette and every palette entry's orientation vector is
  nonempty by construction.

orientation cycling:

- indexes only the selected entry's `orientations()` vector;
- wraps in both directions;
- never iterates `equivalent_orientations()` as separate geometry; and
- naturally leaves a one-orientation prototile unchanged.

after any selection or orientation change:

1. rebuild the complete proposal cache;
2. recompute the active proposal from the last mouse position;
3. request a redraw; and
4. consume the handled Godot event.

selection remains unchanged after a successful placement so repeated placement
of one oriented prototile can be tested. finite supply, when later used by
content, naturally changes proposal availability through `State::preview`.

do not add Godot input-map actions, scene child controls, buttons, labels, or a
palette panel. these temporary direct keys are sufficient for this act.

## proposal enumeration and deduplication

rebuild proposals only when:

- bootstrap first establishes state and selection;
- palette entry selection changes;
- orientation selection changes; or
- an apply attempt occurs.

mouse motion must not rerun core or engine preview. it only ranks the cached
successful proposals.

enumeration uses the current authoritative arrangement, the current palette
selection, and the selected orientation's canonical polygon. for every
arrangement entry:

1. attempt every anchor-edge × candidate-edge `MateFullEdgesCommand`;
2. attempt every anchor-vertex × candidate-vertex `MateVerticesCommand`;
3. retain each successful exact `Placement`; and
4. ignore each expected typed failure without logging it individually.

record aggregate deterministic diagnostics when rebuilding, sufficient to
observe:

- number of full-edge commands attempted;
- number of full-edge previews which succeeded;
- number of vertex commands attempted;
- number of vertex previews which succeeded; and
- number of distinct physical proposals after deduplication.

do not promise a hardcoded proposal count in source. the count changes after
successful insertion and across selection/orientation changes.

two successful previews are the same physical proposal in this act exactly
when their exact `Placement::translation()` points compare equal. selection
fixes prototile identity and distinct orientation for an entire rebuild, so
equal translation implies equal authoritative footprint.

deduplicate with exact q16.48 equality, never projected floats. when several
commands prove the same physical placement, choose one representative by this
deterministic precedence:

1. a `MateFullEdgesCommand` before a `MateVerticesCommand`;
2. lower anchor `PlacementId`;
3. lower anchor feature index; and
4. lower candidate feature index.

after deduplication, order the physical proposals lexicographically by exact
translation x raw value and then exact translation y raw value. representative
command precedence resolves only duplicate proofs; exact translation order
makes the final cache and every presentation-distance tie deterministic without
using floats as identity.

deduplication removes duplicate command proofs, not a distinct placement option.
do not discard a physical proposal because it:

- touches only at one vertex;
- was derived by vertex rather than full-edge mating;
- lies partly or completely outside the viewport;
- is distant from the mouse;
- has an unusual concave footprint; or
- shares additional legal contacts with other placements.

overlap, feature incompatibility, supply exhaustion, overflow, and identifier
exhaustion are discarded only because authoritative preview rejected them.

straightforward scans and value copies are expected. do not add a spatial index,
memoization framework, worker thread, job system, incremental contact graph, or
parallel proposal generation. the proposal cache itself is the one free and
necessary optimization: it prevents thousands of exact previews from being
repeated for every mouse-motion event.

## mouse proposal choice

the mouse selects among exact cached proposals entirely in presentation space.
for each proposal, compute one presentation-only handle as the arithmetic mean
of its authoritative footprint vertices after projecting them with the existing
`to_screen` function:

```haskell
proposalHandle =
  average . map to_screen . vertices . footprint . placement
```

this is intentionally not the mathematical area centroid. for one fixed
selected orientation, the local mean offset is constant, so distinct exact
translations yield correspondingly translated handles. the value is used only
for cursor ranking and never flows into core or engine.

on mouse motion:

1. store the event's local control position;
2. find the proposal with minimum squared euclidean screen distance between its
   handle and the mouse;
3. break an exact floating-point distance tie by the proposal cache's
   deterministic order;
4. set that proposal active; and
5. request a redraw only when the active proposal changes, because the raw mouse
   position is not itself rendered.

there is no snap radius in this diagnostic act. if the cache is nonempty, one
proposal is always nearest. moving the cursor therefore partitions the screen
into proposal-choice regions for distinguishable projected handles.

two exact translations can theoretically project to the same floating-point
handle when their q16.48 separation is below screen precision. retain both exact
proposals in the cache and use deterministic order for the resulting tie; do
not pretend pixels can distinguish them. the current integer-scale tetromino
fixture is not expected to exercise that limitation. adding a separate proposal
cycling control is deferred.

do not test cursor containment against authoritative polygons, hit-test an
arrangement placement, invert `to_screen`, quantize a mouse coordinate, or feed
a `godot::Vector2` to core or engine.

## click-to-apply

on a pressed left mouse button:

- if state, selection, or an active proposal is absent, do nothing safely;
- otherwise visit the active proposal's stored `JoinCommand` variant and call
  the matching `State::apply` overload;
- inspect the complete typed result before using its value;
- on success, preserve selection, rebuild proposals against the mutated
  arrangement, recompute the active proposal from the current mouse position,
  emit one concise success diagnostic naming the new `PlacementId` and command
  family, and request a redraw;
- on failure, report the complete typed `MateCommandError` at the presentation
  boundary, rebuild proposals against the unchanged state, recompute the active
  proposal, and request a redraw; and
- consume the handled mouse event.

an apply failure from a cached successful preview is unexpected in this
single-threaded editor because no mutation occurs between cache selection and
the click handler. nevertheless, handle it non-crashingly and preserve its
typed diagnostic. do not assert preview/apply agreement into existence.

extend the existing game-side error description helpers narrowly for
`MateCommandError` and `JoinError`. do not add error strings to core or engine
and do not log the thousands of ordinary preview failures.

## ghost rendering

continue to draw every stored arrangement footprint exactly as act 4 does.
after the authoritative arrangement and before the temporary selection overlay,
draw at most one ghost:

- use the active `JoinProposal::placement.footprint()` ordered boundary;
- project it through the same one-way `to_screen` function;
- fill it with the selected prototile's existing identity color at visibly
  reduced alpha;
- optionally add one simple presentation-only outline if the pinned Godot api
  supports it without triangulation or another geometry representation; and
- never mutate state from `_draw()`.

the ghost is authoritative preview geometry, not a cursor-translated polygon.
it must jump among exact feature-derived placements as the active proposal
changes.

do not render every cached proposal simultaneously. every option remains
available to mouse ranking, while only the active option becomes a ghost. do
not add invalid red ghosts in this act.

## temporary selected-tile rendering

render the current `Selection`, in its current distinct orientation, in a small
fixed preview cell at the top-left of the editor.

the preview:

- reads the selected `PaletteEntry` and selected
  `OrientedPrototile::canonical_polygon()` directly;
- uses the prototile's existing identity color;
- preserves the oriented polygon's shape with the same cartesian-y-to-screen-y
  flip;
- measures its actual bounding box across every vertex;
- uniformly scales and centers it inside the fixed cell without changing model
  state;
- is derived again when selection changes; and
- is presentation-only.

use a compact cell contained within screen coordinates
`[8, 52] × [8, 52]`, before the act-4 arrangement's existing x origin at 60.
leave a small inner margin and, if useful for contrast, draw one plain
presentation-only background rectangle. do not add text, a tile name, an
orientation label, a button, or a child node.

this selection preview has its own local fit transform. it is not a second world
projection and it must not change the act-4 arrangement projection constants.
no selected-preview pixel value may enter a command.

## redraw and lifetime behavior

retain the event-driven surface. request redraw after:

- bootstrap finishes or fails;
- selection or orientation changes;
- mouse motion changes the active proposal presentation;
- an apply attempt succeeds or fails; and
- any proposal-cache rebuild changes the visible ghost.

do not add `_process`, physics processing, a timer, continuous redraw, signals,
observers, or callbacks.

`_draw()` must tolerate every partially initialized state:

- absent engine state;
- absent selection;
- an empty proposal cache; and
- no active proposal.

all optional and `Result` values must be inspected before dereference. retain
`-fno-exceptions` compatibility and avoid throwing accessors.

## source and build layout

the expected authored changes are confined to:

```text
src/core/Arrangement.h
src/core/Arrangement.cpp
src/engine/State.h
src/engine/State.cpp
src/game/Editor.h
src/game/Editor.cpp
tests/test_join.cpp
tests/test_vertex_join.cpp
tests/test_commands.cpp
docs/PLACE-TILES-ACT-4-1-IMPLEMENTATION-NOTES.md
```

small test changes elsewhere are permitted only when directly needed to reuse an
existing fixture. do not change `SConstruct`: recursive extension discovery and
the explicit core+engine native test trees already have the correct boundary.

do not change GDExtension registration, `main.tscn`, or `project.godot`. the
single fullscreen `Editor` remains the application root.

do not split selection, proposals, input, preview rendering, or error formatting
into new production files or new registered types.

## diagnostics

retain every act-4 bootstrap diagnostic and its established meaning. add concise
diagnostics sufficient for headless execution to establish:

- the initial selection is palette entry zero, orientation zero;
- the initial exhaustive rebuild attempted both full-edge and vertex commands;
- at least one preview of some command family succeeded;
- exact duplicate placements were collapsed when present;
- the resulting initial distinct proposal cache is nonempty;
- one active proposal was chosen from the initialized mouse position when the
  runtime supplies one; and
- the draw path can project the arrangement, selected preview, and active ghost
  when present.

aggregate preview results. never emit one line per attempted or failed feature
pair.

interactive key and mouse success diagnostics may identify entry index,
orientation index, proposal count, command family, and resulting placement id.
do not dump every polygon vertex or command.

headless execution will not synthesize human input unless a tiny
integration-only event injection is already conventional in the repository. do
not add a fake autoplay path, mutate the arrangement from `_ready()` beyond the
retained act-4 bootstrap, or bind public testing methods merely to manufacture
interactive logs.

## verification

run and record:

```text
scons tests
./build/tests/tiles_tests
scons
./Godot.app/Contents/MacOS/Godot --headless --path godot-project --quit-after 2
```

if the established Godot 4.7 executable uses a different local path, use that
exact executable and record it.

the native suite must remain independent of Godot and cover the new core and
engine preview contracts.

the extension build must compile the new `_gui_input` override, event casts,
selection state, command variant, preview cache, and draw path with no authored
warning or error under the repository's existing flags.

the headless run must:

- load the GDExtension without symbol or registration errors;
- instantiate the existing fullscreen `Editor`;
- complete the retained twenty-placement bootstrap;
- initialize a valid selection;
- exhaustively build a nonempty distinct proposal cache;
- issue aggregate proposal counts without per-failure spam;
- reach `_draw()`;
- project the temporary selected oriented prototile;
- project one active ghost when a mouse position and active proposal exist;
- contain no unexpected error or warning line; and
- exit successfully.

headless execution does not verify input feel or pixels. the implementation
notes must explicitly hand these unverified facts to carson:

- the compact top-left selection preview is visible and does not obscure the
  debug arrangement;
- `tab` and `shift+tab` cycle authored palette order and wrap;
- changing entries resets orientation to zero;
- `r` and `shift+r` cycle only distinct oriented geometry and wrap;
- symmetric pieces visibly expose only their actual distinct orientations;
- the selected preview always matches the selected orientation;
- mouse movement makes the ghost jump among exact join locations;
- isolated point-contact proposals remain selectable;
- full-edge, vertex-derived full-edge, partial-edge, and point joins can all be
  observed when the fixture offers them;
- the ghost never moves continuously with the cursor as though free-placed;
- left click adds exactly the highlighted proposal;
- repeated joins redraw without visible drift;
- overlap is never inserted;
- proposal rebuilding remains responsive on the current twenty-anchor debug
  arrangement; and
- arrangement colors, winding, concave fills, and y direction remain correct.

the implementor must not open a visible window to answer these questions.

## acceptance

act 4-1 is complete when all of the following hold:

- `Arrangement` exposes const full-edge and vertex preview operations;
- core preview returns the exact commit-ready `Placement` without mutation;
- core `try_join_*` routes through the corresponding preview logic;
- preview preserves existing typed error precedence and conflict identity;
- `State` exposes const preview overloads for both mating commands;
- engine preview resolves the palette candidate and enforces supply exactly as
  apply does;
- successful preview consumes neither supply nor identity;
- immediate preview and apply agree when state is unchanged;
- native tests cover success, failure, purity, and preview/apply agreement;
- `Editor` owns one guarded valid `Selection`;
- initial selection is palette entry zero, orientation zero;
- tab and shift-tab cycle palette entries and reset orientation;
- r and shift-r cycle the selected entry's distinct orientations;
- the current selected oriented prototile is rendered in the top-left temporary
  preview cell;
- every full-edge and vertex feature pair against every arrangement entry is
  submitted to authoritative preview;
- legal isolated point contacts are retained;
- no sliding or free-translation search is introduced;
- successful physical placements are deduplicated by exact translation;
- representative commands use deterministic full-edge-first precedence;
- proposal generation is cached between state or selection changes;
- mouse motion performs only presentation-space ranking of cached proposals;
- no mouse coordinate becomes an authoritative model coordinate;
- one nearest proposal is rendered as a translucent authoritative ghost;
- left click applies the ghost's stored typed mating command;
- successful apply preserves selection, rebuilds proposals, and redraws;
- failed apply is non-mutating, typed, reported, and followed by a rebuild;
- the retained act-4 debug bootstrap and rendering still work;
- no continuous processing or general ui framework is added;
- all native tests pass;
- the GDExtension builds cleanly;
- the main scene completes its headless checks without unexpected errors; and
- visual and interaction verification is handed explicitly to carson.

## explicitly out of scope

act 4-1 does not implement:

- free placement or player-facing `PlaceCommand`;
- conversion, inversion, rounding, or snapping from pixels to q16.48;
- sliding contact search along an edge interior;
- automatic partial-edge contact without coincident vertices;
- rejection of isolated point contact as an interaction policy;
- positive-length boundary-contact classification;
- target regions, levels, target containment, or solution checking;
- deletion, movement, replacement, undo, redo, or reset;
- placement selection or arrangement hit testing;
- dragging;
- remaining-supply labels or a production palette;
- mouse palette buttons, text labels, panels, menus, or toolbars;
- controller, touch, or accessibility input;
- configurable input bindings;
- invalid red ghosts or rendering every proposal simultaneously;
- a snap radius or tuned proposal-choice heuristic;
- a separate control for cycling proposals whose projected handles coincide;
- zoom, pan, camera, dynamic fitting, or responsive world layout;
- a general render cache or render-view dto;
- per-placement nodes, meshes, textures, materials, or shaders;
- explicit renderer triangulation;
- `_process`, physics processing, timers, continuous redraw, or animation;
- signals, callbacks, observers, services, repositories, or an event bus;
- title screens, scene navigation, campaign flow, persistence, or save data;
- tier-2 or tier-3 geometry;
- tier-1 orthogonality validation or a change to admitted geometry;
- optimization beyond rebuilding proposals only after relevant state changes;
- visible implementor testing, screenshots, or implementor aesthetic judgment;
  or
- changes to any completed contract not explicitly required above.

if implementation reveals that exhaustive feature-pair preview cannot satisfy
this contract without changing a completed geometric or state invariant, stop
and report the exact conflict to carson. do not replace exact preview with
approximate frontend geometry, silently introduce free placement, or widen the
act.
