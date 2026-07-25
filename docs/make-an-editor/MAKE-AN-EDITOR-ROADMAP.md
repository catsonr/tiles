# make an editor roadmap

## status

this is a living roadmap. it records the current destination, decisions, act
boundaries, and known questions for `MAKE-AN-EDITOR`, but it is expected to
change as carson and the formalizer work through each act.

the roadmap is not an immutable implementation plan. each act will be discussed
separately and written as its own implementation plan only after its behavior
and boundary are satisfactory. once formalized, an individual act plan is
immutable; later changes require a new act or superseding plan.

the intended formalization sequence is:

```text
roadmap
   │
   ├── discuss act 0   ──▶ formalize act 0   ──▶ implementation
   ├── discuss act 0-1 ──▶ formalize act 0-1 ──▶ implementation
   ├── discuss act 0-2 ──▶ formalize act 0-2 ──▶ implementation
   ├── discuss act 0-3 ──▶ formalize act 0-3 ──▶ implementation
   ├── discuss act 1   ──▶ formalize act 1   ──▶ superseded before implementation
   ├── discuss act 1-1 ──▶ formalize act 1-1 ──▶ implementation
   └── discuss act 2   ──▶ formalize act 2   ──▶ implementation
```

acts are formalized in order. details deliberately left open by this roadmap
must be settled while formalizing the act which owns them, not guessed by an
implementor.

current act status:

```text
act 0    formalized and implemented
act 0-1  formalized
act 0-2  formalized
act 0-3  formalized
act 1    formalized, then superseded before implementation
act 1-1  formalized; this is the editor to implement
act 2    not yet formalized
```

## context

`PLACE-TILES` established:

- exact q16.48 coordinates, validated simple polygons, and triangulation;
- prototiles and distinct quarter-turn orientations;
- exact placements and pairwise interior-disjoint arrangements;
- full-edge and vertex-derived mating;
- engine-owned palettes, finite or unlimited supply, and typed commands;
- pure join previews;
- a Godot `Control` which renders a hardcoded tetromino arrangement; and
- a first join-oriented selection and placement interaction.

the current surface is a construction fixture, not a level player. act 0 added
an exact runtime `Level`, a temporary rectangular debug `Region`, region-aware
placement legality, and exact completion beneath it. the surface still boots
with a hardcoded unlimited tetromino palette and a twenty-placement debug
arrangement. it has no authored level resource, empty-level first placement,
deletion, undo, palette-resource workflow, region-authoring interface,
persistence, or navigation between authoring and play.

`MAKE-AN-EDITOR` turns that geometry and placement work into a level-authoring
loop and the real level player which consumes its output.

## destination

the milestone is complete when the application can:

1. boot into a purpose-built level editor backed by the application's canonical
   Godot-free prototile catalog;
2. author one connected polygonal region with zero or more polygonal holes;
3. build a nonempty palette from that catalog, assign every entry positive
   finite or unlimited supply, and choose its per-level color;
4. save the resulting level as a Godot resource and load it again;
5. launch the real level player with the current in-memory level, whether or not
   it has been saved;
6. render the region, its holes, the palette, and remaining supplies;
7. place a first tile into an empty region and continue placing legal tiles;
8. delete arbitrary placed tiles and undo successful placement or deletion;
9. reject every placement which overlaps another tile or leaves the region;
10. recognize exact completion without a polygon-union implementation; and
11. return from the player to the same editor draft.

the editor does not contain a reduced or disposable “playtest” simulation. the
application host routes its typed play request to the same `LevelPlayer` which
later campaign and progression code will use.

## project sequence

the broader project is now:

```text
PLACE-TILES
    │
    ▼
MAKE-AN-EDITOR
    │ produces playable level resources and the real player
    ▼
MAKE-IT-FUN
```

`MAKE-IT-FUN` will decide campaign structure, level selection, progression,
completion presentation, player-facing polish, and the final use of the
“count down” theme. those decisions are intentionally informed by levels
authored and played through this milestone.

