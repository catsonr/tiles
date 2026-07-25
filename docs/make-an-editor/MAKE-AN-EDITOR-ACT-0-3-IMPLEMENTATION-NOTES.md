# make an editor act 0-3: implementation notes

## scope of this record

act 0-3 was implemented in the same session as act 0-2, immediately after it,
against one worktree. the act-0-2 foundation — catalog surface, resource graph,
compiler, persistence, registration, integration runner, native catalog tests —
is recorded in `MAKE-AN-EDITOR-ACT-0-2-IMPLEMENTATION-NOTES.md` and is not
repeated here.

this document records only what act 0-3 owns: the complete 34-entry catalog, the
bitmap tracer, the removal of catalog color, the per-level palette-entry color,
and the construction fixture's replacement color.

`git status --short` was empty at session start. nothing was committed.

## modified files

```text
src/content/PrototileCatalog.h        (no Rgb8, no color(); full table)
src/content/PrototileCatalog.cpp
src/content/CellBoundary.h            (new, private content helper)
src/content/CellBoundary.cpp          (new)
src/game/resources/LevelResources.h   (PaletteEntryResource::color)
src/game/resources/LevelResources.cpp
src/game/Editor.cpp                   (generated fixture color)
src/game/testing/ResourceIntegrationRunner.cpp
tests/test_prototile_catalog.cpp
tests/test_tetromino_state.cpp
godot-project/tests/fixtures/canonical_level.tres
```

`src/content/CellBoundary.{h,cpp}` is the "small private content helper file"
act 0-3 permits: it keeps the 18 normative bitmaps and the tracer from making
`PrototileCatalog.cpp` unreadable. it is Godot-free and used only by
`PrototileCatalog.cpp` and the catalog tests.

## canonical catalog surface

`Rgb8` and `CanonicalPrototile::color()` are gone. grep confirms no `Rgb8`, no
catalog `color()`, and no content-specific color table remains anywhere under
`src/content/`, `src/core/`, or `src/engine/`. nothing replaced them: no default
color table, family color, color callback, Godot color, material, texture,
style, or theme lookup exists in content.

`CanonicalPrototile` now owns exactly:

```cpp
const Prototile &prototile() const;
const std::string &display_name() const;
```

catalog construction still proves nonemptiness, unique stable ids, nonempty
display names, exact polygon construction, exact prototile construction, and
stable presentation order. content is now a named ordered set of exact polygons.

## fixed catalog table

34 entries in exactly this order. `source` is `ring` for an explicit exact
whole-game-unit boundary and `cells` for a normative unit-cell bitmap traced
into one. `orient.` is the number of distinct fixed orientations produced by
requesting the four quarter turns; `area` is in square game units.

| id | display name | source | orient. | area |
| --- | --- | --- | --- | --- |
| 1 | `tetromino o (square 2)` | ring | 1 | 4 |
| 2 | `tetromino i` | ring | 2 | 4 |
| 3 | `tetromino t` | ring | 4 | 4 |
| 4 | `tetromino s` | ring | 2 | 4 |
| 5 | `tetromino z` | ring | 2 | 4 |
| 6 | `tetromino j` | ring | 4 | 4 |
| 7 | `tetromino l` | ring | 4 | 4 |
| 8 | `pentomino f` | cells | 4 | 5 |
| 9 | `pentomino f mirrored` | cells | 4 | 5 |
| 10 | `pentomino i` | cells | 2 | 5 |
| 11 | `pentomino l` | cells | 4 | 5 |
| 12 | `pentomino l mirrored` | cells | 4 | 5 |
| 13 | `pentomino n` | cells | 4 | 5 |
| 14 | `pentomino n mirrored` | cells | 4 | 5 |
| 15 | `pentomino p` | cells | 4 | 5 |
| 16 | `pentomino p mirrored` | cells | 4 | 5 |
| 17 | `pentomino t` | cells | 4 | 5 |
| 18 | `pentomino u` | cells | 4 | 5 |
| 19 | `pentomino v` | cells | 4 | 5 |
| 20 | `pentomino w` | cells | 4 | 5 |
| 21 | `pentomino x` | cells | 1 | 5 |
| 22 | `pentomino y` | cells | 4 | 5 |
| 23 | `pentomino y mirrored` | cells | 4 | 5 |
| 24 | `pentomino z` | cells | 2 | 5 |
| 25 | `pentomino z mirrored` | cells | 2 | 5 |
| 26 | `domino` | ring | 2 | 2 |
| 27 | `square 1` | ring | 1 | 1 |
| 28 | `square 3` | ring | 1 | 9 |
| 29 | `square 4` | ring | 1 | 16 |
| 30 | `square 5` | ring | 1 | 25 |
| 31 | `square 6` | ring | 1 | 36 |
| 32 | `square 7` | ring | 1 | 49 |
| 33 | `square 8` | ring | 1 | 64 |
| 34 | `square 9` | ring | 1 | 81 |

