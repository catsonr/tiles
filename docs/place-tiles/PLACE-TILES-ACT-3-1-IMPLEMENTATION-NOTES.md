# place tiles act 3-1 — implementation notes

Notes from implementing `docs/PLACE-TILES-ACT-3-1-IMPLEMENTATION.md`: typed
palette-resolved, supply-aware commands as the only mutation surface above the
exact core. Everything below is verified against the built test executable, a
default GDExtension build, a headless load, and a separate verbatim compile of
the plan's acceptance snippet.

## verification

- `scons tests` builds `build/tests/tiles_tests` from `src/core/**` +
  `src/engine/**` + `tests/**` with the plain toolchain env (no godot-cpp). It
  reports **1569 checks, 0 failed across 173 cases** — the act-3 baseline of
  **1361/144** plus **208 checks / 29 cases** of new command coverage. Every
  pre-existing core and engine case still passes unchanged.
- `scons` still builds the extension; `src/engine/State.cpp` recompiles into the
  shared library under godot-cpp's flags (`-std=c++17 -fno-exceptions`) with no
  warnings or errors. `Commands.h` is header-only and adds no object file.
- Headless run (`Godot --headless --path godot-project --quit`) prints
  `[tiles] gdextension smoke test ready` with no load/symbol/registration errors
  and no warning lines. The extension still registers **only** the disposable
  smoke node; no command, index, or engine value is registered with Godot.
  `register_types.cpp` and the smoke node were not touched.
- The plan's acceptance snippet was transcribed **verbatim** into a standalone
  `main` and compiled against the repository headers with
  `g++ -std=c++17 -fno-exceptions -Wall -Wextra` — zero warnings. It runs: both
  commands succeed, the mating returns `PlacementId(1)`, the arrangement holds 2
  entries, and the derived translation is exactly `(0, -2)` game units.
- No engine file includes a Godot header, mentions a Godot runtime type, or
  depends on `src/nodes/` (grep-verified).

## layout

```
src/engine/Commands.h   (new, header-only: strong indices, commands, errors)
src/engine/State.h      (modified: three apply overloads + private resolver)
src/engine/State.cpp    (modified: candidate resolution, supply, delegation)
tests/test_commands.cpp (new)
```

`SConstruct` needed **no edit**: the extension's recursive `src/**` glob and the
test suite's `src/core` + `src/engine` + `tests` trees already discover both new
files. No core header, core `.cpp`, existing engine file other than `State`, or
existing test was changed. The act-0/1/2 kernel remains byte-for-byte untouched.

## decisions worth recording

- **Candidate resolution is one private `State` method returning a pointer.**
  `resolve_candidate(PaletteEntryIndex, PaletteOrientationIndex) const` returns
  `Result<const OrientedPrototile *, CandidateError>`. It is `const` — lookup and
  supply derivation are strictly read-only — and the pointer addresses the
  palette's **stored** oriented value, so no orientation geometry is ever
  recompiled or copied. The pointer is safe because no command mutates or
  reorders `palette_`, and arrangement mutation cannot reallocate it. All three
  overloads share this one function, which is what makes the entry → orientation
  → supply precedence structurally identical across commands rather than
  replicated three times.
- **Supply counting short-circuits at the configured capacity.** This is the
  whole overflow argument: the running count returns the moment it *equals* the
  capacity, so it never exceeds a value the capacity itself holds, and no
  remaining count is ever derived by subtraction. A finite `UINT64_MAX` capacity
  therefore has no counting overflow path at all — positively tested. Unlimited
  supply short-circuits before the loop. Nothing is stored, decremented,
  reserved, or rolled back; the arrangement stays the single source of usage.
- **Supply identity is exact `PrototileId`, checked against
  `entry.placement.prototile().id()`.** Not geometry comparison, not pointer
  identity, not storage order, not orientation, and not provenance — a matching
  placement preloaded through the `State` constructor counts exactly as one added
  by a command (tested), and a preloaded prototile absent from the palette is
  tolerated and left untouched (tested, including its id and translation).
