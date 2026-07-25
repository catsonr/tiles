# project hail mary roadmap

## status

this is the living roadmap for `PROJECT-HAIL-MARY`. it records the destination,
the decisions already made, the boundaries between remaining acts, and the
questions which belong to later formalization.

this roadmap is not an immutable implementation plan. carson and the formalizer
may revise it as the project develops. each act receives its own immutable
implementation plan only after that act has been discussed and its contract is
satisfactory.

the intended sequence is:

```text
act 0          exact hex-12 source compiler                 implemented
   │
   ▼
act 1          lattice | hex12 → exact runtime palette      implemented
   │
   ▼
act 2          tiling-first lattice | hex12 level editor    implemented
   │
   ▼
act 2-1 alpha  Arrangement → exact Region
   │
   ▼
act 2-1 beta   versioned proof-carrying LevelResource
   │
   ▼
act 2-1 gamma  validate and export from the editor
   │
   ▼
act 2-1 delta  package the editor as a distributable binary
```

current status:

```text
act 0          formalized and implemented
act 1          formalized and implemented
act 2          formalized and implemented
act 2-1 alpha  formalized; awaiting implementation
act 2-1 beta   formalized; awaits alpha implementation
act 2-1 gamma  formalized; awaits beta implementation
act 2-1 delta  being handled as separate binary-export work
```

the acts are sequential but do not depend upon one another's hidden
implementation details:

- act 1 publishes one domain-aware exact palette-compilation boundary;
- act 2 consumes only that public boundary and publishes one exact in-memory
  blueprint arrangement;
- alpha consumes only `Arrangement` and publishes exact coverage compilation;
- beta consumes only act 2's blueprint and alpha's region compiler and publishes
  one versioned proof-carrying resource contract;
- gamma consumes only the editor's exact document and beta's public export
  boundary; and
- delta packages the completed editor without changing level meaning.

the former combined
`PROJECT-HAIL-MARY-ACT-2-1-IMPLEMENTATION.md` contract was deleted before
implementation. alpha, beta, and gamma replace it completely; it is not a
prerequisite and no implementor should attempt it.

if formalizing an act reveals that the following act needs knowledge not
present in the preceding act's public contract, the boundary is wrong and the
roadmap must be reconsidered before either plan is written.

## destination

`PROJECT-HAIL-MARY` is complete when the application contains one working level
editor which:

1. begins a new document in either the `lattice` or `hex12` geometry domain;
2. presents only canonical prototiles supported by that domain;
3. builds a nonempty palette with authored supply and color;
4. lets the author construct one exact, interior-disjoint tiling arrangement
   from those palette entries;
5. derives one connected exact `Region`, including any holes, from the
   arrangement's coverage;
6. rejects arrangements which cannot become one valid region;
7. exports the geometry domain, palette, and exact blueprint as a Godot
   `LevelResource`;
8. round-trips that artifact through the consumer resource compiler;
9. compiles the resource into the existing runtime
   `tiles::engine::Level = Palette × Region`; and
10. proves that replaying the exported blueprint covers its derived region
    exactly.

the final product is an authoring tool which exports levels. a campaign,
level-selection screen, production player interface, progression, scoring, and
presentation polish are later projects.

## why the editor is tiling-first

the current level editor asks the author to draw a region and then discover
whether the selected palette can cover it. that direction is no longer useful
for this project.

the new authoring direction is:

```text
choose geometry domain
        │
        ▼
choose palette
        │
        ▼
construct exact tiling arrangement
        │
        ▼
derive region from arrangement coverage
        │
        ▼
export known-solvable Level resource
```

the blueprint is simultaneously:

- the object the author actually wants to make;
- the exact source from which the target region is derived;
- a witness that the exported level is solvable;
- the source of exact, non-rendered region vertices; and
- the complete geometric witness needed by the eventual level consumer.

the runtime level does not begin with the blueprint arrangement installed. the
resource compiler produces the ordinary empty-play `Level`; the stored
blueprint is an authoring witness and may be replayed by tests and compiler
acceptance, not player progress.

this replaces, rather than supplements, inverse region drawing. the current
editor may be deleted or substantially rewritten wherever that is simpler and
clearer. preserving an unhelpful interaction or resource contract is not a
goal.

## authority and supersession

`DEFINITIONS.md` and `COORDINATES.md` remain authoritative.

the runtime domain remains:

```haskell
data Level = Level
  { palette :: Palette
  , region  :: Region
  }
```

