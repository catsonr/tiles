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
act 0    exact hex-12 source compiler                    implemented
   │
   ▼
act 1    lattice | hex12 → exact runtime palette
   │
   ▼
act 2    tiling-first lattice | hex12 level editor
   │
   ▼
act 2-1  arrangement → region → persistent Level resource
```

current status:

```text
act 0    formalized and implemented
act 1    formalized; awaiting implementation
act 2    roadmap discussion
act 2-1  roadmap discussion
```

the acts are sequential but do not depend upon one another's hidden
implementation details:

- act 1 publishes one domain-aware exact palette-compilation boundary;
- act 2 consumes only that public boundary and publishes one exact in-memory
  blueprint arrangement;
- act 2-1 consumes only that blueprint contract, derives its region, and
  persists the resulting authoring witness.

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
7. saves the geometry domain, palette, and exact blueprint as a Godot
   `LevelResource`;
8. loads or otherwise round-trips that resource through the same compiler;
9. compiles the resource into the existing runtime
   `tiles::engine::Level = Palette × Region`; and
10. proves that replaying the saved blueprint covers its derived region exactly.

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
save known-solvable Level resource
```

the blueprint is simultaneously:

- the object the author actually wants to make;
- the exact source from which the target region is derived;
- a witness that the saved level is solvable;
- the source of exact, non-rendered region vertices; and
- enough information to reopen and continue editing the authored level.

the runtime level does not begin with the blueprint arrangement installed. the
resource compiler produces the ordinary empty-play `Level`; the stored
blueprint is an authoring witness and may be replayed by tests or by the editor,
not player progress.

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

act 2-1 derives a region boundary from an arrangement once during authoring or
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

act 2-1 must retain and test this existing completion path. it must not replace
it with the more expensive authoring-time boundary algorithm.

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
type belongs to act-2 formalization; act 2-1 must not depend on an unrecorded
choice.

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
- save or load a complete `LevelResource`;
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

## act 2-1 — region derivation and Level persistence

### purpose

turn act 2's exact blueprint arrangement into one persistent, compilable,
known-solvable level resource.

### arrangement boundary

act 2-1 adds a pure exact operation equivalent to:

```haskell
regionFromArrangement
  :: Arrangement
  -> Result Region ArrangementBoundaryError
```

this is exact geometry and belongs naturally in `tiles` core. act-2-1
formalization may select a differently named but equivalently narrow surface.
it must not add Godot, authored color, supply, or geometry-domain knowledge to
core.

whole-edge cancellation is insufficient. valid lattice arrangements can place
several short edges against one longer prototile edge, and the existing core
correctly admits such partial-edge contact. the boundary derivation must:

1. collect every directed footprint edge;
2. find every exact collinear overlap endpoint;
3. split overlapping edges into atomic directed subsegments;
4. cancel atomic subsegments covered in opposite directions;
5. retain the uncancelled directed coverage boundary;
6. stitch that boundary into deterministic closed rings;
7. identify exactly one outer ring and zero or more inner rings;
8. construct every ring through `Polygon::make`; and
9. construct the final value through `Region::make`.

the intended arrangement contains roughly fifty placements or fewer.
straightforward bounded pairwise scans are appropriate. do not add a general
polygon-boolean library, spatial index, rasterizer, tolerance, or floating
geometry.

typed rejection must cover at least:

- an empty arrangement;
- more than one positive connected component;
- point-connected or otherwise non-manifold coverage;
- a boundary branch or open walk;
- failure to construct a boundary polygon; and
- failure to construct the final region.

exact error precedence and payloads belong to act-2-1 formalization.

### resource source graph

the target resource shape is:

