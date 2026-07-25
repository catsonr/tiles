# project hail mary act 2-1 gamma: export-only editor integration

## immutable implementation contract

this document is immutable once handed to an implementor. do not revise it to
match implementation choices. stop and ask carson if a genuine contradiction
or behavior-changing ambiguity remains.

record results in:

```text
docs/project-hail-mary/PROJECT-HAIL-MARY-ACT-2-1-GAMMA-IMPLEMENTATION-NOTES.md
```

do not mention this plan or project act in code comments or runtime
documentation. do not commit.

## prerequisite and context seed

project-hail-mary acts 0, 1, 2, alpha, and beta must be implemented and passing.
gamma consumes only:

- act 2's exact editor document, records, arrangement, palette, and colors;
- alpha's `region_from_arrangement`;
- beta's `make_level_resource` and `export_level_resource`; and
- beta's versioned `LevelResource` graph and complete compiler.

before editing:

1. inspect and preserve concurrent work;
2. read the project-hail-mary roadmap, immutable acts 0, 1, 2, alpha, and beta,
   and every implementation note;
3. read the complete current `LevelEditor`, main scene, editor-integration
   runner, resource encoder, compiler, export helper, and registration;
4. inspect beta's exact public contracts and do not reach into its integration
   helpers or resource internals; and
5. establish every current native and headless baseline.

if beta's public encoding or export contract is missing or materially
different, stop. do not reproduce resource construction or persistence inside
the editor.

## goal

extend the working act-2 authoring surface with exactly one final operation:

```text
export the current exact document as one valid .tres level
```

gamma retains alpha's exact derived `Region` only as nonvisual authoring proof:

- it determines whether the document is exportable;
- it preserves typed topology failure for concise status and tests; and
- it proves editor state stays synchronized after every blueprint mutation.

gamma does not render the region. placed tiles and exact placement ghosts remain
the complete canvas presentation.

the flow is:

```text
exact in-memory editor document
        │
        ▼
fresh beta LevelResource encoding
        │
        ▼
beta complete compilation gate
        │
        ▼
user-selected external .tres path
        │
        ▼
ResourceSaver
```

the editor exports artifacts. it does not save or reopen authoring documents.

## document proof state

modify:

```text
src/game/LevelEditor.h
src/game/LevelEditor.cpp
```

extend blueprint-phase `Document` with:

```text
optional exact Region
optional exact ArrangementRegionError
```

exactly one is populated whenever the document owns a locked palette:

```text
alpha success  → Region present ∧ error absent
alpha failure  → Region absent  ∧ error present
```

palette and domain phases own neither.

the document still owns no:

- `LevelResource`;
- compiled resource product;
- resource or filesystem path;
- dirty state;
- last export result;
- loaded level;
- runtime `State`; or
- player progress.

publish const observation equivalent to:

```cpp
const Region *region() const;
const ArrangementRegionError *region_error() const;
bool can_export() const;
```

the pointers are read-only views valid only while the document remains
unchanged, matching the existing observation style.

`can_export()` is exactly:

```text
phase == build_blueprint
∧ blueprint is nonempty
∧ exact Region is present
∧ palette and catalog are available
```

button enablement is only presentation. every export operation rechecks the
same conditions.

## transactional blueprint publication

replace the existing publication boundary with:

```text
candidate records
        │
        ▼
compile_blueprint
        │
        ├── failure: publish nothing
        ▼
candidate Arrangement
        │
        ▼
region_from_arrangement
        │
        ├── success: candidate Region
        └── failure: candidate typed region error
        │
        ▼
publish records + arrangement + exactly one region result
```

act-2 blueprint compilation failure still rejects the edit and preserves the
entire current document.

alpha failure does not reject a valid blueprint edit. publish the records,
arrangement, and typed failure together so empty, disconnected, point-connected,
pinched, or otherwise non-region coverage remains editable.

no observer may see:

- new records with the old arrangement;
- a new arrangement with the old region result;
- both region and error;
- neither region nor error in blueprint phase; or
- a partial candidate after blueprint failure.

### phase transitions

successful palette build enters blueprint phase with:

```text
empty records
empty Arrangement
no Region
ArrangementRegionError::empty_arrangement
```

addition, removal, and clear all route through the same publication boundary.
clearing an already-empty blueprint may still re-establish the same exact proof
state but adds no separate dirty or history meaning.

choosing a domain or returning to domain choice discards the region proof
together with the rest of the document.

## topology status

after an accepted blueprint edit, expose concise author-facing status:

```text
empty_arrangement              → add at least one tile before export
nonmanifold_boundary_vertex    → coverage boundary is nonmanifold
disconnected_coverage          → coverage is disconnected
```

the exact typed error remains available to tests and future diagnostics. status
text need not print raw coordinates, incidence counts, component points, or
internal enum names.

an alpha `internal_invariant_failure`:

- makes the document unexportable;
- reports a concise internal geometry failure to the author; and
- emits one developer error containing enough typed-detail description to
  identify the internal failure kind.

ordinary expected topology failures emit no engine error or warning.

later selection, proposal, supply, and pointer status may replace the visible
text. exportability is always derived from document proof state, never parsed
from status text.

## no region rendering

do not add:

- region fill;
- outer-boundary overlay;
- hole-boundary overlay;
- region triangulation drawing;
- target shading;
- exportability color;
- region preview control; or
- any other visual representation of the derived region.

do not modify the exact model-to-screen projection for gamma. the existing
arrangement and active placement ghost remain the only model geometry drawn by
`LevelEditor`.

region presentation belongs to a future level-playing surface, not this
authoring/export act.

## export controls

modify:

```text
godot-project/main.tscn
```

add exactly:

```text
ExportButton : Button
ExportDialog : FileDialog
```

the button text is:

```text
export .tres
```

configure the dialog:

```text
access                    = ACCESS_FILESYSTEM
file_mode                 = FILE_MODE_SAVE_FILE
filter                    = *.tres
overwrite warning         = enabled
use native dialog         = enabled where supported
default current filename  = level.tres
```

the filter description may be concise, but it must expose only `.tres`.

the button is disabled exactly when `can_export()` is false. pressing a disabled
or programmatically invoked export action still rechecks eligibility and must
not open the dialog for an invalid document.

the filesystem access mode is load-bearing. `ACCESS_RESOURCES` and
`ACCESS_USERDATA` do not satisfy friend-facing export.

native-dialog availability is platform behavior. setting the preference is
required; falling back to Godot's filesystem dialog when the platform does not
support a native one is acceptable.

## path normalization

publish one path normalization rule used by both callback and public operation:

- a path with no extension receives `.tres`;
- a path whose extension is `.tres`, case-insensitively, is preserved;
- a path with any other extension is rejected; and
- an empty path is rejected.

do not replace an existing wrong extension, append `.tres` after a wrong
extension, accept `.res`, or infer a path from the resource, project, process
working directory, or previous export.

the dialog begins with `level.tres` every time no platform-native remembered
filename supersedes that presentation hint.

## export operation

publish:

```cpp
bool export_document(const godot::String &p_path);
```

the toolbar callback and headless runner call this same operation. no second
resource builder, compiler gate, or saver call exists in UI code.

precedence:

1. require blueprint phase, nonempty records, exact region, palette, and
   catalog;
2. normalize or reject the selected path;
3. call beta's `make_level_resource` with exact domain, palette, colors, and
   blueprint records;
4. call beta's `export_level_resource` with the fresh candidate and normalized
   path; and
5. report success or the exact failing stage concisely.

beta independently recompiles the complete resource before saving. gamma does
not bypass that gate merely because it already holds exact palette,
arrangement, and region values.

gamma retains no `Ref<LevelResource>` before or after the call. the fresh graph
exists only for this export attempt.

success and failure preserve:

- domain;
- palette and colors;
- records and arrangement;
- region or typed region error;
- selection and active proposal;
- camera and pointer state;
- generated controls; and
- every other editor document value.

success does not clear, mark, rename, path, or otherwise transform the document.
the author may continue editing and every later export asks for another path.

compilation, encoding, and wrong-extension failures never reach
`ResourceSaver`. saver failure behavior remains beta's honest persistence
contract.

## editor integration

rewrite or extend `LevelEditorIntegrationRunner` through the editor's public
operations and real `main.tscn`.

prove:

- palette build installs empty arrangement plus exact empty-region error;
- one valid placement immediately installs a `Region`;
- valid lattice partial-edge coverage installs the exact canonical region;
- valid hex-12 coverage installs the exact canonical region;
- a connected hole arrangement retains the exact hole in the model without
  adding any region draw path;
- removal of a bridge publishes disconnected error without rolling back;
- every alpha failure reachable through public editor operations publishes its
  exact expected error;
- clear returns to the empty-region error;
- records, arrangement, and region result publish atomically;
- `can_export()` follows exact proof state;
- `ExportButton` exists with exact enablement and label;
- `ExportDialog` has filesystem access, save mode, `.tres`-only filter,
  overwrite warning, native preference, and default filename;
- pressing export while invalid does not open the dialog;
- empty and wrong-extension paths preserve the document and write nothing;
- a path without an extension receives `.tres`;
- an encoding or complete compilation failure preserves the document and writes
  nothing before the saver;
- successful export preserves the complete document;
- the exported lattice and hex-12 resources load and compare exactly through
  beta's consumer path;
- no `LevelResource`, path, dirty state, or export result is retained;
- no open/load/save/save-as action, callback, dialog, or path label exists; and
- no region fill, boundary, hole, target, or other region-rendering operation
  exists in the scene or editor draw path.

headless successful export uses one exact temporary path owned by the runner.
cleanup removes only named files by exact path and removes a temporary directory
only when empty. no recursive deletion or path derived from unchecked input is
allowed.

## manual acceptance

perform a bounded functional pass:

1. construct a lattice blueprint containing partial-edge contact;
2. invoke export and select a filesystem destination outside `res://`;
3. confirm exactly one self-contained `.tres` is produced;
4. construct and export a hex-12 blueprint using at least two prototile types;
5. construct connected coverage with a hole and confirm it exports without any
   added region rendering;
6. create disconnected coverage and confirm export is unavailable;
7. confirm the file dialog offers only `.tres` and warns before overwrite; and
8. confirm successful export leaves the current blueprint fully editable.

record exact observed behavior and limitations. packaged-binary, signing,
notarization, and cross-platform acceptance belong to delta.

## verification

run:

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

native and resource-integration totals are unchanged regression baselines.
record the editor-integration before/after totals separately because gamma
extends its subject.

run `git diff --check`, inspect the complete diff, verify temporary artifacts
are absent, and record the exact outcomes and bounded manual result in the
implementation notes.

## boundaries

gamma does not:

- render the derived region, outer boundary, or holes;
- implement a level-playing surface;
- load, reopen, import, or resume an exported level;
- store a resource, resource path, dirty flag, export history, or last
  destination;
- add save or save-as;
- modify beta's schema, compiler, encoder, persistence, or consumer loader;
- change alpha or act-2 exact geometry;
- change palette locking or domain discard behavior;
- add undo or redo;
- add player progress or runtime state;
- add another resource format or geometry domain;
- implement campaign or level selection; or
- implement delta's binary packaging.

## acceptance summary

gamma is complete exactly when:

```text
every valid blueprint edit
  → records + Arrangement + exact Region proof

every invalid-region blueprint edit
  → records + Arrangement + typed editable failure

every successful export
  → fresh version-1 self-contained .tres
  → beta complete compilation gate
  → user-selected filesystem path

editor loading = absent
region rendering = absent
document persistence = absent
```

and all native and headless baselines pass together.
