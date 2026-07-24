# place tiles act 3 — implementation notes

Notes from implementing `docs/PLACE-TILES-ACT-3-IMPLEMENTATION.md`: the
dependency-free `tiles::engine` layer and its first runtime aggregate — `Supply`,
`PaletteEntry`, `Palette`, `State`, and the handcrafted `make_tetromino_state`.
Everything below is verified against the built test executable, a default
GDExtension build, and a headless load.

## verification

- `scons tests` now builds `build/tests/tiles_tests` (renamed from
  `tiles_core_tests`) from `src/core/**` + `src/engine/**` + `tests/**` with the
  plain toolchain env (no godot-cpp). It reports **1361 checks, 0 failed across
  144 cases** — the act-2 baseline of 1151/119 plus **210 checks / 25 cases** of
  new engine coverage. Every pre-existing core case still passes unchanged.
- `scons` still builds the extension; the three new engine `.cpp` files compile
  into the shared library under godot-cpp's flags (`-std=c++17 -fno-exceptions`)
  with no warnings or errors.
- Headless run (`Godot --headless --path godot-project --quit`) prints
  `[tiles] gdextension smoke test ready` with no load/symbol/registration errors
  and no warning lines. The extension still registers **only** the disposable
  smoke node; no engine value is registered with Godot. `register_types.cpp` and
  the smoke node were not touched.
- The acceptance snippet holds: `state.palette().order() == 7`,
  `entries()[1].orientations().size() == 2` (i), `arrangement().entries().size()
  == 0`, and the palette's first prototile polygon vertices are reachable as
  exact `tiles::Point`s.

## layout

New engine files, following the plan's sketch:

```
src/engine/Supply.h            (header-only value)
src/engine/Palette.{h,cpp}     (PaletteEntry + Palette)
src/engine/State.{h,cpp}
src/engine/TetrominoState.{h,cpp}
```

New tests: `tests/test_supply.cpp`, `tests/test_palette.cpp`,
`tests/test_state.cpp`, `tests/test_tetromino_state.cpp`. Modified:
`SConstruct` only. No core header, core `.cpp`, or existing test was changed —
the act-0/1/2 kernel is byte-for-byte untouched.

## decisions worth recording

- **`Supply` is header-only.** It is a tiny immutable value; the plan's source
  layout lists only `Supply.h`. Representation is a single
  `std::optional<Amount>`: empty means unlimited, a present value is always
  positive by construction. `finite_amount()` returns that optional directly, so
  the "exact for finite / empty for unlimited" contract is structural.
  `finite(0)` is the only rejection. Equality compares the optionals, so two
  unlimiteds are equal, unlimited never equals any finite, and finite equals
  finite iff the amounts match. The factories and friend operators are defined
  in-class and thus implicitly `inline` — no ODR issue across TUs. **Supply is a
  configured capacity, not a decrementing counter**; no remaining-piece count is
  stored anywhere.
- **`PaletteEntry::make` is a thin delegation to `compile_lattice_orientations`.**
  It performs zero orientation logic of its own — no sorting, rejection,
  rotation, canonicalization, or dedup — and on failure returns the exact
  `LatticeOrientationError` unchanged (no logging, no partial entry, no generic
  engine error). The entry stores its own `Prototile` copy alongside the compiled
  `OrientedPrototile`s (which each also value-own a prototile copy). This
  duplication is deliberate and matches the core's non-dangling ownership model;
  no shared ownership was introduced to remove the small copies.
- **`Palette` duplicate-id check is an O(n²) pairwise scan.** For a handcrafted
  palette (7 entries) this is clearer than an auxiliary `set` and stays a pure
  bookkeeping guard. It rejects repeated exact `PrototileId` **even when geometry
  and supply differ** (positively tested). It is explicitly *not* a congruence
  kernel: no geometric comparison across entries exists or was added. `order()`
  is `entries_.size()`, never a supply sum or a variant count — tested against a
  1-variant + 2-variant palette whose order is 2, not 3.
- **`State` members are non-`const` and it has no editor-specific name.** The
  act-3 surface is read-only (both accessors return `const&`, verified by a
  `static_assert`), but the members stay mutable-capable so act 3-1 can add one
  typed mutation API without first stripping `const`. No forwarding ops,
  selection state, held-tile state, revision counters, or callbacks were added.
  Construction cannot fail — `Palette` and `Arrangement` are already valid by
  construction.