this roadmap changes how an authored `LevelResource` obtains that exact runtime
value. it does not add a known solution, geometry domain, Godot resource, or
presentation value to `tiles::engine::Level`.

where this roadmap conflicts with the living
`MAKE-AN-EDITOR-ROADMAP.md`, this roadmap supersedes it for the level-authoring
workflow and `LevelResource` source graph:

- regions are derived from blueprint arrangements rather than drawn first;
- exact blueprint geometry replaces independently authored `Vector2` region
  vertices;
- geometry domain is selected once per level; and
- old development levels need not be migrated.

completed immutable implementation plans remain historical records of what was
implemented. later project-hail-mary acts may replace their resulting editor
and resource surfaces without editing those plans.

## geometry domains

one authored level belongs to exactly one geometry domain:

```haskell
data GeometryDomain
  = Lattice
  | Hex12
```

domain is a source-compilation and authoring concept. it does not enter:

- `OrientedPrototile`;
- `Placement`;
- `Arrangement`;
- `PaletteEntry`;
- `Palette`;
- engine commands;
- runtime `Level`; or
- runtime `State`.

after source compilation, both domains publish the same exact runtime values.
all placement, intersection, joining, containment, completion, and rendering
consumers remain domain-blind.

mixing domains inside one palette is forbidden by construction. although core
values from different domains can coexist mechanically, many of their edges
cannot mate and there is no useful game-design reason to offer such palettes.

changing the domain of an existing editor document need not preserve anything.
the economical behavior is to discard the current document and begin a new
blank document in the selected domain. old resource migration and cross-domain
palette conversion are out of scope.

### lattice

the lattice domain preserves existing tier-1 behavior:

- canonical lattice prototiles come from the shipped catalog;
- requested orientations are the four quarter turns;
- geometrically identical results are collapsed by the existing lattice
  compiler; and
- all existing lattice ids, geometry, results, and errors remain stable.

### hex12

the hex-12 domain initially contains exactly these unit source polygons:

```text
triangle
square
hexagon
dodecagon
```

the domain requests all twelve exact twelfth-turn phases. the act-0 compiler
collapses them into:

```text
triangle      4 distinct boundaries
square        3 distinct boundaries
hexagon       2 distinct boundaries
dodecagon     1 distinct boundary
```

the existing unit lattice square identity is reused for the hex-12 square. its
reference polygon is exactly the same q16.48 unit square, while the geometry
domain selects which compiled orientation set belongs to that identity.

new stable ids are required for the unit triangle, hexagon, and dodecagon.
their exact values and display names belong to act-1 formalization.

`4.8.8`, the square/octagonal direction module, penrose kite and dart,
`Tile(1,1)`, spectres, arbitrary regular polygons, scaling, reflections, and
tier 3 are out of scope for this roadmap.

## exact completion remains cheap

region construction and runtime completion use related exact facts, but they
are not the same algorithm.

alpha derives a region boundary from an arrangement during authoring or
resource compilation. runtime `State` already maintains:

```text
every footprint lies within the closed region
∧ placement interiors are pairwise disjoint
```

under those invariants, completion remains the existing exact test:

```haskell
solved state =
  sum (map footprintDoubledArea (placements state))
    == regionDoubledArea (region state)
```

no runtime union, boundary extraction, tolerance, rasterization, or tiling
search is required. equality of exact doubled areas is sufficient because a
finite collection of contained polygonal footprints with pairwise-disjoint
interiors cannot have equal total area while leaving a positive-area polygonal
gap.

the greek acts must retain and test this existing completion path. they must not
replace it with the more expensive authoring-time boundary algorithm.

## act 0 — exact unit hex-12 core

status: implemented.

act 0 added:

```haskell
compileHex12Orientations
  :: PrototileId
  -> Hex12RegularPolygon
  -> [Orientation]
  -> Result [OrientedPrototile] Hex12CompilationError
```

the implementation proved:

- the fixed q16.48 twelve-direction module;
- exact closure for every supported regular source polygon and phase;
- deterministic orientation grouping;
- exhaustive exact joins in all twelve directions;
- all ten supported euclidean uniform vertex stars; and
- extended patch growth with bit-exact cycle closure.

act 0 deliberately did not add content ids, palettes, resources, editor
behavior, or target generation. those omissions define the work below.

## act 1 — domain-aware exact palettes

### purpose

make this operation possible without an editor:

