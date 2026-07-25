# make an editor act 1: implementation notes

## scope of this record

act 1-1 supersedes act 1 before implementation, so there is one editor and one
record. this document covers the combined contract: the base act-1 region,
camera, save, play, and diagnostic rules, with act 1-1's embedded palette,
startup, and control replacements applied.

the obsolete external-palette workflow was never built. there is no
`new from palette…` action, no palette `FileDialog`, and no read-only palette
summary label anywhere in the delivered scene or source; the integration runner
asserts all three absences.

`git status --short` was empty at session start. nothing was committed.

## delivered files

new:

```text
src/game/LevelEditor.h
src/game/LevelEditor.cpp
src/game/PrototilePreview.h
src/game/PrototilePreview.cpp
src/game/testing/LevelEditorIntegrationRunner.h
src/game/testing/LevelEditorIntegrationRunner.cpp
godot-project/tests/level_editor_integration.tscn
docs/make-an-editor/MAKE-AN-EDITOR-ACT-1-IMPLEMENTATION-NOTES.md
```

modified:

```text
src/register_types.cpp
godot-project/main.tscn
```

nothing under `src/core/`, `src/content/`, `src/engine/`, `src/game/resources/`,
or `tests/` was touched. `src/game/Editor.cpp` is unchanged and still compiles
and registers; it is simply no longer in the main scene.

## scene structure

`godot-project/main.tscn` is one fullscreen `LevelEditor` root plus ordinary
Godot controls. the root draws the canvas and handles canvas input directly —
no split into a second registered canvas control was needed, because the canvas
region has no child covering it and every chrome control blocks its own area.

```text
LevelEditor                                   (root; draws grid/region/loop)
├── Toolbar                    ColorRect      y 0 .. 52
│   └── Margin/Actions         MarginContainer/HBoxContainer
│       ├── NewLevelButton     "new level"
│       ├── OpenDraftButton    "open draft…"
│       ├── RestartRegionButton"restart region"
│       ├── AddHoleButton      "add hole"
│       ├── SaveButton         "save"
│       ├── SaveAsButton       "save as…"
│       ├── PlayButton         "play"
│       ├── Spacer             Control (expand, mouse ignore)
│       └── PathLabel          Label
├── PalettePanel               ColorRect      x 0 .. 360, y 52 .. h-96
│   └── Margin/Body            MarginContainer/VBoxContainer
│       ├── PaletteTitle       Label
│       └── PaletteScroll      ScrollContainer (horizontal scroll disabled)
│           └── PaletteRows    VBoxContainer  ← 34 rows built at runtime
├── StatusBar                  ColorRect      y h-96 .. h
│   └── Margin/Body            MarginContainer/VBoxContainer
│       ├── InstructionLabel   Label (autowrap)
│       └── Line               HBoxContainer
│           ├── StatusLabel    Label
│           ├── Spacer         Control (expand, mouse ignore)
│           └── CoordinateLabel Label
├── OpenDialog                 FileDialog (resources, open file, .tres/.res)
├── SaveDialog                 FileDialog (resources, save file, .tres/.res)
└── DiscardDialog              ConfirmationDialog
```

the three chrome bars are opaque `ColorRect`s rather than `PanelContainer`s
because the default theme's panel stylebox is translucent and the root draws
*beneath* its children: a translucent bar would let grid lines show through the
toolbar and palette panel. this is the smallest change which keeps the base
act's "root draws, standard children provide buttons and text" shape.

`bind_scene()` resolves every required child by stable path, casts it to its
expected type, and reports **one** message naming every missing or mistyped
child before returning false. a partially malformed scene leaves the editor
without a document, without a catalog, and non-crashing; `_draw()` and
`_gui_input()` both tolerate that state.

the canvas rectangle is derived from the actual child rects each draw:

```text
left   = PalettePanel.position.x + PalettePanel.size.x
top    = Toolbar.position.y + Toolbar.size.y
bottom = StatusBar.position.y
right  = LevelEditor.size.x
```

so moving or resizing a bar in the scene moves the canvas with it.

### one layout correction made from an observed frame