- **`TetrominoStateError` is a code + populated-optionals struct.** It carries the
  `TetrominoStateStage` (`polygon | prototile | palette_entry | palette`), the
  affected `PrototileId` for per-piece stages (empty for the palette stage), and
  exactly one populated underlying-error optional (`PolygonError`,
  `PrototileError`, `LatticeOrientationError`, or `PaletteError`) matching the
  stage. The factory threads each checked core result and returns the first
  failure verbatim — it never asserts the content into existence, discards the
  underlying error, logs-and-continues, or publishes a partial palette. Small
  private `*_failure` builders keep the happy path readable.
- **Tetromino rings are authored identically to the act-2 fixtures.** Only
  direction-changing corners, one game unit = `Coordinate::SCALE` raw. `s`/`z`
  and `j`/`l` are distinct chiral prototiles (no reflection). The engine test
  re-runs `compile_lattice_orientations` on each stored `prototile()` and asserts
  representative angles, complete equivalent-orientation groups, and
  `same_boundary` all agree with the core compiler — proving aggregation, not a
  second rotation kernel. Per-entry distinct-orientation counts are
  `1,2,4,2,2,4,4` (total 19), matching the core tetromino tests.

## build integration

`SConstruct` changed in exactly one place: the native test discovery tree tuple
went from `("src/core", "tests")` to `("src/core", "src/engine", "tests")`, the
target was renamed `build/tests/tiles_core_tests -> build/tests/tiles_tests`, and
the surrounding comment now describes a "core-and-engine" suite. The `scons
tests` alias is unchanged. The **extension** build needed no edit: its recursive
`src/**` glob already discovers the new engine sources.

## error paths

Reachable and positively tested:

- `SupplyError::zero_finite_amount` (via `finite(0)`).
- `LatticeOrientationErrorCode::{empty_orientation_set, unsupported_orientation}`
  propagated unchanged through `PaletteEntry::make` (the unsupported case also
  preserves the offending `orientation`).
- `PaletteError::{empty, duplicate_prototile_id}`.

Structurally unreachable in `make_tetromino_state`, kept as honest typed
backstops (never asserted away):

- Every `TetrominoStateStage` failure branch. The authored constants are known
  good — the seven rings validate, normalize, compile the four quarter turns, and
  carry seven distinct ids — so no stage fails at runtime. The branches exist
  because every public core construction path is checked and the factory refuses
  to fabricate content past a failure. There is no reachable public input that
  trips them, hence no positive test for the failure struct itself; the
  successful assembly is exercised exhaustively instead. This mirrors act 2's
  treatment of `polygon_construction_failed` and the footprint backstops.

## out of scope (correctly absent)

No `tiles::game` file or Godot type, no rendering or `Point -> Vector2`
projection, no second polygon/coordinate/snapshot/DTO type, no state mutation
commands, no mutable palette/arrangement access, no selection / held-tile /
current-orientation state, no supply consumption or enforcement, no
remove/undo/redo/persistence, no regions/targets/rotation-rule/levels/solution
checking, no palette congruence search, no procedural generation, no serialized
loading, no scene navigation. `State` stores each entry's distinct admitted
orientations but selects none.

## a note for act 3-1

- **The mutation surface is `State`, and its members are already non-`const`.**
  Act 3-1 should add typed commands as `State` methods (or free functions taking
  `State&`) that call the core's mutating verbs
  (`Arrangement::{try_insert, try_join_full_edges, try_join_vertices}`) and
  translate the returned `Result`/typed error, keeping the arrangement otherwise
  inaccessible for arbitrary external mutation. The read-only accessors can stay;
  they need no change.
- **"Current orientation" is a per-entry vector index, not an angle.** Each
  `PaletteEntry::orientations()` is the deterministic distinct-variant vector
  (length 1/2/4 for the tetrominoes). Cycling is `(index + 1) % size()` over that
  vector — never over the raw `equivalent_orientations()` labels, and no sibling
  pointers or graph are needed. Selection/held-tile/current-index state itself
  belongs to act 3-1 (or later), not to the palette value.
- **`Supply` enforcement is derived, not stored.** When act 3-1 enforces finite
  supply, compute remaining pieces from the configured `Supply` and the
  authoritative `Arrangement` contents; do not add a decrementing counter to the
  entry.
- **`PaletteEntry` holds a redundant `Prototile` copy** (also inside each
  `OrientedPrototile`). If act 3-1 or a later act finds this wasteful at scale,
  that is a core-ownership question (shared ownership vs. an id-indexed catalog),
  flagged here — it was intentionally left as value-owned per the plan.
