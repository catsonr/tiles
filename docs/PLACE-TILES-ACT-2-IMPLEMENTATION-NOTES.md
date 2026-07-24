# place tiles act 2 — implementation notes

Notes from implementing `docs/PLACE-TILES-ACT-2-IMPLEMENTATION.md`: finite exact
quarter-turn orientations, orientation-bearing placements, and transactional
vertex joins over the act 0/1 kernel. Everything below is verified against the
built test executable, a default GDExtension build, and a headless load.

## verification

- `scons tests` builds `build/tests/tiles_core_tests` from `src/core/**` +
  `tests/**` with the plain toolchain env (no godot-cpp). It reports
  **1151 checks, 0 failed across 119 cases** (act 1's 597/77 plus the act 2
  additions).
- `scons` still builds the extension; all core `.cpp` files — including the four
  new ones — compile into the shared library under godot-cpp's flags
  (`-std=c++17 -fno-exceptions`) with no warnings or errors.
- Headless run prints `[tiles] gdextension smoke test ready` with no
  load/symbol/registration errors, and no error/warning lines at all.
- No `SConstruct` edit was needed: the extension glob (`src/**`) and the test
  glob (`src/core` + `tests`) discover the new files automatically.
- No new core value type (`Orientation`, `OrientedPrototile`, `VertexIndex`,
  the placement/orientation errors) is registered with Godot; the disposable
  smoke node is untouched.

## layout

New core files, following the plan's sketch:

```
src/core/Orientation.{h,cpp}
src/core/OrientedPrototile.{h,cpp}
src/core/geometry/Rotation.{h,cpp}
```

Modified: `Coordinate` (added `checked_negate`), `Polygon` (added
`same_boundary` and the shared `translation_normalize`), `Prototile`,
`Placement`, `Alignment`, `Arrangement`. New tests:
`test_orientation.cpp`, `test_lattice_orientation.cpp`, `test_tetromino.cpp`,
`test_vertex_join.cpp`, `test_squared_square.cpp`; updated:
`test_placement.cpp`, `test_join.cpp`, `test_arrangement.cpp`, `TestHarness.h`.

## decisions worth recording

- **`Rotation` is decoupled from `Orientation`.** The transform kernel takes a
  plain `QuarterTurn { zero, quarter, half, three_quarter }` enum, so
  `geometry/` never depends on the `Orientation` domain value. The compiler owns
  the `Orientation -> QuarterTurn` mapping (`quarter_turn_of`), which is also the
  single gate that rejects non-quarter angles.
- **One checked normalization path.** `translation_normalize(const Polygon&)`
  lives in `Polygon.{h,cpp}` and is shared verbatim by `Prototile::make` and the
  orientation compiler. `Prototile::make`'s public behavior and
  `normalization_overflow` error contract are preserved exactly; the checked
  subtraction is no longer duplicated, and no second `Prototile` is fabricated to
  reuse a factory. The rebuild `assert`s success because translation preserves
  simplicity, the lex-min, winding, and triangulation topology.
- **`same_boundary` is a named free function, not `operator==`.** Polygon
  identity is *only* the canonical vertex sequence (triangulation excluded). It
  is deliberately not spelled `operator==`, so it can never be mistaken for
  prototile identity or geometric congruence, which remain distinct relations.
- **`Orientation` stores canonical `step/order` in lowest terms.** `make`
  rejects order 0, reduces step mod order, canonicalizes every whole turn to
  `0/1`, else divides by gcd. Equality is componentwise (safe because the form is
  canonical); ordering is an exact `uint64` cross-product of the two `uint32`
  components, never floating point. `reference/quarter/half/three_quarter` are
  `constexpr` constants in the same canonical form. The general `step/order`
  representation is intentional headroom for the >4-turn systems of act 3.
- **`checked_negate` added to `Coordinate`.** Negating `INT64_MIN` returns
  `ArithmeticError::overflow`; every rotation negation routes through it.
- **The compiler groups by `same_boundary` and stays deterministic.** Requested
  angles are sorted and de-duplicated first, so scanning ascending makes the
  first unused member of each boundary group its least admitted angle (the
  representative), the groups emerge in representative angular order, and each
  group's `equivalent_orientations` is sorted ascending. Every orientation is
  compiled **directly from the reference polygon** — never from another compiled
  result — as the plan and `COORDINATES.md` require.
- **`Placement` owns an `OrientedPrototile`.** It exposes `prototile()` (the
  underlying reference prototile), `orientation()` (representative),
  `oriented_polygon()` (local canonical oriented geometry), `translation()`, and
  `footprint()`. There is no factory overload that silently supplies the
  reference orientation; callers must compile it explicitly. The stored reference
  and oriented polygons stay local and untranslated.
