# make an editor act 0-2: implementation notes

## scope of this record

acts 0-2 and 0-3 were implemented in one session, in that order, against one
worktree. act 0-3 explicitly supersedes three act-0-2 decisions:

```text
catalog color               removed from content
PaletteEntryResource.color  added
initial catalog content     7 tetrominoes -> 34 entries
```

this document records everything act 0-2 owns **as delivered**, and marks each
superseded decision where it appears. the catalog table, the bitmap tracer, the
per-level color property, and the fixture's colors are recorded in
`MAKE-AN-EDITOR-ACT-0-3-IMPLEMENTATION-NOTES.md`.

`git status --short` was empty at session start; no pre-existing user change
existed to preserve. nothing was committed.

## delivered files

new:

```text
src/content/CellBoundary.h
src/content/CellBoundary.cpp
src/content/PrototileCatalog.h
src/content/PrototileCatalog.cpp
src/game/resources/LevelResources.h
src/game/resources/LevelResources.cpp
src/game/resources/ResourceCompiler.h
src/game/resources/ResourceCompiler.cpp
src/game/resources/LevelPersistence.h
src/game/resources/LevelPersistence.cpp
src/game/testing/ResourceIntegrationRunner.h
src/game/testing/ResourceIntegrationRunner.cpp
tests/test_prototile_catalog.cpp
godot-project/tests/fixtures/canonical_level.tres
godot-project/tests/resource_integration.tscn
```

modified:

```text
SConstruct
src/engine/TetrominoState.h
src/engine/TetrominoState.cpp
src/game/Editor.h
src/game/Editor.cpp
src/register_types.cpp
tests/test_tetromino_state.cpp
tests/test_commands.cpp
```

`tests/test_commands.cpp` was not on the plan's expected-modification list. two
of its cases used `make_tetromino_state()` as a fixture and had to follow the
signature change; a local `tetromino_state()` helper was added which builds the
canonical catalog and the bootstrap and returns one optional. no assertion in
that file was weakened.

`src/content/CellBoundary.{h,cpp}` is the small private content helper act 0-3
permits. it is Godot-free and used only by `PrototileCatalog.cpp` and the
catalog tests.

## architectural boundary

```text
src/core/       generic exact geometry, Prototile, Region
src/content/    canonical Prototile values, stable ids, order, display names
src/engine/     Palette, Level, State, Session
src/game/resources/  Godot resource transport, exact compiler, persistence
src/game/testing/    the dedicated headless integration runner
```

`src/content/` includes only `src/core/` and the standard library. grep confirms
no Godot include, type, resource name, or identifier exists in `src/core/`,
`src/content/`, or `src/engine/`; the single textual hit is a pre-existing
comment in `src/core/geometry/ExactInteger.h` reading "it is not a domain or
Godot type".

`src/engine/Palette.h` is unchanged: a `PaletteEntry` still owns an ordinary
`Prototile` and performs no global lookup. the one engine file which now names
`src/content/` is `TetrominoState.{h,cpp}`, the temporary bootstrap, exactly as
the plan directs.

## canonical prototile catalog

### public surface

```cpp
namespace tiles::content {

class CanonicalPrototile final {
public:
    const Prototile &prototile() const;
    const std::string &display_name() const;
private:
    CanonicalPrototile(Prototile, std::string);   // friend: PrototileCatalog
};

enum class PrototileCatalogStage { definition, polygon, prototile, catalog };
enum class PrototileCatalogErrorCode { empty, duplicate_prototile_id, empty_display_name };

struct PrototileCatalogError final {
    PrototileCatalogStage stage;
    std::optional<PrototileId> prototile_id;
    std::optional<CellBoundaryError> definition_error;
    std::optional<PolygonError> polygon_error;
    std::optional<PrototileError> prototile_error;
    std::optional<PrototileCatalogErrorCode> catalog_error;
};

class PrototileCatalog final {
public:
    const std::vector<CanonicalPrototile> &entries() const;
    const CanonicalPrototile *find(PrototileId) const;
private:
    static Result<PrototileCatalog, PrototileCatalogError> build(
        const std::vector<CanonicalDefinition> &);
};

Result<PrototileCatalog, PrototileCatalogError> make_canonical_prototile_catalog();
const std::vector<CanonicalDefinition> &canonical_definitions();

namespace testing {
Result<PrototileCatalog, PrototileCatalogError> make_catalog(
    const std::vector<CanonicalDefinition> &);
}

} // namespace tiles::content
```