the first delivered layout used a 44 px toolbar and a 64 px status bar. a single
rendered frame (see "manual interaction verification") showed the status bar
clipping the second line of its instruction text: 64 px minus 8 px of margin
leaves 56 px for a two-line autowrapped label plus the status/coordinate line,
which needs roughly 74 px. the toolbar was also within about 1 px of the default
theme's minimum button height plus its margins.

the toolbar is now 52 px and the status bar 96 px, with the palette panel's
offsets moved to match. this is the only change made in response to a visual
observation, and it is the only visual claim in this document.

## editor state

```text
Document
├── resource        Ref<LevelResource>          the one authoritative draft
├── compiled_palette optional<engine::Palette>  derived, presentation only
├── compiled_region  optional<Region>           derived, presentation only
├── rows            vector<PaletteRow>          one per catalog entry, in order
├── open_loop       optional<OpenLoop>          the only place an invalid
│                                               proposal may exist
└── dirty           bool                        observational UI state
```

`compiled_palette` is absent exactly when the resource palette has no entries.
`compiled_region` is absent exactly when the resource has no region. neither
cache is ever trusted by save or play: both recompile the complete resource.

camera and snapped-cursor state live outside the document, as presentation
state. no exact value is stored inside a resource, and no resource is mutated
from a drawing callback.

### the one editor-owned transaction rule

every proposal is built as a **candidate graph beside the live one** and
published only after the exact compiler proves it:

- a palette edit builds a new unpathed `PaletteResource` with new unpathed
  entries and assigns it only after `compile_palette_resource` succeeds;
- an outer closure builds a new `PolygonResource` and a new `RegionResource`
  with an empty hole array;
- a hole closure builds a new `RegionResource` reusing the **accepted**
  outer-boundary `Ref` and copying the accepted hole `Ref`s in order, then
  appending the proposed hole exactly once.

nothing is ever appended to a live typed array and removed after failure.

## startup

`_ready()` binds the scene, builds the canonical catalog, creates the 34 rows,
enables keyboard focus, installs one new document, grabs focus, and prints
exactly one line:

```text
[tiles] level editor ready: 34 catalog rows
```

the new document owns an unpathed `LevelResource` containing an unpathed empty
`PaletteResource` and a null region. it is dirty and unsaved, every row is
excluded with unlimited supply and amount 1, the camera is centred on game
origin at 32 px/unit, boundary drawing is idle, and save/save-as/play are
disabled.

no fixture is loaded, no default palette invented, no save path guessed, no
levels directory scanned, and no region seeded.

## palette panel

one row per catalog entry, created programmatically from the runtime catalog in
catalog order. `main.tscn` hand-authors no per-prototile node.

each row is:

```text
[CheckBox] [PrototilePreview 40x40] [Label display name          ]
                                    [OptionButton] [SpinBox] [ColorPickerButton]
```

- the `OptionButton` items are `unlimited` (index 0) and `finite` (index 1);
- the `SpinBox` is integer-stepped with `min 1`, `max 1_000_000`,
  `use_rounded_values`, and neither `allow_greater` nor `allow_lesser`;
- the `ColorPickerButton` has `edit_alpha` off, and every stored and published
  colour is additionally forced to alpha `1` in the editor callback, so the
  no-alpha guarantee does not depend on the control alone;
- supply and colour controls are disabled on an excluded row but keep their last
  local choices; re-including restores them;
- row identity comes from the catalog index. no control names an id, so
  duplicate and unknown ids are unrepresentable rather than validated away.

signals from the dynamically created controls are connected with
`Callable(...).bind(row_index)`, so one bound handler serves all 34 rows. a
`suppress_row_signals_` guard makes a control written from row state
indistinguishable from no event at all, so syncing never loops back as an edit.

### default row colours

```text
hue        = fractional_part((catalog_index + 1) * 0.6180339887498948)
saturation = 0.65
value      = 0.95
alpha      = 1
```

exactly as the act specifies. this is editor presentation: canonical content
still owns no colour, and `Editor::fixture_color` is untouched and unused here.

### palette publication

after one row change the candidate palette is rebuilt from **all** rows in
catalog order, including only included ones. if no row is included the empty
candidate is assigned, `compiled_palette` is cleared, the document is marked
dirty, and the status reads