## scope decisions

### level

for this milestone, a level is exactly:

```haskell
data Level = Level
  { palette :: Palette
  , region  :: Region
  }
```

there is one connected region, not a set of disconnected positive regions.
the region may contain any number of holes.

the rotation rule is implicit and uniform: every prototile requests the four
quarter turns:

```text
0, 1/4, 1/2, 3/4 turn
```

the existing orientation compiler continues to collapse geometrically
identical results, so a square exposes one distinct orientation rather than
four duplicate choices.

the rotation rule is not serialized per level or palette entry. tier-2 and
tier-3 geometry, non-quarter rotations, reflections, and scaling are out of
scope.

### prototiles

the application does not author prototile geometry.

the application ships one canonical Godot-free content catalog. each entry owns
one exact `Prototile`, a stable `PrototileId`, and a display name. the catalog
is the sole source of playable prototile geometry. it owns no color or other
level-specific presentation.

the editor saturates its palette builder from that complete ordered catalog.
level resources serialize ids and per-level palette presentation, never
prototile vertices, catalog names, or duplicate geometry.

the initial catalog contains exactly:

```text
ids 1–7    seven one-sided tetrominoes
ids 8–25   eighteen one-sided pentominoes
id 26      one domino
ids 27–34  squares of side 1 and 3 through 9
```

the existing `o` tetromino at id `1` is also the side-2 square. no second
congruent square identity exists.

the generic tier-1 core remains unrestricted: a `Prototile` may be any polygon
admitted by the existing exact pipeline. shipped catalog definitions live in a
separate Godot-free content layer rather than making particular game pieces a
geometry primitive.

scaling produces a distinct prototile. every included square side length
therefore has its own identity, except that the existing side-2 `o` geometry is
reused rather than duplicated. adding later exact tier-1 content extends the
catalog without changing the level-resource schema.

### per-level color

color belongs to one authored palette entry, not to canonical content:

```haskell
data PaletteEntryResource = PaletteEntryResource
  { prototileId :: Int
  , supply      :: Int
  , color       :: Color
  }
```

the editor generates a usable opaque default and permits the author to replace
it. color remains Godot presentation transport. exact `Palette`, `Level`,
placement legality, supply, region containment, and completion do not depend on
it.

### coordinates and the unit grid

the current editor snaps authored region vertices and direct placement input to
integer game coordinates. the smallest authored unit is therefore `1`.
geometry which conceptually uses halves can be scaled by two before authoring.

this is an editor policy, not a core, resource, region, or level invariant:

```text
core coordinates     = q16.48
resource coordinates = Godot Vector2 values, quantized once when compiled
editor snapping      = integer game coordinates in this milestone
```

resource and compiler contracts must not require whole-valued coordinates. an
authored region resource may contain safely quantizable fractional coordinates,
and a later editor may expose a fractional snap interval without migrating the
resource schema or exact domain types.

no lossy projected or screen coordinate becomes authoritative directly. region
authoring and direct tile placement first snap to an integer game coordinate,
then construct the corresponding exact q16.48 value.

### persistence

levels and their authored dependencies use registered Godot custom resources
and `.tres`/`.res` persistence through `ResourceLoader` and `ResourceSaver`.
json and a parallel bespoke serialization system are not required.

new levels require an explicit save path. an already loaded or previously saved
resource may subsequently save to its owned path.

resource objects are authored transport values. they are validated and compiled
into the exact core and engine model before play. Godot types never enter
`src/core/` or `src/engine/`.

### authoring versus play

the application boots into `LevelEditor`.

authoring and play are separate application states owned by the host:

```text
ApplicationHost
    │
    ├── LevelEditor ── typed request carrying current LevelResource ──▶ LevelPlayer
    │        ▲                                                        │
    │        └────────────────────── return ──────────────────────────┘
    └── owns the unchanged editor draft across the transition
```

