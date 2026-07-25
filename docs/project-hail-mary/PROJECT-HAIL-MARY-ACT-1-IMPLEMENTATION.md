# project hail mary act 1: domain-aware exact palettes

## immutable implementation contract

this document is the complete implementation contract for project hail mary
act 1. once handed to the implementor it is immutable. do not edit this plan to
match an implementation, resolve a later preference, or record completion.
implementation results and any concern for later acts belong in:

```text
docs/project-hail-mary/PROJECT-HAIL-MARY-ACT-1-IMPLEMENTATION-NOTES.md
```

if this plan contains a genuine contradiction, requires an unavailable
contract, or leaves a fork whose alternatives change public behavior, stop and
ask carson. do not guess and do not revise this file.

## implementor context seed

this act begins from the implemented exact hex-12 core and the existing
lattice-only content/resource path. it adds one small integration stratum; it
does not build the new editor.

before editing:

1. run `git status --short --branch`;
2. verify that the current branch is `project-hail-mary`;
3. preserve every pre-existing user or formalizer change;
4. do not stop merely because the worktree is dirty: carson and the formalizer
   may be working concurrently;
5. stop only if pre-existing work overlaps a file this act must change and the
   two changes cannot be preserved together without choosing intent;
6. enumerate authored files with `rg --files`, excluding generated build
   output, `Godot.app/`, and `vendor/`;
7. read completely:
   - `README.md`;
   - `DEFINITIONS.md`;
   - `COORDINATES.md`;
   - `docs/project-hail-mary/PROJECT-HAIL-MARY-ROADMAP.md`;
   - `docs/project-hail-mary/PROJECT-HAIL-MARY-ACT-0-IMPLEMENTATION.md`;
   - `docs/project-hail-mary/PROJECT-HAIL-MARY-ACT-0-IMPLEMENTATION-NOTES.md`;
   - every file beneath `src/core/`, `src/content/`, and `src/engine/`;
   - `src/game/resources/LevelResources.*`;
   - `src/game/resources/ResourceCompiler.*`;
   - the catalog-facing portions of `src/game/LevelEditor.*`;
   - `tests/test_hex12.cpp`;
   - `tests/test_prototile_catalog.cpp`;
   - `tests/test_palette.cpp`;
   - `tests/test_state.cpp`;
   - `tests/test_commands.cpp`;
   - `src/game/testing/ResourceIntegrationRunner.*`;
   - `src/game/testing/LevelEditorIntegrationRunner.*`; and
   - `SConstruct`;
8. establish the baseline with:

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

the committed act-0 native result is:

```text
7634 checks, 0 failed across 355 cases
```

the ordinary headless result reports:

```text
[tiles] level editor ready: 34 catalog rows
```

if concurrent work has legitimately advanced a baseline, record the observed
passing baseline and preserve it. if a required baseline fails for reasons
unrelated to concurrent known work, stop and report the failure before editing.

do not commit.

## goal

make this exact operation available to later game code:

```haskell
compilePaletteResource
  :: GeometryDomain
  -> PrototileCatalog
  -> PaletteResource
  -> Result Palette PaletteResourceError
```

in plain language:

> given `lattice | hex12`, a palette resource containing canonical ids and
> supplies, and the canonical catalog, produce one exact engine palette.

act 1 is the bridge between checked source compilers and the existing
domain-blind runtime palette. it adds no editor domain chooser and serializes no
domain.

## architectural result

the completed dependency path is:

```text
GeometryDomain + canonical content recipe
                    │
                    ▼
       checked core source compiler
       ├── lattice quarter turns
       └── all hex-12 twelfth turns
                    │
                    ▼
       [OrientedPrototile]
                    │
                    ▼
  checked PaletteEntry publication
                    │
                    ▼
             engine::Palette
```

`GeometryDomain` disappears at the source-compilation boundary. none of these
types gain a domain field:

- `OrientedPrototile`;
- `Placement`;
- `Arrangement`;
- `engine::PaletteEntry`;
- `engine::Palette`;
- `engine::Level`;
- `engine::State`; or
- any engine command.

