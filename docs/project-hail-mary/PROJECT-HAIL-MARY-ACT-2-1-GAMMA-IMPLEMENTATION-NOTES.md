# project hail mary act 2-1 gamma — implementation notes

Notes from giving the act-2 authoring surface one nonvisual coverage proof and
one export operation: a region derived at every publication, a `.tres`-only
filesystem save dialog, and an extended headless runner.

## verification

| step | result |
| --- | --- |
| `scons tests` | builds clean |
| `./build/tests/tiles_tests` | **9921 checks, 0 failed across 427 cases** (unchanged) |
| standalone c++17 / `-fno-exceptions` / `-Wall -Wextra` | both modified units clean; two pre-existing unused constants, see below |
| `scons` | GDExtension builds, no warnings or errors |
| `res://tests/resource_integration.tscn` | **414 checks passed** (unchanged) |
| `res://tests/level_editor_integration.tscn` | **308 checks passed** (was 151; gamma extends its subject) |
| ordinary headless load | `[tiles] level editor ready: 37 canonical identities, choose lattice or hex12`, zero error lines |
| `git diff --check` | clean |
| temporary artifacts | none: the runner's export directory is gone after the run |

Baselines re-established before editing, and what they are now:

```text
native                9921 / 0 / 427  →  9921 / 0 / 427 (unchanged)
resource integration  414 checks      →  414 (unchanged)
level editor          151 checks      →  308 (gamma extends its subject)
ordinary run          37 identities   →  37 identities (unchanged)
GDExtension build     clean           →  clean
```

The editor-integration total is the only one that moved, and it moved by
addition: no existing check was removed, renamed, weakened, or changed to accept
a different result. `151 + 157 = 308`.

Nothing under `src/core/`, `src/content/`, `src/engine/`, `src/game/resources/`,
`tests/`, or `ResourceIntegrationRunner` was touched at all, which is why the
native and resource totals are bit-for-bit unchanged. `SConstruct` needed no
edit — no file was added or deleted.

No commit was made.

### one interruption worth recording

The worktree was clean on arrival, but partway through this act a running Godot
**editor** (open on `res://main.tscn`) re-saved the scene on top of my edits
while I was rebuilding `libtiles.dylib` underneath it. Because the GDExtension
was momentarily unresolvable, it downgraded the scene root:

```diff
-[node name="LevelEditor" type="LevelEditor" ...]
+[node name="LevelEditor" type="Control" ...]
+layout_mode = 3
```

which would have broken the whole application, and it dropped the two nodes this
act adds. Carson closed the editor, the root type was restored by hand, and the
nodes were re-added. **Do not run `scons` against this project while the Godot
editor has `main.tscn` open.** The final `git diff` of `main.tscn` is exactly the
two added nodes and nothing else.

## files

Modified, and the complete diff:

```text
src/game/LevelEditor.h                            .cpp
src/game/testing/LevelEditorIntegrationRunner.h   .cpp
godot-project/main.tscn
```

`src/game/PrototilePreview.*`, every resource header and source, both compilers,
`LevelPersistence`, `register_types.cpp`, and every fixture are untouched.

## the document proof state

`Document` gained exactly two fields:

```cpp
std::optional<Region> region;
std::optional<ArrangementRegionError> region_error;
```

and the editor gained three observers:

```cpp
const Region *region() const;                       // nullptr unless derived
const ArrangementRegionError *region_error() const; // nullptr unless refused
bool can_export() const;
```

`can_export()` is spelled as the act spells it — blueprint phase, nonempty
records, an exact region, and an available palette and catalog — and every
export operation calls it for itself. The button's `disabled` state is written
from the same call in `refresh_controls()` and is never consulted as a fact.

The document still owns no `LevelResource`, compiled product, resource or
filesystem path, dirty state, last export result, loaded level, runtime `State`,
or player progress.

## publication is one boundary, and it publishes three things

`publish(candidate)` compiles the complete candidate, and only on success
derives the region and assigns all four values together:

```text
candidate records
   → compile_blueprint      failure → nothing is assigned, document survives
   → region_from_arrangement
   → records + arrangement + exactly one of (region | typed error)
```

Two consequences the act asks for, and where they live:

- **An act-2 compilation failure still rejects the edit.** That branch is
  untouched: it reports, and assigns nothing.
