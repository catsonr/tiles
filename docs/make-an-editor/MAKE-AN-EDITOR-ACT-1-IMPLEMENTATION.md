# make an editor act 1: region authoring surface

## implementor context seed

this act replaces the construction fixture as the main scene with a small
internal `LevelEditor`. its job is deliberately narrow:

```haskell
authorRegion :: Valid PaletteResource -> IO (Valid LevelResource)
```

the editor accepts a separately authored palette, draws one exact region on the
integer game grid, and saves only a resource which successfully compiles into
an exact `engine::Level`.

this is not a general-purpose level-design environment. accepted boundaries are
one-shot values. palette membership and supplies are not edited here. the
surface must be clear and comfortable enough for carson to author a handful of
mathematical tiling levels without hidden controls or repeated setup.

seed the implementation session from the repository. this document fixes the
act-1 contract but does not replace the domain definitions, coordinate rules,
roadmap, or earlier immutable act plans.

before editing:

1. run `git status --short` and preserve every pre-existing user change;
2. enumerate authored files with `rg --files`, excluding generated build output,
   `Godot.app/`, and `vendor/`;
3. read this document completely;
4. read `README.md`, `DEFINITIONS.md`, `COORDINATES.md`, and
   `docs/make-an-editor/MAKE-AN-EDITOR-ROADMAP.md` completely;
5. read all completed `MAKE-AN-EDITOR` implementation plans and implementation
   notes through act 0-2 completely;
6. read the archived place-tiles act-4 and act-4-1 plans and implementation
   notes for the established Godot projection, drawing, focus, and input
   behavior;
7. read every authored file beneath `src/game/`, especially:
   - `src/game/resources/LevelResources.{h,cpp}`;
   - `src/game/resources/ResourceCompiler.{h,cpp}`;
   - `src/game/resources/LevelPersistence.{h,cpp}`;
   - `src/game/Editor.{h,cpp}`;
   - every completed resource integration runner; and
   - `src/register_types.{h,cpp}`;
8. read the canonical catalog public surface and the exact `Region`, `Level`,
   and compiler observation needed for presentation;
9. inspect `SConstruct`, `godot-project/main.tscn`,
   `godot-project/project.godot`, and every completed headless test scene;
10. inspect the pinned Godot 4.7 and godot-cpp headers for every `FileDialog`,
    `ConfirmationDialog`, `Button`, `Label`, signal, focus, input, drawing,
    resource-loading, and cache-mode api used; do not rely on recalled
    signatures; and
11. establish the completed act-0-2 baseline with:

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

if act 0-1 or act 0-2 is absent or unfinished, their implementation notes are
absent, native tests fail, resource integration fails, the GDExtension does not
load, or overlapping user work is changing a file this act must edit, stop and
ask carson rather than guessing against a moving interface.

after implementation, record the exact editor state, resource-loading behavior,
input bindings, camera constants, snap bounds and tie policy, scene structure,
button-state rules, save and play gates, integration coverage, final test totals,
build results, complete headless outputs, and honest manual interaction results
in:

```text
docs/make-an-editor/MAKE-AN-EDITOR-ACT-1-IMPLEMENTATION-NOTES.md
```

do not commit. do not mention this implementation document in code comments or
runtime-facing instructions.

## goal

produce one self-explanatory authoring path:

```text
new from PaletteResource              open LevelResource draft
           │                                      │
           └────────── validate palette ───────────┘
                              │
                              ▼
                    draw or restart outer loop
                              │
                    exact region compilation
                              │
                              ▼
                    add zero or more holes
                              │
                    exact region compilation
                              │
                              ▼
                  compile complete LevelResource
                         │               │
                         ▼               ▼
                     save/save as    play request
```

the defining invariant is:

```text
every file which LevelEditor reports as successfully saved
was successfully compiled as a complete LevelResource immediately before save
```

the editor may temporarily own:

- no resource;
- a resource with a valid palette and no region;
- a complete valid resource plus an uncommitted replacement loop; or
- a complete valid resource plus an uncommitted new-hole loop.

it never commits an invalid proposed boundary into the active
`LevelResource`. expected invalid author input is visible state, not a log-only
failure and not a partially mutated resource.

## deliberate scope reduction

act 1 does not build palettes.

palette ids and supplies are authored separately as `PaletteResource` values.
`LevelEditor`:

- accepts a palette resource;
- compiles it against the canonical catalog before accepting it;
- retains the exact selected `Ref<PaletteResource>`;
- shows one compact read-only summary; and
- never adds, removes, reorders, clones, repairs, or rewrites palette entries.