deviations from the plan's conceptual spelling, all preserving the ownership,
failure, lookup, and metadata contracts:

- `Rgb8` and `CanonicalPrototile::color()` are absent — superseded by act 0-3.
- one extra stage, `definition`, and one extra payload, `definition_error`,
  carry the bitmap-tracer defects act 0-3 asks to keep representable.
- `PrototileCatalog::build` is **private**. the plan forbids a *public*
  arbitrary production catalog constructor, and permits a private general
  constructor "if useful for tests" provided it validates the same invariants.
  it is the single construction path: `make_canonical_prototile_catalog` and
  `content::testing::make_catalog` are its only two friends, and both reach the
  identical validation.
- `canonical_definitions()` exposes the fixed shipped table read-only. it is the
  same table production compiles — there is no second copy — and it exists so
  tests assert shipped order/ids/names/source geometry rather than reading them
  back out of the value they are checking.

the catalog owns every entry by value; every entry owns its `Prototile` by
value; all observation is const. `find` is a linear scan returning a pointer
into catalog storage or `nullptr`. it inserts, constructs, logs, aliases, and
falls back never; a test asserts `find(id) == &entries()[i]` for all 34 ids, and
that order and values are unchanged after 40 lookups including misses.

### per-entry construction precedence

for each definition, in catalog order:

```text
1. empty display name        -> stage catalog, empty_display_name, + id
2. duplicate id              -> stage catalog, duplicate_prototile_id, + id
3. no/both source shapes     -> stage definition, CellBoundaryErrorCode::empty, + id
4. build the exact ring      -> stage definition, complete CellBoundaryError, + id
   (explicit whole-unit corners, or the cell tracer)
5. Polygon::make             -> stage polygon, complete PolygonError, + id
6. Prototile::make           -> stage prototile, complete PrototileError, + id
7. append the complete entry
```

after the loop, an empty table is `stage catalog`, `empty`, with **no** id.

this orders the two per-entry table invariants *before* geometry, where the plan
lists the geometry steps first. the reasoning: a definition which cannot be
published is rejected before work is done to compile it, and the plan's own
requirement — "still enforce those facts inside construction rather than
publishing an unchecked value" — is satisfied either way. exactly one underlying
error field is populated per failure, matching the stage, and no partial catalog
escapes.

whole-game-unit coordinates are built by `content::whole_game_units`, which
rejects any value outside `[-32768, 32767]` before multiplying by
`Coordinate::SCALE`. no floating conversion builds canonical content.

## temporary tetromino bootstrap migration

```cpp
Result<State, TetrominoStateError> make_tetromino_state(
    const content::PrototileCatalog &p_catalog);
```

the bootstrap now resolves ids `1..7` through `find` and owns no boundary, name,
or color table. `TetrominoStateStage` lost its two now-unreachable enumerators
and gained the lookup stage:

```text
before: polygon, prototile, palette_entry, palette, region_outer_polygon, region
after:  catalog_lookup, palette_entry, palette, region_outer_polygon, region
```

`TetrominoStateError::prototile_error` was removed with the `prototile` stage.
a `catalog_lookup` failure populates the id and **no** underlying error, because
absence from the supplied catalog is the complete fact.

the only geometry the bootstrap still constructs is the unchanged debug region
outer rectangle, `x ∈ [-8, 36]`, `y ∈ [-30, 8]`, no holes.

the missing-id path is reachable in tests: `content::testing::make_catalog` is
given the shipped table minus id `3`, and `make_tetromino_state` then fails with
`catalog_lookup` naming `PrototileId(3)`.

## editor migration

