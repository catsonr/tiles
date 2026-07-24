# place tiles roadmap

## destination

the first playable construction milestone is a small editor in which multiple
prototiles can be placed, joined without geometric drift, and rendered
together.

this is not yet a level player. it does not need to decide whether a target
admits a tiling or whether an arrangement solves a level.

## current foundation

the domain vocabulary is fixed in `DEFINITIONS.md`. the authoritative
fixed-point coordinate system is fixed in `COORDINATES.md`. the repository
already builds and loads a minimal c++ GDExtension.

the next work divides into a plain c++ game core and a godot presentation
surface:

```text
godot input ──commands──▶ game core
                           │
                      arrangement
                           │
godot rendering ◀──read-only view
```

the core owns all geometric truth. godot displays that state and forwards
editing intent; rendered coordinates never flow back into the model.

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

act 3 implements tier 2 after tier 1 has established orientation-bearing
placements and the oriented-geometry seam.

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

## godot renderer mvp

### application surface

- replace the disposable smoke-test scene with real application structure;
- extract the reusable `SceneMachine`/`BXScene` foundation from
  `rhythm-game`, without importing its game-specific states or assets;
- create a new title screen; and
- create a level-editor state reachable from the title screen.

### arrangement view

- define the one-way projection from authoritative polygons to godot rendering
  data;
- create and update one visual tile for each placement;
- render multiple placements together from a core arrangement; and
- keep godot node lifetime and presentation state outside the game core.

### editing surface

- provide a small prototile palette;
- allow an oriented prototile to be added to the arrangement;
- allow the user to request a compatible lossless join; and
- refresh the rendered arrangement after core state changes.

the editor only needs enough interaction to demonstrate repeated tile
composition. production authoring tools and level-player interaction are
separate work.

## vertical milestones

1. construct and render one canonical prototile;
2. render a core-owned arrangement containing several independent placements;
3. join a second tile to a selected tile without coordinate drift;
4. repeat joins while rendering the resulting arrangement; and
5. reach the working editor through the title-screen scene flow.

the core and renderer may advance independently against a minimal arrangement
view. each vertical milestone integrates only the surface needed to make the
next behavior visible.

## mvp complete

the milestone is complete when the application can:

- open on a title screen;
- enter the level editor;
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
- save data, polished content authoring, and production ui;
- automated tiling search; and
- rendering details beyond what is needed to distinguish and place tiles.

## open questions

- the exact read-only contract between the core and renderer;
- the authored input and checked projection contract for tier-2 uniform
  geometry;
- which concrete content requirement, if any, justifies tier 3;
- how much of the existing scene-machine transition and stack behavior the mvp
  retains;
- which godot rendering primitive displays a polygon; and
- the minimum editor interaction needed to select tiles, orientations, and
  joins.