ids `1..7` keep their act-0-2 rings verbatim, moved from the bootstrap. they are
given as explicit rings rather than bitmaps precisely so "retains act-0-2
geometry exactly" is true by transcription, not by an argument about the tracer.
a test rebuilds each of the seven from those original ring literals and asserts
`same_boundary` against the catalog entry.

id `1` is both the `o` tetromino and the side-2 square. there is no `square 2`
entry, no alias, and no second congruent identity — a test compares id `1`
against every admitted rotation of every other entry and requires no match.

tetromino orientations total `19`; pentomino orientations total exactly `63`;
both are asserted.

### source-cell definitions

the 18 pentomino bitmaps are transcribed from the act-0-3 normative table with
the bottom printed row at `y = 0`, columns increasing in `+x`, and cell `(x, y)`
occupying `[x, x+1] × [y, y+1]`. as stored in `build_canonical_definitions()`:

```text
 8  f            (1,2) (2,2) (0,1) (1,1) (1,0)
 9  f mirrored   (0,2) (1,2) (1,1) (2,1) (1,0)
10  i            (0,0) (0,1) (0,2) (0,3) (0,4)
11  l            (0,0) (1,0) (0,1) (0,2) (0,3)
12  l mirrored   (0,0) (1,0) (1,1) (1,2) (1,3)
13  n            (0,1) (1,1) (1,0) (2,0) (3,0)
14  n mirrored   (2,1) (3,1) (0,0) (1,0) (2,0)
15  p            (0,2) (1,2) (0,1) (1,1) (0,0)
16  p mirrored   (0,2) (1,2) (0,1) (1,1) (1,0)
17  t            (0,2) (1,2) (2,2) (1,1) (1,0)
18  u            (0,1) (2,1) (0,0) (1,0) (2,0)
19  v            (0,2) (0,1) (0,0) (1,0) (2,0)
20  w            (0,2) (0,1) (1,1) (1,0) (2,0)
21  x            (1,2) (0,1) (1,1) (2,1) (1,0)
22  y            (2,1) (0,0) (1,0) (2,0) (3,0)
23  y mirrored   (1,1) (0,0) (1,0) (2,0) (3,0)
24  z            (0,2) (1,2) (1,1) (1,0) (2,0)
25  z mirrored   (1,2) (2,2) (1,1) (0,0) (1,0)
```

the mirrored definitions are explicit. production never creates id `9`, `12`,
`14`, `16`, `23`, or `25` by reflecting a constructed or quantized `Prototile`;
reflection appears only inside the *tests*, applied to source cell sets, to prove
the pairing. grep confirms no reflection transform exists in `src/core/` or
`src/engine/` beyond the pre-existing `Rotation.h` comment "none reflects".

the domino and squares use `rectangle_ring(w, h)` producing
`(0,0) (w,0) (w,h) (0,h)`, built through the checked `whole_game_units`. no
floating conversion and no procedural scaling of another `Prototile` is involved.

## private unit-cell boundary construction

`content::trace_cell_boundary(const std::vector<Cell> &)` returns
`Result<std::vector<Point>, CellBoundaryError>`.

```text
 1. reject an empty set                                -> empty
 2. reject any cell whose corners leave [-32768, 32767] -> coordinate_out_of_range (+ cell)
 3. reject a duplicate cell                             -> duplicate_cell (+ cell)
 4. reject an edge-disconnected set (BFS over shared
    complete edges; diagonal contact does not connect)  -> disconnected
 5. emit each cell's four unit boundary edges in CCW
    order: (x,y)->(x+1,y)->(x+1,y+1)->(x,y+1)->(x,y)
 6. cancel every directed edge whose reverse is also
    present — exactly the edges interior to the union
 7. require one outgoing and one incoming retained edge
    per corner                                          -> branching_vertex
 8. start at the lexicographically smallest retained
    corner and follow the unique directed cycle
 9. require the cycle to consume every retained edge    -> multiple_boundary_cycles
10. emit only direction-changing corners
11. convert those corners with the checked whole-unit
    builder; the closing vertex is omitted
```