the engine remains incapable of selecting a geometry compiler. content and the
game resource compiler make that choice before publishing a palette entry.

## geometry domain

add:

```text
src/content/GeometryDomain.h
```

with:

```cpp
namespace tiles::content {

enum class GeometryDomain : std::uint8_t {
    lattice,
    hex12,
};

} // namespace tiles::content
```

no string conversion, Godot registration, resource property, domain registry,
bitmask, inheritance, virtual dispatch, or plugin interface belongs in this
act.

every public switch over `GeometryDomain` must handle an invalid value produced
by an integer cast without undefined behavior, assertion, fallback, or silent
selection of another domain.

## canonical identities and presentation order

the canonical catalog contains 37 unique identities after this act.

preserve ids `1..34` exactly. add:

```text
id 35    "triangle 1"      hex12 only
id 36    "hexagon 1"       hex12 only
id 37    "dodecagon 1"     hex12 only
```

the existing:

```text
id 27    "square 1"
```

belongs to both `lattice` and `hex12`. it remains one catalog identity and one
reference `Prototile`, not two congruent entries or an alias.

the domain-filtered presentation views are exactly:

```text
lattice:
  existing ids 1..34 in their existing order

hex12:
  35 triangle 1
  27 square 1
  36 hexagon 1
  37 dodecagon 1
```

the catalog may keep one master storage order different from either view.
domain presentation order must be represented explicitly or derived
deterministically from the shipped source table; it must not depend on sorting
by id at the call site.

## canonical source definitions

extend `CanonicalDefinition` so one definition can describe:

- one lattice source, represented exactly as today by either `ring` or `cells`;
- one optional `Hex12RegularPolygon` source; or
- both, only for a shared identity such as id 27.

use:

```cpp
std::optional<Hex12RegularPolygon> hex12_polygon;
```

or an exactly equivalent closed representation. default construction must keep
existing test-authored lattice definitions source-compatible: an omitted
hex-12 member means no hex-12 support.

source-definition validity is:

```text
at most one of ring and cells is nonempty
∧ at least one of lattice source and hex12_polygon is present
```

preserve the existing typed definition failure for a definition which provides
both `ring` and `cells`. add a catalog-table failure for a definition which
provides neither a lattice source nor a hex-12 source.

the fixed shipped table must be arranged or annotated so filtering it produces
the exact two domain orders above. do not duplicate the unit-square definition
or maintain a second table of playable ids in the editor.

## canonical catalog entry

one `CanonicalPrototile` continues to own:

- one exact reference `Prototile`; and
- one display name.

it additionally retains enough closed metadata to answer whether it supports a
domain and, for hex-12, which `Hex12RegularPolygon` source to compile.

publish:

```cpp
bool supports(GeometryDomain p_domain) const;
```

or an exactly equivalent const query. it returns false for an invalid domain.

do not publish mutable recipe state, arbitrary compiler callbacks, or a public
way to relabel one canonical entry with another source.

### reference construction

catalog construction follows this checked precedence for each definition:

1. display-name and duplicate-id table invariants;
2. source-definition shape;
3. lattice reference construction, when present;
4. hex-12 reference construction, when present; and
5. cross-source reference agreement, when both are present.

a lattice reference is constructed exactly through the current cell/ring,
`Polygon::make`, and `Prototile::make` path.

a hex-12 reference is constructed only through:

```cpp
compile_hex12_orientations(
    id,
    hex12_polygon,
    { Orientation::reference() });
```

the returned sequence must contain one value. the catalog takes that value's
owned reference `Prototile`; it never hand-authors, copies from test constants,
or reconstructs the triangle, square, hexagon, or dodecagon vertices.

when both sources are present, their ids must agree and their reference
polygons must satisfy `same_boundary`. otherwise catalog construction fails
with a typed `reference_geometry_mismatch` carrying the entry id. the lattice
reference remains the one stored only after agreement is proven.

