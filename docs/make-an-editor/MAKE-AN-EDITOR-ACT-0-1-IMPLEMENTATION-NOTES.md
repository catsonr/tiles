# make an editor act 0-1 — implementation notes

## summary

exact placement removal, derived supply status, and single-branch undo are in.
`State` turned out to be safely copyable as a value with no non-value member, so
snapshots are literal `State` copies exactly as specified — no inverse commands,
no shared mutable state, no serialization round trip.

no open questions, no spec deviations, nothing left unimplemented. every
required test case in the plan is satisfiable and satisfied.

## new files

```text
src/engine/Session.h
src/engine/Session.cpp
tests/test_session.cpp
```

modified: `src/core/Arrangement.{h,cpp}`, `src/engine/Commands.h`,
`src/engine/State.{h,cpp}`, `tests/test_arrangement.cpp`,
`tests/test_commands.cpp`, `tests/test_state.cpp`.

`SConstruct`, gdextension registration, `main.tscn`, `project.godot`,
`src/game/Editor.{h,cpp}`, and every other authored file are untouched:
recursive discovery picked up `Session.cpp` and `test_session.cpp` on its own.

## exact arrangement removal

```cpp
enum class RemovalError {
    placement_not_found,
};

Result<PlacementId, RemovalError> Arrangement::try_remove(PlacementId p_placement);
```

one linear scan of `entries_` in storage order comparing `entry.id` for exact
equality; the first (and only possible) match is erased with
`std::vector::erase` and the requested id is returned. no match returns
`placement_not_found` having mutated nothing — the loop's only side effect is
the erase, which is reached only on a match.

`vector::erase` shifts the tail down without reordering it, so survivor relative
order, identities, and placement values are preserved by construction rather
than by a rebuild.

`next_id_` and `exhausted_` are never touched on either path. consequences,
all tested:

- removing `1` from `{0, 1, 2}` and inserting again yields `3`, not `1`;
- deleting the last entry leaves an empty arrangement whose allocator is still
  where it was left;
- an exhausted arrangement is still exhausted after deletion, and still rejects
  insertion with `identifier_exhausted`.

nothing removes by index, geometry, prototile, pointer, or translation; nothing
renumbers, compacts, or rewinds; the removed `Placement` is not returned; and no
adjacency bookkeeping was added.

## typed engine deletion

```cpp
struct RemoveCommand final { PlacementId placement; };
using RemoveCommandError = RemovalError;

Result<PlacementId, RemoveCommandError> State::apply(const RemoveCommand &);
```

the body is one line: `return arrangement_.try_remove(p_command.placement);`.
no palette lookup, orientation lookup, supply check, region test, overlap test,
completion special case, or id allocation. the core's typed success and failure
are published unchanged — not flattened to a bool, string, log line, or existing
command-error variant.

`RemoveCommandError` is a plain alias rather than a `std::variant`: removal has
exactly one alternative, so there is nothing to discriminate.

`solved()` is untouched and remains derived. removing any positive-area
placement necessarily shrinks the covered area below the region's, so a solved
state becomes unsolved with no flag to toggle.

## derived supply status

```cpp
struct SupplyStatus final {
    Supply::Amount used;
    std::optional<Supply::Amount> remaining;
};

std::optional<SupplyStatus> State::supply_status(PaletteEntryIndex) const;
```

`SupplyStatus` lives in `src/engine/State.h` beside the query that produces it.
an out-of-range entry index returns `std::nullopt`; that is the only failure a
constructible state has.

### one count, not two

the act-0 `supply_available` counted with an early stop at capacity, which would
have been a second counting rule beside the palette view. it was replaced by
three functions in the anonymous namespace of `State.cpp`, layered so there is
exactly one derivation:

```text
supply_used(arrangement, id)            -- one increment per matching PrototileId
        │
        ▼
supply_status_of(arrangement, id, supply)
        │                     used, and capacity - used when finite
        ├──────────────▶ State::supply_status   (palette view)
        └──────────────▶ supply_available       (command legality)
```

`supply_available` is now literally
`!status.remaining.has_value() || status.remaining.value() != 0` — a finite
candidate is available exactly when its derived remaining amount is nonzero, so
command resolution and the palette view cannot disagree.

