# make an editor act 0-2: canonical content and godot level resources

## implementor context seed

this act establishes one canonical Godot-free prototile catalog, the reduced
id-only Godot level-resource graph, exact compilation of those resources into
the runtime model, and typed save/load operations.

it builds on completed act 0 and act 0-1 values. it does not introduce the
interactive level editor or migrate the construction fixture into the real
level player.

seed the implementation session from the repository. this document fixes the
act-0-2 contract but does not replace the established domain definitions,
coordinate rules, completed exact-geometry contracts, or earlier immutable act
plans.

before editing:

1. run `git status --short` and preserve every pre-existing user change;
2. enumerate authored files with `rg --files`, excluding generated build output,
   `Godot.app/`, and `vendor/`;
3. read this document completely;
4. read `README.md`, `DEFINITIONS.md`, `COORDINATES.md`, and
   `docs/make-an-editor/MAKE-AN-EDITOR-ROADMAP.md` completely;
5. read
   `docs/make-an-editor/MAKE-AN-EDITOR-ACT-0-IMPLEMENTATION.md`,
   `docs/make-an-editor/MAKE-AN-EDITOR-ACT-0-1-IMPLEMENTATION.md`, and their
   completed implementation notes completely;
6. read the archived place-tiles plans and implementation notes beneath
   `docs/place-tiles/`, especially acts 2, 3, 3-1, 4, and 4-1;
7. read every authored file beneath `src/core/`, `src/engine/`, `src/game/`, and
   `tests/`, paying particular attention to:
   - `src/core/Result.h`;
   - `src/core/Prototile.{h,cpp}`;
   - `src/core/OrientedPrototile.{h,cpp}`;
   - `src/core/geometry/Coordinate.{h,cpp}`;
   - `src/core/geometry/Polygon.{h,cpp}`;
   - `src/engine/Level.{h,cpp}`;
   - `src/engine/Palette.{h,cpp}`;
   - `src/engine/State.{h,cpp}`;
   - `src/engine/Supply.h`;
   - `src/engine/TetrominoState.{h,cpp}`;
   - `src/game/Editor.{h,cpp}`;
   - `src/register_types.{h,cpp}`;
   - `tests/test_coordinate.cpp`;
   - `tests/test_palette.cpp`;
   - `tests/test_tetromino.cpp`;
   - `tests/test_tetromino_state.cpp`; and
   - `tests/TestHarness.{h,cpp}`;
8. inspect `SConstruct`, `godot-project/main.tscn`,
   `godot-project/project.godot`, and `godot-project/tiles.gdextension`;
9. inspect the pinned Godot 4.7 and godot-cpp headers for every resource,
   registration, property, loader, saver, cache-mode, and filesystem api used;
   do not rely on recalled signatures; and
10. establish the completed act-0-1 baseline by running:

   ```text
   scons tests
   ./build/tests/tiles_tests
   scons
   ./Godot.app/Contents/MacOS/Godot --headless --path godot-project --quit-after 2
   ```

if the act-0 or act-0-1 source or notes are absent, native tests fail, the
GDExtension does not load, or the worktree indicates overlapping implementation
work in files this act needs, stop and ask carson rather than guessing against a
moving interface.

after implementation, record exact catalog definitions, public api decisions,
resource properties and defaults, compiler and persistence error precedence,
changed tests, final test totals, build results, complete integration-runner
output, and complete ordinary headless output in:

```text
docs/make-an-editor/MAKE-AN-EDITOR-ACT-0-2-IMPLEMENTATION-NOTES.md
```

do not commit. do not mention this implementation document in code comments or
runtime-facing documentation.

do not launch a visible Godot window. native tests, a dedicated headless
resource-integration scene, compilation, and the ordinary headless application
run are the verification surfaces.

## goal

introduce these relationships:

```haskell
data CanonicalPrototile = CanonicalPrototile
  { prototile   :: Prototile
  , displayName :: String
  , color       :: Rgb8
  }

data PrototileCatalog = PrototileCatalog
  { entries :: NonEmptyOrderedUniqueBy PrototileId CanonicalPrototile
  }

data PaletteEntryResource = PaletteEntryResource
  { prototileId :: Int64
  , supply      :: Int64
  }

data LevelResource = LevelResource
  { palette :: Ref PaletteResource
  , region  :: Ref RegionResource
  }
```

the application ships one canonical catalog of playable prototiles. the catalog
is Godot-free and owns exact `Prototile` values plus the minimal metadata needed
to present them. the editor introduced next will saturate its catalog view from
this value.

Godot level resources never store prototile geometry, display names, colors, or
catalog resources. a palette entry stores only a canonical prototile id and its
supply. the level compiler resolves that id through the supplied catalog and
constructs the runtime palette with the uniform four-quarter-turn rule.