extend catalog errors so a failed hex-12 reference preserves the complete
`Hex12CompilationError`. use a distinct construction stage equivalent to:

```cpp
PrototileCatalogStage::hex12
```

and an optional payload populated exactly for that stage.

invalid `Hex12RegularPolygon` values admitted through the test construction
seam must reach the ordinary hex-12 compiler and return its
`unsupported_polygon` error, not be pre-flattened to a catalog string or
assertion.

## domain-filtered catalog view

publish one read-only query equivalent to:

```cpp
std::vector<const CanonicalPrototile *>
PrototileCatalog::entries_for(GeometryDomain p_domain) const;
```

the exact container spelling may differ if it preserves all of these
properties:

- values remain catalog-owned;
- callers receive no mutation;
- ordering is the exact domain presentation order;
- `lattice` returns 34 entries;
- `hex12` returns four entries;
- no id appears twice within one view;
- id 27 in both views refers to the same catalog identity;
- an invalid domain returns an empty view; and
- pointers or references cannot dangle after ordinary catalog move or copy.

using stored indices into master value storage is the preferred simple
implementation. do not keep self-pointers which become invalid when a catalog
is copied.

retain exact `find(PrototileId)` over all 37 identities. a separate
domain-aware lookup is optional; callers may combine `find` and `supports`.
lookup never compiles, inserts, aliases, logs, or falls back.

## canonical orientation compiler

add a Godot-free content function equivalent to:

```cpp
enum class CanonicalOrientationCompilationErrorCode {
    unsupported_domain,
    prototile_unavailable_in_domain,
    lattice_compilation_failed,
    hex12_compilation_failed,
};

struct CanonicalOrientationCompilationError final {
    CanonicalOrientationCompilationErrorCode code;
    std::optional<LatticeOrientationError> lattice_error;
    std::optional<Hex12CompilationError> hex12_error;
};

Result<std::vector<OrientedPrototile>, CanonicalOrientationCompilationError>
compile_canonical_orientations(
    GeometryDomain p_domain,
    const CanonicalPrototile &p_prototile);
```

it may live beside the catalog or in:

```text
src/content/CanonicalOrientationCompiler.h
src/content/CanonicalOrientationCompiler.cpp
```

choose the separate files if that prevents `PrototileCatalog.cpp` from owning a
second responsibility.

payload rules are exact:

- `unsupported_domain`: both compiler payloads empty;
- `prototile_unavailable_in_domain`: both empty;
- `lattice_compilation_failed`: only `lattice_error`;
- `hex12_compilation_failed`: only `hex12_error`.

the policy is fixed:

```text
lattice:
  request 0/1, 1/4, 1/2, 3/4 from compile_lattice_orientations

hex12:
  request k/12 for every k in [0, 11] from
  compile_hex12_orientations
```

the function calls the selected source compiler exactly once per canonical
entry. it returns that compiler's complete ordered grouped product unchanged.
it does not re-sort, re-group, rotate, deduplicate, inspect floating point, or
cache globally.

for hex-12, the id and polygon kind come from the canonical entry. callers
cannot submit an arbitrary id and claim another canonical source.

## checked precompiled palette entry

add to `engine::PaletteEntry`:

```cpp
enum class PaletteEntryCompilationErrorCode {
    empty_orientations,
    inconsistent_prototile_id,
    inconsistent_reference_boundary,
    representatives_not_strictly_ordered,
    duplicate_orientation_label,
    duplicate_canonical_boundary,
};

struct PaletteEntryCompilationError final {
    PaletteEntryCompilationErrorCode code;
    std::optional<std::size_t> orientation;
    std::optional<std::size_t> conflicting_orientation;
};

static Result<PaletteEntry, PaletteEntryCompilationError> make_compiled(
    Supply p_supply,
    std::vector<OrientedPrototile> p_orientations);
```

the factory validates in this precedence:

1. reject an empty sequence with both indices empty;
2. for each index `i` from one upward:
   1. reject the first id inconsistent with index zero, populating
      `orientation = i`;
   2. reject the first reference boundary inconsistent with index zero,
      populating `orientation = i`;
