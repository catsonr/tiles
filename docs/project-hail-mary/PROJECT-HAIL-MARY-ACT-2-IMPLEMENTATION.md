# project hail mary act 2: tiling-first domain editor

## immutable implementation contract

this document is immutable once handed to an implementor. do not revise it to
match implementation choices. if a real contradiction or behavior-changing
ambiguity remains, stop and ask carson.

record results in:

```text
docs/project-hail-mary/PROJECT-HAIL-MARY-ACT-2-IMPLEMENTATION-NOTES.md
```

do not mention this plan or project act in code comments or runtime
documentation. do not commit.

## prerequisite and context seed

act 1 must already be implemented and passing. act 2 consumes only its public
contracts:

- `content::GeometryDomain`;
- domain-filtered canonical catalog views;
- domain-aware palette-resource compilation;
- checked precompiled `PaletteEntry`; and
- canonical lattice and hex-12 identities and orientation sets.

before editing:

1. inspect `git status --short --branch` and preserve concurrent work;
2. do not stop merely because the worktree is dirty;
3. stop only for an irreconcilable overlap in a file this act must edit;
4. read the phm roadmap, immutable acts 0, 1, and their implementation notes;
5. read every file beneath `src/core/`, `src/content/`, and `src/engine/`;
6. read the complete current `Editor`, `LevelEditor`, preview, resource
   compiler, resource types, registration, main scene, and both headless game
   runners;
7. read the native palette, command, arrangement, placement, session, and
   hex-12 tests; and
8. establish the current post-act-1 native, gdextension, resource-integration,
   editor-integration, and ordinary headless baselines.

if act 1 is absent, failing, or materially deviates from its immutable public
contract, stop rather than reproducing its work inside this act.

## goal

replace the inverse polygon-region editor with one reliable in-memory authoring
surface which:

```text
chooses lattice | hex12
        │
        ▼
locks one exact palette
        │
        ▼
constructs one exact blueprint arrangement
```

act 2 does not derive a region and does not save a complete level. its product
is the stable exact blueprint contract act 2-1 will persist.

visual sophistication is not a goal. clear state, deterministic operations,
transactional failure, and complete tests are.

## why existing placement is sufficient

do not add hex-specific placement, alignment, overlap, joining, or rendering.

act 0 already proved that hex-12 `OrientedPrototile` values pass through:

- `Placement::make`;
- exact full-edge alignment;
- exact vertex alignment;
- `Arrangement` insertion and joining;
- all twelve module directions;
- all ten supported uniform vertex stars; and
- extended multi-cycle patch growth.

after source compilation, placement is geometry-domain agnostic. act 2 selects
different source products but uses the same exact consumers.

## engine blueprint compiler

add:

```text
src/engine/Blueprint.h
src/engine/Blueprint.cpp
tests/test_blueprint.cpp
```

with a domain-blind record:

```cpp
namespace tiles::engine {

struct BlueprintPlacement final {
    PrototileId prototile_id;
    Orientation orientation;
    Point translation;
};

} // namespace tiles::engine
```

`orientation` is the representative orientation of one distinct compiled
palette variant, not an arbitrary equivalent label and not a transient vector
index.

publish a pure compiler equivalent to:

```cpp
enum class BlueprintCompilationErrorCode {
    prototile_not_in_palette,
    orientation_not_in_palette,
    supply_exhausted,
    placement_construction_failed,
    arrangement_insertion_failed,
};

struct BlueprintCompilationError final {
    BlueprintCompilationErrorCode code;
    std::size_t placement;
    PrototileId prototile_id;
    std::optional<Orientation> orientation;
    std::optional<PlacementError> placement_error;
    std::optional<ArrangementError> arrangement_error;
};

Result<Arrangement, BlueprintCompilationError> compile_blueprint(
    const Palette &p_palette,
    const std::vector<BlueprintPlacement> &p_placements);
```

an empty sequence succeeds with an empty `Arrangement`. emptiness is valid
while authoring; act 2-1 rejects it when deriving a region.

compile records in stored order. for each record:

1. find its exact `PrototileId` in authored palette order;
2. find an oriented value whose representative `orientation()` equals the
   record orientation;
3. count prior successfully compiled records of that id and reject when its
   finite supply is exhausted;
4. call `Placement::make` with the record's exact translation; and
5. call `Arrangement::try_insert`.

failure stops at the first record and returns no partial arrangement.

payload rules:

- every error carries record index and id;
- orientation and every later failure carries the record orientation;
- only placement construction carries `placement_error`;
- only arrangement insertion carries `arrangement_error`; and
- every other optional is empty.

do not use palette indices as persistent identity, reinterpret an equivalent
orientation label as a representative, reorder records, clamp supply, or
repair overlaps.

this separate engine compiler is authoring support, not a second runtime
`State`. it owns no domain, resource, color, selection, history, or region.

## editor document state

replace the current document model with exactly three phases:

```cpp
enum class EditorPhase {
    choose_domain,
    choose_palette,
    build_blueprint,
};
```

the active document owns, as applicable:

```text
GeometryDomain
Ref<PaletteResource>
optional exact Palette
vector<BlueprintPlacement>
exact Arrangement
```

the record vector is reconstructable authority. the `Arrangement` is the
complete proof produced from those records. they are published together and
never drift.

no runtime `Level`, `Region`, `State`, `Session`, or fake debug boundary belongs
to an act-2 editor document.

## phase one: choose domain

the application boots directly into `LevelEditor` and visibly offers:

```text
lattice
hex12
```

choosing either installs one fresh palette-phase document.

returning to domain choice or choosing another domain discards the entire
document immediately. there is no migration, conversion, retained palette,
retained arrangement, dirty confirmation, or cross-domain recovery.

invalid domain values cannot be installed through the public editor methods
used by the ui and integration runner.

## phase two: choose and lock palette

show exactly the selected domain's catalog view:

```text
lattice: existing 34 entries
hex12:   triangle 1, square 1, hexagon 1, dodecagon 1
```

one row exposes:

- inclusion;
- canonical reference preview;
- display name;
- unlimited or positive finite supply;
- finite amount when applicable; and
- opaque color.

reuse existing palette row controls when useful. presentation layout and
styling may be simplified.

a `build` action:

1. constructs a candidate `PaletteResource`;
2. invokes act 1's domain-aware palette compiler;
3. rejects an empty or invalid palette without leaving palette phase; and
4. on success stores the exact `Palette`, installs empty records and the empty
   arrangement, and enters blueprint phase.

domain and palette are locked in blueprint phase. there are no live palette
edits. returning to either earlier phase discards the document.

## phase three: build blueprint

### selection

selection is one palette entry and one distinct compiled orientation.

- changing entry resets orientation to index zero;
- orientation cycles through `PaletteEntry::orientations()`, never through an
  equivalent-label vector;
- finite remaining supply is derived from blueprint records;
- an exhausted entry remains visible but cannot add another record; and
- selection carries no invalid sentinel and is optional only when the palette
  is unavailable.

### first placement

when the blueprint is empty, the selected variant has exactly one offered
candidate:

```cpp
BlueprintPlacement {
    selected_id,
    selected_oriented.orientation(),
    Point { Coordinate::from_raw(0), Coordinate::from_raw(0) },
}
```

the mouse does not choose an authoritative translation. accepting the first
ghost compiles and publishes that candidate at exact origin.

### later proposals

for one selected oriented candidate, enumerate:

1. every stored arrangement entry in storage order;
2. every anchor edge × candidate edge in index order; then
3. every anchor vertex × candidate vertex in index order.

use only:

- `Arrangement::preview_join_full_edges`;
- `Arrangement::preview_join_vertices`; and
- the returned exact `Placement`.

ordinary rejected feature pairs are not errors and are not logged.

convert each successful preview to a `BlueprintPlacement` using the selected
id, selected representative orientation, and returned exact translation.

collapse proposals with equal id, representative orientation, and translation,
retaining first discovery order. because each record resolves one canonical
oriented polygon, those fields determine the same exact footprint.

retain a proposal only when its footprint shares a collinear boundary segment
of positive length with at least one stored footprint. decide this with exact
core segment classification. point-only contact remains legal core geometry
but is not offered by this editor.

proposal rebuilding is pure and deterministic. no preview reserves an id,
consumes supply, or changes the blueprint.

### proposal presentation

project exact proposal footprints one-way into Godot coordinates. show only one
active translucent ghost.

the active proposal is the one whose presentation-only handle is nearest the
last local mouse position. ties retain deterministic proposal order. the
handle and mouse position select an already-exact record; neither constructs or
modifies geometry.

left click accepts the active proposal. when no proposal exists it changes
nothing and reports a concise status.

### transactional publication

every accepted addition:

1. copies the record vector;
2. appends the proposed exact record;
3. calls `compile_blueprint` on the complete candidate;
4. preserves the current records and arrangement on failure; and
5. publishes the candidate records and returned arrangement together on
   success.

a proposal immediately following an unmodified successful preview is expected
to compile. nevertheless, inspect the result and report an unexpected typed
failure rather than asserting or half-publishing.

### removal

right click or an equivalently direct pointer action selects the topmost
rendered stored placement containing the pointer and removes that record.
lossy hit testing may choose a record index; it never constructs geometry.

removal:

1. copies records without the selected index;
2. recompiles the complete candidate;
3. preserves the current blueprint on unexpected failure; and
4. publishes records and arrangement together on success.

recompilation may assign new transient `PlacementId`s. no editor selection,
resource identity, or later persistence depends on those ids.

removing a bridge may disconnect coverage. that remains a valid act-2
blueprint; act 2-1 will report that it cannot form one region.

provide one explicit `clear blueprint` action which publishes empty records and
an empty arrangement.