accepted region boundaries are also deliberately simple:

- an outer boundary is drawn once and accepted as a complete polygon;
- each hole is drawn once and accepted as a complete polygon;
- an open loop permits removal of its newest point or complete cancellation;
- an accepted boundary has no vertex handles or later vertex editing;
- an accepted hole cannot be selected, moved, or individually removed; and
- correcting accepted geometry means restarting and redrawing the complete
  region.

this constraint is intentional. the tool will author a small number of known
mathematical constructions. do not spend this act building interaction which is
useful primarily to operate the editor itself.

## architectural boundary

the dependency direction remains:

```text
canonical PrototileCatalog
            │
            ▼
mutable Godot resource graph
            │
            ▼
pure exact resource compiler
            │
            ▼
exact Palette / Region / Level
            │
            ▼
LevelEditor presentation and file actions
```

`LevelEditor` belongs to `tiles::game` and may own Godot values, resource
references, input state, camera state, and derived exact compiler results.

do not:

- add Godot dependencies to `src/core/`, `src/content/`, or `src/engine/`;
- add editor state to `LevelResource`;
- put an open boundary, camera, selection, path dialog, diagnostic, or dirty
  flag into an exact domain value;
- mutate a resource from a drawing callback;
- reproduce polygon or region validation in the game layer;
- construct a second serializer;
- cache exact compiled state inside a mutable resource;
- convert an exact region back into authored resource coordinates; or
- move palette construction into the canonical catalog.

the existing class named `Editor` remains the construction fixture and future
`LevelPlayer` embryo. do not rename, delete, or broadly refactor it in act 1.
remove it only from the main scene. it may remain registered and compiled until
act 2 consumes its useful player behavior.

## source and scene shape

add:

```text
src/game/LevelEditor.h
src/game/LevelEditor.cpp
```

with one registered root `godot::Control`:

```cpp
namespace tiles::game {

class LevelEditor final : public godot::Control {
    GDCLASS(LevelEditor, godot::Control)

protected:
    static void _bind_methods();

public:
    void _ready() override;
    void _draw() override;
    void _gui_input(
        const godot::Ref<godot::InputEvent> &p_event) override;
};

} // namespace tiles::game
```

private helper types and functions may live beside it when they directly model
the state in this plan. do not introduce a general document model, controller
framework, command bus, input manager, scene machine, editor plugin, dock,
autoload, or one node per vertex.

replace `godot-project/main.tscn` with one fullscreen `LevelEditor` root and a
small ordinary Godot `Control` hierarchy containing:

- one top toolbar;
- one palette-summary label;
- one short instruction label;
- one status label;
- the required open/save file dialogs; and
- at most one ordinary discard confirmation dialog for replacing an unsaved
  draft.

the root draws the grid, valid region, active loop, points, and snapped cursor.
standard child controls provide buttons and text. do not manually draw fake
buttons or make the author remember unlabeled screen regions.

use stable scene node names and inspect every required child before use.
partially malformed scene state must report one useful initialization failure
and remain non-crashing.

## editor state

the conceptual state is:

```haskell
data BoundaryKind
  = OuterReplacement
  | NewHole

data OpenLoop = OpenLoop
  { kind     :: BoundaryKind
  , vertices :: [IntegerGridPoint]
  , error    :: Maybe RegionDraftError
  }

data EditorDocument = EditorDocument
  { resource        :: Ref LevelResource
  , compiledPalette :: Palette
  , compiledRegion  :: Maybe Region
  , openLoop        :: Maybe OpenLoop
  , dirty           :: Bool
  }

data Camera = Camera
  { originPixels  :: Vector2
  , pixelsPerUnit :: Double
  }
```

equivalent storage is permitted only when it preserves these ownership facts:

- the active `LevelResource` is one exact `Ref`, not a periodically rebuilt
  parallel graph;
- the selected palette reference is retained unchanged;
- a compiled palette exists whenever a document is accepted;
- `compiledRegion` is absent exactly when the accepted resource has no region;
- a nonempty invalid proposal exists only in `openLoop`;
- the last valid resource region remains unchanged while a loop is open;
- camera and diagnostics are presentation state; and
- dirty state is observational UI state, not persistence authority.

the compiled palette and region are derived proof-bearing values owned by the
editor for read-only presentation. every save and play request must compile the
complete resource again rather than trusting those caches.

do not retain raw pointers or references into a compiled palette, catalog
vector, typed Godot array, or resource child across replacement of the owning
value.