- **An alpha failure does not.** A valid blueprint whose coverage is empty,
  disconnected, or pinched is published *with* its typed reason, so the author
  can place the tile that repairs it. The runner proves the repair path on the
  pinch fixture.

`build_palette()` now routes its empty blueprint through the same boundary
instead of assigning `records.clear()` and `Arrangement()` by hand, so a locked
palette begins with `empty_arrangement` rather than with an absent proof. Its
failure branch is unreachable — `compile_blueprint` accepts an empty sequence by
act-2 spec — and it exists anyway: it un-locks the palette, so "a locked palette
owns exactly one coverage proof" is true by construction rather than by
argument.

Region derivation runs on every publication. At authored scale that is free:
the whole 308-check run, including a sixty-five placement strip built one
placement at a time, takes about a second.

## topology status

`proof_summary()` is the one place the proof becomes text:

```text
region present                → "ready to export"
empty_arrangement             → "add at least one tile before export"
nonmanifold_boundary_vertex   → "coverage boundary is nonmanifold"
disconnected_coverage         → "coverage is disconnected"
internal_invariant_failure    → "the coverage geometry failed an internal check"
```

It is appended to the existing `placed:` / `removed:` / `blueprint cleared`
status lines. No coordinate, incidence count, component point, or enumerator
name reaches the author; the exact typed error stays available through
`region_error()` and is what the runner asserts against. Exportability is read
from the proof state, never parsed from this text.

`internal_invariant_failure` additionally pushes one engine error carrying a
typed description of which of alpha's nine invariants did not hold
(`describe_invariant`). The three expected failures log nothing. Per alpha's
notes every internal kind is structurally unreachable through a public
`Arrangement`, so **that branch is written and unexercised**; no construction
seam was added to fake one, and the act forbids that anyway.

## no region rendering

`_draw()`, `draw_axes()`, `draw_arrangement()`, `draw_ghost()`, and the
projection are byte-identical to act 2. Nothing added a fill, an outer or hole
boundary, a triangulation draw, target shading, an exportability colour, or a
preview control. The derived region is read by `can_export()`, by
`proof_summary()`, and by the runner — and by nothing else.

## the export controls

`main.tscn` gained exactly two nodes:

```text
Toolbar/Margin/Actions/ExportButton : Button      text "export .tres"
ExportDialog                        : FileDialog
```

The button sits between `clear blueprint` and `help`, and ships `disabled` so a
freshly loaded scene never shows an enabled export before `_ready` runs.

**Every load-bearing dialog property is set in `bind_scene()`, not in the
scene.** The scene carries only presentation (title, size, ok text). This is an
interpretive choice: the act lists the configuration as behaviour, and one
authority in code means a scene re-save — exactly the accident described above —
cannot silently widen what an export may write. The code sets:

```cpp
ACCESS_FILESYSTEM
FILE_MODE_SAVE_FILE
one filter: "*.tres", described "tiles level"
CUSTOMIZATION_OVERWRITE_WARNING enabled
use_native_dialog = true
current_file = "level.tres"
```

`CUSTOMIZATION_OVERWRITE_WARNING` is Godot 4.7's spelling of the overwrite
warning; there is no separate boolean property. Native-dialog availability stays
the platform's business — asking for one is what this act owes.

`on_export_pressed()` rechecks `can_export()` first and returns without popping
anything up when the document is ineligible, so a programmatic invocation of the
action is as safe as a disabled button.

## the path rule

One file-local rule, `normalize_export_path`, is the only thing that decides a
destination:

```text
empty            → rejected
no extension     → "<path>.tres"
".tres" any case → preserved exactly
anything else    → rejected
```

`export_document` applies it before anything is encoded, so a wrong extension
never reaches beta's own extension check, `make_level_resource`, or the saver.
No wrong extension is replaced or appended to, `.res` is not an alias, and no
path is inferred from a resource, the project, the working directory, or a
previous export. `String::get_extension()` already refuses to read a dot from a
directory component, so `/tmp/dir.d/level` is extensionless and receives `.tres`.

## the export operation

```cpp
bool export_document(const godot::String &p_path);
```

Precedence, exactly as the act orders it:

```text
can_export()  →  normalize path  →  make_level_resource  →  export_level_resource
```

The toolbar callback is two lines and calls this; the headless runner calls this;
there is no second resource builder, compiler gate, or `ResourceSaver` call in UI
code. The fresh `Ref<LevelResource>` is a local of this function and is not
retained before or after. Beta recompiles the complete candidate itself before
saving and gamma does not skip that gate, even though it already holds an exact
palette, arrangement, and region.

