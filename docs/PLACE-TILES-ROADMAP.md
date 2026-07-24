# place tiles roadmap

## destination

the first playable construction milestone is a small editor in which multiple
prototiles can be placed, joined without geometric drift, and rendered
together.

this is not yet a level player. it does not need to decide whether a target
admits a tiling or whether an arrangement solves a level.

## architecture

the domain vocabulary is fixed in `DEFINITIONS.md`. the authoritative
fixed-point coordinate system is fixed in `COORDINATES.md`. the repository
already builds and loads a minimal c++ GDExtension.

the authored code divides into three one-way layers:

```text
src/core/    namespace tiles          exact geometry and proof-bearing values
src/engine/  namespace tiles::engine  mutable game/editor state
src/game/    namespace tiles::game    godot input and presentation
```

their dependency direction is:

```text
authored content
      │
      ▼
tiles core validators and compilers
      │
      ▼
tiles::engine state ◀──── typed commands from tiles::game
      │
      └──────── read-only state ─────▶ tiles::game rendering
```

`tiles` owns all geometric truth. `tiles::engine` aggregates core values into
the current palette and arrangement and will become the only mutation surface
used by the game. `tiles::game` owns Godot nodes, input interpretation, lossy
coordinate projection, and rendering.

the engine exposes authoritative core polygons through read-only state; it does
not copy them into a second coordinate representation. the game converts
q16.48 `Point`s to Godot rendering coordinates at the final presentation
boundary. rendered coordinates never flow back into either engine state or core
geometry.

the immediate act sequence is:

```text
acts 0–2  exact tier-1 core
act 3     read-only engine state: Supply, PaletteEntry, Palette, State
act 3-1   typed engine commands which mutate State
act 4     first tiles::game surface and direct-to-editor rendering
```

act 3 is deliberately an intermediate aggregation step. act 3-1 establishes
the command boundary before act 4 introduces Godot, so the first game nodes can
consume rather than invent the application model.

act 3-1 exposes exact placement plus full-edge and vertex feature mating as
typed `State` commands. every candidate is selected from the engine-owned
palette through strong palette-entry and distinct-orientation indices. the
engine derives finite-supply use from the authoritative arrangement and
delegates geometry to the existing core; it exposes no mutable arrangement
view.

exact placement accepts an authoritative q16.48 `Point`. the engine does not
infer an integer grid, cell size, snapping rule, or translation restriction
from tetromino content, tier 1, or coordinate representation. those are future
input or explicitly authored level policies. likewise, full-edge and vertex
mating are convenient exact translation derivations rather than an exhaustive
classification of legal contact: exact placement may create any
interior-disjoint contact, including partial-edge contact with no coincident
polygon vertices.

## geometric capability tiers

the game core grows through three geometric capability tiers. every tier
publishes the same runtime product—a validated canonical q16.48 polygon for
each admitted prototile-orientation pair—so placement, intersection, joining,
arrangement storage, and eventual rendering do not depend on how that geometry
was compiled.

source compilers may perform one checked projection onto q16.48. after that
boundary, every gameplay predicate and transformation remains exact in every
tier: no runtime epsilon equality, tolerance test, or corrective movement of
authoritative geometry is permitted.

```text
authored prototile + finite orientation set
                       │
                 geometry tier
                       │
                       ▼
       canonical oriented q16.48 polygons
                       │
          exact translation and joining
                       │
                       ▼
                  arrangement
```

### tier 1 — lattice constructions

tier 1 admits polygons already represented on the q16.48 lattice and compiles
the quarter-turn orientations `0°`, `90°`, `180°`, and `270°` through exact
coordinate permutation and checked negation. geometrically identical
orientations are deduplicated. reflections are never generated as orientation
or placement transforms; a reflected shape may exist only as a separately
authored prototile.

this tier supports square-lattice constructions including the seven one-sided
tetrominoes, unequal-square and pythagorean-style arrangements, and
squared-square constructions. exact vertex-to-vertex joining complements the
full-edge join from act 1; any additional complete-edge, partial-edge, or point
contact is admitted when the existing whole-footprint predicate finds no
positive-area interior overlap.

act 2 implements tier 1 and supersedes acts 0 and 1 only where they deliberately
restricted placements and joins to the reference orientation.

### tier 2 — uniform tilings

tier 2 compiles shared direction modules for the euclidean uniform tilings. it
introduces regular prototiles with side counts:

```text
3, 4, 6, 8, 12
```

the modules support finite orientation systems of up to twelve turns. symbolic
rotation and boundary construction happen before one checked projection onto
q16.48, so symbolically identical vertices receive identical raw coordinates
and opposite direction vectors become exact negatives. uniform palettes have
order at most three, and compilation is organized around the relevant square-
and hexagonal-direction modules. all prototiles in one uniform domain share the
same projected direction vectors; obvious joins therefore continue to use
exact geometric comparison rather than a separately authored compatibility
table.

