# make an editor act 0-1: reversible play session

## implementor context seed

this act adds exact placement deletion, derived supply observation, and
single-branch undo. it builds on the completed act-0 `Level` and region-aware
`State`; it does not add Godot input or move history into geometry values.

seed the implementation session from the repository. this document fixes the
act-0-1 contract but does not replace the domain definitions, coordinate rules,
roadmap, completed act-0 contract, or earlier exact-state contracts.

before editing:

1. run `git status --short` and preserve every pre-existing user change;
2. enumerate authored files with `rg --files`, excluding generated build output,
   `Godot.app/`, and `vendor/`;
3. read this document completely;
4. read `README.md`, `DEFINITIONS.md`, `COORDINATES.md`,
   `docs/make-an-editor/MAKE-AN-EDITOR-ROADMAP.md`, and
   `docs/make-an-editor/MAKE-AN-EDITOR-ACT-0-IMPLEMENTATION.md` completely;
5. read
   `docs/make-an-editor/MAKE-AN-EDITOR-ACT-0-IMPLEMENTATION-NOTES.md`
   completely;
6. read the archived place-tiles implementation notes beneath
   `docs/place-tiles/`, especially acts 1, 3, 3-1, and 4-1;
7. read every authored file beneath `src/core/`, `src/engine/`, and `tests/`,
   paying particular attention to:
   - `src/core/Result.h`;
   - `src/core/Arrangement.{h,cpp}`;
   - `src/core/Placement.{h,cpp}`;
   - `src/core/Region.{h,cpp}`;
   - `src/engine/Commands.h`;
   - `src/engine/Level.{h,cpp}`;
   - `src/engine/Palette.{h,cpp}`;
   - `src/engine/State.{h,cpp}`;
   - `src/engine/Supply.h`;
   - `tests/test_arrangement.cpp`;
   - `tests/test_commands.cpp`;
   - `tests/test_state.cpp`;
   - `tests/test_level.cpp`; and
   - `tests/TestHarness.{h,cpp}`;
8. inspect `src/game/Editor.{h,cpp}` only to preserve its existing build and
   headless behavior;
9. inspect `SConstruct`; and
10. establish the completed act-0 baseline by running:

   ```text
   scons tests
   ./build/tests/tiles_tests
   scons
   ./Godot.app/Contents/MacOS/Godot --headless --path godot-project --quit-after 2
   ```

if the act-0 source or notes are absent, native tests fail, the GDExtension does
not load, or the worktree indicates that act 0 is still being implemented, stop
and ask carson rather than guessing at a moving interface.

after implementation, record public api decisions, state-history behavior,
changed tests, exact final totals, build results, and complete headless output
in:

```text
docs/make-an-editor/MAKE-AN-EDITOR-ACT-0-1-IMPLEMENTATION-NOTES.md
```

do not commit. do not mention this implementation document in code comments or
runtime-facing documentation.

do not launch a visible Godot window. this act has no visual acceptance. native
tests, compilation, and headless execution are the implementor’s verification
surface.

## goal

provide one reversible play-state boundary:

```haskell
data Session = Session
  { currentState :: State
  , undoHistory  :: [State]
  }
```

the mutation path becomes:

```text
typed command
     │
     ▼
copy current State
     │
     ▼
apply to candidate copy
     │
     ├── failure ──▶ discard candidate; preserve current + history
     │
     └── success ──▶ push prior current; install candidate
```

placement deletion is an exact mutation by stable `PlacementId`. undo restores
the complete prior `State`, including arrangement entries, identities,
allocator state, derived supply use, region, palette, and completion.

redo is absent. history is session state, not arrangement geometry and not part
of a serialized level.

## layer ownership

the three responsibilities are:

```text
tiles::Arrangement
    remove one exact stored placement identity

tiles::engine::State
    expose deletion as a typed engine command

tiles::engine::Session
    make successful State mutations undoable
```

do not place an undo stack inside `Arrangement` or `State`.

`Arrangement` is the mathematical placement collection and id allocator.
`State` is one current legal level arrangement. `Session` is the temporal play
interaction around successive states.

this separation prevents recursive snapshots:

```text
bad:
State contains history<State containing history<State ...>>

required:
Session contains current State + vector<State>
```

all new production code remains independent of Godot.

## exact arrangement removal

add:

```cpp
enum class RemovalError {
    placement_not_found,
};

Result<PlacementId, RemovalError> Arrangement::try_remove(
    PlacementId p_placement);
```

the operation searches `entries_` in storage order for the exact id.

on failure:

- return `RemovalError::placement_not_found`;
- preserve every entry and its order;
- preserve every placement value;
- preserve `next_id_`;
- preserve `exhausted_`; and
- perform no partial mutation.

on success:

- erase exactly the matching entry;
- return the same `PlacementId`;
- preserve the relative storage order of every surviving entry;
- preserve every surviving identity and placement;
- preserve `next_id_` unchanged; and
- preserve `exhausted_` unchanged.

placement identities are never reused merely because an entry was deleted.
given ids `0, 1, 2`, removing `1` and inserting another placement yields id `3`.

an exhausted arrangement remains exhausted after deletion. deletion does not
create another representable identity.

removal cannot violate the arrangement invariant: a subset of a pairwise
interior-disjoint collection remains pairwise interior-disjoint. it may make
the coverage disconnected or remove an anchor named by a future command; both
are legal consequences.

do not:

- remove by storage index;
- remove by polygon, translation, prototile identity, pointer, or geometry;
- renumber survivors;
- compact or rewind the allocator;
- return the removed `Placement` as an ownership channel;
- treat a missing id as success; or
- add adjacency bookkeeping.

## typed engine deletion

add to `Commands.h`:

```cpp
struct RemoveCommand final {
    PlacementId placement;
};

using RemoveCommandError = RemovalError;
```

add:

```cpp
Result<PlacementId, RemoveCommandError> State::apply(
    const RemoveCommand &p_command);
```

it delegates directly to `arrangement_.try_remove`.

deletion performs no:

- palette lookup;
- orientation lookup;
- supply availability check;
- region containment check;
- overlap check;
- completion special case; or
- id allocation.

those facts are either irrelevant to removal or derived after the arrangement
changes.

the returned success id and exact removal error remain typed. do not flatten
them into a boolean, string, log line, or unrelated command-error variant.

`State::solved()` remains a read-only derived query. removing any positive-area
placement from a previously solved state necessarily makes it unsolved; do not
store or toggle a separate completion flag.

## derived supply status

add one read-only value:

```cpp
struct SupplyStatus final {
    Supply::Amount used;
    std::optional<Supply::Amount> remaining;
};
```

and:

```cpp
std::optional<SupplyStatus> State::supply_status(
    PaletteEntryIndex p_entry) const;
```

an out-of-range palette entry returns `std::nullopt`. no other failure is
possible for a constructible state.

for a valid entry:

```text
used
    = number of arrangement entries whose exact PrototileId
      equals the palette entry's PrototileId

remaining
    = empty                       when supply is unlimited
    = finite capacity minus used when supply is finite
```

`remaining == std::nullopt` means unlimited, not unknown and not zero.

all orientations of one prototile consume the same supply. geometry equality,
pointer identity, palette index coincidence, and orientation do not affect the
count.

because act 0 prevents arbitrary foreign/preloaded arrangement entries and
every insertion enforces capacity, finite `used <= capacity` is a state
invariant. subtraction cannot underflow.

use `Supply::Amount` for both counts. do not use a signed presentation integer.

refactor candidate supply enforcement to use the same counting logic as
`supply_status`; there must not be one count for command legality and another
for the palette view. a finite candidate is available exactly when
`remaining > 0`.

do not store, increment, decrement, serialize, or independently undo a remaining
supply counter.

## session value

add:

```text
src/engine/Session.h
src/engine/Session.cpp
```

with the conceptual public surface:

```cpp
class Session final {
public:
    explicit Session(State p_initial);

    const State &state() const;

    bool can_undo() const;
    std::size_t undo_depth() const;
    bool undo();

    Result<PlacementId, PlaceCommandError> apply(
        const PlaceCommand &p_command);

    Result<PlacementId, MateCommandError> apply(
        const MateFullEdgesCommand &p_command);

    Result<PlacementId, MateCommandError> apply(
        const MateVerticesCommand &p_command);

    Result<PlacementId, RemoveCommandError> apply(
        const RemoveCommand &p_command);
};
```