```text
choose at least one tile to complete the palette
```

with no error-level diagnostic. otherwise `compile_palette_resource` runs and
the candidate is assigned only on success. an unexpected compiler failure
restores the previous row state into the visible controls, preserves the active
resource and dirty state, shows the complete typed error, and reports one
concise `push_error`. that path is unreachable through the controls and is
deliberately not asserted away.

because publication constructs and replaces, the first palette edit after
opening a level whose palette is an externally pathed resource detaches the
level into a new embedded palette and leaves the external file untouched. the
runner proves this by reloading the external palette from disk afterwards.

## integer-grid camera and snapping

recorded constants:

```text
initial scale       32.0 px per game unit
minimum zoom         4.0 px per game unit
maximum zoom       128.0 px per game unit
wheel factor         1.25 (cursor-centred, both directions)
frame margin        48.0 px on every side when framing an opened region
whole-unit range    [-32768, 32767]
finite supply range [1, 1_000_000]
```

projection:

```text
to_screen(p) = origin_pixels + pixels_per_unit * (p.x, -p.y)
to_world(px) = ((px.x - origin.x) / ppu, -(px.y - origin.y) / ppu)
```

snapping rounds x and y independently with **exact ties away from zero** —
`v < 0 ? ceil(v - 0.5) : floor(v + 0.5)` — chosen to agree with the tie policy
`quantize_double` documents, so the editor and the lattice never disagree about
which way a half lands. a non-finite world value or an integer outside
`[-32768, 32767]` produces no snapped point at all.

an unsnappable cursor is drawn as a conspicuous red cross at the actual pointer
position (not at an invented lattice position), the coordinate label reads
`(out of range)`, and clicking reports the reason and appends nothing.

integers are converted to `Vector2` components exactly once, when a boundary
resource is built. nothing multiplies by `Coordinate::SCALE`, quantizes a raw
pixel, or casts an unchecked value; the resource compiler remains the single
q16.48 boundary.

pan is middle-button drag. zoom is the wheel, cursor-centred, clamped. there is
no rotation, inertia, animation, minimap, scrollbar, or camera node, and no
`_process`, timer, or polling anywhere in the editor.

## drawing

`_draw()` renders, in order: the canvas background rect, the integer grid, the
compiled region, the active open loop, then the snapped cursor.

the grid draws only the whole units inside the visible world extent, so its
density is bounded by the minimum zoom (at 4 px/unit a 1000 px canvas is 250
lines). the `x = 0` and `y = 0` axes use a brighter colour.

the region is drawn from the compiled exact `Region`, never from resource
`Vector2` values. filling uses **the core's own certified triangulation**: each
`Triangle` is projected and drawn as a three-point polygon. that fills concave
boundaries correctly without asking Godot to re-triangulate a boundary the core
has already decomposed, and it needs no second geometry representation. holes
are then re-filled with the canvas background colour and outlined in a distinct
colour, so they read as cut out rather than as overlapping shapes.

an active outer replacement is green, a new hole is amber, and a failed closure
turns the whole loop red; the instruction label also names the current action in
words, so no colour code has to be remembered. the closing edge appears once the
loop has three points, and the first point gains a white ring when closure is
available.

nothing animates, redraws continuously, caches projected polygons, or creates a
node per vertex.

## input bindings

```text
left click          append the snapped point, or close when it is the first
                    point and at least three points are stored
enter / kp enter    close the loop
backspace           remove the newest point
escape              cancel the loop
mouse move          update the snapped cursor and the open-edge preview
middle drag         pan
wheel up/down       zoom around the cursor
```

only pressed, non-echo key events are handled, and only while a loop is open.
mouse events are handled only inside the derived canvas rect, so the toolbar,
palette panel, status bar, and any open dialog take precedence. handled events
are consumed with `accept_event()`.

a left click with no open loop never creates a hidden loop: outer and hole
drawing start from the toolbar only, except that opening a draft whose region is
null begins its outer loop immediately, which is the base act's rule.

