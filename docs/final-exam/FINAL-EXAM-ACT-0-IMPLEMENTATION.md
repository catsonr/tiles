# final exam act 0: one exact playable level

## immutable implementation contract

this document is immutable once handed to an implementor. do not revise it to
match implementation choices. stop and ask carson if a genuine contradiction
or behavior-changing ambiguity remains.

record results in:

```text
docs/final-exam/FINAL-EXAM-ACT-0-IMPLEMENTATION-NOTES.md
```

do not mention this plan or project act in code comments or runtime
documentation. do not commit.

## prerequisite and context seed

implementation begins only on a branch which contains both:

```text
1bdd085  project-hail-mary act-2-1-gamma implemented
4c233bc  configure unsigned-credential macos export
```

the `project-hail-mary` and `export-test` branches diverged at `02e8d8b`.
`export-test` by itself does not contain alpha, beta, or gamma. branch
reconciliation is carson's work, not this act's work. if either required commit
is absent, stop; do not merge, rebase, cherry-pick, or recreate the missing
surface.

before editing:

1. inspect and preserve concurrent work;
2. read `DEFINITIONS.md`, `COORDINATES.md`, `CARSON-PLAN.txt`, and the complete
   project-hail-mary roadmap;
3. read the alpha, beta, and gamma contracts and every implementation note;
4. read the complete current `LevelEditor`, its scene and integration runner,
   `PrototilePreview`, resource graph, resource compiler, level loader,
   `State`, `Session`, and engine commands;
5. inspect `~/Desktop/level.tres`; and
6. establish every current native, GDExtension, resource-integration,
   editor-integration, ordinary-startup, and `git diff --check` baseline.

do not run `scons` while the Godot editor has the scene being edited open.
gamma records an actual scene rewrite caused by doing so.

## goal

produce the smallest complete mechanical level-playing slice:

```text
one required .tres
        │
        ▼
load_level_resource
        │
        ▼
empty exact engine::Session
        │
        ▼
select + rotate + place + remove
        │
        ▼
State::solved()
        │
        ▼
visible "complete!"
```

the application boots directly into one `LevelPlayer`. it loads one baked
level, displays its palette and exact target region, lets the player construct
an arrangement through the existing exact engine commands, and exposes exact
completion.

presentation is intentionally minimal. this act proves mechanics, not the
exam theme, paper art direction, typography, animation, campaign shell, or
production layout.

## temporary level artifact

copy the exact artifact currently at:

```text
~/Desktop/level.tres
```

into the project as:

```text
godot-project/levels/act0-level.tres
```

do not hand-edit, normalize, re-export, simplify, or reinterpret it. the
current artifact is a self-contained hex-12 `LevelResource` with twelve
blueprint placements and a two-entry triangle/square palette. verify the
in-project copy through the public consumer loader.

if the source artifact is absent or no longer compiles through the reconciled
resource surface, stop and report the exact failure.

this is temporary boot content, not a new canonical fixture and not a campaign
format.

## scene separation and startup

preserve the completed editor as its own scene:

```text
godot-project/level_editor.tscn
```

the current `main.tscn` becomes that scene. preserve every existing editor node
and property, including concurrent export-dialog property changes. update
`LevelEditorIntegrationRunner` to load `res://level_editor.tscn` and update its
wording so it no longer calls the editor scene the main scene. no editor
behavior changes.

create:

```text
godot-project/level_player.tscn
```

with a `LevelPlayer` root, and point `project.godot` directly at it:

```text
run/main_scene="res://level_player.tscn"
```

there is no wrapper `main.tscn`, application controller, scene machine, title
screen, or navigation layer in this act.

register `LevelPlayer` and its integration runner with the existing
GDExtension registration boundary. do not bind core, content, or engine values
into Godot.

## required level path

`LevelPlayer` publishes one inspector-set path equivalent to:

```cpp
godot::String level_path;
```

give it a `.tres` file hint. `level_player.tscn` sets it to:

```text
res://levels/act0-level.tres
```

on `_ready()`, construct the canonical catalog and call the existing public
consumer:

```cpp
load_level_resource(level_path, catalog)
```

do not reproduce `ResourceLoader`, compilation, schema interpretation,
blueprint compilation, or region derivation inside `LevelPlayer`.

