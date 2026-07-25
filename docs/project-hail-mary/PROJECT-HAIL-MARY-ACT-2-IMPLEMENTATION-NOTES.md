# project hail mary act 2 — implementation notes

Notes from replacing the inverse region editor with one tiling-first authoring
surface: a domain-blind engine blueprint compiler, a three-phase `LevelEditor`,
and a rewritten headless runner which drives the real main scene through the
editor's own public operations.

## verification

| step | result |
| --- | --- |
| `scons tests` | builds clean |
| `./build/tests/tiles_tests` | **9801 checks, 0 failed across 407 cases** |
| standalone c++17 / `-fno-exceptions` / `-Wall -Wextra -Wpedantic` | no warnings |
| `scons` | GDExtension builds, no warnings or errors |
| `res://tests/resource_integration.tscn` | **270 checks passed** |
| `res://tests/level_editor_integration.tscn` | **151 checks passed** |
| ordinary headless load | `[tiles] level editor ready: 37 canonical identities, choose lattice or hex12`, no error lines |
| `git diff --check` | clean |

Baselines were re-established before editing:

```text
native                9707 checks / 0 failed / 390 cases  →  9801 / 0 / 407
resource integration  270 checks passed                   →  270 (unchanged)
level editor          218 checks passed                   →  151 (rewritten, see below)
ordinary run          34 catalog rows                      →  37 identities, domain prompt
```

`390 + 17 = 407` cases and `9707 + 94 = 9801` checks: every new native case is in
`tests/test_blueprint.cpp`. No existing test was removed, weakened, renamed, or
changed to accept a different result; nothing under `src/core/`, `src/content/`,
`src/game/resources/`, or `ResourceIntegrationRunner` was touched at all.

The level-editor integration count is **not** comparable across the act. Its 218
checks tested region loops, saving, opening, and play requests — surfaces this
act deletes. The 151 checks replacing them test the three phases, exact proposal
derivation, transactional publication, removal, clearing, pointer selection, and
the *absence* of the removed surfaces. `SConstruct` needed no edit: both globs
discovered the new files.

The worktree was clean on arrival. No commit was made.

## files

New:

```text
src/engine/Blueprint.h
src/engine/Blueprint.cpp
tests/test_blueprint.cpp
```

Rewritten:

```text
src/game/LevelEditor.h                        .cpp
src/game/testing/LevelEditorIntegrationRunner.h .cpp
godot-project/main.tscn
```

Deleted:

```text
src/game/Editor.h
src/game/Editor.cpp
```

Modified:

```text
src/register_types.cpp        (Editor include and registration removed)
```

`src/game/PrototilePreview.*`, `src/game/resources/*`, and
`src/game/testing/ResourceIntegrationRunner.*` are untouched. `LevelPersistence`
and `compile_level_resource` remain, still exercised by the resource runner;
`LevelEditor` no longer calls either. `engine::TetrominoState` also remains — its
only former consumer was the deleted `Editor`, but `tests/test_tetromino_state.cpp`
still covers it and the act does not authorise removing a passing test.

## the exact new public surface

```cpp
// engine/Blueprint.h
struct BlueprintPlacement final {
    PrototileId prototile_id;
    Orientation orientation;
    Point translation;
};

enum class BlueprintCompilationErrorCode {
    prototile_not_in_palette, orientation_not_in_palette, supply_exhausted,
    placement_construction_failed, arrangement_insertion_failed,
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
    const Palette &, const std::vector<BlueprintPlacement> &);
```

Exactly as specified, including the payload rules: every failure carries the
record index and id, orientation is populated by the orientation failure and by
every later one, and each nested error is populated only by its own code.

`compile_blueprint` owns no domain, resource, color, selection, history, or
region, and it is not a second runtime `State`. Nothing in `src/core/` or the
rest of `src/engine/` changed to make it possible.

The editor's public surface is the operations the UI callbacks call, and the
headless runner calls the same ones:

```cpp
enum class EditorPhase { choose_domain, choose_palette, build_blueprint };

bool choose_domain(content::GeometryDomain);
void return_to_domain_choice();
void set_row_included / set_row_unlimited / set_row_finite_amount / set_row_color;
bool build_palette();
void select_entry(std::size_t);
void cycle_entry(bool);  void cycle_orientation(bool);
bool accept_active_proposal();
bool remove_record(std::size_t);
void clear_blueprint();
void set_pointer(godot::Vector2);  void clear_pointer();
std::optional<std::size_t> record_at_local(godot::Vector2) const;
```

plus const observation: `phase`, `domain`, `catalog`, `domain_entries`,
`palette_rows`, `palette`, `entry_color`, `blueprint`, `arrangement`,
`selection`, `selected_variant`, `remaining_supply`, `proposals`,
`active_proposal`, `status_text`, the projection accessors, and the generated row
controls. No mutable internal was exposed for testing.

## the phase is derived, never stored

There is no phase field. The document owns a domain and rows, and optionally a
palette:

