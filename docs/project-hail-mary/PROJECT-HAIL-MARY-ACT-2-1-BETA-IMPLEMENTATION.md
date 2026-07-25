# project hail mary act 2-1 beta: proof-carrying LevelResource

## immutable implementation contract

this document is immutable once handed to an implementor. do not revise it to
match implementation choices. stop and ask carson if a genuine contradiction
or behavior-changing ambiguity remains.

record results in:

```text
docs/project-hail-mary/PROJECT-HAIL-MARY-ACT-2-1-BETA-IMPLEMENTATION-NOTES.md
```

do not mention this plan or project act in code comments or runtime
documentation. do not commit.

## prerequisite and context seed

project-hail-mary acts 0, 1, 2, and alpha must be implemented and passing. beta
consumes only their public contracts:

- `content::GeometryDomain`;
- domain-aware palette-resource compilation;
- `engine::BlueprintPlacement` and `compile_blueprint`;
- `region_from_arrangement`;
- exact `Palette`, `Arrangement`, `Region`, `Level`, and `State`; and
- existing Godot resource registration, persistence, and headless integration.

before editing:

1. inspect and preserve concurrent work;
2. read the project-hail-mary roadmap, immutable acts 0, 1, 2, and alpha, and
   every implementation note;
3. read the complete resource types, compiler, persistence helpers,
   registration, fixture, and resource-integration runner;
4. read `GeometryDomain`, `Orientation`, `Palette`, `Blueprint`, `Level`,
   `State`, commands, `ArrangementRegion`, and their complete tests; and
5. establish the current native, GDExtension, resource-integration,
   editor-integration, and ordinary headless baselines.

if alpha's public region compiler is missing or materially different, stop. do
not duplicate boundary derivation inside the Godot layer.

beta does not consume or edit `LevelEditor`. gamma consumes beta after beta
publishes and records the complete resource boundary.

## goal

replace the old region-first resource graph with one versioned, self-contained,
proof-carrying level artifact:

```text
LevelResource
    ├── format_version
    ├── geometry_domain
    ├── PaletteResource
    │     └── [PaletteEntryResource]
    └── blueprint
          └── [BlueprintPlacementResource]
```

the resource stores the exact authoring witness:

```text
domain + palette transport + blueprint records
```

it stores no independently authored region. the only runtime region is derived
by compiling the blueprint into an arrangement and passing that arrangement
through alpha.

the complete one-way flow is:

```text
versioned LevelResource
        │
        ▼
domain-aware exact Palette
        │
        ▼
exact BlueprintPlacement records
        │
        ▼
proof-bearing Arrangement
        │
        ▼
exact derived Region
        │
        ▼
CompiledLevelResource
        ├── authoring witness proof
        └── runtime engine::Level
```

## resource graph

modify:

```text
src/game/resources/LevelResources.h
src/game/resources/LevelResources.cpp
src/register_types.cpp
```

retain `PaletteEntryResource` and `PaletteResource` with their existing property
meaning:

```text
PaletteEntryResource
    ├── prototile_id : signed Godot integer
    ├── supply       : -1 unlimited | positive finite
    └── color        : presentation only

PaletteResource
    └── entries      : Array[PaletteEntryResource]
```

entry order remains authored palette order.

### format version

publish one shared current-format constant equivalent to:

```cpp
constexpr std::int64_t LEVEL_RESOURCE_FORMAT_VERSION = 1;
```

`LevelResource::format_version` is a signed Godot integer:

```text
default  = 1
accepted = exactly 1
```

its setter is transport-only. it preserves every signed value and emits
`changed` exactly once for an actual change.

future incompatible schemas increment the constant. beta implements no
migration, compatibility range, best-effort interpretation, or fallback.

### geometry domain

`LevelResource::geometry_domain` is a signed Godot integer:

```text
0 = lattice
1 = hex12
```

default is lattice.

the setter preserves every other signed value so compilation can reject it. it
does not cast, validate, compile, clamp, or log.

### BlueprintPlacementResource

register one custom `Resource` with signed Godot integer properties:

```text
prototile_id
orientation_step
orientation_order
translation_x_raw
translation_y_raw
```

defaults:

```text
prototile_id       = 0
orientation_step   = 0
orientation_order  = 1
translation_x_raw  = 0
translation_y_raw  = 0
```

each setter:

- preserves every signed value;
- validates and compiles nothing;
- returns without notification when the value is unchanged; and
- emits `changed` exactly once for an actual change.

translations are authoritative signed q16.48 raw integers. they never pass
through `Vector2`, decimal text, multiplication by `Coordinate::SCALE`,
quantization, or rendered geometry.

### LevelResource

replace the old `region` property with:

```text
format_version  : int
geometry_domain : int
blueprint       : Array[BlueprintPlacementResource]
```

retain:

```text
palette : PaletteResource
```

the resource owns no compiled palette, records, arrangement, region, level,
history, player progress, export result, dirty state, editor state, or path
beyond inherited Godot resource identity.

### removed resources

delete:

- `PolygonResource`;
- `RegionResource`;
- their registration;
- their compiler operations and error types;
- their fixture graph; and
- their resource-integration checks.

no retained type or function may preserve independently authored region
vertices. old development `.tres` compatibility is explicitly absent.

## external size boundaries

publish:

```cpp
constexpr std::size_t MAX_BLUEPRINT_PLACEMENTS = 64;
```

a blueprint resource array larger than 64 is rejected before reserving the
decoded record vector or inspecting any record.

alpha and `compile_blueprint` remain mathematically uncapped. this is an
external transport boundary which limits work before the quadratic arrangement
and boundary compilers run.

after a domain is known, reject a palette resource array whose length exceeds
the number of canonical identities available in that domain. this happens
before inspecting or compiling an individual palette entry. a valid palette
cannot exceed that count because `Palette` requires distinct ids.

do not add another arbitrary byte, vertex, edge, coordinate, supply, or color
limit.

## blueprint resource decoding

modify:

```text
src/game/resources/ResourceCompiler.h
src/game/resources/ResourceCompiler.cpp
```

publish:

```cpp
Result<std::vector<engine::BlueprintPlacement>, BlueprintResourceError>
compile_blueprint_resource(
    const godot::TypedArray<BlueprintPlacementResource> &p_resources);
```

decode records in array order. check the 64-record limit before any record.

per-record validation precedence is:

1. null placement resource;
2. negative prototile id;
3. orientation step below zero or above `UINT32_MAX`;
4. orientation order below zero or above `UINT32_MAX`;
5. `Orientation::make(step, order)` failure; and
6. direct raw-coordinate construction.

zero is representable by `std::uint32_t`, so encoded order zero reaches
`Orientation::make` and preserves `OrientationError::zero_order`. do not reject
zero as an out-of-range signed transport value.

the typed error publishes:

- a top-level `too_many_placements` alternative carrying actual and maximum
  counts; or
- the failing record index, exact code, and only the signed encoded values or
  exact `OrientationError` relevant to that code.

every nonnegative signed prototile id fits the current unsigned strong id.
translations accept every signed `int64_t` bit pattern through
`Coordinate::from_raw`.

`Orientation::make` canonicalizes the rational turn. later
`compile_blueprint` requires equality with one compiled variant's representative
orientation. an equivalent but nonrepresentative label therefore decodes
successfully and later fails as `orientation_not_in_palette`.

## compiled authoring product

publish:

```cpp
struct CompiledLevelResource final {
    content::GeometryDomain domain;
    std::vector<engine::BlueprintPlacement> blueprint;
    Arrangement arrangement;
    engine::Level level;
};
```

the product has one authority per meaning:

- `domain` records which source compiler interpreted the transport;
- `blueprint` is the decoded reconstructable witness;
- `arrangement` is the exact proof compiled from that witness; and
- `level` owns the exact palette and region used by runtime play.

do not duplicate palette or region beside `level`.

the arrangement is authoring proof, not runtime state. `engine::State` still
constructs with an empty arrangement and receives no witness placement.

replace the old level compiler with:

```cpp
Result<CompiledLevelResource, LevelResourceError> compile_level_resource(
    const godot::Ref<LevelResource> &p_resource,
    const content::PrototileCatalog &p_catalog);
```

### level compilation precedence

compile in exactly this order:

1. reject a null `LevelResource`;
2. require `format_version == LEVEL_RESOURCE_FORMAT_VERSION`;
3. decode geometry domain, accepting only `0` or `1`;
4. compile the palette through act 1 in that domain;
5. decode blueprint resources, including the 64-record limit;
6. compile the arrangement through act 2's `compile_blueprint`;
7. derive the region through alpha's `region_from_arrangement`; and
8. publish `CompiledLevelResource`.