store:

```cpp
State state_;
std::vector<State> undo_history_;
```

the initial state is owned by value and the initial history is empty.

`state()` is the only state accessor and is const. `Session` exposes no mutable
`State`, `Arrangement`, `Level`, `Palette`, `Region`, or history view.

`undo_depth()` is observation for tests and later interface state. it is the
exact vector size and is never a separately maintained counter.

`can_undo()` is exactly `undo_depth() != 0`.

do not add a command variant merely to unify return types. preserve the existing
overload-specific error types.

## successful mutation algorithm

every `Session::apply` overload uses the same algorithm:

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

equivalent factoring through one private template is encouraged. if the helper
is a template, define it where c++17 instantiation is valid and do not leak it
as public api.

the command is applied to the candidate copy, never to the live `state_`.
therefore a failed command preserves the live state even if a future state
implementation accidentally performs work before reporting failure. the
completed `State` operations remain independently transactional; the session
copy is an additional ownership guarantee, not permission to weaken them.

push history only after success is known. allocation failure follows the
repository’s existing no-exception process behavior; do not add partial
exception recovery to this act.

the installed candidate must be the exact state which produced the returned
success. do not apply the command a second time after pushing history.

every successful placement or deletion adds exactly one snapshot. no failed
mutation, preview, read-only query, selection change, or draw action adds
history.

## undo behavior

`undo()` is:

```text
if history is empty:
    return false
    change nothing

otherwise:
    replace current state with the newest snapshot
    remove that snapshot from history
    return true
```

“nothing to undo” is ordinary interface state, not a domain error. do not add a
fake `UndoError` solely to avoid returning `bool`.

undo restores the whole snapshot, including:

- exact `Level`;
- palette entry order, prototiles, orientations, and supplies;
- exact outer and hole polygons;
- arrangement entry order;
- every surviving and restored placement;
- every `PlacementId`;
- `next_id()` and exhaustion;
- derived used and remaining supply; and
- the result of `solved()`.

undo does not preserve the abandoned current state anywhere. there is no redo
stack.

after undo, a new successful mutation pushes the restored state normally. the
discarded future remains inaccessible.

rewinding `next_id()` through undo is intentional. undoing a placement restores
the pre-placement allocator, so a later successful placement may receive that
same id. no game-side pointer, cached proposal, or hit-test result may survive a
state mutation or undo; act 2 will rebuild derived presentation state.

deleting a placement does not itself change `next_id()`, so undoing a deletion
restores the deleted entry while retaining the same allocator value it had
immediately before deletion.

history is unbounded for this milestone. do not add a cap, persistence, command
compression, checkpointing, or memory policy.

## previews and observation

do not wrap state previews as mutations.

callers may use:

```cpp
session.state().preview(command)
```

all previews remain const and create no snapshot.

likewise, calls to:

- `state()`;
- `can_undo()`;
- `undo_depth()`;
- `supply_status()`;
- `solved()`;
- `level()`;
- `palette()`;
- `region()`; and
- `arrangement()`

must not change state or history.

do not add mutable convenience access merely because the current Godot editor
still owns a `State` directly. migration of the actual player to `Session`
belongs to act 2.

## state and identity examples

these sequences are normative:

```text
initial
  entries = []
  next_id = 0
  history = []

place a
  entries = [a:0]
  next_id = 1
  history = [initial]

place b
  entries = [a:0, b:1]
  next_id = 2
  history = [initial, after-a]

remove a
  entries = [b:1]
  next_id = 2
  history = [initial, after-a, after-b]

undo
  entries = [a:0, b:1]
  next_id = 2
  history = [initial, after-a]

undo
  entries = [a:0]
  next_id = 1
  history = [initial]

place c
  entries = [a:0, c:1]
  next_id = 2
  history = [initial, after-a]
```

the last placement reuses id `1` because the placement which originally
consumed it was undone, not merely deleted.

## native tests

add:

```text
tests/test_session.cpp
```

update:

```text
tests/test_arrangement.cpp
tests/test_commands.cpp
tests/test_state.cpp
```

reuse act-0 level and region fixtures where practical. do not weaken unrelated
completed tests.

### arrangement removal coverage

test:

- removing from an empty arrangement reports `placement_not_found`;
- removing an unknown id from a nonempty arrangement reports the same error;
- both failures preserve complete entries, order, placements, `next_id`, and
  exhaustion;
- removing the first, middle, and last stored entry succeeds;
- success returns the exact requested id;
- survivor order and values are preserved;
- deletion leaves `next_id` unchanged;
- insertion after deletion allocates the next fresh id rather than filling the
  gap;
- deleting the final entry leaves an empty but allocation-continuing
  arrangement;
- deleting from an exhausted arrangement leaves it exhausted; and
- removal does not alter legal contact or overlap invariants among survivors.

### state deletion coverage

test:

- `RemoveCommand` deletes exactly one existing id;
- missing-id failure remains typed and transactional;
- deletion does not alter the level, palette, region, or other placements;
- deletion restores finite supply availability by derived counting;
- deletion changes unlimited `used` but keeps `remaining` empty;
- deletion can make a solved state unsolved;
- deletion of any storage position preserves survivor order; and
- subsequent place and mating commands may use a surviving placement id as
  anchor but report the existing `anchor_not_found` error for a deleted id.

### supply-status coverage

test:

- out-of-range entry returns no status;
- unused finite supply reports `used = 0` and full remaining amount;
- unused unlimited supply reports `used = 0` and no remaining amount;
- placements across distinct orientations count against one entry;
- other prototile identities do not count;
- finite remaining reaches exactly zero;
- the same zero causes command resolution to report `supply_exhausted`;
- deletion increases finite remaining exactly once;
- maximum finite `UINT64_MAX` retains safe counting and subtraction; and
- status queries are observationally pure.

### session coverage

test:

- initial state is exact and history is empty;
- `can_undo`, `undo_depth`, and `undo` agree on empty history;
- successful exact placement adds one snapshot;
- successful full-edge mating adds one snapshot;
- successful vertex mating adds one snapshot;
- successful deletion adds one snapshot;
- every command result and error alternative is preserved unchanged through
  `Session`;
- failed candidate resolution adds no snapshot;
- failed placement construction adds no snapshot;
- failed outside-region placement adds no snapshot;
- failed overlap insertion adds no snapshot;
- failed mating adds no snapshot;
- failed deletion adds no snapshot;
- a failed mutation while history is nonempty does not add a hidden extra undo;
- preview success and preview failure add no snapshot;
- one undo restores complete state and decreases depth by one;
- repeated undo walks back place/place, place/mate, place/delete, and mixed
  sequences in exact reverse success order;
- an extra undo after reaching the initial state returns false and changes
  nothing;
- undo placement rewinds `next_id`;
- a new placement after that undo may reuse the undone id;
- ordinary deletion does not rewind `next_id`;
- undo deletion restores the original placement id and order;
- finite supply exhaustion, deletion, and undo all report the derived expected
  counts;
- undoing the final placement of a solved no-hole level makes it unsolved;
- undoing deletion from a solved holed level makes it solved again;
- a new mutation after undo creates the expected single branch with no redo
  behavior; and
- copied snapshot geometry remains exact at fractional q16.48 translations.

for failure/history tests, observe `undo_depth()` and then undo to the initial
state. checking only current arrangement size is insufficient: an erroneous
hidden snapshot could preserve the same current state while changing future
undo behavior.

### complete-state equivalence

when asserting restoration, compare every observable component:

- level palette order;
- each palette prototile id, supply, distinct orientation sequence, and exact
  canonical polygons;
- region outer boundary, ordered holes, and doubled area;
- arrangement entry count and order;
- each placement id, prototile id, representative orientation, translation,
  oriented polygon, and footprint;
- `next_id()` including exhaustion;
- every palette entry’s supply status; and
- `solved()`.

do not add `operator==` to domain types merely to shorten tests if equality
would imply semantics the project has deliberately kept named or absent. a
test-only comparison helper is appropriate.