only per-level region vertices cross the Godot floating-point boundary. every
component is quantized exactly once through the existing q16.48
`quantize_double` operation before polygon validation. no Godot value enters
`src/core/`, `src/content/`, or `src/engine/`.

## architectural boundary

add one Godot-free shipped-content layer:

```text
src/core/
    generic exact geometry and Prototile

src/content/
    canonical playable Prototile values
    stable ids, order, display names, and rgb metadata

src/engine/
    palettes, levels, state, and sessions

src/game/resources/
    mutable Godot resource transport
    exact resource compiler
    typed persistence
```

the dependency direction is:

```text
exact core constructors
          │
          ▼
canonical content catalog
          │
          ├──────────────────────────────┐
          ▼                              ▼
id-only PaletteResource        catalog presentation metadata
          │
          ▼
exact resource compiler
          │
          ▼
engine::Level
```

`src/content/` may include `src/core/` and the c++ standard library. it must not
include or name Godot, game-layer resources, engine state, rendering, input, or
persistence.

`src/core/` remains generic. particular tetrominoes, pentominoes, square sizes,
display names, and colors are shipped content, not new geometry primitives.

`src/engine/` remains independent of the canonical catalog. an engine
`PaletteEntry` continues to own an ordinary `Prototile`; it does not perform
global lookup or depend on application content.

## canonical prototile catalog

add:

```text
src/content/PrototileCatalog.h
src/content/PrototileCatalog.cpp
```

with the conceptual public surface:

```cpp
namespace tiles::content {

struct Rgb8 final {
    std::uint8_t red;
    std::uint8_t green;
    std::uint8_t blue;
};

class CanonicalPrototile final {
public:
    const Prototile &prototile() const;
    const std::string &display_name() const;
    Rgb8 color() const;
};

enum class PrototileCatalogStage {
    polygon,
    prototile,
    catalog,
};

enum class PrototileCatalogErrorCode {
    empty,
    duplicate_prototile_id,
    empty_display_name,
};

struct PrototileCatalogError final {
    PrototileCatalogStage stage;
    std::optional<PrototileId> prototile_id;
    std::optional<PolygonError> polygon_error;
    std::optional<PrototileError> prototile_error;
    std::optional<PrototileCatalogErrorCode> catalog_error;
};

class PrototileCatalog final {
public:
    const std::vector<CanonicalPrototile> &entries() const;
    const CanonicalPrototile *find(PrototileId p_id) const;
};

Result<PrototileCatalog, PrototileCatalogError>
make_canonical_prototile_catalog();

} // namespace tiles::content
```

equivalent spelling is permitted only when it preserves the ownership,
failure, lookup, and metadata contracts below.

the catalog owns every entry and every entry owns its exact `Prototile` by
value. all observation is const. `find` returns a pointer into the catalog or
`nullptr` when no canonical entry has the requested id. no lookup inserts,
constructs, logs, falls back, aliases one id to another, or returns a sentinel
entry.

catalog order is presentation order and is stable. lookup does not affect it.
construction proves:

- the catalog is nonempty;
- every exact polygon and prototile constructs through the normal core
  factories;
- every id is unique; and
- every display name is nonempty.

if a general private catalog constructor is useful for tests, it must validate
those same invariants. do not expose a public mutation or arbitrary production
catalog constructor merely to shorten fixtures.

### initial canonical content

this act migrates exactly the seven existing one-sided tetromino definitions
from the temporary `TetrominoState` bootstrap into the canonical catalog. it
does not add pentominoes, unit-square content, or further square sizes.

preserve these stable ids and this order:

```text
id  display name  geometry
1   o             existing 2 × 2 o tetromino
2   i             existing 4 × 1 i tetromino
3   t             existing t tetromino
4   s             existing s tetromino
5   z             existing z tetromino
6   j             existing j tetromino
7   l             existing l tetromino
```

the exact game-unit boundary rings are the existing production rings from
`make_tetromino_state()`. move that source of truth; do not transcribe a second
production copy and leave the old one live.

migrate the current debug presentation colors into Godot-free 8-bit metadata:

```text
o  #f2d933
i  #33cceb
t  #a859db
s  #47c757
z  #e33d3d
j  #3d70eb
l  #f58c24
```

Godot presentation converts each channel with `channel / 255.0`. the catalog
stores no `godot::Color`, float color, alpha, material, texture, or drawing
policy.

the `o` tetromino is also the canonical side-2 square geometry. do not add a
second congruent side-2-square identity in this act. later square sizes are
distinct prototiles because scaling changes geometry and therefore identity.

### catalog construction failures

visit definitions in catalog order.