```text
no document                  choose_domain
document without a palette   choose_palette
document with a palette      build_blueprint
```

So a phase cannot disagree with the state it describes, and "the palette is
locked in blueprint phase" is enforced by the same optional that defines the
phase: every row mutator returns early when `document_->palette.has_value()`, and
`build_palette()` refuses to run twice.

Choosing a domain (including the same one again) and returning to domain choice
both construct a whole new document or none. There is no migration path, no
retained palette, no retained arrangement, and no confirmation dialog to bypass.

An invalid `GeometryDomain` — the act requires that one cannot be installed —
is rejected by an explicit two-value check before anything is built, so
`static_cast<GeometryDomain>(200)` installs nothing and leaves the editor in
domain phase. The runner asserts exactly that.

## records and arrangement are published together

`publish(candidate)` is the single mutation boundary for the blueprint. It
compiles the **complete** candidate record vector and assigns both the records
and the returned arrangement, or neither. Addition, removal, and clearing all
route through it, so there is one implementation of transactionality:

```text
records copy → append/drop → compile_blueprint(palette, candidate)
                                   │
                                   ├── failure: nothing is assigned, error reported
                                   └── success: records and arrangement assigned together
```

Record index and arrangement entry index agree, because compilation inserts the
records in stored order and every insertion succeeded. Removal and hit testing
rely on that correspondence and it is stated at both sites. Recompilation
reallocates `PlacementId`s from zero; nothing in the editor keys off them.

## interpretive decisions, recorded

Four, none of them contradictions in the act.

1. **The active proposal when no pointer has been seen.** The act defines the
   active proposal as the one whose handle is nearest the last local mouse
   position. Headlessly — and before the first motion event — there is no such
   position. Implemented: the first proposal in deterministic discovery order.
   It is a documented deterministic choice, not an invented coordinate, and it
   is what makes `accept_active_proposal()` exercisable without a pointer.

2. **Exhausted supply is refused before publication, not through it.** The act
   says an exhausted entry "cannot add another record", and separately says an
   unexpected typed compilation failure should be reported. Implemented:
   `accept_active_proposal()` checks the derived remaining supply first and
   answers with a concise status, so an ordinary exhausted click never reports
   an "unexpected" compiler failure. `compile_blueprint` remains the authority —
   it rejects the same record with `supply_exhausted` — and `publish` still
   reports any failure that does reach it. Proposals themselves are *not*
   supply-filtered: the act lists the enumeration steps and exactly one retention
   filter (positive-length contact), so an exhausted entry still ghosts, stays
   visible, and simply refuses to place.

3. **Proposal order is discovery order, not translation order.** The old
   construction fixture sorted its cache lexicographically by translation. The
   act says to collapse duplicates "retaining first discovery order" and that
   ties "retain deterministic proposal order", so the sort is gone. Enumeration
   order — every stored entry in storage order, all edge pairs, then all vertex
   pairs — is the whole ordering rule.

4. **The selected entry's row preview shows the selected variant.** Every other
   blueprint-phase row previews its identity's reference polygon, as the act's
   "palette reference previews" says. Showing the *selected* variant in the
   selected row is the one place that departs, because otherwise a rotation is
   invisible whenever no proposal is being ghosted. The status bar names the
   orientation numerically as well.

## what the editor draws, and what it never does

Everything drawn comes from an exact value: the arrangement's stored footprints
(filled from each polygon's own certified triangulation, so concave pentominoes
fill correctly without a second triangulator), the active proposal's exact
placement, and the palette's reference polygons in the row previews. The only
other marks are the two axis lines through exact origin and the canvas
background. There is no grid: a whole-unit grid is meaningful in the lattice
domain and misleading in hex-12, and the act forbids adding decoration.

The pointer does two things and nothing else:

- it ranks already-exact proposals by projected handle distance; and
- it hit-tests already-projected footprints to choose a record index.

The hit test is a plain ray cast in screen space over projected boundaries, so it
cannot produce a model coordinate even in principle; it walks storage order
backwards so the topmost rendered placement answers first.

Pan and zoom are the reused camera from the previous editor, unchanged apart from
its scale bounds. Nothing else survived from the region canvas.

## what the native tests assert

`tests/test_blueprint.cpp` (17 cases) covers every item the act lists: an empty
sequence compiling to an empty arrangement; representative lattice and hex-12
blueprints; stored order producing deterministic entries and monotonic ids; exact
ids, orientations, translations, and footprints surviving; the first unknown id
failing at its own record; a nonrepresentative equivalent label failing as
orientation-absent (the unit square's group records all four quarter turns as
equivalents and accepts only `0/1` as its representative); an unsupported
representative failing the same way; finite supply failing on the first excess
record and being counted per identity; placement overflow preserving
`PlacementError::footprint_overflow`; interior overlap preserving the exact
`ArrangementError` and the conflicting `PlacementId`; the complete precedence
chain when several defects coexist in one record; a failure returning no
arrangement; removal-then-recompilation producing exactly the remaining coverage;
lattice partial-edge contact still admitted; and a seven-placement hex-12 patch.