- **The command errors are plain `std::variant` aliases, not a new class.**
  `PlaceCommandError = variant<CandidateError, PlacementError, ArrangementError>`
  and `MateCommandError = variant<CandidateError, JoinError>`. The three
  alternatives are distinct types, so construction is unambiguous and no tag is
  needed. Tests read them with `std::get_if`, matching `Result.h`'s deliberate
  avoidance of the throwing `std::get` under `-fno-exceptions`. Core errors are
  moved through **unchanged** — `ArrangementError::conflicting_placement` and
  `JoinError::{code, conflicting_placement}` are asserted intact at the engine
  boundary, never flattened, logged, or renamed.
- **No pre-testing anywhere.** `apply(PlaceCommand)` does not test overlap before
  `try_insert`; the mating overloads do not resolve the anchor, bounds-check a
  feature index, test edge compatibility, derive a translation, or build a
  footprint. Each is a resolve-then-delegate pair, so transactionality is
  inherited from the core rather than reimplemented — no `State` copy, no
  rollback, no snapshot.
- **The translation reaches the core untouched.** `p_command.translation` is
  passed straight to `Placement::make`. There is no `SCALE` reference, rounding,
  snapping, clamping, cell size, grid check, prototile inspection, or tier/content
  branch anywhere in `State.cpp`. The fractional-translation test uses
  `SCALE/2 + 1` and `-(SCALE/4) - 3` — unreachable on any integer cell grid — and
  asserts the stored translation and footprint origin are bit-identical.
- **`PaletteOrientationIndex` indexes distinct variants, never equivalence
  labels.** The out-of-range-orientation test makes this structural rather than
  documentary: it authors a square with all four quarter turns requested, asserts
  the entry holds **one** orientation whose `equivalent_orientations()` has size
  **four**, and then shows index 1 is rejected as out of range.

## test coverage added (29 cases)

- **strong indices and lookup (4)** — exact values and value comparison;
  `static_assert`s that neither index is implicitly constructible from
  `std::size_t`, that both are explicitly constructible from one, that they are
  distinct types, and that neither is constructible from the other in either
  direction; both out-of-range rejections with `next_id()` and entries unchanged.
- **exact placement (9)** — origin placement with id zero; bit-for-bit
  preservation of a nonzero q16.48 translation; the fractional-translation case;
  selected prototile id plus the second **distinct** orientation identified by
  exact axis-aligned extent (the quarter-turned 4x1 bar measures 1x4); next-id
  allocation; complete `ArrangementError` on interior overlap including the
  conflicting id; `identifier_exhausted` via `testing_with_next_id(UINT64_MAX)`;
  `PlacementError::footprint_overflow`; and one case that runs **all four**
  failure kinds against a populated state and asserts entries, ids, translations,
  the allocator, and the palette are unchanged.
- **supply (8)** — every bullet of the plan's supply list, including the
  preloaded-placement, foreign-prototile, shared-across-orientations, and
  `UINT64_MAX` cases.
- **feature mating (7)** — both successes (the full-edge case is the acceptance
  scenario; both assert the returned id equals the id stored in the appended
  entry and that the derived translation is exact); representative `JoinError`
  preservation for both commands; candidate rejection ahead of a simultaneously
  invalid anchor **and** feature index, asserting no `JoinError` alternative is
  even present; supply rejection ahead of two matings that would otherwise
  succeed; and a failed mating consuming neither supply nor an id.
- **contact semantics (1)** — a 4x1 bar anchor with a 2x2 square placed at the
  exact translation `(1, 1)`, so the square's bottom edge lies strictly inside the
  anchor's top edge. It proves `PlaceCommand` accepts it, that insertion itself is
  the interior-disjointness proof, that **no** pair of polygon vertices coincides
  (so no vertex mating could derive it), and that the corresponding
  `MateFullEdgesCommand` returns `incompatible_edges` because the selected edge
  vectors differ in length. All coordinates are exact; no tolerance comparison
  and no partial-edge path exists in engine or core.