## source and build layout

the expected new files are:

```text
src/engine/Session.h
src/engine/Session.cpp
tests/test_session.cpp
docs/make-an-editor/MAKE-AN-EDITOR-ACT-0-1-IMPLEMENTATION-NOTES.md
```

expected modifications are confined principally to:

```text
src/core/Arrangement.h
src/core/Arrangement.cpp
src/engine/Commands.h
src/engine/State.h
src/engine/State.cpp
tests/test_arrangement.cpp
tests/test_commands.cpp
tests/test_state.cpp
```

small test-fixture changes elsewhere are permitted when directly required to
exercise solved-state restoration.

do not change `SConstruct`: recursive native-test and extension discovery
already include new `.cpp` files.

do not change GDExtension registration, scenes, project settings,
`src/game/Editor.{h,cpp}`, rendering, or input.

## diagnostics

this act needs no new runtime diagnostic. deletion and undo are not yet wired to
Godot input.

do not log:

- every snapshot;
- every supply count;
- successful removal;
- missing-id removal;
- empty-history undo; or
- copied state sizes.

typed results and native tests are the observation surface.

retain all existing headless diagnostics unchanged.

## verification

run and record:

```text
scons tests
./build/tests/tiles_tests
scons
./Godot.app/Contents/MacOS/Godot --headless --path godot-project --quit-after 2
```

grep-verify that no Godot include, type, resource name, or identifier entered
`src/core/` or `src/engine/`.

the native suite must demonstrate exact state restoration, not merely
arrangement-size changes.

the GDExtension must build without requiring any game-layer migration to
`Session`.

the headless run must preserve the completed act-0 bootstrap, proposal
enumeration, and draw behavior with no unexpected error or warning.

## acceptance

act 0-1 is complete when all of the following hold:

- `Arrangement::try_remove` removes exactly one stable id;
- missing-id removal is typed and fully transactional;
- deletion preserves survivor order and values;
- deletion never rewinds or repairs the id allocator;
- exhausted arrangements remain exhausted after deletion;
- `RemoveCommand` is the engine deletion surface;
- state deletion bypasses irrelevant palette, region, overlap, and allocation
  checks;
- finite and unlimited supply status is derived from the arrangement;
- supply enforcement and supply presentation share one count;
- deletion immediately restores finite availability without a mutable counter;
- `Session` owns one current `State` and a private stack of prior `State`
  snapshots;
- the initial session has no undo;
- every successful placement, mating, or deletion adds exactly one snapshot;
- every failed command adds none and preserves current state;
- previews and observation add no history;
- undo restores the newest exact prior state and removes that snapshot;
- undo restores ids, allocator, order, geometry, supply, region, palette, and
  solved status;
- undo on empty history is a non-mutating `false`;
- there is no redo state or api;
- state snapshots do not recursively contain history;
- no mutable state or history view escapes `Session`;
- all new code remains Godot-free;
- all native tests pass;
- the GDExtension builds cleanly;
- completed headless behavior remains intact; and
- implementation notes record exact totals and verification.

## explicitly out of scope

act 0-1 does not implement:

- redo;
- reset or restart shortcuts;
- bounded history or history compression;
- command serialization;
- history persistence in a level resource or save game;
- authoring history;
- undoing palette or region edits;
- pointer hit testing;
- selecting a placement;
- mouse, keyboard, controller, or touch bindings;
- visual deletion affordances;
- palette rendering;
- remaining-supply text;
- completion presentation;
- moving, dragging, replacing, or transforming an existing placement;
- reusing ids after ordinary deletion;
- adjacency, connectivity, or structural-support rules;
- deletion preview;
- a general command bus, event bus, observer, callback, or signal system;
- Godot resources or scenes;
- migration of the current debug `Editor` to `Session`;
- tier-2 or tier-3 geometry; or
- changes to exact placement, overlap, region, or solved semantics.

if implementing snapshots reveals that completed `State` is not safely copyable
as a value, stop and report the exact non-value member and ownership conflict.
do not replace snapshots with ad hoc inverse commands, mutable shared state, or
serialization round trips without a new decision from carson.