Two fixture notes:

- **Nothing is fabricated.** Every prototile comes from `Polygon::make` →
  `Prototile::make`, every oriented value from `compile_lattice_orientations` or
  `compile_hex12_orientations`, and every entry from `PaletteEntry::make` or
  `make_compiled`. There is no unchecked construction seam and none was added.
- **The hex-12 patch authors no translation.** A hexagon is inserted at exact
  origin and six triangles are grown onto its edges through ordinary
  `try_join_full_edges`; the resulting arrangement is then read back as records
  and recompiled, and the two arrangements are compared placement by placement.
  So the test proves the compiler reproduces exact join geometry rather than
  asserting hand-typed hex coordinates.

## what the headless runner proves

It loads the real `res://main.tscn` and drives it only through the public
operations above. Each act-mandated claim maps to at least one check: boot in
domain phase; an invalid domain rejected; 34 ordered lattice rows and 4 ordered
hex-12 rows (`35, 27, 36, 37`), each named and previewed from its own catalog
entry, with the visible child count matching the document; every discard path
(another domain, the same domain again, returning to domain choice) emptying
palette, blueprint, arrangement, proposals, and generated controls; an empty
palette refusing to lock; finite and unlimited supplies compiling; the locked
palette ignoring row edits and refusing a rebuild; exact-origin first placements
in both domains; entry and orientation cycling wrapping in both directions with
the orientation reset on entry change; deterministic, duplicate-free proposals
carrying the selected identity and representative orientation; removal, clearing,
and pointer selection.

Two checks are deliberately independent rather than reading the editor's own
helpers back:

- **positive-length contact and disjointness** are recomputed in the runner from
  `classify_segments` and `interiors_overlap` against every offered proposal; and
- **point-only filtering** is proven by re-enumerating every exact preview the
  editor considers, then showing that the dropped set is nonempty and that every
  dropped preview shares only isolated point contact.

Drawing is checked by its inputs — one projected vertex per exact vertex, a
certified `n - 2` triangulation per footprint, all finite — because the runner
quits inside `_ready` and no frame is ever drawn in a headless test scene. That
is the honest limit of what this runner establishes about rendering.

Absence is checked too: no `play_requested` signal, no save/open/play/region
callbacks bound, no save, play, restart-region, or add-hole toolbar button, and
no open, save, or discard dialog in the scene.

## manual acceptance: not performed

**No visual or manual acceptance is claimed.** The act's bounded smoke pass —
choose lattice and place and remove pieces, restart into hex-12 and grow a
visible patch, confirm orientation cycling and proposal selection are usable,
confirm restarting discards the document — is carson's to run. Everything above
is headless or native evidence; none of it establishes that the surface is
legible on screen.

Running it:

```text
./Godot.app/Contents/MacOS/Godot --path godot-project
```

Bindings: `lattice` / `hex12` / `choose domain` / `build palette` / `rotate` /
`clear blueprint` in the toolbar; left click places the ghost, right click
removes the placement under the pointer, `tab` cycles tiles, `r` rotates
(`shift` reverses both), middle-drag pans, wheel zooms.

## for the act 2-1 implementor

Five things.

1. **`BlueprintPlacement` is the contract, and it is already stable across
   recompilation.** It stores a representative `Orientation` (a canonical
   step/order rational) and an exact `Point`, never a palette index and never a
   `PlacementId`. The roadmap's intended resource encoding — id, rational turn,
   two raw q16.48 translations — is a field-for-field serialization of it.

2. **The editor's record vector is the blueprint.** `LevelEditor::blueprint()`
   returns it and `arrangement()` returns the proof compiled from it; they cannot
   disagree because they are only ever assigned together in `publish`. Persisting
   the records is sufficient; persisting the arrangement would add a second
   authority.

3. **`entry_color(i)` is where authored color lives.** The compiled `Palette`
   carries no color, so the editor keeps one `godot::Color` per locked palette
   entry in palette order. Whatever act 2-1 serializes as `PaletteEntryResource`
   colour should come from there, not from the palette rows, which are the
   pre-lock authoring state.

4. **An empty blueprint is valid here and must be rejected there.**
   `compile_blueprint` succeeds on an empty sequence by spec, and `clear
   blueprint` uses that. Region derivation is where emptiness becomes an error.

5. **A disconnected blueprint is reachable.** Removing a bridge placement is
   permitted and leaves valid act-2 state; the contact filter only constrains
   what is *offered*, not what can survive a removal. Act 2-1's "more than one
   positive connected component" rejection is therefore load-bearing, not
   theoretical.

## not done (correctly out of scope)

No region derivation or rendering; no level save, load, or play request; no
resource-schema change and no serialized domain or blueprint; no runtime
`State`; no fake region; no hex-specific placement, alignment, overlap, joining,
or rendering; no undo or redo; no live palette edits; no migration of old
documents; no player; no third geometry domain; and no visual polish beyond what
makes the three phases legible.
