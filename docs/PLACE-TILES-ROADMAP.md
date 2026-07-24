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

## game core mvp

### represent prototiles

- implement the fixed-point `Coordinate`, `Point`, and validated `Polygon`
  types;
- represent prototile identity and reference geometry;
- produce one canonical quantized polygon for each relevant
  prototile-orientation pair; and
- represent placements as canonical oriented geometry plus exact fixed-point
  translation.

### compose placements

- represent a finite arrangement of placements;
- define a lossless join operation between compatible boundary features;
- derive joined translations through exact fixed-point arithmetic;
- preserve canonical geometry rather than repeatedly transforming prior
  results; and
- expose enough read-only arrangement state for an independent renderer.

a successful join guarantees that its selected boundary features agree
exactly. it does not by itself prove that the whole arrangement is legal,
non-overlapping, or part of a solution.

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
- complete arrangement legality and solution checking;
- target regions and level completion;
- the level-player interface and campaign flow;
- save data, polished content authoring, and production ui;
- automated tiling search; and
- rendering details beyond what is needed to distinguish and place tiles.

## open questions

- which boundary features the first join operation supports;
- how compatible joins are selected and represented;
- the exact read-only contract between the core and renderer;
- when and where canonical oriented polygons are generated;
- how much of the existing scene-machine transition and stack behavior the mvp
  retains;
- which godot rendering primitive displays a polygon; and
- the minimum editor interaction needed to select tiles, orientations, and
  joins.