## startup

the application boots into an empty `LevelEditor`, not into a hardcoded level
and not into the old twenty-placement fixture.

initially:

- no document is active;
- the world camera is centered on game origin at a useful fixed scale;
- the toolbar enables only `new from palette…` and `open draft…`;
- the instruction label says how to begin in one sentence;
- the status label is neutral;
- the palette summary says no palette is loaded; and
- drawing input which requires a document is ignored safely.

do not automatically load a fixture, create a default palette, invent a default
save path, scan a levels directory, or begin a region from a hardcoded shape.

## new from palette

the `new from palette…` button opens a resource-access `FileDialog` filtered to
`.tres` and `.res`.

load the selected path uncached with a `PaletteResource` type hint using the
pinned Godot 4.7 deep cache-ignore mode. then:

1. reject an empty or unsupported path;
2. reject loader failure;
3. reject a resource whose dynamic type is not `PaletteResource`;
4. call `compile_palette_resource` against the editor-owned canonical catalog;
5. on failure, preserve the complete current document and show the complete
   palette compiler diagnostic;
6. on success, construct one new `LevelResource`;
7. assign the exact loaded `Ref<PaletteResource>` to its palette;
8. leave its region null;
9. install the new document with the compiled palette and no compiled region;
10. mark it unsaved and dirty;
11. begin an empty outer-replacement loop immediately; and
12. update controls, instructions, summary, status, focus, and drawing.

the selected palette is an ordinary persistent resource reference. do not
duplicate it into an embedded palette, clear its path, inline it manually,
rewrite its entries, or save it again as a side effect of starting a level.
the later palette-authoring workflow may decide how palette resource files are
created; act 1 consumes them as supplied.

## opening a level draft

`open draft…` opens a resource-access `FileDialog` filtered to `.tres` and
`.res`.

the production `load_level_resource` operation requires a complete compiling
level and therefore cannot open a palette-bearing resource whose region is
still absent. the editor needs one narrow raw-draft loading path:

1. load uncached with the `LevelResource` type hint and deep cache-ignore mode;
2. reject loader failure or wrong dynamic type;
3. require and compile its palette;
4. if region is null, accept it as an incomplete draft;
5. if region is non-null, compile it independently;
6. reject an invalid non-null region with its complete nested error;
7. install only after every required check succeeds; and
8. preserve the current document exactly on every failure.

an accepted loaded resource retains the exact loaded `Ref<LevelResource>` and
its inherited owned resource path. do not clone it, normalize its authored
vertex arrays, re-save it during open, or replace nested resources merely
because compilation canonicalized the exact domain values.

after successful open:

- a missing region begins an empty outer loop immediately;
- a valid region is displayed in idle state;
- the document is not dirty;
- the palette summary is refreshed;
- a valid region is framed once in the viewport with a comfortable margin; and
- no dialog remains open.

raw draft loading is editor-local. do not weaken or overload the completed
production `load_level_resource` contract.

## replacing an active document

`new from palette…` and `open draft…` replace the active document.

if the current document is dirty, use at most one standard
`ConfirmationDialog` with plain language equivalent to:

```text
discard the current unsaved draft?
```

confirmation continues the already selected new/open action. cancellation
preserves the complete current document, open loop, camera, and status.

do not add a custom three-way save/discard/cancel workflow, autosave, recovery
file, recent-file list, close interception, or nested modal state machine.

## palette summary

the palette summary is read-only text derived from the accepted resource and
canonical catalog in authored order.

use one compact form equivalent to:

```text
palette: o × 3  ·  i × ∞  ·  t × 1
```

requirements:

- resolve display names through the same editor-owned canonical catalog;
- show `∞` for encoded unlimited supply;
- show the positive decimal amount for finite supply;
- preserve authored entry order;
- never expose distinct orientations as separate palette entries;
- do not permit clicking, selecting, editing, reordering, or deleting entries;
- do not render a prototile catalog browser, shape grid, supply spinner, or
  color picker; and
- treat an impossible post-validation lookup failure as an internal diagnostic,
  not as a fallback name which changes resource meaning.

the summary exists only to prevent authoring a region against the wrong palette.

## integer-grid camera and snapping

the editor uses one reversible presentation transform:

```haskell
toScreen camera p =
  camera.originPixels
  + camera.pixelsPerUnit * (p.x, -p.y)

toWorld camera pixel =
  ( (pixel.x - origin.x) / pixelsPerUnit
  , -(pixel.y - origin.y) / pixelsPerUnit
  )
```