```text
LevelResource
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

the blueprint stores exact translations as signed 64-bit raw coordinates.
neither translation passes through `Vector2`, a decimal string, a rendered
position, or another quantization boundary.

the orientation encoding must be stable across save and load and must identify
one exact compiled distinct orientation without relying on a transient vector
index. the leading representation is one canonical rational turn stored as
signed integer numerator and positive integer denominator. exact encoding and
validation belong to act-2-1 formalization.

the resource does not independently store region vertices. its region is
derived from the exact replayed blueprint, eliminating two authorities which
could disagree.

old `PolygonResource` and `RegionResource` values may be removed if no retained
surface needs them. no migration of existing development `.tres` levels is
required.

### resource compilation

the one-way compiler performs:

```text
validate domain
    │
    ▼
compile palette through act 1
    │
    ▼
resolve and construct every blueprint placement exactly
    │
    ▼
insert every placement into Arrangement
    │
    ▼
verify palette membership, orientation, and supply
    │
    ▼
derive Region from arrangement coverage
    │
    ▼
publish engine::Level(palette, region)
```

the compiled runtime level contains an empty play arrangement exactly as it
does now. the resource blueprint never becomes saved player progress.

resource compilation is transactional and pure with respect to the resource.
it emits no expected-input diagnostic, mutates no resource, repairs nothing,
caches no core value inside Godot transport, and returns no partial level.

### saving and reopening

the editor saves through the existing Godot `ResourceSaver` mechanism. a save
is permitted only when the complete resource compiles successfully.

act 2-1 must also prove a round trip:

```text
editor blueprint
    → save LevelResource
    → load LevelResource
    → compile exact Level
    → replay saved blueprint
    → State::is_solved() == true
```

raw coordinates, domain, ids, orientations, supplies, colors, placement count,
derived region boundaries, and holes must survive the round trip.

replaying the witness is a test and editor operation. runtime completion still
uses the existing exact area comparison; it does not invoke boundary
derivation.

### boundaries

act 2-1 does not:

- save player progress or history;
- install the witness arrangement into a new play session;
- privilege the witness over other solutions;
- search for another solution;
- generate a campaign;
- retain inverse polygon drawing;
- migrate old resources;
- implement arbitrary polygon union; or
- add another geometry domain.

### acceptance

act 2-1, and therefore `PROJECT-HAIL-MARY`, is complete when:

- lattice and hex-12 blueprint documents save as registered Godot resources;
- each saved resource loads and recompiles without loss;
- each exact replayed blueprint is an interior-disjoint arrangement obeying its
  palette and supply;
- partial-edge cancellation produces the correct coverage boundary;
- connected arrangements with holes produce one valid `Region`;
- empty, disconnected, point-connected, and non-manifold arrangements fail
  with typed errors and produce no level;
- no authoritative region or translation coordinate round-trips through
  `Vector2`;
- the compiled runtime `Level` contains the exact derived palette and region;
- replaying each stored witness into a fresh runtime state makes
  `State::is_solved()` true;
- existing runtime completion remains the exact doubled-area comparison;
- lattice behavior remains unchanged outside the superseded editor/resource
  workflow;
- native tests, GDExtension build, headless integration, persistence round
  trip, and `git diff --check` all pass together.

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

### act 1

- exact ids and display names for triangle, hexagon, and dodecagon;
- the precise catalog recipe representation;
- the checked precompiled `PaletteEntry` factory and its error type;
- the public location and spelling of `GeometryDomain`; and
- the domain-aware palette compiler's precise input and error surface.

### act 2

- the smallest usable input bindings and visible selection model;
- which old editor code is worth retaining;
- whether removal alone is sufficient or authoring undo belongs in this act;
- the exact blueprint draft and construct-and-recompile boundary;
- how exact join proposals are ordered and collapsed; and
- the minimum palette controls needed before arrangement construction.

### act 2-1

- the exact arrangement-boundary algorithm, ordering, and typed errors;
- the stable rational-orientation resource encoding;
- the blueprint resource's placement ordering and identity;
- how tampered resources report palette, orientation, supply, placement,
  arrangement, and boundary failures;
- the save-path and reopen interaction required beyond the persistence proof;
  and
- which superseded region resource classes and integration fixtures are
  removed.