for each definition:

1. construct the exact whole-game-unit boundary points;
2. call `Polygon::make`;
3. call `Prototile::make`; and
4. append only the complete canonical entry.

return the first failure with its stable intended id and complete underlying
error. polygon and prototile stages populate only their corresponding native
error. a catalog-table invariant failure populates only `catalog_error`, plus a
prototile id when the failure belongs to one entry. no partial catalog escapes.

the fixed table is expected to be unique and nonempty. still enforce those
facts inside construction rather than publishing an unchecked value. a catalog
table defect reports the `catalog` stage and does not assert a usable catalog
into existence.

all multiplication used to form whole-game-unit coordinates must be checked or
be compile-time proven within range. do not use a floating conversion to build
canonical content.

## temporary tetromino bootstrap migration

refactor `make_tetromino_state` to accept the canonical catalog by const
reference and resolve ids `1` through `7` from it. construct the temporary
unlimited palette in canonical catalog order, then preserve the completed act-0
debug region and typed placement bootstrap.

conceptually:

```cpp
Result<State, TetrominoStateError> make_tetromino_state(
    const content::PrototileCatalog &p_catalog);
```

the bootstrap must not retain another tetromino boundary table, name table, or
color table. a missing expected canonical id is a typed bootstrap failure,
although the shipped factory and catalog tests make it unreachable in ordinary
execution.

update the temporary `Editor` to own one successfully constructed catalog,
pass it into `make_tetromino_state`, and resolve arrangement colors and readable
labels from that same catalog. remove the `color_for(PrototileId)` switch and
any duplicated display-name mapping.

preserve the existing debug arrangement, selection, proposal, draw, and
diagnostic behavior apart from the explicitly expected color values being the
8-bit canonical equivalents above. this migration does not turn `Editor` into
`LevelEditor` or `LevelPlayer`.

## godot resource graph

add:

```text
src/game/resources/LevelResources.h
src/game/resources/LevelResources.cpp
```

define and register exactly these five custom `Resource` subclasses:

```text
PolygonResource
PaletteEntryResource
PaletteResource
RegionResource
LevelResource
```

one header/source pair is intentional. these are small transport classes with
closely related registration and no independent domain behavior.

every class:

- derives directly from `godot::Resource`;
- uses `GDCLASS`;
- binds every getter and setter;
- exposes the properties below to the inspector;
- stores values by ordinary Godot value or `Ref`;
- calls `emit_changed()` after a property actually changes;
- remains default-constructible for Godot; and
- performs no exact compilation, validation, logging, saving, or loading in a
  setter.

resources are mutable authored transport. a default or partially edited
resource may be invalid. no custom resource constructor may assert domain
validity.

### polygon resource

```haskell
data PolygonResource = PolygonResource
  { vertices :: PackedVector2Array
  }
```

property:

```text
vertices: PackedVector2Array
```

the default is empty. the closing vertex remains implicit: authors must not
repeat the first vertex at the end. the compiler, not the setter, diagnoses an
invalid ring.

`PolygonResource` is used only for per-level region boundaries. no catalog
prototile refers to it.

### palette-entry resource

```haskell
data PaletteEntryResource = PaletteEntryResource
  { prototileId :: Int64
  , supply      :: Int64
  }
```

properties and defaults:

```text
prototile_id: int = 0
supply:       int = -1
```

id `0` is a valid resource encoding and is compiled normally. whether it exists
in the canonical catalog is a separate lookup question. negative ids are
invalid.

supply encoding is:

```text
-1  = unlimited
> 0 = finite supply
0   = invalid
< -1 = invalid
```

the default `-1` makes a newly selected valid catalog id unlimited without
inventing a zero-supply palette entry.

there is no prototile resource reference, polygon, name, color, orientation
rule, scale, reflection, or arbitrary metadata property.

### palette resource

```haskell
data PaletteResource = PaletteResource
  { entries :: TypedArray PaletteEntryResource
  }
```

property:

```text
entries: Array[PaletteEntryResource]
```

the default is an empty correctly typed array. authored order is preserved
exactly and becomes runtime palette order after successful compilation.

### region resource

```haskell
data RegionResource = RegionResource
  { outerBoundary   :: Ref PolygonResource
  , innerBoundaries :: TypedArray PolygonResource
  }
```

properties and defaults:

```text
outer_boundary:   PolygonResource = null
inner_boundaries: Array[PolygonResource] = []
```

hole order is preserved as authored. null hole entries are representable
transport values and fail compilation with their exact index.

### level resource

```haskell
data LevelResource = LevelResource
  { palette :: Ref PaletteResource
  , region  :: Ref RegionResource
  }
```

properties and defaults:

```text
palette: PaletteResource = null
region:  RegionResource = null
```

the level resource contains no arrangement, history, completion flag, known
solution, catalog reference, rotation rule, display metadata, or save-path
property. Godot's inherited resource path is its persistence ownership.

## registration

register all five resource classes at scene initialization before registering
any node which constructs or loads them.

inspector properties must use the correct Godot 4.7 variant types and
resource-type hints. typed arrays must remain typed after default construction,
setter assignment, save, and reload.

registration order is:

```text
PolygonResource
PaletteEntryResource
PaletteResource
RegionResource
LevelResource
integration test runner
Editor
```

the integration runner is registered only because its dedicated scene
instantiates it. registration does not place it in the main application scene.

do not bind core, content, or engine values to Godot. only resource and
integration node classes enter `ClassDB`.

## exact resource compilation

add:

```text
src/game/resources/ResourceCompiler.h
src/game/resources/ResourceCompiler.cpp
```

expose independently reusable polygon, palette, region, and level compilation.
act 1 needs to validate region and palette drafts separately; it must not
reimplement a subset of the level compiler.

the conceptual surface is:

```cpp
Result<Polygon, PolygonResourceError> compile_polygon_resource(
    const godot::Ref<PolygonResource> &p_resource);

Result<engine::Palette, PaletteResourceError> compile_palette_resource(
    const godot::Ref<PaletteResource> &p_resource,
    const content::PrototileCatalog &p_catalog);

Result<Region, RegionResourceError> compile_region_resource(
    const godot::Ref<RegionResource> &p_resource);

Result<engine::Level, LevelResourceError> compile_level_resource(
    const godot::Ref<LevelResource> &p_resource,
    const content::PrototileCatalog &p_catalog);
```

equivalent parameter order or naming is permitted. all four operations must be
const with respect to resources and catalog and return owned exact values.

compilation:

- does not mutate, canonicalize, reorder, replace, or emit changes from a
  resource;
- does not cache an exact value inside a mutable resource;
- does not use a resource path or pointer identity as domain identity;
- does not log expected invalid authored input;
- never converts exact core geometry back into a resource;
- does not accept a fallback prototile for an unknown id; and
- never enters an arrangement or play session.

### polygon compilation

define:

```cpp
enum class CoordinateAxis {
    x,
    y,
};

enum class PolygonResourceErrorCode {
    missing_resource,
    coordinate_quantization_failed,
    polygon_construction_failed,
};

struct PolygonResourceError final {
    PolygonResourceErrorCode code;
    std::optional<std::size_t> vertex;
    std::optional<CoordinateAxis> axis;
    std::optional<QuantizationError> quantization_error;
    std::optional<PolygonError> polygon_error;
};
```

payload population is exact:

- `missing_resource` populates no optional payload;
- `coordinate_quantization_failed` populates `vertex`, `axis`, and
  `quantization_error`; and
- `polygon_construction_failed` populates only `polygon_error`.

visit vertices in authored order. for each vertex, quantize x and then y by
converting the Godot component to `double` and calling the existing
`quantize_double`.

do not:

- multiply a `real_t` by `Coordinate::SCALE` in the game layer;
- cast directly to coordinate storage;
- use Godot rounding;
- clamp;
- reject safely quantizable fractional values;
- require integer game coordinates;
- select an epsilon; or
- pre-canonicalize vertex order.

after every component succeeds, call `Polygon::make` once with the owned exact
points. preserve its complete error.

### palette compilation

define:

```cpp
enum class PaletteResourceErrorCode {
    missing_resource,
    missing_entry,
    negative_prototile_id,
    unknown_prototile_id,
    invalid_supply,
    orientation_compilation_failed,
    palette_construction_failed,
};

struct PaletteResourceError final {
    PaletteResourceErrorCode code;
    std::optional<std::size_t> entry;
    std::optional<std::int64_t> encoded_prototile_id;
    std::optional<std::int64_t> encoded_supply;
    std::optional<PrototileId> prototile_id;
    std::optional<LatticeOrientationError> orientation_error;
    std::optional<engine::PaletteError> palette_error;
};
```

every failure tied to one authored entry populates `entry`. id failures populate
the signed encoded value. after a nonnegative value is converted,
unknown-id and later entry failures also populate the strong `PrototileId`.
invalid supply populates its signed encoding. orientation and palette failures
preserve the complete native errors.

visit entries in authored order. for each entry:

1. reject a null entry;
2. reject a negative `prototile_id`;
3. convert the nonnegative signed value to `PrototileId::Value` without an
   unchecked signed/unsigned cast;