- **`Arrangement` join surface.** `try_join` is renamed `try_join_full_edges`
  (now taking an `OrientedPrototile`), and `try_join_vertices` is added; there is
  no ambiguous `try_join` alias. Both resolve the anchor, run the pure alignment,
  and delegate the whole-footprint proof and the *only* state mutation to
  `try_insert`, so a failed join of either kind is byte-for-byte identity on
  entries, ordering, ids, and the allocator. `JoinErrorCode` gains
  `anchor_vertex_out_of_range` and `candidate_vertex_out_of_range`; lower-level
  typed errors are translated without logging or discarding meaning, and
  `conflicting_placement` remains populated only for `interior_overlap`.

## reachable and unreachable error paths

Reachable and positively tested:

- `OrientationError::zero_order`.
- `LatticeOrientationErrorCode::{empty_orientation_set, unsupported_orientation,
  normalization_overflow}`.
- `LatticeOrientationErrorCode::coordinate_overflow` for the **quarter** and
  **half** turns (both negate `y`), provoked by a normalized prototile with a
  vertex at `y == INT64_MIN`.
- `VertexAlignmentError::{anchor_vertex_out_of_range,
  candidate_vertex_out_of_range, translation_overflow, footprint_overflow}` and
  the corresponding `JoinErrorCode` translations.

Structurally unreachable, kept as typed backstops (honest gaps, in the same
spirit as act 0's `zero_area`/`triangulation_failed` and act 1's low-x note):