the required baked resource is trusted application content. a missing path,
loader failure, wrong resource type, or compilation failure is a programmer or
build error. report its typed detail to the engine error output and fail fast.
no recovery screen, disabled-player state, fallback level, file picker, repair,
migration, or friendly invalid-content flow is required. a hard assertion or
process failure is acceptable.

runtime level replacement and reloading are absent. the path is configured
before `_ready()` and is not a player-facing operation.

## authoritative player state

from `LoadedLevelResource`:

- retain the resource graph only as needed for authored palette colors;
- move or copy the compiled exact `engine::Level` into
  `engine::State`; and
- construct one `engine::Session` whose arrangement begins empty.

the loaded blueprint and its compiled proof arrangement are authoring
witnesses only. do not install either into player state, render either, use
either to generate hints, compare player placements against either, or
privilege the authored solution over another legal solution.

`Session::state()` is the only authoritative current play state.

palette colors remain Godot presentation values in authored palette order.
they never enter `State`, `Session`, commands, legality, supply, or completion.

## minimal scene presentation

the player scene needs only:

- a compact palette panel;
- the drawing canvas owned by the `LevelPlayer` root;
- a concise instruction/status line;
- an undo control only if the bounded undo attempt below succeeds; and
- one centered `Label` whose exact text is `complete!`.

the completion label begins hidden.

avoid decorative containers, theme systems, paper styling, custom fonts,
textures, animation, transitions, responsive-layout frameworks, and polished
menu composition. use plain Godot controls and readable temporary colors.

### palette

show one row per exact engine palette entry in authored order. each row shows:

- the entry's polygon through the existing `PrototilePreview` where practical;
- its authored color;
- enough selection indication to tell which entry is active; and
- current finite remaining supply or `unlimited`.

the row may show the catalog display name, but naming polish is not required.
do not confuse equivalent orientation labels with distinct compiled
orientations.

the first palette entry and its first distinct orientation are selected after
successful load.

### target region

draw the exact `State::region()` beneath proposals and placements:

- fill the outer polygon with one quiet target color;
- outline its boundary;
- cut every inner boundary back to the canvas background; and
- outline every inner boundary.

all rendered coordinates are the final lossy projection of exact q16.48
points. no rendered coordinate, triangulation result, or Godot polygon becomes
model input.

use the region's exact outer and inner polygon data. do not reconstruct a
target from the blueprint or player arrangement.

### camera

on successful load, and when the usable canvas size changes, fit the complete
region bounding box into the canvas with a fixed readable margin and center it.
handle degenerate screen sizes defensively.

act 0 has no pan, zoom, scroll bars, camera persistence, or player-controlled
view transform.

### arrangement and proposal

draw every current `Session::state().arrangement()` footprint in its palette
entry's authored color, with a readable outline. resolve color by exact
`PrototileId`, not by placement storage position, geometric equality, or a
copied color inside the engine.

draw the active exact proposal as one translucent ghost above the target and
arrangement. a proposal is either present in full or absent; there is no
sentinel placement or invalid ghost.

## selection and exact proposal interaction

retain the editor's already-proven mechanical grammar:

```text
click palette row / tab       select next palette entry
shift-tab                     select previous palette entry
r                             select next distinct orientation
shift-r                       select previous distinct orientation
left click on canvas          accept the highlighted exact proposal
right click on placed tile    remove that exact placement
ctrl-z                        undo, only if the bounded attempt succeeds
```

changing palette entry resets its orientation to the first distinct compiled
orientation. cycling wraps. exhausted finite supply produces no proposal for
that entry.

the palette controls and input callbacks call the same public player
operations used by the integration runner. do not build a second controller or
a test-only mutation path.

### first placement

when the arrangement is empty, offer exactly one direct `PlaceCommand` at exact
origin for the selected entry and orientation, and retain it only if:

```cpp
Session::state().preview(command)
```

succeeds.

the editor's blueprint contract begins at origin, so the baked level has at
least one authored solution reachable from that starting convention. do not
derive an exact translation from the pointer.

### later placements

when the arrangement is nonempty, enumerate:

- every complete anchor-edge × candidate-edge
  `MateFullEdgesCommand`; then