The core's exhaustive alignment, overlap, and transactional matrices are
deliberately **not** duplicated. Engine tests prove candidate resolution, supply
enforcement, error preservation, and delegation.

## error paths

Reachable and positively tested at the engine boundary:

- all three `CandidateError` values;
- `PlacementError::footprint_overflow`;
- `ArrangementErrorCode::{interior_overlap, identifier_exhausted}` with
  `conflicting_placement` present and absent respectively; and
- `JoinErrorCode::{anchor_not_found, anchor_edge_out_of_range,
  anchor_vertex_out_of_range, candidate_vertex_out_of_range, incompatible_edges,
  interior_overlap}`.

Reachable but intentionally **not** re-tested here, because they are exhaustively
covered by the core suites and the engine adds no logic to them:
`JoinErrorCode::{candidate_edge_out_of_range, translation_overflow,
footprint_overflow, footprint_construction_failed, identifier_exhausted}`.

Structurally unreachable, unchanged from earlier acts and never asserted away:
`PlacementError::footprint_construction_failed` (exact translation of a valid
polygon reconstructs successfully — see the act-1 and act-2 notes).

## out of scope (correctly absent)

No file beneath `src/game/`; no Godot type, node, resource, scene, signal, input,
or rendering behavior; no `Point -> Vector2` projection; no integer-grid
enforcement, snapping, or cell coordinates; no geometry-tier dispatch; no palette
mutation or runtime orientation compilation; no selection, held-tile,
current-orientation, mouse, or keyboard state; no automatic edge/vertex/contact
selection; no partial-edge mating kernel or contact search; no stored adjacency;
no removal, replacement, movement, undo, redo, or persistence; no public
insertion of arbitrary placements or oriented prototiles; no mutable palette or
arrangement access; no targets, level legality, or solution checking; no
translation rules; no tier-2 geometry; and no change to existing core behavior.

Also deliberately absent per the plan: a generic command base class, a
`std::variant` dispatcher or visitor, a queue, callbacks/signals/observers, a
revision counter, a service/repository/controller interface, a second
state-owning aggregate, and any free function taking `State&`.

No public remaining-supply query was added. The command boundary only needs to
enforce capacity, and the plan defers a consumer-facing query until a concrete
interface requires one.

## a note for act 4

- **The mutation surface is complete and is exactly three overloads.** Own a
  `State` inside a `tiles::game` object, send `PlaceCommand` /
  `MateFullEdgesCommand` / `MateVerticesCommand` into it, and render only through
  `palette()` and `arrangement()`. Nothing else is needed and nothing else exists.
- **Redraw is a game concern.** `apply` returns a `PlacementId` on success and
  changes nothing on failure, so "did anything change?" is exactly
  "did the result have a value?". There is no engine notification system, and act
  3-1 added no revision counter — if act 4 wants one, that is a game-side
  decision, not an engine gap to fill silently.
- **`PaletteOrientationIndex` is what "current orientation" means.** Cycling is
  `(index + 1) % palette().entries()[e].orientations().size()`. Selection state,
  held-tile state, and the current index itself belong to `tiles::game`.
- **Errors are `std::variant`s; a UI will want a translation layer.** Presenting
  them means a `std::visit` or a chain of `std::get_if` in `tiles::game`. Do that
  conversion at the presentation boundary — do not push Godot-facing error values
  back down into the engine.
- **Two O(n) scans per command, both fine at editor scale, both worth naming.**
  Supply derivation scans the arrangement once per command (short-circuiting for
  finite capacities), and the core's anchor lookup scans it again per mating. For
  a handcrafted level this is nothing. If a later act wants thousands of
  placements, the fix is an id-indexed lookup and a per-prototile usage index —
  but that is a real design decision about where derived state may live, not an
  implementation shortcut, and the plan explicitly forbids storing a counter now.
- **Partial-edge contact is expressible only through `PlaceCommand`.** When act 4
  builds input, remember that the two mating commands are translation-derivation
  conveniences, not a classification of legal contact. Any snapping policy that
  offers only mating will silently make a whole class of legal arrangements
  unreachable to the player. The engine already permits them.