the player does not require the resource to be saved first. loading an existing
resource and receiving an unsaved resource both use the same validation and
compilation boundary.

returning from play restores the same editor draft. the play arrangement is
session state and is never serialized into the level.

## region model

### definition

a region owns:

```haskell
data Region = Region
  { outerBoundary   :: Polygon
  , innerBoundaries :: [Polygon]
  }
```

the outer boundary contributes positive area. every inner boundary removes its
interior from that area.

each hole:

- lies strictly inside the outer polygon;
- does not touch the outer boundary;
- does not overlap or touch another hole; and
- remains a distinct complete polygonal boundary.

the region contains the outer polygon’s filled area minus every hole’s open
interior. the outer and inner boundary curves remain part of the closed region.
a tile may therefore touch or share an edge with either kind of boundary.

because holes cannot touch the outer boundary or one another, the positive
region remains connected. arbitrary boolean subtraction which cuts through the
outer boundary, creates disconnected positive components, or merges holes is
not part of this milestone.

### inverse authoring

region authoring has positive and negative modes:

- positive mode authors or replaces the one outer boundary;
- negative mode authors a new complete hole boundary.

closing a loop attempts to validate the complete resulting region. an invalid
outer boundary or hole is rejected without destroying the current valid region
or the still-editable draft loop.

“negative mode” is an authoring metaphor for adding a hole. it is not a general
polygon clipping or constructive-solid-geometry operation.

accepted boundaries are one-shot values. the editor does not move existing
vertices or select and remove individual holes. while a loop is open, its newest
point may be removed and the loop may be cancelled. after acceptance, correcting
the region means restarting and redrawing the complete region while the last
valid resource remains intact until the replacement outer loop succeeds.

### exact area

every public polygon is canonical and counterclockwise, so its exact doubled
area is positive. region doubled area is:

```text
doubledArea(region)
    = doubledArea(outer)
    - sum(doubledArea(hole) for hole in holes)
```

region construction proves that this result is positive.

### placement containment

a placement is contained by a region exactly when its closed footprint:

- lies within the closed outer polygon; and
- has no point in the open interior of any hole.

boundary contact remains legal. containment must be an exact geometric
predicate over authoritative q16.48 geometry. it must support convex and
concave footprints, outer boundaries, and holes. bounding boxes, vertex-only
tests, floating-point point-in-polygon tests, rendering masks, and rasterized
unit cells are not sufficient proofs.

the act-0 implementation plan must specify and test the complete containment
algorithm, including adversarial concave cases where all footprint vertices
appear acceptable but an edge or interior crosses excluded space.

## legality and completion

runtime state becomes conceptually:

```haskell
data Level = Level Palette Region
data State = State Level Arrangement
```

every successful insertion maintains:

```text
candidate belongs to the palette
∧ candidate obeys the compiled quarter-turn choices
∧ candidate does not exceed finite supply
∧ candidate interior overlaps no existing footprint interior
∧ candidate footprint is contained by the region
```

deletion cannot violate placement legality. finite supply use remains derived
from the current arrangement rather than stored as a second mutable counter.

because successful insertion maintains containment and the arrangement already
maintains pairwise interior-disjointness, completion is:

```text
solved(state)
    ⇔ sum(doubledArea(placement footprint)) = doubledArea(level region)
```

for a finite collection of closed polygonal footprints contained in the closed
region with pairwise-disjoint interiors, equal area leaves no uncovered
positive-area polygonal subset. the act-0 plan must record the exact assumptions
which make this equivalence valid and test outer-boundary and hole cases.

the runtime does not compute a polygon union and does not search for a solution.
the editor may save a structurally valid but unsolvable level. playing it to
completion is the author’s available witness that it is well-formed; no known
solution is stored in `LevelResource`.

## canonical catalog and authored resources

the catalog is application content, not a Godot resource:

```text
PrototileCatalog
    └── ordered [CanonicalPrototile]
                     ├── PrototileId
                     ├── exact Prototile
                     └── display name
```

the authored resource graph is:

```text
LevelResource
    ├── PaletteResource
    │     └── [PaletteEntryResource]
    │             ├── prototile id
    │             ├── supply
    │             └── per-level color
    └── RegionResource
          ├── PolygonResource outer
          └── [PolygonResource holes]
```

the initial property responsibilities are:

```haskell
data PolygonResource = PolygonResource
  { vertices :: PackedVector2Array
  }

data PaletteEntryResource = PaletteEntryResource
  { prototileId :: Int
  , supply      :: Int
  , color       :: Color
  }

data PaletteResource = PaletteResource
  { entries :: TypedArray PaletteEntryResource
  }

data RegionResource = RegionResource
  { outerBoundary   :: Ref PolygonResource
  , innerBoundaries :: TypedArray PolygonResource
  }

data LevelResource = LevelResource
  { palette :: Ref PaletteResource
  , region  :: Ref RegionResource
  }
```

resource `supply` uses:

```text
-1  = unlimited
> 0 = finite supply
0   = invalid
< -1 = invalid
```

Godot’s serialized integer is signed, while the exact domain uses unsigned
identities and supplies. the resource compiler must define checked conversion
bounds rather than cast negative or out-of-range values.

resource prototile id `0` is valid. negative ids are invalid. every nonnegative
id must resolve to one entry in the canonical catalog or compilation fails.

`displayName` comes from the canonical catalog. `color` comes from the authored
level palette entry. neither becomes part of core `Prototile` identity or
geometry.

only per-level region vertices cross the Godot numeric boundary. prototile
geometry is constructed exactly from canonical content definitions and is never
quantized from or serialized as `Vector2`.

## reversible play session

deletion is a typed mutation naming a stable `PlacementId`. removing an unknown
identity fails without changing state.

undo belongs to a play-session/history owner above the exact `State`, not to the
mathematical `Arrangement`. the intended mvp is value snapshots:

```haskell
data Session = Session
  { current :: State
  , undo    :: [State]
  }
```

before a successful placement or deletion, the session retains the prior exact
state. failure adds no history entry. undo restores the complete prior state,
including arrangement ordering, placement identities, the next-id allocator,
and therefore derived supply use.

redo is out of scope.

authoring history and play history do not mix. entering play creates a fresh
session history. returning to the editor discards the play arrangement and
reveals the unchanged level draft.

## player interaction requirements

the player must begin from an empty arrangement because arrangement state is not
part of a level. therefore it must provide a legal way to place the first tile.
the current proposal system only derives candidates from existing placement
features and cannot satisfy this requirement by itself.

the act-2 discussion must settle the final player input policy. the leading
option is direct integer-grid placement through the existing exact
`PlaceCommand`:

```text
mouse position
    ──▶ integer game-coordinate snap
    ──▶ exact q16.48 translation
    ──▶ placement preview
    ──▶ typed engine command
```

feature-derived proposals may remain as secondary snapping, be adapted to
region boundaries, or leave the player-facing interaction entirely. no act may
silently retain a system which cannot place into an empty level.

the roadmap requires behavior, not final bindings:

- visually select a palette entry and one of its distinct orientations;
- show the selected tile before placement;
- show whether the current candidate is legal;
- place a legal candidate;
- identify an existing placement from pointer input;
- delete the identified placement;
- undo the latest successful placement or deletion; and
- remain usable when a palette entry has no remaining finite supply.

pointer hit testing may be presentation-side and lossy when it only chooses a
stable `PlacementId`. lossy screen geometry must not construct or modify an
authoritative footprint.

## palette presentation

the player renders every palette entry in authored order. each entry shows:

- its level-authored color;
- its prototile shape;
- which entry is selected;
- the selected distinct orientation;
- `∞` for unlimited supply; or
- the remaining finite amount derived from the arrangement.