appending the same snapped point as the newest stored point is ignored with a
status line. clicking the first point closes rather than appending, so the
stored ring never repeats its first vertex. backspace on an empty loop is safe.
any change to the points clears a stale closure diagnostic. escape mutates
neither the resource nor dirty state.

## save, play, and dirty state

save is gated by:

```text
a non-null LevelResource
∧ no open loop
∧ compile_level_resource succeeds  ← rechecked in the handler, not trusted
```

on compilation failure the saver is **not reached at all** and the draft, its
path, and its dirty state are preserved. `save` with no owned path routes to the
save dialog; `save as…` always opens it; a pathless save overwrites the owned
path. the completed `save_level_resource` is the only writer, and its typed
result is inspected. success keeps the same `Ref`, adopts the returned path,
marks the document clean, and shows the path.

play applies the same gate and then emits

```text
play_requested(level: LevelResource)
```

with the exact current `Ref<LevelResource>` — pointer identity is intentional.
it never saves, clones, clears dirty state, or builds a play `State`. act 1 has
no listener; the standalone editor just reports that the request was published.

dirty becomes true on document install and on every successful palette or region
commit; false on successful open of an intact draft and on successful save.
adding, removing, or cancelling open-loop points does not change it. it is shown
as a `•` after the path (or after `unsaved level`) and is never serialized or
used to intercept application close.

## diagnostics

complete typed errors are formatted at the presentation boundary only, in the
anonymous namespace of `LevelEditor.cpp`. no compiler flattens its own
alternatives, and nothing produced there flows back down. messages name the
useful authored identity: palette entry index with its encoded id or supply,
boundary kind, vertex index and axis, hole index (including both indices for a
disjointness failure), and the effective save path. examples the runner asserts
on:

```text
this outer boundary is invalid: the boundary crosses itself
this hole is invalid: hole 0 are invalid: it is not strictly inside the outer boundary
cannot open …: palette entry 0 names prototile id 999, which is not in the catalog
```

expected invalid authoring is status text, never a Godot error. only
initialization failure, an impossible post-validation catalog lookup, and an
unexpected palette-publication failure use `push_error`, and each does so in
addition to visible status.

## headless integration

`src/game/testing/LevelEditorIntegrationRunner.{h,cpp}` with
`godot-project/tests/level_editor_integration.tscn`. it is registered only
because that scene instantiates it and never enters the main scene.

it **loads and instantiates `res://main.tscn` itself**, so every check observes
exactly the scene the application boots into, and drives it through the editor's
own authoring operations. there is no duplicate region builder, compiler, save
implementation, or alternate editor state in the file; the only local helpers
are resource constructors for negative fixtures and two by-value comparison
functions.

**218 checks, 0 failures.** coverage, in run order:

```text
startup        document, unsaved path, dirty, null region, empty embedded
               palette with no path, no compiled palette/region, idle drawing,
               all seven toolbar buttons present with correct enablement
rows           exactly 34 rows in catalog order; every display name; every
               exact preview polygon compared with same_boundary; pentomino f
               vs f mirrored proven geometrically distinct; all rows excluded,
               unlimited, amount 1, deterministic opaque default colours, with
               supply/colour controls disabled; adjacent defaults differ
palette        one inclusion publishes one entry with id, unlimited supply, row
               colour, and no resource path; finite mode encodes the amount;
               0, -7, 5_000_000, and 2.7 are clamped/rounded to 1, 1, 1_000_000,
               3; colour keeps rgb with alpha forced to 1; exclusion removes the
               entry rather than storing zero; re-inclusion restores supply and
               colour; out-of-order inclusion still serializes catalog order;
               ids provably unique and known; empty palette disables save/play
region         integer probes (including negatives) snap back to themselves; a
               sub-cell nudge snaps to the nearest point; far-away and
               non-finite cursors do not snap; out-of-range append rejected;
               backspace on an empty loop safe; duplicate newest point ignored;
               two-point closure refused without leaving drawing state;
               backspace removes the newest point; escape mutates neither
               resource nor dirty state; a self-intersecting closure keeps all
               four points, sets the failed-closure state, changes nothing, and
               reports its typed reason; changing a point clears that
               diagnostic; a concave six-point boundary commits with six stored
               vertices, no repeated first vertex, and exact authored integers;
               negative coordinates compile to exact lattice values
holes          a hole outside the outer boundary, one touching it, one
               overlapping another hole, and one touching another hole are all
               rejected with the accepted region reference and compiled region
               unchanged; two separated holes commit in authored order and
               reuse the accepted outer-boundary resource
restart        starting a replacement does not clear the accepted region;
               cancelling restores it unchanged; a successful replacement
               removes both old holes atomically and publishes a new resource
preservation   palette edits preserve region, compiled region, camera, and an
               active open loop; region edits preserve every row's state and the
               compiled palette
new level      a dirty draft defers to the confirmation; cancelling preserves
               the complete document; confirming installs a new one and restores
               every row default
save/play      save and play refuse during an open loop and write nothing; a
               deliberately corrupted in-memory palette is caught by the
               pre-save gate with no file written and no play emission; save-as
               writes the file, takes the path, and cleans the document;
               load_level_resource reloads uncached and compiles equivalently
               with every id, supply, and colour intact; an unsupported
               extension fails and preserves dirty state; a pathless save keeps
               the owned path; play emits exactly once with the pointer-
               identical resource and does not change dirty state
open           a complete level opens clean, frames its region inside the
               ordinary zoom bounds, restores every row and its visible
               controls, and leaves unlisted rows at their defaults; a valid
               palette with a null region opens and begins an outer loop; an
               empty palette opens as an editable draft which cannot save; a
               null palette is repaired to empty and marked dirty; unknown ids,
               invalid regions, wrong types, unwritten paths, and empty paths
               are all rejected with the current document preserved exactly;
               the first palette edit after opening an externally pathed palette
               publishes a new embedded palette and leaves the external file
               unchanged on disk
absence        no palette file dialog, no "new from palette" action, no
               read-only palette summary label; the generated rows do exist
cleanup        every temporary file removed, directory removed when empty
```

temporary files live only under
`res://.godot/tiles_level_editor_integration/` and are removed by exact name:
`editor_level.tres`, `palette_only.tres`, `null_palette.tres`,
`empty_palette.tres`, `bad_palette.tres`, `bad_region.tres`,
`wrong_type.tres`, `external_palette.tres`, `external_level.tres`. the
directory is removed only when empty. nothing recursive and nothing derived from
unchecked input is ever removed; the directory's absence was confirmed from the
shell after the run.

like the resource runner, this one deliberately loads a `.tres` path that was
never written in order to distinguish loader failure from every other rejection.
the engine emits its own three-line diagnostic for that read; it is expected
output from a negative check and is the only non-`[tiles]` output the scene
produces.

## verification

commands, exactly as run from the repository root:

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

results:

```text
scons tests                              ok
./build/tests/tiles_tests                4295 checks, 0 failed across 336 cases
scons                                    ok, no warning from any authored unit
res://tests/resource_integration.tscn    220 checks passed, exit 0
res://tests/level_editor_integration.tscn 218 checks passed, exit 0
--quit-after 2                           one readiness line, exit 0
```

native totals are unchanged from the act-0-3 baseline (`4295 / 0 / 336`), and
correctly so: everything this act adds lives in `src/game/`, which is
deliberately outside the native test target.

ordinary headless startup, in full:

```text
Godot Engine v4.7.1.stable.official.a13da4feb - https://godotengine.org

[tiles] level editor ready: 34 catalog rows
```

the GDExtension loads, `LevelEditor` instantiates as the main scene, all 34
rows and their previews are constructed, the draw path is reached with a
document and no region, no tetromino-fixture or proposal diagnostic appears, and
there is no unexpected error or warning.

independent syntax check of every new translation unit plus
`src/register_types.cpp` with
`g++ -std=c++17 -fno-exceptions -Wall -Wextra -fsyntax-only`: **zero
diagnostics originating in `src/`**. the only output is `vendor/godot-cpp`'s own
unused-parameter noise from its macro-generated headers, which predates this
act.

### grep verification