screen projection remains lossy. pointer input becomes authoritative only after
the world value is snapped to an integer game coordinate and stored as that
integer.

use:

- a fixed initial scale near `32` pixels per game unit;
- a lower zoom bound no smaller than `4` pixels per unit;
- an upper zoom bound no larger than `128` pixels per unit;
- a constant wheel multiplier near `1.25`;
- cursor-centered wheel zoom;
- middle-button drag for panning; and
- no rotation, perspective, inertia, animation, minimap, scrollbars, or camera
  node.

record the exact chosen constants in the implementation notes.

snapping:

1. transform the local mouse position into presentation world coordinates;
2. round x and y independently to the nearest integer with one documented tie
   policy;
3. reject a non-finite result;
4. reject an integer outside the q16.48 whole-game-unit range;
5. store the accepted pair as signed integer game coordinates; and
6. when building a resource loop, convert each integer directly to an exactly
   representable `godot::Vector2` component.

do not quantize a raw pixel coordinate, multiply a Godot float by
`Coordinate::SCALE`, cast an unchecked large value, or use a rendered
`Vector2` as a core point.

the resource compiler remains the one q16.48 quantization boundary. integer
editor coordinates are exactly representable as `double`, and compilation
calls the completed quantizer exactly once per component.

an out-of-range snapped cursor is rendered conspicuously and cannot append a
point.

## grid and coordinate feedback

draw integer grid lines covering the visible canvas beneath the toolbar.

- ordinary grid lines are subtle;
- x and y axes are distinguishable;
- line density remains bounded by the minimum zoom;
- the current valid snapped coordinate is shown near the canvas edge or in the
  status area;
- the active snapped point has one small visible marker; and
- no numeric coordinate label is drawn on every grid intersection.

the grid is presentation only. its pixel lines do not define, validate, or
reconstruct polygon geometry.

on opening an existing valid region, fit its exact outer boundary once into the
available canvas with a modest margin and a scale clamped to the normal zoom
bounds. do not continuously auto-fit after input; that would make the canvas
move while the author is drawing.

## region drawing state

there are only three authoring states:

```text
idle with no region
idle with one valid region
drawing one outer replacement or one new hole
```

an outer-replacement loop may begin when a document exists. a new-hole loop may
begin only when a valid compiled region exists.

the accepted `LevelResource::region` is never cleared merely because an outer
replacement begins. the previous valid region remains visible beneath the
active replacement loop until:

- successful closure installs the replacement; or
- cancellation discards the loop and reveals the prior region unchanged.

if the document has no prior region, cancelling leaves it incomplete.

starting a replacement outer loop means the proposed successful region will
contain no holes. old holes are discarded only as part of the same successful
replacement commit.

## visible controls

the top toolbar contains exactly these author actions:

```text
new from palette…
open draft…
restart region
add hole
save
save as…
play
```

context rules:

- `new` and `open` are always available outside another modal dialog;
- `restart region` requires a document and no active loop;
- `add hole` requires a valid region and no active loop;
- `save`, `save as`, and `play` require a complete compiling resource and no
  active loop;
- `save` routes to `save as…` when the resource has no owned path; and
- irrelevant actions are visibly disabled, not accepted and ignored.

do not add palette controls, vertex-mode toggles, positive/negative terminology,
tool icons without labels, a context menu, hidden double-click action, or an
input-map dependency.

the user-facing labels are ordinary language:

- `restart region`, not “positive csg”;
- `add hole`, not “negative mode”; and
- `new from palette…`, not an unexplained blank document.

## drawing input

the canvas uses:

```text
left click        append the current snapped point
click first point close the loop after at least three distinct stored points
enter             close the loop after at least three stored points
backspace         remove the newest open-loop point
escape            cancel the complete open loop
mouse move        update snapped cursor and open-edge preview
middle drag       pan
mouse wheel       zoom around cursor
```

handle only pressed, non-echo key events and the required pointer events.
consume handled events. standard child controls and modal dialogs take
precedence over canvas authoring.

left click while no loop is open does not create a hidden loop. the toolbar
starts outer or hole drawing explicitly, except that a new/incomplete document
begins its first outer loop automatically.

appending the same snapped point as the newest stored point is ignored with a
small status explanation. clicking the first point closes rather than appending
the repeated closing vertex. the resource boundary always stores implicit
closure and never repeats the first vertex at the end.

backspace on an empty loop does nothing safely. changing any open-loop point
clears the prior closure error because that diagnostic described an older
proposal.