### no undo

do not add authoring undo or redo. removal and clear are the complete correction
surface for this act.

## drawing

render:

- domain and palette selection controls appropriate to the phase;
- palette reference previews and colors;
- current selected entry/orientation;
- authoritative arrangement footprints in their palette colors;
- finite remaining supply or unlimited supply;
- the active proposal ghost; and
- concise status/instructions.

all polygons come from exact palette or arrangement values. all projection is
lossy and one-way. do not add textures, animation, decoration, camera
infrastructure beyond reusable existing pan/zoom, responsive layout polish, or
production theming.

## replacement and deletion

`LevelEditor` becomes the one application editor.

reuse correct code from the old `Editor` and `LevelEditor`, especially:

- exact projection;
- arrangement drawing;
- proposal derivation and deduplication;
- positive-length contact filtering;
- palette rows;
- color handling; and
- headless-accessible public operations.

delete obsolete inverse region-loop behavior, region drawing controls, save,
open, play-request, and old document logic from `LevelEditor`.

after useful proposal behavior is absorbed, delete the standalone
`src/game/Editor.*` construction fixture and its registration. do not retain a
second unreachable editor implementation.

keep current resource classes and `ResourceIntegrationRunner` passing for this
transitional act. act 2-1 replaces the resource graph.

update `main.tscn` only as required for the simplified `LevelEditor` controls.
the application still boots directly into that editor.

## native tests

`tests/test_blueprint.cpp` must prove:

- empty records compile to an empty arrangement;
- representative lattice and hex-12 blueprints compile;
- stored order produces deterministic entries and ids;
- exact ids, orientations, translations, and footprints survive;
- first unknown id fails at its record;
- a nonrepresentative equivalent label fails as orientation absent;
- an unsupported representative fails;
- finite supply fails on the first excess record;
- placement overflow preserves the exact `PlacementError`;
- interior overlap preserves the exact `ArrangementError` and conflicting id;
- error precedence when one record has several possible defects;
- a failure returns no arrangement;
- removing a record and recompiling produces exactly the remaining coverage;
- lattice partial-edge arrangements remain admitted; and
- a multi-placement hex-12 uniform patch compiles without any domain-specific
  placement path.

do not fabricate unchecked prototiles, placements, or arrangements.

## headless editor integration

rewrite `LevelEditorIntegrationRunner` around the new public editor operations.
it must exercise the same methods ui callbacks use.

prove:

- boot begins in domain phase;
- lattice selection exposes exactly 34 ordered rows;
- hex-12 selection exposes exactly four ordered rows;
- returning to domain choice discards all state;
- empty palette cannot lock;
- finite and unlimited palette entries compile;
- palette locks on successful build and cannot mutate afterward;
- first lattice and hex-12 placements are exact origin placements;
- entry and distinct-orientation cycling wraps correctly;
- proposal generation is deterministic and contains no duplicate physical
  placement;
- every offered proposal shares positive-length exact boundary contact;
- point-only proposals are filtered;
- repeated lattice and hex-12 additions succeed;
- overlap and exhausted supply preserve the blueprint;
- removal recompiles and publishes exactly;
- clear returns to an empty blueprint;
- screen input only selects proposals or record indices;
- arrangement drawing runs without reconstructing model geometry; and
- no region, save, load, or play state is present.

the runner may use exact public observation methods. do not expose mutable
editor internals merely for testing.

## manual acceptance

perform only a bounded smoke pass:

1. choose lattice, select a small palette, place and remove several pieces;
2. restart, choose hex-12, select at least triangle and hexagon, and grow a
   visible patch;
3. confirm orientation cycling and proposal selection are usable; and
4. confirm restarting discards the document.

record honest observations. visual polish is not acceptance.

## verification

run the current post-act-1:

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

also compile new or modified non-Godot translation units standalone under the
repository's c++17, no-exceptions, and warning flags. run `git diff --check`,
inspect the complete diff, and record exact totals and outputs.

## boundaries

act 2 does not:

- derive or render a region;
- save or load a complete level;
- alter the resource schema;
- serialize domain or blueprint records;
- use runtime `State`;
- add a fake region;
- add hex-specific placement;
- add undo/redo;
- support live palette edits;
- preserve the old inverse editor;
- migrate old documents;
- implement a player;
- add another geometry domain; or
- add visual polish unrelated to legible operation.

## acceptance summary

act 2 is accepted only when:

```text
one editor
∧ explicit lattice | hex12 fresh-document split
∧ locked exact palette
∧ reconstructable BlueprintPlacement records
∧ transactional domain-blind blueprint compilation
∧ exact origin first placement
∧ exact feature-derived later proposals
∧ deterministic addition, removal, and clear
∧ authoritative arrangement rendering
∧ native and headless proof for both domains
```

no model coordinate comes from the pointer, no domain enters placement logic,
and no region is invented before act 2-1.
