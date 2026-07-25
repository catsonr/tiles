# place tiles act 1 — implementation notes

Notes from implementing `docs/PLACE-TILES-ACT-1-IMPLEMENTATION.md`: certified
joins over the act 0 prototile kernel. Everything below is verified against the
built test executable, a default GDExtension build, and a headless load.

## verification

- `scons tests` builds `build/tests/tiles_core_tests` from `src/core/**` +
  `tests/**` with the plain toolchain env. It reports **597 checks, 0 failed
  across 77 cases** (act 0's 169/27 plus the act 1 additions).
- `scons` still builds the extension; the four new core `.cpp` files compile
  into the shared library under godot-cpp's flags (`-std=c++17 -fno-exceptions`)
  with no warnings or errors.
- Headless run prints `[tiles] gdextension smoke test ready` with no
  load/symbol/registration errors (Godot 4.7.1, universal build).
- No SConstruct edit was needed: both the extension glob (`src/**`) and the test
  glob (`src/core` + `tests`) already discover new files automatically.

## layout

New core files, exactly as the plan sketched:

```
src/core/Placement.{h,cpp}
src/core/Arrangement.{h,cpp}
src/core/geometry/Intersection.{h,cpp}
src/core/geometry/Alignment.{h,cpp}
```

Tests: `tests/test_placement.cpp`, `tests/test_intersection.cpp`,
`tests/test_arrangement.cpp`, `tests/test_join.cpp`. No second numeric kernel;
everything reuses act 0's `Result`, checked `Coordinate`/`Point` arithmetic,
`Int256`, and the exact predicates. No Godot types entered the core.

## decisions worth recording

- **Triangle SAT projects raw coordinates, not differences.** For an edge
  `from → to` the axis is the left normal `(-e.y, e.x)` of `e = to - from`; the
  direction is irrelevant to interval separation. A vertex projects as
  `nx*x + ny*y`, accumulated in `Int256`. `nx`/`ny` are 65-bit edge differences
  and `x`/`y` are 64-bit, so a single product can reach ~2^127 and a two-term dot
  product ~2^128 — genuinely past signed 128-bit, which is why the projection
  lives in `Int256` and never in `__int128`. Separation uses the **non-strict**
  `max(a) <= min(b) || max(b) <= min(a)`, so closed triangles that touch on a
  boundary have disjoint open interiors.
- **Polygon overlap is AABB prefilter + triangle double loop.** The bounding-box
  test is weak (`<=`) on raw `int64` and can only reject: equality on an axis
  leaves at most a shared edge/point. Otherwise every triangle of one certified
  triangulation is compared with every triangle of the other, returning at the
  first overlapping pair. Exactness rests on act 0's triangulation covering the
  polygon with interior-disjoint triangles.
- **Placement rebuilds through `Polygon::make`**, mirroring act 0's prototile
  normalization: translate every canonical vertex with `checked_add`, then
  reconstruct. Because translation is order-preserving (it fixes the
  lexicographic minimum and winding and is equivariant with deterministic ear
  clipping), the footprint vertices and triangles are the canonical ones
  translated **once, in the same order** — the tests assert this elementwise,
  not just as a set. The stored prototile polygon stays at the local origin.
- **Alignment compares displacement vectors in `__int128`.** Compatibility is
  `a1 - a0 == b0 - b1` componentwise (endpoint differences may be 65-bit, so not
  `Coordinate` values). The translation `t = a0 - b1` and the verification
  `b1 + t == a0`, `b0 + t == a1` all go through checked `Coordinate` arithmetic;
  the verify step is redundant for a compatible, non-overflowing edge but is kept
  as the plan specifies.
- **Arrangement is transactional by delegating the only mutation to
  `try_insert`.** `try_join` resolves the anchor, runs `align_full_edge` (pure,
  no state), then `try_insert` (the single append point, which also carries the
  whole-footprint overlap proof). Any earlier failure returns before a mutation
  exists, so a failed join is byte-for-byte identity on entries, ordering, and
  the id allocator. Lower-level typed errors are translated, never logged or
  discarded; `conflicting_placement` is populated only for `interior_overlap`.
- **The id allocator is `next_id_` plus an `exhausted_` flag.** Id
  `UINT64_MAX` is a valid, allocatable id; exhaustion is the state reached
  *after* it is consumed. `next_id()` returns `nullopt` once exhausted. The
  test-only seam `Arrangement::testing_with_next_id` presets the allocator of an
  otherwise empty (always-valid) arrangement, so it can drive exhaustion in one
  step without fabricating an invalid public value and cannot create id reuse.

## a deliberately unreachable overflow direction

The plan's placement test list asks for footprint overflow "independently on all
four coordinate directions." Three are genuine and tested (`+x`, `+y`, `-y`).
**Low-x is structurally unreachable through the reference-orientation path** and
is documented rather than faked:

- A prototile polygon is translation-normalized so its canonical first (lex-min)
  vertex is the origin. Therefore its minimum x coordinate is exactly `0`.
- A footprint vertex is `vertex.x + t.x`. Underflow needs
  `t.x < INT64_MIN - vertex.x`, and since `vertex.x >= 0` the right side is
  `<= INT64_MIN` while `t.x >= INT64_MIN`. No representable translation can
  underflow x.

`test_placement.cpp` covers this with an explicit case asserting that a
maximally negative `t.x = INT64_MIN` **succeeds**, with the reasoning inline.
This is the same shape of honest gap act 0 flagged for `zero_area` /
`triangulation_failed`: the checked-arithmetic guard on the x path is correct and
present, but no public input reaches it. If a future act wants low-x overflow to
be a reachable, testable outcome, it would need placements built from geometry
that is not translation-normalized to a zero minimum — a spec decision, not an
implementation one. **Flagging for the formalizer.**

## other unreachable-through-the-happy-path guards

- `AlignmentError::footprint_construction_failed` and
  `PlacementError::footprint_construction_failed` are invariant backstops: exact
  translation of a valid polygon reconstructs successfully. They are surfaced as
  errors (never asserted away) so a future non-order-preserving transform cannot
  silently publish a broken footprint, but no current input triggers them.
- The alignment verify step can in principle return `incompatible_edges` or
  `translation_overflow` a second time; for any input that passed the vector
  compatibility check and produced a representable `t`, it never does. It is kept
  because the plan lists it as a required step.

## not done (correctly out of scope)

No arbitrary orientations, canonical rotated-polygon generation, palettes,
supplies, targets, regions, solution checking, Godot projection, or editor
commands. The disposable smoke-test node is untouched, and no act 1 value type —
`Placement`, `Arrangement`, `PlacementId`, `EdgeIndex`, the errors, or the
geometry helpers — is registered with Godot.