escape cancels the loop without mutating the resource or dirty state.

do not:

- append points continuously while dragging;
- infer closure from an arbitrary screen-distance radius;
- insert intersection points;
- simplify collinear points;
- prevent concavity;
- pre-reject an edge with a separate approximate intersection algorithm; or
- silently reorder winding.

the exact compiler owns all polygon and region validity.

## loop rendering

render, in order:

1. background and integer grid;
2. the last valid compiled region;
3. the active open-loop segments;
4. one preview segment from the newest stored point to the snapped cursor;
5. stored point markers;
6. a distinct first-point marker when closure is available; and
7. the current snapped cursor marker.

render the valid region from the compiled exact `Region`, not by treating
resource `Vector2` values as authoritative geometry.

the outer area uses one quiet translucent fill and clear boundary. holes are
visually cut out with the canvas background and retain a clear boundary. an
active outer replacement is distinguishable from a new hole without requiring
the author to remember a color code; the instruction text must also name the
current action.

one invalid closure keeps the proposed loop visible and changes its boundary or
status treatment enough to associate the displayed diagnostic with that loop.
do not flash, animate, continuously redraw, triangulate a separate collision
mesh, create a node per vertex, or cache projected polygons across camera
changes.

## closing and compiling an outer loop

closing an outer loop:

1. reject fewer than three stored points without leaving drawing state;
2. build a new `PolygonResource` from the stored integer vertices in authored
   order;
3. build a proposed `RegionResource` with that outer boundary and an empty
   correctly typed hole array;
4. call `compile_region_resource`;
5. on failure:
   - preserve the active loop and all its points;
   - preserve the active `LevelResource` and its prior region reference;
   - preserve dirty state;
   - store and display the complete nested error; and
   - remain in outer drawing state;
6. on success:
   - assign the proposed region to the active `LevelResource`;
   - install the returned exact `Region` as the compiled presentation region;
   - clear the open loop and its error;
   - mark the document dirty;
   - return to idle;
   - refresh controls and instructions; and
   - request one redraw.

do not compile the complete level merely to decide whether one proposed outer
boundary is geometrically valid. the independently reusable region compiler
exists for this boundary.

## closing and compiling a hole loop

closing a hole loop:

1. require an existing valid region;
2. reject fewer than three stored points without leaving drawing state;
3. construct one proposed `PolygonResource` from the integer points;
4. construct a new proposed `RegionResource`;
5. reuse the exact accepted outer-boundary resource reference;
6. copy the accepted typed hole references in authored order;
7. append the proposed hole exactly once;
8. call `compile_region_resource`;
9. on failure, preserve the complete active resource, prior region reference,
   compiled region, dirty state, and open-loop points;
10. on success, assign the proposed region, install the returned exact
    `Region`, clear the loop, mark dirty, return to idle, refresh, and redraw.

the resource's old `RegionResource` is not mutated in place while proving the
proposal. construct a candidate graph and publish it only after compilation
succeeds.

this copy-and-publish rule is the editor's transaction boundary. do not append a
hole to the live typed array and attempt to remove it after failure.

## complete-region restart

`restart region` begins one empty outer-replacement loop.

it does not:

- clear the current resource region;
- clear dirty state;
- save;
- change palette;
- delete an accepted hole in place; or
- require confirmation.

successful outer closure atomically replaces the entire accepted region with
the newly compiled no-hole region. cancellation restores the idle view of the
old region.

this is the only correction mechanism for accepted region geometry in act 1.

## instructions

the instruction label is always visible and changes with state.

use short text equivalent to:

```text
no document:
  choose “new from palette…” or “open draft…”

drawing outer:
  outer boundary — click grid points; click the first point or press enter to
  close. backspace removes a point; esc cancels.

drawing hole:
  new hole — click grid points; click the first point or press enter to close.
  backspace removes a point; esc cancels.

idle valid:
  add a hole, restart the region, save, or play.
```

include a compact second phrase or tooltip for:

```text
middle-drag pans · wheel zooms
```

do not require a separate README consultation for ordinary operation. do not
cover the canvas with a tutorial, wizard, or multi-step modal.

## diagnostics

expected authoring failures appear in the status label.

format complete typed errors at the presentation boundary for:

- palette compilation;
- polygon resource compilation;
- region compilation;
- level compilation;
- draft loading;
- saving; and
- an impossible play-request validation failure.

messages must name the useful authored identity when present:

- palette entry index and encoded id or supply;
- boundary kind;
- vertex index and coordinate axis;
- hole index;
- polygon failure;
- region relationship failure; and
- effective save path.

plain-language prefixes may wrap complete typed detail, for example:

```text
outer boundary is invalid: polygon self-intersection …
hole 2 is invalid: touches or leaves the outer boundary …
cannot save: palette entry 1 has supply 0 …
```

do not flatten compiler errors inside the compiler, log one line per vertex, or
show raw variant ordinals. expected invalid drawing is not an error-level Godot
diagnostic.

unexpected initialization, registration, impossible post-validation lookup,
or preview/publish disagreement may use one concise Godot error in addition to
the visible status.

## save and save as

save is available only when no loop is open and a complete resource appears
valid, but the handler must recheck rather than trusting button state.

the fixed algorithm is:

1. require an active non-null `LevelResource`;
2. require no open loop;
3. call `compile_level_resource` against the editor-owned catalog;
4. on compilation failure:
   - do not call `ResourceSaver`;
   - preserve resource, path, dirty state, and presentation;
   - show the complete error; and
   - refresh controls;
5. choose path behavior:
   - ordinary save with an owned path passes no explicit path;
   - ordinary save without an owned path opens save-as;
   - save-as supplies the selected explicit `.tres` or `.res` path;
6. call the completed `save_level_resource`;
7. inspect its complete typed result;
8. on failure, preserve the draft and dirty state and show the error;
9. on success:
   - retain the same resource `Ref`;
   - accept its resulting owned path;
   - mark the document clean;
   - show one concise success status naming the path; and
   - refresh controls.

use one resource-access save `FileDialog` with `.tres` and `.res` filters.
rely on its pinned built-in overwrite confirmation behavior if provided. do not
implement another overwrite dialog, automatic filename, extension correction,
json export, backup file, atomic-save claim, or save of the external palette as
a second action.

the lower persistence primitive intentionally permits invalid resources. the
editor's compile-before-save gate is therefore load-bearing and must not be
moved into or confused with `save_level_resource`.

## dirty indication

show the owned path or a clear `unsaved level` label and append a small visible
dirty marker after a successful region mutation.

dirty becomes true when:

- a new document is installed; or
- an outer or hole proposal is successfully committed.

dirty becomes false when:

- an existing draft is successfully opened; or
- save/save-as succeeds.

adding, removing, or cancelling open-loop points does not alter dirty because
the accepted resource has not changed.

dirty state does not intercept application close and is not serialized.

## typed play request

register one Godot signal:

```text
play_requested(level: LevelResource)
```

bind its argument as an object with the exact `LevelResource` resource-type
hint supported by pinned Godot 4.7.

the `play` handler:

1. requires an active document and no open loop;
2. calls `compile_level_resource` against the catalog;
3. on failure, emits nothing and displays the complete error;
4. on success, emits `play_requested` with the exact current
   `Ref<LevelResource>`; and
5. does not save, clone, clear dirty state, construct a play `State`, or mutate
   the editor resource.

pointer identity is intentional. act 2's host receives the same current
in-memory draft, including one which has never been saved.

act 1 has no listener, placeholder player, scene transition, host, play
arrangement, or completion behavior. in the standalone editor, pressing play
validates and emits the signal; a concise status may say that the valid request
was published.

## redraw and focus

the root control explicitly accepts keyboard focus after successful
initialization and reclaims it after a file dialog closes or a toolbar action
begins drawing.

request redraw after:

- initialization;
- document install or failed install;
- camera pan or zoom;
- snapped cursor change;
- open-loop point change;
- closure success or failure;
- cancellation;
- region restart or hole start;
- save status change when visible drawing changes; and
- any control-state transition affecting the canvas.

do not add `_process`, physics processing, a timer, continuous redraw, polling,
worker threads, or an observer framework.

`_draw()` and `_gui_input()` must tolerate absent catalog initialization,
document, region, loop, snapped cursor, or scene child.

## headless editor integration

add one dedicated scene-only runner:

```text
src/game/testing/LevelEditorIntegrationRunner.h
src/game/testing/LevelEditorIntegrationRunner.cpp
godot-project/tests/level_editor_integration.tscn
```

the runner may use one narrow testing peer or production controller seam to
drive the same operations as the UI. it must not receive a duplicate region
builder, compiler, save implementation, or alternate editor state.

register it only because its dedicated test scene instantiates it. it never
enters the main scene.

verify headlessly:

- `LevelEditor` and every required standard child instantiate;
- initial state contains no document and exposes the correct enabled actions;
- a valid palette resource begins a new incomplete level;
- invalid, empty, duplicate-id, unknown-id, and invalid-supply palettes are
  rejected without replacing an existing document;
- the palette summary preserves order, names, finite amounts, and `∞`;
- integer snapping produces exact integer resource components, including
  negative coordinates;
- non-finite and out-of-q16.48-range cursor values cannot append;
- closing with fewer than three points remains open;
- duplicate newest points do not append;
- closing never repeats the first vertex in the resource array;
- one valid outer loop commits one valid no-hole region;
- an invalid outer loop remains editable and leaves the prior region reference
  unchanged;
- backspace clears the newest point and stale closure diagnostic;
- escape cancels without mutating the resource or dirty state;
- one valid hole appends after existing holes and compiles;
- holes outside, touching the outer boundary, or touching/overlapping another
  hole remain uncommitted with their complete typed errors;
- restart preserves the old region until a replacement succeeds;
- successful replacement removes old holes atomically;
- raw open accepts a valid palette with a null region;
- raw open accepts a complete valid level and frames its region;
- raw open rejects wrong types, invalid palettes, and invalid non-null regions
  while preserving the current document;
- save/save-as handlers refuse an active loop;
- a complete valid resource saves, receives the correct path, reloads uncached
  through `load_level_resource`, and compiles equivalently;
- a deliberately corrupted in-memory palette is rejected by the immediate
  pre-save compilation gate and no save call occurs;
- save failure preserves dirty state;
- play failure emits no signal;
- play success emits exactly once with the pointer-identical current
  `LevelResource`;
- new/open replacement cancellation preserves document, loop, camera, and
  dirty state; and
- every generated file is removed.

use exact temporary paths beneath:

```text
res://.godot/tiles_level_editor_integration/
```

follow the completed resource runner's narrow cleanup rules. remove only exact
files created by this runner and the directory only when empty. never
recursively remove `.godot`, `godot-project`, `res://`, `user://`, or a path
derived from unchecked input.

the runner prints one concise success summary, reports failed assertions through
the established test surface, and exits nonzero on failure.

## manual interaction verification

this act has visual and interaction acceptance which headless assertions cannot
honestly establish.

run the main scene visibly and verify:

1. the empty startup explains how to begin;
2. ordinary controls are legible without remembering keyboard shortcuts;
3. choosing a palette begins outer drawing;
4. grid snapping and coordinate feedback are obvious;
5. panning and cursor-centered zoom remain stable;
6. an open concave loop, closing edge, first point, and snapped cursor are
   distinguishable;
7. an invalid closure remains visible with a useful correction message;
8. backspace and escape behave as the visible instructions claim;
9. a committed region and holes are visually unambiguous;
10. toolbar enablement follows state;
11. save-as produces a resource at the selected path;
12. opening that resource reproduces the region and palette summary; and
13. no interaction requires knowledge absent from the visible instruction text.

author at least:

- one concave no-hole region; and
- one outer region with two separated holes.

save, reopen, and compile both.

record what was actually observed. if the implementor cannot launch or inspect
a visible window in the available environment, say so and ask carson to perform
the bounded manual pass; do not claim visual success from a headless draw call.

## source and build layout

expected new files:

```text
src/game/LevelEditor.h
src/game/LevelEditor.cpp
src/game/testing/LevelEditorIntegrationRunner.h
src/game/testing/LevelEditorIntegrationRunner.cpp
godot-project/tests/level_editor_integration.tscn
docs/make-an-editor/MAKE-AN-EDITOR-ACT-1-IMPLEMENTATION-NOTES.md
```

expected modifications:

```text
src/register_types.cpp
godot-project/main.tscn
```

small changes to an existing game-side diagnostic helper are permitted only if
they remove exact duplication without coupling the construction fixture to
`LevelEditor`.

do not change:

- `src/core/`;
- `src/content/`;
- `src/engine/`;
- resource schemas;
- resource compiler or persistence semantics;
- canonical catalog definitions;
- the old `Editor` interaction or rendering behavior;
- native domain tests merely to accommodate game-layer code;
- project rendering or stretch settings; or
- the `.gdextension` descriptor.

recursive extension discovery already finds new `.cpp` files. do not add a
special production source list. `src/game/` remains excluded from the native
test executable.

## runtime diagnostics

ordinary startup may print at most one concise readiness line. file actions and
boundary authoring use visible status text rather than terminal spam.

do not log:

- mouse motion;
- every snapped coordinate;
- every appended point;
- every grid line;
- every draw;
- every compiler attempt;
- every toolbar enablement change; or
- every signal connection.

the dedicated integration runner may print its one summary. the old `Editor`
fixture does not instantiate in the main scene and therefore must not emit its
bootstrap/proposal diagnostics during ordinary act-1 startup.

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
    res://tests/level_editor_integration.tscn
./Godot.app/Contents/MacOS/Godot \
    --headless \
    --path godot-project \
    --quit-after 2
```

also perform the manual visible interaction pass above.

independently compile new translation units under the repository's established
c++17 and no-exception assumptions if the ordinary build does not make warning
provenance clear.

grep-verify:

- no Godot dependency entered `src/core/`, `src/content/`, or `src/engine/`;
- no palette editing control or catalog browser was added;
- no accepted-boundary vertex editor, individual-hole editor, or authoring
  history was added;
- no invalid proposed region mutates the active resource;
- no save path can bypass complete compilation;
- no json or bespoke serializer was added;
- no old construction fixture node remains in `main.tscn`; and
- no generated integration file remains.

the ordinary headless run must:

- load the GDExtension and all required custom classes;
- instantiate `LevelEditor` as the main scene;
- reach its draw path without a document;
- print no old tetromino fixture or proposal diagnostics;
- contain no unexpected error or warning; and
- exit successfully.

## acceptance

act 1 is complete when:

- the application boots into one standalone `LevelEditor`;
- empty startup explains how to create or open a draft;
- a new draft accepts one separately authored valid `PaletteResource`;
- palette compilation failure preserves the current document;
- an existing resource may open with a valid palette and either no region or
  one valid region;
- the editor shows the accepted palette in a compact read-only summary;
- no palette membership or supply editing exists;
- region input snaps only to integer game coordinates;
- the canvas provides a visible grid, coordinate feedback, simple pan, and
  cursor-centered zoom;
- outer and hole loops have visible points, open-edge preview, close,
  backspace, and cancel behavior;
- exact compiler errors remain visible and correctable;
- a failed proposal preserves the last valid region and the complete open loop;
- successful proposals atomically publish complete `RegionResource` values;
- accepted boundaries have no vertex editor;
- restart is the one correction path for accepted geometry;
- save and save-as are impossible during an open loop;
- every save attempt compiles the complete resource first;
- compiler failure prevents `ResourceSaver` from being called;
- successful save retains the same resource and correct owned path;
- play emits the exact current valid in-memory resource and never saves it;
- the standalone act has no player or application host;
- all native tests remain green;
- resource integration remains green;
- dedicated editor integration passes and cleans up;
- the GDExtension builds cleanly;
- ordinary headless startup is clean;
- manual authoring proves the controls and instructions are usable; and
- implementation notes contain exact results and honest visual findings.

## explicitly out of scope

act 1 does not implement:

- palette membership editing;
- palette supply editing;
- a canonical catalog browser;
- palette-resource creation beyond selecting an existing resource;
- prototile geometry authoring;
- accepted-boundary vertex selection, movement, insertion, or deletion;
- individual accepted-hole selection, removal, or replacement;
- authoring undo or redo;
- arbitrary polygon clipping or boolean operations;
- automatic winding correction or polygon repair;
- freehand/drag drawing;
- fractional editor snapping;
- disconnected positive regions;
- region templates or procedural region generation;
- solvability proof, known solutions, or automated search;
- autosave, recovery, backups, recent files, or close interception;
- a general project/document framework;
- `LevelPlayer`;
- `ApplicationHost`;
- a placeholder play simulation;
- editor/player transitions;
- arrangement state;
- campaign metadata, level selection, or progression;
- controller or touch input;
- theming, animation, sound, or visual polish beyond legibility;
- a Godot editor plugin; or
- schema migration and compatibility guarantees.

if the pinned Godot 4.7 resource loader cannot open an incomplete
palette-bearing `LevelResource` without invoking complete compilation, stop and
report the exact loader behavior. do not weaken production load validation or
serialize a parallel draft format.

if a standard `Control` root cannot receive canvas input reliably around the
ordinary child controls in the specified scene shape, make the smallest
possible split into one registered or scene-local canvas control and record it.
do not replace the standard toolbar with manually hit-tested drawing.

if saving an externally pathed `PaletteResource` reference inside a
`LevelResource` does not round-trip through the completed persistence contract,
stop and report the exact resource graph and Godot error. do not silently clone,
inline, or rewrite the palette.