3. for each adjacent pair, reject the first whose representatives are not
   strictly increasing, populating both indices;
4. scan group pairs in lexicographic `(i, j)` order and reject the first exact
   `Orientation` label appearing in both groups, populating both indices; and
5. scan group pairs in lexicographic order and reject the first pair whose
   canonical polygons satisfy `same_boundary`, populating both indices.

all nonempty failures populate `orientation`; pair failures also populate
`conflicting_orientation`. no other combination is permitted.

on success:

- copy or move the common reference `Prototile` from index zero;
- retain the supplied `Supply`;
- retain the complete oriented vector in the supplied order; and
- publish no second mutable view.

this factory validates composition of already proof-bearing values. it does not
recompile geometry or rediscover which source compiler produced an individual
value. mixed-domain prevention belongs to the content compiler.

retain the existing lattice convenience API and its exact result type:

```cpp
static Result<PaletteEntry, LatticeOrientationError> make(
    const Prototile &p_prototile,
    Supply p_supply,
    std::vector<Orientation> p_requested_orientations);
```

after successful lattice compilation it must delegate to `make_compiled`.
failure of `make_compiled` there is structurally unreachable because one call
to the lattice compiler already guarantees all six conditions. document and
assert that internal invariant; do not widen or replace the established
lattice error type.

`Palette::make`, duplicate-id behavior, supply semantics, and all engine
commands remain unchanged.

## domain-aware palette resource compilation

change the public palette compiler to:

```cpp
Result<engine::Palette, PaletteResourceError> compile_palette_resource(
    content::GeometryDomain p_domain,
    const godot::Ref<PaletteResource> &p_resource,
    const content::PrototileCatalog &p_catalog);
```

domain is first because it governs interpretation of every authored entry.

extend `PaletteResourceErrorCode` with equivalents of:

```cpp
unsupported_geometry_domain,
prototile_unavailable_in_domain,
palette_entry_construction_failed,
```

replace the lattice-only orientation payload with:

```cpp
std::optional<content::CanonicalOrientationCompilationError>
    orientation_error;
std::optional<engine::PaletteEntryCompilationError>
    palette_entry_error;
```

or an exactly equivalent complete typed nesting. retain all existing id,
supply, entry-index, and final `PaletteError` payloads.

error precedence is:

1. missing palette resource;
2. invalid geometry domain;
3. each authored entry in order:
   1. missing entry;
   2. negative encoded id;
   3. unknown nonnegative id;
   4. known id unavailable in the selected domain;
   5. invalid supply;
   6. canonical orientation compilation;
   7. checked precompiled-entry construction;
4. final `Palette::make`, including empty and duplicate-id rejection.

every entry failure populates `entry`. once a nonnegative id has been converted,
every subsequent entry failure also populates `encoded_prototile_id` and strong
`prototile_id`. only invalid supply populates `encoded_supply`.

`orientation_error` is populated only for canonical orientation compilation;
`palette_entry_error` only for checked entry construction; `palette_error` only
for final palette construction. no failure logs, substitutes another domain,
uses a fallback shape, skips an entry, or publishes a partial palette.

authored color remains completely unread by exact palette compilation.

### existing level compiler

act 1 does not serialize a domain. the existing `LevelResource` and
`compile_level_resource` therefore remain lattice-only for this transitional
act and call:

```cpp
compile_palette_resource(
    content::GeometryDomain::lattice,
    resource->get_palette(),
    catalog);
```

act 2-1 later replaces the level resource source graph and supplies the
serialized domain. do not anticipate that act here.

## transitional current editor

act 1 does not build the new editor. the existing `LevelEditor` must remain
explicitly lattice-only and visually unchanged.

adapt it to use the catalog's lattice domain view for:

- row construction;
- row count and ready diagnostic;
- row-to-id lookup;
- restoring an opened palette;
- publishing a palette; and
- every row bound check.

it must continue to report:

```text
[tiles] level editor ready: 34 catalog rows
```