- **`coordinate_overflow` from an `x` negation.** The three-quarter turn
  `(y, -x)` and the half turn's `x` negation can only overflow if some `x ==
  INT64_MIN`, but every normalized polygon has `x >= 0`. It is therefore
  unreachable and has no positive test; the three-quarter case is instead shown
  to compile cleanly on a plain fixture. (Note: three-quarter *can* still fail on
  a `y == INT64_MIN` prototile, but via `normalization_overflow` — the `y` value
  lands in the `x` slot and then cannot be normalized — not via a negation
  overflow.)
- **`LatticeOrientationErrorCode::polygon_construction_failed`.** After a
  representable coordinate transform, rotating a valid simple polygon yields a
  valid simple polygon, so `Polygon::make` on the rotated ring does not fail.
  Surfaced as a typed error (never asserted away) so a future non-rigid transform
  cannot silently publish broken geometry, but there is no reachable public input
  and thus no positive test.
- **`PlacementError::footprint_construction_failed`** and
  **`VertexAlignmentError::footprint_construction_failed`** — unchanged from act 1
  reasoning: exact translation of a valid polygon reconstructs successfully.
- **The vertex-align verify mismatch.** `align_vertex` derives `t = a - b`
  componentwise with checked subtraction, so `b + t == a` holds exactly and the
  checked add of the representable value `a` never overflows. The mismatch branch
  cannot fire; it is folded into `translation_overflow` and documented inline.
- **Low-x footprint overflow.** Still unreachable, and now shown to hold for
  *every* orientation, not just the reference: `translation_normalize` fixes
  minimum `x == 0` for each compiled orientation, so `INT64_MIN` x-translation
  succeeds even for a quarter-turned fixture (asserted in `test_placement.cpp`).

## no reflection

There is no reflection flag, transform, compiler branch, or join option. All four
quarter turns have positive orientation; winding normalization only reverses a
submitted clockwise ring into canonical counterclockwise storage — a
representation change, not geometry. The tetromino tests assert that **no `l`
orientation equals any `j` orientation and no `s` equals any `z`**, confirming
rotations of one prototile never cross chirality.

## a note for the next formalizer

- **The "mixed-orientation full-edge join chain" test uses the square**, whose
  four quarter turns are geometrically identical; it therefore mixes orientation
  *labels* over identical canonical geometry and proves exact (drift-free)
  coordinates regardless of the label. A full-edge chain that alternates
  *geometrically distinct* orientations is awkward because rotation swaps edge
  lengths, so mating a fixed-direction anchor edge forces a same-direction
  candidate edge and cannot freely re-orient a non-square tile step to step. The
  genuine geometric value of rotation in full-edge joining is instead covered by
  the rectangle "incompatible in reference, succeeds after a quarter turn" test
  (plus the tetromino compilation and vertex-join fixtures). If a formalizer wants
  a full-edge chain that provably alternates distinct orientations, it likely
  needs a prototile with equal-length perpendicular edges that are *not*
  rotationally symmetric — that is a fixture/spec choice, flagged here.
- `polygon_construction_failed` in the compiler remains an untestable invariant
  backstop (above). Making it reachable would require admitting geometry that
  breaks under a representable transform — a spec decision, not an implementation
  one.

## not done (correctly out of scope)

No tier-2 uniform modules or regular-polygon factories, no 45/30/60-degree or
arbitrary rotation, no new floating-point or trig logic, no reflection or
scaling, no vertex-to-edge or automatic joins, no stable authored feature
identities across orientation, no palettes/supplies/targets/regions/solution
checking, and no Godot projection, rendering, or editor surface. The
`quantize_double` model-entry path is unchanged.

## direction: how an engine should talk to this core

This section is a pointer, not a decision. It is written at the implementor's
request:

> "youre well versed now, so please append to your notes a section on how you
> feel an engine should communicate with the existing core. youre not like,
> making any decisions. rather, point us in the right direction."

The intended topology is `game -> engine -> core`, where the core is a pure
geometric prover the game never touches directly. Everything below is how the
existing act-2 surface *wants* to be driven; the formalizer should treat it as
input, and it lines up with the roadmap's open question about "the exact
read-only contract between the core and renderer."

- **The seam is a one-way airlock, and that is the load-bearing rule.** The core
  is exact q16.48 (`Coordinate::raw()` is an `int64`, value = `raw / 2^48`); it
  is cartesian **y-up, CCW**. The engine converts *out* to float for Godot
  (mind the y-down flip) and must **never** let a rendered/float coordinate flow
  back into a `Coordinate`. Intents from the game arrive as integer lattice
  quantities or as feature selections (ids/indices), never as reconstructed
  render coordinates. The instant a float round-trips into the model, the
  "no drift / exact shared vertices" guarantee is gone. `quantize_double` is the
  *only* sanctioned float-to-lattice entry, and it is for authoring source
  geometry, not for feeding back rendered state.

- **Commands in, a read-only view out.** The core already separates these
  cleanly: the mutating verbs are `Arrangement::{try_insert, try_join_full_edges,
  try_join_vertices}`; the query surface is `entries()` (a
  `const std::vector<Entry>&`) plus the per-`Placement` readers
  (`footprint()`, `oriented_polygon()`, `prototile()`, `orientation()`,
  `translation()`) and `next_id()`. The engine's shape should mirror this: accept
  an intent, call one verb, translate the returned `Result`/typed error into
  whatever the game understands. The core never throws, never logs, and holds no
  global mutable state — an `Arrangement` is a plain owned value — so it is
  deterministic and safe to drive off-thread as long as one owner touches one
  arrangement at a time (a "compute a snapshot, hand it to the renderer" loop
  fits naturally).

- **`PlacementId` is the stable handle; the `entries()` index is not.** An
  arrangement is mathematically unordered, and storage order is not meaning. The
  engine's render nodes should key off `PlacementId` (monotonic, allocated only
  on successful insert), not off position in the vector, so it stays correct if a
  future act reorders or removes entries.

- **Geometry is immutable per placement, so cache aggressively.** A `Placement`
  never mutates after insertion; its footprint vertices and triangulation are
  fixed. A render cache keyed by `PlacementId` therefore needs no per-frame
  invalidation — it only grows as inserts succeed. (Removal does not exist in the
  core yet; see below.)

- **The engine owns everything the core deliberately omits.** Concretely: the
  prototile catalog and `PrototileId` assignment (the core does *not* dedupe or
  register ids); the compiled-orientation catalog — call
  `compile_lattice_orientations` **once** per prototile and cache the returned
  variant vector, since its length is exactly the number of distinct rotations
  and indexing it is the natural "rotate" action; and all policy the domain
  defines but this act does not enforce — palette, supply, rotation rule, targets.
  Serialization and any spatial/hit-test index are likewise engine concerns.

- **What the seam will soon want, for the formalizer to scope.** Three gaps are
  worth naming now: (1) there is **no remove/undo** in the core — an editor will
  need it, and it interacts with id stability and the disjointness invariant;
  (2) there is **no public "which placement is under this point" query** — the
  exact predicates exist internally (`interiors_overlap`, the segment/orientation
  kernel) but nothing is exposed for picking; (3) there is **no explicit
  render-view DTO** — today the engine reads live `Polygon` references, which is
  fine in-process but couples the renderer to a core type. Whether the seam
  should hand out a plain snapshot struct instead is exactly the roadmap's open
  read-only-contract question, and it is an engine/formalizer call, not an
  implementor one.