4. resolve that exact id through `PrototileCatalog::find`;
5. reject an unknown id;
6. decode supply;
7. call `PaletteEntry::make` with the catalog-owned exact prototile and exactly
   these requested orientations:

   ```text
   Orientation::reference()
   Orientation::quarter()
   Orientation::half()
   Orientation::three_quarter()
   ```

8. append the complete entry.

supply decoding is:

```text
-1  -> Supply::unlimited()
> 0 -> Supply::finite(static_cast<Supply::Amount>(value))
0 or < -1 -> invalid_supply
```

because a Godot `int` is signed 64-bit, every positive resource supply and every
nonnegative resource id is representable in the corresponding unsigned 64-bit
domain. nevertheless, branch on the sign before conversion. never cast a
negative value and reinterpret it as a large unsigned amount.

after all entries compile, call `Palette::make` once. its `empty` and
`duplicate_prototile_id` errors remain authoritative.

orientation compilation is intentionally repeated for palette entries rather
than stored in the catalog. the catalog owns canonical prototiles; the engine
palette owns its compiled admitted orientations. do not introduce a second
`PaletteEntry` constructor or expose mutable compiled geometry merely to avoid
this small deterministic compilation.

### region compilation

define:

```cpp
enum class RegionResourceErrorCode {
    missing_resource,
    outer_boundary_invalid,
    inner_boundary_invalid,
    region_construction_failed,
};

struct RegionResourceError final {
    RegionResourceErrorCode code;
    std::optional<std::size_t> hole;
    std::optional<PolygonResourceError> polygon_error;
    std::optional<RegionError> region_error;
};
```

precedence is:

1. reject a null `RegionResource`;
2. compile `outer_boundary`;
3. compile holes in authored order; then
4. call `Region::make`.

an absent outer boundary is reported as `outer_boundary_invalid` containing
`PolygonResourceError::missing_resource`.

an absent or invalid hole is reported as `inner_boundary_invalid`, with its
authored index and complete nested polygon error.

a relationship failure among otherwise valid polygons is
`region_construction_failed` with the complete act-0 `RegionError`, including
its deterministic hole indices.

### level compilation

define:

```cpp
enum class LevelResourceErrorCode {
    missing_resource,
    palette_invalid,
    region_invalid,
};

struct LevelResourceError final {
    LevelResourceErrorCode code;
    std::optional<PaletteResourceError> palette_error;
    std::optional<RegionResourceError> region_error;
};
```

precedence is:

1. reject a null `LevelResource`;
2. compile the palette;
3. compile the region; then
4. construct `engine::Level`.

palette failure populates only `palette_error`; region failure populates only
`region_error`. successful construction cannot fail because both members are
already proof-bearing exact values.

the compiler accepts id `0` if and only if the supplied canonical catalog
contains it. the initial shipped catalog does not.

## typed persistence

add:

```text
src/game/resources/LevelPersistence.h
src/game/resources/LevelPersistence.cpp
```

with the conceptual surface:

```cpp
enum class SaveLevelErrorCode {
    missing_resource,
    path_required,
    unsupported_extension,
    saver_failed,
};

struct SaveLevelError final {
    SaveLevelErrorCode code;
    godot::Error godot_error;
};

Result<godot::String, SaveLevelError> save_level_resource(
    const godot::Ref<LevelResource> &p_resource,
    const godot::String &p_explicit_path = godot::String());

enum class LoadLevelErrorCode {
    path_required,
    unsupported_extension,
    loader_failed,
    wrong_resource_type,
    compilation_failed,
};

struct LoadLevelError final {
    LoadLevelErrorCode code;
    std::optional<LevelResourceError> compilation_error;
};

struct LoadedLevel final {
    godot::Ref<LevelResource> resource;
    engine::Level level;
};

Result<LoadedLevel, LoadLevelError> load_level_resource(
    const godot::String &p_path,
    const content::PrototileCatalog &p_catalog);
```

equivalent spelling is permitted only when it preserves path ownership,
uncached loading, underlying save error, typed compilation error, and return of
both resource and exact level.

### save behavior

save deliberately does not compile or validate the resource. carson does not
intend to author invalid saved states, and duplicating the compilation gate in
persistence adds no required protection. validation remains available through
the explicit compiler and occurs during load and play.

save behavior is:

1. reject a null resource;
2. choose the effective path:
   - a nonempty explicit path wins;
   - otherwise use `resource->get_path()`;
3. reject an empty effective path;
4. accept exactly `.tres` and `.res`, case-insensitively;
5. call `ResourceSaver`; and
6. return the effective path only on success.

when an explicit path is supplied, use the Godot saver flag which changes the
resource's owned path after a successful save. this is both initial save and
save-as behavior. a later call without an explicit path overwrites that owned
path.

when no explicit path is supplied, save to the already owned path without
changing its identity.