Every typed failure is described at this one presentation boundary:
`LevelResourceEncodingError`, `ExportLevelError`, and — nested inside it —
`LevelResourceError`, `BlueprintResourceError`, `OrientationError`, and
`ArrangementRegionError`. Nothing is flattened before it gets here.

Success changes nothing: no clear, rename, path, dirty flag, or remembered
destination. The status line reports the path that was written and the document
is exactly as editable as it was a moment earlier.

## what the headless runner proves

157 new checks, driving the real `res://main.tscn` through the editor's public
operations. Placements at particular exact translations are made by `place_at`,
which finds the offered proposal with that exact translation, moves the pointer
to its projected handle, asserts that proposal became active, and accepts it — so
every fixture below is built the way a person builds it, and no coordinate is
injected past the proposal path.

Coverage proof:

- a locked palette installs an empty arrangement plus exactly
  `empty_arrangement`;
- one placement immediately installs the region `(0,0) (2,0) (2,2) (0,2)`;
- **partial-edge contact** — a unit square against half of the o tetromino's
  right edge — installs the exact six-vertex canonical boundary and the exact
  area;
- **a hole** — a ring of eight unit squares — installs outer `(0,0) (3,0) (3,3)
  (0,3)` with exactly one hole `(1,1) (2,1) (2,2) (1,2)`, area 8 units, equal to
  the independently summed footprint area;
- **hex-12** coverage (one hexagon plus three triangles, grown through ordinary
  proposals) installs a region whose exact area equals the summed footprints and
  whose complete boundary equals the region derived from the same records
  rebuilt **in reverse order** through `Placement::make` / `try_insert` — the
  canonicality claim, not just a shape claim;
- **bridge removal** publishes `disconnected_coverage` with both component
  points in lexicographic order and does not roll the removal back;
- **a pinch** — a 3 x 3 block less its centre and one corner, every cell
  edge-connected — publishes `nonmanifold_boundary_vertex` at exactly `(1,2)`
  with indegree 2 and outdegree 2, and filling the corner derives a region
  again;
- clearing returns to `empty_arrangement`;
- every publication carries exactly one of region and error, with one
  arrangement entry per record;
- a rejected edit preserves records, arrangement, proof, selection, proposals,
  camera, colours, and controls;
- the three concise status lines are what the author actually sees.

Export:

- `ExportButton` exists, is labelled `export .tres`, and is disabled in domain
  phase, in palette phase, with an empty blueprint, and on disconnected
  coverage, and enabled exactly when `can_export()`;
- `ExportDialog` has filesystem access, save mode, exactly one `*.tres` filter,
  the overwrite warning, the native preference, and `level.tres`;
- pressing export while ineligible opens nothing and changes nothing;
- an empty path, `.res`, and `.json` are refused, write no file, and preserve
  the complete document;
- a path with no extension is written as `<path>.tres` and nothing is written at
  the bare name;
- an uppercase `.TRES` destination is preserved exactly as given;
- a second export writes its own destination and leaves the first artifact in
  place, and an empty path is *still* refused afterwards — nothing was
  remembered;
- a successful export preserves the complete document, snapshot-compared field
  by field;
- the exported **lattice** and **hex-12** artifacts load through beta's
  `load_level_resource` and compare exactly: domain, every record's id,
  orientation and raw translation, region boundary, hole count, exact area,
  palette ids and supplies, and authored colours;
- a **sixty-five placement** strip is a perfectly valid region that
  `make_level_resource` refuses, and that refusal writes nothing and preserves
  the document — the reachable "fails before the saver" case;
- no open/load/save/save-as method, action, dialog, or path label exists, and no
  region-drawing method or overlay node exists.

The runner owns one temporary directory, `user://tiles_level_editor_export`
**globalized to an absolute host path**, so the export path actually exercised
is an ordinary filesystem path rather than an engine path. Nine exact filenames
are removed before the run and again after it; the directory is removed only
when it is empty. Nothing recursive and nothing derived from unchecked input is
ever removed. The final check asserts every named file is gone.

### honest limits of the runner

Three, stated rather than papered over.

1. **"Pressing export while invalid does not open the dialog" is proven by the
   dialog never becoming visible**, and this runner never pops it up at all. The
   real proof is structural: `on_export_pressed` returns before `popup_*` when
   `can_export()` is false. Driving a real native save dialog is a manual
   concern.