palette order remains the number of prototile entries, not the number of
distinct orientations or the sum of supplies.

the level editor builds an embedded `PaletteResource` directly from the complete
catalog. one visible row per catalog entry controls membership, unlimited or
positive finite supply, and opaque per-level color. row identity makes duplicate
and unknown ids unrepresentable; excluded rows are absent rather than encoded
with zero supply. resource order follows catalog order.

## application ownership

the milestone now has two meaningful application states, so the disposable
single-node scene is no longer sufficient.

act 1-1 produces a standalone `LevelEditor` which publishes a typed play
request carrying its current valid `Ref<LevelResource>`. it does not require a
placeholder player or own a transition to a player which does not yet exist.

act 2 introduces the minimal application host together with the real
`LevelPlayer`. the host owns both transitions and transports the current
`Ref<LevelResource>` from the editor to the player. it does not need to become a
general campaign scene machine.

the current class named `Editor` is conceptually the embryo of `LevelPlayer`.
its projection, arrangement drawing, selection, exact command submission,
ghost rendering, and useful proposal code may be retained. its hardcoded
tetromino construction, debug grid, overlap fixture, and assumption that an
arrangement is already nonempty must not survive as player requirements.

## acts

### act 0 — region and level semantics

act 0 establishes exact target geometry and makes region legality part of every
runtime placement.

it includes:

- the validated hole-bearing `Region` value;
- exact region-area calculation;
- exact polygon-footprint containment in the outer boundary and outside every
  hole interior;
- all outer/hole relationship validation;
- runtime `Level`;
- integration of `Level` into engine `State`;
- region-aware typed placement and mating failures;
- the exact solved query;
- read-only region and level observation needed by later rendering; and
- exhaustive native tests for convex, concave, boundary-contact, crossing,
  hole, overflow, and transactional-failure cases.

it does not include Godot resources, rendering, editor input, deletion, undo,
serialization, or completion presentation.

acceptance:

```text
given a palette and a region with holes,
State accepts exactly the placements legal for both,
and solved() becomes true exactly at equal contained area.
```

### act 0-1 — reversible arrangement state

act 0-1 makes play mutation reversible without weakening exact state ownership.

it includes:

- exact removal by `PlacementId`;
- a typed engine removal command and error;
- transactional missing-id behavior;
- a play-session history owner;
- undo for successful placement and deletion;
- no history entry for any failed operation;
- exact restoration of placement ids, allocator state, arrangement order, and
  derived supply;
- read-only used/remaining supply queries needed by the palette view; and
- native tests for removal, supply restoration, failed operations, and mixed
  place/delete/undo sequences.

it does not include pointer hit testing, controls, drawing, authoring history,
redo, or Godot.

acceptance:

```text
every successful play mutation can be undone to an observably identical
prior State; every failure leaves both State and history unchanged.
```

### act 0-2 — godot resources and exact compilation

act 0-2 establishes the authored resource graph and its one-way compiler into
the exact runtime model.

it includes:

- a Godot-free canonical content catalog with stable ids, exact prototiles,
  display names, temporary catalog colors, ordered lookup, and the existing
  tetromino definitions;
- registered polygon, palette-entry, palette, region, and level resources;
- inspector-visible properties and resource-type hints;
- id-only palette serialization and checked canonical-catalog lookup;
- checked conversion of region Godot values to q16.48 and resource supply to
  domain integers;
- implicit compilation of all four quarter turns;
- structured errors identifying the resource and compilation stage;
- saving a new resource to a chosen path;
- overwriting an already pathed resource;
- loading a saved resource;
- round-trip equivalence of authored values and compiled exact region and
  catalog-resolved palette geometry;
- one small authored level fixture for integration; and
- headless Godot verification in addition to dependency-free native tests where
  applicable.

it does not include an interactive editor, player, campaign loading, arbitrary
rotation rules, runtime prototile geometry authoring, pentomino content, or
additional square-size content.