identity is the exact `PrototileId`. geometry equality, pointer identity,
palette index coincidence, and orientation are irrelevant, so all orientations
of one prototile draw on one capacity (tested).

`remaining == std::nullopt` means unlimited. a used-up finite entry reports
`remaining == 0`, and that same zero is what publishes `supply_exhausted`.

### arithmetic safety

dropping the early stop means `used` is now counted to completion. it is one
increment per arrangement entry, so it is bounded by `entries_.size()` and
cannot leave `std::uint64_t`.

`capacity - used` is guarded by `assert(used <= capacity)`. that is an
invariant, not a hope: a `State` starts empty, the only way to add an entry is a
command that proved capacity beforehand, and act 0 removed every preloading
seam. a `UINT64_MAX` capacity is tested for both counting and subtraction.

nothing stores, increments, decrements, serializes, or independently undoes a
remaining counter.

## session

```cpp
class Session final {
public:
    explicit Session(State p_initial);
    const State &state() const;
    bool can_undo() const;              // undo_depth() != 0
    std::size_t undo_depth() const;     // undo_history_.size()
    bool undo();
    Result<PlacementId, PlaceCommandError>  apply(const PlaceCommand &);
    Result<PlacementId, MateCommandError>   apply(const MateFullEdgesCommand &);
    Result<PlacementId, MateCommandError>   apply(const MateVerticesCommand &);
    Result<PlacementId, RemoveCommandError> apply(const RemoveCommand &);
private:
    template <typename CommandT>
    auto apply_reversible(const CommandT &)
        -> decltype(std::declval<State &>().apply(p_command));
    State state_;
    std::vector<State> undo_history_;
};
```

`state()` is the only state accessor and is const through a mutable session. no
mutable `State`, `Arrangement`, `Level`, `Palette`, `Region`, or history view
escapes. `undo_depth()` is the vector size, never a parallel counter.
`can_undo()` is defined in terms of it.

the four overloads keep their existing command-specific error types; no unifying
command variant was introduced.

### the shared algorithm

all four overloads are one line each, delegating to the private template:

```cpp
State candidate = state_;
auto result = candidate.apply(p_command);
if (!result.has_value()) {
    return result;
}
undo_history_.push_back(std::move(state_));
state_ = std::move(candidate);
return result;
```

the command touches only the candidate copy, and the installed candidate is
exactly the one that produced the returned success — it is never reapplied after
the history push. history is pushed only after success is known.

**one c++17 detail worth recording.** the template was first written with a
deduced (`auto`) return type and the four public overloads defined inline in the
class. that does not compile: the inline bodies are complete-class contexts
parsed at the end of the class, so they call `apply_reversible` before its
definition is available for return-type deduction. the fix is a trailing return
type, `-> decltype(std::declval<State &>().apply(p_command))`, declared in-class
and defined after the class body in the same header. `std::declval` is used
rather than `state_` because a member's trailing return type is not a
complete-class context in c++17. this keeps the helper private and non-deduced
while every overload still forwards its own exact error type.

### snapshots are plain values

the plan asked to stop and report if `State` were not safely copyable. it is:
`State` is `Level` (`Palette` + `Region`) plus `Arrangement`, and no type in
that closure declares a destructor, copy constructor, or assignment operator, so
every special member is implicitly generated over `std::vector`, `std::optional`,
and trivially copyable leaves. there is no pointer, reference, handle, or
observer anywhere in the closure. `test_state.cpp` pins
`is_copy_constructible_v<State>` and `is_move_assignable_v<State>`.

so a snapshot is a prior `State` and nothing else. `Session` holding
`vector<State>` while `State` holds no history is what makes the recursive
`State<history<State<...>>>` shape unrepresentable.

## undo behavior

```cpp
bool Session::undo() {
    if (undo_history_.empty()) return false;
    state_ = std::move(undo_history_.back());
    undo_history_.pop_back();
    return true;
}
```

empty history is a non-mutating `false`; no `UndoError` was invented to avoid
returning a bool. restoration is whole-value, so it carries the exact level,
palette order/supplies/orientations/polygons, outer and hole boundaries,
arrangement entry order, every id, `next_id()` including exhaustion, every
derived supply status, and `solved()` — all verified componentwise, not by
arrangement size.

