# place tiles act 0 — implementation notes

Notes from implementing `docs/PLACE-TILES-ACT-0-IMPLEMENTATION.md`: the plain
c++ prototile kernel. Everything below is verified against the built test
executable and a headless GDExtension load.

## verification

- `scons tests` builds `build/tests/tiles_core_tests` from `src/core/**` +
  `tests/**` with a plain toolchain env (no godot-cpp). It reports
  **169 checks, 0 failed across 27 cases**.
- `scons` still builds the extension; the core `.cpp` files compile into the
  shared library under godot-cpp's flags (`-std=c++17 -fno-exceptions`) with no
  warnings or errors.
- Headless run prints `[tiles] gdextension smoke test ready` with no
  load/symbol/registration errors (Godot 4.7.1, universal build).

## layout and build

- Core lives under `src/core/` exactly as the plan sketched. It includes no
  godot headers and takes no godot dependency.
- The core is compiled twice: once as position-independent `.os` objects into
  the extension (via the existing recursive `src/**` glob), and once as plain
  `.o` objects into the test executable. The two never collide because the
  suffixes differ.
- The `tests` alias uses a **separate** `Environment`, so the test binary is
  genuinely dependency-free. It is never part of the default build.
- `build/` is gitignored; the stray `.o`/`.os` objects were already covered.

## decisions worth recording

- **`Result<T, E>`** wraps `std::variant<T, E>` with named `success`/`failure`
  factories and `std::in_place_index` construction (so it is correct even if
  `T == E`, and supports move-only success values). Accessors use `std::get_if`
  (noexcept) rather than `std::get`, because the extension build is
  `-fno-exceptions`; there is no throwing path anywhere in the core. Inactive
  access asserts.
- **Quantization is pure integer/bit work.** `quantize_double` decomposes the
  IEEE-754 sign/exponent/significand, forms `|value| * 2^48` exactly as a
  128-bit magnitude, and rounds half-away-from-zero by comparing the dropped
  remainder against `2^(k-1)`. It therefore does not read the hardware rounding
  mode (verified under all four `fe*round` modes). The negative endpoint maps to
  `INT64_MIN` via an explicit magnitude-`2^63` case — it is never produced by
  negating in signed 64-bit.
- **`Int256`** is a four-limb two's-complement integer. Multiplication takes two
  `__int128` operands (coordinate values or 65-bit coordinate differences) and
  produces the full 256-bit product via 64×64 schoolbook partials; comparison is
  `sign(a - b)`, which is safe because operands stay far below the 256-bit range.
  Coordinate differences are always formed in `__int128` first, never as a
  possibly-65-bit `int64` subtraction.
- **Segment classification** uses the four-orientation scheme with an explicit
  collinear branch that projects onto whichever axis keeps the shared line
  monotone, then compares 1-D interval endpoints: `lo < hi` → overlap,
  `lo == hi` → single-point touch, else disjoint. Shared endpoints and
  collinear end-to-end contact both land in `endpoint_touch`; only positive-
  length collinear sharing is `collinear_overlap`.
- **Validation order** follows the plan's seven steps literally, so
  multiply-invalid input gets a stable diagnosis. The bow-tie test confirms
  `self_intersection` (step 6) is reported before `zero_area` (step 7) even
  though the bow tie also has zero area.
- **Triangulation** is deterministic ear clipping over the canonical CCW ring:
  scan active indices in order, clip the first strictly-CCW ear that contains no
  other active vertex (inside *or on*). The certificate is checked structurally
  in tests: `n - 2` triangles, all CCW, vertices drawn from the polygon, and the
  triangle doubled-areas summing **exactly** to the polygon's doubled area.
- **Prototile translation-normalization** subtracts canonical vertex zero with
  checked arithmetic and *reconstructs through `Polygon::make`* rather than
  reaching into an incomplete polygon. Because translation preserves winding,
  the lexicographic minimum, simplicity, and triangulation topology, the rebuild
  always succeeds; the code asserts this rather than inventing a spurious error
  variant.

## two defensive error variants are unreachable through the public path

This is deliberate and matches the plan's framing of them as defect signals, but
worth stating plainly:

- **`PolygonError::zero_area`** cannot be reached through `Polygon::make`. A
  simple polygon has zero area **iff** all its vertices are collinear, and any
  all-collinear ring is already rejected at step 5 (`redundant_collinear_vertex`
  or `adjacent_edge_overlap`). The test `fully collinear input never yields a
  polygon` documents this: such input is rejected, just with an earlier code.
  The area check remains as the final backstop.
- **`PolygonError::triangulation_failed`** is likewise a backstop: the two-ears
  theorem guarantees a validated simple polygon triangulates. There is no
  reachable input that passes validation and then fails to triangulate, so it
  has no direct positive test.

If future work wants `zero_area` to be a reachable, testable outcome, the
validation order would have to change (e.g. area before the collinear-triple
checks) — that is a spec decision, not an implementation one, so it was left
as-is. Flagging for the formalizer.

## not done (correctly out of scope)

No placements, orientations beyond the reference, arrangements, joins, palettes,
regions, levels, rendering, or godot-facing geometry. The disposable smoke-test
node is untouched, and no core value type is registered with Godot.