`Editor` owns `std::optional<content::PrototileCatalog> catalog_`, constructs it
first in `_ready()`, reports a typed catalog failure through its existing
diagnostic visitor (`describe(PrototileCatalogStage)`), and passes the catalog
into `make_tetromino_state`.

- the `color_for(PrototileId)` seven-case switch is gone. (act 0-3 replaces it
  with a generated fixture color; see those notes.)
- the `DebugColumn { palette_entry, label }` table is gone. columns are now
  `constexpr std::size_t DEBUG_COLUMNS[] = { 1, 0, 2, 3, 4, 5, 6 }` and every
  readable label comes from `Editor::label_for(PrototileId)`, which resolves the
  catalog display name or returns `"<unknown prototile>"`.
- the previously hardcoded `"o"` strings in the overlap-fixture diagnostics also
  resolve through `label_for`, so the visible text is now
  `tetromino o (square 2)`.

arrangement, selection, proposal, draw, and diagnostic behaviour are otherwise
unchanged; the headless output below is identical to the act-0-1 baseline except
for the new catalog line and the label text.

## godot resource graph

five classes in `src/game/resources/LevelResources.{h,cpp}`, each deriving
directly from `godot::Resource`, using `GDCLASS`, binding every getter and
setter, default-constructible, and calling `emit_changed()` only after a value
actually differs. no setter validates, compiles, quantizes, logs, saves, or
loads, and no resource caches an exact value.

| class | property | variant type | hint | default |
| --- | --- | --- | --- | --- |
| `PolygonResource` | `vertices` | `PACKED_VECTOR2_ARRAY` | — | empty |
| `PaletteEntryResource` | `prototile_id` | `INT` | — | `0` |
| | `supply` | `INT` | — | `-1` |
| | `color` | `COLOR` | `COLOR_NO_ALPHA` | opaque white (act 0-3) |
| `PaletteResource` | `entries` | `ARRAY` | `ARRAY_TYPE` → `PaletteEntryResource` | empty, typed |
| `RegionResource` | `outer_boundary` | `OBJECT` | `RESOURCE_TYPE` → `PolygonResource` | `null` |
| | `inner_boundaries` | `ARRAY` | `ARRAY_TYPE` → `PolygonResource` | empty, typed |
| `LevelResource` | `palette` | `OBJECT` | `RESOURCE_TYPE` → `PaletteResource` | `null` |
| | `region` | `OBJECT` | `RESOURCE_TYPE` → `RegionResource` | `null` |

the typed-array hint string is built as
`vformat("%d/%d:%s", Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE, class_name)`
from the pinned enumerators rather than hardcoded numbers; godot-cpp ships no
`MAKE_RESOURCE_TYPE_HINT` helper.

no prototile resource, polygon, name, color, orientation rule, scale,
reflection, arrangement, history, completion flag, known solution, catalog
reference, or save-path property exists anywhere in the graph.

### registration

`src/register_types.cpp`, at `MODULE_INITIALIZATION_LEVEL_SCENE`, in exactly the
planned order:

```text
PolygonResource, PaletteEntryResource, PaletteResource, RegionResource,
LevelResource, ResourceIntegrationRunner, Editor
```

only resource and node classes enter `ClassDB`. the runner is registered solely
because its dedicated scene instantiates it; `main.tscn` is unchanged.

## exact resource compilation

`src/game/resources/ResourceCompiler.{h,cpp}` exposes the four planned
operations with the planned error types verbatim. all four are const with
respect to resource and catalog and return owned exact values.

### polygon

```text
missing_resource                -> no payload
coordinate_quantization_failed  -> vertex + axis + QuantizationError
polygon_construction_failed     -> PolygonError only
```

vertices are visited in authored order; for each, `x` then `y` is widened to
`double` and passed to the existing `quantize_double`. nothing multiplies a
`real_t` by `Coordinate::SCALE`, casts to coordinate storage, rounds through
Godot, clamps, picks an epsilon, requires whole coordinates, or pre-canonicalizes
order. `Polygon::make` is then called once and its error preserved whole.

### palette

per authored entry, in order:

```text
null entry            -> missing_entry           + entry
prototile_id < 0      -> negative_prototile_id   + entry + encoded id (no strong id)
catalog miss          -> unknown_prototile_id    + entry + encoded id + strong id
supply 0 or < -1      -> invalid_supply          + entry + encoded id + strong id + encoded supply
PaletteEntry::make    -> orientation_compilation_failed + entry + ids + LatticeOrientationError
```

then `Palette::make` once; its `empty` / `duplicate_prototile_id` remain
authoritative under `palette_construction_failed`.

the sign is branched on before conversion, so no negative value is ever
reinterpreted as a large unsigned id. supply decodes `-1 → unlimited`,
`> 0 → Supply::finite(...)`; `Supply::finite` returns a `Result` and its verdict
is inspected rather than assumed even though the value is provably positive.

each entry requests exactly `reference, quarter, half, three_quarter` and uses
the catalog-owned exact `Prototile`. orientation compilation is repeated per
palette entry by design; no second `PaletteEntry` constructor was added.

authored color is read by no part of this operation (act 0-3).

### region

```text
1. null RegionResource -> missing_resource
2. outer boundary      -> outer_boundary_invalid + nested PolygonResourceError
                          (an absent outer is nested missing_resource)
3. holes, in order     -> inner_boundary_invalid + hole index + nested error
4. Region::make        -> region_construction_failed + complete RegionError
                          (including its deterministic hole indices)
```

### level

```text
1. null LevelResource -> missing_resource
2. palette            -> palette_invalid, populating only palette_error
3. region             -> region_invalid, populating only region_error
4. engine::Level(palette, region)   — cannot fail
```

id `0` is accepted iff the supplied catalog contains it. the shipped catalog
does not, so the integration check observes `unknown_prototile_id` rather than a
negative-id error for `0`.

## typed persistence

`src/game/resources/LevelPersistence.{h,cpp}` matches the planned surface.

save: reject null → effective path (explicit wins, else `get_path()`) → reject
empty → accept only `.tres`/`.res` case-insensitively → `ResourceSaver::save` →
return the effective path. no compilation, no overwrite confirmation, no
generated filename, no extension switching, no json, no manual subresource
traversal, no path stored in `LevelResource`. `saver_failed` preserves the
non-`OK` `godot::Error`; every earlier failure reports `godot::OK` rather than a
fabricated code.

**one deviation, forced by pinned engine behaviour.** the plan says to "use the
Godot saver flag which changes the resource's owned path after a successful
save". `ResourceSaver::FLAG_CHANGE_PATH` in Godot 4.7.1 sets the path only for
the duration of the write and **restores the previous path afterwards** — this
was observed, not assumed: with the flag alone, `get_path()` was still empty
after an explicit save and three integration checks failed. the flag is still
passed for explicit saves (it governs path resolution during the write), and
ownership is then claimed explicitly with `Resource::take_over_path(path)`. the
specified behaviour — explicit save takes ownership, later pathless save
overwrites the same file, save-as moves ownership — is delivered exactly, and is
checked directly by the integration runner.

load: reject empty path → reject other extensions → `ResourceLoader::load(path,
"LevelResource", CACHE_MODE_IGNORE_DEEP)` → reject null → `Object::cast_to<
LevelResource>` → compile against the catalog → return
`LoadedLevel { resource, level }`. no `godot::Error` is fabricated for the
loader; `loader_failed` and `wrong_resource_type` are separate codes. no
fallback, substitution, repair, or partial publication exists.

## authored fixture

`godot-project/tests/fixtures/canonical_level.tres`, using only the five custom
types and no prototile or catalog resource:

```text
palette entry 0: prototile_id 1, supply  3, color (0.75, 0.5,  0.25, 1)
palette entry 1: prototile_id 2, supply -1, color (0.25, 0.75, 0.5,  1)
outer:  (0, 0) (10, 0) (10, 6.5) (0, 6.5)
hole 0: (2, 2) (4, 2)  (4, 4)    (2, 4)
hole 1: (6, 1.5) (8, 1.5) (8, 3.5) (6, 3.5)
```