preserve a non-`OK` `godot::Error` in `SaveLevelError::godot_error`.
non-saver failures use `godot::OK` in that field; do not invent a fake engine
error code.

do not:

- compile first;
- add overwrite confirmation;
- generate a default filename;
- silently switch extensions;
- serialize json;
- manually traverse or save subresources;
- store the path in `LevelResource`; or
- claim transactional filesystem behavior Godot does not provide.

overwrite confirmation and path-picking interface belong to act 1.

### load behavior

load behavior is:

1. reject an empty path;
2. reject an extension other than `.tres` or `.res`;
3. call `ResourceLoader::load` with the `LevelResource` type hint and the pinned
   Godot 4.7 deep cache-ignore mode;
4. reject a null loaded resource;
5. dynamically cast to `LevelResource` and reject a wrong type;
6. compile it against the supplied canonical catalog; and
7. return the exact loaded `Ref<LevelResource>` together with the exact compiled
   `engine::Level`.

cache bypass is load semantics for this authoring milestone: asking to load a
path observes its persisted contents, not an older cached object or the
in-memory resource which was just saved there.

`ResourceLoader::load` does not return a complete `godot::Error` through the
pinned api. do not fabricate one. distinguish loader failure from wrong type and
preserve the complete compiler failure when compilation fails.

loading performs no fallback to the editor's current resource, no catalog
substitution, no repair, and no partial exact level publication.

## authored integration fixture

add one small text resource:

```text
godot-project/tests/fixtures/canonical_level.tres
```

it contains:

- a two-entry palette in authored order:
  - canonical id `1` (`o`) with finite supply `3`;
  - canonical id `2` (`i`) with unlimited supply;
- one valid outer polygon;
- at least one valid hole;
- no arrangement or known solution; and
- only the five custom resource types from this act.

use safely quantizable fractional coordinates in at least one outer or hole
vertex. the fixture proves that resource coordinates are not restricted to the
editor's later integer-grid input policy.

the fixture need not be solvable. structural resource validity and exact
compilation are the contract.

do not create prototile or catalog `.tres` files.

## native tests

add:

```text
tests/test_prototile_catalog.cpp
```

update `SConstruct` so the dependency-free native test target includes
`src/content/` in addition to `src/core/`, `src/engine/`, and `tests/`.
`src/content/` is Godot-free, so this does not weaken the native boundary.

### catalog coverage

test:

- canonical catalog construction succeeds;
- the catalog is nonempty;
- exact order is `o, i, t, s, z, j, l`;
- exact ids are `1` through `7`;
- every id is unique;
- every display name is nonempty and matches the table;
- every rgb value matches the fixed metadata;
- `find` returns the exact matching entry for all seven ids;
- unknown ids, including `0`, return `nullptr`;
- lookup does not change catalog order or values;
- each exact boundary matches the existing tetromino geometry;
- every canonical prototile is translation-normalized;
- each entry compiles the four requested quarter turns;
- distinct orientation counts remain `1, 2, 4, 2, 2, 4, 4`;
- chiral pairs remain geometrically distinct under admitted rotations; and
- a copied or moved catalog retains exact lookup and order.

if a private general catalog-construction seam is added, test empty, duplicate
id, empty name, polygon failure, and prototile failure through that seam.
otherwise, do not expose one solely to synthesize impossible shipped-table
failures.

### bootstrap migration coverage

update tetromino-state tests to construct one canonical catalog and pass it to
the bootstrap.

test:

- the same seven-entry unlimited palette is produced in canonical order;
- each runtime palette geometry comes from the matching catalog id;
- orientation counts remain unchanged;
- the completed act-0 debug region remains unchanged;
- missing expected bootstrap ids fail through the typed seam if such a
  constructible test seam exists; and
- no production tetromino geometry or metadata table remains outside
  `src/content/`.

do not weaken unrelated exact core, engine, region, state, session, or proposal
tests.

## headless resource integration

add:

```text
src/game/testing/ResourceIntegrationRunner.h
src/game/testing/ResourceIntegrationRunner.cpp
godot-project/tests/resource_integration.tscn
```

the runner is a minimal registered Godot node instantiated only by the
dedicated test scene. on `_ready()` it performs deterministic checks, prints one
concise summary on success, reports each failed assertion through
`push_error`, and exits its scene tree with a nonzero code if any check fails.

it must not enter the main scene, run during ordinary application startup, open
a window, or become a general test framework.

verify:

- all five resource classes instantiate;
- every default and property type matches this plan;
- typed arrays remain correctly typed;
- setters/getters preserve values and emit a changed notification where
  observable without timing assumptions;
- a programmatically composed valid level compiles;
- resource id `0` reaches canonical lookup and reports unknown id rather than a
  negative-id error;