```haskell
compilePalette
  :: GeometryDomain
  -> CanonicalCatalog
  -> NonEmpty AuthoredPaletteEntry
  -> Result Palette PaletteCompilationError
```

in plain language:

> given `lattice | hex12`, selected canonical prototile ids, and supplies,
> produce one exact engine palette.

today this is impossible because `engine::PaletteEntry::make` always invokes
the lattice compiler internally, and the canonical catalog describes only
lattice source geometry.

### scope

act 1 owns:

- the Godot-free `GeometryDomain` value;
- catalog source recipes and domain membership;
- new canonical unit triangle, hexagon, and dodecagon identities;
- reuse of the existing unit square identity in both domains;
- selection of lattice quarter turns versus all hex-12 twelfth turns;
- one narrow checked engine path which constructs `PaletteEntry` from an
  already-compiled nonempty sequence of `OrientedPrototile` values;
- preservation of the existing lattice-oriented `PaletteEntry::make`
  convenience path, if retaining it remains useful;
- one domain-aware palette compiler usable by later game code;
- complete typed errors retaining the responsible entry, prototile id, source
  compiler error, or palette error; and
- native and integration tests for both domains.

the checked precompiled-entry path must prove at minimum that:

- the sequence is nonempty;
- every oriented value carries one consistent prototile identity;
- every oriented value carries the same reference prototile geometry; and
- no invalid partial entry is published on failure.

the exact validation surface and error representation belong to act-1
formalization.

### boundaries

act 1 does not:

- build or replace the editor;
- serialize a geometry domain;
- construct a blueprint arrangement;
- derive a region;
- save a level;
- add domain knowledge to runtime engine state or commands;
- generalize the core into a compiler registry or virtual geometry framework;
- change existing lattice results; or
- implement another geometry domain.

### acceptance

act 1 is complete when tests can request representative lattice and hex-12
palettes through one domain-aware public boundary and observe:

- the intended canonical ids and authored ordering;
- unchanged lattice orientation sets;
- exact hex-12 variant counts `4 / 3 / 2 / 1`;
- the shared unit-square id with domain-selected orientation behavior;
- configured finite and unlimited supplies;
- rejection of ids unavailable in the selected domain;
- rejection of duplicate ids;
- complete failure without a partial palette; and
- unchanged behavior in every downstream domain-blind engine consumer.

## act 2 — tiling-first domain editor

### purpose

replace the current inverse region editor with one working in-memory blueprint
editor split explicitly between `lattice` and `hex12`.

act 2 ends with an exact arrangement the author can build and revise. it does
not yet need to save that arrangement as a level.

### document flow

a new document begins by choosing one geometry domain. choosing or changing the
domain may discard the entire current document.

the editor then:

1. presents the canonical catalog entries admitted by that domain;
2. lets the author form a nonempty palette;
3. assigns positive finite or unlimited supply and an opaque color to every
   selected entry;
4. compiles the palette through act 1;
5. places the first selected oriented prototile at exact translation `(0, 0)`;
6. offers further placements derived from exact arrangement features;
7. inserts only placements accepted by the ordinary exact arrangement proof;
8. permits placements to be removed; and
9. renders the authoritative arrangement after every accepted change.

the first placement needs no pointer-derived model coordinate. absolute
blueprint position carries no level meaning, so exact origin is sufficient.
afterward the mouse may select among a finite set of exact proposals; rendered
or screen coordinates never construct authoritative geometry.

point-only contact remains valid in core but need not be offered by the editor.
every offered later placement should share positive-length boundary contact
with the existing blueprint so ordinary authored arrangements remain connected
by construction.

### blueprint authority

the blueprint is authoring state, not a runtime `engine::State`. runtime
`State` requires a completed `Level`, including the very region this blueprint
will eventually define.

act-2 formalization must select one clear mutation boundary without inventing a
fake oversized region. the leading approach is construct-and-recompile:

```text
current blueprint transport
        │
        ├── add/remove one proposed placement in a candidate copy
        ▼
compile complete candidate into exact Arrangement
        │
        ├── failure: preserve current document
        └── success: publish candidate and exact arrangement
```

this resembles the current palette editor's transactional publication and
keeps the resource/compiler direction one-way. whether the act-2 in-memory
transport is already the final Godot resource type or a smaller private draft
type belongs to act-2 formalization; later greek acts must not depend on an
unrecorded choice.

### current editor

the current `LevelEditor`, its inverse region-loop workflow, and its supporting
controls are disposable. act 2 may:

- reuse code which remains directly useful;
- substantially rewrite the class;
- replace it with a new class; or
- delete obsolete region-authoring code and tests.

preservation for its own sake is not a requirement. any retained projection,
preview, palette-row, persistence, or test code must serve the new workflow
rather than force the new workflow through the old model.

### boundaries

act 2 does not:

- derive the final region;
- define arrangement boundary extraction;
- export or consume a complete `LevelResource`;
- migrate old editor documents;
- provide a player;
- prove campaign progression; or
- implement automated tiling search.

### acceptance

act 2 is complete when the running Godot editor can, in separate fresh
documents:

- choose `lattice` and construct a multi-placement exact lattice arrangement;
- choose `hex12` and construct a multi-placement exact uniform arrangement;
- present only domain-compatible prototiles;
- cycle each entry through its actual distinct compiled orientations;
- place the first tile at exact origin;
- repeatedly add exact, non-overlapping, edge-connected placements;
- reject an overlapping or supply-exhausting candidate transactionally;
- remove an existing placement;
- discard the document when changing domains; and
- render every accepted arrangement from authoritative q16.48 geometry.

tests must establish model behavior independently of pointer coordinates and
must include a headless integration path for both domains.

## act 2-1 alpha — exact arrangement coverage

status: formalized; awaiting implementation.

alpha publishes one pure exact operation:

```haskell
regionFromArrangement
  :: Arrangement
  -> Result Region ArrangementRegionError
```

it collects directed footprint edges, subdivides exact contacts into atomic
segments, cancels internal shared segments, proves a manifold directed boundary,
classifies one positive outer component and any negative hole cycles, and
constructs the result through `Polygon::make` and `Region::make`.

whole-edge cancellation is insufficient. valid arrangements may place several
short edges against one longer edge. no coordinate may pass through floating
point, division, interpolation, rasterization, or tolerance.

expected rejection has exactly three authored meanings:

- the arrangement is empty;
- its coverage boundary is nonmanifold, including point connections and
  pinches; or
- its coverage has more than one positive component.

impossible consequences of the public arrangement invariant still return typed
internal evidence rather than publishing partial geometry.

alpha owns no Godot, domain, palette, blueprint transport, editor, persistence,
or runtime-completion behavior. its immutable implementation contract contains
the complete algorithm, precedence, provenance, and native acceptance.

## act 2-1 beta — proof-carrying LevelResource

status: formalized; awaits alpha implementation.

beta turns act 2's exact blueprint records and alpha's region compiler into one
versioned, self-contained Godot resource artifact:

```text
LevelResource
    ├── schema version
    ├── geometry domain
    ├── PaletteResource
    │     └── [PaletteEntryResource]
    │             ├── prototile id
    │             ├── supply
    │             └── color
    └── blueprint
          └── [BlueprintPlacementResource]
                  ├── prototile id
                  ├── exact orientation identity
                  ├── raw q16.48 translation x
                  └── raw q16.48 translation y
```

the resource stores the authoring witness, not region vertices, compiled
geometry, editor state, file identity, player progress, or history.

resource compilation is one-way:

```text
validate version and domain
        │
        ▼
compile palette through act 1
        │
        ▼
decode exact blueprint records
        │
        ▼
compile Arrangement through act 2
        │
        ▼
compile Region through alpha
        │
        ▼
publish exact authoring proof + runtime Level
```

beta must publish enough exact compiled information for both resource
round-trip proof and gamma export without asking gamma to repeat hidden compiler
stages. the runtime `engine::Level` remains only `Palette × Region`; any richer
compiled-resource product is authoring support and does not enter runtime state.

the externally supplied blueprint receives a formalized size limit before any
quadratic arrangement or boundary work. malformed transport is rejected with
typed stage-specific errors and produces no partial compiled document.

the exact limit is 64 blueprint records.

beta owns `ResourceLoader` round-trip proof because the artifact's eventual
consumer must be able to load it. this does not imply that the authoring editor
can open, resume, or modify an exported file.

old development `PolygonResource` and `RegionResource` compatibility remains
unnecessary. beta formalization decides their removal together with the old
fixtures and compiler paths.

## act 2-1 gamma — export-only editor integration

status: formalized; awaits beta implementation.

gamma extends the act-2 editor with alpha's derived region and beta's export
artifact.

after each accepted blueprint mutation:

- alpha derives a region;
- success makes the current document exportable;
- expected failure leaves the valid blueprint editable and unexportable; and
- region failure never rolls back a successful blueprint edit.