```text
Godot in src/core, src/content, src/engine   only the pre-existing ExactInteger comment
palette file dialog / catalog browser        none
accepted-boundary vertex or hole editor      none (no vertex handle, drag, insert, move)
authoring undo/redo                          none
invalid proposal mutating the active resource none (candidate-then-publish only)
save path bypassing complete compilation     none (save_document is the only writer)
json or bespoke serializer                   none
old construction fixture node in main.tscn   none
generated integration files after exit       none
```

## manual interaction verification — NOT DONE

**the bounded manual pass in the act document was not performed, and nothing in
this record should be read as a visual acceptance claim.**

what actually happened: the application was launched windowed once
(`--path godot-project`, OpenGL compatibility renderer on the M1) and it
initialized cleanly with no error or warning. one frame was then recovered with
Godot's own `--write-movie` and inspected. that single frame showed the palette
panel populated with legible rows — checkbox, shape preview, display name,
`unlimited` dropdown, amount spinner, and a distinct colour swatch per row, all
visibly belonging to the same row — the scrollable list, the grid on the canvas,
and the instruction text. it also showed the status bar clipping its second line,
which is the layout correction recorded above. carson then stopped the capture
work and will perform the visual pass.

so the following remain **unverified** and are carson's to check:

1. the empty startup explains how to begin;
2. ordinary controls are legible without remembering keyboard shortcuts;
3. all 34 entries are reachable through ordinary scrolling;
4. reflected pentominoes can be distinguished by eye (the runner proves only
   that the previews receive geometrically different polygons);
5. grid snapping and coordinate feedback are obvious;
6. panning and cursor-centred zoom feel stable;
7. an open concave loop, its closing edge, its first point, and the snapped
   cursor are distinguishable;
8. an invalid closure stays visible with a useful correction message;
9. backspace and escape behave as the visible instructions claim;
10. a committed region and its holes are visually unambiguous;
11. toolbar enablement visibly follows state;
12. save-as produces a resource at the selected path and reopening it reproduces
    the region and the palette rows;
13. finite/unlimited switching is obvious and the colour picker shows no alpha;
14. palette work does not resize, cover, or steal pointer input from the canvas;
15. empty-palette feedback reads as clear rather than alarming;
16. the toolbar and status bar are now correctly sized after the correction;
17. the two required authored levels — one concave no-hole region, and one outer
    region with two separated holes — save, reopen, and compile;
18. the required palette exercise — a tetromino, a chiral pentomino and its
    mirror, the domino, `square 1`, the `o`/side-2 square, and one square of
    side 3..9, with both finite and unlimited supplies and visibly different
    colours — is comfortable to assemble.

the headless runner covers the *state* behind most of these; none of it is
evidence about legibility, spacing, or feel.

## points for the next formalizer or implementor

1. **act 2's host is the only listener `play_requested` needs.** the signal
   carries the pointer-identical current `Ref<LevelResource>`, including one
   which has never been saved, and the editor holds that same `Ref` afterwards.
   returning from play must therefore not assume the resource was copied.
2. **`Editor` is now dead in the running application.** it still compiles and
   registers, but nothing instantiates it. act 2 should take its projection,
   arrangement drawing, and command submission into `LevelPlayer` and delete the
   fixture, `fixture_color` with it.
3. **the canvas rect is derived from three named scene children.** if act 2
   reuses this scene shape for the player, keep that derivation rather than
   hardcoding offsets — it is what keeps input, grid, and framing agreeing.
4. **accepted boundaries have no vertex editor, by design.** `restart region` is
   the only correction path. if authoring a large region turns out to be painful
   in practice, that is a new scope decision, not a bug in this act.
5. **the editor's `[1, 1_000_000]` finite bound is an authoring convenience.**
   opening a resource authored elsewhere with a larger positive supply clamps
   only the *row control*, and the clamped value is republished the moment the
   author edits that row. if larger supplies ever matter, raise the control
   bound; the resource and compiler contracts already admit them.
6. **`ColorPickerButton` with `edit_alpha` off was not trusted alone.** every
   colour that reaches row state or a published entry is passed through an
   explicit alpha-to-one normalization, so opacity does not depend on control
   behaviour in a future engine version.