acceptance:

```text
a LevelResource containing canonical prototile ids can be saved, loaded,
validated against the canonical catalog, and compiled into one equivalent exact
Level without serializing prototile geometry.
```

act 0-3 deliberately supersedes act-0-2's catalog-color and initial-content
boundaries after this foundation is complete.

### act 0-3 — complete initial catalog and per-level color

act 0-3 completes the first shipped polygon set and moves presentation color
from global content into each authored level palette entry.

it includes:

- retaining tetromino ids `1` through `7`;
- all 18 one-sided pentominoes at ids `8` through `25`;
- one domino at id `26`;
- squares of side `1` through `9`, with existing `o` id `1` serving as side `2`;
- exact private unit-cell bitmap-to-boundary construction for shipped
  polyominoes;
- fixed catalog order, ids, family-qualified display names, and orientation
  counts;
- removal of catalog color;
- per-level `Color` on `PaletteEntryResource`;
- authored color persistence beside id and supply;
- unchanged presentation-free exact engine compilation; and
- expanded native catalog and headless resource-integration coverage.

it does not include runtime polyomino construction, placement reflection,
hexominoes, square sizes above `9`, palette UI, or player rendering.

acceptance:

```text
the Godot-free catalog contains exactly the initial 34 unique polygon
identities, while every level palette entry independently persists its own
presentation color without moving color into the exact engine.
```

### act 1 — superseded region-only editor plan

act 1 was formalized as a region-only editor consuming an external
`PaletteResource`. before implementation, palette authoring and per-level color
were moved into the editor. the immutable act-1 document remains as the
region-authoring base contract, but its external-palette boundary must not be
implemented.

### act 1-1 — level editor

act 1-1 supersedes act 1 and produces a complete valid level resource through a
small internal authoring surface.

it includes:

- booting directly into a new unsaved `LevelResource`;
- an embedded palette built from all canonical catalog entries;
- one visible catalog row per exact prototile;
- shape preview, membership, positive finite or unlimited supply, and opaque
  per-level color;
- structurally preventing duplicate, unknown, zero-supply, and reordered
  palette entries;
- opening an existing draft and mapping its palette into catalog rows;
- positive outer-boundary drawing on the integer grid;
- negative hole-boundary drawing on the integer grid;
- visible open-loop previews and close/cancel behavior;
- exact validation feedback without destroying the last valid region;
- one-shot accepted boundaries and complete-region restart as the correction
  workflow;
- a visible integer grid with simple pan, zoom, and snapped-coordinate feedback;
- short visible instructions and ordinary, context-sensitive controls;
- clear invalid-draft diagnostics;
- save and save-as;
- a full compilation gate before every save;
- publishing a typed play request carrying the current in-memory valid
  `Ref<LevelResource>`.

it does not include palette files, palette import/export, palette reordering,
accepted-vertex editing, individual-hole removal, authoring undo/redo,
prototile geometry authoring, arbitrary boolean polygon subtraction,
disconnected positive regions, automated solvability search, campaign
metadata, a level selector, `LevelPlayer`, the application host, editor/player
transitions, or player completion polish.

acceptance:

```text
a human can choose exact catalog pieces, supplies, and colors; draw one outer
boundary and zero or more holes; save only a successfully compiled
LevelResource; reopen it; and publish the same current valid in-memory resource
through the typed play-request boundary.
```

### act 2 — level player and clear condition

act 2 turns the construction fixture into the real reusable `LevelPlayer`.

it includes:

- a minimal application host which owns `LevelEditor`/`LevelPlayer` transitions;
- receiving the editor's typed play request and transporting its exact current
  `Ref<LevelResource>` into the player;
- accepting and compiling any valid in-memory `LevelResource`;
- starting with an empty arrangement and fresh play history;
- rendering the positive region and visually clear negative holes;
- rendering all palette entries with their level-authored colors, orientations,
  selection, and remaining supply;