`6.5`, `1.5`, and `3.5` are the safely quantizable fractional coordinates the
plan requires: they prove resource geometry is not restricted to the editor's
later integer-grid policy. every fractional value is dyadic, so it survives the
`real_t == float` build exactly and can be compared against exact half-unit
lattice values rather than a tolerance.

exact doubled region area: `2 × (10 × 6.5 − 2 × 2 − 2 × 2) = 114` square game
units. the fixture is not solvable; structural validity and exact compilation
are the contract.

## native tests

`SConstruct`'s dependency-free test tree became
`("src/core", "src/content", "src/engine", "tests")`. `src/content/` is
Godot-free, so the native boundary is unweakened. the extension build already
discovers new `.cpp` files under `src/` and needed no change.

`tests/test_prototile_catalog.cpp` is new. `tests/test_tetromino_state.cpp` was
rewritten to build one canonical catalog per case and pass it to the bootstrap,
keeping every previous assertion and adding two: that every runtime palette
geometry is `same_boundary` with its matching catalog entry, and that a catalog
missing id `3` fails through the typed `catalog_lookup` seam.

totals:

```text
baseline (act 0-1):  2701 checks, 0 failed across 297 cases
delivered:           4295 checks, 0 failed across 336 cases
```

## headless resource integration

`ResourceIntegrationRunner` is one registered `Node` instantiated only by
`godot-project/tests/resource_integration.tscn`. on `_ready()` it runs 220
deterministic checks, reports each failure through `push_error`, prints one
summary line, removes its exact temporary files, and quits the scene tree with
`0` or `1`.

it covers, in order: resource instantiation and defaults; inspector property
types and hints; `changed`-notification behaviour on real and no-op assignments;
polygon compilation including x-before-y precedence, non-finite and out-of-range
`QuantizationError`, and exact fractional quantization; palette compilation
including id `0`, negative ids, unknown ids, null entries, every supply
encoding, empty and duplicate palettes, orientation counts, and colour being
ignored; region compilation including outer-before-hole and hole-order
precedence and nested indexed errors; level palette-before-region precedence;
the authored fixture's authored and compiled values; and the full persistence
matrix.

temporary files live only under `res://.godot/tiles_resource_integration/` and
are named exactly `level.tres`, `level.res`, `saved_as.tres`, `invalid.tres`,
`wrong_type.tres`. each is removed by exact name before the run (in case an
earlier run was interrupted) and after it; the directory is then removed only if
it is empty. nothing recursive and nothing derived from unchecked input is ever
removed. a post-run check confirms every file is gone, and the directory's
absence was verified from the shell after the run.

### round-trip equivalence

authored and compiled equivalence are proven as two separate claims, both by
value and never by pointer identity, and no `operator==` was added to any domain
or resource type:

- authored: entry count and order, each signed id, each signed supply, each
  complete `godot::Color`, outer vertex count/order/components, hole count and
  order, every hole vertex, and the loaded resource's owned path.
- compiled: palette order, each `PrototileId`, each `Supply`, each canonical
  prototile boundary, each distinct orientation's equivalent-orientation
  sequence and exact polygon, region outer boundary, ordered hole boundaries,
  and exact region doubled area.

the text fixture is additionally compared against an identical programmatically
composed region, so the authored file introduces no second representation.

### expected engine output

the runner deliberately loads a `.tres` path that was never written, in order to
show that `loader_failed` is distinct from `wrong_resource_type`. the engine
emits its own three-line diagnostic for that read. this is expected output from
a negative check, not a defect; it is the only non-`[tiles]` output the scene
produces, and there is no way to observe a genuine loader failure without
provoking the engine's own report.

## verification

commands, exactly as run from the repository root:

```text
scons tests
./build/tests/tiles_tests
scons
./Godot.app/Contents/MacOS/Godot --headless --path godot-project \
    res://tests/resource_integration.tscn
./Godot.app/Contents/MacOS/Godot --headless --path godot-project --quit-after 2
```

the integration scene is selected by a bare `res://...` positional argument
after `--path`; no `--editor` and no `--quit-after` is needed, because the runner
quits its own tree.

### `scons tests` / `./build/tests/tiles_tests`

