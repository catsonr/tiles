# make an editor act 0-3: complete initial catalog and per-level color

## status and supersession

this is a new immutable act. it does not edit or replace the act-0-2 plan.

act 0-2 remains the required implementation foundation: it introduces the
canonical catalog, resource graph, exact compiler, persistence, and seven
tetrominoes. this act deliberately supersedes only these act-0-2 decisions:

- the canonical catalog no longer owns presentation color;
- `PaletteEntryResource` gains one per-level presentation color; and
- the initial canonical content expands beyond the seven tetrominoes.

all unaffected act-0-2 contracts remain in force.

## implementor context seed

this act turns the initial catalog into the complete polygon set intended for
the first game:

```text
7 one-sided tetrominoes
18 one-sided pentominoes
1 domino
integer-sided squares 1 through 9
```

the side-2 square is exactly the existing `o` tetromino. it retains id `1` and
is never duplicated.

the catalog remains Godot-free and owns exact `Prototile` values. level-specific
color moves into the Godot resource graph because color is presentation chosen
while authoring one level, not identity or shipped geometry.

before editing:

1. run `git status --short` and preserve every pre-existing user change;
2. enumerate authored files with `rg --files`, excluding generated build output,
   `Godot.app/`, and `vendor/`;
3. read this document completely;
4. read `README.md`, `DEFINITIONS.md`, `COORDINATES.md`, and
   `docs/make-an-editor/MAKE-AN-EDITOR-ROADMAP.md` completely;
5. read every completed `MAKE-AN-EDITOR` plan and implementation note through
   act 0-2 completely;
6. read the complete catalog, tetromino bootstrap, resource, compiler,
   persistence, registration, integration-runner, and catalog-test sources;
7. inspect the pinned Godot 4.7 `Color`, property-hint, resource saver, and
   loader APIs used;
8. establish the completed act-0-2 baseline with all native, build, resource
   integration, and ordinary headless commands recorded by that act; and
9. confirm no other session is changing catalog ids, resource schema, catalog
   construction, or palette integration files.

if act 0-2 is not completely implemented and documented, stop. do not combine
the two acts opportunistically or reinterpret this delta against an unfinished
api.

after implementation, record the exact id/name/bitmap table, boundary-tracing
algorithm, every resulting canonical ring, orientation counts, resource
property changes, color migration, changed tests, final totals, build results,
complete integration output, and complete ordinary headless output in:

```text
docs/make-an-editor/MAKE-AN-EDITOR-ACT-0-3-IMPLEMENTATION-NOTES.md
```

do not commit. do not mention this implementation document in code comments or
runtime-facing documentation.

## goal

the canonical content relationship becomes:

```haskell
data CanonicalPrototile = CanonicalPrototile
  { prototile   :: Prototile
  , displayName :: String
  }

data PaletteEntryResource = PaletteEntryResource
  { prototileId :: Int64
  , supply      :: Int64
  , color       :: Color
  }
```

the separation is exact:

```text
catalog:
  stable id + display name + exact polygon

level palette entry:
  catalog id + supply + presentation color

engine palette entry:
  exact prototile + compiled orientations + supply
```

color never affects:

- `Prototile` identity;
- catalog lookup;
- palette uniqueness;
- orientation compilation;
- placement legality;
- supply;
- region containment;
- completion; or
- exact round-trip comparison.

the resource retains color for the editor and player while the exact
`engine::Level` remains presentation-free.

## canonical catalog surface

remove `Rgb8` and `CanonicalPrototile::color()` from
`src/content/PrototileCatalog.{h,cpp}`.

`CanonicalPrototile` continues to own:

```cpp
const Prototile &prototile() const;
const std::string &display_name() const;
```

catalog construction continues to prove:

- nonempty catalog;
- unique stable ids;
- nonempty display names;
- exact polygon construction;
- exact prototile construction; and
- stable presentation order.

do not replace removed color with:

- another catalog color type;
- a default-color table;
- a family color;
- a color-generating callback;
- Godot color;
- material, texture, or style metadata; or
- a hidden global theme lookup.

catalog content is now essentially a named ordered set of exact polygons.

## fixed catalog order and ids

the catalog contains exactly 34 entries in this order:

```text
id  display name                     content
 1  tetromino o (square 2)           existing o
 2  tetromino i                      existing i
 3  tetromino t                      existing t
 4  tetromino s                      existing s
 5  tetromino z                      existing z
 6  tetromino j                      existing j
 7  tetromino l                      existing l

 8  pentomino f                      f
 9  pentomino f mirrored             reflection of f
10  pentomino i                      i
11  pentomino l                      l
12  pentomino l mirrored             reflection of l
13  pentomino n                      n
14  pentomino n mirrored             reflection of n
15  pentomino p                      p
16  pentomino p mirrored             reflection of p
17  pentomino t                      t
18  pentomino u                      u
19  pentomino v                      v
20  pentomino w                      w
21  pentomino x                      x
22  pentomino y                      y
23  pentomino y mirrored             reflection of y
24  pentomino z                      z
25  pentomino z mirrored             reflection of z

26  domino                           2 × 1 domino

27  square 1                         side length 1
28  square 3                         side length 3
29  square 4                         side length 4
30  square 5                         side length 5
31  square 6                         side length 6
32  square 7                         side length 7
33  square 8                         side length 8
34  square 9                         side length 9
```

ids `1` through `7` retain their act-0-2 geometry and identity exactly. their
display names become family-qualified as shown so a flat catalog distinguishes
tetromino and pentomino letters.

id `1` is both the `o` tetromino and the side-2 square. there is no `square 2`
entry elsewhere, no alias id, and no second congruent prototile.

ids are stable serialized identities. do not renumber by area, bitmap, family,
name, or later catalog sorting.

## pentomino definition

the catalog contains all 18 one-sided pentominoes. reflections are distinct
catalog entries because placement reflection remains forbidden.

the six chiral free pentominoes are:

```text
f, l, n, p, y, z
```

each receives one explicit mirrored entry. the remaining six are achiral under
rotation plus reflection and receive one entry:

```text
i, t, u, v, w, x
```

this produces:

```text
6 × 2 + 6 = 18
```

do not add reflection as a placement transform or orientation. each mirrored
piece is an ordinary independently identified exact `Prototile`.

## authoritative unit-cell bitmaps

the following bitmaps are normative source definitions.

each `#` denotes one occupied closed unit cell. columns increase in positive x.
the bottom printed row has y coordinate `0`; printed rows above it increase in
positive y. one cell in column `x`, row `y` occupies:

```text
[x, x + 1] × [y, y + 1]
```

`.` is empty. whitespace surrounding a bitmap is not geometry.

```text
id 8 — pentomino f

.##
##.
.#.

id 9 — pentomino f mirrored

##.
.##
.#.

id 10 — pentomino i

#
#
#
#
#

id 11 — pentomino l

#.
#.
#.
##

id 12 — pentomino l mirrored

.#
.#
.#
##

id 13 — pentomino n

##..
.###

id 14 — pentomino n mirrored

..##
###.

id 15 — pentomino p

##
##
#.

id 16 — pentomino p mirrored

##
##
.#

id 17 — pentomino t

###
.#.
.#.

id 18 — pentomino u

#.#
###

id 19 — pentomino v

#..
#..
###

id 20 — pentomino w

#..
##.
.##

id 21 — pentomino x

.#.
###
.#.

id 22 — pentomino y

..#.
####

id 23 — pentomino y mirrored

.#..
####

id 24 — pentomino z

##.
.#.
.##

id 25 — pentomino z mirrored

.##
.#.
##.
```

every bitmap has exactly five occupied cells, is edge-connected, has no hole,
and defines one simple polygonal union.

the normative mirrored bitmaps are explicit. production construction must not
create id `9`, `12`, `14`, `16`, `23`, or `25` by reflecting an already
constructed or quantized `Prototile`.

## domino and squares

id `26` is the normalized axis-aligned union of two unit cells:

```text
##
```

its exact boundary is:

```text
(0,0), (2,0), (2,1), (0,1)
```

new square id `k` with side `s` uses:

```text
(0,0), (s,0), (s,s), (0,s)
```

for:

```text
id 27 -> s = 1
id 28 -> s = 3
id 29 -> s = 4
id 30 -> s = 5
id 31 -> s = 6
id 32 -> s = 7
id 33 -> s = 8
id 34 -> s = 9
```

use checked or compile-time proven whole-unit coordinate construction. no
floating conversion or procedural scaling of another `Prototile` is permitted.

## private unit-cell boundary construction

do not hand-transcribe 18 long polygon rings alongside the normative bitmaps.

add one private content-construction helper inside
`PrototileCatalog.cpp` or one namespace-private content source if separation is
needed for legibility. it is not public core geometry and not a runtime polygon
boolean operation.

for one fixed finite occupied-cell set:

1. reject an empty set;
2. reject duplicate cells;
3. verify exact edge connectivity;
4. for every occupied cell, enumerate its four unit boundary edges;
5. cancel each edge whose opposite belongs to the neighboring occupied cell;
6. retain the directed exterior edges;
7. verify every retained boundary vertex has exactly one incoming and one
   outgoing retained edge;
8. begin at the lexicographically smallest boundary vertex;
9. follow the unique directed cycle;
10. reject an early repeat, branch, second cycle, or unused retained edge;
11. omit the repeated closing vertex;
12. construct exact whole-game-unit `Point`s; and
13. pass the resulting ring through `Polygon::make`.