- placing the first tile and subsequent tiles under the settled exact input
  policy;
- legal/illegal placement previews;
- rejecting overlap, outside-region coverage, and supply exhaustion without
  corrupting session state;
- selecting and deleting arbitrary placements;
- undoing placement and deletion;
- checking the exact solved query after successful state changes;
- presenting a minimal unmistakable clear state;
- returning to the same unchanged editor draft through the application host;
  and
- removing every debug-grid and hardcoded-level assumption from the player.

the act-2 formalization must decide direct grid placement versus retained
feature-derived snapping, exact pointer behavior, placement anchoring, input
bindings, and the minimal clear presentation.

it does not include level selection, progression, save-game data, scoring,
timers, hints, automated solving, final audiovisual polish, or the final
countdown structure.

acceptance:

```text
the application host can give the same LevelPlayer an unsaved editor resource
or a loaded resource, play it from an empty arrangement with delete and undo,
report clear only after exact coverage of its hole-bearing region, and return
to the unchanged editor draft.
```

## dependency shape

```text
act 0: exact Region, Level, legality, solved
    │
    ├──────────────▶ act 0-2: Godot resources and compiler
    │                         │
    │                         ▼
    │                 act 0-3: catalog + color
    │                         │
    │                         ▼
    │                act 1-1: LevelEditor
    │                         │
    ▼                         │
act 0-1: deletion and history │
    │                         │
    └──────────────┬──────────┘
                   ▼
       act 2: ApplicationHost + LevelPlayer
```

act 0-1 is conceptually independent of resource representation. act 0-2
depends only on the public exact values established by act 0. act 0-3 completes
the catalog and color transport established by act 0-2. act 1-1 builds an
embedded palette and region, then terminates at a typed play request without
owning play semantics.
act 2 consumes all prior public surfaces, introduces the application host, and
contains the integrated player interaction and both application-state
transitions.

## verification principles

- `src/core/`, `src/content/`, and `src/engine/` remain free of Godot headers,
  values, and identifiers.
- every geometric and state invariant has dependency-free native coverage.
- resource registration, persistence, and compilation receive headless Godot
  coverage.
- failed construction, compilation, placement, deletion, loading, and saving
  paths are observed rather than logged and ignored.
- exact q16.48 values remain authoritative after the single resource
  quantization boundary.
- rendering and pointer selection never become a second source of geometric
  truth.
- no act relies only on a visual claim which can be asserted through exact
  state or headless integration.
- manual visual and interaction checks are recorded honestly when automation
  cannot establish them.

## explicitly deferred

- tier-2 uniform tilings and tier-3 geometry;
- non-quarter-turn rotation rules;
- reflected placements;
- in-application prototile creation;
- palette files, import/export, presets, and reordering;
- accepted-boundary vertex editing and individual-hole correction;
- authoring undo and redo;
- disconnected positive targets;
- arbitrary polygon boolean operations;
- automated solvability proof or solution search;
- known-solution serialization;
- redo;
- level selection and campaign progression;
- save-game progress;
- final completion effects, scoring, timers, hints, and polish;
- the final interpretation of the “count down” theme; and
- compatibility guarantees for resource schemas beyond what the jam needs.

these features are deferred, not disproven. current interfaces should avoid
gratuitously forbidding them, but no act may implement speculative machinery
for them without a new scope decision.

## handoff state after make-an-editor

`MAKE-IT-FUN` receives:

- one 34-entry extensible canonical catalog of reusable exact prototiles;
- persistent hole-bearing level resources;
- a purpose-built palette-and-region editor which emits only compiled-valid
  level saves;
- a reusable level player;
- exact placement legality and completion;
- palette rendering with per-level color and remaining supply;
- deletion and undo;
- several human-authored, manually solvable levels; and
- observed evidence about what placing tiles actually feels like.

that is the point at which progression and the countdown should be designed.
the next roadmap must respond to play rather than guess ahead of it.