- every anchor-vertex × candidate-vertex `MateVerticesCommand`.

bounds come from the actual stored anchor footprint and selected oriented
candidate. preview every command through the current `State`. ordinary
rejection for containment, overlap, candidate lookup, orientation, supply, or
joining is not logged and creates no proposal.

retain the exact previewed `Placement` together with the exact typed command
which produced it. accepting a proposal applies that stored command through
`Session`; it does not reconstruct a command or translation from pixels.

as in the editor:

- retain deterministic discovery order;
- deduplicate proposals by exact selected identity, representative
  orientation, and q16.48 translation;
- offer only proposals sharing positive-length boundary contact with the
  existing arrangement; and
- let the pointer rank the already-exact proposal handles by projected
  distance.

floating-point projection decides presentation ranking only. it never creates,
rounds, snaps, clamps, or modifies authoritative geometry.

### mutation

a successful left click applies exactly the highlighted command through
`Session`. a right click hit-tests rendered footprints from topmost to
bottommost, resolves one stable `PlacementId`, and applies `RemoveCommand`
through `Session`.

after every successful placement, removal, or retained undo:

- discard every cached proposal, placement pointer, and hit-test result;
- rebuild proposals from the new current state;
- refresh supply and selection presentation;
- refresh completion;
- refresh concise status; and
- redraw.

a failed command leaves the complete current state and history unchanged,
reports concise status, and creates no invented fallback action.

## bounded undo attempt

wire one minimal undo action to the existing `Session::undo()`:

```text
ctrl-z
optional plain "undo" button
```

exercise it through the real player integration after placement and removal.
if it restores the exact prior arrangement, supply, allocator, proposals, and
completion without special repair, retain it.

if player integration exposes any defect, stale presentation state, or
behavior requiring changes below `src/game/`, remove the undo control and
binding entirely, record the observed failure in the implementation notes, and
continue. do not modify `Session`, `State`, core geometry, or engine tests to
rescue undo in this act. right-click removal is the required correction
mechanic; undo is not required for act acceptance.

redo is absent either way.

## exact completion

after initialization and every successful mutation, completion is exactly:

```cpp
Session::state().solved()
```

do not independently sum floating areas, compare tile counts, inspect the
blueprint witness, rasterize coverage, introduce tolerances, or store a
separate completion flag.

when `solved()` is true, show `complete!`. otherwise hide it. completion remains
live: removing a placement after solving hides the label, and an undo which
honestly restores the solved state shows it again.

act 0 does not freeze, submit, score, time, persist, or navigate away from a
completed level.

## public observation

publish only enough const observation for presentation and the integration
runner to verify:

- configured level path;
- whether the player has initialized;
- current `Session` or current const `State`;
- selected palette entry and orientation;
- current exact proposals and active proposal;
- authored entry color;
- projection used by drawing and pointer ranking; and
- whether the completion label should be visible.

read-only views are valid only while player state remains unchanged. do not
expose mutable engine, core, palette, region, arrangement, history, or resource
internals.

## integration verification

add:

```text
src/game/testing/LevelPlayerIntegrationRunner.h
src/game/testing/LevelPlayerIntegrationRunner.cpp
godot-project/tests/level_player_integration.tscn
```

drive production operations and the real `level_player.tscn`. no test-only
geometry insertion, mutable `State`, prebuilt player arrangement, blueprint
installation, or pixel-to-model seam may be added.

prove at minimum:

- the real scene names `res://levels/act0-level.tres`;
- the copied level loads through `load_level_resource`;
- the player session starts with an empty arrangement and no blueprint witness;
- palette order, exact ids, distinct orientations, authored colors, and
  finite/unlimited supply agree with the loaded artifact;
- the current region is the compiled exact region, including observation of
  inner boundaries;
- the region fit projection contains its complete bounding box inside the
  usable canvas margin;
- the default selection is the first entry and first distinct orientation;
- an empty arrangement offers only a state-proven origin placement;
- later proposals come from state-proven exact mating commands;
- candidates outside the region, overlapping the arrangement, or beyond
  finite supply are not offered;