the exact region and typed failure are nonvisual authoring proof. gamma adds no
region fill, outer-boundary overlay, hole-boundary overlay, target shading, or
other region rendering.

the editor has one export operation, not document persistence:

```text
authoritative in-memory document
        │
        ▼
construct one fresh self-contained LevelResource
        │
        ▼
compile the complete candidate through beta
        │
        ├── failure: write nothing
        ▼
filesystem save dialog for one .tres destination
        │
        ▼
ResourceSaver
```

the editor does not load exported levels. it therefore owns no open action,
loaded-document reconstruction, reusable resource path, save/save-as
distinction, or dirty state. each export asks for its destination and writes
one complete `.tres`.

the file dialog must access the host filesystem rather than only packaged
`res://` content. gamma proves the lower-level external-path save behavior and
performs a bounded manual dialog pass; beta proves that the resulting artifact
loads and compiles.

## act 2-1 delta — distributable editor binary

status: separate work in progress.

delta packages the completed editor as a distributable application, ideally a
single executable per supported platform. platform targets, GDExtension release
artifacts, export presets, signing, notarization, and packaged filesystem smoke
tests belong to delta.

delta does not change exact geometry, resource meaning, the alpha compiler, the
beta schema, or gamma's export operation.

## greek-act acceptance

the greek sequence is complete when:

- lattice and hex-12 arrangements derive exact regions, including holes;
- empty, disconnected, point-connected, and nonmanifold coverage fail with
  typed evidence;
- the resource contains a versioned domain, palette, and exact blueprint but no
  independently authored region;
- every exported blueprint recompiles into an interior-disjoint arrangement
  obeying palette membership, orientation, and supply;
- raw coordinates, rational orientations, ids, order, supplies, colors,
  placement count, derived boundaries, and holes survive resource round trip;
- replaying the exported witness into a fresh runtime state makes
  `State::is_solved()` true;
- the editor writes a self-contained `.tres` to a user-selected external path
  and never loads one;
- existing runtime completion remains the exact doubled-area comparison; and
- native tests, GDExtension build, headless integration, persistence round trip,
  packaged acceptance, and `git diff --check` pass at their respective act
  boundaries.

## project-wide invariants

every remaining act preserves:

- q16.48 as the only authoritative runtime coordinate representation;
- exact gameplay predicates after source compilation;
- checked canonical polygons and regions;
- validated source-compiled oriented prototiles;
- exact translation and joining;
- pairwise interior-disjoint arrangements;
- no reflections, scaling, curved edges, or holes within prototiles;
- value ownership without dangling geometry;
- no Godot dependency in `src/core/` or `src/engine/`;
- Godot resources as fallible transport rather than domain values;
- rendered coordinates as a one-way lossy projection;
- typed failures and transactional publication;
- no partial palette, arrangement, region, resource compilation, or save;
- authored arrangements of roughly fifty placements or fewer; and
- no commit by an implementor.

## deliberately absent

this roadmap does not include:

- penrose kite and dart;
- spectres or `Tile(1,1)`;
- square/octagonal `4.8.8`;
- a geometry-plugin system;
- mixed-domain palettes;
- arbitrary regular polygons;
- runtime trigonometry;
- floating or epsilon gameplay geometry;
- reflections or scaling;
- tile decoration or edge-matching rules;
- disconnected target regions;
- an arbitrary polygon-union library;
- automated tiling search;
- a level player or application host;
- campaign order, progression, scoring, or completion presentation;
- save data for player progress;
- compatibility or migration for old editor resources; or
- production ui polish unrelated to exporting correct levels.

## questions routed to formalization

alpha's exact algorithm, error distinction, ordering, provenance, and test
surface are resolved by its immutable implementation contract.

### beta

beta's version-1 schema, 64-record limit, rich compiled authoring product,
rational-orientation decoding, typed failures, fresh encoder, `.tres` export,
consumer loading, replay proof, and old-region-resource removal are resolved by
its immutable implementation contract.

### gamma

gamma's nonvisual region proof, transactional edit publication, export
enablement, external-filesystem `.tres` dialog, path normalization, absence of
loading and document persistence, and headless/manual acceptance are resolved by
its immutable implementation contract.

### delta

- supported platforms and artifact shape;
- release GDExtension builds and export presets;
- signing, notarization, and operating-system trust behavior; and
- packaged external-filesystem export acceptance.