```text
scons: Reading SConscript files ...
Auto-detected 8 CPU cores available for build parallelism. Using 7 cores by default. You can override it with the -j argument.
Building for architecture universal on platform macos
scons: done reading SConscript files.
scons: Building targets ...
scons: `tests' is up to date.
scons: done building targets.

4295 checks, 0 failed across 336 cases
```

### `scons`

```text
scons: Reading SConscript files ...
Auto-detected 8 CPU cores available for build parallelism. Using 7 cores by default. You can override it with the -j argument.
Building for architecture universal on platform macos
scons: done reading SConscript files.
scons: Building targets ...
scons: `vendor/godot-cpp/bin/libgodot-cpp.macos.template_debug.universal.a' is up to date.
scons: `godot-project/bin/macos/libtiles.macos.template_debug.dylib' is up to date.
scons: done building targets.
```

the library builds with no warning from any authored translation unit.

### resource integration scene

```text
Godot Engine v4.7.1.stable.official.a13da4feb - https://godotengine.org

ERROR: Cannot open file 'res://.godot/tiles_resource_integration/never_written.tres'.
   at: load (scene/resources/resource_format_text.cpp:1442)
ERROR: Failed loading resource: res://.godot/tiles_resource_integration/never_written.tres.
   at: _load (core/io/resource_loader.cpp:317)
ERROR: Error loading resource: 'res://.godot/tiles_resource_integration/never_written.tres'.
   at: load (core/core_bind.cpp:82)
[tiles] resource integration: 220 checks passed
exit=0
```

the three `ERROR:` lines are the engine's own report of the deliberate
loader-failure check described above.

### ordinary headless run

```text
Godot Engine v4.7.1.stable.official.a13da4feb - https://godotengine.org

[tiles] canonical catalog constructed: 34 prototiles
[tiles] handcrafted tetromino state constructed: palette order 7
[tiles] debug region: outer bounds x [-8.0, 36.0], y [-30.0, 8.0], 0 holes
[tiles] debug grid: 19 distinct palette orientations placed across 7 columns
[tiles] overlap fixture: a second tetromino o (square 2) was rejected with interior_overlap against placement 19 (expected)
[tiles] overlap fixture: entry count and next_id unchanged by the rejection
[tiles] bootstrap complete: 20 authoritative placements available to draw
[tiles] selection: palette entry 0 (prototile id 1), orientation 0 of 1
[tiles] proposals (initial): full-edge 512 attempted / 28 succeeded, vertex 512 attempted / 243 succeeded, 168 distinct after collapsing 103 duplicate proofs, 45 dropped as point-only contact, 123 offered
[tiles] active proposal: index 4 of 123, handle (36.0, 40.0), cursor (0.0, 0.0)
[tiles] first draw: 20 footprints projected, selection preview projected, active ghost projected
exit=0
```

every count matches the act-0-1 baseline. no integration-runner output appears,
and no error or warning is present.

### grep verification

```text
Godot in src/core, src/content, src/engine   only one pre-existing comment
PrototileResource / PrototileCatalogResource none
production tetromino geometry outside content none (only the debug region rect)
production name/color mapping outside content none
json or bespoke serializer                    none
generated integration files after exit        none
```

## points for the next formalizer or implementor

1. `ResourceSaver::FLAG_CHANGE_PATH` does not persist path ownership in this
   engine version. any future save path must keep the explicit
   `take_over_path`, or the editor's save-as will silently lose its target.
2. `real_t` is `float` in this build. resource vertices therefore carry float
   precision before the single q16.48 quantization. this is fine for the
   integer-grid editor and for dyadic fractions, but a future fractional snap
   interval should prefer dyadic steps so authored values survive the `.tres`
   text round trip exactly.
3. `content::testing::make_catalog` and `canonical_definitions()` exist as
   deliberately narrow seams. if act 1-1 or later wants a runtime-configurable
   catalog, that is a new scope decision, not a use of these.
4. `LoadedLevel` returns the authored resource beside the exact level precisely
   so the player can read per-entry color in the same order the compiler built
   runtime entries. nothing copies color into `src/engine/`, and nothing should.