a later geometric act implements tier 2 after the tier-1 engine and game
vertical slice is working. its act number is intentionally not assigned here.

### tier 3 — bounded nonuniform compilation

tier 3 is deferred. no family, representation, error model, snapping rule, or
implementation technique is selected for it. any future tier 3 must publish the
same authoritative q16.48 product and preserve the exact-runtime boundary
above; nothing else is committed here.

## game core mvp

### represent prototiles

- implement the fixed-point `Coordinate`, `Point`, and validated `Polygon`
  types;
- represent prototile identity and reference geometry;
- compile one canonical quantized polygon for each relevant
  prototile-orientation pair through the active geometric tier; and
- represent placements as canonical oriented geometry plus exact fixed-point
  translation.

### compose placements

- represent a finite arrangement of placements;
- define lossless full-edge and vertex-to-vertex joins;
- derive joined translations through exact fixed-point arithmetic;
- preserve canonical geometry rather than repeatedly transforming prior
  results; and
- expose enough read-only arrangement state for an independent renderer.

pure alignment guarantees only that its selected boundary features agree
exactly. insertion retains the existing pairwise interior-disjointness
invariant; neither operation proves level legality or that an arrangement is a
solution.

## engine mvp

### state

- represent supply as either a positive finite amount or unlimited;
- group one prototile, its supply, and its distinct admitted
  `OrientedPrototile`s into one `PaletteEntry`;
- represent a nonempty authored-order `Palette`;
- define palette order as the number of entries, never the number of oriented
  variants or available pieces;
- own one palette and one authoritative `Arrangement` in
  `tiles::engine::State`; and
- expose both through read-only views.

levels are handcrafted. palette construction rejects duplicate
`PrototileId`s, but does not search for geometric congruence between different
entries; authored content is responsible for not listing congruent prototiles
as distinct types.

act 3 introduces this state without mutation commands. act 3-1 adds typed
commands for editing it while preserving the core invariants and keeping the
arrangement inaccessible for arbitrary external mutation.

the commands resolve palette-authored oriented candidates, derive configured
supply use from arrangement contents, and either place at an exact q16.48
translation or delegate full-edge/vertex feature mating to the core. they do
not encode geometry tiers, integer-grid policy, selection, or presentation
state.

## game mvp

### application surface

- place Godot-specific code in `src/game/` under `tiles::game`;
- replace the disposable smoke-test scene with one fullscreen editor surface;
- boot directly into that editor; and
- defer scene navigation, a title screen, and `SceneMachine` until the
  application contains more than one meaningful state.

### arrangement view

- define the one-way projection from authoritative polygons to godot rendering
  data;
- render palette reference polygons and arrangement footprints from
  `tiles::engine::State`;
- begin with a custom `Control::_draw()` surface rather than one node per
  placement; and
- keep Godot node lifetime and presentation state in `tiles::game`.

### editing surface

- display the engine-owned prototile palette;
- allow an oriented prototile to be added to the arrangement;
- allow the user to request a compatible lossless join; and
- refresh the rendered arrangement after core state changes.

game-side selection and any snapping policy produce typed engine commands.
feature mating can derive a translation from selected complete edges or
vertices; exact placement remains the general path for other legal contact,
including partial-edge contact which shares no polygon vertices.

the editor only needs enough interaction to demonstrate repeated tile
composition. production authoring tools and level-player interaction are
separate work.

## vertical milestones

1. aggregate one handcrafted palette and arrangement in
   `tiles::engine::State`;
2. mutate that state only through typed engine commands;
3. boot directly into a Godot editor which renders the engine-owned palette;
4. render an engine-owned arrangement containing placements;
5. join a second tile through the engine without coordinate drift; and
6. repeat joins while rendering the resulting arrangement.

each milestone integrates only the surface needed to make the next behavior
visible. regions, level rules, and the final player may reuse the same engine
state without changing the core-to-rendering direction.

## mvp complete

the milestone is complete when the application can:

- open directly into the editor;
- display a small palette of prototiles and their allowed orientations;
- add and losslessly join multiple tiles;
- render the authoritative arrangement after every edit; and
- repeat joins without accumulated transform or quantization drift.

## out of scope

- proving that a level or partial arrangement admits a tiling;
- level legality and solution checking beyond the arrangement's existing
  pairwise interior-disjointness invariant;
- target regions and level completion;
- the level-player interface and campaign flow;
- title-screen and multi-scene navigation;
- save data, polished content authoring, and production ui;
- automated tiling search; and
- rendering details beyond what is needed to distinguish and place tiles.

## open questions

- the authored input and checked projection contract for tier-2 uniform
  geometry;
- which concrete content requirement, if any, justifies tier 3;
- the eventual `Region`, `Target`, `RotationRule`, and `Level` engine
  materialization;
- the authored-content loading format;
- when the application becomes large enough to justify scene navigation; and
- the minimum editor interaction needed to select tiles, orientations, and
  joins.