`LevelResourceError` has one code for each failure stage:

```text
missing_resource
unsupported_format_version
unsupported_geometry_domain
palette_invalid
blueprint_resource_invalid
blueprint_arrangement_invalid
arrangement_region_invalid
```

version and domain failures carry their signed encoded values. every nested
error is populated exactly for its own code. no error is logged, flattened to
text, repaired, or replaced by a partial compiled product.

compilation:

- mutates no resource or subresource;
- emits no `changed` notification;
- treats no pointer or path as content identity;
- caches no core value inside a resource;
- reads no color into exact geometry;
- reorders nothing; and
- returns no partial palette, records, arrangement, region, level, or compiled
  product.

## exact resource encoding

publish a fresh encoder equivalent to:

```cpp
Result<godot::Ref<LevelResource>, LevelResourceEncodingError>
make_level_resource(
    content::GeometryDomain p_domain,
    const engine::Palette &p_palette,
    const std::vector<godot::Color> &p_colors,
    const std::vector<engine::BlueprintPlacement> &p_blueprint);
```

the exact palette, parallel authored colors, and exact blueprint are the
authoritative input. do not accept rendered geometry, palette-row indices,
proposal state, or an existing `LevelResource`.

validate in order:

1. supported domain;
2. color count equal to palette entry count;
3. each palette entry's `PrototileId` representable by signed `int64_t`, in
   palette order;
4. each finite supply representable as a positive signed `int64_t`, in palette
   order;
5. blueprint count at most 64; and
6. each blueprint id representable by signed `int64_t`, in record order.

the encoding error carries the relevant entry or record index and unsigned value
for representability failures, and actual/expected counts for count failures.

on success construct a completely fresh graph:

- format version is current version 1;
- domain uses the stable `0 | 1` encoding;
- palette entries preserve exact palette order;
- unlimited supply writes `-1`;
- finite supply writes its exact positive value;
- colors preserve their exact Godot values;
- blueprint resources preserve record order;
- orientation writes canonical `step()` and `order()`;
- translation writes each raw `int64_t` directly; and
- every child resource is fresh and unpathed.

the encoder does not compile its output. callers use the public compiler for
that independent proof.

## export persistence

modify:

```text
src/game/resources/LevelPersistence.h
src/game/resources/LevelPersistence.cpp
```

replace save/save-as behavior with one explicit export:

```cpp
Result<godot::String, ExportLevelError> export_level_resource(
    const godot::Ref<LevelResource> &p_resource,
    const godot::String &p_explicit_path,
    const content::PrototileCatalog &p_catalog);
```

precedence:

1. missing resource;
2. empty explicit path;
3. extension other than `.tres`, case-insensitively;
4. complete `compile_level_resource` failure; and
5. `ResourceSaver` failure.

compilation failure carries the exact `LevelResourceError` and never reaches the
saver. saver failure carries the exact Godot error.

save with bundled subresources and without change-path behavior. success returns
the explicit path. success and failure both preserve:

- the root resource's inherited path;
- every subresource path;
- every property value;
- every pointer relationship; and
- every `changed` notification count.

there is no implicit resource-owned path, `.res` support, save, save-as, path
takeover, editor document identity, or JSON/bespoke serializer.

do not promise that an operating-system or engine I/O failure leaves no partial
file after the saver has begun. report the exact failure honestly. compilation
and extension failures write nothing because they never reach `ResourceSaver`.

## consumer loading

retain a non-editor consumer operation:

```cpp
struct LoadedLevelResource final {
    godot::Ref<LevelResource> resource;
    CompiledLevelResource compiled;
};

Result<LoadedLevelResource, LoadLevelError> load_level_resource(
    const godot::String &p_path,
    const content::PrototileCatalog &p_catalog);
```

precedence:

1. empty path;
2. extension other than `.tres`, case-insensitively;
3. loader failure;
4. loaded object not exact `LevelResource` type; and
5. complete compilation failure.

load uncached with deep cache bypass. do not fall back to a cached resource,
current editor document, alternate type, migration adapter, or repaired graph.

this is the eventual level consumer and round-trip proof. gamma's authoring
editor never calls it and exposes no load operation.

## known-solution replay

keep replay as a pure local helper in `ResourceIntegrationRunner`, not a new
production engine or resource API.

given one `CompiledLevelResource`:

1. construct a fresh `engine::State` from `compiled.level`;
2. map each blueprint id to the exact palette entry index;
3. map each representative orientation to the exact compiled orientation index;
4. apply one exact `PlaceCommand` at the stored translation in record order;
5. require every command to succeed; and
6. require final `State::is_solved()` to be true.

the helper returns or asserts through the runner's test protocol only. no
replayed `State` enters the resource, compiled product, editor, or future player.

runtime completion remains the existing exact doubled-area comparison and never
calls alpha.

## headless resource integration

rewrite `ResourceIntegrationRunner` and replace the canonical fixture with the
new graph.

prove:

- registration, property metadata, exact defaults, and setter notifications;
- current format version and every unsupported signed value;
- domain encoding and rejection;
- domain-specific palette count limits before entry inspection;
- palette compilation in both domains;
- blueprint count 64 succeeding and 65 failing before record inspection;
- null records and every signed id/orientation decoding failure;
- zero orientation order preserving `OrientationError::zero_order`;
- equivalent nonrepresentative orientation reaching blueprint compilation;
- exact raw-coordinate preservation, including signed extrema;
- exact arrangement compilation and alpha region derivation;
- connected coverage with no holes, one hole, and multiple holes;
- partial-edge contact;
- the complete rich compiled product;
- colors not entering exact geometry;
- a later record failure publishing no partial product;
- every encoding error and precedence;
- fresh resource encoding in exact palette and record order;
- a generated graph containing no pathed child resource;
- compilation mutating no resource or notification count;
- export rejecting null, empty path, wrong extension, and invalid compilation
  before the saver;
- successful self-contained `.tres` export;
- uncached load and compile round trip;
- ids, order, orientations, supplies, colors, raw translations, arrangement,
  region boundaries, holes, and exact area surviving;
- known-solution replay reaching `State::is_solved()`;
- the old `PolygonResource` and `RegionResource` types and properties being
  absent; and
- every temporary file removed by exact path with no broad or recursive
  deletion.

the primary round trip must save and load the exact same `.tres` artifact through
the public export and consumer operations. inspecting only the in-memory
pre-save graph is insufficient.

## fixture

replace:

```text
godot-project/tests/fixtures/canonical_level.tres
```

with one version-1 proof-carrying lattice resource containing:

- a nonempty palette;
- at least two palette entries with different supplies and colors;
- at least two exact blueprint records;
- representative rational orientations;
- at least one nonzero raw translation; and
- connected valid coverage.

the fixture contains no external resource dependency and no region vertices.
hex-12, holes, raw extrema, malformed resources, and size limits may remain
programmatic integration fixtures.

## verification

run:

```text
scons tests
./build/tests/tiles_tests
scons
./Godot.app/Contents/MacOS/Godot --headless --path godot-project \
    res://tests/resource_integration.tscn
./Godot.app/Contents/MacOS/Godot --headless --path godot-project \
    res://tests/level_editor_integration.tscn
./Godot.app/Contents/MacOS/Godot --headless --path godot-project --quit-after 2
```

the native total and editor-integration total are unchanged regression
baselines. record the resource-integration before/after totals separately
because beta replaces its subject.

compile modified non-Godot-capable units under the applicable c++17,
no-exceptions, and warning flags. run `git diff --check`, inspect the complete
diff, verify all temporary artifacts are absent, and record exact outcomes in
the implementation notes.

## boundaries

beta does not:

- edit `LevelEditor`, its scene, or its integration runner;
- derive or render presentation geometry;
- let the authoring editor load a level;
- store independently authored region vertices;
- store player state, progress, history, score, or completion;
- install witness placements into runtime `State`;
- privilege the witness over another legal solution;
- migrate old resources;
- accept `.res`, JSON, or a custom format;
- add another geometry domain;
- change alpha, blueprint, palette, level, state, command, or completion
  semantics;
- implement gamma's export UI; or
- implement delta's distributable binary.

## acceptance summary

beta is complete exactly when:

```text
version-1 domain + palette + raw blueprint resource
∧ exact bounded transport decoding
∧ act-2 Arrangement compilation
∧ alpha Region compilation
∧ rich authoring proof + ordinary runtime Level
∧ fresh self-contained encoding
∧ compile-before-save .tres export
∧ uncached consumer load
∧ exact round trip
∧ known-solution replay
∧ no independent region authority
```

and all native and headless baselines pass together.