- a negative id reports its signed value and entry index;
- an unknown positive id reports its strong id and entry index;
- null palette entries report their authored index;
- supply `-1`, positive finite, `0`, and less-than-`-1` follow the exact
  encoding;
- empty and duplicate palettes preserve `PaletteError`;
- x-before-y, vertex-order, entry-order, outer-before-hole, hole-order,
  palette-before-region, and nested-error precedence;
- non-finite and out-of-range region coordinates preserve
  `QuantizationError`;
- safely quantizable fractional region coordinates compile exactly;
- polygon and region construction errors remain nested and indexed;
- the authored fixture loads uncached and compiles;
- its palette resolves ids `1` and `2` to the canonical exact geometries;
- its finite and unlimited supplies compile exactly;
- its outer boundary, ordered holes, and doubled area are exact;
- its presentation names and colors are obtainable from the same catalog by
  runtime palette ids;
- saving a newly composed resource without a path reports `path_required`;
- `.tres` and `.res` are both accepted;
- another extension is rejected;
- explicit save gives the resource an owned path;
- a later pathless save overwrites that path;
- save-as changes ownership to the new path;
- persisted nested resources reload as the correct custom types;
- uncached reload returns authored values equivalent field by field;
- compiled pre-save and post-load levels have equivalent exact palette and
  region values;
- wrong resource type is distinct from loader failure;
- a persisted structurally invalid level can be saved and then fails load at
  the typed compilation stage, proving save performs no hidden validation; and
- every expected temporary file is removed after the check.

use exact temporary paths beneath:

```text
res://.godot/tiles_resource_integration/
```

create only that narrow generated directory if required. remove only the exact
temporary files created by the runner, then remove that directory if empty.
never recursively remove `.godot`, `godot-project`, `res://`, `user://`, or a
path derived from unchecked input.

if the runner is interrupted and leaves one of its exact named temporary files,
it may remove that exact file before starting the next run.

the integration runner may use small local check helpers but must inspect every
`Result`, `Ref`, Godot return code, and array cast before accessing success
values.

## resource round-trip equivalence

round-trip equivalence does not use pointer identity.

compare authored resource values:

- palette entry count and order;
- each signed prototile id;
- each signed supply encoding;
- outer vertex count, order, and component values;
- hole count and order;
- every hole vertex count, order, and component value; and
- the loaded resource's owned path.

compare compiled exact values:

- runtime palette order;
- each exact `PrototileId`;
- each supply;
- each canonical prototile boundary;
- each distinct orientation sequence and exact polygon;
- region outer canonical boundary;
- ordered canonical hole boundaries; and
- exact region doubled area.

do not add general `operator==` overloads to domain or resource types merely to
shorten the integration runner.

resource values are not rewritten into core-canonical vertex order during save.
authored resource equivalence and compiled exact equivalence are separate
claims, and both must pass.

## source and build layout

expected new authored files are:

```text
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
docs/make-an-editor/MAKE-AN-EDITOR-ACT-0-2-IMPLEMENTATION-NOTES.md
```

expected modifications include:

```text
SConstruct
src/engine/TetrominoState.h
src/engine/TetrominoState.cpp
src/game/Editor.h
src/game/Editor.cpp
src/register_types.cpp
tests/test_tetromino_state.cpp
```

small changes to a shared test helper are permitted when directly useful for
exact resource-compiler comparisons.

do not change:

- core `Prototile` identity or construction semantics;
- engine `Palette`, `Level`, `State`, or `Session` ownership;
- the main scene's node structure;
- project display or rendering settings;
- coordinate representation;
- region semantics;
- arrangement or command behavior; or
- the `.gdextension` descriptor.

the ordinary extension source discovery already finds new `.cpp` files beneath
`src/`. change only the native test-source tree list in `SConstruct` so it also
discovers Godot-free `src/content/*.cpp`.

## diagnostics

the canonical catalog factory, resource compiler, save operation, and load
operation return typed errors. they do not log ordinary invalid input.

the temporary `Editor` may report one concise fatal bootstrap error if canonical
catalog construction fails. update its existing typed diagnostic visitor rather
than flattening catalog construction into a boolean.

the dedicated integration runner reports test failures because it is a test
surface. successful resource operations in production emit no logs.

preserve the ordinary headless application's established concise bootstrap,
selection, proposal, and draw diagnostics. do not add one line per resource,
catalog lookup, vertex, orientation, save subresource, or load stage.

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

use the exact pinned Godot 4.7 command form proven by inspection and execution;
if selecting the integration scene requires `--path ... --editor` or
`--path ... res://...` ordering different from the conceptual command above,
record the verified command in the implementation notes.