choose cell-edge directions so the traced exterior is counterclockwise, but
still rely on `Polygon::make` as the authoritative polygon validator and
canonicalizer.

construction failures retain:

- stable intended prototile id;
- catalog stage;
- complete polygon or prototile error; and
- one content-definition error for empty, duplicate, disconnected, branching,
  multiple-cycle, or unused-edge bitmap defects if the private helper makes
  those failures representable.

do not:

- expose a general polyomino type;
- add cell geometry to `src/core/`;
- rasterize at runtime;
- compute a floating union;
- use this helper for region authoring;
- retain occupied cells inside `Prototile`;
- generate placement reflections; or
- bypass `Polygon::make`.

the helper is shipped-content compilation which converts a compact exact source
description into the same canonical `Polygon` product as every other catalog
entry.

## expected distinct orientation counts

requesting the four quarter turns must produce:

```text
tetrominoes:
  o 1, i 2, t 4, s 2, z 2, j 4, l 4

pentominoes:
  f 4, f mirrored 4
  i 2
  l 4, l mirrored 4
  n 4, n mirrored 4
  p 4, p mirrored 4
  t 4
  u 4
  v 4
  w 4
  x 1
  y 4, y mirrored 4
  z 2, z mirrored 2

domino:
  2

every square:
  1
```

every chiral pair must remain geometrically distinct under rotations alone.
the 18 one-sided pentomino entries produce exactly 63 distinct fixed
orientations in total.

## per-level palette color

extend `PaletteEntryResource` with:

```haskell
color :: godot Color
```

property:

```text
color: Color = opaque white
```

use the pinned no-alpha color property hint when available. the authored value
is presentation transport. its setter follows the resource classes' established
plain mutable behavior and emits `changed` only after an actual change.

the editor introduced by act 1-1 chooses opaque colors. the resource schema does
not assign content-specific defaults beyond neutral opaque white.

`compile_palette_resource` reads id and supply exactly as before and ignores
color when constructing `engine::Palette`. color adds no engine member and no
new palette compilation failure.

the complete resource remains available beside the compiled `engine::Level`:

```text
LoadedLevel.resource
LoadedLevel.level
```

the player may therefore read palette-entry color in the same authored order in
which the compiler constructs runtime palette entries. do not copy color into
`src/engine/`.

## authored and compiled equivalence

extend authored resource equivalence with:

- every palette entry's complete `godot::Color` value; and
- palette entry order associating that color with its encoded id and supply.

compiled exact equivalence remains unchanged because color is not exact game
state.

round-trip tests prove:

```text
resource color before save = resource color after uncached load
```

they do not add color to `engine::Palette` comparison.

## temporary construction-fixture color

act 0-2 temporarily moved the seven debug colors into canonical content so the
old `Editor` could render its fixture. removing catalog color requires one
temporary presentation-local replacement until act 2 retires the fixture.

use one deterministic game-local color function derived from
`PrototileId::value()` for construction-fixture drawing. it must:

- require no authored table;
- return an opaque visible color for every id;
- keep no catalog or engine dependency on Godot;
- remain presentation-only; and
- not become the LevelPlayer color policy.

do not transcribe the old seven-color table back into a switch. exact channel
continuity for a disposable fixture is not a content contract after this act.

the new `LevelEditor` and later `LevelPlayer` use per-level resource colors,
never this fallback.

## resource fixture and integration changes

update the act-0-2 authored fixture so each palette entry explicitly carries a
different opaque color.

extend resource integration to verify:

- default `PaletteEntryResource::color` is opaque white;
- the inspector property has the correct Godot type and color hint;
- set/get and changed notification behavior;
- typed palette arrays preserve the new property;
- programmatic and text-authored resources preserve color;
- `.tres` and `.res` save/load preserve every component;
- save-as and uncached reload preserve association with entry id and supply;
- palette compilation ignores color and produces exact equivalent engine
  palettes for resources differing only in color;
- invalid id, supply, orientation, and palette errors retain act-0-2 precedence
  regardless of color;
- `LoadedLevel` retains the colored resource graph beside its exact level; and
- no catalog color remains.

do not add a color compiler, color error variant, engine presentation value, or
resource-to-catalog fallback.

## native catalog tests

extend dependency-free catalog tests to prove:

- exactly 34 entries exist;
- ids are exactly `1` through `34`;
- order and display names match the fixed table;
- lookup succeeds for every id and fails outside the table;
- no entry owns color metadata;
- ids `1` through `7` retain exact act-0-2 geometry;
- every normative pentomino bitmap has five unique edge-connected cells;
- each bitmap boundary compiles successfully;
- each exact pentomino area is five square game units;
- each mirrored pair is an exact reflection at the source-definition level;
- each mirrored pair is not congruent under the four permitted rotations;
- achiral pentominoes have no duplicate mirror entry;
- expected orientation counts match the table;
- pentomino orientation counts sum to exactly `63`;
- domino geometry, area, and orientation count are exact;
- square ids have exact side lengths, areas, and one orientation;
- id `1` is exact side-2 square geometry;
- no second catalog entry is congruent to id `1`;
- every pair of different catalog ids has distinct geometry under admitted
  rotation when the pair has equal area; and
- copied and moved catalogs retain exact lookup and order.

test the private cell-boundary helper through the fixed definitions and, if an
appropriately narrow test seam exists, through empty, duplicate, disconnected,
branching/multiple-boundary, and hole-bearing invalid fixtures. do not expose a
public arbitrary polyomino constructor solely for tests.

## source and build layout

expected modifications:

```text
src/content/PrototileCatalog.h
src/content/PrototileCatalog.cpp
src/game/resources/LevelResources.h
src/game/resources/LevelResources.cpp
src/game/Editor.cpp
src/game/testing/ResourceIntegrationRunner.cpp
tests/test_prototile_catalog.cpp
tests/test_tetromino_state.cpp
godot-project/tests/fixtures/canonical_level.tres
```

small private content helper files are permitted when the bitmap table and
tracer would make `PrototileCatalog.cpp` materially less legible. if added,
keep them under `src/content/`, Godot-free, and non-public to other layers.

do not change:

- core polygon, prototile, orientation, or placement semantics;
- engine palette, level, state, or session ownership;
- level, palette, region, or polygon resource relationships other than the new
  palette-entry color property;
- id or supply encoding;
- resource compiler precedence other than reading past the new ignored
  presentation field;
- persistence path behavior;
- region geometry;
- main scene ownership;
- input; or
- project rendering settings.

## verification

run and record:

```text
scons tests
./build/tests/tiles_tests
scons
./Godot.app/Contents/MacOS/Godot \
    --headless \
    --path godot-project \
    res://tests/resource_integration.tscn
./Godot.app/Contents/MacOS/Godot \
    --headless \
    --path godot-project \
    --quit-after 2
```

independently compile changed Godot-free translation units under the established
c++17 and no-exception assumptions when warning provenance is unclear.

grep-verify:

- no Godot include or color entered `src/core/`, `src/content/`, or
  `src/engine/`;
- no `Rgb8` or catalog `color()` remains;
- no content-specific color table remains;
- no duplicate side-2 square exists;
- no reflection transform entered placement or orientation code;
- no polyomino cells entered core runtime values;
- no json or alternate serializer was added; and
- no generated integration files remain.

## acceptance

act 0-3 is complete when:

- the canonical catalog contains exactly the fixed 34 entries;
- ids, names, order, and geometry match this plan;
- all seven tetromino ids and geometry remain stable;
- all 18 one-sided pentominoes exist;
- six chiral pairs remain distinct without placement reflection;
- domino id `26` is exact;
- squares of side `1` through `9` are available;
- id `1` alone represents side `2`;
- all content constructs through exact whole-unit geometry and normal core
  validators;
- the private bitmap tracer emits one complete simple boundary and no runtime
  cell representation;
- orientation counts are exact;
- canonical content owns no color;
- every palette entry resource owns one per-level Godot color;
- color round-trips with id and supply;
- engine compilation ignores color;
- exact engine layers remain presentation-free;
- the old fixture uses only a temporary generated presentation color;
- native catalog tests pass;
- resource integration passes and cleans up;
- the extension builds cleanly;
- ordinary headless startup remains clean; and
- implementation notes record every canonical ring and verified result.

## explicitly out of scope

act 0-3 does not implement:

- hexominoes or larger polyomino families;
- trominoes;
- a second monomino identity beyond `square 1`;
- another domino orientation as identity;
- square sizes above `9`;
- a duplicate side-2 square;
- rectangles other than the domino;
- arbitrary runtime polyomino construction;
- catalog mutation or mods;
- reflected placement transforms;
- non-quarter rotations;
- scaling fields;
- catalog color or default content palettes;
- materials, textures, patterns, or alpha policy;
- palette editing UI;
- region authoring;
- player rendering;
- campaign metadata; or
- migration guarantees beyond the fixed stable ids.

if any normative bitmap produces an invalid, branched, multi-cycle, or
hole-bearing boundary under the exact tracer, stop and report the id, bitmap,
retained edges, traced ring, and complete `PolygonError`. do not silently alter
the shape, hand-author a different ring, or weaken polygon validation.

if adding `Color` to `PaletteEntryResource` breaks typed-array persistence or
uncached round-trip under pinned Godot 4.7, stop and report the smallest failing
resource. do not move color back into canonical content or into the engine.