two deliberate refinements of the plan's step list, both recorded because they
change nothing observable but do change where things happen:

- **collinear merging (step 10) is mandatory, not optional.** the plan's step
  list does not mention it, but `Polygon::make` rejects
  `redundant_collinear_vertex`, so a bar's four unit bottom edges must become one
  polygon edge or *every* straight-sided piece would fail validation. this is the
  same convention the pre-existing tetromino rings already used ("only
  direction-changing corners"). a test asserts the 4×1 bar traces to exactly 4
  vertices and the plus to exactly 12.
- **`Polygon::make` is called by the catalog, not inside the tracer.** the plan
  lists it as the tracer's step 13; the helper instead returns the ring and the
  catalog's `polygon` stage calls the factory. this keeps act 0-2's error
  contract exact — the `polygon` stage populates only `polygon_error`, and the
  `definition` stage populates only tracer defects — instead of burying a
  `PolygonError` two levels deep. the product is identical: shipped content is
  still validated and canonicalized by the ordinary core authority, and no
  content bypasses it.

the choice of CCW cell-edge direction means the traced exterior is already
counterclockwise, but `Polygon::make` remains the authoritative validator and
canonicalizer.

the helper exposes no general polyomino type, adds no cell geometry to
`src/core/`, rasterizes nothing at runtime, computes no floating union, is not
used for region authoring, and leaves no cells inside any `Prototile`. grep
confirms `Cell` appears in no core or engine translation unit.

### tracer rejections under test

each failure mode is exercised through the helper directly:

```text
empty                     {}
duplicate_cell            {(0,0), (1,0), (0,0)}
coordinate_out_of_range   {(40000, 0)}
disconnected              {(0,0), (1,1)}
branching_vertex          {(0,0), (-1,0), (-1,1), (-1,2), (0,2), (1,2), (1,1)}
multiple_boundary_cycles  3x3 minus its centre cell
```

the `branching_vertex` fixture is edge-connected the long way around while only
the diagonally opposite cells `(0,0)` and `(1,1)` meet at corner `(1,1)`, so the
boundary passes through that corner twice. none of the 34 shipped definitions
contains such a pinch.

## resulting canonical rings

every entry's stored canonical boundary, in whole game units, counterclockwise,
beginning at its lexicographically smallest vertex translated to the local
origin (which is why several extend to negative `y`). the closing vertex is
implicit.

```text
 1  tetromino o (square 2) (0,0) (2,0) (2,2) (0,2)
 2  tetromino i            (0,0) (4,0) (4,1) (0,1)
 3  tetromino t            (0,0) (3,0) (3,1) (2,1) (2,2) (1,2) (1,1) (0,1)
 4  tetromino s            (0,0) (2,0) (2,1) (3,1) (3,2) (1,2) (1,1) (0,1)
 5  tetromino z            (0,0) (1,0) (1,-1) (3,-1) (3,0) (2,0) (2,1) (0,1)
 6  tetromino j            (0,0) (2,0) (2,3) (1,3) (1,1) (0,1)
 7  tetromino l            (0,0) (2,0) (2,1) (1,1) (1,3) (0,3)
 8  pentomino f            (0,0) (1,0) (1,-1) (2,-1) (2,1) (3,1) (3,2) (1,2) (1,1) (0,1)
 9  pentomino f mirrored   (0,0) (1,0) (1,-2) (2,-2) (2,-1) (3,-1) (3,0) (2,0) (2,1) (0,1)
10  pentomino i            (0,0) (1,0) (1,5) (0,5)
11  pentomino l            (0,0) (2,0) (2,1) (1,1) (1,4) (0,4)
12  pentomino l mirrored   (0,0) (2,0) (2,4) (1,4) (1,1) (0,1)
13  pentomino n            (0,0) (1,0) (1,-1) (4,-1) (4,0) (2,0) (2,1) (0,1)
14  pentomino n mirrored   (0,0) (3,0) (3,1) (4,1) (4,2) (2,2) (2,1) (0,1)
15  pentomino p            (0,0) (1,0) (1,1) (2,1) (2,3) (0,3)
16  pentomino p mirrored   (0,0) (1,0) (1,-1) (2,-1) (2,2) (0,2)
17  pentomino t            (0,0) (1,0) (1,-2) (2,-2) (2,0) (3,0) (3,1) (0,1)
18  pentomino u            (0,0) (3,0) (3,2) (2,2) (2,1) (1,1) (1,2) (0,2)
19  pentomino v            (0,0) (3,0) (3,1) (1,1) (1,3) (0,3)
20  pentomino w            (0,0) (1,0) (1,-1) (3,-1) (3,0) (2,0) (2,1) (1,1) (1,2) (0,2)
21  pentomino x            (0,0) (1,0) (1,-1) (2,-1) (2,0) (3,0) (3,1) (2,1) (2,2) (1,2) (1,1) (0,1)
22  pentomino y            (0,0) (4,0) (4,1) (3,1) (3,2) (2,2) (2,1) (0,1)
23  pentomino y mirrored   (0,0) (4,0) (4,1) (2,1) (2,2) (1,2) (1,1) (0,1)
24  pentomino z            (0,0) (1,0) (1,-2) (3,-2) (3,-1) (2,-1) (2,1) (0,1)
25  pentomino z mirrored   (0,0) (2,0) (2,2) (3,2) (3,3) (1,3) (1,1) (0,1)
26  domino                 (0,0) (2,0) (2,1) (0,1)
27  square 1               (0,0) (1,0) (1,1) (0,1)
28  square 3               (0,0) (3,0) (3,3) (0,3)
29  square 4               (0,0) (4,0) (4,4) (0,4)
30  square 5               (0,0) (5,0) (5,5) (0,5)
31  square 6               (0,0) (6,0) (6,6) (0,6)
32  square 7               (0,0) (7,0) (7,7) (0,7)
33  square 8               (0,0) (8,0) (8,8) (0,8)
34  square 9               (0,0) (9,0) (9,9) (0,9)
```

every ring is a simple, hole-free, counterclockwise polygon with an
`n − 2` triangle triangulation; no bitmap produced a branched, multi-cycle, or
hole-bearing boundary, so no defect had to be reported back.

## per-level palette color

`PaletteEntryResource` gained:

```text
color: Color = Color(1, 1, 1, 1), PROPERTY_HINT_COLOR_NO_ALPHA
```

the setter follows the same plain mutable behaviour as the other resource
properties and emits `changed` only after an actual change; the integration
runner asserts both the emitting and the non-emitting case.

`compile_palette_resource` reads `prototile_id` and `supply` exactly as before
and never reads `color`. no engine member, no new compilation failure, and no
color compiler, color error variant, or resource-to-catalog fallback was added.
the runner compiles two palettes differing *only* in color and asserts the
resulting exact palettes are equivalent field by field.

`LoadedLevel` continues to return `{ resource, level }`, so the player can read
per-entry color in the same authored order the compiler used to build runtime
entries. nothing copies color into `src/engine/`.

authored equivalence was extended with each entry's complete `godot::Color` and
with the order association between color, encoded id, and encoded supply.
compiled equivalence is unchanged, because color is not exact game state. the
round trip proves `color before save == color after uncached load` for both
`.tres` and `.res`, and after save-as.

## temporary construction-fixture color

`Editor::color_for(PrototileId)`'s seven-case switch was replaced by:

```cpp
constexpr double HUE_STEP = 0.61803398874989484820;   // golden-ratio conjugate

godot::Color fixture_color(PrototileId p_id) {
    const double hue = std::fmod(static_cast<double>(p_id.value()) * HUE_STEP, 1.0);
    return godot::Color::from_hsv(static_cast<float>(hue), 0.62f, 0.92f);
}
```

it requires no authored table, returns an opaque visible color for every id
(including ids far beyond the catalog), keeps no catalog or engine dependency on
Godot, and is presentation-only. successive ids land far apart in hue because
the step is irrational. exact channel continuity with the old seven-color table
is deliberately not preserved — the plan states that is not a content contract
after this act — so the construction fixture now renders in different colors than
before. that is the only intended visual change.

the new `LevelEditor` and later `LevelPlayer` must use per-level resource colors
and never this fallback.

## fixture and integration changes

each fixture palette entry now carries a different explicit opaque color:

```text
entry 0: prototile_id 1, supply  3, Color(0.75, 0.5,  0.25, 1)
entry 1: prototile_id 2, supply -1, Color(0.25, 0.75, 0.5,  1)
```

all four channel values are dyadic, so they survive the `real_t == float` build
and the `.tres` text round trip exactly and are compared for equality rather
than with a tolerance.

integration additionally verifies: the opaque-white default; the `COLOR` variant
type and `COLOR_NO_ALPHA` hint; set/get and changed behaviour; that typed
palette arrays preserve the new property; that both programmatic and
text-authored resources preserve color; that `.tres` and `.res` save/load
preserve every component; that save-as and uncached reload keep color associated
with its entry's id and supply; that palette compilation ignores color; that
every act-0-2 error precedence is unchanged regardless of color; that
`LoadedLevel` retains the colored graph beside the exact level; and that the two
fixture entries carry *different* colors.

## native catalog tests

`tests/test_prototile_catalog.cpp` proves: exactly 34 entries; ids exactly
`1..34` and unique; order and display names matching a table restated inside the
test file; lookup succeeding for every id and returning `nullptr` for `0`, `35`,
`100`, and a large value; lookup returning catalog-owned storage; lookup leaving
order and values unchanged; ids `1..7` matching the original tetromino rings;
every entry translation-normalized with positive doubled area and an
`n − 2` triangulation; every area matching the table; every pentomino bitmap
having five unique edge-connected cells; each bitmap tracing to a valid boundary
of exactly five square units which equals its catalog entry; each mirrored pair
being an exact reflection at the source-definition level; each mirrored pair
being non-congruent under the four permitted rotations; each achiral pentomino
being congruent to its own mirror and having exactly one congruent catalog entry
(itself); orientation counts matching the table; pentomino orientations summing
to `63`; the domino and every square's exact geometry, area, and orientation
count; id `1` being the exact side-2 square with nothing else congruent to it;
every equal-area pair of distinct ids being non-congruent under admitted
rotation; and copied and moved catalogs retaining exact lookup and order.

the shipped table has no color to test for, so absence is asserted structurally:
two `static_assert`s pin `CanonicalPrototile`'s complete observation surface to
`const Prototile &` and `const std::string &`, and the file would have to name
any reintroduced color accessor for it to compile.

table-invariant and tracer rejections are exercised through
`content::testing::make_catalog` and `content::trace_cell_boundary`: empty table,
duplicate id, empty display name, a polygon failure, a definition failure, and a
definition with no geometry description, each checked for the right stage, the
right intended id, and exactly one populated payload.

totals:

```text
act-0-1 baseline:  2701 checks, 0 failed across 297 cases
delivered:         4295 checks, 0 failed across 336 cases
```

## verification

identical commands and identical outputs to those recorded in the act-0-2
notes — both acts were verified together in the delivered state:

```text
scons tests                             ok
./build/tests/tiles_tests               4295 checks, 0 failed across 336 cases
scons                                   ok, no warning from any authored unit
res://tests/resource_integration.tscn   220 checks passed, exit 0
--quit-after 2                          clean bootstrap and draw, exit 0
```

grep verification specific to this act:

```text
Godot include or color in core/content/engine   none
Rgb8 or catalog color()                          none
content-specific color table                     none
duplicate side-2 square                          none (test-proven, not just grep)
reflection transform in placement/orientation    none
polyomino cells in core runtime values           none
json or alternate serializer                     none
generated integration files after exit           none
```

## points for the next formalizer or implementor

1. act 1-1's palette rows should read display names from
   `catalog.entries()` in order and generate their default opaque color
   themselves. the catalog offers no default color and should not gain one.
2. `fixture_color` is scoped to `Editor` only. act 2 retires that fixture; the
   function should go with it rather than becoming the player's color policy.
3. the tracer is content-compile-time only. if a later act wants author-defined
   polyominoes, that needs a new scope decision — the helper is deliberately
   non-public to other layers and rejects holes, pinches, and disconnected sets
   rather than trying to represent them.
4. all 34 entries are one-sided under quarter turns only. adding a reflected
   piece means adding a *new id*, never a new placement transform.