independently compile new or changed translation units under the repository's
established c++17 and no-exception assumptions if the ordinary build does not
make warning provenance clear.

grep-verify:

- no Godot include, type, resource name, or identifier entered `src/core/`,
  `src/content/`, or `src/engine/`;
- no `PrototileResource` or `PrototileCatalogResource` exists;
- no production tetromino geometry table remains outside `src/content/`;
- no production prototile color/name mapping remains outside the canonical
  catalog;
- no json or bespoke text serializer was introduced; and
- no generated integration file remains after the runner exits.

the native suite must prove canonical content independently of Godot.

the integration scene must:

- load the GDExtension and all custom resource classes;
- complete every resource, compiler, persistence, and round-trip check;
- remove its exact generated files;
- print its success summary;
- contain no unexpected error or warning; and
- exit successfully.

the ordinary headless run must:

- construct the canonical catalog;
- construct the temporary tetromino state from that catalog;
- preserve completed region, arrangement, selection, proposal, and draw
  behavior;
- contain no resource integration runner output;
- contain no unexpected error or warning; and
- exit successfully.

## acceptance

act 0-2 is complete when all of the following hold:

- one Godot-free canonical prototile catalog is the sole production source of
  playable prototile geometry, ids, order, names, and colors;
- the catalog owns exact `Prototile` values and provides const lookup by stable
  `PrototileId`;
- the seven existing one-sided tetrominoes retain ids `1` through `7`, exact
  geometry, order, orientation counts, and canonical metadata;
- no duplicate tetromino geometry or presentation table remains in the
  temporary bootstrap;
- the generic core remains unaware of shipped catalog content;
- exactly five registered resource classes represent polygon, palette entry,
  palette, region, and level transport;
- level resources serialize prototile ids rather than prototile resources or
  prototile vertices;
- id `0` is a valid encoding and negative ids are rejected before unsigned
  conversion;
- unknown ids fail exact catalog lookup without fallback;
- resource supply encoding is exactly `-1` or positive;
- palette compilation requests exactly the four quarter turns and preserves
  distinct-orientation collapse;
- only region coordinates are quantized from Godot values;
- every region component uses the existing deterministic q16.48 quantizer
  exactly once;
- polygon, palette, region, and level compilation are independently reusable,
  pure, typed operations;
- compiler errors preserve exact authored indices, precedence, and underlying
  native errors;
- saving supports explicit initial path, existing owned path, save-as, `.tres`,
  and `.res`;
- save deliberately does not validate or compile;
- load bypasses stale cache, restores the exact custom resource graph, and
  compiles before publishing a `LoadedLevel`;
- a loaded result contains both the authored `Ref<LevelResource>` and equivalent
  exact `engine::Level`;
- authored and compiled round-trip equivalence are both proven;
- one fractional, hole-bearing, id-only level fixture loads and compiles;
- the core, content, and engine layers remain Godot-free;
- all native tests pass;
- the GDExtension builds cleanly;
- the dedicated headless resource integration passes and cleans up;
- the ordinary headless construction surface still initializes and draws;
  and
- implementation notes contain exact catalog data, algorithms, properties,
  totals, commands, and complete outputs.

## explicitly out of scope

act 0-2 does not implement:

- `LevelEditor`;
- `LevelPlayer`;
- `ApplicationHost`;
- the typed play request;
- region drawing or integer-grid input;
- palette controls;
- path-picking dialogs;
- overwrite confirmation;
- authoring history;
- play history wiring;
- catalog persistence;
- a `PrototileCatalogResource`;
- a `PrototileResource`;
- resource-authored prototile geometry, names, or colors;
- runtime catalog mutation;
- mod or content-pack loading;
- user-defined prototiles;
- pentomino catalog entries;
- unit-square or additional square-size catalog entries;
- procedural scaling or a scale field in any resource;
- reflected or non-quarter-turn variants;
- per-level rotation rules;
- disconnected positive regions;
- json or another serialization format;
- validation before save;
- invalid-resource repair;
- schema migration;
- compatibility guarantees beyond stable ids needed by the current project;
- arrangement, history, solution, or completion serialization;
- known-solution storage;
- campaign metadata or level selection;
- visible resource-test UI; or
- a general Godot test framework.

if implementing the custom resources reveals that the pinned Godot 4.7 property
system cannot persist the typed graph above, stop and report the smallest
failing property and observed engine behavior. do not silently serialize
prototile geometry, replace typed references with dictionaries, introduce json,
or move Godot types into the exact layers.

if canonical catalog construction reveals that an existing production
tetromino definition does not pass the completed exact constructors, stop and
report the exact id, ring, stage, and error. do not change stable geometry or
weaken validation merely to make shipped content construct.