2. **"No region rendering" is proven by absence of methods and nodes**, plus the
   fact that `_draw` is unchanged in this diff. A headless runner quits inside
   `_ready` and never draws a frame, so nothing here observes pixels — the same
   honest limit act 2 recorded.
3. **"No `LevelResource`, path, dirty state, or export result is retained"** is
   proven behaviourally (no destination is remembered between exports; nothing
   about the document changes) and by the absence of any such accessor. The
   `Document` struct is the structural proof and it is three lines long.

`ExportLevelErrorCode::saver_failed` is still not forced, for the reason beta
gave: provoking a real `ResourceSaver` failure means an unwritable path or a
full filesystem, and fabricating one would test the fixture. Gamma's handling of
it is one branch that reports the exact engine error.

## interpretive decisions, recorded

Five, none of them contradictions in the act.

1. **The dialog's load-bearing configuration lives in code**, not in
   `main.tscn`. Reason above; the runner asserts the live node, so the assertion
   is against what actually ships either way.
2. **The temporary export destination is several exact paths in one owned
   directory**, not literally one path. Proving "every export names its own
   destination" and "an extensionless path receives `.tres`" needs more than one
   filename. Each is exact, named, and removed by name.
3. **`can_export()` is spelled with `phase()`** rather than with the equivalent
   `palette.has_value()`, because the act states it that way and the two cannot
   disagree.
4. **Status text composes** — `"placed: 4 tiles | ready to export"` — rather than
   replacing the placement count. The act says later selection and pointer status
   may replace the visible text entirely; this keeps both facts visible until
   then.
5. **`build_palette()` rolls back the palette if publication ever fails.** Dead
   code today, and cheaper than an argument about an invariant.

Two pre-existing warts left alone, deliberately: `HEX_ROW_SQUARE` and
`HEX_ROW_DODECAGON` in the runner are declared and unused (act 2), which
`-Wunused-const-variable` reports in a standalone compile and the scons build
does not. They are act-2 code documenting the hex-12 row order, and this act has
no business editing them.

## boundaries respected

No region, boundary, hole, or target rendering; no level-playing surface; no
open, import, or resume; no stored resource, path, dirty flag, export history,
or last destination; no save or save-as; no change to beta's schema, compiler,
encoder, persistence, or consumer loader; no change to alpha or act-2 exact
geometry; no change to palette locking or domain discard behaviour; no undo or
redo; no player progress or runtime state; no second resource format or geometry
domain; no campaign or level selection; and none of delta's packaging.

## manual acceptance: not performed

**No visual or manual acceptance is claimed.** Everything above is headless or
native evidence, and none of it establishes that the export button reads well,
that the native save dialog behaves on macOS, or that a `.tres` written to the
Desktop opens where a friend expects it. That pass is carson's.

The act's bounded functional pass, in order:

```text
./Godot.app/Contents/MacOS/Godot --path godot-project
```

1. `lattice`, include `tetromino o (square 2)` and `square 1`, build the
   palette, place the tetromino at origin and then a unit square against half of
   its right edge — partial-edge contact;
2. `export .tres`, choose somewhere outside the project (Desktop, say), confirm
   one self-contained `.tres` appears;
3. `hex12`, include at least two tile types, build a patch, export it;
4. build a ring of unit squares around one empty cell — it should stay
   exportable, and nothing about the hole should be drawn;
5. remove a bridging tile so the coverage splits — the status should read
   `coverage is disconnected` and `export .tres` should go grey;
6. confirm the dialog offers only `.tres` and warns before overwriting an
   existing file;
7. confirm that after a successful export the blueprint is still fully editable
   and a second export asks for its own destination.

## for the delta implementor

Three things.

1. **The export path is `LevelEditor::export_document`**, and it is the only
   place the editor writes anything. Packaging must keep `ACCESS_FILESYSTEM`
   working in an exported binary — that is the one thing about this act a
   sandboxed or notarized build can break.
2. **The dialog asks for a native dialog and tolerates not getting one.** On a
   platform where Godot falls back to its own dialog, everything above still
   holds; only the look changes.
3. **Nothing in the editor loads a level.** If delta wants a smoke test on the
   packaged binary, the artifact it produces has to be read back by something
   else — `load_level_resource` is that something, and the resource-integration
   scene already drives it.