all its `compile_palette_resource` calls explicitly pass
`GeometryDomain::lattice`.

do not add a domain selector, hex-12 rows, new document workflow, blueprint, or
new visual control. act 2 replaces this surface.

the old standalone `Editor` and temporary tetromino state require no change:
they resolve existing lattice ids and use the established lattice convenience
constructor.

## native tests

### geometry domain and catalog

extend `tests/test_prototile_catalog.cpp` or add one focused content test.
prove:

- the master catalog has exactly 37 unique ids;
- ids `1..34` retain exact names and reference boundaries;
- ids `35..37` have the specified names and exact reference side counts;
- the lattice view is exactly ids `1..34` in existing order;
- the hex-12 view is exactly `{35, 27, 36, 37}`;
- invalid domain views are empty;
- every view entry reports support for its domain;
- lattice-only and hex-only entries reject the other domain;
- id 27 supports both;
- id 27's lattice reference and hex-12 phase-zero reference have the same
  exact boundary;
- the three hex-only references equal the reference prototiles returned by the
  ordinary hex-12 compiler;
- all 37 ids are found exactly and unknown ids remain absent;
- no two different ids within either domain view are congruent under that
  domain's admitted rotations;
- a custom definition with neither source fails as missing geometry;
- both ring and cells retain their existing typed definition failure;
- invalid hex-12 polygon metadata preserves `unsupported_polygon`;
- deliberately mismatched lattice and hex-12 references return
  `reference_geometry_mismatch`; and
- duplicate ids and empty names retain existing precedence.

expected values in tests must not be read back from production order tables or
production geometry accessors.

### canonical orientation compiler

prove:

- every representative lattice entry retains its existing exact groups;
- all 34 lattice entries compile successfully;
- hex-12 groups are exactly `4 / 3 / 2 / 1`;
- their complete equivalence-label tables match act 0;
- each compiler is called through only its supported domain;
- lattice-only and hex-only mismatches return
  `prototile_unavailable_in_domain`;
- invalid domains return `unsupported_domain`; and
- payload optionals match their error codes exactly.

### checked palette entry

extend `tests/test_palette.cpp`. construct proof-bearing values only through
ordinary core compilers, then synthesize vectors to prove:

- empty rejection;
- successful lattice and hex-12 entries;
- inconsistent id rejection;
- inconsistent reference boundary rejection;
- non-increasing representative rejection;
- duplicate equivalent-orientation label rejection;
- duplicate canonical-boundary rejection;
- exact precedence and indices when several defects coexist;
- configured supply and complete orientation vectors survive success; and
- the legacy lattice factory retains all existing successes and errors.

do not add an unchecked `OrientedPrototile` construction seam for tests.

### downstream domain blindness

using palettes compiled through the new public path, prove representative
hex-12 candidates can:

- be resolved by palette entry and orientation index;
- make exact placements;
- enter an arrangement;
- join through existing full-edge or vertex operations; and
- report supply through existing engine behavior.

do not change `State` to carry a domain merely to make this test possible. a
small ordinary `Region` fixture may contain the placements.

## Godot resource integration

extend `ResourceIntegrationRunner` rather than add another scene.

retain all existing lattice resource checks, now passing
`GeometryDomain::lattice`. add checks which:

- compile one representative lattice palette unchanged;
- compile the four-entry hex-12 palette in exact presentation order;
- observe distinct orientation counts `4 / 3 / 2 / 1`;
- observe exact ids, supplies, reference boundaries, representative
  orientations, equivalence labels, and oriented boundaries;
- prove authored colors do not affect exact compiled equality;
- reject a lattice-only id in `hex12`;
- reject a hex-only id in `lattice`;
- reject an invalid domain at the specified precedence;
- preserve complete nested compiler errors;
- preserve a resource and catalog unchanged after every compilation; and
- publish no partial palette after a later entry fails.

existing `compile_level_resource`, persistence fixtures, and
`LevelEditorIntegrationRunner` remain lattice-only and must continue passing.