the abandoned state is destroyed by the move-assignment. there is no redo stack
and no redo api.

`next_id()` rewinding through undo is intentional and tested: undoing a
placement restores the pre-placement allocator, and a later placement then
legitimately receives that same identity. undoing a *deletion* restores the
entry while keeping the allocator value it had immediately before the deletion,
because deletion never moved it.

history is unbounded. no cap, persistence, compression, or checkpointing.

## previews and observation

no preview is wrapped as a mutation. callers use `session.state().preview(cmd)`,
which is const and creates no snapshot. `state()`, `can_undo()`,
`undo_depth()`, `supply_status()`, `solved()`, `level()`, `palette()`,
`region()`, and `arrangement()` are all verified to leave state and history
untouched.

no mutable convenience access was added for the current Godot `Editor`, which
still owns a `State` directly and was not migrated.

## tests

new `tests/test_session.cpp`: **21 cases**.

it carries a test-only complete-state comparison — `same_state`, built from
`same_palette`/`same_region`/`same_arrangement`/`same_placement`/
`same_supply_status` over the existing named `same_boundary`. no `operator==`
was added to any domain type: equality on `Polygon`, `Prototile`, or `Placement`
would imply congruence or identity semantics the project has deliberately kept
named or absent.

`same_state` compares palette order; each entry's prototile id, exact reference
polygon, supply, and full distinct-orientation sequence with equivalence labels
and canonical polygons; the region's outer boundary, ordered holes, and doubled
area; arrangement entry count and storage order; each placement's id, prototile
id, representative orientation, translation, oriented polygon, and footprint;
`next_id()` including exhaustion; every entry's supply status; and `solved()`.

coverage: initial exactness and empty history; `can_undo`/`undo_depth`/`undo`
agreement on empty history; static assertions that no mutable state or history
view escapes and that `undo()` returns a plain bool; one snapshot each for
place, full-edge mate, vertex mate, and delete; static assertions plus value
checks that every command result and error alternative survives unchanged;
failure adding no snapshot for candidate resolution, orientation, footprint
construction, outside-region, overlap, mating, exhausted supply, and deletion;
failures while history is nonempty adding no hidden undo (observed through
`undo_depth()` and then walked to the initial state); previews and observations
adding none; one undo restoring completely; a six-state mixed timeline
(place/place, place/mate, mate, delete) walked back in exact reverse order
against recorded snapshots; the extra undo at the bottom; allocator rewind and
identity reuse after undo; deletion not rewinding the allocator; undo of a
deletion restoring id and storage position; derived supply counts across
exhaustion, deletion, and undo; a two-entry palette with a holed region restored
whole; solved-to-unsolved on undoing a final placement (no-hole); unsolved-to-
solved on undoing a deletion (holed); single-branch continuation with no redo;
and exact q16.48 fractional geometry surviving two snapshot copies bit for bit.

updated:

- `tests/test_arrangement.cpp` — 11 → **19 cases**. empty-arrangement and
  unknown-id removal failures with full preservation of entries, order,
  placements, `next_id`, and exhaustion; first/middle/last removal in a loop
  with exact returned id and survivor order/values; no identity reuse after
  deletion; deleting the final entry leaving an allocation-continuing
  arrangement; exhausted staying exhausted; and survivors keeping their contact
  and overlap invariants (each still names itself as the conflict, and the
  vacated space re-accepts a full-edge-contacting placement);
- `tests/test_commands.cpp` — 35 → **51 cases**. a `derived supply status`
  section (out-of-range, unused finite, unlimited, orientations sharing one
  capacity, foreign ids not counting, remaining reaching exactly zero and that
  zero exhausting both `apply` and `preview`, deletion restoring exactly one,
  unlimited `used` moving with `remaining` staying empty, `UINT64_MAX` counting
  and subtraction, and observational purity) and an `exact deletion` section
  (deleting exactly the named placement, missing-id being typed and
  transactional, the level/palette/region/survivors untouched, survivor order
  across every storage position, a deleted id reporting the existing
  `anchor_not_found` for both matings while survivors remain ordinary anchors,
  and solved → unsolved → solved with a fresh identity). one existing comment
  about counting stopping at capacity was corrected to describe the new count;
  no check was weakened or removed;