- exact duplicate translations collapse deterministically;
- point-only contacts are not offered;
- pointer movement changes ranking only and never exact proposal geometry;
- left click applies exactly the ghosted command;
- right click removes exactly the hit stable placement;
- supply and proposals refresh after every successful mutation;
- the existing small canonical resource fixture can be supplied to a player
  instance and solved through ordinary player operations;
- completion is false before its final placement, true immediately after it,
  and the `complete!` visibility agrees;
- removal from the solved fixture makes completion false and hides the label;
- if undo is retained, its exact arrangement, supply, proposal, allocator, and
  completion restoration all pass;
- no player code reads blueprint placement geometry after loading;
- the editor integration runner loads `level_editor.tscn` and every existing
  editor behavior remains unchanged; and
- ordinary startup instantiates `LevelPlayer`, loads the act-0 level, and emits
  no error.

headless tests cannot honestly prove pixels. expose and assert the exact model
and projection data which drawing consumes, then perform the bounded manual
pass below.

## manual acceptance

with the Godot editor closed before every native rebuild:

1. boot the project and confirm it enters `LevelPlayer`, not `LevelEditor`;
2. confirm the temporary triangle/square level's target and two-entry palette
   appear without decorative application chrome;
3. select entries, cycle orientations, and confirm the ghost changes among
   exact legal placements;
4. place several tiles and confirm colors and remaining supply update;
5. right-click a placed tile and confirm only that tile disappears;
6. if undo survived integration, confirm it restores the exact visible prior
   state;
7. run the small canonical fixture through the player and complete it;
8. confirm `complete!` appears exactly on completion and disappears after
   removal; and
9. open `level_editor.tscn` directly and confirm the completed editor still
   behaves as before.

record exact observations and any retained undo limitation.

## verification

run the reconciled branch's established equivalents of:

```text
scons tests
./build/tests/tiles_tests
scons
./Godot.app/Contents/MacOS/Godot --headless --path godot-project \
    res://tests/resource_integration.tscn
./Godot.app/Contents/MacOS/Godot --headless --path godot-project \
    res://tests/level_editor_integration.tscn
./Godot.app/Contents/MacOS/Godot --headless --path godot-project \
    res://tests/level_player_integration.tscn
./Godot.app/Contents/MacOS/Godot --headless --path godot-project --quit-after 2
```

record exact before/after totals. native, resource, and editor totals are
unchanged regression baselines; the player runner is new.

also:

- compile modified translation units standalone with the project's established
  c++17, no-exceptions, and warning settings;
- run `git diff --check`;
- inspect the complete diff;
- verify no temporary files or exported binaries were added beyond the exact
  copied act-0 level artifact; and
- verify the implementor made no commit.

## boundaries

act 0 does not:

- implement a campaign, packet, page, title screen, difficulty, timer, credits,
  result screen, scoring, or progression;
- implement paper presentation, custom typography, textures, animation,
  transitions, or production visual polish;
- freeze or submit a solved level;
- persist player progress, history, selection, or view state;
- load, select, or replace a level at runtime;
- add pan, zoom, scroll bars, or camera controls;
- add redo;
- add arbitrary pointer-to-model placement, free dragging, or a snapping grid;
- add automated solving, hints, or blueprint display;
- change the level-resource schema, compiler, loader, or persistence contract;
- change `State`, `Session`, commands, core geometry, content compilation, or
  exact completion;
- change editor behavior while separating its scene;
- support disconnected, pinched, or generalized nonmanifold target regions;
- add geometry, prototiles, domains, reflections, scaling, or curves;
- implement controller or touch input;
- perform packaging, signing, notarization, export-workflow, or platform work;
  or
- broadly refactor `LevelEditor` in pursuit of code sharing.

small game-side drawing or projection helpers may be shared only when doing so
is narrower and safer than duplication. preserving the editor is more important
than removing a few repeated presentation lines.

## acceptance summary

act 0 is complete exactly when:

```text
project startup
  → LevelPlayer
  → required baked .tres loaded through public consumer
  → empty exact Session

player input
  → already-exact state-proven proposal
  → transactional Session mutation
  → exact arrangement + derived supply

State::solved() == true
  → visible "complete!"

editor scene
  → separately loadable
  → behavior unchanged

presentation
  → intentionally minimal
```

undo may be retained only if its bounded integration passes without changing
the engine. every other item above is required.