update diagnostic formatting only as required to render the new typed
alternatives. strings are a presentation boundary, never the error contract.

## verification

after implementation:

1. run `scons tests`;
2. run `./build/tests/tiles_tests`;
3. compile each new or modified non-Godot translation unit standalone under
   c++17, `-fno-exceptions`, and the repository warning flags;
4. run `scons`;
5. run:

   ```text
   ./Godot.app/Contents/MacOS/Godot --headless --path godot-project \
       res://tests/resource_integration.tscn
   ./Godot.app/Contents/MacOS/Godot --headless --path godot-project \
       res://tests/level_editor_integration.tscn
   ./Godot.app/Contents/MacOS/Godot --headless --path godot-project --quit-after 2
   ```

6. verify the ordinary run still reports exactly 34 catalog rows and no error
   lines;
7. run `git diff --check`;
8. inspect `git diff --stat` and the complete diff;
9. verify no generated `.godot`, build, imported, or temporary integration file
   is included;
10. verify no existing lattice test was removed, weakened, renamed to evade
    discovery, or changed to accept a different result; and
11. record exact native and headless check totals and complete build outcomes
    in the implementation notes.

do not claim visual or manual editor acceptance: this act deliberately leaves
the visible editor lattice-only and unchanged.

## allowed implementation surface

expected new files:

```text
src/content/GeometryDomain.h
src/content/CanonicalOrientationCompiler.h
src/content/CanonicalOrientationCompiler.cpp
docs/project-hail-mary/PROJECT-HAIL-MARY-ACT-1-IMPLEMENTATION-NOTES.md
```

the compiler may remain in `PrototileCatalog.*` instead of separate files when
that is materially clearer.

expected modified files:

```text
src/content/PrototileCatalog.h
src/content/PrototileCatalog.cpp
src/engine/Palette.h
src/engine/Palette.cpp
src/game/resources/ResourceCompiler.h
src/game/resources/ResourceCompiler.cpp
src/game/LevelEditor.h
src/game/LevelEditor.cpp
src/game/testing/ResourceIntegrationRunner.h
src/game/testing/ResourceIntegrationRunner.cpp
tests/test_prototile_catalog.cpp
tests/test_palette.cpp
```

additional existing tests may be modified only to pass the now-explicit
`lattice` argument or to consume the catalog's lattice view. do not mechanically
rewrite unrelated code.

do not modify:

- q16.48 representation;
- any core compiler or geometry predicate merely to integrate its result;
- `OrientedPrototile` construction access;
- placement, arrangement, region, level, state, session, or command semantics;
- resource schemas or registered resource properties;
- `SConstruct`, unless recursive source discovery is demonstrably broken;
- Godot scenes;
- the visible editor layout; or
- immutable plans and completed implementation notes.

## out of scope

- the new tiling-first editor;
- a domain chooser;
- serializing geometry domain;
- blueprint placements;
- arrangement-to-region construction;
- level export under the new resource graph;
- a player or play session;
- mixed-domain palettes;
- square/octagonal geometry;
- penrose, spectres, or `Tile(1,1)`;
- reflections, scaling, curves, decoration, or matching rules;
- arbitrary source compilers;
- compiler registration or type erasure;
- runtime domain checks after palette compilation;
- target generation;
- polygon union;
- migration of old resources; and
- campaign or production-ui work.

## acceptance summary

act 1 is accepted only when all of the following hold together:

```text
37 unique canonical identities
∧ lattice view = existing ids 1..34
∧ hex12 view = {35, 27, 36, 37}
∧ shared id-27 reference boundary
∧ exact domain-selected orientation compilation
∧ hex12 distinct groups = 4 / 3 / 2 / 1
∧ checked precompiled PaletteEntry publication
∧ complete typed resource errors
∧ unchanged downstream engine behavior
∧ unchanged visible 34-row lattice editor
∧ all native and headless verification passes
```

no domain reaches runtime engine state, no partial palette is published, and no
source geometry is reconstructed outside its checked core compiler.