- `tests/test_state.cpp` — 7 → **9 cases**. `supply_status` as a const
  observation returning an owned `std::optional<SupplyStatus>`, and static
  assertions that `State` is copyable and move-assignable so a session can
  snapshot it — the structural claim that history lives above the state.

no unrelated completed test was weakened or removed.

## verification

```text
$ scons tests
$ ./build/tests/tiles_tests
2701 checks, 0 failed across 297 cases     (act-0 baseline: 2204 / 250)
```

`+497` checks, `+47` cases.

each new and modified translation unit also compiles clean and warning-free
standalone under
`c++ -std=c++17 -fno-exceptions -Wall -Wextra -Wpedantic -Isrc -Itests`.

grep over `src/core/` and `src/engine/` for `godot`, `gdextension`,
`godot_cpp`, `Ref<`, `Variant`, `Node2D`, `Control`, `String`, `PackedVector`,
`Vector2`, and `Color` finds no Godot include, type, resource name, or
identifier — the only hits are `std::variant`, the word "invariant", `<cstring>`,
and prose comments.

```text
$ scons
Compiling shared src/register_types.cpp ...
Compiling shared src/core/Arrangement.cpp ...
Compiling shared src/engine/Session.cpp ...
Compiling shared src/engine/State.cpp ...
Compiling shared src/engine/TetrominoState.cpp ...
Compiling shared src/game/Editor.cpp ...
Linking Shared Library godot-project/bin/macos/libtiles.macos.template_debug.dylib ...
scons: done building targets.
```

the extension builds cleanly with no game-layer migration to `Session`.

```text
$ ./Godot.app/Contents/MacOS/Godot --headless --path godot-project --quit-after 2
Godot Engine v4.7.1.stable.official.a13da4feb - https://godotengine.org

[tiles] handcrafted tetromino state constructed: palette order 7
[tiles] debug region: outer bounds x [-8.0, 36.0], y [-30.0, 8.0], 0 holes
[tiles] debug grid: 19 distinct palette orientations placed across 7 columns
[tiles] overlap fixture: second o rejected with interior_overlap against placement 19 (expected)
[tiles] overlap fixture: entry count and next_id unchanged by the rejection
[tiles] bootstrap complete: 20 authoritative placements available to draw
[tiles] selection: palette entry 0 (prototile id 1), orientation 0 of 1
[tiles] proposals (initial): full-edge 512 attempted / 28 succeeded, vertex 512 attempted / 243 succeeded, 168 distinct after collapsing 103 duplicate proofs, 45 dropped as point-only contact, 123 offered
[tiles] active proposal: index 4 of 123, handle (36.0, 40.0), cursor (0.0, 0.0)
[tiles] first draw: 20 footprints projected, selection preview projected, active ghost projected
=== exit: 0 ===
```

byte-identical to the act-0 baseline: same bootstrap, same proposal counts, same
draw, no new diagnostic, no error, no warning. no visible window was launched.

this act added no runtime diagnostic. nothing logs snapshots, supply counts,
successful or missing-id removal, empty-history undo, or copied state sizes.
typed results and native tests are the whole observation surface.

## notes for the next act

- `Session` is ready for act 2 but nothing consumes it yet. `src/game/Editor`
  still owns a `State` directly, which is exactly the migration act 2 owns;
- `undo_depth()` is deliberately public observation. an interface can render
  "undo available" from it without a second flag;
- `supply_status()` is the palette view's read. it returns
  `std::optional<SupplyStatus>` by value, so a renderer holds no reference into
  state across a mutation;
- **nothing derived from a state may outlive a mutation or an undo.** undo
  rewinds `next_id()`, so a `PlacementId` cached by a hit test, proposal list,
  or selection can silently come to name a different placement. act 2's
  presentation state must be rebuilt after every successful `Session::apply` and
  every `undo()` returning true;
- history is unbounded and every snapshot is a full `State` copy including the
  level. for jam-sized levels this is trivially cheap; if a level ever grows
  large enough for it to matter, the cheap fix is sharing the immutable `Level`
  behind the snapshots rather than bounding history.
